// Copyright (c) 2010-2023 Sundog Software LLC. All rights reserved worldwide.

#include "StratocumulusCloudLayer.h"
#include "StratocumulusCloud.h"
#include "Configuration.h"
#include "Vector4.h"
#include "CloudGenerator.h"
#include "CloudGeneratorFactory.h"
#include "MathUtils.h"
#include "Metaball.h"
#include "ResourceLoader.h"
#include "Camera.h"
#include "OverriddenCameraPos.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "ThreadCameraStreamData.h"
#include "CloudLayerTcsUserData.h"
#include "TextureManager.h"
#include "StratocumulusShaders.h"
#include "Renderable.h"
#include "Mutex.h"
#include "SLAssert.h"
#include "Utils.h"

using namespace std;

#define HAS_CLOUD_BIT 0x01
#define PHASE_TRANSITION_BIT 0x02
#define VAPOR_BIT 0x04
#define BILINEAR_LIGHTING 1
#define COLOR_RANDOMNESS 0.1

//#define SDF

// The minimum voxel height with a cloud in it, to prevent clamping artifacts
#define MIN_HEIGHT 1

namespace SilverLining
{

StratocumulusCloudLayer::StratocumulusCloudLayer(const Atmosphere& atmosphere)
    : CloudLayer(atmosphere),
      voxels(0), sdfData(0),
      volumeLightingVectorFullRefreshTolerance(cos(1.0 * (MathUtils::Pi() / 180))),
      volumeLightingVectorSliceRefreshTolerance(cos(0.1 * (MathUtils::Pi() / 180))),
      inscatteringTerm(0.75),
      coverageMap(0),
      Renderable(true)
{
    Configuration::GetDoubleValue("stratocumulus-voxel-dimension", voxelDimension);
    if (voxelDimension < 1.0) {
        printf("warning: stratocumulus-voxel-dimension < 1. Verify SilverLining.config value is correct. Clamping to 1\n");
        voxelDimension = 1.0;
    }
    voxelDimension *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("stratocumulus-droplet-size", dropletSize);
    dropletSize *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("stratocumulus-droplets-per-cubic-cm", dropletsPerCubicCm);
    dropletsPerCubicCm /= pow(Atmosphere::GetUnitScale(), 3.0);
    Configuration::GetDoubleValue("stratocumulus-albedo", albedo);
    Configuration::GetDoubleValue("stratocumulus-extinction-probability", extinctionProbability);
    Configuration::GetDoubleValue("stratocumulus-transition-probability", transitionProbability);
    Configuration::GetDoubleValue("stratocumulus-vapor-probability", vaporProbability);
    Configuration::GetIntValue("stratocumulus-initial-evolve", initialEvolve);
    Configuration::GetDoubleValue("stratocumulus-skylight-term", ambientScattering);
    Configuration::GetDoubleValue("stratocumulus-inscattering-term", inscatteringTerm);
    Configuration::GetDoubleValue("stratocumulus-light-sampling-distance", lightSamplingDistance);
    lightSamplingDistance *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("stratocumulus-multiple-scattering-term", multipleScatteringTerm);
    Configuration::GetDoubleValue("stratocumulus-jitter", jitter);
    jitter *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("stratocumulus-max-size", maxSize);
    maxSize *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("stratocumulus-fade-falloff", fadeFalloff);
    lightExp = 1.0;
    Configuration::GetDoubleValue("stratocumulus-light-exponent", lightExp);

    cloudMotion = Vector3(0, 0, 0);

    updateCloudsWithWind = false;
    Configuration::GetBoolValue("update-stratocumulus-clouds-with-wind", updateCloudsWithWind);
}

StratocumulusCloudLayer::~StratocumulusCloudLayer()
{
    FreeVoxels();

    if (sdfData) SL_DELETE[] sdfData;
}

class StratocumulusCloudLayerTcsUserData : public TcsUserData
{
public:
    StratocumulusCloudLayerTcsUserData(CloudLayer* parentLayer, ThreadCameraStreamData* tcsData);
    virtual ~StratocumulusCloudLayerTcsUserData();
public:
    bool    invalid;
    Matrix3 lastBasis;
    Vector3 cameraMotion, lastCameraPos;

    Vector3 originTexCoords;

    Color lightColor;
    Vector3 lightWorldPos;
    Vector3 lightObjectPos;

    Vector3 volumeLightingVector;

    int     volumeLightingSlice;

    unsigned char* cloudTexData;
    float* randomData;

    CloudLayer* parentCloudLayer;
    ThreadCameraStreamData* tcsData;

    // we get and set this in DrawBlendedObject lazily (if null)
    // Its constant for all layers for a given atmosphere
    // just make this mutable
    TextureHandle noiseTexture;

    VertexBuffer *vb;
    IndexBuffer *ib;

    TextureHandle volumeData, sdfVolumeData;
};

StratocumulusCloudLayerTcsUserData::StratocumulusCloudLayerTcsUserData(CloudLayer* _parentCloudLayer, ThreadCameraStreamData* _tcsData)
    : invalid(true)
    , volumeLightingVector(0, 0, 0)
    , volumeLightingSlice(0)
    , cloudTexData(0)
    , randomData(0)
    , noiseTexture(0)
    , parentCloudLayer(_parentCloudLayer)
    , tcsData(_tcsData)
    , vb(0), ib(0)
    , volumeData(0), sdfVolumeData(0)
{
    // lastBasis - initialize
    for (int m = 0; m < 3; m++) {
        for (int n = 0; n < 3; n++) {
            if (n == m)
                lastBasis.elem[m][n] = 1.0;
            else
                lastBasis.elem[m][n] = 0.0;
        }
    }

    TextureManager* textureManager = 0;
    if (parentCloudLayer && parentCloudLayer->GetAtmosphere().TexturesAreShared()) {
        textureManager = parentCloudLayer->GetAtmosphere().GetDefaultTcsData()->GetTextureManager();
    } else {
        textureManager = tcsData->GetTextureManager();
    }

    SL_ASSERT(textureManager);
    noiseTexture = textureManager->GetNoiseTexture();

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = 0;

    BufferProperties bufferProperties;
    bufferProperties.genBuffer = true;
    bufferProperties.dynamic = true;

    name = (enableObjectLabeling) ? "StratocumulusCloudLayer::VertexBuffer" : (const char *)0;
    // Allocate geometry and indices for bounding box.
    if (tcsData->GetRenderer()->GetIsVulkan()) {
        bufferProperties.numOfPossibleOffsets = 50; // actually, we possibly need only 1 + 6 + 1 (regular + env map + shadow) for each stream, but give 50 just in case
        bufferProperties.frameIndexed = true;
        vb = SL_NEW VertexBuffer(8, name, tcsData, bufferProperties);
    } else {
        vb = SL_NEW VertexBuffer(8, name, tcsData, bufferProperties);
    }

    name = (enableObjectLabeling) ? "StratocumulusCloudLayer::IndexBuffer" : (const char *)0;
    ib = SL_NEW IndexBuffer(4 * 6 * 2, name, tcsData, bufferProperties);
}

StratocumulusCloudLayerTcsUserData::~StratocumulusCloudLayerTcsUserData()
{
    if (vb) SL_DELETE vb;
    if (ib) SL_DELETE ib;

    if (cloudTexData) SL_DELETE[] cloudTexData;
#ifdef COLOR_RANDOMNESS
    if (randomData) SL_DELETE[] randomData;
#endif  // COLOR_RANDOMNESS

    Renderer* renderer = tcsData->GetRenderer();

    if (volumeData) renderer->ReleaseTexture(volumeData);
    if (sdfVolumeData) renderer->ReleaseTexture(sdfVolumeData);
}

StratocumulusCloudLayerTcsUserData* StratocumulusCloudLayer::GetOrCreateTcsUserData(ThreadCameraStreamData* tcsData) const
{
    if (tcsData == 0) {
        return 0;
    }
    TcsUserData* userData = tcsData->GetUserData(this);
    if (userData == 0) {
        userData = SL_NEW StratocumulusCloudLayerTcsUserData(const_cast<StratocumulusCloudLayer*>(this), tcsData);
        tcsData->SetUserData(this, userData);
    }
    return (StratocumulusCloudLayerTcsUserData*)userData;
}

void StratocumulusCloudLayer::SetBaseWidth(double meters)
{
    if (meters > maxSize) meters = maxSize;
    CloudLayer::SetBaseWidth(meters);
}

void StratocumulusCloudLayer::SetBaseLength(double meters)
{
    if (meters > maxSize) meters = maxSize;
    CloudLayer::SetBaseLength(meters);
}

void StratocumulusCloudLayer::MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData)
{
    const bool wrappedCloud = (isInfinite || cloudWrap);

    const Vector3 v(x, y, z);
    const Vector3 localV = v * tcsData->GetCamera()->GetInverseBasis3x3();

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    // Update the world position of each cloud
    if (updateCloudsWithWind) {
        const SL_VECTOR(Cloud*)& clouds = tcsUserData->clouds;
        for (SL_VECTOR(Cloud *)::const_iterator it = clouds.begin(); it != clouds.end(); it++) {

            Cloud* cloud = *it;

            cloud->SetWorldPosition(cloud->GetWorldPosition(tcsData) + v);

            // If cloud layer is wrapped, update the position of each cloud
            // relative to the layer
            if (wrappedCloud) {
                Vector3 layerPos = cloud->GetLayerPosition() + localV;
                cloud->SetLayerPosition(layerPos);
            }
        }
    }

    if (!wrappedCloud) {
        tcsUserData->layerX += localV.x;
        tcsUserData->layerZ += localV.z;
        tcsUserData->baseAltitude += localV.y;
    } else {
        cloudMotion = cloudMotion + Vector3(x, y, z);
    }

    noiseOffset = noiseOffset + Vector3(x, y, z);
}

void StratocumulusCloudLayer::FreeVoxels()
{
    if (voxels) {
        int i, j, k;

        for (i = 0; i < voxelWidth; i++) {
            for (j = 0; j < voxelDepth; j++) {
                for (k = 0; k < voxelHeight; k++) {
                    SL_DELETE voxels[i][j][k];
                }
                SL_FREE(voxels[i][j]);
            }
            SL_FREE(voxels[i]);
        }
        SL_FREE(voxels);
    }
    voxels = 0;
}

void StratocumulusCloudLayer::UploadSDF(ThreadCameraStreamData* tcsData)
{
    unsigned int texBytes = (voxelWidth * voxelHeight * voxelDepth) * sizeof(float);
    sdfData = SL_NEW float[texBytes];
    float *p = sdfData;

    int maxDim = voxelWidth;
    if (voxelHeight > maxDim) maxDim = voxelHeight;
    if (voxelDepth > maxDim) maxDim = voxelDepth;
    int halfMaxDim = maxDim / 2;

    for (int h = 0; h < voxelHeight; h++) {
        for (int d = 0; d < voxelDepth; d++) {
            for (int w = 0; w < voxelWidth; w++) {
                if (voxels[w][d][h]->states & HAS_CLOUD_BIT) {
                    *p++ = -1.0f;
                } else {
                    float dist = 0;
                    for (int radius = 1; radius < halfMaxDim; radius++) {
                        if (dist > 0) break;
                        for (int hh = h - radius; hh <= h + radius; hh++) {
                            if (hh >= 0 && hh < voxelHeight) {
                                for (int dd = d - radius; dd <= d + radius; dd++) {
                                    if (dd >= 0 && dd < voxelDepth) {
                                        for (int ww = w - radius; ww <= w + radius; ww++) {
                                            if (ww >= 0 && ww < voxelWidth) {
                                                if (voxels[ww][dd][hh]->states & HAS_CLOUD_BIT) {
                                                    /*
                                                    float wDist = (w - ww);
                                                    if (wDist >= 1.0f) wDist -= 1.0f;
                                                    float dDist = (d - dd);
                                                    if (dDist >= 1.0f) dDist -= 1.0f;
                                                    float hDist = (h - hh);
                                                    if (hDist >= 1.0f) hDist -= 1.0f;
                                                    float wN = wDist / voxelWidth;
                                                    float dN = dDist / voxelDepth;
                                                    float hN = hDist / voxelHeight;
                                                    dist = sqrtf(wN*wN + dN*dN + hN*hN);
                                                    */
                                                    dist = (float)((radius - 1) * voxelDimension);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (dist > 0) {
                        *p++ = dist;
                    } else {
                        *p++ = -1.0f;
                    }
                }
            }
        }
    }

    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    TextureHandle& sdfVolumeData = tcsUserData->sdfVolumeData;

    Renderer* renderer = tcsData->GetRenderer();
    if (sdfVolumeData) renderer->ReleaseTexture(sdfVolumeData);
    sdfVolumeData = 0;

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling) ? "SilverLining::Stratocumulus Cloud Layer SDF Texture" : (const char *)0;

    // Warning: only implemented in OpenGL renderer right now
    renderer->Load3DTextureFloat(sdfData, voxelWidth, voxelDepth, voxelHeight, &sdfVolumeData, true, true, false, name);
}

void StratocumulusCloudLayer::UploadVolumeTexture(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    StratocumulusCloudLayerTcsUserData* stratoTcsUserData = GetOrCreateTcsUserData(tcsData);
    unsigned char*& cloudTexData = stratoTcsUserData->cloudTexData;
    float*& randomData = stratoTcsUserData->randomData;

    unsigned int texBytes = (voxelWidth * voxelHeight * voxelDepth) * sizeof(unsigned char);
    cloudTexData = SL_NEW unsigned char[texBytes * 2];
    unsigned char *p = cloudTexData;

#ifdef COLOR_RANDOMNESS
    randomData = SL_NEW float[voxelWidth * voxelHeight  * voxelDepth];
    float *r = randomData;
#endif  // COLOR_RANDOMNESS

    //int renType = renderer->GetType();

    for (int h = 0; h < voxelHeight; h++) {
        for (int d = 0; d < voxelDepth; d++) {
            for (int w = 0; w < voxelWidth; w++) {
                float col = (voxels[w][d][h]->states & HAS_CLOUD_BIT) ? 1.0f : 0.0f;
                *p++ = (unsigned char)(col * 255.0f);
                *p++ = 0;

#ifdef COLOR_RANDOMNESS
                float rnd = rng->UniformRandomFloat();
                *r++ = 1.0f - rnd * (float)COLOR_RANDOMNESS;
#endif  // COLOR_RANDOMNESS
            }
        }
    }

    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    TextureHandle& volumeData = tcsUserData->volumeData;

    Renderer* renderer = tcsData->GetRenderer();
    if (volumeData) renderer->ReleaseTexture(volumeData);
    volumeData = 0;

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = (enableObjectLabeling) ? "SilverLining::Stratocumulus Cloud Layer Volume Texture" : (const char *)0;

    bool wrap = GetIsInfinite();
    renderer->Load3DTextureLA(cloudTexData, voxelWidth, voxelDepth, voxelHeight, &volumeData, wrap, wrap, false, name);
}

void StratocumulusCloudLayer::TrimExcessVoxels()
{
    int i, j, k;

    int maxH = 0;

    for (i = 0; i < voxelWidth; i++) {
        for (j = 0; j < voxelDepth; j++) {
            for (k = 0; k < voxelHeight; k++) {
                if (voxels[i][j][k]->states & HAS_CLOUD_BIT) {
                    if (k > maxH) maxH = k;
                }
            }
        }
    }

    if (maxH < voxelHeight - 1 - MIN_HEIGHT) {
        int newVoxelHeight = maxH + 1 + MIN_HEIGHT;
        for (i = 0; i < voxelWidth; i++) {
            for (j = 0; j < voxelDepth; j++) {
                StratocumulusVoxel **newVoxels = (StratocumulusVoxel**)(SL_MALLOC(newVoxelHeight * sizeof(StratocumulusVoxel*)));

                for (k = 0; k < newVoxelHeight; k++)
                    newVoxels[k] = voxels[i][j][k];

                for (k = newVoxelHeight; k < voxelHeight; k++)
                    SL_DELETE voxels[i][j][k];

                SL_FREE(voxels[i][j]);
                voxels[i][j] = newVoxels;
            }
        }

        voxelHeight = newVoxelHeight;
        SetThickness(voxelHeight * voxelDimension); // Comment this out to force cloud layer to fill specified thickness by vertically stretching
    }
}

Vector3 StratocumulusCloudLayer::GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    double x, y, z;
    CloudLayer::GetSortPosition(x, y, z, camera, tcsData);
    return Vector3(x, y, z);
}

double StratocumulusCloudLayer::GetDistance(const Vector3& from, const Vector3& to, const Matrix4& mv, bool rightHanded, const Camera* camera, ThreadCameraStreamData* tcsData) const
{
    if (tcsData->GetSortByScreenDepth()) {
        Vector3 clipSpace = mv * GetSortPosition(camera, tcsData);
        return rightHanded ? -clipSpace.z : clipSpace.z;
    } else {
        Vector3 V = from - GetSortPosition(camera, tcsData);
        return V.Length();
    }
}

void StratocumulusCloudLayer::CreateCloud(double cloudWidth, double cloudDepth, double cloudHeight, const Vector3& layerWorldPosition, const Atmosphere& atm, ThreadCameraStreamData* tcsData)
{
    if (cloudWidth > voxelDimension && cloudDepth > voxelDimension
            && cloudHeight > (voxelDimension * MIN_HEIGHT)) {
        unsigned int vWidth = (int)(cloudWidth / voxelDimension);
        unsigned int vHeight = (int)(cloudHeight / voxelDimension);
        unsigned int vDepth = (int)(cloudDepth / voxelDimension);

        unsigned int vHalfWidth = vWidth >> 1;
        //unsigned int vHalfHeight = vHeight >> 1;
        unsigned int vHalfDepth = vDepth >> 1;

        // Pick a random origin for a bounding ellipsoid
        RandomNumberGenerator* rng = atm.GetRandomNumberGenerator();

        int cw, cd;
        if (GetIsInfinite()) {
            cw = rng->UniformRandomIntRange(0, voxelWidth);
            cd = rng->UniformRandomIntRange(0, voxelDepth);
        } else {
            cw = rng->UniformRandomIntRange(vHalfWidth + 1, voxelWidth - vHalfWidth - 1);
            cd = rng->UniformRandomIntRange(vHalfDepth + 1, voxelDepth - vHalfDepth - 1);
        }
        int ch = MIN_HEIGHT;


        // Keep track of each cloud in the model so we can expose their individual bounding volume info
        Vector3 posInLayer(cw * voxelDimension, ch * voxelDimension, cd * voxelDimension);
        posInLayer = posInLayer - Vector3(GetBaseWidth() / 2.0, 0.0, GetBaseLength() / 2.0);
        Vector3 posInWorld = (layerWorldPosition + posInLayer) * tcsData->GetCamera()->GetBasis3x3();
        StratocumulusCloud* cloud = SL_NEW StratocumulusCloud(this, atm.GetRandomNumberGenerator()->UniformRandomDouble());
        cloud->SetWorldPosition(posInWorld);
        cloud->SetLayerPosition(posInLayer);
        cloud->SetSize(cloudWidth, cloudDepth, cloudHeight);
        AddCloud(cloud, tcsData);
        AddToCoverage(cloud, tcsData);

        for (int w = cw - (int)vHalfWidth; w < cw + (int)vHalfWidth; w++) {
            for (int d = cd - (int)vHalfDepth; d < cd + (int)vHalfDepth; d++) {
                for (int h = ch; h < ch + (int)vHeight; h++) {
                    if (w >= 0 && d >= 0 && h >= 0 && w < voxelWidth && d < voxelDepth && h < voxelHeight) {
                        double x = (double)(w - cw);
                        double y = (double)(d - cd);
                        double z = (double)(h - ch);
                        double depth = 1.0 - ((x * x) / (vHalfWidth * vHalfWidth) +
                                              (y * y) / (vHalfDepth * vHalfDepth) + (z * z) / (vHeight * vHeight));
                        if (depth >= 0 && depth <= 1.0) {
                            voxels[w][d][h]->vaporProbability = (float)(vaporProbability * depth);
                            voxels[w][d][h]->phaseTransitionProbability = (float)(transitionProbability * depth);
                            voxels[w][d][h]->extinctionProbability = (float)(extinctionProbability * (1.0 - depth));
                        }
                    }
                }
            }
        }
    }
}

void StratocumulusCloudLayer::InitCoverageMap()
{
    coverageW = (int)(CloudLayer::GetBaseWidth() / voxelDimension);
    coverageL = (int)(CloudLayer::GetBaseLength() / voxelDimension);
    totalCoverageCells = coverageW * coverageL;
    cellsCovered = 0;

    if (coverageMap) SL_DELETE[] coverageMap;
    coverageMap = SL_NEW bool[totalCoverageCells];
    for (int i = 0; i < totalCoverageCells; i++) {
        coverageMap[i] = false;
    }
}

void StratocumulusCloudLayer::DeleteCoverageMap()
{
    if (coverageMap) SL_DELETE[] coverageMap;
    coverageMap = 0;
}

void StratocumulusCloudLayer::AddToCoverage(Cloud *cloud, ThreadCameraStreamData* tcsData)
{
    const Camera* camera = tcsData->GetCamera();
    Vector3 pos = cloud->GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();
    double layerX, layerZ;
    CloudLayer::GetLayerPosition(layerX, layerZ, tcsData);
    double x = pos.x - (layerX - CloudLayer::GetBaseWidth() * 0.5);
    double z = pos.z - (layerZ - CloudLayer::GetBaseLength() * 0.5);

    double cloudWidth, cloudDepth, cloudHeight;
    cloud->GetSize(cloudWidth, cloudDepth, cloudHeight);
    int cellsWide = (int)(cloudWidth / voxelDimension);
    int cellsDeep = (int)(cloudDepth / voxelDimension);

    int cellX = (int)(x / voxelDimension);
    int cellZ = (int)(z / voxelDimension);

    for (int xx = cellX - cellsWide / 2; xx < cellX + cellsWide / 2; xx++) {
        for (int z = cellZ - cellsDeep / 2; z < cellZ + cellsDeep / 2; z++) {
            int idx = xx + z * coverageW;
            if (idx >= 0 && idx < totalCoverageCells) {
                if (!coverageMap[idx]) {
                    cellsCovered++;
                }
                coverageMap[idx] = true;
            }
        }
    }

}

float StratocumulusCloudLayer::GetCoverage()
{
    return (float)cellsCovered / (float)totalCoverageCells;
}


bool StratocumulusCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    ClearClouds(tcsData);

    Vector3 layerWorldPosition;
    double x, z;
    GetLayerPosition(x, z, tcsData);
    layerWorldPosition = Vector3(x, GetBaseAltitude(tcsData), z);

    int i, j, k;

    // Allocate the voxels
    voxelWidth = (int)(GetBaseWidth() / voxelDimension);
    voxelHeight = (int)(GetThickness() / voxelDimension) + MIN_HEIGHT;
    voxelDepth = (int)(GetBaseLength() / voxelDimension);

    voxelWidth = SLMAX(voxelWidth, 1);
    voxelHeight = SLMAX(voxelHeight, 1);
    voxelDepth = SLMAX(voxelDepth, 1);

    voxels = (StratocumulusVoxel****)(SL_MALLOC(voxelWidth * sizeof(StratocumulusVoxel*)));
    for (i = 0; i < voxelWidth; i++) {
        voxels[i] = (StratocumulusVoxel***)(SL_MALLOC(voxelDepth * sizeof(StratocumulusVoxel*)));
        for (j = 0; j < voxelDepth; j++) {
            voxels[i][j] = (StratocumulusVoxel**)(SL_MALLOC(voxelHeight * sizeof(StratocumulusVoxel*)));
            for (k = 0; k < voxelHeight; k++) {
                voxels[i][j][k] = SL_NEW StratocumulusVoxel();
            }
        }
    }

    CloudGenerator *generator = CloudGeneratorFactory::Create("stratocumulus");
    if (generator) {
        generator->SetDesiredArea(GetBaseWidth() * GetBaseLength());

        double densityMultiplier = 1.0;
        Configuration::GetDoubleValue("stratocumulus-density", densityMultiplier);
        generator->SetDesiredCoverage(GetDensity() * densityMultiplier);

        generator->StartGeneration();

        InitCoverageMap();

        double cloudWidth, cloudDepth, cloudHeight;

        while (generator->GetNextCloud(cloudWidth, cloudDepth, cloudHeight, atmosphere.GetRandomNumberGenerator())) {
            CreateCloud(cloudWidth, cloudDepth, cloudHeight, layerWorldPosition, atmosphere, tcsData);
        }

        int maxExtraRuns = 1;
        Configuration::GetIntValue("stratocumulus-max-extra-generation-runs", maxExtraRuns);
        double densityPrecision = 0.05;
        Configuration::GetDoubleValue("stratocumulus-density-precision", densityPrecision);

        int extraRuns = 0;
        while (GetCoverage() < generator->GetDesiredCoverage() * (1.0 - densityPrecision)) {
            extraRuns++;
            if (extraRuns > maxExtraRuns) {
                break;
            }

            generator->StartGeneration();
            while (generator->GetNextCloud(cloudWidth, cloudDepth, cloudHeight, atmosphere.GetRandomNumberGenerator())) {
                CreateCloud(cloudWidth, cloudDepth, cloudHeight, layerWorldPosition, atmosphere, tcsData);
                if (GetCoverage() >= generator->GetDesiredCoverage()) {
                    break;
                }
            }
        }

        DeleteCoverageMap();

        SL_DELETE generator;
    }

    for (i = 0; i < initialEvolve; i++) {
        IncrementGrowth(atmosphere.GetRandomNumberGenerator());
    }

    TrimExcessVoxels();

    double thickFactor = 1.0;
    Configuration::GetDoubleValue("stratocumulus-thickness-multiplier", thickFactor);
    SetThickness(thickness * thickFactor);

    UploadVolumeTexture(atmosphere.GetRandomNumberGenerator(), tcsData);

#ifdef SDF
    UploadSDF();
#endif
    FreeVoxels();


    const Camera* camera = tcsData->GetCamera();
    Camera dummyCamera(camera->IsRightHanded());
    dummyCamera.SetUpVector(camera->GetUpVector());
    dummyCamera.SetRightVector(camera->GetRightVector());

    // initialize bounding box vertices before serialization.
    ComputeBoundingBox(&atmosphere, &dummyCamera, &dummyCamera, OverriddenCameraPos(Vector3(0, 0, 0)), tcsData);

    const int indicesBack[] = { 3, 7, 0, 4,
                                1, 5, 2, 6,
                                2, 3, 1, 0,
                                7, 6, 4, 5,
                                4, 5, 0, 1,
                                3, 2, 7, 6
                              };

    const int indicesFront[] = { 0, 4, 3, 7,
                                 2, 6, 1, 5,
                                 3, 2, 0, 1,
                                 4, 5, 7, 6,
                                 0, 1, 4, 5,
                                 7, 6, 3, 2
                               };

    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    VertexBuffer* vb = tcsUserData->vb;
    IndexBuffer* ib = tcsUserData->ib;
    if (ib->LockBuffer()) {
        Index *idx = ib->GetIndices();

        for (i = 0; i < 4 * 6; i++) {
            idx[i] = (Index)indicesFront[i];
        }

        for (i = 0; i < 4 * 6; i++) {
            idx[i + 4 * 6] = (Index)indicesBack[i];
        }

        ib->UnlockBuffer();
    }

    // initialize once
    tcsData->GetStratocumulusShaders()->GetShader();
    tcsData->GetStratocumulusShaders()->GetShaderHdr();

    tcsData->SetForceImmediate(false);

    return true;
}

Vector3 StratocumulusCloudLayer::GetWorldPosition(const ThreadCameraStreamData* tcsData) const
{
    double layerX, layerZ;
    double baseAlt;
    GetLayerPosition(layerX, layerZ, const_cast<ThreadCameraStreamData*>(tcsData));
    baseAlt = GetBaseAltitudeGeocentric(const_cast<ThreadCameraStreamData*>(tcsData));

    Vector3 layerPos(layerX, baseAlt, layerZ);

    const Camera* camera = tcsData->GetCamera();

    layerPos = layerPos * camera->GetBasis3x3();
    return layerPos;
}

void StratocumulusCloudLayer::ComputeTexCoords(Vertex *v, const Atmosphere *atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData) const
{
    // Get the cloud position in world coords
    double layerX, layerZ;
    double width, depth;
    double baseAlt, thickness;
    GetLayerPosition(layerX, layerZ, tcsData);

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    baseAlt = tcsUserData->geocentricAltitude;
    thickness = GetThickness();
    width = GetBaseWidth();
    depth = GetBaseLength();
    layerX -= width * 0.5;
    layerZ -= depth * 0.5;

    // Transform from eye coords to world coords
    Matrix4 modelview = renderCamera->GetModelViewMatrix();
    if (GetIsInfinite()) {
        modelview.elem[0][3] = modelview.elem[1][3] = modelview.elem[2][3] = 0;
    }
    modelview = modelview.Inverse();
    Vector4 pt(v->x, v->y, v->z, 1.0);
    pt = modelview * pt;

    // add in motion for stationary layers with wrapping clouds
    if (!GetIsInfinite()) {
        pt.x -= cloudMotion.x;
        pt.y -= cloudMotion.y;
        pt.z -= cloudMotion.z;
    }

    // Transform to standard basis
    Vector3 pt3(pt.x, pt.y, pt.z);
    pt3 = GetLocalBasis(tcsUserData) * pt3;

    // Now figure it out
    if (GetIsInfinite()) {
        Vector3 camPosYup = overriddenCameraPos.GetCamPos() * renderCamera->GetInverseBasis3x3();

        Vector3 camPos = (sceneCamera) ? sceneCamera->GetPosition() : Vector3(0, 0, 0);
        Vector3 offset = overriddenCameraPos.GetCamPos() - camPos;
        Vector3 offsetYup = offset * renderCamera->GetInverseBasis3x3();

        v->SetU((float)((pt3.x + offsetYup.x) / width) + 0.5f);
        v->SetV((float)((pt3.y - baseAlt + camPosYup.y) / thickness));
        v->SetT((float)((pt3.z + offsetYup.z) / depth) + 0.5f);
    } else {
        v->SetU((float)((pt3.x - layerX) / width));
        v->SetV((float)((pt3.y - baseAlt) / thickness));
        v->SetT((float)((pt3.z - layerZ) / depth));
    }
}

void StratocumulusCloudLayer::ComputeBoundingBox(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, ThreadCameraStreamData* tcsData) const
{
    // Construct a bounding box for the cloud layer in world coords
    Vector3 bbox[8];

    double layerX, layerZ;
    double width, depth;
    double baseAlt, thickness;
    GetLayerPosition(layerX, layerZ, tcsData);

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    baseAlt = tcsUserData->geocentricAltitude;
    thickness = GetThickness();
    width = GetBaseWidth();
    depth = GetBaseLength();

    StratocumulusCloudLayerTcsUserData* stratoTcsUserData = GetOrCreateTcsUserData(tcsData);
    Vector3& originTexCoords = stratoTcsUserData->originTexCoords;

    // Figure out the texcoords of the box origin and store it for the shader
    if (GetIsInfinite()) {

        const Renderer* renderer = tcsData->GetRenderer();

        Matrix3& lastBasis = stratoTcsUserData->lastBasis;
        Vector3& cameraMotion = stratoTcsUserData->cameraMotion;
        Vector3& lastCameraPos = stratoTcsUserData->lastCameraPos;

        Vector3 camPos = (sceneCamera) ? sceneCamera->GetPosition() : Vector3(0, 0, 0);

        Vector3 referencePos = camPos; // World position of the main camera
        Vector3 viewPos = overriddenCameraPos.GetCamPos(); // World position of the current view's camera (could be a shadow map looking down from above, etc.)
        Vector3 offset = viewPos - referencePos; // Offset between the two so we can render in the right place relative to the view
        Vector3 refPosYUp = lastBasis * referencePos; // Main camera in the frame of reference from the previous call (this might change each frame in a geocentric system)
        Vector3 delta = refPosYUp - lastCameraPos; // Camera motion from the previous call using the previous call's frame of reference
        cameraMotion = cameraMotion + delta; // This should be maintained per-view once we start passing cameras through
        lastBasis = sceneCamera->GetBasis3x3(); // Store off the new frame of reference
        lastCameraPos = lastBasis * camPos; // Store off the new camera position

        Vector3 motionYup = cloudMotion * sceneCamera->GetInverseBasis3x3(); // Cloud motion translated to Y-up
        Vector3 viewPosYup = viewPos * sceneCamera->GetInverseBasis3x3(); // Current view translated to Y-up

        // 3D texture coordinates of the origin - rotates the clouds due to wind or camera motion in an infinite cloud layer
        originTexCoords = Vector3((cameraMotion.x - motionYup.x) / width,
                                  motionYup.y / thickness,
                                  (cameraMotion.z - motionYup.z) / depth);

        // If the view isn't in the same place as the main camera, account for that
        Vector3 offsetYup = offset * sceneCamera->GetInverseBasis3x3();
        layerX = -offsetYup.x;
        layerZ = -offsetYup.z;

        // Adjust the cloud layer altitude to account for rendering relative to the camera
        baseAlt = baseAlt - viewPosYup.y;
    } else {
        // Non-infinite layers just bake camera motion and wind into the position of the layer itself, instead of using texture rotation tricks.
        originTexCoords = Vector3(0, 0, 0);
    }

    const Matrix3& basis = GetLocalBasis(tcsUserData);
    Vector3 layerPos = Vector3(layerX, baseAlt, layerZ) * basis;

    Vector3 halfRight = Vector3(width, 0, 0) * basis * 0.5;
    Vector3 fullBack = Vector3(0, 0, -depth) * basis;
    Vector3 halfBack = fullBack * 0.5;
    Vector3 up = Vector3(0, thickness, 0) * basis;

    bbox[0] = layerPos - halfRight - halfBack;
    bbox[1] = bbox[0] + up;
    bbox[2] = layerPos + halfRight - halfBack + up;
    bbox[3] = bbox[2] - up;
    bbox[4] = bbox[0] + fullBack;
    bbox[5] = bbox[1] + fullBack;
    bbox[6] = bbox[2] + fullBack;
    bbox[7] = bbox[3] + fullBack;

    // Transform it into eye coords

    Matrix4 mv = renderCamera->GetModelViewMatrix();
    if (GetIsInfinite()) {
        // If infinite we render relative to the viewpoint (see above)
        mv.elem[0][3] = mv.elem[1][3] = mv.elem[2][3] = 0;
    }

    for (int i = 0; i < 8; i++) {
        Vector4 v(bbox[i]);
        v = mv * v;
        bbox[i] = Vector3(v.x, v.y, v.z);
    }

    VertexBuffer* vb = stratoTcsUserData->vb;

    if (tcsData->GetRenderer()->GetIsVulkan()) {
        vb->IncrementOffsetIndex();
    }
    if (vb->LockBuffer()) {
        Vertex *verts = vb->GetVertices();

        if (verts) {
            const double fade = GetFade(tcsData);

            Color vertColor(fade * alpha, fade * alpha, fade * alpha, fade * alpha);

            for (int i = 0; i < 8; i++) {
                verts[i].x = (float)bbox[i].x;
                verts[i].y = (float)bbox[i].y;
                verts[i].z = (float)bbox[i].z;
                verts[i].w = 1.0f;
                verts[i].SetColor(vertColor);
                ComputeTexCoords(&verts[i], atmosphere, sceneCamera, renderCamera, overriddenCameraPos, tcsData);
            }
        }

        vb->UnlockBuffer();
    }
}

bool StratocumulusCloudLayer::Draw(int pass, const Vector3 *lightPos, const Color *pLightColor,
                                   bool pInvalid, bool wantsLightingUpdate, unsigned long now, const Sky*,
                                   bool, const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                                   , ThreadCameraStreamData* tcsData) const
{
    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    bool& invalid = tcsUserData->invalid;

    Color& lightColor = tcsUserData->lightColor;
    Vector3& lightWorldPos = tcsUserData->lightWorldPos;
    Vector3& lightObjectPos = tcsUserData->lightObjectPos;

    if (pInvalid) {
        invalid = true;
    }

    if (pass == 1) {
        ComputeBoundingBox(atmosphere, sceneCamera, renderCamera, overriddenCameraPos, tcsData);

        if (hasForcedCloudColor) {
            lightColor = Color(forcedCloudColor.x, forcedCloudColor.y, forcedCloudColor.z);
            if (forcedCloudColorLighting) {
                lightColor = lightColor * (*pLightColor);
            }
        } else {
            lightColor = *pLightColor;
        }
        lightColor.r = (float)pow(lightColor.r, (float)lightExp);
        lightColor.g = (float)pow(lightColor.g, (float)lightExp);
        lightColor.b = (float)pow(lightColor.b, (float)lightExp);
        lightColor.ScaleToUnitOrLess(tcsData->GetHDREnabled());
        lightObjectPos = *lightPos;

        Renderer* renderer = tcsData->GetRenderer();

        lightWorldPos = lightObjectPos * tcsData->GetCamera()->GetBasis3x3();

        renderer->SubmitBlendedObject(const_cast<StratocumulusCloudLayer*>(this));
    }
    return true;
}

Vector3 StratocumulusCloudLayer::ApplyRoundedEdges(CurveModes curveMode) const
{
    Vector3 centerRadius2;

    if (GetIsInfinite()) {
        return Vector3(0.5, 0.5, 4.0);
    }

    switch (curveMode) {

    case ALL:
        centerRadius2 = Vector3(0.5, 0.5, 0.5);
        break;

    case NORTH:
        centerRadius2 = Vector3(0.5, 1.0, 0.5);
        break;

    case SOUTH:
        centerRadius2 = Vector3(0.5, 0, 0.5);
        break;

    case EAST:
        centerRadius2 = Vector3(0, 0.5, 0.5);
        break;

    case WEST:
        centerRadius2 = Vector3(1.0, 0.5, 0.5);
        break;

    case NORTHWEST:
        centerRadius2 = Vector3(1.0, 1.0, 1.0);
        break;

    case NORTHEAST:
        centerRadius2 = Vector3(0, 1.0, 1.0);
        break;

    case SOUTHEAST:
        centerRadius2 = Vector3(0, 0, 1.0);
        break;

    case SOUTHWEST:
        centerRadius2 = Vector3(1.0, 0, 1.0);
        break;

    case SQUARE:
        centerRadius2 = Vector3(0.5, 0.5, 2.0);
        break;
    }

    centerRadius2.z = centerRadius2.z * centerRadius2.z;

    return centerRadius2;
}

void StratocumulusCloudLayer::SubUploadVolumeTexture(ThreadCameraStreamData* tcsData) const
{
    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);

    unsigned char* cloudTexData = tcsUserData->cloudTexData;
    const TextureHandle& volumeData = tcsUserData->volumeData;
    Renderer* renderer = tcsData->GetRenderer();

    // Check and compute volume lighting and update volume texture if necessary.
    int updateSliceMin = 0, updateSliceMax = 0;
    if (ComputeVolumeLighting(updateSliceMin, updateSliceMax, tcsData)) {
        const int rowSize = voxelWidth * 2 * sizeof(unsigned char);
        const int sliceSize = voxelDepth * rowSize;
        const unsigned char * data = cloudTexData + updateSliceMin * sliceSize;
        renderer->SubLoad3DTextureLA(data,
                                     voxelWidth, voxelDepth, updateSliceMax - updateSliceMin,
                                     0, 0, updateSliceMin,
                                     rowSize, sliceSize, volumeData);
    }
}

void StratocumulusCloudLayer::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
        , ThreadCameraStreamData* tcsData) const
{
    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);

    VertexBuffer* vb = tcsUserData->vb;
    if (vb == nullptr) {
        return;
    }
    IndexBuffer* ib = tcsUserData->ib;
    if (ib == nullptr) {
        return;
    }

    const StratocumulusShaders* stratocumulusShaders = tcsData->GetStratocumulusShaders();
    ShaderHandle currentShader = atmosphere->GetHDREnabled() ? stratocumulusShaders->GetShaderHdr() : stratocumulusShaders->GetShader();
    const StratocumulusShaderConstantLocations& currentLocations = (atmosphere->GetHDREnabled()) ? stratocumulusShaders->GetShaderConstantLocationsHdr()
            : stratocumulusShaders->GetShaderConstantLocations();
    if (!currentShader) {
        return;
    }

    SubUploadVolumeTexture(tcsData);

    Renderer* renderer = tcsData->GetRenderer();

    renderer->IncrementConstantBuffersOffset(currentShader);

    bool readDepth = true;
    Configuration::GetBoolValue("stratocumulus-read-depth", readDepth);
    renderer->EnableDepthReads(readDepth);
    renderer->EnableDepthWrites(false);
    renderer->EnableTexture2D(false);
    renderer->EnableTexture3D(true);
    renderer->EnableLighting(false);
    renderer->EnableBackfaceCulling(true);
    renderer->EnableBlending(ONE, INVSRCALPHA);

    const TextureHandle& volumeData = tcsUserData->volumeData;
    renderer->Enable3DTexture(volumeData, 0);

    TextureHandle noiseTexture = tcsUserData->noiseTexture;
    SL_ASSERT(noiseTexture);
    renderer->Enable3DTexture(noiseTexture, 1);

    const TextureHandle& sdfVolumeData = tcsUserData->sdfVolumeData;
    if (sdfVolumeData) renderer->Enable3DTexture(sdfVolumeData, 2);

    renderer->BindShader(currentShader);

    const Matrix4& proj = renderCamera->GetProjectionMatrix();

    const double extinctionCrossSection = MathUtils::Pi() * dropletSize * dropletSize;
    const double extinctionCoefficient = extinctionCrossSection * (dropletsPerCubicCm * 100 * 100 * 100);

    const double opticalDepth = extinctionCoefficient * lightSamplingDistance;
    const double extinction = 1.0 - exp(-opticalDepth);

    const double constTerm = (albedo * opticalDepth) / (4.0 * MathUtils::Pi());


    const Vector3 lightObjectPos = tcsUserData->lightObjectPos;
    const Vector3 lightWorldPos = tcsUserData->lightWorldPos;
    const Color lightColor = tcsUserData->lightColor;
    const Vector3 originTexCoords = tcsUserData->originTexCoords;

    const Vector4 lightObjectPosAndConstTerm(lightObjectPos.x, lightObjectPos.y, lightObjectPos.z, constTerm);

    const Vector4 lightWorldDirAndExtinction(lightWorldPos.x, lightWorldPos.y, lightWorldPos.z, extinction);

    Vertex camPos(0, 0, 0, 1);
    ComputeTexCoords(&camPos, atmosphere, sceneCamera, renderCamera, overriddenCameraPos, tcsData);
    const Vector3 camTexCoords(camPos.GetU(), camPos.GetV(), camPos.GetT());

    Vector3 lightTexCoords = Vector3(lightObjectPos.x * (double)voxelWidth,
                                     lightObjectPos.y * (double)voxelHeight, lightObjectPos.z * (double)voxelDepth);
    lightTexCoords.Normalize();

    const float fade = (GetFadeTowardEdges() && fadeOverride) ? 1.0f : 0.0f;
    const Vector3 fadeFlag(fade, fade, fadeFalloff);

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();

    Color forceColor;
    bool forceConstantColorLocal = billboardParams->GetForceConstantColor(forceColor);

    CloudLayerTcsUserData* cloudLayerTcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);
    Vector3 roundCenter = ApplyRoundedEdges(cloudLayerTcsUserData->curveMode);

    if (renderer->SupportsConstantLocations()) {
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_outputScale, atmosphere->GetOutputScale());
        renderer->SetConstantMatrix4AtLocation(currentShader, currentLocations.sl_projectionMatrix, proj);
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_extinctionCoefficient, Vector3(extinctionCoefficient, extinctionCoefficient, extinctionCoefficient));
        renderer->SetConstantVector4AtLocation(currentShader, currentLocations.sl_lightObjectDirAndConstTerm, lightObjectPosAndConstTerm);
        renderer->SetConstantVector4AtLocation(currentShader, currentLocations.sl_lightWorldDirAndExtinction, lightWorldDirAndExtinction);
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_cameraTexCoords, camTexCoords);
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_lightTexCoords, lightTexCoords);
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_roundCenter, roundCenter);
    } else {
        renderer->SetConstantVector(currentShader, "sl_outputScale", atmosphere->GetOutputScale());
        renderer->SetConstantMatrix(currentShader, "sl_projectionMatrix", proj);
        renderer->SetConstantVector(currentShader, "sl_extinctionCoefficient", Vector3(extinctionCoefficient, extinctionCoefficient, extinctionCoefficient));
        renderer->SetConstantVector4(currentShader, "sl_lightObjectDirAndConstTerm", lightObjectPosAndConstTerm);
        renderer->SetConstantVector4(currentShader, "sl_lightWorldDirAndExtinction", lightWorldDirAndExtinction);
        renderer->SetConstantVector(currentShader, "sl_cameraTexCoords", camTexCoords);
        renderer->SetConstantVector(currentShader, "sl_lightTexCoords", lightTexCoords);
        renderer->SetConstantVector(currentShader, "sl_roundCenter", roundCenter);
    }


    if (forceConstantColorLocal) {
        if (renderer->SupportsConstantLocations()) {
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_lightColor, forceColor.ToVector3());
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_skyLightColor, forceColor.ToVector3() * ambientScattering);
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_multipleScatteringTerm, forceColor.ToVector3() * multipleScatteringTerm);
        } else {
            renderer->SetConstantVector(currentShader, "sl_lightColor", forceColor.ToVector3());
            renderer->SetConstantVector(currentShader, "sl_skyLightColor", forceColor.ToVector3() * ambientScattering);
            renderer->SetConstantVector(currentShader, "sl_multipleScatteringTerm", forceColor.ToVector3() * multipleScatteringTerm);
        }

    } else {

        Color skyLight = metaballParams->GetAmbientColor();
        skyLight.r = (float)pow(skyLight.r, (float)lightExp);
        skyLight.g = (float)pow(skyLight.g, (float)lightExp);
        skyLight.b = (float)pow(skyLight.b, (float)lightExp);

        const Vector3 multipleScattering = Vector3(lightColor.r, lightColor.g, lightColor.b) * multipleScatteringTerm;

        if (renderer->SupportsConstantLocations()) {
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_lightColor, Vector3(lightColor.r, lightColor.g, lightColor.b));
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_skyLightColor, Vector3(skyLight.r, skyLight.g, skyLight.b) * ambientScattering);
            renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_multipleScatteringTerm, multipleScattering);
        } else {
            renderer->SetConstantVector(currentShader, "sl_lightColor", Vector3(lightColor.r, lightColor.g, lightColor.b));
            renderer->SetConstantVector(currentShader, "sl_skyLightColor", Vector3(skyLight.r, skyLight.g, skyLight.b) * ambientScattering);
            renderer->SetConstantVector(currentShader, "sl_multipleScatteringTerm", multipleScattering);
        }
    }

    const Vector3 voxelDimensions = Vector3(voxelWidth, voxelHeight, voxelDepth);

    const Color fogColor = billboardParams->GetFogColor();
    const double fogDensity = billboardParams->GetFogDensity();
    const Vector4 fogColorAndDensity(fogColor.r, fogColor.g, fogColor.b, fogDensity);


    const Vector3 unitScale = Vector3(Atmosphere::GetUnitScale(), 0, 0);

    if (renderer->SupportsConstantLocations()) {
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_fadeFlag, fadeFlag);

        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_voxelDimensions, voxelDimensions * voxelDimension);
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_viewSampleDimensions, Vector3(1.0 / (voxelWidth * voxelDimension),
                                              1.0 / (voxelHeight * voxelDimension), 1.0 / (voxelDepth * voxelDimension)));
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_lightSampleDimensions, Vector3(lightSamplingDistance / (voxelWidth * voxelDimension),
                                              lightSamplingDistance / (voxelHeight * voxelDimension), lightSamplingDistance / (voxelDepth * voxelDimension)));
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_noiseOffset, Vector3(noiseOffset.x / (voxelWidth * voxelDimension),
                                              noiseOffset.y / (voxelHeight * voxelDimension), noiseOffset.z / (voxelDepth * voxelDimension)));
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_jitter, Vector3(jitter / (voxelWidth * voxelDimension), jitter / (voxelHeight * voxelDimension),
                                              jitter / (voxelDepth * voxelDimension)));
        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_originTexCoords, originTexCoords);


        renderer->SetConstantVector4AtLocation(currentShader, currentLocations.sl_fogColorAndDensity, fogColorAndDensity);

        renderer->SetConstantVectorAtLocation(currentShader, currentLocations.sl_unitScale, unitScale);

    } else {
        renderer->SetConstantVector(currentShader, "sl_fadeFlag", fadeFlag);

        renderer->SetConstantVector(currentShader, "sl_voxelDimensions", voxelDimensions * voxelDimension);
        renderer->SetConstantVector(currentShader, "sl_viewSampleDimensions", Vector3(1.0 / (voxelWidth * voxelDimension),
                                    1.0 / (voxelHeight * voxelDimension), 1.0 / (voxelDepth * voxelDimension)));
        renderer->SetConstantVector(currentShader, "sl_lightSampleDimensions", Vector3(lightSamplingDistance / (voxelWidth * voxelDimension),
                                    lightSamplingDistance / (voxelHeight * voxelDimension), lightSamplingDistance / (voxelDepth * voxelDimension)));
        renderer->SetConstantVector(currentShader, "sl_noiseOffset", Vector3(noiseOffset.x / (voxelWidth * voxelDimension),
                                    noiseOffset.y / (voxelHeight * voxelDimension), noiseOffset.z / (voxelDepth * voxelDimension)));
        renderer->SetConstantVector(currentShader, "sl_jitter", Vector3(jitter / (voxelWidth * voxelDimension), jitter / (voxelHeight * voxelDimension),
                                    jitter / (voxelDepth * voxelDimension)));
        renderer->SetConstantVector(currentShader, "sl_originTexCoords", originTexCoords);


        renderer->SetConstantVector4(currentShader, "sl_fogColorAndDensity", fogColorAndDensity);

        renderer->SetConstantVector(currentShader, "sl_unitScale", unitScale);
    }

    const Vector3 posInObject = camTexCoords;
    const bool isInside = (posInObject.x > 0 && posInObject.x < 1.0 && posInObject.y > 0 && posInObject.y < 1.0
                           && posInObject.z > 0 && posInObject.z < 1.0);

    // Don't render edges for orthographic views
    bool isOrthographic = proj.elem[3][0] == 0 && proj.elem[3][1] == 0 && proj.elem[3][2] == 0 && proj.elem[3][3] == 1.0;
    int nFaces = isOrthographic ? 2 : 6;

    for (int face = 0; face < nFaces; face++) {
        if (isInside) {
            renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), face * 4 + 4 * 6, 4, 8, true, true);
        } else {
            renderer->DrawStrip(vb->GetHandle(), ib->GetHandle(), face * 4, 4, 8, true, true);
        }
    }

    renderer->UnbindShader();
    renderer->EnableTexture3D(false);
    renderer->EnableDepthWrites(true);
    renderer->DisableTexture(0);
    renderer->DisableTexture(1);
    renderer->EnableBackfaceCulling(false);
}

bool StratocumulusCloudLayer::DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const
{
    return true;
}

bool StratocumulusCloudLayer::EndDraw(int pass) const
{
    return true;
}

bool StratocumulusCloudLayer::Serialize(std::ostream& s, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    unsigned char* cloudTexData = tcsUserData->cloudTexData;

    VertexBuffer* vb = tcsUserData->vb;
    IndexBuffer* ib = tcsUserData->ib;

#define WRITE_DOUBLE(X) s.write((char *)&X, sizeof(double))
#define WRITE_INT(X) s.write((char *)&X, sizeof(int))

    WRITE_DOUBLE(voxelDimension);
    WRITE_DOUBLE(lightSamplingDistance);
    WRITE_DOUBLE(multipleScatteringTerm);
    WRITE_DOUBLE(dropletSize);
    WRITE_DOUBLE(ambientScattering);
    WRITE_DOUBLE(dropletsPerCubicCm);
    WRITE_DOUBLE(albedo);
    WRITE_DOUBLE(extinctionProbability);
    WRITE_DOUBLE(transitionProbability);
    WRITE_DOUBLE(vaporProbability);
    WRITE_DOUBLE(jitter);
    WRITE_DOUBLE(maxSize);
    WRITE_INT(initialEvolve);
    WRITE_INT(voxelWidth);
    WRITE_INT(voxelDepth);
    WRITE_INT(voxelHeight);
    WRITE_DOUBLE(inscatteringTerm);

    cloudMotion.Serialize(s);
    noiseOffset.Serialize(s);
    tcsUserData->lightColor.Serialize(s);
    tcsUserData->lightWorldPos.Serialize(s);
    tcsUserData->lightObjectPos.Serialize(s);
    tcsUserData->originTexCoords.Serialize(s);

    if (vb->LockBuffer()) {
        int vbSize = vb->GetNumVertices();
        WRITE_INT(vbSize);
        Vertex *v = vb->GetVertices();
        s.write((char *)v, vbSize * sizeof(Vertex));
        vb->UnlockBuffer();
    } else {
        int vbSize = 0;
        WRITE_INT(vbSize);
    }

    if (ib->LockBuffer()) {
        int ibSize = ib->GetNumIndices();
        WRITE_INT(ibSize);
        Index *i = ib->GetIndices();
        s.write((char *)i, ibSize * sizeof(Index));
        ib->UnlockBuffer();
    } else {
        int ibSize = 0;
        WRITE_INT(ibSize);
    }

    s.write((char *)cloudTexData, voxelWidth * voxelHeight * voxelDepth * 2 * sizeof(unsigned char));

    return CloudLayer::Serialize(s, tcsData);
}

bool StratocumulusCloudLayer::Unserialize(const Atmosphere& atm, std::istream& s, void* data)
{
    SL_ASSERT(&atmosphere == &atm);
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    VertexBuffer* vb = tcsUserData->vb;
    IndexBuffer* ib = tcsUserData->ib;

    unsigned char*& cloudTexData = tcsUserData->cloudTexData;
    float*& randomData = tcsUserData->randomData;

#define READ_DOUBLE(X) s.read((char *)&X, sizeof(double))
#define READ_INT(X) s.read((char *)&X, sizeof(int))

    READ_DOUBLE(voxelDimension);
    READ_DOUBLE(lightSamplingDistance);
    READ_DOUBLE(multipleScatteringTerm);
    READ_DOUBLE(dropletSize);
    READ_DOUBLE(ambientScattering);
    READ_DOUBLE(dropletsPerCubicCm);
    READ_DOUBLE(albedo);
    READ_DOUBLE(extinctionProbability);
    READ_DOUBLE(transitionProbability);
    READ_DOUBLE(vaporProbability);
    READ_DOUBLE(jitter);
    READ_DOUBLE(maxSize);
    READ_INT(initialEvolve);
    READ_INT(voxelWidth);
    READ_INT(voxelDepth);
    READ_INT(voxelHeight);
    READ_DOUBLE(inscatteringTerm);

    cloudMotion.Unserialize(s);
    noiseOffset.Unserialize(s);
    tcsUserData->lightColor.Unserialize(s);
    tcsUserData->lightWorldPos.Unserialize(s);
    tcsUserData->lightObjectPos.Unserialize(s);
    tcsUserData->originTexCoords.Unserialize(s);

    int numVerts, numIndices;
    READ_INT(numVerts);
    if (numVerts > 0) {
        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "StratocumulusCloudLayer::VertexBuffer" : (const char *)0;

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = true;
        bufferProperties.dynamic = false;

        vb = SL_NEW VertexBuffer(numVerts, name, tcsData, bufferProperties);
        if (vb->LockBuffer()) {
            Vertex *v = vb->GetVertices();
            s.read((char *)v, numVerts * sizeof(Vertex));
            vb->UnlockBuffer();
        }
    }

    READ_INT(numIndices);
    if (numIndices > 0) {
        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "StratocumulusCloudLayer::IndexBuffer" : (const char *)0;

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = true;
        bufferProperties.dynamic = false;

        ib = SL_NEW IndexBuffer(numIndices, name, tcsData, bufferProperties);
        if (ib->LockBuffer()) {
            Index *i = ib->GetIndices();
            s.read((char *)i, numIndices * sizeof(Index));
            ib->UnlockBuffer();
        }
    }

    if (cloudTexData) SL_DELETE[] cloudTexData;
    unsigned int cloudTexBytes = voxelWidth * voxelHeight * voxelDepth * 2 * sizeof(unsigned char);
    cloudTexData = SL_NEW unsigned char[cloudTexBytes];
    s.read((char *)cloudTexData, cloudTexBytes);
    Renderer* renderer = tcsData->GetRenderer();

    TextureHandle& volumeData = tcsUserData->volumeData;
    if (volumeData) renderer->ReleaseTexture(volumeData);
    volumeData = 0;

    bool enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
    const char* name = 0;

    name = (enableObjectLabeling) ? "SilverLining::Stratocumulus Cloud Layer Cloud Texture Data" : (const char *)0;
    bool wrap = GetIsInfinite();
    renderer->Load3DTextureLA(cloudTexData, voxelWidth, voxelDepth, voxelHeight, &volumeData, wrap, wrap, false, name);

#ifdef COLOR_RANDOMNESS
    randomData = SL_NEW float[voxelWidth * voxelHeight  * voxelDepth];
    float *r = randomData;

    for (int h = 0; h < voxelHeight; h++) {
        for (int d = 0; d < voxelDepth; d++) {
            for (int w = 0; w < voxelWidth; w++) {
                *r++ = 1.0f - atm.GetRandomNumberGenerator()->UniformRandomFloat() * (float)COLOR_RANDOMNESS;
            }
        }
    }
#endif  // COLOR_RANDOMNESS

    return CloudLayer::Unserialize(atm, s, tcsData);
}

void StratocumulusCloudLayer::IncrementGrowth(const RandomNumberGenerator* rng)
{
    int i, j, k;

    for (i = 0; i < voxelWidth; i++) {
        for (j = 0; j < voxelDepth; j++) {
            for (k = MIN_HEIGHT; k < voxelHeight; k++) {

                bool fAct = (
                                (i + 1 < voxelWidth ? voxels[i + 1][j][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (j + 1 < voxelDepth ? voxels[i][j + 1][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (k + 1 < voxelHeight ? voxels[i][j][k + 1]->states & PHASE_TRANSITION_BIT : false) ||
                                (i - 1 >= 0 ? voxels[i - 1][j][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (j - 1 >= 0 ? voxels[i][j - 1][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (k - 1 >= 0 ? voxels[i][j][k - 1]->states & PHASE_TRANSITION_BIT : false) ||
                                (i - 2 >= 0 ? voxels[i - 2][j][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (i + 2 < voxelWidth ? voxels[i + 2][j][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (j - 2 >= 0 ? voxels[i][j - 2][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (j + 2 < voxelDepth ? voxels[i][j + 2][k]->states & PHASE_TRANSITION_BIT : false) ||
                                (k - 2 >= 0 ? voxels[i][j][k - 2]->states & PHASE_TRANSITION_BIT : false)
                            );

                bool thisAct = (voxels[i][j][k]->states & PHASE_TRANSITION_BIT) != 0;

                double rnd;
                rnd = rng->UniformRandomDouble();

                bool phaseTransition = ((!thisAct) && (voxels[i][j][k]->states & VAPOR_BIT) && fAct) ||
                                       (rnd < voxels[i][j][k]->phaseTransitionProbability);
                phaseTransition ? voxels[i][j][k]->states |= PHASE_TRANSITION_BIT : voxels[i][j][k]->states &= ~PHASE_TRANSITION_BIT;

                rnd = rng->UniformRandomDouble();

                bool vapor = ((voxels[i][j][k]->states & VAPOR_BIT) && !thisAct) || (rnd < voxels[i][j][k]->vaporProbability);
                vapor ? voxels[i][j][k]->states |= VAPOR_BIT : voxels[i][j][k]->states &= ~VAPOR_BIT;

                rnd = rng->UniformRandomDouble();

                bool hasCloud = ((voxels[i][j][k]->states & HAS_CLOUD_BIT) || thisAct) && (rnd > voxels[i][j][k]->extinctionProbability);
                hasCloud ? voxels[i][j][k]->states |= HAS_CLOUD_BIT : voxels[i][j][k]->states &= ~HAS_CLOUD_BIT;

            }
        }
    }
}

bool StratocumulusCloudLayer::HasPrecipitationAtPosition(const Camera* camera, void* data) const
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();

    // Bail if we have no precip at all.
    if (precipitationEffects.size() == 0 || !IsRenderable(tcsData)) return false;

    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    // Don't assume which way is up.
    Vector3 camPos = camera->GetPosition() * camera->GetInverseBasis3x3();

    // Bail if you're not under the entire layer at all.
    if (camPos.y > tcsUserData->geocentricAltitude) return false;

    if (!GetIsInfinite()) {
        if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
        if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
        if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
        if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;
    }

    return true;
}


bool StratocumulusCloudLayer::IsInsideCloud(double x, double y, double z, void* data, double* distanceInside) const
{
    if (distanceInside) *distanceInside = 0;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);
    CloudLayerTcsUserData* tcsUserData = CloudLayer::GetOrCreateCloudLayerTcsUserData(tcsData);

    const double layerX = tcsUserData->layerX;
    const double layerZ = tcsUserData->layerZ;

    // Don't assume which way is up.
    const Vector3 camPos = Vector3(x,y,z) * tcsData->GetCamera()->GetInverseBasis3x3();

    // Bail if you're not in the entire layer at all.
    if (camPos.y < tcsUserData->geocentricAltitude) return false;
    if (camPos.y > tcsUserData->geocentricAltitude + GetThickness()) return false;

    if (GetIsInfinite()) {
        if (distanceInside) {
            double y1 = fabs(camPos.y - tcsUserData->geocentricAltitude);
            double y2 = fabs(camPos.y - (tcsUserData->geocentricAltitude + GetThickness()));
            *distanceInside = y1 < y2 ? y1 : y2;
        }
        return true;
    }

    if (camPos.x < (layerX - GetBaseWidth() * 0.5)) return false;
    if (camPos.x >(layerX + GetBaseWidth() * 0.5)) return false;
    if (camPos.z > (layerZ + GetBaseLength() * 0.5)) return false;
    if (camPos.z < (layerZ - GetBaseLength() * 0.5)) return false;

    return IsInsideAnyCloud(camPos, tcsData, tcsUserData, distanceInside);
}

bool StratocumulusCloudLayer::IsInsideCloud(const Camera* camera, void* data, double* distanceInside) const
{
    const Vector3& pos = camera->GetPosition();

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();;

    return IsInsideCloud(pos.x, pos.y, pos.z, tcsData, distanceInside);

}

#if BILINEAR_LIGHTING
static inline float sample(unsigned char * data, float coord[3], int c[3])
{
    int z0 = int(floor(coord[2])), z1 = z0 + 1;
    float fz0 = z1 - coord[2], fz1 = coord[2] - z0;

    int y0 = int(floor(coord[1])), y1 = y0 + 1;
    float fy0 = y1 - coord[1], fy1 = coord[1] - y0;

    int x0 = int(floor(coord[0])), x1 = x0 + 1;
    float fx0 = x1 - coord[0], fx1 = coord[0] - x0;

    if (z0 < 0) z0 = 0;
    else if (z0 >= c[2]) z0 = c[2] - 1;

    if (z1 < 0) z1 = 0;
    else if (z1 >= c[2]) z1 = c[2] - 1;

    z0 *= c[0] * c[1] * 2;
    z1 *= c[0] * c[1] * 2;
    y0 = ((y0 + c[1]) % c[1]) * c[0] * 2;
    y1 = ((y1 + c[1]) % c[1]) * c[0] * 2;
    x0 = ((x0 + c[0]) % c[0]) * 2;
    x1 = ((x1 + c[0]) % c[0]) * 2;

    return data[z0 + y0 + x0] * fx0 * fy0 * fz0 +
           data[z1 + y0 + x0] * fx0 * fy0 * fz1 +
           data[z0 + y1 + x0] * fx0 * fy1 * fz0 +
           data[z1 + y1 + x0] * fx0 * fy1 * fz1 +
           data[z0 + y0 + x1] * fx1 * fy0 * fz0 +
           data[z1 + y0 + x1] * fx1 * fy0 * fz1 +
           data[z0 + y1 + x1] * fx1 * fy1 * fz0 +
           data[z1 + y1 + x1] * fx1 * fy1 * fz1;
}
#else // NEAREST_LIGHTING
static inline float sample( unsigned char * data, float coord[3], int c[3] )
{
    int z = int( floor( coord[2] + 0.5 ) );
    int y = int( floor( coord[1] + 0.5 ) );
    int x = int( floor( coord[0] + 0.5 ) );
    if( z < 0 ) z = 0;
    else if( z >= c[2] ) z = c[2]-1;
    z *= c[0] * c[1] * 2;
    y = ( ( y + c[1] ) % c[1] ) * c[0] * 2;
    x = ( ( x + c[0] ) % c[0] ) * 2;

    return data[ z + y + x ];
}
#endif

static inline float randomSample(float *data, float coord[3], int c[3])
{
    int z = int(floor(coord[2] + 0.5));
    int y = int(floor(coord[1] + 0.5));
    int x = int(floor(coord[0] + 0.5));
    if (z < 0) z = 0;
    else if (z >= c[2]) z = c[2] - 1;
    z *= c[0] * c[1];
    y = ((y + c[1]) % c[1]) * c[0];
    x = ((x + c[0]) % c[0]);

    return data[z + y + x];
}

int StratocumulusCloudLayer::ComputeVolumeLighting(int & updateSliceMin, int & updateSliceMax, ThreadCameraStreamData* tcsData) const
{
    updateSliceMin = updateSliceMax = 0;
    // Lot of 3 element vectors.  We juggle these elements with varying indices.
    int   c[3] = { voxelWidth, voxelDepth, voxelHeight };

    StratocumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateTcsUserData(tcsData);
    unsigned char* cloudTexData = tcsUserData->cloudTexData;
    float* randomData = tcsUserData->randomData;

    const Vector3& lightObjectPos = tcsUserData->lightObjectPos;
    bool& invalid = tcsUserData->invalid;
    Vector3& volumeLightingVector = tcsUserData->volumeLightingVector;
    int& volumeLightingSlice = tcsUserData->volumeLightingSlice;

    Vector3 lightVector = lightObjectPos;

    if (lightVector.Length() > 0)
        lightVector.Normalize();
    else
        lightVector = Vector3(0, 0, 1);

    double change = volumeLightingVector.Dot(lightVector);

    if (invalid) {
        change = 0;
        invalid = false;
    }

    if (change > volumeLightingVectorSliceRefreshTolerance && volumeLightingSlice == 0)
        return 0; // nothing to do - lighting is up to date

    int slice = 0, sliceCount = c[2];

    if (change > volumeLightingVectorFullRefreshTolerance) {
        if (volumeLightingSlice == 0)
            volumeLightingVector = lightVector; // record light vector for consecutive slices

        slice = volumeLightingSlice;
        sliceCount = 1;
        volumeLightingSlice++;

        if (volumeLightingSlice >= c[2])
            volumeLightingSlice = 0; // mark it ready for new iteration loop
    } else {
        volumeLightingVector = lightVector;
        volumeLightingSlice = 0;
    }

    float l[3] = { float(volumeLightingVector.x), float(volumeLightingVector.z), float(volumeLightingVector.y) };
    float labs[3] = { MathUtilsFloat::Abs(l[0]), MathUtilsFloat::Abs(l[1]), MathUtilsFloat::Abs(l[2]) };

    int   i0 = labs[1] < labs[0] ? 1 : 0, // primary coord from x,y is the one with larger absolute
          i1 = labs[1] < labs[0] ? 0 : 1; // secondary coord from x,y is the one with smaller absolute

    int imax = labs[2] > labs[i1] ? 2 : i1; // imax is an index of max absolute coord
    //int imin = labs[2] < labs[i0] ? 2 : i0; // imin is an index of min absolute coord
    //int imid = imax == 2 ? i1 : ( imin == 2 ? i0 : 2 ); // imid is abs median coordinate index

    l[0] /= labs[imax]; // compute constant offset to prior samples overshadowing (or not) computed sample
    l[1] /= labs[imax];
    l[2] /= labs[imax];

    double volumeLightSamplingDistance = lightObjectPos.Length() * voxelDimension / labs[imax];
    double extinctionCrossSection = MathUtils::Pi() * dropletSize * dropletSize;
    double extinctionCoefficient = extinctionCrossSection * (dropletsPerCubicCm * 100 * 100 * 100);

    double opticalDepth = extinctionCoefficient * volumeLightSamplingDistance;

    // Harris model uses following value for constTerm:
    // albedo * opticalDepth * phaseFunction( -lightVector, lightVector ) * steradians_area_of_inscattering_integration  / (4.0 * MathUtils::Pi())
    // above model does not work well here so instead we use constant inscatteringTerm to replace all factors except albedo

    float constTerm = float(albedo * inscatteringTerm);
    float extinction = constTerm + expf(-(float)opticalDepth);

    int s[3] = { l[0] < 0 ? -1 : 1, // increments/decrements for updating the samples in right direction
                 l[1] < 0 ? -1 : 1, // so that we accumulate light values from previously computed ones
                 l[2] < 0 ? -1 : 1
               }; // direction is as important as direction of mem move in overlapping regions

    int d[3] = { 2, 2 * c[0], 2 * c[0] * c[1] }; // memory deltas for one unit steps along each coord

    int o[3] = { l[0] < 0 ? c[0] - 1 - slice : slice, // origin - starting position for iterations
                 l[1] < 0 ? c[1] - 1 - slice : slice,
                 l[2] < 0 ? c[2] - 1 - slice : slice
               };

    if (sliceCount == 1) {
        updateSliceMin = o[2];
        updateSliceMax = o[2] + 1;
    } else {
        updateSliceMin = 0;
        updateSliceMax = c[2];
    }

    float p[3];

    unsigned char * pb = cloudTexData + o[2] * d[2] + o[1] * d[1] + o[0] * d[0];
    unsigned char * cloudTexDataLast = &(cloudTexData[2 * voxelWidth * voxelHeight * voxelDepth - 1]);

    int d0 = d[i0] * s[i0];
    int d1 = d[i1] * s[i1] - c[i0] * d0;
    int d2 = d[2] * s[2] - c[i1] * d[i1] * s[i1];

    for (p[2] = o[2] - l[2]; sliceCount-- > 0; pb += d2, p[2] += s[2]) {
        p[i1] = o[i1] - l[i1];
        for (int c1 = c[i1]; c1-- > 0; pb += d1, p[i1] += s[i1]) {
            p[i0] = o[i0] - l[i0];
            for (int c0 = c[i0]; c0-- > 0; pb += d0, p[i0] += s[i0]) {
                if (pb < cloudTexData) {
                    if (d0 <= 0.f) {
                        pb += c0 * d0;
                        break;
                    }
                } else if (pb >= cloudTexDataLast) {
                    if (d0 >= 0.f) {
                        pb += c0 * d0;
                        break;
                    }
                } else {
                    pb[1] = 0xFF;
                    float src = sample(cloudTexData, p, c);

                    double r = 1.0;
#ifdef COLOR_RANDOMNESS
                    r = (double)randomSample(randomData, p, c);
#endif
                    double scale = (pb[0] == 255 ? extinction : 1.) * r;
                    pb[1] = static_cast<unsigned char>(scale * src);
                }
            }
        }
    }

    return updateSliceMax - updateSliceMin;
}

ShaderHandle StratocumulusCloudLayer::GetShader(ThreadCameraStreamData* tcsData) const
{
    return atmosphere.GetHDREnabled() ? tcsData->GetStratocumulusShaders()->GetShaderHdr()
           : tcsData->GetStratocumulusShaders()->GetShader();
}

void StratocumulusCloudLayer::ReloadShaders(ThreadCameraStreamData* tcsData)
{
    tcsData->ReloadStratocumulusShaders();
}

void StratocumulusCloudLayer::ClearClouds(void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    this->RemoveListenener(tcsData->GetRenderer()->GetListener());

    CloudLayer::ClearClouds(tcsData);
}
}
