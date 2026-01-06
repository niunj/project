// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file StarShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "StarShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class StarShader : public MemObject
    {
    public:
        StarShader(ThreadCameraStreamData* _tcsData);
        virtual ~StarShader();
    public:
        ShaderHandle GetShader(void) const;
        const StarShaderConstantLocations& GetConstantLocations(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle starShader;
        StarShaderConstantLocations starShaderConstantLocations;
    };
}