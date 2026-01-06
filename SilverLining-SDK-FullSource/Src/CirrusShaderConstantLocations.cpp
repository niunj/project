// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "CirrusShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
CirrusShaderConstantLocations::CirrusShaderConstantLocations()
{
    Reset();
}

CirrusShaderConstantLocations::~CirrusShaderConstantLocations()
{
    // nothing else to do
}

void CirrusShaderConstantLocations::Init(ShaderHandle shader, ThreadCameraStreamData* tcsData)
{
    Reset();

    const Renderer* renderer = tcsData->GetRenderer();
    if (renderer == 0 || !(renderer->SupportsConstantLocations())) {
        return;
    }

    sl_modelView = renderer->GetConstantLocation(shader, "sl_modelView");
    sl_modelViewProj = renderer->GetConstantLocation(shader, "sl_modelViewProj");
    sl_fogColorAndDensity = renderer->GetConstantLocation(shader, "sl_fogColorAndDensity");
    sl_lightingColor = renderer->GetConstantLocation(shader, "sl_lightingColor");

    sl_outputScale = renderer->GetConstantLocation(shader, "sl_outputScale");
    sl_textureOffset = renderer->GetConstantLocation(shader, "sl_textureOffset");
}

void CirrusShaderConstantLocations::Reset()
{
    sl_modelView = -1;
    sl_modelViewProj = -1;
    sl_fogColorAndDensity = -1;
    sl_lightingColor = -1;

    sl_outputScale = -1;
    sl_textureOffset = -1;
}
}