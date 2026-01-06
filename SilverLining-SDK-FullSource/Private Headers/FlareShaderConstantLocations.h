// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file FlareShaderConstantLocations.h
\brief Manages the constant(uniform/offset in an ubo) locations for flare shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class FlareShaderConstantLocations : public MemObject
    {
    public:
        FlareShaderConstantLocations();
        void Init(ShaderHandle shaderHandle, ThreadCameraStreamData* tcsData);

        int sl_modelView;
        int sl_modelViewProj;
        int sl_lightingColor;

        int sl_outputScale;
    private:
        void Reset(void);
    };
}