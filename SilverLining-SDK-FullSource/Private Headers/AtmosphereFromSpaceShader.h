// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file AtmosphereFromSpaceShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class AtmosphereFromSpaceShader : public MemObject
    {
    public:
        AtmosphereFromSpaceShader(ThreadCameraStreamData* _tcsData, bool doShadow);
        virtual ~AtmosphereFromSpaceShader();
    public:
        ShaderHandle GetShader(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle atmoShader;
    };
}