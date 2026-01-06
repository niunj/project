// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "PrecipitationShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{

PrecipitationShaderConstantLocations::PrecipitationShaderConstantLocations()
{
    Reset();
}
void PrecipitationShaderConstantLocations::Init(Renderer* renderer, ShaderHandle _shaderHandle)
{
    Reset();
    if (renderer == nullptr) {
        return;
    }
    if (renderer->GetIsOpenGL()==false && renderer->GetIsVulkan()==false) {
        return;
    }

    shaderHandle = _shaderHandle;

    sl_proj = renderer->GetConstantLocation(shaderHandle, "sl_proj");
    sl_cameraModelView = renderer->GetConstantLocation(shaderHandle, "sl_cameraModelView");
    sl_rotateTranslate = renderer->GetConstantLocation(shaderHandle, "sl_rotateTranslate");
    sl_scale = renderer->GetConstantLocation(shaderHandle, "sl_scale");

    sl_outputScale = renderer->GetConstantLocation(shaderHandle, "sl_outputScale");

    sl_modelViewProj = renderer->GetConstantLocation(shaderHandle, "sl_modelViewProj");
    sl_lightingColor = renderer->GetConstantLocation(shaderHandle, "sl_lightingColor");

}
void PrecipitationShaderConstantLocations::Reset()
{
    shaderHandle = 0;

    sl_proj = -1;
    sl_cameraModelView = -1;
    sl_rotateTranslate = -1;
    sl_scale = -1;

    sl_outputScale = -1;

    sl_modelViewProj = -1;
    sl_lightingColor = -1;
}
}