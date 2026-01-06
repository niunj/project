// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file SkyShaders.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SkyShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;
    class SkyShaders : public MemObject
    {
    public:
        SkyShaders(ThreadCameraStreamData* _tcsData, bool _simpleShader);
        virtual ~SkyShaders();

    public:
        ShaderHandle GetSkyShader(void) const;
        ShaderHandle GetSkyShaderHdr(void) const;

        ShaderHandle GetSkyShaderHosenWilkie(void) const;
        ShaderHandle GetSkyShaderHosenWilkieHdr(void) const;

        ShaderHandle GetShaderFor(SkyModel skyModel, bool disableToneMapping) const;

        const SkyShaderConstantLocations& GetSkyShaderConstantLocations(SkyModel skyModel, bool disableToneMapping) const;
    private:
        void DeleteShaders(void);
    private:
        ThreadCameraStreamData* tcsData;
        bool simpleShader;


        ShaderHandle skyShader, skyShaderHDR;
        ShaderHandle skyShaderHW, skyShaderHDRHW;

        SkyShaderConstantLocations skyShaderConstantLocations;
        SkyShaderConstantLocations skyShaderHDRConstantLocations;
        SkyShaderConstantLocations skyShaderHWConstantLocations;
        SkyShaderConstantLocations skyShaderHDRHWConstantLocations;
    }; 
}