// Copyright (c) 2009-2023 Sundog Software LLC, All rights reserved worldwide.

#include "Rain.h"
#include "ResourceLoader.h"
#include "VertexBuffer.h"
#include "Renderer.h"
#include "Matrix4.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "Utils.h"
#include "PrecipitationManager.h"
#include "Camera.h"
#include "ScopedConstantPrep.h"
#include "PrecipitationParticleStructure.h"
#include "PrecipitationShaders.h"
#include "TextureManager.h"
#include "SLAssert.h"

#include <sstream>

//#define CUBE_VOLUME

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace SilverLining;
using namespace std;

#define NUM_RADII 65536
static Vector3f randomOffsets[NUM_RADII];

Rain::Rain(const Atmosphere* _atmosphere, const Vector3& camPos, ThreadCameraStreamData* _tcsData)
    : Precipitation(_atmosphere, _tcsData)
    , texturesBuffer(0)
    , particlesBuffer(0)
{
    lastComputedIntensity = lastComputedFogDensity = -1;
    maxParticles = 100000;
    maxIntensity = 30.0;
    rainAlphaThreshold = 0.01f;
    rainWidthMultiplier = 1.0f;
    maxRenderedParticles = 3000;
    visibilityMultiplier = 1.0;
    visibility = 0;
    fogDensity = 0;
    rainConstantAlpha = 0;
    rainStreakFrameTime = 0;
    rainStreakCameraCoords = false;
    minParticlePixels = 5.0;
    rainVelocityFactor = 1.0;
    cameraVelocityBufferIndex = 0;
    cameraVelocityBufferFilled = false;

    Configuration::GetBoolValue("rain-streak-camera-coords", rainStreakCameraCoords);
    Configuration::GetIntValue("rain-max-particles", maxParticles);
    Configuration::GetIntValue("rain-max-rendered-particles", maxRenderedParticles);
    Configuration::GetDoubleValue("rain-max-intensity", maxIntensity);
    Configuration::GetFloatValue("rain-alpha-threshold", rainAlphaThreshold);
    Configuration::GetFloatValue("rain-streak-width-multiplier", rainWidthMultiplier);
    Configuration::GetDoubleValue("rain-visibility-multiplier", visibilityMultiplier);
    Configuration::GetDoubleValue("rain-near-clip", nearClip);
    nearClip *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("rain-constant-alpha", rainConstantAlpha);
    Configuration::GetDoubleValue("rain-streak-frame-time", rainStreakFrameTime);
    Configuration::GetDoubleValue("rain-volume-radius", farClip);
    farClip *= (float)Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("rain-minimum-pixels", minParticlePixels);
    Configuration::GetBoolValue("rain-use-depth-buffer", useDepthBuffer);
    Configuration::GetBoolValue("rain-write-depth", writeDepth);
    Configuration::GetDoubleValue("rain-velocity-factor", rainVelocityFactor);

    fastPathRendering = renderer->SupportsInstancedRendering()
                        && tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRain();

    tcsData->SetForceImmediate(true);

    InitializeBuffers();
    InitializePositions(camPos);
    LoadStreakTextures();
    if (fastPathRendering) {
        InitializeTexturesBuffer();
        InitializeParticlesBuffer();
    }
    LoadStreakColors();
    ComputeOscillationTimes();

    // Initialize once
    ShaderHandle precipShader = (fastPathRendering) ? tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRain() :
                                tcsData->GetPrecipitationShaders()->GetShaderProgram();
    static_cast<void*>(precipShader);

    tcsData->SetForceImmediate(false);
}

void RainParticle::InitializeParticlePosition(const Vector3& camPos, int idxOffset, const RandomNumberGenerator* rng)
{
    int index = rng->UniformRandomIntRange(0, NUM_RADII - 1);
    index += idxOffset;
    index %= NUM_RADII;
    const Vector3f& offset = randomOffsets[index];

    pos.x = camPos.x + offset.x;
    pos.y = camPos.y + offset.y;
    pos.z = camPos.z + offset.z;
}

void Rain::ComputeOscillationTimes()
{
    const double kSurfaceTension = 0.0728; // N/m2
    const double kDensityWater = 1000.0; // kg/m3
    for (int i = 0; i < NUM_RAINDROP_DIAMETERS; i++) {
        double D = MIN_RAINDROP_DIAMETER + i * RAINDROP_DIAMETER_INCREMENT;
        D *= 0.001; // convert from mm to m

        // Oscillation frequency per [Garg Nayar 2006]
        double w2 = MathUtils::TwoPi() * sqrt((8.0 * kSurfaceTension) / (4.0 * MathUtils::Pi() * MathUtils::Pi() * kDensityWater * D * D * D));
        oscillationTimes[i] = MathUtils::TwoPi() / w2;
    }
}

void Rain::InitializePositions(const Vector3& camPos)
{
    float range = (float)(farClip - nearClip);

    const RandomNumberGenerator *rng = atmosphere->GetRandomNumberGenerator();

    for (int i = 0; i < NUM_RADII; i++) {
#ifdef CUBE_VOLUME
        float x = 0, y = 0, z = 0;
        while ( x <= nearClip && x >= -nearClip && y <= nearClip && y >= -nearClip && z <= nearClip && z >= -nearClip ) {
            x = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
            y = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
            z = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
        }
        randomOffsets[i] = Vector3f(x, y, z);
#else

        float x = 0, y = 0, z = 0;
        float r2 = 0;
        do {
            x = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
            y = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
            z = rng->UniformRandomFloat() * (float)farClip * 2.0f - (float)farClip;
            r2 = x*x + y*y + z*z;
        } while (r2 < (nearClip * nearClip) || r2 > (farClip * farClip));

        randomOffsets[i] = Vector3f(x, y, z);
#endif
    }

    for (SL_VECTOR(RainParticle) ::iterator it = particles.begin(); it != particles.end(); it++) {
        (*it).InitializeParticlePosition(camPos, 0, rng);
    }
}

void Rain::SetEffectRange(double fNear, double fFar, bool bUseDepthBuffer, const Camera* camera)
{
    if (fNear > 0 && fFar > 0 && fFar > fNear) {
        if (fNear != nearClip || fFar != farClip || bUseDepthBuffer != useDepthBuffer) {
            Precipitation::SetEffectRange(fNear, fFar, bUseDepthBuffer, camera);
            InitializePositions(camera->GetPosition());
        }
    }
}

void Rain::InitializeBuffers()
{
    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = 0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = true;

    name = (enableObjectLabeling)? "Rain::VertexBuffer" : (const char *)0;
    vertexBuffer = SL_NEW VertexBuffer(4, name, tcsData, bufferProperties);

    name = (enableObjectLabeling)? "Rain::IndexBuffer" : (const char *)0;
    indexBuffer = SL_NEW IndexBuffer(4, name, tcsData, bufferProperties);

    if (vertexBuffer && indexBuffer) {
        if (vertexBuffer->LockBuffer()) {
            if (indexBuffer->LockBuffer()) {
                Vertex *v = vertexBuffer->GetVertices();
                Index *idx = indexBuffer->GetIndices();

                if (v && idx) {
                    particles.reserve(maxParticles);
                    for (int i = 0; i < maxParticles; i++) {
                        particles.push_back(RainParticle());
                    }

                    Color startColor(1.0f, 1.0f, 1.0f, 1.0f);
                    Color endColor(1.0f, 1.0f, 1.0f, 1.0f);

                    v[0].SetColor(startColor);
                    v[0].SetUV(0, 0);
                    v[0].x = -0.5;
                    v[0].y = 0.5;
                    v[0].z = 0;
                    v[0].w = 1.0f;
                    idx[0] = 0;

                    v[1].SetColor(startColor);
                    v[1].SetUV(1, 0);
                    v[1].x = 0.5;
                    v[1].y = 0.5;
                    v[1].z = 0;
                    v[1].w = 1.0f;
                    idx[1] = 1;

                    v[2].SetColor(endColor);
                    v[2].SetUV(0, 1);
                    v[2].x = -0.5;
                    v[2].y = -0.5;
                    v[2].z = 0;
                    v[2].w = 1.0f;
                    idx[2] = 2;

                    v[3].SetColor(endColor);
                    v[3].SetUV(1, 1);
                    v[3].x = 0.5;
                    v[3].y = -0.5;
                    v[3].z = 0;
                    v[3].w = 1.0f;
                    idx[3] = 3;
                }
                indexBuffer->UnlockBuffer();
            }
            vertexBuffer->UnlockBuffer();
        }
    }
}

static int textureIndex(int i, int j)
{
    return i*NUM_RAIN_TEXTURE_OSCILLATIONS + j;
}

static int colorIndex(int i, int j)
{
    return i*NUM_RAIN_TEXTURE_OSCILLATIONS + j;
}
static int numRainTextures(void)
{
    return NUM_RAIN_TEXTURE_ANGLES * NUM_RAIN_TEXTURE_OSCILLATIONS;
}

void Rain::LoadStreakTextures()
{

    TextureManager* textureManager = (atmosphere->TexturesAreShared()) ? atmosphere->GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();

    for (int i = 0; i < NUM_RAIN_TEXTURE_ANGLES; i++) {
        double camAngle = (double)i * (double)RAIN_TEXTURE_ANGLE_INCREMENT;
        camAngle = 90.0 - camAngle;
        camAngle = MathUtils::ToRadians(camAngle);
        angleThresholds[i] = cos(camAngle);

        for (int j = 0; j < NUM_RAIN_TEXTURE_OSCILLATIONS; j++) {
            rainTextures[textureIndex(i, j)] = textureManager->GetOrCreateRainTexture(i, j);
        }
    }
}

void Rain::InitializeTexturesBuffer()
{
    if (renderer->GetIsVulkan()) {
        ShaderHandle precipShader = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRain();

        for (int i = 0; i < NUM_RAIN_TEXTURE_ANGLES; i++) {
            for (int j = 0; j < NUM_RAIN_TEXTURE_OSCILLATIONS; j++) {
                int rainTextureIndex = textureIndex(i, j);
                renderer->EnableTexture(rainTextures[rainTextureIndex], rainTextureIndex);
            }
        }
        renderer->BindShader(precipShader);
        renderer->UnbindShader();

        for (int i = 0; i < NUM_RAIN_TEXTURE_ANGLES; i++) {
            for (int j = 0; j < NUM_RAIN_TEXTURE_OSCILLATIONS; j++) {
                int rainTextureIndex = textureIndex(i, j);
                renderer->DisableTexture(rainTextureIndex);
            }
        }
    } else {
        const int texturesBufferSize = numRainTextures() * sizeof(GPUAddressType);

        BufferProperties bufferProperties;
        bufferProperties.useStagingBuffer = false;
        bufferProperties.useMapUnmap = true;
        texturesBuffer = renderer->CreateBuffer("textures_buffer", UNIFORM_BUFFER, texturesBufferSize, bufferProperties);

        void* context = renderer->GetContext();

        GPUAddressType* data = static_cast<GPUAddressType*>(texturesBuffer->Lock(0, texturesBufferSize, context));
        for (int i = 0; i < numRainTextures(); ++i) {
            GPUAddressType address = renderer->GetTextureAddress(rainTextures[i]);
            renderer->MakeTextureAddressResident(address);
            data[i] = address;
        }

        texturesBuffer->Unlock(0, texturesBufferSize, context);
    }
}

void Rain::InitializeParticlesBuffer()
{
    const int particleSize = sizeof(Particle);

    bool precip_particles_use_staging_buffer = true;
    Configuration::GetBoolValue("precip-particles-use-staging-buffer", precip_particles_use_staging_buffer);

    BufferProperties bufferProperties;
    bufferProperties.useStagingBuffer = precip_particles_use_staging_buffer;
    bufferProperties.useMapUnmap = true;
    bufferProperties.frameIndexed = true; // for vulkan

    if (tcsData->GetRenderer()->GetIsOpenGL()) {
        particlesBuffer = renderer->CreateBuffer("particles_buffer", SHADER_STORAGE_BUFFER, particleSize * maxRenderedParticles, bufferProperties);
    } else if (tcsData->GetRenderer()->GetIsVulkan()) {
        particlesBuffer = renderer->CreateBuffer("particles_buffer", SHADER_STORAGE_BUFFER, particleSize * maxRenderedParticles * NUM_RAINDROP_DIAMETERS, bufferProperties);
    }
}

void Rain::LoadStreakColors()
{
    char *data;
    unsigned int dataLen;
    if (Atmosphere::GetResourceLoader()->LoadResource("rain/normalized_env_max.txt", data, dataLen, true)) {
        std::stringstream *s = SL_NEW std::stringstream(string(data));
        for (int i = 0; i < NUM_RAIN_TEXTURE_ANGLES; i++) {
            char buf[1024];
            s->getline(buf, 1024);

            int osc = 0;
            char *pch = strtok(buf, " \t\n");
            while (pch != NULL) {
                double c = LocaleSafeAtoF(pch);
                if (osc < NUM_RAIN_TEXTURE_OSCILLATIONS) {
                    rainColors[colorIndex(i, osc++)] = Color(c, c, c, 1.0);
                }
                pch = strtok(NULL, " \t\r\n");
            }
        }

        Atmosphere::GetResourceLoader()->FreeResource(data);
        SL_DELETE s;
    }
}

Rain::~Rain()
{
    if (indexBuffer) SL_DELETE indexBuffer;
    if (vertexBuffer) SL_DELETE vertexBuffer;

    if (texturesBuffer) renderer->DestroyBuffer(texturesBuffer);
    if (particlesBuffer) renderer->DestroyBuffer(particlesBuffer);
}

void Rain::SetIntensity(double R)
{
    // Source: Marshall, J.S., and W. McK. Palmer, 1948, "The Distribution of Raindrops With Size",
    // Journal of Meteorology, 5, 165-166.

    // Their work has really stood the test of time...

    Configuration::GetDoubleValue("rain-velocity-factor", rainVelocityFactor);

    if (R > maxIntensity) R = maxIntensity;
    if (R < 0) R = 0;

    intensity = R;

    if (intensity == 0) {
        fogDensity = 0;
        return;
    }

    if (R == lastComputedIntensity) {
        fogDensity = lastComputedFogDensity;
        return;
    }

    const double N0 = 8000.0;
    double A = 41.0 * pow(R, -0.21);
    double Nd[NUM_RAINDROP_DIAMETERS];
    double NdTotal = 0;
    int i;

    for (i = 0; i < NUM_RAINDROP_DIAMETERS; i++) {
        double D = MIN_RAINDROP_DIAMETER + (double)i * RAINDROP_DIAMETER_INCREMENT;
        Nd[i] = N0 * exp(-A * D * 0.001);
        NdTotal += Nd[i];
        terminalVelocities[i] = pow( 3.778 * D, 0.67) * Atmosphere::GetUnitScale() * rainVelocityFactor;
        dropDiameters[i] = D * Atmosphere::GetUnitScale();
    }

    // Normalize and multiply by available particles.
    double totalParticles = (R / maxIntensity) * (double)maxParticles;
    for (i = 0; i < NUM_RAINDROP_DIAMETERS; i++) {
        double n = (Nd[i] / NdTotal) * totalParticles;
        nDrops[i] = (int)n;
    }

    // Now estimate visibility per Atlas, David, 1953, "Optical Extinction by Rainfall", Journal of Meterology Vol. 10
    // 486-488. Another oldie but goodie
    visibility = 11.6 * pow(R, -0.63); // This assumes "Bergeron type" rainfall. Orographic has a different equation
    visibility *= visibilityMultiplier;
    visibility *= Atmosphere::GetUnitScale();

    // From Koschmeider equation
    fogDensity = (3.91 / (visibility * 1000.0)) / 1.3;

    lastComputedIntensity = intensity;
    lastComputedFogDensity = fogDensity;
}

void Rain::GetRainTextureAndColor(Color& outRainColor, int& currentRainTextureIndex, bool& rainTextureSet, int& oscCount, int& oscNum, int dropsPerOscillation
                                  , const Color& lightColor, int angleIndex, float rainAlpha, ShaderHandle precipShader, Color& rainColor) const
{
    // Cycle through the available images for different types of drop oscillation patterns.
    if (oscCount > dropsPerOscillation) {
        oscNum++;
        if (oscNum < NUM_RAIN_TEXTURE_OSCILLATIONS) {
            rainTextureSet = true;
            rainColor = Color(lightColor * rainColors[colorIndex(angleIndex, oscNum)]);
            rainColor.a = (float)rainAlpha;
        }
        oscCount = 0;
    }

    oscCount++;

    outRainColor = rainColor;
    currentRainTextureIndex = textureIndex(angleIndex, oscNum);
}

bool Rain::IsCulled(const Vector3& particlePosition, const Frustum& adjustedFrustum, const double halfStreak) const
{
    // You might think doing a cull test against every single particle
    // would kill performance, but not doing this kills it a LOT more.
    bool cull = false;
    for (int i = 0; i < 5; i++) { // we purposely ignore the far clip plane
        const Plane& p = adjustedFrustum.GetPlane(i);
        const Vector3& N = p.GetNormal();
        double origin = N.Dot(particlePosition) + p.GetDistance();
        if (origin < -halfStreak) { // The "effective radius" is the streak length / 2.
            cull = true;
            break;
        }
    }
    return cull;
}

void Rain::DrawParticles(int particleIdx
                         , const int dropIndex
                         , const int angleIndex
                         , const Color& lightColor
                         , const float rainAlpha
                         , const double halfStreak
                         , const Matrix4& _rotateTranslate
                         , const Matrix4& scale
                         , const Frustum& adjustedFrustum
                         , const Matrix4& proj
                         , const Camera* camera
                         , ShaderHandle precipShader
                         , Color& rainColor) const
{
    const int dropsPerOscillation = (int)((float)nDrops[dropIndex] / (float)NUM_RAIN_TEXTURE_OSCILLATIONS);
    int oscCount = 0;
    int oscNum = 0;
    renderer->EnableTexture(rainTextures[textureIndex(angleIndex, oscNum)], 0);

    Color currentRainColor;
    int currentRainTextureIndex;

    int particleIndex = 0;
    int numStreaksRendered = 0;

    Matrix4 rotateTranslate(_rotateTranslate);

    // Now, draw each particle.
    for (int currentParticleIndex = particleIdx; currentParticleIndex < particleIdx + nDrops[dropIndex]; currentParticleIndex++) {
        if (numStreaksRendered >= maxRenderedParticles)
            break;

        bool rainTextureSet = false;
        GetRainTextureAndColor(currentRainColor, currentRainTextureIndex, rainTextureSet, oscCount, oscNum, dropsPerOscillation, lightColor, angleIndex, rainAlpha, precipShader, rainColor);

        const Vector3& particlePosition = particles[currentParticleIndex].pos;

        if (IsCulled(particlePosition, adjustedFrustum, halfStreak)) {
            continue;
        }

        if (rainTextureSet) {
            renderer->EnableTexture(rainTextures[currentRainTextureIndex], 0);
        }

        if (precipShader) {
            renderer->SetConstantVector4(precipShader, "sl_lightingColor", Vector4(currentRainColor.r, currentRainColor.g, currentRainColor.b, currentRainColor.a));
        } else {
            renderer->SetCurrentColor(rainColor);
        }

        rotateTranslate.elem[0][3] = particlePosition.x;
        rotateTranslate.elem[1][3] = particlePosition.y;
        rotateTranslate.elem[2][3] = particlePosition.z;

        if (precipShader) {
            Matrix4 mvp = proj * (camera->GetModelViewMatrix() * rotateTranslate * scale);
            renderer->SetConstantMatrix(precipShader, "sl_modelViewProj", mvp);
        } else {
            renderer->SetModelviewMatrix(camera->GetModelViewMatrix() * rotateTranslate * scale);
        }

        renderer->DrawStripDirect(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(),
                                  vertexBuffer->GetNumVertices());

        ++numStreaksRendered;
        ++particleIndex;
    }
}

void Rain::DrawParticlesInstanced(int particleIdx
                                  , const int dropIndex
                                  , const int angleIndex
                                  , const Color& lightColor
                                  , const float rainAlpha
                                  , const double halfStreak
                                  , const Matrix4& rotateTranslate
                                  , const Matrix4& scale
                                  , const Frustum& adjustedFrustum
                                  , const Matrix4& proj
                                  , const Camera* camera
                                  , ShaderHandle precipShader
                                  , Color& rainColor) const
{
    int numStreaksRendered = 0;

    const int dropsPerOscillation = (int)((float)nDrops[dropIndex] / (float)NUM_RAIN_TEXTURE_OSCILLATIONS);
    int oscCount = 0;
    int oscNum = 0;

    Color currentRainColor;
    int currentRainTextureIndex;

    Matrix4 modelViewMatrix = camera->GetModelViewMatrix();
    modelViewMatrix.SetTranslation(Vector3(0, 0, 0));

    if (renderer->GetIsOpenGL()|| renderer->GetIsVulkan()) {
        const PrecipitationShaderConstantLocations& locations = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRainLocations();
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_proj, proj);
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_cameraModelView, modelViewMatrix);
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_rotateTranslate, rotateTranslate);
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_scale, scale);
    } else {
        renderer->SetConstantMatrix(precipShader, "sl_proj", proj);
        renderer->SetConstantMatrix(precipShader, "sl_cameraModelView", modelViewMatrix);
        renderer->SetConstantMatrix(precipShader, "sl_rotateTranslate", rotateTranslate);
        renderer->SetConstantMatrix(precipShader, "sl_scale", scale);
    }

    int particleIndex = 0;
    void* context = renderer->GetContext();
    Particle* pParticles = static_cast<Particle*>(particlesBuffer->Lock(0, particlesBuffer->Size(), context));

    if (renderer->GetIsVulkan()) {
        pParticles += maxRenderedParticles * dropIndex;
    }

    bool rainTextureSet = false;

    const Vector3 cameraPosition = camera->GetPosition();
    // Now, draw each particle.
    for (int currentParticleIndex = particleIdx; currentParticleIndex < particleIdx + nDrops[dropIndex]; currentParticleIndex++) {
        if (numStreaksRendered >= maxRenderedParticles)
            break;

        GetRainTextureAndColor(currentRainColor, currentRainTextureIndex, rainTextureSet, oscCount, oscNum, dropsPerOscillation, lightColor, angleIndex, rainAlpha, precipShader, rainColor);

        const Vector3& particlePosition = particles[currentParticleIndex].pos;

        pParticles[particleIndex].sl_particlePosition.x = (float)(particlePosition.x-cameraPosition.x);
        pParticles[particleIndex].sl_particlePosition.y = (float)(particlePosition.y-cameraPosition.y);
        pParticles[particleIndex].sl_particlePosition.z = (float)(particlePosition.z-cameraPosition.z);

        pParticles[particleIndex].sl_lightingColor.x = currentRainColor.r;
        pParticles[particleIndex].sl_lightingColor.y = currentRainColor.g;
        pParticles[particleIndex].sl_lightingColor.z = currentRainColor.b;
        pParticles[particleIndex].sl_lightingColor.w = currentRainColor.a;

        pParticles[particleIndex].sl_textureIndex = currentRainTextureIndex;

        ++numStreaksRendered;
        ++particleIndex;
    }
    particlesBuffer->Unlock(0, particlesBuffer->Size(), context);

    if (numStreaksRendered == 0) {
        return;
    }

    if (texturesBuffer) {
        texturesBuffer->Bind(2, context);
    }

    if (particlesBuffer) {
        if (renderer->GetIsOpenGL()) {
            particlesBuffer->Bind(1, context);
        } else if (renderer->GetIsVulkan()) {
            particlesBuffer->Bind(2, context);
        }
    }

    if (renderer->GetIsOpenGL()) {
        renderer->DrawStripDirectInstanced(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(),
                                           vertexBuffer->GetNumVertices(), numStreaksRendered, 0);
    } else if (renderer->GetIsVulkan()) {
        renderer->DrawStripDirectInstanced(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(),
                                           vertexBuffer->GetNumVertices(), numStreaksRendered, dropIndex * maxRenderedParticles);
    }

    if (particlesBuffer) {
        if (renderer->GetIsOpenGL()) {
            particlesBuffer->UnBind(1, context);
        } else if (renderer->GetIsVulkan()) {
            particlesBuffer->UnBind(2, context);
        }
    }

    if (texturesBuffer) {
        texturesBuffer->UnBind(2, context);
    }
}

void Rain::Render(double dt, const Color& lightColor, const Camera* camera)
{
    if (intensity <= 0) return;

    // Clamp dt to something reasonable
    double clampedDt = GetClampedDt(dt);

    ShaderHandle precipShader = (fastPathRendering) ? tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRain() :
                                tcsData->GetPrecipitationShaders()->GetShaderProgram();

    float nearDepth, farDepth;
    renderer->GetDepthRange(nearDepth, farDepth);
    //renderer->SetDepthRange(0.0f, 1.0f);

    Matrix4 savedTexture;
    // Assume texture matrix is identity, to prevent stalling to get whatever it really is.
    //ren->GetTextureMatrix(&savedTexture);

    //Set and later restore near clip - need to see streaks very close to the camera.
    //We disable use of the depth buffer so this has no ill effects.


    // Remove skew
    bool noSkew = true;
    Configuration::GetBoolValue("precipitation-no-skew", noSkew);
    Camera adjustedCam(renderer->GetIsRightHanded());
    adjustedCam.SetUpVector(camera->GetUpVector());
    adjustedCam.SetRightVector(camera->GetRightVector());
    adjustedCam.SetModelViewMatrix(camera->GetModelViewMatrix());
    adjustedCam.SetProjectionMatrix(camera->GetProjectionMatrix());

    if (!useDepthBuffer) {
        adjustedCam.AdjustNearFarClip(nearClip, farClip, noSkew, nearDepth, farDepth, renderer);
    }
    Frustum adjustedFrustum = adjustedCam.GetFrustumWorldSpace();

    Matrix4 proj = adjustedCam.GetProjectionMatrix();
    Matrix4 invProj = proj.Inverse();

    Matrix4 projNoSkew, invProjNoSkew;
    if (!useDepthBuffer) {
        projNoSkew = adjustedCam.GetProjectionNoSkew(nearClip, farClip, nearDepth, farDepth, renderer);
    } else {
        double znear, zfar;
        camera->GetNearFarClip(znear, zfar);
        projNoSkew = adjustedCam.GetProjectionNoSkew(znear, zfar, nearDepth, farDepth, renderer);
    }
    invProjNoSkew = projNoSkew.Inverse();

    int particleIdx = 0;

    // Set up the proper state.
    renderer->EnableBackfaceCulling(false);
    renderer->EnableTexture2D(true);
    renderer->EnableLighting(false);
#ifdef ANDROID
    renderer->EnableBlending(SRCALPHA, ONE);
#else
    renderer->EnableBlending(SRCALPHA, INVSRCALPHA);
#endif
    renderer->EnableDepthReads(useDepthBuffer);
    renderer->EnableDepthWrites(writeDepth);

    Vector3 currentCamPos = camera->GetPosition();

    double velocity, heading;
    atmosphere->GetConditions().GetWind(velocity, heading, atmosphere->GetConditions().GetLocation(tcsData).GetAltitude());
    double windX, windZ;
    atmosphere->GetConditions().GetPrecipitationWind(windX, windZ);
    double dx = windX + sin(MathUtils::ToRadians(-heading)) * velocity;
    double dz = windZ + cos(MathUtils::ToRadians(-heading)) * velocity;
    Vector3 wind(Vector3(dx, 0, dz) * camera->GetBasis3x3() * 0.2 * clampedDt);
    Vector3 streakWind;
    if (rainStreakFrameTime > 0) {
        streakWind = Vector3(Vector3(dx, 0, dz) * camera->GetBasis3x3() * 0.2 * rainStreakFrameTime);
    } else {
        streakWind = wind;
    }

    // In real life, rain streaks aren't that easy to see. If you want to fudge
    // how prominent the rain streaks are, we let you do that...
    double rainBrightness = 1.0;
    Configuration::GetDoubleValue("rain-streak-brightness", rainBrightness);
    Configuration::GetBoolValue("rain-streak-camera-coords", rainStreakCameraCoords);
    Configuration::GetFloatValue("rain-streak-width-multiplier", rainWidthMultiplier);
    Configuration::GetDoubleValue("rain-constant-alpha", rainConstantAlpha);

    // Vector pointing away from the eyepoint into the screen
    bool useNDC = true;
    Configuration::GetBoolValue("billboard-use-ndc", useNDC);
    Vector3 camDir;
    Matrix4 view = camera->GetModelViewMatrix();
    if (useNDC) {
        Vector3 ndcIn(0, 0, 1);
        // de-project to world space
        view.elem[0][3] = view.elem[1][3] = view.elem[2][3] = 0;
        Matrix4 mvp = camera->GetProjectionMatrix() * view;
        Matrix4 invMvp = mvp.Inverse();
        camDir = invMvp * ndcIn;
        camDir.Normalize();
    } else {
        camDir = Vector3(-view.elem[2][0], -view.elem[2][1], -view.elem[2][2]);
    }

    // Compute vector of raindrop's movement relative to the camera since the last frame
    if (dt > 0) {
        cameraVelocities[cameraVelocityBufferIndex] = (previousCamPos - currentCamPos) * (1.0/dt);
        previousCamPos = currentCamPos;
        cameraVelocityBufferIndex++;
        if (cameraVelocityBufferIndex == CAMERA_VELOCITY_BUFFER_SIZE) {
            cameraVelocityBufferIndex = 0;
            cameraVelocityBufferFilled = true;
        }

        if (!cameraVelocityBufferFilled) {
            cameraMotion = previousCamPos - currentCamPos;
        } else {
            Vector3 tot;
            for (int i = 0; i < CAMERA_VELOCITY_BUFFER_SIZE; i++) {
                tot = tot + cameraVelocities[i];
            }
            cameraMotion = (tot * (1.0 / (double)(CAMERA_VELOCITY_BUFFER_SIZE))) * (rainStreakFrameTime > 0 ? rainStreakFrameTime : clampedDt);
        }
    }

    for (int i = 0; i < NUM_RAINDROP_DIAMETERS; i++) {
        // Compute terminal velocity of raindrop based on its diameter from Atlas and Ulbrich (1977)
        double v = terminalVelocities[i];
        double dropDiameter = dropDiameters[i];

        // Compute texture scale for v coord
        double vScale = clampedDt / oscillationTimes[i];
        Matrix4 textureMatrix;
        textureMatrix.elem[1][1] = vScale;
        renderer->SetTextureMatrix(textureMatrix);

        double dy = -v * clampedDt;

        Vector3 down = Vector3(0, dy, 0) * camera->GetBasis3x3();
        Vector3 streakDown;
        if (rainStreakFrameTime > 0) {
            streakDown = Vector3(0, -v * rainStreakFrameTime, 0) * camera->GetBasis3x3();
        } else {
            streakDown = down;
        }

        Vector3 motion = down + wind;
        Vector3 streakMotion = streakDown + streakWind;

        Vector3 dp = cameraMotion + motion;
        Vector3 streakDp = cameraMotion + streakMotion;
        if (!rainStreakCameraCoords) {
            streakDp = streakMotion;
        }

        Matrix4 rotateTranslate;

        double streakLength = streakDp.Length();
        int angleIndex = 0;
        double rainAlpha = 0.0;

        if (dp.SquaredLength() > 0) {
            // Select the proper camera angle texture index
            // Streak textures based on Garg, K., and Nayark, S.K, "Photorealistic Rendering of Rain Streaks",
            // Proceedings of ACM SIGGRAPH 2006, 996-1002

            Vector3 rainDir = down;
            rainDir.Normalize();
            camDir.Normalize();
            double cosT = MathUtils::Abs(camDir.Dot(rainDir));

            for (int k = 0; k < NUM_RAIN_TEXTURE_ANGLES; k++) {
                if (cosT > angleThresholds[k]) angleIndex = k;
            }

            // The streak textures are scaled to use the available dynamic range.
            // rainColors[] maps these back to their real intensities. We modulate
            // by the available light, and then compute alpha based on Garg, K., and
            // Nayar, S.K. 2005. When Does a Camera See Rain? ICCV, 1067-1074

            Color rainColor = lightColor * rainColors[colorIndex(angleIndex, 0)];

            if (rainConstantAlpha > 0) {
                rainAlpha = rainConstantAlpha;
            } else {
                rainAlpha = ((2.0 * dropDiameter * 0.001) / (streakDown.Length())) * rainBrightness;
            }

            rainColor.a = (float)rainAlpha;

            renderer->SetCurrentColor(rainColor);

            // Compute basis of motion vector, make a rotation matrix to properly orient
            // the streaks as a billboard using the motion vector as a fixed axis.
            Vector3 up = dp * -1.0;
            if (!rainStreakCameraCoords) {
                up = motion * -1.0;
            }

            up.Normalize();

            Vector3 in = camDir * -1;
            Vector3 right = up.Cross(in);
            right.Normalize();

            // Handle case where up == in
            if (right.SquaredLength() == 0) right = Vector3(1,0,0);

            in = right.Cross(up);
            in.Normalize();

            rotateTranslate.elem[0][0] = right.x;
            rotateTranslate.elem[0][1] = right.y;
            rotateTranslate.elem[0][2] = right.z;
            rotateTranslate.elem[1][0] = up.x;
            rotateTranslate.elem[1][1] = up.y;
            rotateTranslate.elem[1][2] = up.z;
            rotateTranslate.elem[2][0] = in.x;
            rotateTranslate.elem[2][1] = in.y;
            rotateTranslate.elem[2][2] = in.z;

            rotateTranslate.Transpose();
        }

        if (vertexBuffer && indexBuffer && rainAlpha > rainAlphaThreshold) {
            // Do physics.
            UpdateParticles(motion, dp, particleIdx, particleIdx + nDrops[i], camera);

            // Make streaks of proper width and length using a scaling matrix
            Vector3 vScale(dropDiameter * 0.001 * rainWidthMultiplier, streakLength,
                           dropDiameter * 0.001 * rainWidthMultiplier);

            Vector4 worldMinSize(0,0,0,0);

            if (minParticlePixels > 0) {
                // Enforce minimum screen size at the near clip plane
                // Compute desired minimum size in NDC
                int x, y, w, h;
                renderer->GetViewport(x, y, w, h);
                double nearClipNDC = nearDepth;// renderer->GetIsDirectX() ? 0.0 : -1.0;
                Vector4 minSize = Vector4(minParticlePixels / ((double)w * 0.5), minParticlePixels / ((double)h * 0.5),
                                          nearClipNDC, 1.0);
                // Deproject to world size
                worldMinSize = invProjNoSkew * minSize;
            }

            Matrix4 scale;
            scale.elem[0][0] = vScale.x > worldMinSize.x ? vScale.x : worldMinSize.x;
            scale.elem[1][1] = vScale.y > worldMinSize.y ? vScale.y : worldMinSize.y;
            scale.elem[2][2] = 1.0;

            double halfStreak = -dy * 0.5;

            renderer->EnableTexture(rainTextures[textureIndex(angleIndex, 0)], 0);

            renderer->IncrementConstantBuffersOffset(precipShader);

            if (precipShader) {
                renderer->BindShader(precipShader);
                ScopedConstantPrep prep(renderer, precipShader, false);
                if (fastPathRendering && (renderer->GetIsOpenGL()||renderer->GetIsVulkan())) {
                    const PrecipitationShaderConstantLocations& locations = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedRainLocations();
                    renderer->SetConstantVectorAtLocation(precipShader, locations.sl_outputScale, atmosphere->GetOutputScale());
                } else {
                    renderer->SetConstantVector(precipShader, "sl_outputScale", atmosphere->GetOutputScale());
                    renderer->SetConstantVector4(precipShader, "sl_lightingColor", Vector4(rainColor.r, rainColor.g, rainColor.b, rainColor.a));
                }
            }

            // Make the vertex and index buffer current.
            renderer->SetVertexBuffer(vertexBuffer->GetHandle(), false);
            renderer->SetIndexBuffer(indexBuffer->GetHandle());

            if (fastPathRendering) {
                DrawParticlesInstanced(particleIdx, i, angleIndex
                                       , lightColor, (float)rainAlpha, halfStreak
                                       , rotateTranslate, scale, adjustedFrustum, proj, camera
                                       , precipShader, rainColor);
            } else {
                DrawParticles(particleIdx, i, angleIndex
                              , lightColor, (float)rainAlpha, halfStreak
                              , rotateTranslate, scale, adjustedFrustum, proj, camera
                              , precipShader, rainColor);
            }

            renderer->UnsetIndexBuffer();
            renderer->UnsetVertexBuffer();
            renderer->UnbindShader();

            particleIdx += nDrops[i];

        }
    }

    // Clean up our mess.
    renderer->EnableDepthWrites(true);
    renderer->EnableDepthReads(true);
    renderer->EnableLighting(false);
    renderer->EnableTexture2D(true);
    //ren->EnableBackfaceCulling(true);
    renderer->UnbindShader();

    renderer->SetDepthRange(nearDepth, farDepth);

    renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
    renderer->SetProjectionMatrix(camera->GetProjectionMatrix());
    renderer->SetTextureMatrix(savedTexture);
}

void Rain::UpdateParticles(const Vector3& motion, const Vector3& dp, int start, int end, const Camera* camera)
{
    const Vector3& camPos = camera->GetPosition();
    Vector3 center = camPos - dp;
#ifndef CUBE_VOLUME
    double  farClipSquared = farClip * farClip;
    double  nearClipSquared = nearClip * nearClip;
#endif

    // Enable OpenMP in your language settings to parallelize the particle physics update.
    // In practice, it doesn't really do much good on a dual core.
#ifdef _OPENMP
    #pragma omp parallel for \
    shared(start, end, camPos, center)
#endif
    for (int i = start; i < end; i++) {
#ifdef CUBE_VOLUME
        if ( MathUtils::Abs(particles[i].pos.x - camPos.x) > farClip ||
                MathUtils::Abs(particles[i].pos.y - camPos.y) > farClip ||
                MathUtils::Abs(particles[i].pos.z - camPos.z) > farClip )
#else
        double sqLen = (particles[i].pos - camPos).SquaredLength();
        if ( sqLen > farClipSquared || sqLen < nearClipSquared )
#endif
        {
            particles[i].InitializeParticlePosition(center, i, atmosphere->GetRandomNumberGenerator());
        } else {
            particles[i].pos = particles[i].pos + motion;
        }
    } /* End parallel for loop */
}

