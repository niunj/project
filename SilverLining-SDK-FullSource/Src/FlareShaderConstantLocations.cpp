// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "FlareShaderConstantLocations.h"
#include "Renderer.h"

namespace SilverLining
{
FlareShaderConstantLocations::FlareShaderConstantLocations()
{
    Reset();
}
void FlareShaderConstantLocations::Init(ShaderHandle shader, ThreadCameraStreamData* tcsData)
{
    Reset();
    const Renderer* renderer = tcsData->GetRenderer();
    sl_modelView = renderer->GetConstantLocation(shader, "sl_modelView");
    sl_modelViewProj = renderer->GetConstantLocation(shader, "sl_modelViewProj");
    sl_lightingColor = renderer->GetConstantLocation(shader, "sl_lightingColor");
    sl_outputScale = renderer->GetConstantLocation(shader, "sl_outputScale");
}

void FlareShaderConstantLocations::Reset()
{
    sl_modelView = -1;
    sl_modelViewProj = -1;
    sl_lightingColor = -1;
    sl_outputScale = -1;
}
}