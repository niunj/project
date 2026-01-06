// Copyright 2006-2023 Sundog Software. All rights reserved worldwide.

#include "CirroStratusCloud.h"
#include "Configuration.h"
#include "Metaball.h"
#include "CloudLayer.h"
#include "Billboard.h"
#include "Sky.h"
#include "CrepuscularRays.h"
#include "ShadowMap.h"
#include "Camera.h"
#include "OverriddenCameraPos.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "ScopedConstantPrep.h"
#include "ThreadCameraStreamData.h"
#include "CirrusShader.h"
#include "CloudLayerTcsUserData.h"
#include "TextureManager.h"
#include "SLAssert.h"
#include <stdlib.h>

using namespace SilverLining;
using namespace std;

CirroStratusCloud::CirroStratusCloud(CloudLayer *layer, double coverageThreshold)
    : Cloud(layer, coverageThreshold), texture(0), vb(0), ib(0), fadeFalloff(5.0), animateCirrus(true)
    , tcsData(0)
{
    // nothing else to do
}

void CirroStratusCloud::Init(double w, double h, ThreadCameraStreamData* _tcsData)
{
    tcsData = _tcsData;
    width = w;
    height = h;

    extinction = 0.5f;
    albedo = 0.9f;
    Configuration::GetFloatValue("cirrus-extinction", extinction);
    Configuration::GetFloatValue("cirrus-albedo", albedo);
    Configuration::GetFloatValue("cirrus-fade-falloff", fadeFalloff);
    Configuration::GetBoolValue("cirrus-animate", animateCirrus);

    Renderer* renderer = tcsData->GetRenderer();

    TextureManager* textureManager = (parentCloudLayer->GetAtmosphere().TexturesAreShared()) ? parentCloudLayer->GetAtmosphere().GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
    texture = textureManager->GetOrCreateCirroStratusTexture();

    gridDim = 10;
    Configuration::GetIntValue("cirrus-grid-dimension", gridDim);
    bool curve = true;
    Configuration::GetBoolValue("cirrus-round-earth", curve);
    double earthRadius = 6371000;
    Configuration::GetDoubleValue("earth-radius-meters-polar", earthRadius);
    earthRadius *= Atmosphere::GetUnitScale();

    double radius = earthRadius;

    if (parentCloudLayer->GetCurveTowardGround()) {
        double baseWidth = parentCloudLayer->GetBaseWidth();
        double baseLength = parentCloudLayer->GetBaseLength();
        double a = sqrt(baseWidth * baseWidth + baseLength * baseLength) / 2.0;
        double b = parentCloudLayer->GetBaseAltitude(tcsData);
        radius = (a*a + b*b) / (2.0 * b);
        curve = true;
    }

    bool fade = GetParentCloudLayer()->GetFadeTowardEdges() && GetParentCloudLayer()->GetIsInfinite();
    float halfGrid = (float)gridDim * 0.5f;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = false;


    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = 0;

    name = (enableObjectLabeling)? "CirroStratusCloud::VertexBuffer" : (const char *)0;
    vb = SL_NEW VertexBuffer(gridDim * gridDim, name, tcsData, bufferProperties);
    if (vb && vb->LockBuffer()) {
        Vertex *verts = vb->GetVertices();

        if (verts) {
            float hw = (float)w * 0.5f;
            float hh = (float)h * 0.5f;

            float incX = (float)w / (float)(gridDim - 1);
            float incY = (float)h / (float)(gridDim - 1);
            float incT = 1.0f / (float)(gridDim - 1);

            // Bottom verts
            for (int row = 0; row < gridDim; row++) {
                for (int col = 0; col < gridDim; col++) {
                    int idx = row * gridDim + col;
                    Vector3 v(-hw + col * incX, 0, hh - row * incY);
                    if (curve) {
                        double dist = v.Length();
                        if (dist < radius) {
                            double displacement = radius - (sqrt(radius * radius - dist * dist));
                            v.y = -displacement;
                        }
                    }

                    verts[idx].x = (float)v.x;
                    verts[idx].y = (float)v.y;
                    verts[idx].z = (float)v.z;
                    verts[idx].w = 1.0f;
                    verts[idx].SetUV(col * incT, row * incT);

                    Color vertColor = Color(1.0, 1.0, 1.0);
                    if (fade) {
                        float dx = (row - halfGrid) / halfGrid;
                        float dy = (col - halfGrid) / halfGrid;
                        float distFromCenter = sqrtf(dx * dx + dy * dy);
                        vertColor.a *= exp(-fadeFalloff * distFromCenter * distFromCenter * distFromCenter);
                    }
                    verts[idx].SetColor(vertColor);
                }
            }
        }
        vb->UnlockBuffer();
    }

    numVerts = gridDim * gridDim;

    nIndices = (gridDim - 1) * (gridDim * 2 + 2);
    name = (enableObjectLabeling)? "CirroStratussCloud::IndexBuffer" : (const char *)0;

    ib = SL_NEW IndexBuffer(nIndices, name, tcsData, bufferProperties);
    if (ib && ib->LockBuffer()) {
        Index *indices = ib->GetIndices();
        int idx = 0;
        for (int strip = 0; strip < gridDim - 1; strip++) {
            for (int col = 0; col < gridDim; col++) {
                Index a = (Index)(strip * gridDim + col);
                Index b = (Index)((strip + 1) * gridDim + col);

                indices[idx++] = a;
                indices[idx++] = b;
            }

            // Degenerate triangles to wrap back to next strip
            indices[idx] = indices[idx-1];
            ++idx;
            indices[idx] = (Index)((strip+1) * gridDim);

            idx++;
        }
        ib->UnlockBuffer();
    }

    // initialize the shader if not initialized already
    tcsData->GetCirrusShader();
}

CirroStratusCloud::~CirroStratusCloud()
{
    if (vb) {
        SL_DELETE vb;
    }

    if (ib) {
        SL_DELETE ib;
    }
}

bool CirroStratusCloud::IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    return false;
}

bool CirroStratusCloud::Draw(int pass, const Vector3 &lightPos, const Vector3& lightDir
                             , const Color &lightColor, bool invalid, const Sky *sky, bool
                             , const Camera* camera
                             , ThreadCameraStreamData* tcsData)
{
    if (pass == 0) {
        Color nLightColor;
        nLightColor = lightColor * extinction //* (float)(lightDir.Dot(GetParentCloudLayer()->GetLocalUpVector()))
                      + tcsData->GetMetaballRenderingParams()->GetAmbientColor() * albedo;
        nLightColor.ClampToUnitOrLess(tcsData->GetHDREnabled());
        cloudColor = nLightColor;
    } else {
        Vector3 dir = GetWorldPosition(tcsData) - camera->GetPosition();
        dir.Normalize();
        dir = dir * camera->GetInverseBasis3x3();
        if (dir.y < 0) dir.y = 0;
        fogColor = sky->SkyColorAt(dir, 0.0);

        Renderer* renderer = tcsData->GetRenderer();
        renderer->SubmitBlendedObject(this);
    }

    return true;
}

void CirroStratusCloud::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const
{
    // Don't cast rays from cirrus
    if (atmosphere) {
        const CrepuscularRays *rays = tcsData->GetCrepuscularRays();
        if (rays && rays->IsRendering()) return;
        const ShadowMap *sm = tcsData->GetShadowMapObject();
        if (sm && sm->IsRendering()) return;
    }

    Renderer* renderer = tcsData->GetRenderer();

    float cloudColorAlpha = (float)fade * (float)alpha;

    Vector3 camPos = sceneCamera->GetPosition();
    camPos = camPos * sceneCamera->GetInverseBasis3x3();

    const CloudLayer* parentCloudLayer = GetParentCloudLayer();
    CloudLayerTcsUserData* tcsUserData = parentCloudLayer->GetOrCreateCloudLayerTcsUserData(tcsData);
    double centerY = tcsUserData->geocentricAltitude;

    double layerX, layerZ;
    parentCloudLayer->GetLayerPosition(layerX, layerZ, tcsData);

    bool viewingFromEdge = ((camPos.x < (layerX - parentCloudLayer->GetBaseWidth() * 0.5)) ||
                            (camPos.x > (layerX + parentCloudLayer->GetBaseWidth() * 0.5)) ||
                            (camPos.z > (layerZ + parentCloudLayer->GetBaseLength() * 0.5)) ||
                            (camPos.z < (layerZ - parentCloudLayer->GetBaseLength() * 0.5)));

    bool drawTop = (camPos.y >= (centerY + parentCloudLayer->GetThickness()));
    bool drawBottom = ((camPos.y <= centerY) || viewingFromEdge);

    if (!(drawTop || drawBottom)) return;

    const BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    bool fogOn = renderer->GetFogEnabled();
    if (!fogOn) {
        renderer->EnableFog(true);
        renderer->ConfigureFog(billboardParams->GetFogDensity(), 1, 100000, fogColor);
    }

    // Set the blend and z buffer modes
#ifdef ANDROID
    renderer->EnableBlending(SRCALPHA, ONE);
#else
    renderer->EnableBlending(SRCALPHA, INVSRCALPHA);
#endif
    renderer->EnableDepthReads(true);
    renderer->EnableDepthWrites(false);
    renderer->EnableTexture2D(true);
    renderer->EnableLighting(false);
    renderer->EnableBackfaceCulling(false);

    Matrix4 xlate;
    double texOffsetX = 0, texOffsetY = 0;

    Camera adjustedCam(renderCamera, true, false);
    if (parentCloudLayer->GetIsInfinite()) {
        double deltaY = centerY;
        if (drawTop) deltaY += parentCloudLayer->GetThickness();
        deltaY -= camPos.y;
        Vector3 offset = parentCloudLayer->GetLocalUpVector(tcsUserData) * deltaY;

        Matrix4 orientation = adjustedCam.GetModelViewMatrix();
        orientation.elem[0][3] = orientation.elem[1][3] = orientation.elem[2][3] = 0;

        xlate.elem[0][3] = offset.x;
        xlate.elem[1][3] = offset.y;
        xlate.elem[2][3] = offset.z;
        xlate.elem[3][3] = 1;

        adjustedCam.SetModelViewMatrix(orientation * xlate * sceneCamera->GetInverseBasis4x4());
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());

        // Texture animation
        if (animateCirrus && (parentCloudLayer->GetFadeTowardEdges()&&fadeOverride)) {
            const Vector3& camPos = overriddenCameraPos.GetCamPos();
            Vector3 camPosYUp = camPos * sceneCamera->GetInverseBasis3x3();
            texOffsetX = camPosYUp.x / parentCloudLayer->GetBaseWidth();
            texOffsetY = -camPosYUp.z / parentCloudLayer->GetBaseLength();
            texOffsetX = fmod(texOffsetX, 1.0);
            texOffsetY = fmod(texOffsetY, 1.0);
        }
    } else {
        // Translate to the cloud position
        Vector3 offset(0, 0, 0);
        if (drawTop) {
            offset = parentCloudLayer->GetLocalUpVector(tcsUserData) * parentCloudLayer->GetThickness();
        }

        Vector3 pos = GetWorldPosition(tcsData) + offset;
        xlate.elem[0][3] = pos.x;
        xlate.elem[1][3] = pos.y;
        xlate.elem[2][3] = pos.z;
        adjustedCam.MultiplyModelViewMatrix(xlate * sceneCamera->GetInverseBasis4x4());
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());
    }

    renderer->EnableTexture(texture, 0);

    const ShaderHandle hdrShader = tcsData->GetCirrusShader()->GetShader();

    if (hdrShader) {
        renderer->BindShader(hdrShader);
        const Vector4 fog(fogColor.r, fogColor.g, fogColor.b,
                          tcsData->GetBillboardRenderingParams()->GetFogDensity());
        ScopedConstantPrep prep(renderer, hdrShader, false);
        if (renderer->GetType() == Atmosphere::OPENGL32CORE || renderer->GetType() == Atmosphere::OPENGL) {
            const CirrusShaderConstantLocations& shaderConstantLocations = tcsData->GetCirrusShader()->GetConstantLocations();
            renderer->SetConstantVectorAtLocation(hdrShader, shaderConstantLocations.sl_outputScale, atmosphere->GetOutputScale());
            renderer->SetConstantMatrix4AtLocation(hdrShader, shaderConstantLocations.sl_modelView, adjustedCam.GetModelViewMatrixFloatArray());
            renderer->SetConstantMatrix4AtLocation(hdrShader, shaderConstantLocations.sl_modelViewProj, adjustedCam.GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantVector4AtLocation(hdrShader, shaderConstantLocations.sl_fogColorAndDensity, fog);
            renderer->SetConstantVector4AtLocation(hdrShader, shaderConstantLocations.sl_lightingColor, Vector4(cloudColor.r, cloudColor.g, cloudColor.b, cloudColorAlpha));
            renderer->SetConstantVector4AtLocation(hdrShader, shaderConstantLocations.sl_textureOffset, Vector4(texOffsetX, texOffsetY, 0, 0));
        } else {
            renderer->SetConstantVector(hdrShader, "sl_outputScale", atmosphere->GetOutputScale());
            renderer->SetConstantMatrix(hdrShader, "sl_modelView", adjustedCam.GetModelViewMatrixFloatArray());
            renderer->SetConstantMatrix(hdrShader, "sl_modelViewProj", adjustedCam.GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantVector4(hdrShader, "sl_fogColorAndDensity", fog);
            renderer->SetConstantVector4(hdrShader, "sl_lightingColor", Vector4(cloudColor.r, cloudColor.g, cloudColor.b, cloudColorAlpha));
            renderer->SetConstantVector4(hdrShader, "sl_textureOffset", Vector4(texOffsetX, texOffsetY, 0, 0));
        }
    }

    vb->DrawIndexedStrip(*ib, true);

    renderer->UnbindShader();

    renderer->SetModelviewMatrix(sceneCamera->GetModelViewMatrix());
    renderer->DisableBlending();
    //ren->EnableDepthWrites(true);

    if (!fogOn) {
        renderer->EnableFog(false);
    }
}

bool CirroStratusCloud::Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
{
    return false;
}

Vector3 CirroStratusCloud::GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    double x, y, z;
    GetParentCloudLayer()->GetSortPosition(x, y, z, camera, tcsData);
    return Vector3(x, y, z);
}

double CirroStratusCloud::GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    const double farFarAway = 1000000;

    // Find ray / plane intersection point.
    Vector3 xformPos = GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();
    double D = xformPos.y;

    Vector3 v = to - from;
    Vector3 Rd = v;
    Rd.Normalize();

    Vector3 R0 = from;
    Vector3 Pn = Vector3(0, -1, 0) * camera->GetBasis3x3();

    const double epsilon = 2.4E-7;

    double Vd = Pn.Dot(Rd);
    if (Vd >= -epsilon) {
        // ray points away from plane, try the other plane normal
        Pn = Vector3(0, 1, 0) * camera->GetBasis3x3();
        Vd = Pn.Dot(Rd);
        D = -D;
    }

    if (Vd >= -epsilon) return farFarAway;

    double V0 = -(Pn.Dot(R0) + D);
    double t = V0 / Vd;

    if (t < 0) return farFarAway; // does not intersect

    Vector3 Pi(R0.x + Rd.x * t, R0.y + Rd.y * t, R0.z + Rd.z * t);

    if (tcsData->GetSortByScreenDepth()) {
        double dist = ((mv * Pi).z);
        return rightHanded ? -dist : dist;
    } else {
        return (Pi - from).Length();
    }
}

void CirroStratusCloud::GetSize(double& pwidth, double& pdepth, double& pheight) const
{
    pwidth = width;
    pdepth = height;
    pheight = GetParentCloudLayer()->GetThickness();
}
