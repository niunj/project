// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#pragma once
#include "MemAlloc.h"
#include "SilverLiningOpenGLPreamble.h"

namespace SilverLining
{
    class VertexAttribLocations
    {
    public:
        VertexAttribLocations(int _vertexLocation, int _colorLocation, int _texcoordLocation);

        virtual ~VertexAttribLocations();

        virtual void InitFromProgram(unsigned int program);

        virtual void SetUpVertexAttribPointers(void) const;

        virtual void EnableVertexAttribArrays(void) const;
    public:
        GLint vertexLocation;
        GLint colorLocation;
        GLint texcoordLocation;
    private:
        VertexAttribLocations();
    private:
    };
}