// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"
#include "TextureLoader.h"

#include <vector>

namespace SilverLining
{
    class Context;

    class TextureRenderer
    {
    public:
        TextureRenderer();
        virtual ~TextureRenderer();
    public:
        virtual bool Initialize(int w, int h, Context* _context, const char* name) = 0;
        virtual bool MakeCurrent(bool clear) = 0;
        virtual bool BindToTexture(void* camera) = 0;
        virtual TextureHandle GetTextureHandle() = 0;
    protected:
        Context* context;
    };

    class TextureRendererFactory
    {
    public:
        static TextureRenderer *MakeTextureRenderer();

    private:
        static bool SetupFramebufferObjects();

        static bool initialized;
        static bool hasFBO;
    };

    class FrameBuffer
    {
    public:
        FrameBuffer(int w, int h, Context* _context);
        virtual ~FrameBuffer();

        int w, h, nAttachments;
        GLuint fbo;
        Context* context;
    };

    class FramebufferManager
    {
    public:
        static FramebufferManager *GetInstance();
        virtual ~FramebufferManager();

        bool GetFramebuffer(int w, int h, Context* _context, GLuint& fbo, GLenum& attachment);

    private:
        FramebufferManager();
        SL_VECTOR(FrameBuffer*) framebuffers;
        GLint maxAttachments;
    };

    class TextureRendererFBO : public TextureRenderer
    {
    public:
        TextureRendererFBO();
        virtual ~TextureRendererFBO();

        bool Initialize(int w, int h, Context* _context, const char* name);
        bool MakeCurrent(bool clear);
        bool BindToTexture(void* camera);
        TextureHandle GetTextureHandle();

    private:
        GLuint fbo, savedFbo;
        GLenum attachment;
        glTexture render_texture;
        int savedViewport[4];
    };
}