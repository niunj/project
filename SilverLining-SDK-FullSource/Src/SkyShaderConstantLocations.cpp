// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "SkyShaderConstantLocations.h"
#include "Renderer.h"
#include "Atmosphere.h"

namespace SilverLining
{
SkyShaderConstantLocations::SkyShaderConstantLocations()
    : invalid(true), shader(0)
{
    Reset();
}

void SkyShaderConstantLocations::Reset()
{
    sl_XHosekABC_Loc = -1;
    sl_XHosekDEF_Loc = -1;
    sl_XHosekGHI_Loc = -1;
    sl_YHosekABC_Loc = -1;
    sl_YHosekDEF_Loc = -1;
    sl_YHosekGHI_Loc = -1;
    sl_ZHosekABC_Loc = -1;
    sl_ZHosekDEF_Loc = -1;
    sl_ZHosekGHI_Loc = -1;
    sl_HosekRadiances_Loc = -1;

    sl_sunPos_Loc = -1;
    sl_sunPerez_Loc = -1;
    sl_zenithPerez_Loc = -1;

    sl_moonPos_Loc = -1;
    sl_moonPerez_Loc = -1;
    sl_zenithMoonPerez_Loc = -1;

    sl_xPerezABC_Loc = -1;
    sl_xPerezDE_Loc = -1;
    sl_yPerezABC_Loc = -1;
    sl_yPerezDE_Loc = -1;
    sl_YPerezABC_Loc = -1;
    sl_YPerezDE_Loc = -1;
    sl_luminanceScales_Loc = -1;

    sl_kAndLdmax_Loc = -1;

    sl_overcast_Loc = -1;
    sl_fog_Loc = -1;

    sl_outputScale_Loc = -1;

    sl_XYZtoRGB_Loc = -1;

    sl_modelView_Loc = -1;
    sl_modelViewProj_Loc = -1;

    sl_forceDepth_Loc = -1;
}

void SkyShaderConstantLocations::Init(ShaderHandle _shader, ThreadCameraStreamData* tcsData)
{
    if (invalid == false && shader == _shader) {
        return;
    }
    const Renderer* renderer = tcsData->GetRenderer();
    if (renderer->GetType() != Atmosphere::OPENGL32CORE && renderer->GetType() != Atmosphere::OPENGL && renderer->GetType()!=Atmosphere::VULKAN) {
        return;
    }

    shader = _shader;

    sl_XHosekABC_Loc = renderer->GetConstantLocation(shader, "sl_XHosekABC");
    sl_XHosekDEF_Loc = renderer->GetConstantLocation(shader, "sl_XHosekDEF");
    sl_XHosekGHI_Loc = renderer->GetConstantLocation(shader, "sl_XHosekGHI");
    sl_YHosekABC_Loc = renderer->GetConstantLocation(shader, "sl_YHosekABC");
    sl_YHosekDEF_Loc = renderer->GetConstantLocation(shader, "sl_YHosekDEF");
    sl_YHosekGHI_Loc = renderer->GetConstantLocation(shader, "sl_YHosekGHI");
    sl_ZHosekABC_Loc = renderer->GetConstantLocation(shader, "sl_ZHosekABC");
    sl_ZHosekDEF_Loc = renderer->GetConstantLocation(shader, "sl_ZHosekDEF");
    sl_ZHosekGHI_Loc = renderer->GetConstantLocation(shader, "sl_ZHosekGHI");
    sl_HosekRadiances_Loc = renderer->GetConstantLocation(shader, "sl_HosekRadiances");

    sl_sunPos_Loc = renderer->GetConstantLocation(shader, "sl_sunPos");
    sl_sunPerez_Loc = renderer->GetConstantLocation(shader, "sl_sunPerez");
    sl_zenithPerez_Loc = renderer->GetConstantLocation(shader, "sl_zenithPerez");


    sl_moonPos_Loc = renderer->GetConstantLocation(shader, "sl_moonPos");
    sl_moonPerez_Loc = renderer->GetConstantLocation(shader, "sl_moonPerez");
    sl_zenithMoonPerez_Loc = renderer->GetConstantLocation(shader, "sl_zenithMoonPerez");


    sl_xPerezABC_Loc = renderer->GetConstantLocation(shader, "sl_xPerezABC");
    sl_xPerezDE_Loc = renderer->GetConstantLocation(shader, "sl_xPerezDE");

    sl_yPerezABC_Loc = renderer->GetConstantLocation(shader, "sl_yPerezABC");
    sl_yPerezDE_Loc = renderer->GetConstantLocation(shader, "sl_yPerezDE");

    sl_YPerezABC_Loc = renderer->GetConstantLocation(shader, "sl_YPerezABC");
    sl_YPerezDE_Loc = renderer->GetConstantLocation(shader, "sl_YPerezDE");

    sl_luminanceScales_Loc = renderer->GetConstantLocation(shader, "sl_luminanceScales");

    sl_kAndLdmax_Loc = renderer->GetConstantLocation(shader, "sl_kAndLdmax");

    sl_overcast_Loc = renderer->GetConstantLocation(shader, "sl_overcast");
    sl_fog_Loc = renderer->GetConstantLocation(shader, "sl_fog");

    sl_outputScale_Loc = renderer->GetConstantLocation(shader, "sl_outputScale");

    sl_XYZtoRGB_Loc = renderer->GetConstantLocation(shader, "sl_XYZtoRGB");

    sl_modelView_Loc = renderer->GetConstantLocation(shader, "sl_modelView");
    sl_modelViewProj_Loc = renderer->GetConstantLocation(shader, "sl_modelViewProj");

    if (renderer->GetIsVulkan()) sl_forceDepth_Loc = renderer->GetConstantLocation(shader, "sl_forceDepth");

    invalid = false;
}
}

