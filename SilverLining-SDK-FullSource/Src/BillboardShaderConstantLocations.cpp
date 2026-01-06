// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "BillboardShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
void BillboardShaderConstantLocations::Reset()
{
    sl_modelViewLoc = -1;
    sl_modelViewProjLoc = -1;
    sl_billboardLoc = -1;
    sl_basisLoc = -1;
    sl_flipTCoordsAndFogDensityLoc = -1;
    sl_fogColorLoc = -1;
    sl_billboardColorLoc = -1;
    sl_fadeLoc = -1;

    sl_outputScaleLoc = -1;

    sl_noSpinLoc = -1;
    sl_forceDepthLoc = -1;
}

void BillboardShaderConstantLocations::Init(Renderer* renderer, ShaderHandle shaderHandle)
{
    Reset();

    sl_modelViewLoc = renderer->GetConstantLocation(shaderHandle, "sl_modelView");
    sl_modelViewProjLoc = renderer->GetConstantLocation(shaderHandle, "sl_modelViewProj");
    sl_billboardLoc = renderer->GetConstantLocation(shaderHandle, "sl_billboard");
    sl_basisLoc = renderer->GetConstantLocation(shaderHandle, "sl_basis");
    sl_flipTCoordsAndFogDensityLoc = renderer->GetConstantLocation(shaderHandle, "sl_flipTCoordsAndFogDensity");
    sl_fogColorLoc = renderer->GetConstantLocation(shaderHandle, "sl_fogColor");
    sl_fadeLoc = renderer->GetConstantLocation(shaderHandle, "sl_fade");
    sl_billboardColorLoc = renderer->GetConstantLocation(shaderHandle, "sl_billboardColor");

    sl_outputScaleLoc = renderer->GetConstantLocation(shaderHandle, "sl_outputScale");

    sl_noSpinLoc = renderer->GetConstantLocation(shaderHandle, "sl_noSpin");

    sl_forceDepthLoc = renderer->GetConstantLocation(shaderHandle, "sl_forceDepth");
}

BillboardShaderConstantLocations::BillboardShaderConstantLocations()
{
    Reset();
}

BillboardShaderConstantLocations::~BillboardShaderConstantLocations()
{
    // nothing to do
}

}