// Copyright (c) 2006-2023 Sundog Software. All rights reserved worldwide.

#include "Lightning.h"
#include "Renderer.h"
#include "Cloud.h"
#include "CloudLayer.h"
#include "MathUtils.h"
#include "Configuration.h"
#include "ShadowMap.h"
#include "Utils.h"
#include "Camera.h"
#include "BillboardRenderingParameters.h"
#include "BillboardRenderer.h"
#include "ThreadCameraStreamData.h"
#include "Mutex.h"
#include "SLAssert.h"

using namespace std;

// Uncomment to force lightning to be on at all times
//#define TEST_LIGHTNING

namespace SilverLining
{
class LightningParameters : public MemObject
{
public:
    LightningParameters(int renderer);
    virtual ~LightningParameters();
public:
    /// The width of the glow effect around each lightning bolt, in pixels.
    double glowWidth;

    /// The color of the main lightning bolt.
    Color lightningColor;

    /// The color of the glow surrounding the lightning bolt.
    Color glowColor;

    /// Multiplier for HDR mode
    float hdrBoost;

    // Whether to use tcs data rng
    bool tcsDataRng;

    // Vulkan only
    float minLineWidth;
    float maxLineWidth;
};

LightningParameters::LightningParameters(int renderer)
{
    glowWidth = 1.5;
    lightningColor = Color(0.8, 0.8, 1.0, 1.0);
    glowColor = Color(0.8, 0.8, 1.0, 0.3);
    float hdrBoost = 100.0;

    Configuration::GetDoubleValue("lightning-glow-width", glowWidth);

    double r, g, b, a;
    Configuration::GetDoubleValue("lightning-color-r", r);
    Configuration::GetDoubleValue("lightning-color-g", g);
    Configuration::GetDoubleValue("lightning-color-b", b);
    Configuration::GetDoubleValue("lightning-color-a", a);

    lightningColor = Color(r, g, b, a);

    Configuration::GetDoubleValue("lightning-glow-color-r", r);
    Configuration::GetDoubleValue("lightning-glow-color-g", g);
    Configuration::GetDoubleValue("lightning-glow-color-b", b);
    Configuration::GetDoubleValue("lightning-glow-color-a", a);

    glowColor = Color(r, g, b, a);

    Configuration::GetFloatValue("lightning-hdr-boost", hdrBoost);

    tcsDataRng = false;
    Configuration::GetBoolValue("tcs-data-effects-rng-unique", tcsDataRng);

    if (renderer == Atmosphere::VULKAN) {
        Configuration::GetFloatValue("vulkan-line-width-min", minLineWidth);
        Configuration::GetFloatValue("vulkan-line-width-max", maxLineWidth);
    } else {
        minLineWidth = -1;
        maxLineWidth = -1;
    }
}

LightningParameters::~LightningParameters()
{

}

const Color& Lightning::GetGlowColor() const
{
    return lightningParameters->glowColor;
}

LightningBranch::LightningBranch(ThreadCameraStreamData* _tcsData, const LightningParameters* _lightningParameters)
    : isMainBranch(false)
    , vertexBuffer(0)
    , vertexBufferDirty(true)
    , tcsData(_tcsData)
    , lightningParameters(_lightningParameters)
{
}

LightningBranch::~LightningBranch()
{
    if (vertexBuffer) {
        tcsData->GetBillboardRenderer()->ReturnAVertexBuffer(vertexBuffer);
        vertexBuffer = 0;
    }
}

void LightningBranch::SyncVertexBuffer()
{
    if (vertexBufferDirty) {
        if (vertexBuffer) {
            tcsData->GetBillboardRenderer()->ReturnAVertexBuffer(vertexBuffer);
            vertexBuffer = 0;
        }

        const int numOfPoints = (int)(points.size());
        Vertex *verts = SL_NEW Vertex[numOfPoints];
        for (int i = 0; i < numOfPoints; i++) {
            verts[i].x = (float)(points[i].x);
            verts[i].y = (float)(points[i].y);
            verts[i].z = (float)(points[i].z);
            verts[i].w = 1.0;
        }

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "LightningBranch::VertexBuffer" : (const char *)0;

        // These are mappable (last parameter) for vulkan.
        // This is so that the buffer pointer can be directly gotten and vertices changed
        vertexBuffer = tcsData->GetBillboardRenderer()->GetAVertexBuffer(numOfPoints, name);
        SL_ASSERT(vertexBuffer);
        if (vertexBuffer) {
            // Not updating just the colors. Hence pass in false, since we are updating everything
            vertexBuffer->Update(0, verts, numOfPoints, false);
        } else {
            std::cout << "vertexBuffer==0" << std::endl;
        }
        SL_DELETE[] verts;

        vertexBufferDirty = false;
    }
}

bool LightningBranch::Draw(bool hdr, double distanceFactor, const Lightning* lightning, ThreadCameraStreamData* tcsData) const
{
    double glowLineWidth = lineWidth * lightningParameters->glowWidth;

    Color glow = lightningParameters->glowColor;
    Color main = lightningParameters->lightningColor;

    if (hdr) {
        glow = glow * lightningParameters->hdrBoost;
        main = main * lightningParameters->hdrBoost;
    }

    if (isMainBranch) {
        glow.a *= (float)lightning->GetMainBranchCoeff();
        main.a *= (float)lightning->GetMainBranchCoeff();
    } else {
        glow.a *= (float)lightning->GetBranchCoeff();
        main.a *= (float)lightning->GetBranchCoeff();
    }

    glow.a *= lightning->GetFogAlpha();
    main.a *= lightning->GetFogAlpha();

    LightningBranch* nonConstThis = const_cast<LightningBranch*>(this);
    nonConstThis->SyncVertexBuffer();

    Renderer* renderer = tcsData->GetRenderer();

    bool fogEnabled = renderer->GetFogEnabled();
    renderer->EnableFog(false);

    renderer->EnableBackfaceCulling(false);

    if (glowLineWidth > 1.0) {
        float floatLineWidth = (float)(glowLineWidth * distanceFactor);
        if (renderer->GetType() == Atmosphere::VULKAN) {
            floatLineWidth = MathUtilsFloat::clamp(floatLineWidth, lightningParameters->minLineWidth, lightningParameters->maxLineWidth);
        }
        if (!renderer->DrawLineStrip(vertexBuffer->GetHandle(), glow, vertexBuffer->GetNumVertices(), floatLineWidth)) {
            renderer->DrawAALines(glow, glowLineWidth * distanceFactor, points);
        }
    }

    float floatLineWidth = (float)(lineWidth * distanceFactor);
    if (renderer->GetType() == Atmosphere::VULKAN) {
        floatLineWidth = MathUtilsFloat::clamp(floatLineWidth, lightningParameters->minLineWidth, lightningParameters->maxLineWidth);
    }
    if (!renderer->DrawLineStrip(vertexBuffer->GetHandle(), main, vertexBuffer->GetNumVertices(), floatLineWidth)) {
        renderer->DrawAALines(main, lineWidth * distanceFactor, points);
    }

    renderer->EnableFog(fogEnabled);

    return true;
}

MutexContainer Lightning::internalMutexContainer;
int Lightning::refCount = 0;
LightningParameters* Lightning::theLightningParameters = 0;

Lightning::Lightning(Cloud *parentCloud, double pheight, const Vector3& offset, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData) : discharging(false), lightingApplied(false),
    offsetPos(offset), cloud(parentCloud)
{
    {
        ScopedMutex scopedMutex(internalMutexContainer.mutex);
        ++refCount;
        if (theLightningParameters == 0) {
            theLightningParameters = SL_NEW LightningParameters(tcsData->GetRenderer()->GetType());
        }
    }
    lightningParameters = theLightningParameters;

    height = pheight;

    initialThickness = 3.0;
    maxSegmentsPerBranch = 30;
    maxSegmentLength = 50.0;
    segmentAngleMean = 50.0;
    maxBranchProbability = 0.15;
    minBranchThickness = 0.3;
    maxBranchThickness = 0.5;
    thicknessReduction = 0.8;
    fireDuration = 500;
    maxDischargePeriod = 20000;
    perspectiveDistance = 50000.0;

    mainBranchCoeff = 1.0;
    branchCoeff = 1.0;
    fogAlpha = 1.0f;

    Configuration::GetDoubleValue("lightning-initial-thickness", initialThickness);
    Configuration::GetIntValue("lightning-max-segments-per-branch", maxSegmentsPerBranch);
    Configuration::GetDoubleValue("lightning-max-segment-length", maxSegmentLength);
    maxSegmentLength *= Atmosphere::GetUnitScale();
    Configuration::GetDoubleValue("lightning-max-segment-angle", segmentAngleMean);
    Configuration::GetDoubleValue("lightning-max-branch-probability", maxBranchProbability);
    Configuration::GetDoubleValue("lightning-min-branch-thickness", minBranchThickness);
    Configuration::GetDoubleValue("lightning-max-branch-thickness", maxBranchThickness);
    Configuration::GetDoubleValue("lightning-thickness-reduction", thicknessReduction);
    Configuration::GetDoubleValue("lightning-max-discharge-period", maxDischargePeriod);

    Configuration::GetDoubleValue("lightning-perspective-distance", perspectiveDistance);
    perspectiveDistance *= Atmosphere::GetUnitScale();


    //Configuration::GetIntValue("lightning-auto-discharge", autoDischarge);

    int fireDurationInt;
    Configuration::GetIntValue("lightning-discharge-ms", fireDurationInt);
    fireDuration = fireDurationInt;
    forceDischarge = false;

    lastFireTime = nextFireTime = 0;

    double cwidth, cdepth, cheight;
    parentCloud->GetSize(cwidth, cdepth, cheight);
    double cloudDiameter = sqrt(cwidth * cwidth + cdepth * cdepth + cheight * cheight);
    SetDepthOffset(cloudDiameter);

    Generate(rng, tcsData);
}

void Lightning::DeleteBranches(void)
{
    for (SL_VECTOR(LightningBranch*)::const_iterator it = branches.begin(); it != branches.end(); it++) {
        LightningBranch* branch = *it;
        SL_DELETE branch;
    }
    branches.clear();
}

Lightning::~Lightning()
{
    DeleteBranches();
    {
        ScopedMutex scopedMutex(internalMutexContainer.mutex);
        --refCount;
        if (refCount == 0 && theLightningParameters) {
            SL_DELETE theLightningParameters;
            theLightningParameters = 0;
        }
    }
}

double Lightning::GetDistance(const Vector3& pt)
{
    Vector3 realEnd = endPos + offsetPos;
    Vector3 realStart = startPos + offsetPos;

    Vector3 v = (realEnd - realStart);
    Vector3 w = pt - realStart;

    double c1 = w.Dot(v);
    if (c1 <= 0)
        return (pt - realStart).Length();

    double c2 = v.Dot(v);
    if (c2 <= c1)
        return (pt - realEnd).Length();

    double b = c1 / c2;
    Vector3 Pb = realStart + (v * b);

    return (pt - Pb).Length();
}

void Lightning::ForceStrike(const Atmosphere& atm, bool value)
{
    nextFireTime = atm.GetConditions().GetMillisecondTimer()->GetMilliseconds();
    if (cloud->GetParentCloudLayer()->GetLightningDischargeMode() == FORCE_ON_OFF) {
        forceDischarge = value;
    }
}

bool Lightning::Draw(int pass, const Atmosphere& atmosphere, ThreadCameraStreamData* tcsData)
{
    // Parameters URS wants adjustible at runtime
    int fireDurationInt;
    Configuration::GetIntValue("lightning-discharge-ms", fireDurationInt);
    fireDuration = fireDurationInt;
    Configuration::GetDoubleValue("lightning-max-discharge-period", maxDischargePeriod);

    hdr = atmosphere.GetHDREnabled();

    if (pass == 1) {
#ifdef TEST_LIGHTNING
        discharging = true;

        LightningBranch::branchCoeff = 1;
        LightningBranch::mainBranchCoeff = 1;

        renderer->SubmitBlendedObject(this);
#else
        if (discharging) {
            unsigned long now = atmosphere.GetConditions().GetMillisecondTimer()->GetMilliseconds();
            double t = (double)(now - lastFireTime);
            Lightning::branchCoeff = 1.0 - (t / (double)fireDuration);

            Lightning::mainBranchCoeff = (cos(t * 0.1) / exp(t * 0.005)) + 0.5;

            if (Lightning::mainBranchCoeff < 0) Lightning::mainBranchCoeff = 0;
            if (Lightning::mainBranchCoeff > 1) Lightning::mainBranchCoeff = 1;
            if (Lightning::branchCoeff < 0) Lightning::branchCoeff = 0;
            if (Lightning::branchCoeff > 1) Lightning::branchCoeff = 1;

            Renderer* renderer = tcsData->GetRenderer();
            renderer->SubmitBlendedObject(this);
        }
#endif
    }

    return true;
}

void Lightning::ResetDischargeTimer()
{
    nextFireTime = 0;
}

void Lightning::Visit(int pass, const Atmosphere& atm, ThreadCameraStreamData* tcsData)
{
    if (pass == 1) {

        unsigned long now = atm.GetConditions().GetMillisecondTimer()->GetMilliseconds();
        bool firstFire = (nextFireTime == 0) || (now - nextFireTime) > maxDischargePeriod;

        if (now < lastFireTime) { // someone moved time backward
            lastFireTime = nextFireTime = now;
        }

        const RandomNumberGenerator* rng = (theLightningParameters->tcsDataRng) ? tcsData->GetRandomNumberGenerator() : atm.GetRandomNumberGenerator();

        if (now >= nextFireTime) {
            if (cloud->GetParentCloudLayer()->GetLightningDischargeMode() == AUTO_DISCHARGE) {
                unsigned long urandperiod = (unsigned long)(rng->UniformRandomDouble() * maxDischargePeriod);
                nextFireTime = now + urandperiod;
            } else {
                nextFireTime = 4294967295UL; // max 32 bit unsigned
            }

            if (firstFire) {
                return;
            }

            if (cloud->GetParentCloudLayer()->GetLightningDischargeMode() != FORCE_ON_OFF) {
                lastFireTime = now;
            }

            cloud->GetParentCloudLayer()->LightningNotify(GetWorldPosition(tcsData));
        }

        if (cloud->GetParentCloudLayer()->GetLightningDischargeMode() != FORCE_ON_OFF) {

            if ((now >= lastFireTime) && (now < (lastFireTime + fireDuration))) {
                discharging = true;
            } else {
                discharging = false;
            }

        } else {
            discharging = forceDischarge;
        }
    }
}

Vector3 Lightning::GetWorldPosition(const ThreadCameraStreamData* tcsData) const
{
    Vector3 pos = cloud->GetWorldPosition(tcsData) + offsetPos;
    return pos;
}

Vector3 Lightning::GetSortPosition(const Camera* camera, const ThreadCameraStreamData* tcsData) const
{
    Vector3 halfHeight = Vector3(0, height * 0.5, 0);
    halfHeight = halfHeight * tcsData->GetCamera()->GetBasis3x3();
    return GetWorldPosition(tcsData) - halfHeight;
}

void Lightning::DrawBlendedObject(const Atmosphere* atmosphere, const Camera* sceneCamera, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, const OverriddenCameraPos& overriddenCameraPos, bool fadeOverride
                                  , ThreadCameraStreamData* tcsData) const
{
    double distance = 0;
    if (atmosphere) {
        const ShadowMap *sm = tcsData->GetShadowMapObject();
        if (sm && sm->IsRendering()) return;

        distance = (sceneCamera->GetPosition() - GetSortPosition(sceneCamera, tcsData)).Length();
        double fogDensity = tcsData->GetBillboardRenderingParams()->GetFogDensity();
        double fogExponent = fogDensity * distance;
        if (fogExponent < 0) fogExponent = 0;
        if (fogExponent > 1.0) fogExponent = 1.0;

        fogExponent *= fogExponent;

        fogAlpha = 1.0f - (float)fogExponent;
        if (fogAlpha < 0) fogAlpha = 0;
        if (fogAlpha > 1.0f) fogAlpha = 1.0f;
    }

    Renderer* renderer = tcsData->GetRenderer();

    renderer->UnbindShader();
    renderer->EnableLighting(false);

    for (int i = 0; i < 8; i++) {
        renderer->DisableTexture(i);
    }

    renderer->EnableBlending(SRCALPHA, INVSRCALPHA, true);

    Vector3 pos = GetWorldPosition(tcsData);

    Matrix4 scalingMatrix;
    scalingMatrix.elem[0][3] = pos.x;
    scalingMatrix.elem[1][3] = pos.y;
    scalingMatrix.elem[2][3] = pos.z;

    Camera adjustedCamera(renderCamera, true, false);
    adjustedCamera.MultiplyModelViewMatrix(scalingMatrix);

    renderer->SetModelviewMatrix(adjustedCamera.GetModelViewMatrix());

    renderer->EnableTexture2D(false);

    renderer->EnableDepthReads(true);

    double normDistance = distance / perspectiveDistance;
    if (normDistance > 1.0) normDistance = 1.0;
    double distanceFactor = 1.0 - normDistance;

    for (SL_VECTOR(LightningBranch*)::const_iterator it = branches.begin(); it != branches.end(); it++) {
        const LightningBranch* branch = *it;
        branch->Draw(hdr, distanceFactor, this, tcsData);
    }

    renderer->SetModelviewMatrix(renderCamera->GetModelViewMatrix());

    renderer->EnableTexture2D(true);
}

void Lightning::GetBranchAngles(double seedTheta, double seedPhi, double& theta, double& phi, const RandomNumberGenerator* rng)
{
    theta = MathUtils::ToRadians(segmentAngleMean);
    phi = MathUtils::ToRadians(segmentAngleMean);

    theta = seedTheta + ((rng->UniformRandomDouble() * (theta * 2.0)) - theta);
    phi = seedPhi + ((rng->UniformRandomDouble() * (phi * 2.0)) - phi);
}

Vector3 Lightning::GenerateSegment(const Vector3& lastPoint, double seedTheta, double seedPhi,
                                   double& theta, double& phi, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    GetBranchAngles(seedTheta, seedPhi, theta, phi, rng);

    double segmentLength = rng->UniformRandomDouble() * maxSegmentLength;

    Vector3 delta;
    delta.x = segmentLength * sin(theta) * cos(phi);
    delta.z = -(segmentLength * sin(theta) * sin(phi));
    delta.y = -(segmentLength * cos(theta));

    const Camera* camera = tcsData->GetCamera();

    delta = delta * camera->GetBasis3x3();

    return lastPoint + delta;
}

void Lightning::GenerateBranch(double theta, double phi, double thickness, const Vector3& startPos,
                               int segmentsLeft, LightningBranch* branch, const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    const Camera* camera = tcsData->GetCamera();
    Matrix3 invBasis = camera->GetInverseBasis3x3();

    Vector3 pt = startPos;

    branch->points.push_back(startPos);

    while (segmentsLeft > 0) {
        double newTheta, newPhi;
        pt = GenerateSegment(pt, theta, phi, newTheta, newPhi, rng, tcsData);

        branch->points.push_back(pt);
        segmentsLeft--;

        if ((pt * invBasis).y < -height)
            return;

        if (rng->UniformRandomDouble() < BranchProbability((pt * invBasis).y)) {
            LightningBranch* newBranch = SL_NEW LightningBranch(tcsData, lightningParameters);
            newBranch->lineWidth = thickness * thicknessReduction;
            GenerateBranch(newTheta, newPhi, thickness * thicknessReduction, pt, segmentsLeft, newBranch, rng, tcsData);
            branches.push_back(newBranch);
        }
    }
}

double Lightning::BranchProbability(double y)
{
    double pFromTop = MathUtils::Abs(y) / height;
    return pFromTop * maxBranchProbability;
}

bool Lightning::Generate(const RandomNumberGenerator* rng, ThreadCameraStreamData* tcsData)
{
    DeleteBranches();

    LightningBranch* mainChannel = SL_NEW LightningBranch(tcsData, lightningParameters);
    mainChannel->lineWidth = initialThickness;

    Vector3 lastPt(0, 0, 0);
    startPos = lastPt;
    mainChannel->points.push_back(lastPt);
    mainChannel->isMainBranch = true;

    const Camera* camera = tcsData->GetCamera();

    while ((lastPt * camera->GetInverseBasis3x3()).y > -height) {
        double theta, phi;
        Vector3 nextPoint = GenerateSegment(lastPt, 0, 0, theta, phi, rng, tcsData);
        mainChannel->points.push_back(nextPoint);

        // Spawn a branch?
        if (rng->UniformRandomDouble() < BranchProbability((lastPt * camera->GetInverseBasis3x3()).y)) {
            double branchThickness = rng->UniformRandomDouble() * (maxBranchThickness - minBranchThickness) +
                                     minBranchThickness;
            branchThickness *= initialThickness;
            LightningBranch* newBranch = SL_NEW LightningBranch(tcsData, lightningParameters);
            newBranch->lineWidth = branchThickness;
            int numSegments = (int)(rng->UniformRandomDouble() * maxSegmentsPerBranch);
            GenerateBranch(theta, phi, branchThickness, nextPoint, numSegments, newBranch, rng, tcsData);
            branches.push_back(newBranch);
        }

        lastPt = nextPoint;
    }

    endPos = lastPt;

    branches.push_back(mainChannel);

    for (SL_VECTOR(LightningBranch*)::const_iterator it = branches.begin(); it != branches.end(); it++) {
        LightningBranch* branch = *it;
        branch->SyncVertexBuffer();
    }

    return true;
}

double Lightning::GetMainBranchCoeff(void) const
{
    return mainBranchCoeff;
}
double Lightning::GetBranchCoeff(void) const
{
    return branchCoeff;
}
float Lightning::GetFogAlpha(void) const
{
    return fogAlpha;
}


}