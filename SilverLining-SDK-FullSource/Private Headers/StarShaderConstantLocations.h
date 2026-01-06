// Copyright (c) 2004-2024  Sundog Software, LLC. All rights reserved worldwide.

/**
\file StarShaderConstantLocations
\brief Manages the constant(uniform/offset in an ubo) locations for star shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class StarShaderConstantLocations : public MemObject
    {
    public:
        StarShaderConstantLocations();
        virtual ~StarShaderConstantLocations();
    public:
        void Init(ShaderHandle shaderHandle, ThreadCameraStreamData* tcsData);
    public:
        int sl_invBasis;
        int sl_modelView;
        int sl_modelViewProj;
        int sl_equatorialToHorizon;
        int sl_fog;
        int sl_fogDistance;
        int sl_up;

        int sl_outputScale;

        int sl_pointSize;
        int sl_forceDepth;
    private:
        void Reset();
    };
}