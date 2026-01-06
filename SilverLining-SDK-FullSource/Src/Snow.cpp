// Copyright (c) 2009-2023 Sundog Software LLC, all rights reserved worldwide

#include "Snow.h"
#include "VertexBuffer.h"
#include "Atmosphere.h"
#include "Configuration.h"
#include "Renderer.h"
#include "MathUtils.h"
#include "PrecipitationManager.h"
#include "Camera.h"
#include "ScopedConstantPrep.h"
#include "PrecipitationShaders.h"
#include "TextureManager.h"
#include "SLAssert.h"

#if defined(__INTEL_COMPILER)
#include <mathimf.h>
#else
#include <math.h>
#endif

//#define CUBE_VOLUME

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;
using namespace SilverLining;

#define NUM_RADII 65536
static Vector3f randomOffsets[NUM_RADII];


void SnowParticle::InitializeParticlePosition(const Vector3& camPos, int idxOffset, const RandomNumberGenerator* rng)
{
    int index = rng->UniformRandomIntRange(0, NUM_RADII - 1);
    index += idxOffset;
    index %= NUM_RADII;
    const Vector3f& offset = randomOffsets[index];
    mat.elem[0][3] = camPos.x + offset.x;
    mat.elem[1][3] = camPos.y + offset.y;
    mat.elem[2][3] = camPos.z + offset.z;
}

void SnowParticle::InitializeParticleRotation(const RandomNumberGenerator* rng)
{
    const Vector3f& offset1 = randomOffsets[rng->UniformRandomIntRange(0, NUM_RADII - 1)];
    const Vector3f& offset2 = randomOffsets[rng->UniformRandomIntRange(0, NUM_RADII - 1)];
    Vector3 up (offset1.x, offset1.y, offset1.z);
    Vector3 right(offset2.x, offset2.y, offset2.z);
    right.Normalize();
    Vector3 in = right.Cross(up);
    in.Normalize();
    up = in.Cross(right);
    up.Normalize();

    mat.elem[0][0] = right.x;
    mat.elem[1][0] = right.y;
    mat.elem[2][0] = right.z;
    mat.elem[0][0] = up.x;
    mat.elem[1][1] = up.y;
    mat.elem[2][1] = up.z;
    mat.elem[0][2] = in.x;
    mat.elem[1][2] = in.y;
    mat.elem[2][2] = in.z;
}

Snow::Snow(const Atmosphere* _atmosphere, const Vector3& camPos, ThreadCameraStreamData* _tcsData)
    : Precipitation(_atmosphere, _tcsData)
    , matricesBuffer(0)
{
    isWet = false;
    indexBuffer = 0;
    vertexBuffer = 0;
    lastComputedIsWet = false;
    lastComputedIntensity = lastComputedFogDensity = -1;

    maxParticles = 100000;
    maxRenderedParticles = 3000;
    maxIntensity = 3.0;
    farClip = 10.0;
    visibilityMultiplier = 1.0;
    lambda = 1.6;
    sizeMultiplier = 1.0;
    rotateParticles = true;
    nearClip = 0.1f * Atmosphere::GetUnitScale();
    minParticlePixels = 5.0;

    double r = 0.8, g = 0.8, b = 0.8, a = 1.0;
    Configuration::GetIntValue("snow-max-particles", maxParticles);
    Configuration::GetIntValue("snow-max-rendered-particles", maxRenderedParticles);
    Configuration::GetDoubleValue("snow-max-intensity", maxIntensity);
    Configuration::GetDoubleValue("snow-volume-radius", farClip);
    farClip *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("snow-visibility-multiplier", visibilityMultiplier);
    Configuration::GetDoubleValue("snow-lambda", lambda);
    Configuration::GetDoubleValue("snowflake-size-multiplier", sizeMultiplier);
    Configuration::GetBoolValue("snow-rotate-particles", rotateParticles);
    Configuration::GetDoubleValue("snow-minimum-pixels", minParticlePixels);

    Configuration::GetBoolValue("snow-use-depth-buffer", useDepthBuffer);
    Configuration::GetBoolValue("snow-write-depth", writeDepth);
    wetSnowVelocity = 2.0;
    drySnowVelocity = 1.0;
    Configuration::GetDoubleValue("wet-snow-velocity", wetSnowVelocity);
    Configuration::GetDoubleValue("dry-snow-velocity", drySnowVelocity);

    Configuration::GetDoubleValue("snow-color-r", r);
    Configuration::GetDoubleValue("snow-color-g", g);
    Configuration::GetDoubleValue("snow-color-b", b);
    Configuration::GetDoubleValue("snow-color-a", a);
    snowColor = Color(r, g, b, a);

    Configuration::GetDoubleValue("snow-near-clip", nearClip);
    nearClip *= Atmosphere::GetUnitScale();

    tcsData->SetForceImmediate(true);

    InitializeBuffers();
    InitializeParticles(camPos);

    fastPathRendering = renderer->SupportsInstancedRendering()
                        && tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnow();

    if (fastPathRendering) {
        InitializeMatricesBuffer();
    }

    TextureManager* textureManager = (atmosphere->TexturesAreShared()) ? atmosphere->GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
    snowTexture = textureManager->GetOrCreateSnowTexture();

    // Initialize once
    ShaderHandle precipShader = (fastPathRendering) ? tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnow() :
                                tcsData->GetPrecipitationShaders()->GetShaderProgram();
    static_cast<void*>(precipShader);

    tcsData->SetForceImmediate(false);
}

Snow::~Snow()
{
    if (indexBuffer) SL_DELETE indexBuffer;
    if (vertexBuffer) SL_DELETE vertexBuffer;

    if (matricesBuffer) renderer->DestroyBuffer(matricesBuffer);
}

void Snow::SetIntensity(double R)
{
    if (R < 0) R = 0;
    if (R > maxIntensity) R = maxIntensity;

    intensity = R;

    if (isWet == lastComputedIsWet && R == lastComputedIntensity) {
        fogDensity = lastComputedFogDensity;
        return;
    }

    if (intensity == 0) {
        fogDensity = 0;
        return;
    }

    double snowVelocityFactor = 1.0;
    Configuration::GetDoubleValue("snow-velocity-factor", snowVelocityFactor);

    terminalVelocity = isWet ? wetSnowVelocity : drySnowVelocity; // m/s
    terminalVelocity *= snowVelocityFactor;

    double C3 = isWet ? 0.072 : 0.017;
    double Vt = terminalVelocity * 100.0; // cm/s
    double intensityCmS = (intensity * 0.1) / 3600.0; // mm/hr -> cm/s

    // Reference: Rasmussen, Vivekanandan, Cole, Meyers, Masters, "The Estimation of Snowfall Rate Using Visibility",
    // 1999, Journal of Applied Meteorology, Vol. 38, pp 1542-1563

    double N0 = (6.0 * pow(lambda, 4.0) * intensityCmS) / (MathUtils::Pi() * C3 * 6.0 * Vt);

    double visibilityCm = (1.3 * C3 * Vt) / intensityCmS;

    visibility = visibilityCm * 0.01 * visibilityMultiplier * Atmosphere::GetUnitScale();
    terminalVelocity *= Atmosphere::GetUnitScale();

    // From Koschmeider equation
    fogDensity = (3.91 / visibility) / 1.3;

    double totalFlakes = 0;
    int i;

    for (i = 0; i < NUM_SNOWFLAKE_DIAMETERS; i++) {
        flakeDiameters[i] = SNOWFLAKE_MIN_DIAMETER + (i * SNOWFLAKE_DIAMETER_INCREMENT);
        double Dcm = flakeDiameters[i] * 0.1; // mm -> cm
        nFlakes[i] = N0 * exp(lambda * Dcm);
        totalFlakes += nFlakes[i];

        flakeDiameters[i] *= Atmosphere::GetUnitScale();
    }

    // Normalize and multiply by available particles.
    double totalParticles = (R / maxIntensity) * (double)maxParticles;
    for (i = 0; i < NUM_SNOWFLAKE_DIAMETERS; i++) {
        nFlakes[i] = (nFlakes[i] / totalFlakes) * totalParticles;
    }

    lastComputedIsWet = isWet;
    lastComputedIntensity = intensity;
    lastComputedFogDensity = fogDensity;
}

static void CopyRotationMatrix(SnowParticle& particle, const Matrix4& billboard)
{
    for (int ii = 0; ii < 3; ii++) {
        for (int jj = 0; jj < 3; jj++) {
            particle.mat.elem[ii][jj] = billboard.elem[ii][jj];
        }
    }
}

struct mat4 {
    float mat[4][4];
};

static void CopyMatrix(mat4& lhs, const Matrix4& rhs, const Matrix4& _modelview)
{
    Matrix4 modelview = _modelview * rhs;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            lhs.mat[i][j] = (float)modelview.elem[j][i];
        }
    }
}

void Snow::InitializeMatricesBuffer()
{
    const int particleSize = sizeof(mat4);

    bool precip_particles_use_staging_buffer = true;
    Configuration::GetBoolValue("precip-particles-use-staging-buffer", precip_particles_use_staging_buffer);

    BufferProperties bufferProperties;
    bufferProperties.useStagingBuffer = precip_particles_use_staging_buffer;
    bufferProperties.useMapUnmap = true;
    bufferProperties.frameIndexed = true; // for vulkan

    if (tcsData->GetRenderer()->GetIsOpenGL()) {
        matricesBuffer = renderer->CreateBuffer("matrices_buffer", SHADER_STORAGE_BUFFER, particleSize * maxRenderedParticles, bufferProperties);
    } else {
        matricesBuffer = renderer->CreateBuffer("matrices_buffer", SHADER_STORAGE_BUFFER, particleSize * maxRenderedParticles * NUM_SNOWFLAKE_DIAMETERS, bufferProperties);
    }
}

bool Snow::IsCulled(const Vector3& particlePosition, const double s, const Frustum& adjustedFrustum)
{
    // You might think doing a cull test against every single particle
    // would kill performance, but not doing this kills it a LOT more.
    bool cull = false;

    for (int plane = 0; plane < 5; plane++) {
        const Plane& p = adjustedFrustum.GetPlane(plane);
        const Vector3& N = p.GetNormal();
        double origin = N.Dot(particlePosition) + p.GetDistance();
        if (origin < -s) {
            cull = true;
            break;
        }
    }
    return cull;
}

void Snow::DrawParticles(const int startIdx, const int endIdx, const double s
                         , const Frustum& adjustedFrustum
                         , const Matrix4& billboard
                         , const Matrix4& proj
                         , const Matrix4& scale
                         , const Camera* camera
                         , ShaderHandle precipShader)
{
    renderer->SetConstantMatrix(precipShader, "sl_proj", proj);
    renderer->SetConstantMatrix(precipShader, "sl_cameraModelView", camera->GetModelViewMatrix());
    renderer->SetConstantMatrix(precipShader, "sl_scale", scale);

    // Now, draw each particle.
    // Note - trying to parallelize this doesn't do any good, since you can't draw as you go,
    // and can't break out of the loop early. We tried, it actually hurt performance.

    int numFlakesRendered = 0;
    for (int currentParticleIndex = startIdx; currentParticleIndex < endIdx; currentParticleIndex++) {
        if (numFlakesRendered >= maxRenderedParticles)
            break;

        const Vector3 particlePosition(particles[currentParticleIndex].mat.elem[0][3], particles[currentParticleIndex].mat.elem[1][3], particles[currentParticleIndex].mat.elem[2][3]);

        if (IsCulled(particlePosition, s, adjustedFrustum)) {
            continue;
        }

        if (rotateParticles == false) {
            CopyRotationMatrix(particles[currentParticleIndex], billboard);
        }

        if (precipShader) {
            Matrix4 mvp = proj * (camera->GetModelViewMatrix() * particles[currentParticleIndex].mat * scale);
            renderer->SetConstantMatrix(precipShader, "sl_modelViewProj", mvp);
        } else {
            renderer->SetModelviewMatrix(camera->GetModelViewMatrix() * particles[currentParticleIndex].mat * scale);
        }

        renderer->DrawStripDirect(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(), vertexBuffer->GetNumVertices());

        numFlakesRendered++;
    }
}

void Snow::DrawParticlesInstanced(const int startIdx, const int endIdx, int diameterIndex, const double s
                                  , const Frustum& adjustedFrustum
                                  , const Matrix4& billboard
                                  , const Matrix4& proj
                                  , const Matrix4& scale
                                  , const Camera* camera
                                  , ShaderHandle precipShader)
{
    if (fastPathRendering && (renderer->GetIsOpenGL() || renderer->GetIsVulkan())) {
        const PrecipitationShaderConstantLocations& locations = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnowLocations();

        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_proj, proj);
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_cameraModelView, camera->GetModelViewMatrix());
        renderer->SetConstantMatrix4AtLocation(precipShader, locations.sl_scale, scale);
    } else {
        renderer->SetConstantMatrix(precipShader, "sl_proj", proj);
        renderer->SetConstantMatrix(precipShader, "sl_cameraModelView", camera->GetModelViewMatrix());
        renderer->SetConstantMatrix(precipShader, "sl_scale", scale);
    }

    void* context = renderer->GetContext();

    // Now, draw each particle.
    // Note - trying to parallelize this doesn't do any good, since you can't draw as you go,
    // and can't break out of the loop early. We tried, it actually hurt performance.
    mat4* pMatrices = static_cast<mat4*>(matricesBuffer->Lock(0, matricesBuffer->Size(), context));

    if (renderer->GetIsVulkan()) {
        pMatrices += maxRenderedParticles * diameterIndex;
    }

    int numFlakesRendered = 0;
    for (int currentParticleIndex = startIdx; currentParticleIndex < endIdx; currentParticleIndex++) {
        if (numFlakesRendered >= maxRenderedParticles)
            break;

        const Vector3 particlePosition(particles[currentParticleIndex].mat.elem[0][3], particles[currentParticleIndex].mat.elem[1][3], particles[currentParticleIndex].mat.elem[2][3]);

        if (rotateParticles == false) {
            CopyRotationMatrix(particles[currentParticleIndex], billboard);
        }

        CopyMatrix(pMatrices[numFlakesRendered], particles[currentParticleIndex].mat, camera->GetModelViewMatrix());

        numFlakesRendered++;
    }
    matricesBuffer->Unlock(0, matricesBuffer->Size(), context);

    if (matricesBuffer) {
        if (renderer->GetIsOpenGL()) {
            matricesBuffer->Bind(1, context);
        } else if (renderer->GetIsVulkan()) {
            matricesBuffer->Bind(2, context);
        }
    }

    if (renderer->GetIsOpenGL()) {
        renderer->DrawStripDirectInstanced(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(), vertexBuffer->GetNumVertices(), numFlakesRendered, 0);
    } else if (renderer->GetIsVulkan()) {
        renderer->DrawStripDirectInstanced(indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(), vertexBuffer->GetNumVertices(), numFlakesRendered, diameterIndex * maxRenderedParticles);
    }

    if (matricesBuffer) {
        if (renderer->GetIsOpenGL()) {
            matricesBuffer->UnBind(1, context);
        } else if (renderer->GetIsVulkan()) {
            matricesBuffer->UnBind(2, context);
        }
    }
}

void Snow::Render(double dt, const Color& lightColor, const Camera* camera)
{
    if (intensity <= 0) return;

    // Clamp dt to something reasonable
    double clampedDt = GetClampedDt(dt);

    ShaderHandle precipShader = (fastPathRendering) ? tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnow()
                                : tcsData->GetPrecipitationShaders()->GetShaderProgram();

    float nearDepth, farDepth;
    renderer->GetDepthRange(nearDepth, farDepth);
    //renderer->SetDepthRange(0.0f, 1.0f);

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
    renderer->SetCurrentColor(lightColor * snowColor);
    renderer->EnableTexture(snowTexture, 0);

    Vector3 currentCamPos = camera->GetPosition();

    double velocity, heading;
    atmosphere->GetConditions().GetWind(velocity, heading, atmosphere->GetConditions().GetLocation(tcsData).GetAltitude());
    double windX, windZ;
    atmosphere->GetConditions().GetPrecipitationWind(windX, windZ);
    double dx = windX + sin(MathUtils::ToRadians(-heading)) * velocity;
    double dz = windZ + cos(MathUtils::ToRadians(-heading)) * velocity;

    int numFlakesRendered = 0;

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

    Configuration::GetDoubleValue("snowflake-size-multiplier", sizeMultiplier);
    Configuration::GetBoolValue("snow-rotate-particles", rotateParticles);

    float a = 1.0f;
    Configuration::GetFloatValue("snow-color-a", a);
    snowColor.a = a;

    for (int i = 0; i < NUM_SNOWFLAKE_DIAMETERS; i++) {
        double dy = -terminalVelocity * clampedDt;

        Vector3 down = Vector3(0, dy, 0) * camera->GetBasis3x3();

        // Compute vector of raindrop's movement relative to the camera since the last frame
        Vector3 wind(Vector3(dx, 0, dz) * camera->GetBasis3x3() * 0.05 * clampedDt * flakeDiameters[i]);

        Vector3 cameraMotion = (previousCamPos - currentCamPos);
        Vector3 particleMotion = down + wind;

        //Vector3 dp = (previousCamPos - currentCamPos) + particleMotion;

        Matrix4 billboard;

        if (!rotateParticles) {
            // Compute basis of motion vector, make a rotation matrix to properly orient
            // the flakes as a billboard using the motion vector as a fixed axis.
            Vector3 up = down * -1.0;
            up.Normalize();

            Vector3 in = camDir * -1;
            Vector3 right = up.Cross(in);
            right.Normalize();
            in = right.Cross(up);
            in.Normalize();

            billboard.elem[0][0] = right.x;
            billboard.elem[0][1] = right.y;
            billboard.elem[0][2] = right.z;
            billboard.elem[1][0] = up.x;
            billboard.elem[1][1] = up.y;
            billboard.elem[1][2] = up.z;
            billboard.elem[2][0] = in.x;
            billboard.elem[2][1] = in.y;
            billboard.elem[2][2] = in.z;

            billboard.Transpose();
        }


        if (vertexBuffer && indexBuffer) {
            int startIdx = particleIdx;
            int endIdx = particleIdx + (int)nFlakes[i];

            // Do physics.
            UpdateParticles(particleMotion, cameraMotion, startIdx, endIdx, camera);

            // Make streaks of proper width and length using a scaling matrix
            Matrix4 scale;
            double s = flakeDiameters[i] * 0.001 * sizeMultiplier;

            if (minParticlePixels > 0) {
                // Enforce minimum screen size at the near clip plane
                // Compute desired minimum size in NDC
                int x, y, w, h;
                renderer->GetViewport(x, y, w, h);
                double nearClipNDC = nearDepth;// renderer->GetIsDirectX() ? 0.0 : -1.0;
                Vector4 minSize = Vector4(minParticlePixels / ((double)w * 0.5), minParticlePixels / ((double)h * 0.5),
                                          nearClipNDC, 1.0);
                // Deproject to world size
                Vector4 worldMinSize = invProjNoSkew * minSize;

                double pointSize = worldMinSize.y; //worldMinSize.x > worldMinSize.y ? worldMinSize.x : worldMinSize.y;

                if (s < pointSize) s = pointSize;
            }

            scale.elem[0][0] = s;
            scale.elem[1][1] = s;
            scale.elem[2][2] = s;

            renderer->IncrementConstantBuffersOffset(precipShader);

            if (precipShader) {
                renderer->BindShader(precipShader);
                ScopedConstantPrep prep(renderer, precipShader, false);
                if (fastPathRendering && (renderer->GetIsOpenGL() || renderer->GetIsVulkan())) {
                    const PrecipitationShaderConstantLocations& locations = tcsData->GetPrecipitationShaders()->GetShaderProgramInstancedSnowLocations();
                    renderer->SetConstantVectorAtLocation(precipShader, locations.sl_outputScale, atmosphere->GetOutputScale());
                    renderer->SetConstantVector4AtLocation(precipShader, locations.sl_lightingColor, Vector4(lightColor.r * snowColor.r,
                                                           lightColor.g * snowColor.g, lightColor.b * snowColor.b, lightColor.a * snowColor.a));
                } else {
                    renderer->SetConstantVector(precipShader, "sl_outputScale", atmosphere->GetOutputScale());
                    renderer->SetConstantVector4(precipShader, "sl_lightingColor", Vector4(lightColor.r * snowColor.r,
                                                 lightColor.g * snowColor.g, lightColor.b * snowColor.b, lightColor.a * snowColor.a));
                }
            }

            // Set the current buffers
            renderer->SetVertexBuffer(vertexBuffer->GetHandle(), false);
            renderer->SetIndexBuffer(indexBuffer->GetHandle());

            if (fastPathRendering) {
                DrawParticlesInstanced(startIdx, endIdx, i, s, adjustedFrustum, billboard, proj, scale, camera, precipShader);
            } else {
                DrawParticles(startIdx, endIdx, s, adjustedFrustum, billboard, proj, scale, camera, precipShader);
            }

            renderer->UnsetVertexBuffer();
            renderer->UnsetIndexBuffer();

            renderer->UnbindShader();

            particleIdx += (int)nFlakes[i];

        }
    }

    // Clean up our mess.
    renderer->EnableDepthWrites(true);
    renderer->EnableDepthReads(true);
    renderer->EnableLighting(false);
    renderer->EnableTexture2D(true);
    //ren->EnableBackfaceCulling(true);

    renderer->SetDepthRange(nearDepth, farDepth);

    previousCamPos = currentCamPos;

    renderer->SetModelviewMatrix(camera->GetModelViewMatrix());
    renderer->SetProjectionMatrix(camera->GetProjectionMatrix());
}

void Snow::InitializeBuffers()
{
    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = 0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = true;

    name = (enableObjectLabeling)? "Snow::VertexBuffer" : (const char *)0;
    vertexBuffer = SL_NEW VertexBuffer(4, name, tcsData, bufferProperties);

    name = (enableObjectLabeling)? "Snow::IndexBuffer" : (const char *)0;
    indexBuffer = SL_NEW IndexBuffer(4, name, tcsData, bufferProperties);

    if (vertexBuffer && indexBuffer) {
        if (vertexBuffer->LockBuffer()) {
            if (indexBuffer->LockBuffer()) {
                Vertex *v = vertexBuffer->GetVertices();
                Index *idx = indexBuffer->GetIndices();

                if (v && idx) {
                    particles.reserve(maxParticles);
                    for (int i = 0; i < maxParticles; i++) {
                        particles.push_back(SnowParticle());
                    }

                    Color white(1, 1, 1, 1);

                    v[0].SetColor(white);
                    v[0].SetUV(0, 0);
                    v[0].x = -0.5;
                    v[0].y = 0.5;
                    v[0].z = 0;
                    v[0].w = 1.0f;
                    idx[0] = 0;

                    v[1].SetColor(white);
                    v[1].SetUV(1, 0);
                    v[1].x = 0.5;
                    v[1].y = 0.5;
                    v[1].z = 0;
                    v[1].w = 1.0f;
                    idx[1] = 1;

                    v[2].SetColor(white);
                    v[2].SetUV(0, 1);
                    v[2].x = -0.5;
                    v[2].y = -0.5;
                    v[2].z = 0;
                    v[2].w = 1.0f;
                    idx[2] = 2;

                    v[3].SetColor(white);
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

void Snow::InitializeParticles(const Vector3& camPos)
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
        } while (r2 < (nearClip * nearClip) || r2 >(farClip * farClip));

        randomOffsets[i] = Vector3f(x, y, z);
#endif
    }

    SL_VECTOR(SnowParticle) ::iterator it;
    for (it = particles.begin(); it != particles.end(); it++) {
        if (rotateParticles) {
            (*it).InitializeParticleRotation(atmosphere->GetRandomNumberGenerator());
        }
        (*it).InitializeParticlePosition(camPos, 0, atmosphere->GetRandomNumberGenerator());

    }
}

void Snow::SetEffectRange(double fNear, double fFar, bool bUseDepthBuffer, const Camera* camera)
{
    if (fNear > 0 && fFar > 0 && fFar > fNear) {
        if (fNear != nearClip || fFar != farClip || bUseDepthBuffer != useDepthBuffer) {
            Precipitation::SetEffectRange(fNear, fFar, bUseDepthBuffer, camera);
            InitializeParticles(camera->GetPosition());
        }
    }
}

void Snow::UpdateParticles(const Vector3& flakeMotion, const Vector3& camMotion, int start, int end, const Camera* camera)
{
    const Vector3& camPos = camera->GetPosition();
    Vector3 center = camPos - camMotion;
#ifndef CUBE_VOLUME
    double  farClipSquared = farClip * farClip;
    double  nearClipSquared = nearClip * nearClip;
#endif

    // Enable OpenMP in your language settings to parallelize the particle physics update.
    // In practice, it doesn't really do much good on a dual core.
#ifdef _OPENMP
    #pragma omp parallel for \
    shared(start, end, camPos, flakeMotion, center)
#endif
    for (int i = start; i < end; i++) {
#ifdef CUBE_VOLUME
        if ( MathUtils::Abs(particles[i].mat.elem[0][3] - camPos.x) > farClip ||
                MathUtils::Abs(particles[i].mat.elem[1][3] - camPos.y) > farClip ||
                MathUtils::Abs(particles[i].mat.elem[2][3] - camPos.z) > farClip )
#else
        Vector3 pos(particles[i].mat.elem[0][3], particles[i].mat.elem[1][3], particles[i].mat.elem[2][3]);
        double sqLen = (pos - camPos).SquaredLength();
        if ( sqLen > farClipSquared || sqLen < nearClipSquared )
#endif
        {
            particles[i].InitializeParticlePosition(center, i, atmosphere->GetRandomNumberGenerator());
        } else {
            particles[i].mat.elem[0][3] += flakeMotion.x;
            particles[i].mat.elem[1][3] += flakeMotion.y;
            particles[i].mat.elem[2][3] += flakeMotion.z;
        }
    } /* End parallel for loop */
}

