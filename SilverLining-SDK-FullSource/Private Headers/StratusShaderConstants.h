// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file StratusShaderConstants.h
\brief Manages the constant(uniform/offset in an ubo) locations for stratus shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class StratusShaderConstants : public MemObject
    {
    public:
        StratusShaderConstants();
        virtual ~StratusShaderConstants();
    public:
        void Reset(void);
        void Init(ShaderHandle shader, ThreadCameraStreamData* tcsData);

        int sl_modelViewProjLoc;
        int sl_modelViewLoc;
        int sl_modelPosLoc;
        int sl_cameraPosLoc;

        int sl_lightDirectionLoc;
        int sl_layerThicknessAndIsTopLoc;
        int sl_groundColorLoc;
        int sl_outputScaleLoc;
        int sl_cloudTintLoc;

        int sl_displacementVectorAndContrastLoc;
        int sl_fadeAndDisplacementFactorLoc;
        int sl_upVectorAndThicknessLoc;
        int sl_sunColorLoc;
        int sl_skyColorLoc;
        int sl_invBasisLoc;
        int sl_layerSizeAndUnitScaleLoc;
        int sl_fogColorAndDensityLoc;
        int sl_extinctionFactorLoc;
    };
}