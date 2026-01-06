// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file CirrusShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "CirrusShaderConstantLocations.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class CirrusShader : public MemObject
    {
    public:
        CirrusShader(ThreadCameraStreamData* _tcsData);
        virtual ~CirrusShader();
    public:
        ShaderHandle GetShader(void) const;
        const CirrusShaderConstantLocations& GetConstantLocations(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle hdrShader;
        CirrusShaderConstantLocations constantLocations;
    };
}