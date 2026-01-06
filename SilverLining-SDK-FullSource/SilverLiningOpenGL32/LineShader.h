// Copyright (c) 2004-2020  Sundog Software, LLC All rights reserved worldwide.

/**
\file LineShader.h
\brief Classes that declare a container for the SilverLining OpenGL32 line shader
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "LineShader.h"

namespace SilverLining
{
    struct LineShader {
        LineShader();

        ShaderHandle sh;
        int        colorUniformLocation;
        int        modelViewProjUniformLocation;
        int        vertexLocation;
        bool         initialized;        
    };
}