// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file GlareShader.h
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"

namespace SilverLining
{
    class ThreadCameraStreamData;

    class GlareShader : public MemObject
    {
    public:
        GlareShader(ThreadCameraStreamData* _tcsData);
        virtual ~GlareShader();
    public:
        virtual bool Bind(void);
        virtual void UnBind(void);
        virtual ShaderHandle GetShader(void) const;
    private:
        ThreadCameraStreamData* tcsData;
        ShaderHandle shader;
    };
}