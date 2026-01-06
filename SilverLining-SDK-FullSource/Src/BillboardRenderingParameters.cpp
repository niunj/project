// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "BillboardRenderingParameters.h"
#include "Camera.h"
#include "MathUtils.h"
#include "SLAssert.h"

namespace SilverLining
{
BillboardRenderingParameters::BillboardRenderingParameters()
    : spinEnabled(true)
    , softness(0.0f)
    , forceConstantColor(false)
    , forceColor()
    , additiveBlending(true)
    , verticalGradient(0.0)
    , flattening(1.0f)
    , fadeStartTime(0)
    , fogColorBoost(1.0f)
{
    // nothing to do
}

BillboardRenderingParameters::~BillboardRenderingParameters()
{
    // nothing to do
}

void BillboardRenderingParameters::SetFogColor(const Color& fogColor)
{
    fog = fogColor;
}


Color BillboardRenderingParameters::GetFogColor() const
{
    return fog;
}

void BillboardRenderingParameters::SetFogDensity(double density)
{
    fogDensity = density;
}

double BillboardRenderingParameters::GetFogDensity() const
{
    return fogDensity;
}

double BillboardRenderingParameters::CalculateFogFade(const Vector3& center, const Camera* camera) const
{
    double eyeLength2 = (center - camera->GetPosition()).SquaredLength();
    const double fogExponent2 = eyeLength2 * GetFogDensity() * GetFogDensity();
    double fogFade = 1.0 - fogExponent2;
    return fogFade;
}

void BillboardRenderingParameters::SetSpinEnabled(bool enabled)
{
    spinEnabled = enabled;
}

bool BillboardRenderingParameters::GetSpinEnabled() const
{
    return spinEnabled;
}

void BillboardRenderingParameters::SetSoftness(float pSoftness)
{
    softness = pSoftness;
}

float BillboardRenderingParameters::GetSoftness() const
{
    return softness;
}

void BillboardRenderingParameters::ForceConstantColor(bool force, const Color& color)
{
    forceConstantColor = force;
    forceColor = color;
}

bool BillboardRenderingParameters::GetForceConstantColor(Color& color) const
{
    color = forceColor;
    return forceConstantColor;
}

void BillboardRenderingParameters::SetAdditiveBlending(bool additive)
{
    additiveBlending = additive;
}

bool BillboardRenderingParameters::GetAdditiveBlending() const
{
    return additiveBlending;
}

void BillboardRenderingParameters::SetVerticalGradient(double gradient)
{
    verticalGradient = gradient;
}
double BillboardRenderingParameters::GetVerticalGradient() const
{
    return verticalGradient;
}

void BillboardRenderingParameters::SetFlattening(float f)
{
    flattening = f;
}
float BillboardRenderingParameters::GetFlattening() const
{
    return flattening;
}

void BillboardRenderingParameters::SetFadeStartTime(unsigned long ms)
{
    fadeStartTime = ms;
}
unsigned long BillboardRenderingParameters::GetFadeStartTime() const
{
    return fadeStartTime;
}

void BillboardRenderingParameters::SetFogColorBoost(float val)
{
    fogColorBoost = val;
}
float BillboardRenderingParameters::GetFogColorBoost(void) const
{
    return fogColorBoost;
}


}