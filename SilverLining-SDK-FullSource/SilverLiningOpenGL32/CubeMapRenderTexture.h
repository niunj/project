// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"

#include "TextureLoader.h"

namespace SilverLining
{
    class CubeMapFboManager;
    class CubeMapRenderTexture : public MemObject
    {
    public:
        //! constructor
        CubeMapRenderTexture(int w, int h, bool floatingPoint, bool generateMipMaps, const char* name, bool avoidStalls);

        //! destructor
        ~CubeMapRenderTexture();

        //! make current
        bool MakeCurrent(CubeFace face);

        //! bind
        bool Bind();

        //! get texture handle
        bool GetTextureHandle(SilverLining::TextureHandle* texHandle);

    protected:
        const bool avoidStalls;
        GLuint fbo, depth_rb;
        void* fboCreatedIn;
        glTexture texture;
        GLint savedFrameBuffer, savedRenderBuffer;

        CubeMapFboManager* fboManager;
    private:
        CubeMapRenderTexture();
    };
}