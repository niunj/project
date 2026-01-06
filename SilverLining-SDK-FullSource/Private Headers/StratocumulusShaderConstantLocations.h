// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file StratocumulusShaderConstantLocations.h
\brief Manages the constant(uniform/offset in an ubo) locations for stratocumulus shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class StratocumulusShaderConstantLocations : public MemObject
    {
    public:
        StratocumulusShaderConstantLocations();
        void Init(ShaderHandle shader, ThreadCameraStreamData* tcsData);

        int sl_projectionMatrix;

        int sl_lightColor;
        int sl_lightObjectDirAndConstTerm;
        int sl_lightTexCoords;
        int sl_viewSampleDimensions;
        int sl_lightSampleDimensions;
        int sl_voxelDimensions;
        int sl_lightWorldDirAndExtinction;
        int sl_cameraTexCoords;
        int sl_skyLightColor;
        int sl_originTexCoords;
        int sl_fadeFlag;
        int sl_fogColorAndDensity;
        int sl_multipleScatteringTerm;
        int sl_noiseOffset;
        int sl_extinctionCoefficient;
        int sl_jitter;
        int sl_unitScale;
        int sl_outputScale;
        int sl_roundCenter;
    private:
        void Reset(void);
    };
}