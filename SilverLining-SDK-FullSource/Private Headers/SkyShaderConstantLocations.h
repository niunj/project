// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file SkyShaderConstantLocations.h
\brief Manages the constant(uniform/offset in an ubo) locations for sky shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class SkyShaderConstantLocations : public MemObject
    {
    public:
        SkyShaderConstantLocations();

        //! initialize the locations for the given shader
        void Init(ShaderHandle shaderHandle, ThreadCameraStreamData* tcsData);

        bool invalid;

        ShaderHandle shader;

        int sl_XHosekABC_Loc;
        int sl_XHosekDEF_Loc;
        int sl_XHosekGHI_Loc;
        int sl_YHosekABC_Loc;
        int sl_YHosekDEF_Loc;
        int sl_YHosekGHI_Loc;
        int sl_ZHosekABC_Loc;
        int sl_ZHosekDEF_Loc;
        int sl_ZHosekGHI_Loc;
        int sl_HosekRadiances_Loc;

        int sl_sunPos_Loc;
        int sl_sunPerez_Loc;
        int sl_zenithPerez_Loc;

        int sl_moonPos_Loc;
        int sl_moonPerez_Loc;
        int sl_zenithMoonPerez_Loc;

        int sl_xPerezABC_Loc;
        int sl_xPerezDE_Loc;
        int sl_yPerezABC_Loc;
        int sl_yPerezDE_Loc;
        int sl_YPerezABC_Loc;
        int sl_YPerezDE_Loc;
        int sl_luminanceScales_Loc;

        int sl_kAndLdmax_Loc;

        int sl_overcast_Loc;
        int sl_fog_Loc;

        int sl_outputScale_Loc;

        int sl_XYZtoRGB_Loc;

        int sl_modelView_Loc;

        int sl_modelViewProj_Loc;

        int sl_forceDepth_Loc;
    private:
        //! invalidate (reset to -1)
        void Reset();
    private:
    };
}