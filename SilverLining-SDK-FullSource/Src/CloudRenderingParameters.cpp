// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "CloudRenderingParameters.h"
#include "Configuration.h"
#include "MathUtils.h"
#include "SLAssert.h"

namespace SilverLining
{
CloudRenderingParameters::CloudRenderingParameters()
    : cosLightChangeAngle(0.999847)
    , ambientColor()
{
    double lightChangeAngle = 1.0;
    Configuration::GetDoubleValue("cloud-light-change-angle", lightChangeAngle);

    cosLightChangeAngle = cos(MathUtils::ToRadians(lightChangeAngle));
}

CloudRenderingParameters::~CloudRenderingParameters()
{
    // nothing to do
}

void CloudRenderingParameters::SetAmbientColor(const Color& val)
{
    ambientColor = val;
}

double CloudRenderingParameters::GetCosLightChangeAngle(void) const
{
    return cosLightChangeAngle;
}
}