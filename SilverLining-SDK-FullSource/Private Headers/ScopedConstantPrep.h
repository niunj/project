// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class Renderer;
    class ScopedConstantPrep
    {
    public:
        ScopedConstantPrep(Renderer* renderer, ShaderHandle shader, bool doPost);
        virtual ~ScopedConstantPrep();
    protected:
        Renderer* renderer;
        ShaderHandle shaderHandle;
        bool doPost;
    };
}