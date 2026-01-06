// Copyright (c) 2004-2017  Sundog Software, LLC. All rights reserved worldwide.

#include "CumulusCloudLayer.h"
#include "ResourceLoader.h"
#include "CumulusCloud.h"
#include "Metaball.h"
#include "Configuration.h"
#include "CloudGenerator.h"
#include "CloudDistributor.h"
#include "Renderer.h"
#include "Billboard.h"
#include <algorithm>
#include "CloudGeneratorFactory.h"
#include "CloudDistributorFactory.h"
#include "MathUtils.h"
#include "BillboardBuffer.h"
#include "TextureManager.h"
#include "BillboardShader.h"
#include "SLAssert.h"
#include "TcsUserData.h"
#include "Camera.h"
#include <sstream>

using namespace std;

namespace SilverLining
{
class CumulusCloudLayerTcsUserData : public TcsUserData
{
public:
    CumulusCloudLayerTcsUserData(ThreadCameraStreamData* _tcsData);
    virtual ~CumulusCloudLayerTcsUserData();
public:
    TextureHandle shadowTex;
    RenderTargetHandle offscreen;

    ThreadCameraStreamData* tcsData;
};

CumulusCloudLayerTcsUserData::CumulusCloudLayerTcsUserData(ThreadCameraStreamData* _tcsData)
    : shadowTex(0)
    , offscreen(0)
    , tcsData(_tcsData)
{

}
CumulusCloudLayerTcsUserData::~CumulusCloudLayerTcsUserData()
{
    if (shadowTex) {
        tcsData->GetRenderer()->ReleaseTexture(shadowTex);
        shadowTex = 0;
    }
    if (offscreen) {
        tcsData->GetRenderer()->ReleaseRenderTarget(offscreen);
        offscreen = 0;
    }
}

CumulusCloudLayerTcsUserData* CumulusCloudLayer::GetOrCreateCumulusCloudLayerTcsUserData(ThreadCameraStreamData* tcsData) const
{
    if (tcsData == 0) {
        return 0;
    }
    TcsUserData* userData = tcsData->GetUserData(this);
    if (userData == 0) {
        userData = SL_NEW CumulusCloudLayerTcsUserData(tcsData);
        tcsData->SetUserData(this, userData);
    }
    return (CumulusCloudLayerTcsUserData*)userData;
}

CumulusCloudLayer::CumulusCloudLayer(const Atmosphere& atmosphere) : CloudLayer(atmosphere),  generator(0), distributor(0), makeShadowMaps(false)
    , restoreBackBuffer(false), savedBackBuffer(0), spinRate(0.1), maxHeight(0), maxSize(0), coverageMap(0)
{
    shadowMapPos = Vector3(0,0,0);

    renderLightingPassOffscreen = false;
    Configuration::GetBoolValue("render-offscreen", renderLightingPassOffscreen);

    renderLightingPassOffscreen = false;
}

CumulusCloudLayer::~CumulusCloudLayer()
{
    if (generator) {
        SL_DELETE generator;
    }

    if (distributor) {
        SL_DELETE distributor;
    }

    if (savedBackBuffer) {
        SL_DELETE[] savedBackBuffer;
    }
}

void CumulusCloudLayer::InitCoverageMap()
{
    coverageW = (int)(CloudLayer::GetBaseWidth() / parameters.voxelSize);
    coverageL = (int)(CloudLayer::GetBaseLength() / parameters.voxelSize);
    totalCoverageCells = coverageW * coverageL;
    cellsCovered = 0;

    if (coverageMap) SL_DELETE[] coverageMap;
    coverageMap = SL_NEW bool[totalCoverageCells];
    for (int i = 0; i < totalCoverageCells; i++) {
        coverageMap[i] = false;
    }
}

void CumulusCloudLayer::DeleteCoverageMap()
{
    if (coverageMap) SL_DELETE[] coverageMap;
    coverageMap = 0;
}

void CumulusCloudLayer::AddToCoverage(Cloud *cloud, ThreadCameraStreamData* tcsData)
{
    const Camera* camera = tcsData->GetCamera();
    Vector3 pos = cloud->GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();
    double layerX, layerZ;
    CloudLayer::GetLayerPosition(layerX, layerZ, tcsData);
    double x = pos.x - (layerX - CloudLayer::GetBaseWidth() * 0.5);
    double z = pos.z - (layerZ - CloudLayer::GetBaseLength() * 0.5);

    double cloudWidth, cloudDepth, cloudHeight;
    cloud->GetSize(cloudWidth, cloudDepth, cloudHeight);
    int cellsWide = (int)(cloudWidth / parameters.voxelSize);
    int cellsDeep = (int)(cloudDepth / parameters.voxelSize);

    int cellX = (int)(x / parameters.voxelSize);
    int cellZ = (int)(z / parameters.voxelSize);

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

float CumulusCloudLayer::GetCoverage()
{
    return (float)cellsCovered / (float)totalCoverageCells;
}

void CumulusCloudLayer::SetCloudAnimationEffects(double pSpinRate, bool pGrowth, int pIterations, int pTimeStepInterval)
{
    spinRate = pSpinRate;

    CloudTypes type = GetType();

    if (type == CUMULUS_CONGESTUS || type == CUMULUS_CONGESTUS_HI_RES) {
        growth = pGrowth;

        if (pIterations > 0) {
            parameters.initialEvolveSteps = pIterations;
        }

        if (pTimeStepInterval > 0) {
            parameters.timeStepInterval = pTimeStepInterval;
        }
    }
}

bool CumulusCloudLayer::SupportsShadowMaps()
{
    return (!renderLightingPassOffscreen);
}

void CumulusCloudLayer::SetupSurfaces(ThreadCameraStreamData* tcsData)
{
    Renderer* renderer = tcsData->GetRenderer();

    CumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateCumulusCloudLayerTcsUserData(tcsData);

    if (renderLightingPassOffscreen) {
        int offscreenDim = 512;
        Configuration::GetIntValue("offscreen-dimension", offscreenDim);

        RenderTargetHandle offscreen = tcsUserData->offscreen;
        renderLightingPassOffscreen = renderer->InitRenderTarget(offscreenDim, offscreenDim,
                                      &offscreen);
        shadowTexDim = offscreenDim;
    }

    if (!renderLightingPassOffscreen) {
        int shadowMapDim = 512;
        Configuration::GetIntValue("shadow-map-dimension", shadowMapDim);

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling)? "SilverLining::Cumulus Cloud Luminance Texture" : (const char *)0;

        TextureHandle& shadowTex = tcsUserData->shadowTex;
        renderer->CreateLuminanceTexture(shadowMapDim, shadowMapDim, &shadowTex, name);
        shadowTexDim = shadowMapDim;
    }
}

bool SILVERLINING_API CumulusCloudLayer::AddCloudAt(const Atmosphere& atm, const Vector3& relativePosition, const Vector3& dimensions, void* data)
{
    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    if (!atm.IsInitialized()) return false;

    if (MathUtils::Abs(relativePosition.x) > GetBaseWidth() * 0.5) return false;
    if (MathUtils::Abs(relativePosition.z) > GetBaseLength() * 0.5) return false;
    if (relativePosition.y < 0) return false;

    if (dimensions.x < parameters.voxelSize || dimensions.y < parameters.voxelSize ||
            dimensions.z < parameters.voxelSize) return false;

    parameters.width = dimensions.x;
    parameters.depth = dimensions.z;
    parameters.height = dimensions.y;
    parameters.spinRate = GetSpinRate();

    const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);

    CumulusCloud *cloud = CreateCloud(this, atm.GetRandomNumberGenerator()->UniformRandomDouble(), voxelShaderInstancing, tcsData->DrawIndirect(), tcsData);
    cloud->Init(parameters, atm, tcsData);

    const Camera* camera = tcsData->GetCamera();

    double velocity, heading;
    atm.GetConditions().GetWind(velocity, heading, GetBaseAltitude(tcsData));
    double dx = localWindX + sin(MathUtils::ToRadians(heading)) * velocity;
    double dz = localWindZ + cos(MathUtils::ToRadians(heading)) * velocity;
    cloud->ApplyWindShear(Vector3(dx, 0, dz) * camera->GetBasis3x3(), tcsData);

    double layerX, layerZ;
    double baseAlt;
    GetLayerPosition(layerX, layerZ, tcsData);
    baseAlt = GetBaseAltitude(tcsData);

    Vector3 center(layerX, baseAlt, layerZ);

    Vector3 cloudPos = relativePosition + center;
    Vector3 layerPos = relativePosition;

    cloud->SetNeedsGeocentricPlacement(true);
    cloud->SetWorldPosition(cloudPos * camera->GetBasis3x3());
    cloud->SetLayerPosition(layerPos);

    AddCloud(cloud, tcsData);

    const BillboardBufferProvider* provider = tcsData->GetBillboardBufferProvider();

    for (int i = 0; i < parameters.initialEvolveSteps; i++) {
        cloud->IncrementTimeStep(0, atm.GetRandomNumberGenerator(), provider, tcsData);
    }

    cloud->CloudPlaced(atm, tcsData);

    return true;
}

void CumulusCloudLayer::CreateCloud(const Atmosphere& atm, double cloudWidth, double cloudDepth, double cloudHeight, ThreadCameraStreamData* tcsData)
{
    if (cloudWidth > parameters.voxelSize && cloudDepth > parameters.voxelSize
            && cloudHeight > parameters.voxelSize) {
        if (!CreateCloudFromFile(atm, 0, tcsData)) {
            parameters.width = cloudWidth;
            parameters.depth = cloudDepth;
            parameters.height = cloudHeight;
            parameters.spinRate = GetSpinRate();

            const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);
            CumulusCloud *cloud = CreateCloud(this, atm.GetRandomNumberGenerator()->UniformRandomDouble(), voxelShaderInstancing, tcsData->DrawIndirect(), tcsData);
            cloud->Init(parameters, atm, tcsData);

            double worldWidth, worldDepth, worldHeight;
            cloud->GetSize(worldWidth, worldDepth, worldHeight);
            if (worldHeight > maxHeight) maxHeight = worldHeight;
            if (worldWidth > maxSize) maxSize = worldWidth;
            if (worldDepth > maxSize) maxSize = worldDepth;

            const Camera* camera = tcsData->GetCamera();

            double velocity, heading;
            atm.GetConditions().GetWind(velocity, heading, GetBaseAltitude(tcsData));
            double dx = localWindX + sin(MathUtils::ToRadians(heading)) * velocity;
            double dz = localWindZ + cos(MathUtils::ToRadians(heading)) * velocity;
            cloud->ApplyWindShear(Vector3(dx, 0, dz) * camera->GetBasis3x3(), tcsData);

            if (distributor->PlaceCloud(cloud, parameters.minimumDistanceScale, atm.GetRandomNumberGenerator(), tcsData)) {
                AddCloud(cloud, tcsData);

                AddToCoverage(cloud, tcsData);

                const BillboardBufferProvider* provider = tcsData->GetBillboardBufferProvider();

                for (int i = 0; i < parameters.initialEvolveSteps; i++) {
                    cloud->IncrementTimeStep(0, atm.GetRandomNumberGenerator(), provider, tcsData);
                }

                cloud->CloudPlaced(atm, tcsData);
            } else {
                SL_DELETE cloud;
            }
        }
    }

}

bool CumulusCloudLayer::SeedClouds(const Atmosphere& atm, void* data)
{
    SL_ASSERT(&atm == &atmosphere);
    if (!atmosphere.IsInitialized()) return false;

    ThreadCameraStreamData* tcsData = (data) ? static_cast<ThreadCameraStreamData*>(data) : atmosphere.GetDefaultTcsData();
    SL_ASSERT(tcsData);

    tcsData->SetForceImmediate(true);

    const Renderer* renderer = tcsData->GetRenderer();

    renderLightingPassOffscreen = false;

    ClearClouds(tcsData);

    ScanCloudFiles(atmosphere);

    SetupSurfaces(tcsData);

    if (generator && distributor) {
        generator->SetDesiredArea(GetBaseWidth() * GetBaseLength());
        generator->SetDesiredCoverage(GetDensity());

        double layerX, layerZ;
        double width, depth;
        double baseAlt, thickness;
        GetLayerPosition(layerX, layerZ, tcsData);
        baseAlt = GetBaseAltitude(tcsData);
        thickness = GetThickness();
        width = GetBaseWidth();
        depth = GetBaseLength();

        Vector3 minCorner(layerX - (width * 0.5), baseAlt, layerZ - (depth * 0.5));
        Vector3 maxCorner(layerX + (width * 0.5), baseAlt + thickness, layerZ + (depth * 0.5));

        distributor->Init(minCorner, maxCorner, parameters.voxelSize);

        generator->StartGeneration();

        double cloudWidth, cloudDepth, cloudHeight;

        maxHeight = maxSize = 0;

        InitCoverageMap();

        while (generator->GetNextCloud(cloudWidth, cloudDepth, cloudHeight, atmosphere.GetRandomNumberGenerator())) {
            CreateCloud(atmosphere, cloudWidth, cloudDepth, cloudHeight, tcsData);
        }

        int maxExtraRuns = 1;
        Configuration::GetIntValue("cumulus-max-extra-generation-runs", maxExtraRuns);
        double densityPrecision = 0.05;
        Configuration::GetDoubleValue("cumulus-density-precision", densityPrecision);

        int extraRuns = 0;
        while (GetCoverage() < GetDensity() * (1.0 - densityPrecision)) {
            extraRuns++;
            if (extraRuns > maxExtraRuns) {
                break;
            }

            generator->StartGeneration();
            while (generator->GetNextCloud(cloudWidth, cloudDepth, cloudHeight, atmosphere.GetRandomNumberGenerator())) {
                CreateCloud(atmosphere, cloudWidth, cloudDepth, cloudHeight, tcsData);
                if (GetCoverage() >= GetDensity()) {
                    break;
                }
            }
        }

        DeleteCoverageMap();
    }

    tcsData->SetForceImmediate(false);

    return true;
}

static SL_STRING GetFileExtension(const SL_STRING& FileName)
{
    if (FileName.find_last_of(".") != std::string::npos)
        return FileName.substr(FileName.find_last_of(".") + 1);
    return "";
}

void CumulusCloudLayer::ScanCloudFiles(const Atmosphere& atm)
{
    SL_STRING cloudPath("Clouds/");
    cloudPath += cloudTypeStr;
    cloudPath += "/";

    SL_STRING cloudExtLower, cloudExtUpper;

    if (useVrmlCloudFormat) {
        cloudPath += "vrml/";
        cloudExtLower = "wrl";
        cloudExtUpper = "WRL";
    } else {
        cloudPath += "editor/";
        cloudExtLower = "dat";
        cloudExtUpper = "DAT";
    }

    SL_VECTOR(SL_STRING) cloudFileNames;
    Atmosphere::GetResourceLoader()->GetFilesInDirectory(cloudPath.c_str(), cloudFileNames);

    cloudFiles.clear();
    SL_VECTOR(SL_STRING) ::iterator it;
    for (it = cloudFileNames.begin(); it != cloudFileNames.end(); it++) {
        SL_STRING fname = *it;
        SL_STRING fext = GetFileExtension(fname);
        if (fext == cloudExtUpper || fext == cloudExtLower) {
            cloudFiles.push_back(cloudPath + fname);
        }
    }
}

TextureHandle CumulusCloudLayer::GetCustomAtlasTexture(ThreadCameraStreamData* tcsData) const
{
    if (atlasName.empty()) {
        return 0;
    }
    TextureManager* textureManager = (atmosphere.TexturesAreShared()) ? atmosphere.GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
    TextureHandle atlasTexture = textureManager->GetOrCreateNamedAtlasTexture(atlasName);
    return atlasTexture;
}

bool CumulusCloudLayer::CreateCloudFromFile(const Atmosphere& atm, double scaleTo, ThreadCameraStreamData* tcsData)
{
    bool success = false;

    int nClouds = (int)cloudFiles.size();
    if (nClouds > 0) {
        int idx = atm.GetRandomNumberGenerator()->UniformRandomIntRange(0, nClouds - 1);
        SL_STRING fname = cloudFiles[idx];
        char *data;
        char *rotationData;
        unsigned int dataLen;
        unsigned int rotationDataLen;

        parameters.width = GetBaseWidth();
        parameters.depth = GetBaseLength();
        parameters.height = GetThickness();
        parameters.spinRate = GetSpinRate();

        if (Atmosphere::GetResourceLoader()->LoadResource(fname.c_str(), data, dataLen, useVrmlCloudFormat)) {
            std::istream* stream = 0;

            stream = SL_NEW std::stringstream(std::string(data, dataLen));

            // Load the specific texture atlas within the cloud path resource directory.
            if (!useVrmlCloudFormat) {
                SL_STRING atlasPath("Clouds/");
                atlasPath += cloudTypeStr;
                SL_VECTOR(SL_STRING) atlasFileNames;
                Atmosphere::GetResourceLoader()->GetFilesInDirectory(atlasPath.c_str(), atlasFileNames);

                SL_VECTOR(SL_STRING) tgaFiles;
                tgaFiles.clear();
                SL_VECTOR(SL_STRING) ::iterator it;
                for (it = atlasFileNames.begin(); it != atlasFileNames.end(); it++) {
                    SL_STRING fname = *it;
                    SL_STRING fext = GetFileExtension(fname);
                    if (fext == "TGA" || fext == "tga") {
                        tgaFiles.push_back(fname);
                    }
                }
                if (tgaFiles.size() > 0) {
                    atlasName = atlasPath + "/" + tgaFiles[0];
                    // preload the atlas texture.
                    // If we are using deferred or using vulkan, this won't load later on
                    if (tcsData->GetDeferred() || tcsData->GetRenderer()->GetIsVulkan()) {
                        GetCustomAtlasTexture(tcsData);
                    }
                    SetUsingCloudAtlas(true);
                }
            }

            // Get rotation flags stream (if available) for cloud atlas
            std::istream* rotationStream = 0;
            if (usingCloudAtlas) {
                SL_STRING rotationFlagFile("Clouds/");
                rotationFlagFile += cloudTypeStr;
                rotationFlagFile += "/";
                rotationFlagFile += "rotationFlags.in";

                if (Atmosphere::GetResourceLoader()->LoadResource(rotationFlagFile.c_str(), rotationData, rotationDataLen, true)) {
                    rotationStream = SL_NEW std::stringstream(std::string(rotationData, rotationDataLen));
                }
            }

            const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);
            CumulusCloud *cloud = CreateCloud(this, atm.GetRandomNumberGenerator()->UniformRandomDouble(), voxelShaderInstancing, tcsData->DrawIndirect(), tcsData);
            if (cloud->InitFromFile(parameters, stream, rotationStream, useVrmlCloudFormat, &atm, 1.0, tcsData)) {
                double width, depth, height;
                if (scaleTo > 0) {
                    cloud->GetSize(width, depth, height);
                    double s = scaleTo / height;
                    stream->clear();
                    stream->seekg(0, std::ios_base::beg);
                    if (rotationStream) {
                        rotationStream->clear();
                        rotationStream->seekg(0, std::ios_base::beg);
                    }
                    cloud->InitFromFile(parameters, stream, rotationStream, useVrmlCloudFormat, &atm, s, tcsData);
                }
                cloud->GetSize(width, depth, height);
                if (height > maxHeight) {
                    maxHeight = height;
                }
                if (depth > maxSize) {
                    maxSize = depth;
                }
                if (width > maxSize) {
                    maxSize = width;
                }

                if (distributor) {
                    distributor->PlaceCloud(cloud, parameters.minimumDistanceScale, atm.GetRandomNumberGenerator(), tcsData);
                } else {
                    double x, z;
                    GetLayerPosition(x, z, tcsData);
                    Vector3 pos(x, GetBaseAltitude(tcsData), z);
                    pos = pos * tcsData->GetCamera()->GetBasis3x3();
                    cloud->SetWorldPosition(pos);
                }

                AddCloud(cloud, tcsData);
                cloud->CloudPlaced(atm, tcsData);
                success = true;
            } else {
                SL_DELETE cloud;
            }

            SL_DELETE stream;
            Atmosphere::GetResourceLoader()->FreeResource(data);

            if (rotationStream) {
                SL_DELETE rotationStream;
                Atmosphere::GetResourceLoader()->FreeResource(rotationData);
            }
        }
    }

    return success;
}

bool CumulusCloudLayer::DrawSetup(int pass, const Vector3 *lightPos, const Color *lightColor, ThreadCameraStreamData* tcsData) const
{
    Renderer* renderer = tcsData->GetRenderer();

    if (pass == 0) {

    } else {
        // Set the blend and z buffer modes
        renderer->EnableBlending(ONE, INVSRCALPHA);
        renderer->EnableDepthReads(true);
        renderer->EnableDepthWrites(false);
        renderer->EnableTexture2D(true);
        renderer->EnableLighting(false);
    }

    return true;
}

bool CumulusCloudLayer::EndDraw(int pass) const
{
    return true;
}

void CumulusCloudLayer::ReadConfiguration(const SL_STRING& configPrefix)
{
    cloudTypeStr = configPrefix;

    Configuration::GetDoubleValue((configPrefix + "-voxel-dimension").c_str(),
                                  parameters.voxelSize);
    parameters.voxelSize *= Atmosphere::GetUnitScale();

    Configuration::GetDoubleValue((configPrefix + "-extinction-probability").c_str(),
                                  parameters.extinctionProbability);
    Configuration::GetDoubleValue((configPrefix + "-transition-probability").c_str(),
                                  parameters.transitionProbability);
    Configuration::GetDoubleValue((configPrefix + "-vapor-probability").c_str(),
                                  parameters.vaporProbability);
    Configuration::GetDoubleValue((configPrefix + "-initial-vapor-probability").c_str(),
                                  parameters.initialVaporProbability);
    Configuration::GetIntValue((configPrefix + "-initial-evolve").c_str(),
                               parameters.initialEvolveSteps);
    Configuration::GetIntValue((configPrefix + "-time-step-interval").c_str(),
                               parameters.timeStepInterval);
    Configuration::GetDoubleValue((configPrefix + "-droplet-size").c_str(),
                                  parameters.dropletSize);
    parameters.dropletSize *= Atmosphere::GetUnitScale();

    Configuration::GetDoubleValue((configPrefix + "-water-content").c_str(),
                                  parameters.waterContent);
    parameters.waterContent /= pow(Atmosphere::GetUnitScale(), 3.0);

    Configuration::GetDoubleValue((configPrefix + "-albedo").c_str(),
                                  parameters.albedo);

    Configuration::GetDoubleValue((configPrefix + "-droplets-per-cubic-cm").c_str(),
                                  parameters.dropletsPerCubicCm);
    parameters.dropletsPerCubicCm /= pow(Atmosphere::GetUnitScale(), 3.0);

    parameters.ambientScattering = 0;
    Configuration::GetFloatValue((configPrefix + "-ambient-scattering").c_str(),
                                 parameters.ambientScattering);

    parameters.ambientScatteringHiRes = 0;
    Configuration::GetFloatValue((configPrefix + "-ambient-scattering-hi-res").c_str(),
                                 parameters.ambientScatteringHiRes);

    parameters.attenuation = 0;
    Configuration::GetDoubleValue((configPrefix + "-attenuation").c_str(),
                                  parameters.attenuation);
    parameters.attenuation *= Atmosphere::GetUnitScale();

    parameters.attenuationHiRes = 0;
    Configuration::GetDoubleValue((configPrefix + "-attenuation-hi-res").c_str(),
                                  parameters.attenuationHiRes);
    parameters.attenuationHiRes *= Atmosphere::GetUnitScale();

    parameters.minimumDistanceScale = 1.0;
    Configuration::GetDoubleValue((configPrefix + "-minimum-cloud-distance-scale").c_str(),
                                  parameters.minimumDistanceScale);

    parameters.verticalGradient = 0;
    Configuration::GetDoubleValue((configPrefix + "-vertical-gradient").c_str(),
                                  parameters.verticalGradient);

    generator = CloudGeneratorFactory::Create(configPrefix);
    distributor = CloudDistributorFactory::Create(configPrefix);

    useVrmlCloudFormat = true;
    Configuration::GetBoolValue("use-vrml-cloud-format", useVrmlCloudFormat);
}

void CumulusCloudLayer::GenerateShadowMaps(bool enable)
{
    makeShadowMaps = enable;
}

void CumulusCloudLayer::MoveClouds(double x, double y, double z, ThreadCameraStreamData* tcsData)
{
    CloudLayer::MoveClouds(x, y, z, tcsData);
    shadowMapElapsedWind = shadowMapElapsedWind + Vector3(x, y, z);
}

bool CumulusCloudLayer::BindShadowMap(int textureStage, double *m, ThreadCameraStreamData* tcsData) const
{
    CumulusCloudLayerTcsUserData* tcsUserData = GetOrCreateCumulusCloudLayerTcsUserData(tcsData);
    TextureHandle& shadowTex = tcsUserData->shadowTex;

    if (!shadowTex) return false;
    Renderer* renderer = tcsData->GetRenderer();

    renderer->EnableTexture(shadowTex, textureStage);

    Vector3 currentPos(0,0,0);
    GetLayerPosition(currentPos.x, currentPos.z, tcsData);

    Matrix4 lightTrans;
    if (!GetIsInfinite()) {
        lightTrans.elem[0][3] = shadowMapPos.x - currentPos.x;
        lightTrans.elem[2][3] = shadowMapPos.z - currentPos.z;
    } else {
        lightTrans.elem[0][3] = -shadowMapElapsedWind.x;
        lightTrans.elem[2][3] = -shadowMapElapsedWind.z;
    }

    Matrix4 lightProjViewTrans = lightProjView * lightTrans;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            *m++ = lightProjViewTrans.elem[row][col];
        }
    }

    return true;
}

bool CumulusCloudLayer::SaveClouds(std::ostream& s, ThreadCameraStreamData* tcsData) const
{
    s.write((char *)&parameters, sizeof(CumulusCloudInit));
    s.write((char *)&makeShadowMaps, sizeof(bool));

    s.write((char *)&spinRate, sizeof(double));
    s.write((char *)&growth, sizeof(bool));

    return CloudLayer::SaveClouds(s, tcsData);
}

bool CumulusCloudLayer::RestoreClouds(const Atmosphere& atm, std::istream& s, ThreadCameraStreamData* tcsData)
{
    s.read((char *)&parameters, sizeof(CumulusCloudInit));
    s.read((char *)&makeShadowMaps, sizeof(bool));

    s.read((char *)&spinRate, sizeof(double));
    s.read((char *)&growth, sizeof(bool));

    int nClouds;
    s.read((char *)&nClouds, sizeof(int));

    SetupSurfaces(tcsData);

    const bool voxelShaderInstancing = (tcsData->GetBillboardShader() && tcsData->GetBillboardShader()->GetShaderHandle() != 0);

    for (int i = 0; i < nClouds; i++) {
        CumulusCloud *cloud = CreateCloud(this, atm.GetRandomNumberGenerator()->UniformRandomDouble(), voxelShaderInstancing, tcsData->DrawIndirect(), tcsData);
        if (cloud->Unserialize(s, tcsData, atm.GetRandomNumberGenerator(), tcsData->GetBillboardBufferProvider())) {
            AddCloud(cloud, tcsData);

            double width, depth, height;
            cloud->GetSize(width, depth, height);
            if (height > maxHeight) {
                maxHeight = height;
            }
            if (width > maxSize) {
                maxSize = width;
            }
            if (depth > maxSize) {
                maxSize = depth;
            }

            cloud->CloudPlaced(atm, tcsData);
        } else {
            SL_DELETE cloud;
        }
    }

    return true;
}
}