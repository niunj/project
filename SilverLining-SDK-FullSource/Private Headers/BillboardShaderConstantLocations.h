// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/**
\file BillboardShaderConstantLocations.h
\brief Manages the constant(uniform/offset in an ubo) locations for a billboard shader
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class Renderer;

    class BillboardShaderConstantLocations : public MemObject
    {
    public:
        BillboardShaderConstantLocations();
        virtual ~BillboardShaderConstantLocations();
    public:
        //! initialize for a given shader
        void Init(Renderer* renderer, ShaderHandle shaderHandle);
    public:
        int sl_modelViewLoc;
        int sl_modelViewProjLoc;
        int sl_billboardLoc;
        int sl_basisLoc;
        int sl_flipTCoordsAndFogDensityLoc;
        int sl_fogColorLoc;
        int sl_billboardColorLoc;
        int sl_fadeLoc;

        int sl_outputScaleLoc;

        int sl_noSpinLoc;
        int sl_forceDepthLoc;
    private:
        //! internal. reset to -1
        void Reset(void);
    };
}