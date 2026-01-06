// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file StratocumulusShaders.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "StratocumulusShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class StratocumulusShaders : public MemObject
    {
    public:
        StratocumulusShaders(ThreadCameraStreamData* _tcsData);
        virtual ~StratocumulusShaders();

        ShaderHandle GetShader(void) const;
        ShaderHandle GetShaderHdr(void) const;

        const StratocumulusShaderConstantLocations& GetShaderConstantLocations(void) const;
        const StratocumulusShaderConstantLocations& GetShaderConstantLocationsHdr(void) const;

    public:
        mutable ShaderHandle shader, hdrShader;
    private:
        void DeleteShaders(void);
    private:
        ThreadCameraStreamData* tcsData;
        
        mutable StratocumulusShaderConstantLocations shaderConstantLocations;
        mutable StratocumulusShaderConstantLocations shaderConstantLocationsHDR;
    };
}