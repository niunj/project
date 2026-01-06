// Copyright (c) 2011-2021 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"

#include "TextureLoader.h"

namespace SilverLining
{
    class CubeMapRenderTexture : public MemObject
    {
    public:
        //! constructor
        CubeMapRenderTexture(int w, int h, bool floatingPoint, bool generateMipMaps, const char* name, bool avoidStalls);

        //! destructor
        ~CubeMapRenderTexture();

    public:
        //! make current
        bool MakeCurrent(CubeFace face);

        //! bind
        bool Bind();

    public:
        GLuint buffer, depth_rb;
        glTexture texture;
        GLint savedFrameBuffer, savedRenderBuffer;

        bool avoidStalls;
    };

}