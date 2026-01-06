// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file StratusShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "StratusShaderConstants.h"

namespace SilverLining
{
    class ThreadCameraStreamData;
    class StratusShader : public MemObject
    {
    public:
        StratusShader(ThreadCameraStreamData* _tcsData);
        virtual ~StratusShader();
    public:
        ShaderHandle GetShader(void) const;
        const StratusShaderConstants& GetConstantLocations(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle stratusShader;
        StratusShaderConstants constantLocations;
    };
}