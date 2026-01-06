// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "MetaballRenderingParameters.h"
#include "Configuration.h"
#include "MathUtils.h"

namespace SilverLining
{
void MetaballRenderingParameters::ReadConfig()
{
    const char * phaseFunctionString;
    Configuration::GetStringValue("phase-function", phaseFunctionString);
    if (strcmp(phaseFunctionString, "isotropic") == 0) {
        phaseFunction = ISOTROPIC;
    } else if (strcmp(phaseFunctionString, "anisotropic") == 0) {
        phaseFunction = ANISOTROPIC;
    } else if (strcmp(phaseFunctionString, "lambertian") == 0) {
        phaseFunction = LAMBERTIAN;
    } else if (strcmp(phaseFunctionString, "rayleigh") == 0) {
        phaseFunction = RAYLEIGH;
    } else if (strcmp(phaseFunctionString, "henyey-greenstein") == 0) {
        phaseFunction = HENYEY_GREENSTEIN;
        Configuration::GetDoubleValue("henyey-greenstein-g1", henyeyGreensteinG1);
        Configuration::GetDoubleValue("henyey-greenstein-g2", henyeyGreensteinG2);
        Configuration::GetDoubleValue("henyey-greenstein-f", henyeyGreensteinF);
    }

    Configuration::GetFloatValue("max-metaball-color", maxMetaballColor);
}

MetaballRenderingParameters::MetaballRenderingParameters()
    : ambient(0, 0, 0)
    , lightPosition(0, 0, 0)
    , cameraPosition(0, 0, 0)
    , offsetPosition(0, 0, 0)

    , phaseFunction(LAMBERTIAN)
    , anisotropicX(0.2)
    , henyeyGreensteinG1(0.8)
    , henyeyGreensteinG2(-0.1)
    , henyeyGreensteinF(0.85)
    , maxMetaballColor(0.9f)
{
    ReadConfig();
}

MetaballRenderingParameters::~MetaballRenderingParameters()
{
    // nothing else to do
}

void MetaballRenderingParameters::SetAmbientColor(const Color& amb)
{
    ambient = amb;
}

Color MetaballRenderingParameters::GetAmbientColor() const
{
    return ambient;
}

void MetaballRenderingParameters::SetLightPosition(const Vector3& pos)
{
    lightPosition = pos;
}

const Vector3& MetaballRenderingParameters::GetLightPosition(void) const
{
    return lightPosition;
}

void MetaballRenderingParameters::SetCameraPosition(const Vector3& pos)
{
    cameraPosition = pos;
}

const Vector3& MetaballRenderingParameters::GetCameraPosition() const
{
    return cameraPosition;
}

void MetaballRenderingParameters::SetOffsetPosition(const Vector3& pos)
{
    offsetPosition = pos;
}

const Vector3& MetaballRenderingParameters::GetOffsetPosition() const
{
    return offsetPosition;
}

double MetaballRenderingParameters::PhaseFunction(double cosa) const
{
    switch (phaseFunction) {

    case ISOTROPIC:
        return 1;

    case ANISOTROPIC:
        return (1 + anisotropicX * cosa);

    case LAMBERTIAN: {
        double a = MathUtils::Acos(cosa);
        return (8.0 / 3.0) * MathUtils::Pi() * (sin(a) + (MathUtils::Pi() - a) * cosa);
    }

    case RAYLEIGH:
        return 0.75 * (1 + cosa * cosa);

    case HENYEY_GREENSTEIN: {
        double g1sq = henyeyGreensteinG1 * henyeyGreensteinG1;
        double g1 = (1 - g1sq) / pow((1 + g1sq - 2 * henyeyGreensteinG1 * cosa), 1.5);
        double g2sq = henyeyGreensteinG2 * henyeyGreensteinG2;
        double g2 = (1 - g2sq) / pow((1 + g2sq - 2 * henyeyGreensteinG2 * cosa), 1.5);
        return (g1 * henyeyGreensteinF) + (g2 * (1 - henyeyGreensteinF));
    }

    default:
        return 1;
    }
}

float MetaballRenderingParameters::GetMaxMetaballColor(void) const
{
    return maxMetaballColor;
}
}