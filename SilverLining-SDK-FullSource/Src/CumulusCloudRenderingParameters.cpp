// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "CumulusCloudRenderingParameters.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "SLAssert.h"

namespace SilverLining
{
CumulusCloudRenderingParameters::CumulusCloudRenderingParameters(double unitScale)
    :  recompiles(0)
    , noUpdate(true)
    , cullInteriorVoxels(false)
    , quickLightingAttenuation(2000.0)
    , quickLightingAttenuationHiRes(3000.0)
    , recompileBudget(1)
    , sortAngleThreshold(cos(MathUtils::ToRadians(10.0)))
    , alphaCullThreshold(0.1)
    , colorRandomness(0.2)
    , colorRandomnessHiRes(0.4)
    , darkenWithDensity(true)
    , darkenAmount(0.1)
    , fogRefreshFrequency(10)
    , spreadOutFog(true)
    , atlasBlend(false)
    , sizeRandomness(0.2f)
    , inc(0)
{
    ReadConfig(unitScale);
}

CumulusCloudRenderingParameters::~CumulusCloudRenderingParameters()
{
    // nothing to do
}

void CumulusCloudRenderingParameters::SetFrameStarted()
{
    recompiles = 0;
}

void CumulusCloudRenderingParameters::ReadConfig(double unitScale)
{
    /** The purpose of this is to spread out the calculation of fog on each cloud over N
    frames, where N is the value of fogRefreshFrequency. */
    int iRefresh = fogRefreshFrequency;
    Configuration::GetIntValue("cumulus-fog-refresh-frequency", iRefresh);
    fogRefreshFrequency = iRefresh;

    if (Configuration::GetDoubleValue("cumulus-sorting-threshold-angle", sortAngleThreshold)) {
        sortAngleThreshold = cos(MathUtils::ToRadians(sortAngleThreshold));
    }

    Configuration::GetIntValue("cumulus-recompile-budget", recompileBudget);
    Configuration::GetDoubleValue("cumulus-lighting-attenuation",
                                  quickLightingAttenuation);
    Configuration::GetDoubleValue("cumulus-lighting-attenuation-hi-res",
                                  quickLightingAttenuationHiRes);
    quickLightingAttenuation *= unitScale;
    quickLightingAttenuationHiRes *= unitScale;

    Configuration::GetBoolValue("disable-cloud-growth", noUpdate);
    Configuration::GetBoolValue("cull-interior-voxels", cullInteriorVoxels);
    Configuration::GetDoubleValue("cumulus-cull-fade-threshold", alphaCullThreshold);

    Configuration::GetDoubleValue("metaball-color-randomness", colorRandomness);
    Configuration::GetDoubleValue("metaball-color-randomness-hi-res", colorRandomnessHiRes);

    Configuration::GetFloatValue("cumulus-voxel-size-variation", sizeRandomness);

    Configuration::GetBoolValue("cumulus-darken-with-density", darkenWithDensity);
    Configuration::GetDoubleValue("cumulus-density-darkening", darkenAmount);
    Configuration::GetBoolValue("cumulus-spread-out-fog", spreadOutFog);

    Configuration::GetBoolValue("cloud-atlas-normal-blend", atlasBlend);;
}

void CumulusCloudRenderingParameters::IncrementRecompiles(void)
{
    ++recompiles;
}

int CumulusCloudRenderingParameters::GetFogRefreshSlice(void) const
{
    if (inc >= fogRefreshFrequency) {
        inc = 0;
    }
    int valToReturn = inc;
    inc++;
    return valToReturn;
}


double CumulusCloudRenderingParameters::GetSortAngleThreshold(void) const
{
    return sortAngleThreshold;
}
int CumulusCloudRenderingParameters::GetRecompiles(void) const
{
    return recompiles;
}
int CumulusCloudRenderingParameters::GetRecompileBudget(void) const
{
    return recompileBudget;
}
double CumulusCloudRenderingParameters::GetColorRandomness(void) const
{
    return colorRandomness;
}
double CumulusCloudRenderingParameters::GetColorRandomnessHiRes(void) const
{
    return colorRandomnessHiRes;
}
float CumulusCloudRenderingParameters::GetSizeRandomness(void) const
{
    return sizeRandomness;
}
double CumulusCloudRenderingParameters::GetQuickLightingAttenuation(void) const
{
    return quickLightingAttenuation;
}
double CumulusCloudRenderingParameters::GetQuickLightingAttenuationHiRes(void) const
{
    return quickLightingAttenuationHiRes;
}
bool CumulusCloudRenderingParameters::GetNoUpdate(void) const
{
    return noUpdate;
}
bool CumulusCloudRenderingParameters::GetCullInteriorVoxels(void) const
{
    return cullInteriorVoxels;
}
double CumulusCloudRenderingParameters::GetAlphaCullThreshold(void) const
{
    return alphaCullThreshold;
}
bool CumulusCloudRenderingParameters::GetDarkenWithDensity(void) const
{
    return darkenWithDensity;
}
double CumulusCloudRenderingParameters::GetDarkenAmount(void) const
{
    return darkenAmount;
}
bool CumulusCloudRenderingParameters::GetSpreadOutFog(void) const
{
    return spreadOutFog;
}
bool CumulusCloudRenderingParameters::GetAtlasBlend(void) const
{
    return atlasBlend;
}
bool CumulusCloudRenderingParameters::GetUseVrmlCloudFormat(void) const
{
    return useVrmlCloudFormat;
}
unsigned int CumulusCloudRenderingParameters::GetFogRefreshFrequency(void) const
{
    return fogRefreshFrequency;
}
}