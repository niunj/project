// Copyright (c) 2020 Sundog Software LLC, All rights reserved worldwide

/**
\file PrecipitationShaderConstantLocations.h
\brief Container for precipitation shader and constant locations
*/

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class Renderer;
    class PrecipitationShaderConstantLocations : public MemObject
    {
    public:
        PrecipitationShaderConstantLocations();
        ShaderHandle shaderHandle;

        int sl_proj;
        int sl_cameraModelView;
        int sl_rotateTranslate;
        int sl_scale;

        int sl_outputScale;

        // for snow
        int sl_modelViewProj;
        int sl_lightingColor;

        void Init(Renderer* renderer, ShaderHandle shaderHandle);
        void Reset(void);
    private:
    };
}
