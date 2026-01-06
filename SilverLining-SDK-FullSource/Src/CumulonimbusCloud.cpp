// Copyright (c) 2006-2017 Sundog Software, LLC. All rights reserved worldwide.

#include "CumulonimbusCloud.h"
#include "CloudLayer.h"
#include "Voxel.h"
#include "BillboardBuffer.h"
#include "Configuration.h"
#include "Virga.h"
#include "Lightning.h"
#include "Utils.h"
#include "EnvironmentMap.h"
#include "Camera.h"
#include "BillboardRenderingParameters.h"
#include "CumulusCloudRenderingParameters.h"
#include "MathUtils.h"
#include "SLAssert.h"

using namespace SilverLining;
using namespace std;

#define ELLIPSOID_BOUNDS 0
#define CULL_FADED_CLOUDS

CumulonimbusCloud::CumulonimbusCloud(CloudLayer *layer, double coverageThreshold, bool voxelsUseShaderInstancing, bool _drawIndirect, ThreadCameraStreamData* _tcsData) : CumulusCloud(layer, coverageThreshold, voxelsUseShaderInstancing, _drawIndirect, _tcsData)
{
    atmosphere = 0;
    boundScale = 0.5;
    flattening = 1.0f;
    Configuration::GetFloatValue("cumulonimbus-voxel-flattening", flattening);
}

CumulonimbusCloud::~CumulonimbusCloud()
{
    CleanResources();
}

void CumulonimbusCloud::CleanResources()
{
    SL_VECTOR(Virga*) ::iterator vit;
    for (vit = virgaList.begin(); vit != virgaList.end(); vit++) {
        SL_DELETE *vit;
    }

    virgaList.clear();

    SL_VECTOR(Lightning*) ::iterator lit;
    for (lit = lightningList.begin(); lit != lightningList.end(); lit++) {
        SL_DELETE *lit;
    }

    lightningList.clear();

    litVoxels.clear();
}

void CumulonimbusCloud::ComputeLightningLighting(Lightning * l)
{
    double lightningLight = 1E10;
    Configuration::GetDoubleValue("lightning-light", lightningLight);
    bool illuminateCloud = true;
    Configuration::GetBoolValue("cumulonimbus-illuminate-from-lightning", illuminateCloud);
    if (!illuminateCloud) return;

    int i, j, k;
    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                if (voxels[i][j][k]->GetHasCloud()) {
                    const Vector3f& fVoxelPos = voxels[i][j][k]->GetWorldPosition();
                    double dist = l->GetDistance(Vector3(fVoxelPos.x, fVoxelPos.y, fVoxelPos.z));

                    double I = 0;

                    if (dist > 0) {
                        I = lightningLight / (dist * dist);
                    }

                    if (I > 0.1) {
                        if (I > 1.0) I = 1.0;
                        Color lightningColor = l->GetGlowColor();
                        lightningColor = lightningColor * (float)I;
                        LitVoxel lv;
                        lv.lightningColor = lightningColor;
                        lv.origColor = voxels[i][j][k]->GetMetaballColor();
                        lv.voxel = voxels[i][j][k];
                        lv.hasOrigColor = false;
                        litVoxels[l].push_back(lv);
                    }
                }
            }
        }
    }
}

void CumulonimbusCloud::ApplyLightningLighting(Lightning * l)
{
    SL_VECTOR(LitVoxel) ::iterator it;
    for (it = litVoxels[l].begin(); it != litVoxels[l].end(); it++) {
        LitVoxel& v = *it;

        if (!v.voxel->GetHasLightning()) {
            v.voxel->SetHasLightning(true);
            if (!v.hasOrigColor) {
                v.hasOrigColor = true;
                v.origColor = v.voxel->GetMetaballColor();;
                Color litColor = v.origColor + v.lightningColor;
                litColor.ClampToUnitOrLess(tcsData->GetHDREnabled());
                v.voxel->SetMetaballColor(litColor, tcsData->GetHDREnabled());
            }
        }
    }

    l->SetLightingApplied(true);
}

void CumulonimbusCloud::RestoreLightningLighting(Lightning * l)
{
    SL_VECTOR(LitVoxel) ::iterator it;
    for (it = litVoxels[l].begin(); it != litVoxels[l].end(); it++) {
        LitVoxel& v = *it;
        if (v.hasOrigColor) {
            v.hasOrigColor = false;
            v.voxel->SetMetaballColor(v.origColor, tcsData->GetHDREnabled());
            v.voxel->SetHasLightning(false);
        }
    }

    l->SetLightingApplied(false);
}

bool CumulonimbusCloud::ForceLightning(bool value)
{
    int nl = (int)lightningList.size();
    if (nl > 0) {
        int n = atmosphere->GetRandomNumberGenerator()->UniformRandomIntRange(0, nl-1);
        lightningList[n]->ForceStrike(*atmosphere, value);
        return true;
    }

    return false;
}

bool CumulonimbusCloud::IsCulled(const Camera* cullCamera, ThreadCameraStreamData* tcsData) const
{
    Vector3 center = GetWorldPosition(tcsData);

    double w, h, d;
    GetSize(w, d, h);
    w *= 0.5;
    d *= 0.5;
    h += GetParentCloudLayer()->GetBaseAltitude(tcsData);

    Frustum f = cullCamera->GetFrustumWorldSpace();
    if (atmosphere->GetFarCullingDisabled()) {
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
        unsigned int now = atmosphere->GetConditions().GetMillisecondTimer()->GetMilliseconds();
        if (now - fadeStartTime > timeStepInterval) {
            return true;
        }
    }

    const double fade = tcsData->GetBillboardRenderingParams()->CalculateFogFade(center, cullCamera);// *GetAlpha();
    if (fade <= tcsData->GetCumulusCloudRenderingParams()->GetAlphaCullThreshold()) {
        return true;
    }
#endif

    return false;
}

bool CumulonimbusCloud::Draw(int pass, const Vector3& lightPos, const Vector3& lightDir,
                             const Color& lightColor, bool invalid, const Sky *sky, bool drawLightning, const Camera* camera
                             , ThreadCameraStreamData* tcsData)
{
    double maxDischargePeriod = 0.;

    for (SL_VECTOR(Virga*)::iterator vit = virgaList.begin(); vit != virgaList.end(); vit++) {
        (*vit)->SetColor(lightColor, camera->GetPosition(), tcsData);
        (*vit)->Draw(pass, tcsData);
    }

    bool doLightning = true;
    if (tcsData->GetIsDrawingEnvMap()) {
        bool envMapLightning = false;
        Configuration::GetBoolValue("environment-maps-have-lightning", envMapLightning);
        if (!envMapLightning) {
            doLightning = false;
        }
    }

    Configuration::GetDoubleValue("lightning-max-discharge-period", maxDischargePeriod);
    if (maxDischargePeriod == 0.)
        doLightning = false;

    bool illuminateCloud = true;
    Configuration::GetBoolValue("cumulonimbus-illuminate-from-lightning", illuminateCloud);

    if (doLightning && pass == 0) {
        SL_VECTOR(Lightning*) ::iterator lit;
        for (lit = lightningList.begin(); lit != lightningList.end(); lit++) {
            RestoreLightningLighting(*lit);
        }
    }

    if (doLightning && drawLightning && pass == 1) {
        // Apply lighting from lightning.
        SL_VECTOR(Lightning*) ::iterator lit;
        for (lit = lightningList.begin(); lit != lightningList.end(); lit++) {
            (*lit)->Draw(pass, *atmosphere, tcsData);

            if (illuminateCloud) {
                if ((*lit)->GetIsDischarging() && !(*lit)->GetLightingApplied()) {
                    ApplyLightningLighting(*lit);
                    Invalidate(tcsData);
                }

                if (!(*lit)->GetIsDischarging() && (*lit)->GetLightingApplied()) {
                    RestoreLightningLighting(*lit);
                    Invalidate(tcsData);
                }
            }
        }
    }

    return CumulusCloud::Draw(pass, lightPos, lightDir, lightColor, invalid, sky, drawLightning, camera, tcsData);
}

void CumulonimbusCloud::Visit(int pass, ThreadCameraStreamData* tcsData)
{
    if (!tcsData->GetIsDrawingEnvMap()) {
        if (pass == 1) {
            // Update lightning.
            SL_VECTOR(Lightning*) ::iterator lit;
            for (lit = lightningList.begin(); lit != lightningList.end(); lit++) {
                (*lit)->Visit(pass, *atmosphere, tcsData);
            }
        }
    }
}


void CumulonimbusCloud::ResetLightningTimer()
{
    SL_VECTOR(Lightning*) ::iterator lit;
    for (lit = lightningList.begin(); lit != lightningList.end(); lit++) {
        (*lit)->ResetDischargeTimer();
    }

}

void CumulonimbusCloud::ApplyWindShear(const Vector3& wind, ThreadCameraStreamData* tcsData)
{
    double updraft = 10.0; // m/s
    Configuration::GetDoubleValue("cumulonimbus-updraft-speed", updraft);
    updraft *= Atmosphere::GetUnitScale();
    CumulusCloud::Shear(updraft, wind, tcsData);
}

double CumulonimbusCloud::GetVaporProbability(int i, int j, int k, const RandomNumberGenerator* rng)
{
    double anvilHeight = 0.85;
    Configuration::GetDoubleValue("cumulonimbus-anvil-height", anvilHeight);
    double shaftConstriction = 0.4;
    Configuration::GetDoubleValue("cumulonimbus-shaft-constriction", shaftConstriction);

    double midX = (double)width * towerCenterX;
    double midY = (double)height * anvilHeight;
    double midZ = (double)depth * towerCenterY;

    double halfWidth = (double)width * 0.5;
    double halfDepth = (double)depth * 0.5;
    double a2 = halfWidth * halfWidth;
    double b2 = midY * midY;
    double c2 = halfDepth * halfDepth;

    double x = (double)i - midX;
    double y = (double)k;
    double z = (double)j - midZ;

    double dist;
    if ( (double)k > (double)height * anvilHeight) {
        dist = ((x*x) + (z*z)) / (a2 + c2);
        double distFromAnvilBottom = (k - (height * anvilHeight));
        distFromAnvilBottom /= height * (1.0 - anvilHeight);
        dist *= (1.0 - distFromAnvilBottom);
    } else {
        x *= shaftConstriction;
        z *= shaftConstriction;
        dist = ((x * x) + (y * y) + (z * z)) / (a2 + b2 + c2);
    }

    double scale = (1.0 - dist);
    if (scale < 0) scale = 0;

    double rnd = rng->UniformRandomDouble();
    rnd *= scale;

    if (rnd > 1.0) rnd = 1.0;

    return rnd;
}

void CumulonimbusCloud::InitializeVoxelState(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    // Note - cloud world position not yet set at this point, so don't try and use it.
    voxelsDirty = true;

    int i, j, k;
    Vector3 origin(0, 0, 0);
    origin.x -= width * voxelSize * 0.5;
    origin.z -= depth * voxelSize * 0.5;

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                Vector3 delta(i * voxelSize, k * voxelSize, j * voxelSize);

                Vector3 pos = origin + delta;

                if (voxelSize > 2.0) {
                    pos.x += rng->UniformRandomIntRange(0, (int)(voxelSize * 0.5) - 1);
                    pos.z += rng->UniformRandomIntRange(0, (int)(voxelSize * 0.5) - 1);
                }

                voxels[i][j][k]->SetWorldPosition(pos);

                voxels[i][j][k]->SetHasCloud(false);

                double rnd = GetVaporProbability(i, j, k, rng);
                voxels[i][j][k]->SetVapor(rnd < initialVaporProbability);

                voxels[i][j][k]->SetPhaseTransition(false);
            }
        }
    }

    // seed the cloud
    double rndX = rng->UniformRandomDouble();
    double rndY = rng->UniformRandomDouble();

    towerCenterX = rndX;
    towerCenterY = rndY;

    int X = (int)(rndX * width);
    int Y = (int)(rndY * depth);

    for (i = 0; i < height; i++) {
        voxels[X][Y][i]->SetPhaseTransition(true);
    }

    lastTimeStep = (unsigned long)time(NULL);
}

void CumulonimbusCloud::CloudPlaced(const Atmosphere& atm, ThreadCameraStreamData* tcsData)
{
    CleanResources();

    atmosphere = &atm;

    double lightningDensity = 0.1;
    Configuration::GetDoubleValue("cumulonimbus-lightning-density", lightningDensity);
    double virgaHeight = 1.3;
    Configuration::GetDoubleValue("cumulonimbus-virga-height", virgaHeight);
    float earthRadius = 6356752.3f;
    Configuration::GetFloatValue("earth-radius-meters-polar", earthRadius);
    earthRadius *= (float)Atmosphere::GetUnitScale();


    // Add virga & lightning
    const char *rainTexStr;
    Configuration::GetStringValue("cumulonimbus-rain-texture", rainTexStr);
    bool hasVirga = (strcmp(rainTexStr, "none") != 0);

    float heightOffset = 0.3f;
    Configuration::GetFloatValue("virga-height-offset", heightOffset);

    double lightningLengthExtension = 0;
    Configuration::GetDoubleValue("lightning-length-extension", lightningLengthExtension);

    const Camera* camera = tcsData->GetCamera();

    Vector3 xformPos = GetWorldPosition(tcsData) * camera->GetInverseBasis3x3();

    for (int i = 0; i < width; i++) {
        for (int j = 0; j < depth; j++) {
            if (hasVirga) {
                double maxY = voxelSize;
                for (int k = 0; k < height; k++) {
                    if (voxels[i][j][k]->GetHasCloud()) {
                        const Vector3f& fVirgaPos = voxels[i][j][k]->GetWorldPosition();
                        Vector3 virgaPos(fVirgaPos.x, fVirgaPos.y, fVirgaPos.z);
                        Vector3 xformVoxelPos = virgaPos;// * camera->GetInverseBasis3x3();
                        if (xformVoxelPos.y <= maxY) {
                            double adjustedHeight = (xformPos.y + xformVoxelPos.y) * virgaHeight;
                            xformVoxelPos.y = -xformPos.y * 0.5 - heightOffset * adjustedHeight;
                            Virga *virga = SL_NEW Virga(this, voxelSize,
                                                        adjustedHeight,
                                                        xformVoxelPos,
                                                        atm, tcsData);
                            virgaList.push_back(virga);
                        }
                    }
                }
            }

            const RandomNumberGenerator* rng = atm.GetRandomNumberGenerator();
            // Add lightning
            if (rng->UniformRandomDouble() < lightningDensity && height > 0) {
                if (voxels[i][j][height-1]->GetHasCloud()) {
                    const Vector3f fVoxelPos = voxels[i][j][height - 1]->GetWorldPosition();
                    Vector3 xformVoxelPos = Vector3(fVoxelPos.x, fVoxelPos.y, fVoxelPos.z);

                    double lightningHeight = xformPos.y + xformVoxelPos.y;
                    if (lightningHeight > earthRadius) { // assume we are in an ecef system
                        lightningHeight -= earthRadius;
                    }
                    Lightning *lightning = SL_NEW Lightning(this, lightningHeight + lightningLengthExtension,
                                                            Vector3(fVoxelPos.x, fVoxelPos.y, fVoxelPos.z) * camera->GetBasis3x3(), atm.GetRandomNumberGenerator(), tcsData);
                    lightningList.push_back(lightning);
                    ComputeLightningLighting(lightning);
                }
            }
        }
    }
}

void CumulonimbusCloud::IncrementTimeStep(unsigned long now, const RandomNumberGenerator* rng, const BillboardBufferProvider* provider, ThreadCameraStreamData* tcsData)
{
    voxelsDirty = true;

    int i, j, k;

    for (i = 0; i < width; i++) {
        for (j = 0; j < depth; j++) {
            for (k = 0; k < height; k++) {
                bool fAct = (
                                (i+1 < width  ? voxels[i+1][j][k]->GetPhaseTransition() : false) ||
                                (j+1 < depth  ? voxels[i][j+1][k]->GetPhaseTransition() : false) ||
                                (i-1 >= 0     ? voxels[i-1][j][k]->GetPhaseTransition() : false) ||
                                (j-1 >= 0     ? voxels[i][j-1][k]->GetPhaseTransition() : false) ||
                                (k-1 >= 0     ? voxels[i][j][k-1]->GetPhaseTransition() : false) ||
                                (i-2 >= 0     ? voxels[i-2][j][k]->GetPhaseTransition() : false) ||
                                (i+2 < width  ? voxels[i+2][j][k]->GetPhaseTransition() : false) ||
                                (j-2 >= 0     ? voxels[i][j-2][k]->GetPhaseTransition() : false) ||
                                (j+2 < depth  ? voxels[i][j+2][k]->GetPhaseTransition() : false)
                            );

                if (k+2 > height) fAct = true; // anvil growth at top

                bool thisAct = voxels[i][j][k]->GetPhaseTransition();

                double rnd = GetVaporProbability(i, j, k, rng);

                voxels[i][j][k]->SetPhaseTransition((
                                                        (!thisAct) &&
                                                        voxels[i][j][k]->GetVapor() &&
                                                        fAct) || (rnd < transitionProbability));

                rnd = GetVaporProbability(i, j, k, rng);
                voxels[i][j][k]->SetVapor(
                    (voxels[i][j][k]->GetVapor() && !thisAct) || (rnd < vaporProbability));

                rnd = rng->UniformRandomDouble();
                bool oldHasCloud = voxels[i][j][k]->GetHasCloud();
                bool newHasCloud = ((oldHasCloud || thisAct) && (rnd > extinctionProbability * (1.0 - rnd)));

                float timeStepSeconds = (float)timeStepInterval * 0.001f;
                if (newHasCloud && !oldHasCloud) {
                    voxels[i][j][k]->SetFadeRate(1.0f / timeStepSeconds);
                    voxels[i][j][k]->SetCreated(true);
                    voxels[i][j][k]->SetDestroyed(false);
                } else if (!newHasCloud && oldHasCloud) {
                    voxels[i][j][k]->SetFadeRate(-1.0f / timeStepSeconds);
                    voxels[i][j][k]->SetCreated(false);
                    voxels[i][j][k]->SetDestroyed(true);
                } else {
                    voxels[i][j][k]->SetFadeRate(0.0);
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
