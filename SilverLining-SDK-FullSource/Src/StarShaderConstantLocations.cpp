// Copyright (c) 2004-2024  Sundog Software, LLC. All rights reserved worldwide.

#include "StarShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StarShaderConstantLocations::StarShaderConstantLocations()
{
    Reset();
}

StarShaderConstantLocations::~StarShaderConstantLocations()
{
    // nothing else to do
}

void StarShaderConstantLocations::Init(ShaderHandle shaderHandle, ThreadCameraStreamData* tcsData)
{
    Reset();

    const Renderer* renderer = tcsData->GetRenderer();

    sl_invBasis = renderer->GetConstantLocation(shaderHandle, "sl_invBasis");
    sl_modelView = renderer->GetConstantLocation(shaderHandle, "sl_modelView");
    sl_modelViewProj = renderer->GetConstantLocation(shaderHandle, "sl_modelViewProj");
    sl_equatorialToHorizon = renderer->GetConstantLocation(shaderHandle, "sl_equatorialToHorizon");
    sl_fog = renderer->GetConstantLocation(shaderHandle, "sl_fog");
    sl_fogDistance = renderer->GetConstantLocation(shaderHandle, "sl_fogDistance");
    sl_up = renderer->GetConstantLocation(shaderHandle, "sl_up");

    sl_outputScale = renderer->GetConstantLocation(shaderHandle, "sl_outputScale");

    sl_pointSize = renderer->GetConstantLocation(shaderHandle, "sl_pointSize");

    sl_forceDepth = renderer->GetConstantLocation(shaderHandle, "sl_forceDepth");
}

void StarShaderConstantLocations::Reset()
{
    sl_invBasis = -1;
    sl_modelView = -1;
    sl_modelViewProj = -1;
    sl_equatorialToHorizon = -1;;
    sl_fog = -1;;
    sl_fogDistance = -1;;
    sl_up = -1;;

    sl_outputScale = -1;

    sl_pointSize = -1;

    sl_forceDepth = -1;
}
}