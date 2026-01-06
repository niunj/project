// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file CirrusShaderConstantLocations.h
\brief Manages the constant(uniform/offset in an ubo) locations for Cirrus shaders
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class CirrusShaderConstantLocations : public MemObject
    {
    public:
        CirrusShaderConstantLocations();
        virtual ~CirrusShaderConstantLocations();
    public:
        void Init(ShaderHandle shader, ThreadCameraStreamData* tcsData);
    public:

        int sl_modelView;
        int sl_modelViewProj;
        int sl_fogColorAndDensity;
        int sl_lightingColor;

        int sl_outputScale;
        int sl_textureOffset;
    private:
        void Reset(void);
    };
}