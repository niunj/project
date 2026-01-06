// Copyright 2006-2023 Sundog Software, LLC. All rights reserved worldwide.

#include "StratusCloud.h"
#include "Configuration.h"
#include "Billboard.h"
#include "Metaball.h"
#include "CloudLayer.h"
#include "MathUtils.h"
#include "Camera.h"
#include "OverriddenCameraPos.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "CloudRenderingParameters.h"
#include "StratusCloudLayer.h"
#include "ScopedConstantPrep.h"
#include "ThreadCameraStreamData.h"
#include "CloudLayerTcsUserData.h"
#include "SLAssert.h"
#include "mie.h"

using namespace SilverLining;
using namespace std;

StratusCloud::StratusCloud(CloudLayer *layer, double coverageThreshold) : Cloud(layer, coverageThreshold), fadeFalloff(5.0)
    , tcsData(0)
{
    fogCutoff = 0.005;
    Configuration::GetDoubleValue("stratus-fog-render-cutoff", fogCutoff);

    doBrokenVisibility = false;
    Configuration::GetBoolValue("stratus-handle-broken-visibility", doBrokenVisibility);

    lightScale = 1.0f;
    Configuration::GetFloatValue("stratus-deck-light-scale", lightScale);

    skyColorScale = Vector3(1.0, 1.0, 1.0);
    Configuration::GetDoubleValue("stratus-sky-color-scale-r", skyColorScale.x);
    Configuration::GetDoubleValue("stratus-sky-color-scale-g", skyColorScale.y);
    Configuration::GetDoubleValue("stratus-sky-color-scale-b", skyColorScale.z);

    groundColorScale = Vector3(1.0, 1.0, 1.0);
    Configuration::GetDoubleValue("stratus-ground-color-scale-r", groundColorScale.x);
    Configuration::GetDoubleValue("stratus-ground-color-scale-g", groundColorScale.y);
    Configuration::GetDoubleValue("stratus-ground-color-scale-b", groundColorScale.z);

    for (int i = 0; i < NUM_SECTIONS; i++) {
        ibTop[i] = 0;
        ibBottom[i] = 0;
        textures[i] = 0;
        vb[i] = 0;
    }

    mieTexture = 0;
}

void StratusCloud::CreateTexture(double density, double edgeTexSizeX, double edgeTexSizeY, ThreadCameraStreamData* tcsData)
{
    StratusTextureData* stratusTextureData = static_cast<StratusCloudLayer*>(GetParentCloudLayer())
            ->GetOrCreateTextureData(density, edgeTexSizeX, edgeTexSizeY);
    if (stratusTextureData == 0) {
        return;
    }

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling)? "SilverLining::Stratus Cloud Texture" : (const char *)0;

    Renderer* renderer = tcsData->GetRenderer();
    const int width = stratusTextureData->scudMapW;
    const int height = stratusTextureData->scudMapH;
    // TO DO: If textures are shared, use StratusCloudTcsUserData to lazily create/store the texture in
    // the default tcs data, similar to metaball texture manager
    for (int i = 0; i < stratusTextureData->numTextures; ++i) {
        const bool repeatU = stratusTextureData->repeatU[i] > 0;
        const bool repeatV = stratusTextureData->repeatV[i] > 0;

        unsigned char* pixels = stratusTextureData->pixels[i];
        renderer->LoadTexture(pixels, width, height, &textures[i], repeatU, repeatV, name);
    }
}

Vector3 StratusCloud::GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    double x, y, z;
    GetParentCloudLayer()->GetSortPosition(x, y, z, camera, tcsData);
    return Vector3(x, y, z);
}

double StratusCloud::DistanceToPlane(const Vector3& from, const Vector3& to, double D, const Matrix4& mv, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    const double farFarAway = 1000000;

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
        return ((mv * Pi).z);
    } else {
        return (Pi - from).Length();
    }
}

double StratusCloud::GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    bool forcePlanar = false;
    Configuration::GetBoolValue("stratus-force-planar-sort", forcePlanar);

    if (!GetParentCloudLayer()->GetIsInfinite() && !forcePlanar) {
        if (tcsData->GetSortByScreenDepth()) {
            Vector3 clipSpace = mv * GetSortPosition(camera, tcsData);
            return rightHanded ? -clipSpace.z : clipSpace.z;
        } else {
            Vector3 V = from - GetSortPosition(camera, tcsData);
            return V.Length();
        }
    } else {
        // Find ray / plane intersection point.
        Vector3 xformPos = GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();
        double D = xformPos.y;

        // No distance if we're in the cloud
        Vector3 xformFrom = from * camera->GetInverseBasis3x3();
        if (xformFrom.y > xformPos.y && xformFrom.y < (xformPos.y + GetParentCloudLayer()->GetThickness())) {
            return 0;
        }

        double bottomDist = DistanceToPlane(from, to, D, mv, camera, tcsData);
        double topDist = DistanceToPlane(from, to, D + GetParentCloudLayer()->GetThickness(), mv, camera, tcsData);

        if (rightHanded) {
            bottomDist *= -1.0;
            topDist *= -1.0;
        }
        return topDist < bottomDist ? topDist : bottomDist;
    }
}

float StratusCloud::LookupScud(const Vector3& pos, ThreadCameraStreamData* tcsData)
{
    const StratusTextureData* stratusTextureData = static_cast<StratusCloudLayer*>(GetParentCloudLayer())
            ->GetTextureData();
    if (stratusTextureData == 0) {
        return 0;
    }
    const int scudMapW = stratusTextureData->scudMapW;
    const int scudMapH = stratusTextureData->scudMapH;
    const SL_VECTOR(unsigned char*)& scudMap = stratusTextureData->scudMap;

    double scudThickness;
    Configuration::GetDoubleValue("stratus-scud-thickness", scudThickness);
    scudThickness *= Atmosphere::GetUnitScale();

    CloudLayer* parentCloudLayer = GetParentCloudLayer();

    CloudLayerTcsUserData* tcsUserData = parentCloudLayer->GetOrCreateCloudLayerTcsUserData(tcsData);

    Matrix3 basis = parentCloudLayer->GetLocalBasis(tcsUserData);
    Matrix3 invBasis = basis.Transpose();

    Vector3 xformpos = pos * invBasis;
    Vector3 xformCloudPos = GetWorldPosition(tcsData) * invBasis;

    double layerBottom = tcsUserData->geocentricAltitude;
    double scudBottom = layerBottom - scudThickness;
    double cloudBottom = scudBottom + scudThickness;
    double cloudTop = cloudBottom + depth;
    double scudTop = cloudTop + scudThickness;

    if (xformpos.y < scudBottom || xformpos.y > scudTop) return 0;

    double layerX, layerZ;
    parentCloudLayer->GetLayerPosition(layerX, layerZ, tcsData);

    if (!parentCloudLayer->GetIsInfinite()) {
        if (xformpos.x < (layerX - parentCloudLayer->GetBaseWidth() * 0.5)) return 0;
        if (xformpos.x > (layerX + parentCloudLayer->GetBaseWidth() * 0.5)) return 0;
        if (xformpos.z > (layerZ + parentCloudLayer->GetBaseLength() * 0.5)) return 0;
        if (xformpos.z < (layerZ - parentCloudLayer->GetBaseLength() * 0.5)) return 0;
    }

    if (xformpos.y > cloudBottom && xformpos.y < cloudTop && textureDensity >= 1.0) return 1;

    float cloudDistance = 0;
    if (xformpos.y < cloudBottom) {
        cloudDistance = (float)((cloudBottom - xformpos.y) / (cloudBottom - scudBottom));
    }
    if (xformpos.y > cloudTop) {
        cloudDistance = (float)((xformpos.y - cloudTop) / (scudTop - cloudTop));
    }

    if (doBrokenVisibility && segmentWidth > 0 && segmentHeight > 0) {
        double interpX, interpY;

        if (parentCloudLayer->GetIsInfinite()) {
            Vector3 center = xformCloudPos;
            interpX = fmod(xformpos.x + center.x, segmentWidth);
            interpY = fmod(xformpos.z + center.z, segmentHeight);
        } else {
            double posInCloudX = xformpos.x - (xformCloudPos.x - parentCloudLayer->GetBaseWidth() * 0.5);
            double posInCloudY = xformpos.z - (xformCloudPos.z - parentCloudLayer->GetBaseLength() * 0.5);

            interpX = fmod(posInCloudX, segmentWidth);
            interpY = fmod(posInCloudY, segmentHeight);
        }

        interpX /= segmentWidth;
        interpY /= segmentHeight;
        if (interpX < 0) interpX += 1.0;
        if (interpY < 0) interpY += 1.0;
        interpY = 1.0 - interpY;

        int mapX = (int)(interpX * (double)scudMapW);
        int mapY = (int)(interpY * (double)scudMapH);
        int mapIdx = mapY * scudMapH + mapX;

        unsigned char scud = 0;

        if (mapIdx < scudMapW * scudMapH) {
            scud = scudMap[TILABLE][mapIdx];
        }

        return ((float)scud / 255.0f) * (1.0f - cloudDistance);
    }

    return (1.0f - cloudDistance);
}

IndexBuffer *StratusCloud::CreateIndices(int w, int h, bool top, ThreadCameraStreamData* tcsData)
{
    if (!(w > 0 && h > 0)) return 0;

    int offset = 0;
    if (top) offset = w * h;

    int nIndices = (h - 1) * (w * 2 + 2);

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling)? "StratusCloud::IndexBuffer" : (const char *)0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = false;

    IndexBuffer *ib = SL_NEW IndexBuffer(nIndices, name, tcsData, bufferProperties);
    if (ib && ib->LockBuffer()) {
        Index *indices = ib->GetIndices();

        int idx = 0;
        for (int strip = 0; strip < h - 1; strip++) {
            for (int col = 0; col < w; col++) {
                Index a = (Index)(strip * w + col);
                Index b = (Index)((strip + 1) * w + col);

                if (top) {
                    indices[idx++] = b + (Index)offset;
                    indices[idx++] = a + (Index)offset;
                } else {
                    indices[idx++] = a + (Index)offset;
                    indices[idx++] = b + (Index)offset;
                }
            }

            // Degenerate triangles to wrap back to next strip
            indices[idx] = indices[idx-1];
            ++idx;

            if (strip == h - 2) {
                // There is no next strip...
                indices[idx] = indices[idx - 1];
            } else {
                if (top) {
                    indices[idx] = (Index)((strip + 2) * w + offset);
                } else {
                    indices[idx] = (Index)((strip + 1) * w + offset);
                }
            }
            //SL_ASSERT(indices[idx] < w * h * 2);

            idx++;
        }

        ib->UnlockBuffer();

        return ib;
    }

    return NULL;
}

static double mapDistance(double x, double buffer, double maximum)
{
    if (x < buffer) return 0;

    double range = maximum - buffer;

    x = x - buffer;

    double nx = x / range;

    return nx * maximum;
}

VertexBuffer *StratusCloud::CreateVertices(int x1, int x2, int y1, int y2,
        float u1, float u2, float v1, float v2, double baseWidth, double baseLength
        , ThreadCameraStreamData* tcsData)
{
    bool curve = true;
    Configuration::GetBoolValue("stratus-round-earth", curve);

    if (!parentCloudLayer->GetIsInfinite()) {
        curve = false;
    }

    double earthRadius = 6371000;
    Configuration::GetDoubleValue("earth-radius-meters-polar", earthRadius);
    earthRadius *= Atmosphere::GetUnitScale();

    double tileBuffer = segmentWidth * 1.44;

    double radius = earthRadius;

    double a = baseWidth < baseLength ? baseWidth : baseLength; //sqrt(baseWidth * baseWidth + baseLength * baseLength) / 2.0;
    a *= 0.5;

    if (parentCloudLayer->GetCurveTowardGround()) {
        double scale = 1.0;
        Configuration::GetDoubleValue("curve-toward-ground-radius-scale", scale);
        double b = parentCloudLayer->GetBaseAltitude(tcsData);
        radius = (a*a + b*b) / (2.0 * b);
        radius *= scale;
        curve = true;
    }

    //radius -= tileBuffer;

    bool onlyCurveBottom = false;
    Configuration::GetBoolValue("stratus-only-curve-bottom", onlyCurveBottom);

    int w = x2 - x1;
    int h = y2 - y1;

    if (!(w > 0 && h > 0)) return NULL;

    float incTx = (u2 - u1) / (float)(w -1);
    float incTy = (v2 - v1) / (float)(h -1);

    int row, col;

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling)? "StratusCloud::VertexBuffer" : (const char *)0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = false;

    VertexBuffer *vb = SL_NEW VertexBuffer(w * h * 2, name, tcsData, bufferProperties);
    if (vb && vb->LockBuffer()) {
        Vertex *verts = vb->GetVertices();
        if (verts) {
            float tu, tv;
            // Bottom verts
            for (row = 0; row < h; row++) {
                for (col = 0; col < w; col++) {
                    int idx = row * w + col;
                    Vector3 v(-hw + (x1 + col) * incX, 0, hh - (y1 + row) * incY);

                    if (curve) {
                        double dist = v.Length();
                        dist = mapDistance(dist, tileBuffer, a);// -= segmentWidth * 1.44;
                        double displacement = radius - (sqrt(radius * radius - dist * dist));
                        v.y = -displacement;
                    }

                    verts[idx].x = (float)v.x;
                    verts[idx].y = (float)v.y;
                    verts[idx].z = (float)v.z;
                    verts[idx].w = 1.0f;

                    tu = u1 + col * incTx;
                    tv = v1 + row * incTy;
                    verts[idx].SetU(tu);
                    verts[idx].SetV(tv);

                    verts[idx].SetColor(Color(1.0,1.0,1.0,1.0));
                }
            }

            // Top verts
            for (row = 0; row < h; row++) {
                for (col = 0; col < w; col++) {
                    int idx = row * w + col;
                    idx += w * h;

                    Vector3 v(-hw + (x1 + col) * incX, depth, hh - (y1 + row) * incY);

                    if (curve && !onlyCurveBottom) {
                        Vector3 planar(v.x, 0, v.z);
                        double dist = planar.Length();
                        dist = mapDistance(dist, tileBuffer, a);// -= segmentWidth * 1.44;
                        double displacement = radius - (sqrt(radius * radius - dist * dist));
                        v.y -= displacement;
                    }

                    verts[idx].x = (float)v.x;
                    verts[idx].y = (float)v.y;
                    verts[idx].z = (float)v.z;
                    verts[idx].w = 1.0f;

                    tu = u1 + col * incTx;
                    tv = v1 + row * incTy;
                    verts[idx].SetU(tu);
                    verts[idx].SetV(tv);

                    verts[idx].SetColor(Color(1.0,1.0,1.0,1.0));
                }
            }
        }

        vb->UnlockBuffer();
    }

    return vb;
}

void StratusCloud::CreateMieLookupTexture(ThreadCameraStreamData* tcsData)
{
    int width = 0;
    const float* pixels = GetParentCloudLayer()->GetAtmosphere().GetDefaultTcsData()->GetStratusMieLookupPixels(width);

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling)? "SilverLining::Stratus Cloud Texture RGB" : (const char *)0;

    Renderer* renderer = tcsData->GetRenderer();
    renderer->LoadFloatTextureRGB(pixels, width, 2, &mieTexture, name);
}

static float findClosestDivisor(float target, int value)
{
    for (int candidate = value; candidate > 0; candidate--) {
        float divisor = (float)value / (float)candidate;
        if (divisor > target) {
            return divisor;
        }
    }

    return target;
}

void StratusCloud::Init(double w, double h, double thickness, double density, ThreadCameraStreamData* _tcsData)
{
    tcsData = _tcsData;
    width = w;
    height = h;
    depth = thickness;

    if (density > 1.0) density = 1.0;
    textureDensity = density;

    segmentWidth = segmentHeight = 0;

    dim = 50;
    if (density >= 1.0) {
        Configuration::GetIntValue("stratus-grid-dimension-solid", dim);
    } else {
        Configuration::GetIntValue("stratus-grid-dimension", dim);
    }

    double stratusScaleFactor = 1.0;
    Configuration::GetDoubleValue("stratus-scale-factor", stratusScaleFactor);
    double scale = 200000.0 * stratusScaleFactor * Atmosphere::GetUnitScale();

    dim = (int)((dim / scale) * w);

    if (dim <= 1) return;

    hw = (float)w * 0.5f;
    hh = (float)h * 0.5f;

    const float texDensityX = 10.0f / (float)scale;
    const float texDensityY = 10.0f / (float)scale;
    float maxU = ceil((float)w * texDensityX);
    float maxV = ceil((float)h * texDensityY);

    // maxU, maxV must be even divisors of (dim - 1)
    //maxU = findClosestDivisor(maxU, dim - 1);
    //maxV = findClosestDivisor(maxV, dim - 1);

    segmentWidth = w / maxU;
    segmentHeight = h / maxV;

    //float texelX = 1.0f / scudMapW;
    //float texelY = 1.0f / scudMapH;

    incX = (float)w / (float)(dim - 1);
    incY = (float)h / (float)(dim - 1);
    float incTx = (float)(maxU / (dim - 1));
    float incTy = (float)(maxV / (dim - 1));

    edgeVertsX = int(1.0f / incTx) + 1;
    edgeVertsY = int(1.0f / incTy) + 1;

    if (GetParentCloudLayer()->GetIsInfinite()) edgeVertsX = edgeVertsY = 0;

    double stratusEdgeFalloff = 4.0;
    Configuration::GetDoubleValue("stratus-edge-falloff", stratusEdgeFalloff);

    Configuration::GetFloatValue("stratus-fade-falloff", fadeFalloff);

    const float edgeTexSizeX = incTx * (edgeVertsX - 1);
    const float edgeTexSizeY = incTy * (edgeVertsY - 1);

    CreateTexture(density, edgeTexSizeX, edgeTexSizeY, tcsData);

    CreateMieLookupTexture(tcsData);

    vb[TILABLE]      = CreateVertices(edgeVertsX - 1,            dim - edgeVertsX + 1,
                                      edgeVertsY - 1,            dim - edgeVertsY + 1,
                                      edgeTexSizeX,              incTx * (dim - edgeVertsX),
                                      edgeTexSizeY,              incTy * (dim - edgeVertsY), w, h, tcsData);

    vb[LEFT]         = CreateVertices(0,                         edgeVertsX,
                                      edgeVertsY - 1,            dim - edgeVertsY + 1,
                                      0,                         edgeTexSizeX,
                                      incTy * (edgeVertsY - 1), incTy * (dim - edgeVertsY), w, h, tcsData);

    vb[RIGHT]        = CreateVertices(dim - edgeVertsX,          dim,
                                      edgeVertsY - 1,            dim - edgeVertsY + 1,
                                      1.0f - edgeTexSizeX,       1.0,
                                      incTy * (edgeVertsY - 1), incTy * (dim - edgeVertsY), w, h, tcsData);

    vb[TOP]          = CreateVertices(edgeVertsX - 1,            dim - edgeVertsX + 1,
                                      0,                         edgeVertsY,
                                      incTx * (edgeVertsX - 1),  incTx * (dim - edgeVertsX),
                                      0, edgeTexSizeY, w, h, tcsData);

    vb[BOTTOM]       = CreateVertices(edgeVertsX - 1,            dim - edgeVertsX + 1,
                                      dim - edgeVertsY,          dim,
                                      incTx * (edgeVertsX - 1),  incTx * (dim - edgeVertsX),
                                      1.0f - edgeTexSizeY, 1.0, w, h, tcsData);

    vb[TOPLEFT]      = CreateVertices(0,                         edgeVertsX,
                                      0,                         edgeVertsY,
                                      0,                         edgeTexSizeX,
                                      0, edgeTexSizeY, w, h, tcsData);

    vb[TOPRIGHT]     = CreateVertices(dim - edgeVertsX,          dim,
                                      0,                         edgeVertsY,
                                      1.0f - edgeTexSizeX,       1.0,
                                      0, edgeTexSizeY, w, h, tcsData);

    vb[BOTTOMLEFT]   = CreateVertices(0,                         edgeVertsX,
                                      dim - edgeVertsY,          dim,
                                      0,                         edgeTexSizeX,
                                      1.0f - edgeTexSizeY, 1.0, w, h, tcsData);

    vb[BOTTOMRIGHT]  = CreateVertices(dim - edgeVertsX,          dim,
                                      dim - edgeVertsY,          dim,
                                      1.0f - edgeTexSizeX,       1.0,
                                      1.0f - edgeTexSizeY, 1.0, w, h, tcsData);


    ibTop[TILABLE] = CreateIndices(dim - (edgeVertsX - 1) * 2, dim - (edgeVertsY - 1) * 2, true, tcsData);
    ibTop[LEFT] = CreateIndices(edgeVertsX, dim - (edgeVertsY - 1) * 2, true, tcsData);
    ibTop[RIGHT] = CreateIndices(edgeVertsX, dim - (edgeVertsY - 1) * 2, true, tcsData);
    ibTop[TOP] = CreateIndices(dim - (edgeVertsX - 1) * 2, edgeVertsY, true, tcsData);
    ibTop[BOTTOM] = CreateIndices(dim - (edgeVertsX - 1) * 2, edgeVertsY, true, tcsData);
    ibTop[TOPLEFT] = CreateIndices(edgeVertsX, edgeVertsY, true, tcsData);
    ibTop[TOPRIGHT] = CreateIndices(edgeVertsX, edgeVertsY, true, tcsData);
    ibTop[BOTTOMLEFT] = CreateIndices(edgeVertsX, edgeVertsY, true, tcsData);
    ibTop[BOTTOMRIGHT] = CreateIndices(edgeVertsX, edgeVertsY, true, tcsData);

    ibBottom[TILABLE] = CreateIndices(dim - (edgeVertsX - 1) * 2, dim - (edgeVertsY - 1) * 2, false, tcsData);
    ibBottom[LEFT] = CreateIndices(edgeVertsX, dim - (edgeVertsY - 1) * 2, false, tcsData);
    ibBottom[RIGHT] = CreateIndices(edgeVertsX, dim - (edgeVertsY - 1) * 2, false, tcsData);
    ibBottom[TOP] = CreateIndices(dim - (edgeVertsX - 1) * 2, edgeVertsY, false, tcsData);
    ibBottom[BOTTOM] = CreateIndices(dim - (edgeVertsX - 1) * 2, edgeVertsY, false, tcsData);
    ibBottom[TOPLEFT] = CreateIndices(edgeVertsX, edgeVertsY, false, tcsData);
    ibBottom[TOPRIGHT] = CreateIndices(edgeVertsX, edgeVertsY, false, tcsData);
    ibBottom[BOTTOMLEFT] = CreateIndices(edgeVertsX, edgeVertsY, false, tcsData);
    ibBottom[BOTTOMRIGHT] = CreateIndices(edgeVertsX, edgeVertsY, false, tcsData);

    // initialize the shader if not initialized already
    tcsData->GetStratusShader();
}

StratusCloud::~StratusCloud()
{
    Renderer* renderer = tcsData->GetRenderer();

    if (mieTexture) {
        renderer->ReleaseTexture(mieTexture);
    }

    for (int i = 0; i < NUM_SECTIONS; i++) {
        if (vb[i]) {
            SL_DELETE vb[i];
        }

        if (textures[i]) {
            renderer->ReleaseTexture(textures[i]);
        }

        if (ibTop[i]) {
            SL_DELETE ibTop[i];
        }

        if (ibBottom[i]) {
            SL_DELETE ibBottom[i];
        }
    }
}

bool StratusCloud::IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    return false;
}

bool StratusCloud::Draw(int pass, const Vector3 &lightPos, const Vector3& lightDir,
                        const Color &lightColor, bool invalid, const Sky *sky, bool, const Camera* camera, ThreadCameraStreamData* tcsData)
{
    lightDirection = lightDir;
    sunColor = Vector3(lightColor.r, lightColor.g, lightColor.b);

    if (pass > 0) {
        Renderer* renderer = tcsData->GetRenderer();
        if (!(renderer->GetFogEnabled() && renderer->GetFogDensity() > fogCutoff)) {
            renderer->SubmitBlendedObject(this);
        }
    }

    return true;
}

void StratusCloud::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                                     , ThreadCameraStreamData* tcsData) const
{
    Renderer* renderer = tcsData->GetRenderer();

    bool onlyFadeBottom = false;
    Configuration::GetBoolValue("stratus-only-fade-bottom", onlyFadeBottom);

    Vector3 camPos = overriddenCameraPos.GetCamPos();
    camPos = camPos * sceneCamera->GetInverseBasis3x3();
    Vector3 center = GetWorldPosition(tcsData) * sceneCamera->GetInverseBasis3x3();

    bool viewingFromEdge = false;
    double cloudHeight = 0;

    MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();

    CloudLayer* parentCloudLayer = GetParentCloudLayer();
    CloudLayerTcsUserData* tcsUserData = parentCloudLayer->GetOrCreateCloudLayerTcsUserData(tcsData);

    Color originalAmbient;
    Vector3 forcedAmbient;
    bool ambientOverride = false, doLighting;
    if (parentCloudLayer->GetOverriddenCloudColor(forcedAmbient, doLighting)) {
        originalAmbient = metaballParams->GetAmbientColor();
        Color forcedColor = Color(forcedAmbient.x, forcedAmbient.y, forcedAmbient.z);
        if (doLighting) {
            forcedColor = forcedColor * originalAmbient;
        }
        metaballParams->SetAmbientColor(forcedColor);
        tcsData->GetCloudRenderingParams()->SetAmbientColor(forcedColor);
    }

    if (!parentCloudLayer->GetIsInfinite()) {
        double layerX, layerZ;
        parentCloudLayer->GetLayerPosition(layerX, layerZ, tcsData);

        Vector3 camPosRelative = parentCloudLayer->GetLocalBasis(tcsUserData) * overriddenCameraPos.GetCamPos();

        viewingFromEdge = ((camPosRelative.x < (layerX - parentCloudLayer->GetBaseWidth() * 0.5)) ||
                           (camPosRelative.x >(layerX + parentCloudLayer->GetBaseWidth() * 0.5)) ||
                           (camPosRelative.z > (layerZ + parentCloudLayer->GetBaseLength() * 0.5)) ||
                           (camPosRelative.z < (layerZ - parentCloudLayer->GetBaseLength() * 0.5)));
        cloudHeight = center.y;
    } else {
        cloudHeight = tcsUserData->geocentricAltitude;
    }

    bool drawTop = (camPos.y >= cloudHeight + depth);
    bool drawBottom = (camPos.y <= cloudHeight || (viewingFromEdge && camPos.y < cloudHeight + depth) );

    //bool drawTop = (camPos.y >= cloudHeight);
    //bool drawBottom = (camPos.y < cloudHeight + depth);

    if (!(drawTop || drawBottom)) return;

    bool alwaysWriteDepth = false;

    float writeDepthDensityTest = 0.9f;
    Configuration::GetFloatValue("stratus-write-depth-density-test", writeDepthDensityTest);

    if (parentCloudLayer->GetDensity() > writeDepthDensityTest) {
        Configuration::GetBoolValue("stratus-always-write-depth", alwaysWriteDepth);
    }

    renderer->EnableBlending(SRCALPHA, INVSRCALPHA);
    renderer->EnableDepthReads(true);
    renderer->EnableDepthWrites(alwaysWriteDepth ? true : drawTop);
    renderer->EnableTexture2D(true);
    renderer->EnableLighting(false);
    renderer->EnableBackfaceCulling(viewingFromEdge ? false : true);

    bool setFog = false;

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    if (!renderer->GetFogEnabled()) {
        renderer->EnableFog(true);
        renderer->ConfigureFog(billboardParams->GetFogDensity(), 1, 100000, billboardParams->GetFogColor());
        setFog = true;
    }

    double fadeFactor = 0.0;

    ShaderHandle stratusShader = tcsData->GetStratusShader()->GetShader();

    renderer->IncrementConstantBuffersOffset(stratusShader);

    if (stratusShader) {
        const Color fogColor = billboardParams->GetFogColor();
        const Vector4 fog(fogColor.r, fogColor.g, fogColor.b, billboardParams->GetFogDensity());

        Vector4 cloudTint(lightScale, lightScale, lightScale, 1.0);
        Color forceColor;
        if (billboardParams->GetForceConstantColor(forceColor))
            cloudTint = forceColor.ToVector4();

        const Color ambientColor = metaballParams->GetAmbientColor();

        double coverage = parentCloudLayer->GetDensity();
        if (coverage > 1.0) coverage = 1.0;
        double xmit = exp(-(coverage*coverage));

        const Vector4 groundColor = Vector4(ambientColor.r * groundColorScale.x * xmit,
                                            ambientColor.g * groundColorScale.y * xmit,
                                            ambientColor.b * groundColorScale.z * xmit, 1.0);

        if (parentCloudLayer->GetIsInfinite() && (parentCloudLayer->GetFadeTowardEdges() && fadeOverride)) {
            fadeFactor = fadeFalloff;
        }

        ScopedConstantPrep prep(renderer, stratusShader, false);

        if (renderer->SupportsConstantLocations()) {
            const StratusShaderConstants& shaderConstants = tcsData->GetStratusShader()->GetConstantLocations();
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_fogColorAndDensityLoc, fog);
            renderer->SetConstantVectorAtLocation(stratusShader,  shaderConstants.sl_outputScaleLoc, atmosphere->GetOutputScale());
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_cloudTintLoc, cloudTint);
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_lightDirectionLoc, lightDirection);
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_sunColorLoc, sunColor);

            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_skyColorLoc, Vector3(ambientColor.r,
                                                   ambientColor.g, ambientColor.b) * skyColorScale);

            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_layerSizeAndUnitScaleLoc, Vector4(
                    width, height, Atmosphere::GetUnitScale(), fadeFactor));

            renderer->SetConstantMatrix4AtLocation(stratusShader, shaderConstants.sl_invBasisLoc, sceneCamera->GetInverseBasis4x4());

            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_groundColorLoc, groundColor);
        } else {
            renderer->SetConstantVector4(stratusShader, "sl_fogColorAndDensity", fog);
            renderer->SetConstantVector(stratusShader, "sl_outputScale", atmosphere->GetOutputScale());
            renderer->SetConstantVector4(stratusShader, "sl_cloudTint", cloudTint);
            renderer->SetConstantVector4(stratusShader, "sl_lightDirection", lightDirection);
            renderer->SetConstantVector4(stratusShader, "sl_sunColor", sunColor);

            renderer->SetConstantVector4(stratusShader, "sl_skyColor", Vector3(ambientColor.r,
                                         ambientColor.g, ambientColor.b) * skyColorScale);

            renderer->SetConstantVector4(stratusShader, "sl_layerSizeAndUnitScale", Vector4(
                                             width, height, Atmosphere::GetUnitScale(), fadeFactor));

            renderer->SetConstantMatrix(stratusShader, "sl_invBasis", sceneCamera->GetInverseBasis4x4());

            renderer->SetConstantVector4(stratusShader, "sl_groundColor", groundColor);
        }
    }

    Vector3 modelOffset;
    Vector3 texOffset;

    Camera adjustedCam(renderCamera, true, false);
    if (parentCloudLayer->GetIsInfinite()) {
        // Translate to the Camera position
        Matrix4 xlate;

        Vector3 offset;

        camPos = sceneCamera->GetPosition();
        camPos = camPos * sceneCamera->GetInverseBasis3x3();

        //offset.x = -fmod(camPos.x, segmentWidth) + fmod(center.x, segmentWidth);
        offset.y = tcsUserData->geocentricAltitude - camPos.y;
        //offset.z = -fmod(camPos.z, segmentHeight) + fmod(center.z, segmentHeight);

        double dx = center.x - camPos.x;
        double dz = center.z - camPos.z;
        offset.x = fmod(dx, segmentWidth);
        offset.z = fmod(dz, segmentWidth);

        if (dx > 0) offset.x -= segmentWidth;
        if (dz > 0) offset.z -= segmentWidth;

        offset = offset * sceneCamera->GetBasis3x3();

        xlate.elem[0][3] = offset.x;
        xlate.elem[1][3] = offset.y;
        xlate.elem[2][3] = offset.z;

        Matrix4 view = adjustedCam.GetModelViewMatrix();
        view.elem[0][3] = view.elem[1][3] = view.elem[2][3] = 0;
        adjustedCam.SetModelViewMatrix(view * xlate * sceneCamera->GetInverseBasis4x4());
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());

        modelOffset = offset + overriddenCameraPos.GetCamPos();
    } else {
        // Translate to the cloud position
        Matrix4 xlate;
        Vector3 worldPos = GetWorldPosition(tcsData);
        xlate.elem[0][3] = worldPos.x;
        xlate.elem[1][3] = worldPos.y;
        xlate.elem[2][3] = worldPos.z;

        modelOffset = worldPos;
        Matrix4 localBasis = parentCloudLayer->GetLocalBasis4(tcsUserData);
        Matrix4 invBasis = localBasis.Inverse();

        adjustedCam.MultiplyModelViewMatrix(xlate * invBasis);
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());
    }

    double scudThickness;
    Configuration::GetDoubleValue("stratus-scud-thickness", scudThickness);
    scudThickness *= Atmosphere::GetUnitScale();

    float extinctionFactor = 1.0f;
    Configuration::GetFloatValue("stratus-extinction-factor", extinctionFactor);

    bool flatTops = false;
    Configuration::GetBoolValue("stratus-flat-tops", flatTops);

    double minNSrc = (depth - scudThickness) / depth;
    if (parentCloudLayer->GetDensity() < 1.0) minNSrc = 0;

    if (stratusShader) {
        renderer->BindShader(stratusShader);
        const Vector3 localUp = parentCloudLayer->GetLocalUpVector(tcsUserData);
        const Vector3 up = localUp * scudThickness;
        const Vector4 dvc(up.x, up.y, up.z, 1.0);
        const Vector4 vfade(fade * alpha, 1.0, parentCloudLayer->GetDensity() > 1.0 ? 1.0 : parentCloudLayer->GetDensity(), 0.0);//textureDensity == 1.0 ? 1.0 : 0.0, 0.0, 0.0);
        //if (parentCloudLayer->GetIsInfinite()) {
        //    vfade.y = 0.0;
        //}
        const Vector4 upThickness(localUp.x, localUp.y, localUp.z,
                                  flatTops ? 0 : depth);

        if (renderer->SupportsConstantLocations()) {
            const StratusShaderConstants& shaderConstants = tcsData->GetStratusShader()->GetConstantLocations();
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_displacementVectorAndContrastLoc,  dvc);
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_fadeAndDisplacementFactorLoc,  vfade);
            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_upVectorAndThicknessLoc,  upThickness);

            renderer->SetConstantVectorAtLocation(stratusShader, shaderConstants.sl_extinctionFactorLoc,  Vector3(extinctionFactor, minNSrc, 0.0f));
            renderer->SetConstantMatrix4AtLocation(stratusShader, shaderConstants.sl_modelViewProjLoc,  adjustedCam.GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantMatrix4AtLocation(stratusShader, shaderConstants.sl_modelViewLoc,  adjustedCam.GetModelViewMatrixFloatArray());
            renderer->SetConstantVectorAtLocation(stratusShader, shaderConstants.sl_modelPosLoc,  modelOffset * sceneCamera->GetInverseBasis3x3());
            renderer->SetConstantVectorAtLocation(stratusShader, shaderConstants.sl_cameraPosLoc,  overriddenCameraPos.GetCamPos());
        } else {
            renderer->SetConstantVector4(stratusShader, "sl_displacementVectorAndContrast", dvc);
            renderer->SetConstantVector4(stratusShader, "sl_fadeAndDisplacementFactor", vfade);
            renderer->SetConstantVector4(stratusShader, "sl_upVectorAndThickness", upThickness);

            renderer->SetConstantVector(stratusShader, "sl_extinctionFactor", Vector3(extinctionFactor, minNSrc, 0.0f));
            renderer->SetConstantMatrix(stratusShader, "sl_modelViewProj", adjustedCam.GetModelViewProjectionMatrixFloatArray());
            renderer->SetConstantMatrix(stratusShader, "sl_modelView", adjustedCam.GetModelViewMatrixFloatArray());
            renderer->SetConstantVector(stratusShader, "sl_modelPos", modelOffset * sceneCamera->GetInverseBasis3x3());
            renderer->SetConstantVector(stratusShader, "sl_cameraPos", overriddenCameraPos.GetCamPos());
        }
    }

    int i;
    for (i = 0; i < NUM_SECTIONS; i++) {
        if (textures[i] && vb[i] && ibTop[i]) {

            if (stratusShader) {
                if (renderer->SupportsConstantLocations()) {
                    const StratusShaderConstants& shaderConstants = tcsData->GetStratusShader()->GetConstantLocations();
                    renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_layerThicknessAndIsTopLoc, Vector4(depth, scudThickness, depth, 1.0));


                    if (onlyFadeBottom) {
                        renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_layerSizeAndUnitScaleLoc, Vector4(
                                width, height, Atmosphere::GetUnitScale(), 0.0));
                    }
                } else {
                    renderer->SetConstantVector4(stratusShader, "sl_layerThicknessAndIsTop", Vector4(depth, scudThickness, depth, 1.0));

                    if (onlyFadeBottom) {
                        renderer->SetConstantVector4(stratusShader, "sl_layerSizeAndUnitScale", Vector4(
                                                         width, height, Atmosphere::GetUnitScale(), 0.0));
                    }
                }
            }

            renderer->EnableTexture(textures[i], 0);
            renderer->EnableTexture(mieTexture, 1);
            renderer->BindShader(stratusShader);

            if (drawTop) {
                vb[i]->DrawIndexedStrip(*ibTop[i], true);
            }
        }
    }

    if (drawBottom) {
        for (i = 0; i < NUM_SECTIONS; i++) {
            if (textures[i] && vb[i] && ibBottom[i]) {
                renderer->EnableTexture(textures[i], 0);
                renderer->EnableTexture(mieTexture, 1);
                renderer->BindShader(stratusShader);

                if (stratusShader) {

                    if (renderer->SupportsConstantLocations()) {
                        const StratusShaderConstants& shaderConstants = tcsData->GetStratusShader()->GetConstantLocations();
                        renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_layerThicknessAndIsTopLoc, Vector4(depth, scudThickness, camPos.y > cloudHeight ? 1.0 : 0.0, 0.0));

                        const Vector3 localUp = parentCloudLayer->GetLocalUpVector(tcsUserData);
                        Vector4 v(localUp.x, localUp.y, localUp.z, 0);
                        renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_upVectorAndThicknessLoc, v);
                        v.x = v.y = v.z = 0;
                        v.w = 1.0;
                        renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_displacementVectorAndContrastLoc, v);

                        if (onlyFadeBottom) {
                            renderer->SetConstantVector4AtLocation(stratusShader, shaderConstants.sl_layerSizeAndUnitScaleLoc, Vector4(
                                    width, height, Atmosphere::GetUnitScale(), fadeFactor));
                        }
                    } else {
                        renderer->SetConstantVector4(stratusShader, "sl_layerThicknessAndIsTop", Vector4(depth, scudThickness, camPos.y > cloudHeight ? 1.0 : 0.0, 0.0));

                        const Vector3 localUp = parentCloudLayer->GetLocalUpVector(tcsUserData);
                        Vector4 v(localUp.x, localUp.y, localUp.z, 0);
                        renderer->SetConstantVector4(stratusShader, "sl_upVectorAndThickness", v);
                        v.x = v.y = v.z = 0;
                        v.w = 1.0;
                        renderer->SetConstantVector4(stratusShader, "sl_displacementVectorAndContrast", v);

                        if (onlyFadeBottom) {
                            renderer->SetConstantVector4(stratusShader, "sl_layerSizeAndUnitScale", Vector4(
                                                             width, height, Atmosphere::GetUnitScale(), fadeFactor));
                        }
                    }
                }

                vb[i]->DrawIndexedStrip(*ibBottom[i], true);
            }
        }
    }

    if (stratusShader) {
        renderer->UnbindShader();
    }

    renderer->SetCurrentColor(Color(1,1,1));

    if (setFog) {
        renderer->EnableFog(false);
    }

    //ren->EnableBackfaceCulling(true);

    renderer->EnableDepthWrites(true);

    renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());
}

bool StratusCloud::Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
{
    return false;
}

void StratusCloud::GetSize(double& pwidth, double& pdepth, double& pheight) const
{
    pwidth = width;
    pdepth = height;
    pheight = depth;
}
