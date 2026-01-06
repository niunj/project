// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file FlareShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "FlareShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;
    class FlareShader : public MemObject
    {
    public:
        FlareShader(ThreadCameraStreamData* _tcsData);
        virtual ~FlareShader();
    public:
        ShaderHandle GetShader(void) const;
        const FlareShaderConstantLocations& GetConstantLocations(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle flareShader;
        FlareShaderConstantLocations constantLocations;
    };
}