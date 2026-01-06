// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.
#include "StratusShaderConstants.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
StratusShaderConstants::StratusShaderConstants()
{
    Reset();
}

StratusShaderConstants::~StratusShaderConstants()
{
    // nop
}

void StratusShaderConstants::Reset()
{
    sl_modelViewProjLoc = -1;
    sl_modelViewLoc = -1;
    sl_modelPosLoc = -1;
    sl_cameraPosLoc = -1;

    sl_lightDirectionLoc = -1;
    sl_layerThicknessAndIsTopLoc = -1;
    sl_groundColorLoc = -1;
    sl_outputScaleLoc = -1;
    sl_cloudTintLoc = -1;

    sl_displacementVectorAndContrastLoc = -1;
    sl_fadeAndDisplacementFactorLoc = -1;
    sl_upVectorAndThicknessLoc = -1;
    sl_sunColorLoc = -1;
    sl_skyColorLoc = -1;
    sl_invBasisLoc = -1;
    sl_layerSizeAndUnitScaleLoc = -1;
    sl_fogColorAndDensityLoc = -1;
    sl_extinctionFactorLoc = -1;
}


void StratusShaderConstants::Init(ShaderHandle shader, ThreadCameraStreamData* tcsData)
{
    const Renderer* renderer = tcsData->GetRenderer();
    if (renderer == 0 || !(renderer->SupportsConstantLocations())) {
        return;
    }

    sl_modelViewProjLoc = renderer->GetConstantLocation(shader, "sl_modelViewProj");
    sl_modelViewLoc = renderer->GetConstantLocation(shader, "sl_modelView");
    sl_modelPosLoc = renderer->GetConstantLocation(shader, "sl_modelPos");
    sl_cameraPosLoc = renderer->GetConstantLocation(shader, "sl_cameraPos");

    sl_lightDirectionLoc = renderer->GetConstantLocation(shader, "sl_lightDirection");
    sl_layerThicknessAndIsTopLoc = renderer->GetConstantLocation(shader, "sl_layerThicknessAndIsTop");
    sl_groundColorLoc = renderer->GetConstantLocation(shader, "sl_groundColor");
    sl_outputScaleLoc = renderer->GetConstantLocation(shader, "sl_outputScale");
    sl_cloudTintLoc = renderer->GetConstantLocation(shader, "sl_cloudTint");

    sl_displacementVectorAndContrastLoc = renderer->GetConstantLocation(shader, "sl_displacementVectorAndContrast");
    sl_fadeAndDisplacementFactorLoc = renderer->GetConstantLocation(shader, "sl_fadeAndDisplacementFactor");
    sl_upVectorAndThicknessLoc = renderer->GetConstantLocation(shader, "sl_upVectorAndThickness");
    sl_sunColorLoc = renderer->GetConstantLocation(shader, "sl_sunColor");
    sl_skyColorLoc = renderer->GetConstantLocation(shader, "sl_skyColor");
    sl_invBasisLoc = renderer->GetConstantLocation(shader, "sl_invBasis");
    sl_layerSizeAndUnitScaleLoc = renderer->GetConstantLocation(shader, "sl_layerSizeAndUnitScale");
    sl_fogColorAndDensityLoc = renderer->GetConstantLocation(shader, "sl_fogColorAndDensity");
    sl_extinctionFactorLoc = renderer->GetConstantLocation(shader, "sl_extinctionFactor");
}
}