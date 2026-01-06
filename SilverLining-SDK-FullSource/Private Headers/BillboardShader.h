// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file BillboardShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SLMap.h"
#include "SilverLiningTypes.h"
#include "Color.h"
#include "ShaderStructures.h"

namespace SilverLining
{
    class Atmosphere;
	class Renderer;
    class ThreadCameraStreamData;
    class Camera;
    class OverriddenBillboardMatrix;
    class BillboardShaderConstantLocations;

    //! structure for each billboard parameters (via UBO/SSBO)
    //! if we are using billboard instancing 
    struct BillboardParams
    {
        mat4 sl_modelView;
        mat4 sl_modelViewProj;
        mat4 sl_billboard;
        mat4 sl_basis;

        vec4f sl_flipTCoordsAndFogDensity;
        vec4f sl_fogColor;
        vec4f sl_billboardColor;
        vec4f sl_fade;

        vec4f sl_outputScale;
        vec4f sl_noSpin;
        vec4f sl_padding0;
        vec4f sl_forceDepth;
    };

    class BillboardShader : public MemObject
    {
    public:
        //! constructor
        BillboardShader(ThreadCameraStreamData* tcsData, bool instanced);
        //! destructor
        virtual ~BillboardShader();
    public:
        ShaderHandle GetShaderHandle(void) const;
        const BillboardShaderConstantLocations* GetConstantLocations(void) const;
    private:
        //! not allowed
        BillboardShader();
    private:
        ShaderHandle shaderHandle;
        BillboardShaderConstantLocations* locations;
        ThreadCameraStreamData* tcsData;
        const bool instanced;
    };
 
    //! Helper class to render billboards
    class BillboardShadingUtilities : public MemObject
    {
    public:
        //! constructor
        BillboardShadingUtilities(ShaderHandle billboardShader, const BillboardShaderConstantLocations* billboardShaderConstantLocations, ThreadCameraStreamData* tcsData);
        //! destructor
        virtual ~BillboardShadingUtilities();
    public:
        void SetShaderColor(const Color& color) const;
        void UnsetShaderColor() const;
        void BindAndSaveFogAndSetupConstants(double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix, bool& savedFog, bool infiniteDepth) const;
        void UnbindAndRestoreFog(bool savedFog) const;

    protected:
        const ShaderHandle billboardShader;
        const BillboardShaderConstantLocations* billboardShaderConstantLocations;
        ThreadCameraStreamData* tcsData;
    };

}