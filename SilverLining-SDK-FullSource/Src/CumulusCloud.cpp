// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "CumulusCloud.h"
#include "CumulusCloudLayer.h"
#include "Configuration.h"
#include "Voxel.h"
#include "Renderer.h"
#include "Billboard.h"
#include "BillboardBuffer.h"
#include "Metaball.h"
#include "Utils.h"
#include "Sky.h"
#include "EnvironmentMap.h"
#include "MathUtils.h"
#include "Camera.h"
#include "OverriddenBillboardMatrix.h"
#include "BillboardRenderingParameters.h"
#include "MetaballRenderingParameters.h"
#include "ThreadCameraStreamData.h"
#include "TextureManager.h"
#include "BillboardRenderer.h"
#include "QuadBatches.h"
#include "CloudRenderingParameters.h"
#include "CumulusCloudRenderingParameters.h"
#include "CloudCompilationOperation.h"
#include "SLAssert.h"

#include <algorithm>

using namespace std;

#define ELLIPSOID_BOUNDS 0
#define CULL_FADED_CLOUDS

namespace SilverLining
{
class CumulusCloudCompilationOperation : public CloudCompilationOperation
{
public:
    CumulusCloudCompilationOperation(bool isRightHanded);
    virtual ~CumulusCloudCompilationOperation();

    void Execute(ThreadCameraStreamData* tcsData);
public:
    const CumulusCloud* cumulusCloud;
    TextureHandle cloudTexture;
    double fade;
    Vector3 outputScale;
    unsigned long millis;

    Camera renderCamera;
    OverriddenBillboardMatrix overriddenBillboardMatrix;
};

CumulusCloudCompilationOperation::CumulusCloudCompilationOperation(bool isRightHanded)
    : renderCamera(isRightHanded)
    , overriddenBillboardMatrix(&renderCamera)
{
}

CumulusCloudCompilationOperation::~CumulusCloudCompilationOperation()
{
}

void CumulusCloudCompilationOperation::Execute(ThreadCameraStreamData* tcsData)
{
    bool savedFog = false;
    BillboardRenderer* billboardRenderer = tcsData->GetBillboardRenderer();
    // for some reason it matters whether we do this with a render camera, overridden billboard matrix
    // there's flickering otherwise
    billboardRenderer->StartBatchDraw(cloudTexture, fade, outputScale, true, millis, &renderCamera, &overriddenBillboardMatrix, savedFog);

    for (SL_VECTOR(Voxel*)::const_iterator it = cumulusCloud->drawList.begin(); it != cumulusCloud->drawList.end(); it++) {
        // here it is ok that render camera, overridden billboard matrix are 0. They are not used inside
        (*it)->DrawInBatch(false, 0, 0, tcsData);
    }
    // here it is ok that render camera is 0. Its not used inside except for an assertion check
    cumulusCloud->compiledObject = billboardRenderer->EndBatchDraw(0, savedFog, false, cumulusCloud->compiledObject);
    QuadBatches* quadBatches = cumulusCloud->compiledObject;
    if (cumulusCloud->drawIndirect && quadBatches->IsCompiledForIndirect() == false) {
        quadBatches->CompileForIndirect(tcsData);
        const_cast<CumulusCloud*>(cumulusCloud)->SetInstanced(true);
    }
}

CumulusCloud::CumulusCloud(CloudLayer *layer, double coverageThreshold, bool _voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* _tcsData) : Cloud(layer, coverageThreshold), width(0), height(0), depth(0), lastTimeStep(0), timeStepInterval(0)
    , voxels(0), compiledObject(0), fadeStartTime(0), voxelsDirty(true), voxelsUseShaderInstancing(_voxelsUseShaderInstancing), boundScale(0.25)
    , compilationParameters(0)
    , drawIndirect(_drawIndirect)
    , tcsData(_tcsData)
{
    lazyVoxelCreation = false;
    Configuration::GetBoolValue("lazy-voxel-creation", lazyVoxelCreation);
    CumulusCloudLayer *ccl = dynamic_cast<CumulusCloudLayer*>(layer);
    if (ccl) {
        if (ccl->GetGrowthEnabled()) {
            lazyVoxelCreation = false;
        }
    }
    minimumSpinVoxelHeight = 4;
    Configuration::GetIntValue("cumulus-minimum-spin-height", minimumSpinVoxelHeight);

    flattening = 1.0f;
    Configuration::GetFloatValue("cumulus-voxel-flattening", flattening);

    perCloudBillboards = false;
    Configuration::GetBoolValue("cumulus-billboards-per-cloud", perCloudBillboards);

    screenBlendFactor = 0.3;
    Configuration::GetDoubleValue("billboard-world-screen-blend-factor", screenBlendFactor);


    TextureManager* textureManager = 0;
    if (parentCloudLayer && parentCloudLayer->GetAtmosphere().TexturesAreShared()) {
        textureManager = parentCloudLayer->GetAtmosphere().GetDefaultTcsData()->GetTextureManager();
    } else {
        textureManager = tcsData->GetTextureManager();
    }
    SL_ASSERT(textureManager);
}

void CumulusCloud::Shear(double updraftSpeed, const Vector3& wind, ThreadCameraStreamData* tcsData)
{
    Vector3 minBounds(1.0E6, 1.0E6, 1.0E6);
    Vector3 maxBounds(-1.0E6, -1.0E6, -1.0E6);

    double cloudHeight = (height - 1) * voxelSize;

    double minUpdraftSpeed = (wind.Length() * cloudHeight) / (width * voxelSize);
    if (updraftSpeed < minUpdraftSpeed) {
        updraftSpeed = minUpdraftSpeed;
    }

    double t = cloudHeight / updraftSpeed;

    const Camera* camera = tcsData->GetCamera();
    Vector3 shearCenterOffset = (wind * t * 0.5) * camera->GetInverseBasis3x3();

    for (int k = 0; k < height; k++) {
        double dY = k * voxelSize;
        double t = dY / updraftSpeed;

        Vector3 shearOffset = (wind * t) * camera->GetInverseBasis3x3() - shearCenterOffset;

        for (int i = 0; i < width; i++) {
            for (int j = 0; j < depth; j++) {
                Vector3f wpf = voxels[i][j][k]->GetWorldPosition();
                Vector3 wp(wpf.x, wpf.y, wpf.z);
                wp = wp + shearOffset;

                voxels[i][j][k]->SetWorldPosition(wp);

                if (wp.x < minBounds.x) minBounds.x = wp.x;
                if (wp.y < minBounds.y) minBounds.y = wp.y;
                if (wp.z < minBounds.z) minBounds.z = wp.z;
                if (wp.x > maxBounds.x) maxBounds.x = wp.x;
                if (wp.y > maxBounds.y) maxBounds.y = wp.y;
                if (wp.z > maxBounds.z) maxBounds.z = wp.z;

            }
        }
    }

    //SetWorldPosition( GetWorldPosition() + shearCenterOffset );

    double voxelRadius = Voxel::ComputeVoxelRadius(voxelSize);
    bounds.x = (maxBounds.x - minBounds.x) + voxelRadius * 4.0;
    bounds.y = (maxBounds.y - minBounds.y) + voxelRadius * 4.0;
    bounds.z = (maxBounds.z - minBounds.z) + voxelRadius * 4.0;
}


bool CumulusCloud::InitFromFile(const CumulusCloudInit& init, std::istream *stream, std::istream *rotStream, bool useVrmlFormat, const Atmosphere* atmosphere, double scale, ThreadCameraStreamData* tcsData)
{
    voxelsDirty = true;

    const CumulusCloudRenderingParameters* params = tcsData->GetCumulusCloudRenderingParams();

    fogRefreshSlice = params->GetFogRefreshSlice();

    extinctionProbability = init.extinctionProbability;
    transitionProbability = init.transitionProbability;
    vaporProbability = init.vaporProbability;
    initialVaporProbability = init.initialVaporProbability;
    timeStepInterval = (unsigned int)(init.timeStepInterval * 1000.0);
    albedo = init.albedo;
    dropletsPerCubicCm = init.dropletsPerCubicCm;
    ambientScattering = init.ambientScattering;
    ambientScatteringHiRes = init.ambientScatteringHiRes;
    attenuation = init.attenuation;
    attenuationHiRes = init.attenuationHiRes;
    verticalGradient = init.verticalGradient;
    dropletSize = init.dropletSize;
    spinRate = init.spinRate;

    // Convert to cubic meters
    double dropletsPerCubicM = dropletsPerCubicCm * 100 * 100 * 100;

    // Build up a vector of puffs that will become voxels
    SL_VECTOR(Puff) puffs;

    if (useVrmlFormat) {
        InitFromVRMLFile(&puffs, stream, atmosphere, scale);
    } else {
        InitFromCloudEditorFile(&puffs, stream, atmosphere, scale);
    }

    // Get rotation flags from stream (if available)
    int* rotationFlags = 0;
    int realTextureIds[] = { 0, 13, 14, 15, 16, 9, 10, 11, 12, 5, 6, 7, 8, 1, 2, 3, 4 };
    int flagArraySize = 0;
    char buf[1024];
    if (rotStream) {
        int count = 0;

        while (rotStream->good()) {
            if (!rotationFlags) {
                rotStream->getline(buf, sizeof(int));
                SL_STRING s(reinterpret_cast<char*>(buf));

                std::stringstream converter(s.c_str());
                converter.imbue(std::locale("C"));
                converter >> flagArraySize;

                rotationFlags = SL_NEW int[flagArraySize];
            } else if (count < flagArraySize) {
                rotStream->getline(buf, sizeof(int)* flagArraySize);
                SL_STRING s(reinterpret_cast<char*>(buf));

                std::stringstream converter(s.c_str());
                converter.imbue(std::locale("C"));

                while (count < flagArraySize) {
                    converter >> rotationFlags[count];
                    count++;
                }
                break;
            }
        }
    }

    // Center it.
    double minX, maxX, minY, maxY, minZ, maxZ;
    minX = minY = minZ = 1.0E6;
    maxX = maxY = maxZ = -1.0E6;
    int i;

    for (i = 0; i < (int)puffs.size(); i++) {
        if (puffs[i].pos.x > maxX) maxX = puffs[i].pos.x;
        if (puffs[i].pos.x < minX) minX = puffs[i].pos.x;
        if (puffs[i].pos.y > maxY) maxY = puffs[i].pos.y;
        if (puffs[i].pos.y < minY) minY = puffs[i].pos.y;
        if (puffs[i].pos.z > maxZ) maxZ = puffs[i].pos.z;
        if (puffs[i].pos.z < minZ) minZ = puffs[i].pos.z;
    }
    double yOffset = -minY;
    double xMid = minX + (maxX - minX) * 0.5;
    double zMid = minZ + (maxZ - minZ) * 0.5;
    double xOffset = -xMid;
    double zOffset = -zMid;
    Vector3 offset(xOffset, yOffset, zOffset);

    for (i = 0; i < (int)puffs.size(); i++) {
        puffs[i].pos = puffs[i].pos + offset;
    }

    width = (int)puffs.size();
    height = 1;
    depth = 1;

    Vector3 minBounds(1.0E6, 1.0E6, 1.0E6);
    Vector3 maxBounds(-1.0E6, -1.0E6, -1.0E6);

    if (voxels) {
        FreeVoxels();
    }

    if (width > 0) {

    }

    const BillboardBufferProvider* provider = tcsData->GetBillboardBufferProvider();

    const bool hdr = tcsData->GetHDREnabled();
    voxels = (Voxel****)(SL_MALLOC(width * sizeof(Voxel*)));
    for (i = 0; i < width; i++) {
        SL_ASSERT(voxels);
        voxels[i] = (Voxel***)(SL_MALLOC(sizeof(Voxel*)));
        voxels[i][0] = (Voxel**)(SL_MALLOC(sizeof(Voxel*)));

        bool rotate = true;
        if (rotationFlags && puffs[i].textureId <= flagArraySize && puffs[i].textureId > 0) {
            rotate = rotationFlags[realTextureIds[puffs[i].textureId] - 1] != 0;
        }

        float _rotateAngle = 0;
        float _rotateSpeed = 0;

        if (rotate) {
            float spinRate = (float)(init.spinRate);
            Billboard::GetRandomRotateAngleAndSpeed(atmosphere->GetRandomNumberGenerator(), spinRate, _rotateAngle, _rotateSpeed);
        } else {
            _rotateAngle = MathUtilsFloat::Epsilon();
            _rotateSpeed = MathUtilsFloat::Epsilon();
        }

        if (useVrmlFormat) {
            voxels[i][0][0] = SL_NEW Voxel(puffs[i].radius, init.dropletSize, dropletsPerCubicM, init.spinRate, 0, _rotateAngle, _rotateSpeed, voxelsUseShaderInstancing, hdr, provider->GetABillboardBuffer());
        } else {
            voxels[i][0][0] = SL_NEW Voxel(puffs[i].radius, init.dropletSize, dropletsPerCubicM, init.spinRate, puffs[i].textureId, _rotateAngle, _rotateSpeed, voxelsUseShaderInstancing, hdr, provider->GetABillboardBuffer());
        }
        voxels[i][0][0]->SetWorldPosition(puffs[i].pos);
        voxels[i][0][0]->SetHasCloud(true);

        if (puffs[i].pos.x < minBounds.x) minBounds.x = puffs[i].pos.x;
        if (puffs[i].pos.y < minBounds.y) minBounds.y = puffs[i].pos.y;
        if (puffs[i].pos.z < minBounds.z) minBounds.z = puffs[i].pos.z;
        if (puffs[i].pos.x > maxBounds.x) maxBounds.x = puffs[i].pos.x;
        if (puffs[i].pos.y > maxBounds.y) maxBounds.y = puffs[i].pos.y;
        if (puffs[i].pos.z > maxBounds.z) maxBounds.z = puffs[i].pos.z;
    }

    double voxelRadius = Voxel::ComputeVoxelRadius(voxelSize);
    bounds.x = (maxBounds.x - minBounds.x) + voxelRadius * 4.0;
    bounds.y = (maxBounds.y - minBounds.y) + voxelRadius * 4.0;
    bounds.z = (maxBounds.z - minBounds.z) + voxelRadius * 4.0;

    if (rotationFlags) {
        SL_DELETE[] rotationFlags;
    }

    return puffs.size() > 0;
}

bool CumulusCloud::Init(const CumulusCloudInit& init, const Atmosphere& atmosphere, ThreadCameraStreamData* tcsData)
{

    const CumulusCloudRenderingParameters* params = tcsData->GetCumulusCloudRenderingParams();

    fogRefreshSlice = params->GetFogRefreshSlice();

    voxelSize = init.voxelSize;
    SL_ASSERT(voxelSize > 0);

    double minSizeFactor = 0.25;
    Configuration::GetDoubleValue("cumulus-minimum-voxel-size-factor", minSizeFactor);
    if (voxelSize > init.width * minSizeFactor) {
        voxelSize = init.width * minSizeFactor;
    }
    if (voxelSize > init.depth * minSizeFactor) {
        voxelSize = init.depth * minSizeFactor;
    }

    width = (int)(init.width / voxelSize);
    height = (int)(init.height / voxelSize);
    depth = (int)(init.depth / voxelSize);

    extinctionProbability = init.extinctionProbability;
    transitionProbability = init.transitionProbability;
    vaporProbability = init.vaporProbability;
    initialVaporProbability = init.initialVaporProbability;
    timeStepInterval = (unsigned int)(init.timeStepInterval * 1000.0);
    albedo = init.albedo;
    dropletsPerCubicCm = init.dropletsPerCubicCm;
    ambientScattering = init.ambientScattering;
    ambientScatteringHiRes = init.ambientScatteringHiRes;
    attenuation = init.attenuation;
    attenuationHiRes = init.attenuationHiRes;
    verticalGradient = init.verticalGradient;
    dropletSize = init.dropletSize;
    spinRate = init.spinRate;

    // Convert to cubic meters
    double dropletsPerCubicM = dropletsPerCubicCm * 100 * 100 * 100;

    const bool hdr = tcsData->GetHDREnabled();

    voxels = (Voxel****)(SL_MALLOC(width * sizeof(Voxel*)));
    for (int i = 0; i < width; i++) {
        SL_ASSERT(voxels);
        voxels[i] = (Voxel***)(SL_MALLOC(depth * sizeof(Voxel*)));
        for (int j = 0; j < depth; j++) {
            SL_ASSERT(voxels[i]);
            voxels[i][j] = (Voxel**)(SL_MALLOC(height * sizeof(Voxel*)));
            for (int k = 0; k < height; k++) {
                if (lazyVoxelCreation) {
                    voxels[i][j][k] = SL_NEW Voxel(voxelSize, init.dropletSize, dropletsPerCubicM);
                    SL_ASSERT(voxels[i][j][k]);
                } else {
                    float sizeVariation = (float)voxelSize * params->GetSizeRandomness();
                    float dSize = atmosphere.GetRandomNumberGenerator()->UniformRandomFloat() * sizeVariation;
                    dSize -= sizeVariation * 0.5f;

                    bool fixed = false;
                    int atlasIdx = 0;

                    if (GetParentCloudLayer()->UsingCloudAtlas()) {

                        if (height > minimumSpinVoxelHeight) {
                            float normalizedHeight = (float)(k) / (float)height;
                            float rnd = atmosphere.GetRandomNumberGenerator()->UniformRandomFloat();
                            fixed = normalizedHeight > rnd;
                        }

                        if (fixed) {
                            int roll = atmosphere.GetRandomNumberGenerator()->UniformRandomIntRange(0, 13);
                            int hiResTextures[14] = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 15, 16 };
                            atlasIdx = hiResTextures[roll];
                        } else {
                            int roll = atmosphere.GetRandomNumberGenerator()->UniformRandomIntRange(0, 3);
                            int spinningTextures[4] = { 9, 13, 8, 9 };
                            atlasIdx = spinningTextures[roll];
                        }
                    }

                    bool randomRotate = !fixed;
                    float _rotateAngle = 0;
                    float _rotateSpeed = 0;
                    if (randomRotate) {
                        float spinRate = (float)(init.spinRate);
                        Billboard::GetRandomRotateAngleAndSpeed(atmosphere.GetRandomNumberGenerator(), spinRate, _rotateAngle, _rotateSpeed);
                    } else {
                        _rotateAngle = MathUtilsFloat::Epsilon();
                        _rotateSpeed = MathUtilsFloat::Epsilon();
                    }

                    voxels[i][j][k] = SL_NEW Voxel(voxelSize + dSize, init.dropletSize, dropletsPerCubicM, init.spinRate, atlasIdx, _rotateAngle, _rotateSpeed, voxelsUseShaderInstancing, hdr, tcsData->GetBillboardBufferProvider()->GetABillboardBuffer());
                    SL_ASSERT(voxels[i][j][k]);
                }
            }
        }
    }

    // Compute bounds
    // actual cloud size is up to dimension + 2 times spacing of voxels
    // (to account for the position randomization) plus the radius of the
    // actual voxels on each end (*2)
    double voxelRadius = Voxel::ComputeVoxelRadius(voxelSize);
    bounds.x = (width + 2) * voxelSize + voxelRadius * 4.0;
    bounds.z = (depth + 2) * voxelSize + voxelRadius * 4.0;
    bounds.y = (height)* voxelSize + voxelRadius * 4.0;

    InitializeVoxelState(atmosphere.GetRandomNumberGenerator(), tcsData);

    return true;
}

void CumulusCloud::MakeVoxel(Voxel *v, int k, const RandomNumberGenerator* rng, const BillboardBuffer& buffer, ThreadCameraStreamData* tcsData)
{
    if (lazyVoxelCreation) {

        const CumulusCloudRenderingParameters* params = tcsData->GetCumulusCloudRenderingParams();

        float sizeVariation = (float)voxelSize * params->GetSizeRandomness();
        float dSize = rng->UniformRandomFloat() * sizeVariation;
        dSize -= sizeVariation * 0.5f;

        bool fixed = false;
        int atlasIdx = 0;

        if (GetParentCloudLayer()->UsingCloudAtlas()) {

            fixed = true;

            if (height > minimumSpinVoxelHeight) {
                float normalizedHeight = (float)(k) / (float)height;
                float rnd = rng->UniformRandomFloat();
                fixed = normalizedHeight > rnd;
            }

            if (fixed) {
                int roll = rng->UniformRandomIntRange(0, 13);
                int hiResTextures[14] = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 15, 16 };
                atlasIdx = hiResTextures[roll];
            } else {
                int roll = rng->UniformRandomIntRange(0, 3);
                int spinningTextures[4] = { 9, 13, 8, 9 };
                atlasIdx = spinningTextures[roll];
            }
        }

        v->AllocateGraphics(voxelSize + dSize, spinRate, atlasIdx, !fixed, rng, voxelsUseShaderInstancing, tcsData->GetHDREnabled(), buffer);
        v->SetWorldPosition(v->GetWorldPosition());
    }
}

void CumulusCloud::FreeVoxels()
{
    if (voxels) {
        int i, j, k;

        for (i = 0; i < width; i++) {
            for (j = 0; j < depth; j++) {
                for (k = 0; k < height; k++) {
                    if (voxels[i][j][k]) {
                        SL_DELETE voxels[i][j][k];
                    }
                }
                SL_FREE(voxels[i][j]);
            }
            SL_FREE(voxels[i]);
        }
        SL_FREE(voxels);
    }
    voxels = 0;
}

CumulusCloud::~CumulusCloud()
{
    FreeVoxels();

    if (compiledObject) SL_DELETE compiledObject;

    if (compilationParameters) SL_DELETE compilationParameters;
}

void CumulusCloud::GetSize(double& pwidth, double& pdepth, double& pheight) const
{
    pwidth = bounds.x;
    pdepth = bounds.z;
    pheight = bounds.y;
}

void CumulusCloud::Submit(Voxel *v)
{
    drawList.push_back(v);
}

struct VoxelComparator {
    public:
    VoxelComparator(const ThreadCameraStreamData* _tcsData
    , Vector3 _sortPos
    , Vector3 _offsetPos
    , VoxelSortingModes _sortMode)
        : tcsData(_tcsData)
        , sortPos(_sortPos)
        , offsetPos(_offsetPos)
        , sortMode(_sortMode)
    {
    }

    bool operator()(const Voxel* lhs, const Voxel* rhs) const
    {
        const Vector3f& lhsWorldPosition = lhs->GetWorldPosition();
        const Vector3f& rhsWorldPosition = rhs->GetWorldPosition();

        const Camera* camera = tcsData->GetCamera();

        const Matrix3& basis = camera->GetBasis3x3();
        const double lhsPosition = (sortPos - (Vector3(lhsWorldPosition.x, lhsWorldPosition.y, lhsWorldPosition.z) * basis + offsetPos)).SquaredLength();
        const double rhsPosition = (sortPos - (Vector3(rhsWorldPosition.x, rhsWorldPosition.y, rhsWorldPosition.z) * basis + offsetPos)).SquaredLength();

        if (sortMode == FRONT_TO_BACK) {
            return lhsPosition < rhsPosition;
        } else {
            return rhsPosition < lhsPosition;
        }
    }
protected:
    const ThreadCameraStreamData* tcsData;

    const Vector3 sortPos;
    const Vector3 offsetPos;
    const VoxelSortingModes sortMode;
};

void CumulusCloud::ShadeCloud(const Vector3& lightDir, const Color& lightColor, ThreadCameraStreamData* tcsData) const
{
    /* For each voxel, find the ray from the voxel to the light source. Then,
    find the intersection of this ray with the cloud's bounding ellipsoid.
    Attenuate the light for this voxel by the normalized distance into
    the cloud volume by the light ray hitting it.

    This technique is less accurate since it does not simulate the actual
    cloud voxels shading each other, but allows us to get reasonable visual
    results without the expense of doing pixel read-back during the lighting
    pass.
    */

    Vector3 V(lightDir * -1);

    double w, h, d;
    GetSize(w, d, h);

    // ray / ellipsoid intersection algorithm from Lengyel
    const double m = (double)w / (double)(h * 2.0); // height*2 since the volume is a
    // hemi-ellipsoid, we want the full ellipsoid.
    const double n = (double)w / (double)d;

    const double a = (V.x * V.x) + (m * m * V.y * V.y) + (n * n * V.z * V.z);
    const double twoa = 2.0 * a;
    const double invTwoA = 1.0 / twoa;
    const double foura = 4.0 * a;
    const double r = (double)w * 0.5;
    const double r2 = r * r;
    const double m2 = m * m;
    const double n2 = n * n;

    const double constTerm = albedo / (4.0 * MathUtils::Pi());

    const CumulusCloudRenderingParameters* params = tcsData->GetCumulusCloudRenderingParams();

    double finalAttenuation = params->GetQuickLightingAttenuation();
    if (attenuation > 0) {
        finalAttenuation = attenuation;
    }

    if (GetParentCloudLayer()->UsingCloudAtlas()) {
        finalAttenuation = params->GetQuickLightingAttenuationHiRes();
        if (attenuationHiRes > 0) {
            finalAttenuation = attenuationHiRes;
        }
    }

    const double invAttenuation = 1.0 / finalAttenuation;

    const bool hdr = tcsData->GetHDREnabled();

    for (SL_VECTOR(Voxel*)::const_iterator it = drawList.begin(); it != drawList.end(); it++) {
        const Vector3f& Sf = (*it)->GetWorldPosition(); // The voxel "world position" is really
        // object space
        Vector3 S(Sf.x, Sf.y, Sf.z);
        const double b = 2.0 * (S.x * V.x + m2 * S.y * V.y + n2 * S.z * V.z);
        const double c = S.x * S.x + m2 * S.y * S.y + n2 * S.z * S.z - r2;
        const double D = b * b - foura * c;

        double t;

        if (D >= 0) {
            t = (-b - sqrt(D)) * invTwoA; // argh, this sqrt seems unavoidable.

            t *= -invAttenuation;

            if (t > 1.0) t = 1.0;
            if (t < 0) t = 0;

            // t is now a parameter along the ray from the voxel to the sun.
            // Flip to represent the distance into the cloud volume.
            t = 1.0 - t;

            t *= constTerm * (*it)->GetOpticalDepth();

            Color voxelColor(t * lightColor.r,
                             t * lightColor.g,
                             t * lightColor.b,
                             1.0);

            (*it)->SetMetaballColor(voxelColor, hdr);
        } else {
            (*it)->SetMetaballColor(lightColor * (float)constTerm * (*it)->GetOpticalDepth(), hdr);
        }
    }

    //drawList.clear();
}

void CumulusCloud::DrawCloud(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
                             , ThreadCameraStreamData* tcsData) const
{
    Renderer* renderer = tcsData->GetRenderer();

    //TODO: Make billboards play nice with GL_FOG.
    //if (renderer->GetFogEnabled()) return;

    // Save off modelview matrix
    const Vector3& camPos = sceneCamera->GetPosition();

    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();
    CumulusCloudRenderingParameters* cumulusParams = tcsData->GetCumulusCloudRenderingParams();

    // Darken clouds with density
    Color currentForce;
    bool darknessApplied = false;
    if (!billboardParams->GetForceConstantColor(currentForce) && cumulusParams->GetDarkenWithDensity()) {
        darknessApplied = true;
        double density = GetParentCloudLayer()->GetDensity();
        if (density < 0) density = 0;
        if (density > 1.0) density = 1.0;
        double factor = 1.0 - (density * density * density * cumulusParams->GetDarkenAmount());
        Color darkness(factor, factor, factor, 0.0);
        billboardParams->ForceConstantColor(true, darkness);
    }

    MetaballRenderingParameters* metaballParams = tcsData->GetMetaballRenderingParams();
    Color originalAmbient;
    Vector3 forcedAmbient;
    bool ambientOverride = false, doLighting;
    if (GetParentCloudLayer()->GetOverriddenCloudColor(forcedAmbient, doLighting)) {
        originalAmbient = metaballParams->GetAmbientColor();
        Color forcedColor = Color(forcedAmbient.x, forcedAmbient.y, forcedAmbient.z);
        if (doLighting) {
            forcedColor = forcedColor * originalAmbient;
        }
        metaballParams->SetAmbientColor(forcedColor);
        tcsData->GetCloudRenderingParams()->SetAmbientColor(forcedColor);
        ambientOverride = true;
    }

    // Do a gradient
    billboardParams->SetVerticalGradient(verticalGradient);
    billboardParams->SetSoftness(GetParentCloudLayer()->GetIsSoft() ? (float)(voxelSize) : 0);
    billboardParams->SetFlattening(flattening);

    // Translate to the cloud position
    Matrix4 xlate;
    const Vector3& center = GetWorldPosition(tcsData);
    xlate.elem[0][3] = center.x;
    xlate.elem[1][3] = center.y;
    xlate.elem[2][3] = center.z;

    Camera adjustedCam(renderCamera, true, false);
    adjustedCam.MultiplyModelViewMatrix(xlate);
    if (GetInstanced() == false) {
        renderer->SetModelviewMatrix(adjustedCam.GetModelViewMatrix());
    }

    renderer->EnableBlending(cumulusParams->GetAtlasBlend() ? SRCALPHA : ONE, INVSRCALPHA);
    billboardParams->SetAdditiveBlending(!cumulusParams->GetAtlasBlend());
    renderer->EnableDepthReads(true);
    renderer->EnableDepthWrites(false);
    renderer->EnableBackfaceCulling(false);

    TextureHandle cloudTexture = 0;
    if (GetParentCloudLayer()->UsingCloudAtlas()) {
        cloudTexture = GetParentCloudLayer()->GetCustomAtlasTexture(tcsData);
        if (!cloudTexture) {
            TextureManager* textureManager = (atmosphere->TexturesAreShared()) ? atmosphere->GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
            cloudTexture = textureManager->GetAtlasTexture();
        }
    } else {
        TextureManager* textureManager = (atmosphere->TexturesAreShared()) ? atmosphere->GetDefaultTcsData()->GetTextureManager() : tcsData->GetTextureManager();
        cloudTexture = textureManager->GetWispTexture();
    }

    // The sort order burned into the quad batch will look wrong after the camera
    // angle changes enough...
    // Do an additional check with env map. This will prevent env map from invalidating
    // a cloud for no good reason
    if (cumulusParams->GetRecompiles() < cumulusParams->GetRecompileBudget() && !tcsData->GetIsDrawingEnvMap()) {
        Vector3 v1 = camPos - center;
        Vector3 v2 = lastCameraPos - center;
        v1.Normalize();
        v2.Normalize();
        double camAngle = MathUtils::Abs(v1.Dot(v2));
        if ((camAngle < cumulusParams->GetSortAngleThreshold()) || invalid) {
            invalid = false;
            cumulusParams->IncrementRecompiles();
            lastCameraPos = camPos;
            if (compiledObject) {
                compiledObject->Invalidate();
            }
        }
    }

    const unsigned long millis = atmosphere->GetConditions().GetMillisecondTimer()->GetMilliseconds();

    // Slide the ubo offset by one for this cloud and the set of ubo constants being set for this particular cloud.
    // This is needed for vulkan.
    renderer->IncrementConstantBuffersOffset(tcsData->GetBillboardShader()->GetShaderHandle());

    bool compiled = compiledObject && !compiledObject->IsInvalid();
    if (compiled == false) {
        metaballParams->SetCameraPosition(camPos);
        metaballParams->SetOffsetPosition(center);

        sort(drawList.begin(), drawList.end(), VoxelComparator(tcsData, camPos, center, BACK_TO_FRONT));

        // Draw every billboard
        const double randomness = (GetParentCloudLayer()->UsingCloudAtlas()) ? cumulusParams->GetColorRandomnessHiRes() :
                                  cumulusParams->GetColorRandomness();

        for (SL_VECTOR(Voxel*)::const_iterator it = drawList.begin(); it != drawList.end(); it++) {
            (*it)->ComputeBillboardColor(1, randomness, GetParentCloudLayer()->UsingCloudAtlas() ? ambientScatteringHiRes : ambientScattering, tcsData);
        }

        if (!compilationParameters) {
            compilationParameters = SL_NEW CumulusCloudCompilationOperation(renderCamera->IsRightHanded());
        }

        compilationParameters->cumulusCloud = this;
        compilationParameters->cloudTexture = cloudTexture;
        compilationParameters->fade = fade * alpha;
        compilationParameters->outputScale = atmosphere->GetOutputScale();
        compilationParameters->millis = millis;
        compilationParameters->renderCamera = adjustedCam;
        compilationParameters->overriddenBillboardMatrix = overriddenBillboardMatrix;

        compilationParameters->Execute(tcsData);
    }

    compiledObject->Draw(cloudTexture, fade * alpha, atmosphere, millis, &adjustedCam, overriddenBillboardMatrix, drawIndirect, tcsData);

    // Restore modelview matrix
    renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());

    billboardParams->SetAdditiveBlending(true);

    if (darknessApplied) {
        billboardParams->ForceConstantColor(false, currentForce);
    }

    if (ambientOverride) {
        metaballParams->SetAmbientColor(originalAmbient);
        tcsData->GetCloudRenderingParams()->SetAmbientColor(originalAmbient);
    }

    billboardParams->SetVerticalGradient(0);
    billboardParams->SetFlattening(1.0f);

    // Blow away list of clouds
    if (voxelsDirty) {
        //drawList.clear();
        voxelsDirty = false;
    }
}

bool CumulusCloud::IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    const Vector3& center = GetWorldPositionRef();

    double w = bounds.x * 0.5;
    double h = bounds.y;
    double d = bounds.z * 0.5;

    Frustum f = cullCamera->GetFrustumWorldSpace();
    if (parentCloudLayer->GetAtmosphere().GetFarCullingDisabled()) {
        f.EnableFarClipCulling(false);
    }

    for (int i = 0; i < f.GetNumCullingPlanes(); i++) {
        const Plane& p = f.GetPlane(i);
        const Vector3& N = p.GetNormal();
        double origin = N.Dot(center) + p.GetDistance();

        double RDotN = w * N.x;
        double SDotN = h * N.y;
        double TDotN = d * N.z;


#if ELLIPSOID_BOUNDS
        double rEff = sqrt(RDotN * RDotN + SDotN * SDotN + TDotN * TDotN);
#else // BOX_BOUNDS
        //  max limit is:  rEff = sqrt(w*w + h*h + d*d);
        double rEff = MathUtils::Abs(RDotN) + MathUtils::Abs(SDotN) + MathUtils::Abs(TDotN);
#endif

        if (origin < -rEff) {
            return true;
        }
    }

#ifdef CULL_FADED_CLOUDS
    if (fadeState == FADED_OUT) {
        unsigned int now = parentCloudLayer->GetAtmosphere().GetConditions().GetMillisecondTimer()->GetMilliseconds();
        if (now - fadeStartTime > timeStepInterval) {
            return true;
        }
    }

    const CumulusCloudRenderingParameters* cumulusParams = tcsData->GetCumulusCloudRenderingParams();

    const double fade = tcsData->GetBillboardRenderingParams()->CalculateFogFade(center, cullCamera);// *GetAlpha();
    if (fade <= cumulusParams->GetAlphaCullThreshold()) {
        return true;
    }
#endif

    return false;
}

bool CumulusCloud::Update(unsigned long now, bool forceUpdate, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
{
    CumulusCloudLayer *cloudLayer = dynamic_cast<CumulusCloudLayer*>(GetParentCloudLayer());
    if (!cloudLayer) return false;

    const CumulusCloudRenderingParameters* cumulusParams = tcsData->GetCumulusCloudRenderingParams();

    if (!cumulusParams->GetNoUpdate() && (forceUpdate || cloudLayer->GetGrowthEnabled())) {
        double dt = (double)(now - lastTimeStep) / (double)timeStepInterval;

        if (dt > 1.0) {
            IncrementTimeStep(now, rng, provider, tcsData);
            return true;
        }
    }

    return false;
}

bool CumulusCloud::Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
                        const Color& lightColor, bool pInvalid, const Sky *sky, bool, const Camera* camera
                        , ThreadCameraStreamData* tcsData)
{
    if (voxelsDirty) {
        drawList.clear();
    }

    const CumulusCloudRenderingParameters* cumulusParams = tcsData->GetCumulusCloudRenderingParams();

    /** Updating fog is expensive, so this gets spread out. */
    if (cumulusParams->GetSpreadOutFog()) {
        if (pass == 1) frameCounter++;
        if (frameCounter >= cumulusParams->GetFogRefreshFrequency()) {
            frameCounter = 0;
        }

        if ((frameCounter == fogRefreshSlice) || IsInvalid(tcsData) || pInvalid || pass == 0) {
            Vector3 dir = (GetWorldPosition(tcsData) + camera->GetUpVector() * (bounds.y * 0.5))
                          - camera->GetPosition();
            dir.Normalize();
            dir = dir * camera->GetInverseBasis3x3();
            fogColor = sky->SkyColorAt(dir, 0.0);
        }
    } else {
        Vector3 dir = (GetWorldPosition(tcsData) + camera->GetUpVector() * (bounds.y * 0.5))
                      - camera->GetPosition();
        dir.Normalize();
        dir = dir * camera->GetInverseBasis3x3();
        fogColor = sky->SkyColorAt(dir, 0.0);
    }

    if (pass == 0) {
        if (!LightingChanged(lightDir, tcsData) && !IsInvalid(tcsData) && !pInvalid) {
            return false;
        }

        if (compiledObject) {
            compiledObject->Invalidate();
        }

        invalid = false;
    }

    if (voxelsDirty) {
        int i, j, k;

        bool cullInteriorVoxels = cumulusParams->GetCullInteriorVoxels();

        for (i = 0; i < width; i++) {
            for (j = 0; j < depth; j++) {
                for (k = 0; k < height; k++) {
                    if (voxels[i][j][k]->GetHasCloud() || voxels[i][j][k]->GetDestroyed()) {
                        if (cullInteriorVoxels) {
                            if ((i > 0 && voxels[i - 1][j][k]->GetHasCloud()) &&
                                    (i < width - 1 && voxels[i + 1][j][k]->GetHasCloud()) &&
                                    (j > 0 && voxels[i][j - 1][k]->GetHasCloud()) &&
                                    (j < depth - 1 && voxels[i][j + 1][k]->GetHasCloud()) &&
                                    (k > 0 && voxels[i][j][k - 1]->GetHasCloud()) &&
                                    (k < height - 1 && voxels[i][j][k + 1]->GetHasCloud())) {
                                continue;
                            }
                        }

                        Submit(voxels[i][j][k]);
                    }
                }
            }
        }
    }

    if (drawList.size() > 0) {
        if (pass == 0) {
            Color nLightColorLocal = lightColor;
            nLightColorLocal.ScaleToUnitOrLess(tcsData->GetHDREnabled());
            ShadeCloud(lightDir * camera->GetInverseBasis3x3(), nLightColorLocal, tcsData);
        } else if (pass == 1) {
            Renderer* renderer = tcsData->GetRenderer();
            renderer->SubmitBlendedObject(this);
        }
    }

    return true;
}

void CumulusCloud::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                                     , ThreadCameraStreamData* tcsData) const
{
    BillboardRenderingParameters* billboardParams = tcsData->GetBillboardRenderingParams();

    Color oldFog = billboardParams->GetFogColor();
    billboardParams->SetFogColor(fogColor);

    billboardParams->SetFadeStartTime(fadeStartTime);

    if (!overriddenBillboardMatrix.isSet && perCloudBillboards) {
        Vector3 Normal = (GetWorldPosition(tcsData) - sceneCamera->GetPosition()) * -1.0;
        Vector3 halfBounds = (bounds)* boundScale;
        if (Normal.SquaredLength() > halfBounds.SquaredLength()) { // Direction to the cloud isn't meaningful when you're inside it...
            Normal.Normalize();
            Vector3 Up = sceneCamera->GetUpVector();

            const Matrix4& modelViewMatrix = sceneCamera->GetModelViewMatrix();
            Vector3 camRight = Vector3(modelViewMatrix.elem[0][0], modelViewMatrix.elem[0][1], modelViewMatrix.elem[0][2]);
            Vector3 altUp = Normal.Cross(camRight);
            altUp.Normalize();

            double screenBlend = MathUtils::Abs(Up.Dot(Normal));
            screenBlend *= screenBlendFactor;
            Up = (altUp * screenBlend) + (Up * (1.0 - screenBlend));

            Vector3 Right = Up.Cross(Normal);
            Right.Normalize();
            Up = Normal.Cross(Right);
            Up.Normalize();
            //Normal = Right.Cross(Up);
            //Normal.Normalize();

            Matrix4 billboard;
            if (sceneCamera->IsRightHanded()) {
                billboard = Matrix4(Right.x, Up.x, Normal.x, 0,
                                    Right.y, Up.y, Normal.y, 0,
                                    Right.z, Up.z, Normal.z, 0,
                                    0, 0, 0, 1);
            } else {
                billboard = Matrix4(Right.x, Up.x, -Normal.x, 0,
                                    Right.y, Up.y, -Normal.y, 0,
                                    Right.z, Up.z, -Normal.z, 0,
                                    0, 0, 0, 1);
            }

            DrawCloud(atmosphere, sceneCamera, renderCamera, OverriddenBillboardMatrix(billboard), tcsData);
        } else {
            DrawCloud(atmosphere, sceneCamera, renderCamera, overriddenBillboardMatrix, tcsData);
        }
    } else {
        DrawCloud(atmosphere, sceneCamera, renderCamera, overriddenBillboardMatrix, tcsData);
    }

    billboardParams->SetFogColor(oldFog);
}

void CumulusCloud::InitializeVoxelState(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    voxelsDirty = true;

    int i, j, k;
    Vector3 origin(0, 0, 0);
    origin.x -= width * voxelSize * 0.5;
    origin.z -= depth * voxelSize * 0.5;

    double midX = (double)width * 0.5;
    double midY = (double)height; // Use full height to simulate hemi-ellipsoid
    double midZ = (double)depth * 0.5;

    double a2 = midX * midX;
    double b2 = midY * midY;
    double c2 = midZ * midZ;

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                double x = (double)i - midX;
                double y = (double)k;
                double z = (double)j - midZ;
                double dist = (x * x) / a2 + (y * y) / b2 + (z * z) / c2;
                double scale = 1.0 - dist;
                if (scale < 0) scale = 0;

                Vector3 delta(i * voxelSize, k * voxelSize, j * voxelSize);

                Vector3 pos = origin + delta;

                if (voxelSize > 2.0) {
                    pos.x += rng->UniformRandomIntRange(0, (int)(voxelSize * 0.5) - 1);
                    pos.z += rng->UniformRandomIntRange(0, (int)(voxelSize * 0.5) - 1);
                }

                voxels[i][j][k]->SetWorldPosition(pos);

                voxels[i][j][k]->SetHasCloud(false);

                double rnd = rng->UniformRandomDouble();
                rnd *= scale;

                voxels[i][j][k]->SetVapor(rnd > (1.0 - initialVaporProbability));

                voxels[i][j][k]->SetPhaseTransition(false);
            }
        }
    }

    // seed the cloud

    if (width >= 4 && depth >= 4) {
        voxels[width >> 2][depth >> 2][0]->SetPhaseTransition(true);
        voxels[width >> 2][depth - (depth >> 2)][0]->SetPhaseTransition(true);
        voxels[width - (width >> 2)][depth - (depth >> 2)][0]->SetPhaseTransition(true);
        voxels[width - (width >> 2)][depth >> 2][0]->SetPhaseTransition(true);
    }

    lastTimeStep = (unsigned long)time(NULL);
}

void CumulusCloud::FadeIn(const MillisecondTimer* timer)
{
    if (fadeState != FADED_IN) {
        unsigned int now = timer->GetMilliseconds();
        fadeStartTime = now;
        float timeStepSeconds = (float)timeStepInterval * 0.001f;

        int i, j, k;

        for (i = 0; i < width; i++) {
            for (j = 0; j < depth; j++) {
                for (k = 0; k < height; k++) {
                    voxels[i][j][k]->SetFadeRate(1.0f / timeStepSeconds);
                }
            }
        }

        fadeState = FADED_IN;
        invalid = true;
    }
}

void CumulusCloud::FadeOut(const MillisecondTimer* timer)
{
    if (fadeState != FADED_OUT) {
        unsigned int now = timer->GetMilliseconds();
        fadeStartTime = now;
        float timeStepSeconds = (float)timeStepInterval * 0.001f;

        int i, j, k;

        for (i = 0; i < width; i++) {
            for (j = 0; j < depth; j++) {
                for (k = 0; k < height; k++) {
                    voxels[i][j][k]->SetFadeRate(-1.0f / timeStepSeconds);
                }
            }
        }

        fadeState = FADED_OUT;
        invalid = true;
    }
}

void CumulusCloud::IncrementTimeStep(unsigned long now, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
{
    voxelsDirty = true;

    fadeStartTime = now;

    double midX = (double)width * 0.5;
    double midY = (double)height; // Use full height to simulate hemi-ellipsoid
    double midZ = (double)depth * 0.5;

    double a2 = midX * midX;
    double b2 = midY * midY;
    double c2 = midZ * midZ;

    int i, j, k;

    const bool noUpdate = tcsData->GetCumulusCloudRenderingParams()->GetNoUpdate();

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                double x = (double)i - midX;
                double y = (double)k;
                double z = (double)j - midZ;
                double dist = (x * x) / a2 + (y * y) / b2 + (z * z) / c2;
                double scale = 1.0 - dist;
                if (scale < 0) scale = 0;

                bool fAct = (
                                (i + 1 < width ? voxels[i + 1][j][k]->GetPhaseTransition() : false) ||
                                (j + 1 < depth ? voxels[i][j + 1][k]->GetPhaseTransition() : false) ||
                                (k + 1 < height ? voxels[i][j][k + 1]->GetPhaseTransition() : false) ||
                                (i - 1 >= 0 ? voxels[i - 1][j][k]->GetPhaseTransition() : false) ||
                                (j - 1 >= 0 ? voxels[i][j - 1][k]->GetPhaseTransition() : false) ||
                                (k - 1 >= 0 ? voxels[i][j][k - 1]->GetPhaseTransition() : false) ||
                                (i - 2 >= 0 ? voxels[i - 2][j][k]->GetPhaseTransition() : false) ||
                                (i + 2 < width ? voxels[i + 2][j][k]->GetPhaseTransition() : false) ||
                                (j - 2 >= 0 ? voxels[i][j - 2][k]->GetPhaseTransition() : false) ||
                                (j + 2 < depth ? voxels[i][j + 2][k]->GetPhaseTransition() : false) ||
                                (k - 2 >= 0 ? voxels[i][j][k - 2]->GetPhaseTransition() : false)
                            );

                bool thisAct = voxels[i][j][k]->GetPhaseTransition();

                double rnd;
                rnd = rng->UniformRandomDouble();

                voxels[i][j][k]->SetPhaseTransition((
                                                        (!thisAct) &&
                                                        voxels[i][j][k]->GetVapor() &&
                                                        fAct) || (rnd < transitionProbability * scale));

                rnd = rng->UniformRandomDouble();
                voxels[i][j][k]->SetVapor(
                    (voxels[i][j][k]->GetVapor() && !thisAct) || (rnd < vaporProbability * scale));

                rnd = rng->UniformRandomDouble();
                bool oldHasCloud = voxels[i][j][k]->GetHasCloud();
                bool newHasCloud = ((oldHasCloud || thisAct) && (rnd > extinctionProbability * (1.0 - scale)));

                float timeStepSeconds = (float)timeStepInterval * 0.001f;
                if (newHasCloud && !oldHasCloud) {
                    if (!noUpdate) voxels[i][j][k]->SetFadeRate(1.0f / timeStepSeconds);
                    voxels[i][j][k]->SetCreated(true);
                    voxels[i][j][k]->SetDestroyed(false);
                } else if (!newHasCloud && oldHasCloud) {
                    if (!noUpdate) voxels[i][j][k]->SetFadeRate(-1.0f / timeStepSeconds);
                    voxels[i][j][k]->SetCreated(false);
                    voxels[i][j][k]->SetDestroyed(true);
                } else {
                    if (!noUpdate) voxels[i][j][k]->SetFadeRate(0.0);
                    voxels[i][j][k]->SetCreated(false);
                    voxels[i][j][k]->SetDestroyed(false);
                }

                voxels[i][j][k]->SetHasCloud(newHasCloud);

                if (newHasCloud) {
                    MakeVoxel(voxels[i][j][k], k, rng, provider->GetABillboardBuffer(), tcsData);
                }
            }
        }
    }

    invalid = true;

    lastTimeStep = now;
}

bool CumulusCloud::Unserialize(std::istream& s, ThreadCameraStreamData* tcsData, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider)
{

    Vector3 pos;
    pos.Unserialize(s);
    SetWorldPosition(pos);

    s.read((char *)&width, sizeof(int));
    s.read((char *)&height, sizeof(int));
    s.read((char *)&depth, sizeof(int));

    layerPosition.Unserialize(s);

    s.read((char *)&vaporProbability, sizeof(double));
    s.read((char *)&transitionProbability, sizeof(double));
    s.read((char *)&extinctionProbability, sizeof(double));
    s.read((char *)&initialVaporProbability, sizeof(double));
    s.read((char *)&albedo, sizeof(double));
    s.read((char *)&dropletsPerCubicCm, sizeof(double));
    s.read((char *)&attenuation, sizeof(double));
    s.read((char *)&attenuationHiRes, sizeof(double));
    s.read((char *)&ambientScattering, sizeof(float));
    s.read((char *)&ambientScatteringHiRes, sizeof(float));

    s.read((char *)&voxelSize, sizeof(double));

    s.read((char *)&verticalGradient, sizeof(double));

    // nLightColor is not a member anymore.
    // serialize dummy, to avoid bumping up the version
    Color nLightColorDummy;
    nLightColorDummy.Unserialize(s);

    bounds.Unserialize(s);

    const bool hdr = tcsData->GetHDREnabled();
    voxels = (Voxel****)(SL_MALLOC(width * sizeof(Voxel*)));
    for (int i = 0; i < width; i++) {
        SL_ASSERT(voxels);
        voxels[i] = (Voxel***)(SL_MALLOC(depth * sizeof(Voxel*)));
        for (int j = 0; j < depth; j++) {
            SL_ASSERT(voxels[i]);
            voxels[i][j] = (Voxel**)(SL_MALLOC(height * sizeof(Voxel*)));
            for (int k = 0; k < height; k++) {
                SL_ASSERT(voxels[i][j]);
                voxels[i][j][k] = SL_NEW Voxel(s, rng, voxelsUseShaderInstancing, hdr, provider->GetABillboardBuffer());
            }
        }
    }

    return true;
}

bool CumulusCloud::Serialize(std::ostream& s, ThreadCameraStreamData* tcsData)
{
    Vector3 pos = GetWorldPosition(tcsData);
    pos.Serialize(s);

    s.write((char *)&width, sizeof(int));
    s.write((char *)&height, sizeof(int));
    s.write((char *)&depth, sizeof(int));

    layerPosition.Serialize(s);

    s.write((char *)&vaporProbability, sizeof(double));
    s.write((char *)&transitionProbability, sizeof(double));
    s.write((char *)&extinctionProbability, sizeof(double));
    s.write((char *)&initialVaporProbability, sizeof(double));
    s.write((char *)&albedo, sizeof(double));
    s.write((char *)&dropletsPerCubicCm, sizeof(double));
    s.write((char *)&attenuation, sizeof(double));
    s.write((char *)&attenuationHiRes, sizeof(double));

    s.write((char *)&ambientScattering, sizeof(float));
    s.write((char *)&ambientScatteringHiRes, sizeof(float));

    s.write((char *)&voxelSize, sizeof(double));

    s.write((char *)&verticalGradient, sizeof(double));

    // nLightColor is not a member anymore.
    // serialize dummy, to avoid bumping up the version
    Color nLightColorDummy;
    nLightColorDummy.Serialize(s);

    bounds.Serialize(s);

    int i, j, k;

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                voxels[i][j][k]->Serialize(s);
            }
        }
    }

    return true;
}

bool CumulusCloud::ExportToVRML(const char *filename) const
{
    FILE *f = fopen(filename, "wt");
    if (f) {
        fprintf(f, "#VRML V2.0 utf8\n\n");

        int i, j, k;
        int n = 1;

        for (i = 0; i < width; i++) {
            for (j = 0; j < depth; j++) {
                for (k = 0; k < height; k++) {
                    if (voxels[i][j][k]->GetHasCloud()) {
                        fprintf(f, "DEF Sphere%d Transform {\n", n++);
                        fprintf(f, "  translation %f %f %f\n", voxels[i][j][k]->GetWorldPosition().x,
                                voxels[i][j][k]->GetWorldPosition().y, voxels[i][j][k]->GetWorldPosition().z);
                        fprintf(f, "  children [\n");
                        fprintf(f, "    Shape {\n");
                        fprintf(f, "      geometry Sphere { radius %f }\n", voxelSize);
                        fprintf(f, "    }\n");
                        fprintf(f, "  ]\n");
                        fprintf(f, "}\n\n");
                    }
                }
            }
        }

        fclose(f);
    }
    return true;
}

static void Swap(double& a, double& b)
{
    double tmp = b;
    b = a;
    a = tmp;
}

static bool SolveQuadratic(const double &a, const double &b, const double &c, double &x0, double &x1)
{
    double discr = b * b - 4 * a * c;
    if (discr < 0) return false;
    else if (discr == 0) x0 = x1 = -0.5 * b / a;
    else {
        double q = (b > 0) ?
                   -0.5 * (b + sqrt(discr)) :
                   -0.5 * (b - sqrt(discr));
        x0 = q / a;
        x1 = c / q;
    }
    if (x0 > x1) Swap(x0, x1);

    return true;
}

// Uncomment to take orientation of cloud billboards into account in intersection test
// Otherwise we treat them as spheres
//#define USE_PLANE

void CumulusCloud::Intersect(const Vector3& origin,
                             const Vector3& direction,
                             SL_VECTOR(double)& hit_ranges, ThreadCameraStreamData* tcsData)
{
    // Some Linear Algebra...
    // plane eq. -  point-normal form
    // n dot (r - r0) = 0 expands to n dot r = n dot r0
    // or d = -(n dot r0)
    // Ray-Plane intersection:
    // Ray: P = P0 + tV
    // Plane: P dot N + d = 0
    // Substituting for P, we get:
    // (P0 + tV) dot N + d = 0
    // Solving for t:
    // t = -(P0 dot N + d) / V dot N
    // Which gives point on the plane:
    // P = P0 + tV

    //SL_VECTOR(Voxel*) ::iterator it;

    const Camera* camera = tcsData->GetCamera();

    // world cloud position
    Vector3 cloud_pos = GetWorldPosition(tcsData);

#ifdef USE_PLANE
    // world billboard matrix
    Matrix4 billboard = renderer->GetBillboardMatrix();

    // world space billboard normal
    Vector3 normal = Vector3(billboard.elem[0][2],
                             billboard.elem[1][2],
                             billboard.elem[2][2]);
#endif

    const bool cullInteriorVoxels = tcsData->GetCumulusCloudRenderingParams()->GetCullInteriorVoxels();

    int i, j, k;

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                if (voxels[i][j][k]->GetHasCloud() || voxels[i][j][k]->GetDestroyed()) {
                    if (cullInteriorVoxels) {
                        if ((i > 0 && voxels[i - 1][j][k]->GetHasCloud()) &&
                                (i < width - 1 && voxels[i + 1][j][k]->GetHasCloud()) &&
                                (j > 0 && voxels[i][j - 1][k]->GetHasCloud()) &&
                                (j < depth - 1 && voxels[i][j + 1][k]->GetHasCloud()) &&
                                (k > 0 && voxels[i][j][k - 1]->GetHasCloud()) &&
                                (k < height - 1 && voxels[i][j][k + 1]->GetHasCloud())) {
                            continue;
                        }
                    }

                    Voxel* v = voxels[i][j][k];

                    // get voxel offset in object space
                    Vector3f vf_pos = v->GetWorldPosition();

                    // convert to vector of doubles in order to transform to world space
                    Vector3 voxel_pos = Vector3(vf_pos.x, vf_pos.y, vf_pos.z);

                    // compute voxel offset in world space
                    voxel_pos = voxel_pos * camera->GetBasis3x3();

                    // add the offset to the parent cloud's world center position
                    // to obtain the voxel's final world space position
                    Vector3 pos(cloud_pos.x + voxel_pos.x,
                                cloud_pos.y + voxel_pos.y,
                                cloud_pos.z + voxel_pos.z);

                    // Get the half-size of the square-shaped billboard
                    double radius = v->GetRadius();

#ifdef USE_PLANE
                    // Compute our 'd' for the voxel's plane equat}ion
                    // plane eq: PdotN + d = 0 so d = -PdotN
                    double d = -pos.Dot(normal);

                    // We now compute the intersection of our
                    // ray and this voxel's plane equation
                    // eye vector as the plane's normal but we negate it
                    // See derivation above
                    Vector3 dir = direction;

                    double DdN = dir.Dot(normal);
                    if (DdN < 1E-6) continue;
                    double t = -(origin.Dot(normal) + d) / DdN;

                    // We now have the point on the plane
                    Vector3 p = origin + dir * t;

                    // We test for containment using this voxel's radius
                    // Compute the vector between the center of our billboard
                    // and the intersection point on the plane
                    // Note: we are testing against a square not a circle
                    Vector3 delta = p - pos;

                    // Check if the hit point is within a square of width
                    // 2 * radius positioned at the voxel's center
                    if (delta.x < -radius ||
                            delta.x > radius ||
                            delta.y < -radius ||
                            delta.y > radius ||
                            delta.z < -radius ||
                            delta.z > radius) {
                        continue;
                    }
#else
                    double radius2 = radius * radius;
                    Vector3 L = origin - pos;
                    double a = direction.Dot(direction);
                    double b = 2.0 * direction.Dot(L);
                    double c = L.Dot(L) - radius2;
                    double t0, t1;
                    if (!SolveQuadratic(a, b, c, t0, t1)) continue;

                    if (t0 > t1) Swap(t0, t1);

                    if (t0 < 0) {
                        t0 = t1; // if t0 is negative, let's use t1 instead
                        if (t0 < 0) continue; // both t0 and t1 are negative
                    }

                    double t = t0;
#endif
                    // We have a valid hit!
                    hit_ranges.push_back(t);
                }
            }
        }
    }
}

// helper functions
void CumulusCloud::InitFromVRMLFile(SL_VECTOR(Puff)* puffs, std::istream *stream, const Atmosphere* atmosphere, double scale)
{
    char buf[1024];
    SL_VECTOR(Vector3 *) translationStack;
    Vector3 currentTrans(0, 0, 0);
    Vector3 *trans = 0;

    while (stream->good()) {
        stream->getline(buf, 1024);
        // Look for a sphere definition
        SL_STRING s(buf);
        SL_STRING::size_type pos;
        pos = s.find("geometry Sphere");
        if (pos != SL_STRING::npos) {
            SL_STRING s2 = s.substr(pos);
            float radius;
            std::stringstream converter(s2.c_str());
            converter.imbue(std::locale("C"));
            SL_STRING geometry, sphere, bracket, sradius;
            converter >> geometry >> sphere >> bracket >> sradius >> radius;

            //sscanf(s2.c_str(), "geometry Sphere { radius %f", &radius);

            radius *= (float)scale;
            Puff p;
            p.radius = radius;
            voxelSize = radius;
            p.pos = currentTrans;
            puffs->push_back(p);
        } else {
            // Entering a child, push on a new translation vector
            pos = s.find("{");
            if (pos == SL_STRING::npos) pos = s.find("[");
            if (pos != SL_STRING::npos) {
                trans = SL_NEW Vector3(0, 0, 0);
                translationStack.push_back(trans);
            } else {
                // Exiting a child, pop off a translation vector.
                pos = s.find("}");
                if (pos == SL_STRING::npos) pos = s.find("]");
                if (pos != SL_STRING::npos) {
                    trans = translationStack.back();
                    currentTrans = currentTrans - *trans;
                    translationStack.pop_back();
                    SL_DELETE trans;
                } else {
                    // Look for a translation, update the translation vector
                    pos = s.find("translation");
                    if (pos != SL_STRING::npos) {
                        SL_STRING s2 = s.substr(pos);
                        float x, y, z;

                        std::stringstream converter(s2.c_str());
                        converter.imbue(std::locale("C"));
                        SL_STRING translation;
                        converter >> translation >> x >> y >> z;
                        //sscanf(s2.c_str(), "translation %f %f %f", &x, &y, &z);
                        if (trans) {
                            *trans = Vector3(x * scale, -z * scale, y * scale);
                            currentTrans = currentTrans + *trans;
                        }
                    }
                }
            }
        }
    }
}


void CumulusCloud::InitFromCloudEditorFile(SL_VECTOR(Puff)* puffs, std::istream *stream, const Atmosphere* atmosphere, double scale)
{
    bool notFoundStartingHeaders = true;
    bool notFoundPuffSize = true;
    bool notFoundVoxelHeader = true;
    bool notFoundVoxelInfo = true;

#if defined(_MSC_VER)
    __int64 globalPuffSize = 0;
    __int64 numVoxels = 0;
#else
    long long globalPuffSize = 0;
    long long numVoxels = 0;
#endif

    double radius = 0;

    int realTextureIds[] = { 0, 13, 14, 15, 16, 9, 10, 11, 12, 5, 6, 7, 8, 1, 2, 3, 4 };

    RandomNumberGenerator *rng = atmosphere->GetRandomNumberGenerator();

    while (stream->good()) {
        if (notFoundStartingHeaders) {
            unsigned char buf[8];
            stream->read(reinterpret_cast<char*>(buf), sizeof(buf));

            SL_STRING s(reinterpret_cast<char*>(buf));
            SL_STRING::size_type pos;
            pos = s.find("SLCloud");

            if (pos != SL_STRING::npos) {
                notFoundStartingHeaders = false;
            } else {
                break;
            }
        } else if (notFoundPuffSize) {
            unsigned char buf[9];
            stream->read(reinterpret_cast<char*>(buf), sizeof(buf));

            SL_STRING s(reinterpret_cast<char*>(buf));
            SL_STRING::size_type pos;
            pos = s.find("PuffSize");

            if (pos != SL_STRING::npos) {
                unsigned char buf2[sizeof(globalPuffSize)];
                stream->read(reinterpret_cast<char*>(buf2), sizeof(globalPuffSize));

                for (int i = 0; i < sizeof(globalPuffSize); i++) {
                    globalPuffSize |= buf2[i] << (i * 8);
                }

                radius = (double)globalPuffSize;
                radius *= (float)scale;
                voxelSize = radius;
                notFoundPuffSize = false;
            } else {
                break;
            }
        } else if (notFoundVoxelHeader) {
            unsigned char buf[7];
            stream->read(reinterpret_cast<char*>(buf), sizeof(buf));

            SL_STRING s(reinterpret_cast<char*>(buf));
            SL_STRING::size_type pos;
            pos = s.find("Voxels");

            if (pos != SL_STRING::npos) {
                unsigned char buf2[sizeof(numVoxels)];
                stream->read(reinterpret_cast<char*>(buf2), sizeof(numVoxels));

                for (int i = 0; i < sizeof(numVoxels); i++) {
                    numVoxels |= buf2[i] << (i * 8);
                }
                notFoundVoxelHeader = false;
            } else {
                break;
            }
        } else if (notFoundVoxelInfo) {
            for (long i = 0; i < numVoxels; i++) {
                unsigned char buf[28];
                stream->read(reinterpret_cast<char*>(buf), sizeof(buf));
                SL_STRING s(reinterpret_cast<char*>(buf));

                int posX = 0;
                int posY = 0;
                int posZ = 0;
                int scaleX = 0;
                int scaleY = 0;
                int scaleZ = 0;
                int textureId = 0;

                for (int i = 0; i < sizeof(buf); i++) {
                    if (i < 4) {
                        posX |= buf[i] << (i * 8);
                    } else if (i < 8) {
                        posY |= buf[i] << ((i - 4) * 8);
                    } else if (i < 12) {
                        posZ |= buf[i] << ((i - 8) * 8);
                    } else if (i < 16) {
                        scaleX |= buf[i] << ((i - 12) * 8);
                    } else if (i < 20) {
                        scaleY |= buf[i] << ((i - 16) * 8);
                    } else if (i < 24) {
                        scaleZ |= buf[i] << ((i - 20) * 8);
                    } else {
                        textureId |= buf[i] << ((i - 24) * 8);
                    }
                }

                if (textureId == 0) {
                    textureId = 1;
                }

                Puff p;
                p.pos = Vector3(posX * scale, posY * scale, posZ * scale);

                p.pos.x += rng->UniformRandomDouble() * voxelSize - (voxelSize * 0.5);
                p.pos.y += rng->UniformRandomDouble() * voxelSize - (voxelSize * 0.5);
                p.pos.z += rng->UniformRandomDouble() * voxelSize - (voxelSize * 0.5);

                p.textureId = realTextureIds[textureId];
                p.radius = (float)radius;
                puffs->push_back(p);
            }
            notFoundVoxelInfo = false;
        } else {
            break;
        }
    }
}
}
