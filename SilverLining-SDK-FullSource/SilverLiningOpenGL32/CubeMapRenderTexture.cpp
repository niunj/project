#include "CubeMapRenderTexture.h"
#include "CubeMapFboManager.h"
#include "OpenGLUtils.h"

namespace SilverLining
{
CubeMapRenderTexture::CubeMapRenderTexture(int w, int h, bool floatingPoint, bool generateMipMaps, const char* name, bool _avoidStalls)
    : fbo(0), fboCreatedIn(0), depth_rb(0)
    , savedFrameBuffer(0), savedRenderBuffer(0)
    , avoidStalls(_avoidStalls)
{
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFrameBuffer);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &savedRenderBuffer);

    //RGBA8 Cube map texture, 24 bit depth texture
    glGenTextures(1, &texture.TextureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture.TextureID);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);

    if (!generateMipMaps) {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
    } else {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texture.TextureID, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);

    //NULL means reserve texture memory, but texels are undefined
    if (floatingPoint) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 0, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 1, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 2, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 3, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 4, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 5, 0, GL_RGBA16F, w, h, 0, GL_BGRA, GL_FLOAT, NULL);
    } else {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 0, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 1, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 2, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 3, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 4, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + 5, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    }

    //-------------------------

    fboManager = CubeMapFboManager::instance();
    fboManager->AddRef();
    fbo = fboManager->CreateFbo(fboCreatedIn);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    //Attach one of the faces of the Cubemap texture to this FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, texture.TextureID, 0);
    //-------------------------
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    //-------------------------
    //Attach depth buffer to FBO
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

    glBindFramebuffer(GL_FRAMEBUFFER, savedFrameBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, savedRenderBuffer);

    texture.TextureID;
    texture.Width = w;
    texture.Height = h;
    texture.Bpp = 32;
    texture.Type = GL_RGBA;
}


CubeMapRenderTexture::~CubeMapRenderTexture()
{
    glDeleteRenderbuffers(1, &depth_rb);

    int savedFrameBuffer = 0;
    int savedRenderBuffer = 0;
    if (!avoidStalls) {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFrameBuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &savedRenderBuffer);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    fboManager->DestroyFbo(fbo, fboCreatedIn);
    glDeleteTextures(1, &texture.TextureID);

    if (!avoidStalls) {
        glBindFramebuffer(GL_FRAMEBUFFER, savedFrameBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, savedRenderBuffer);
    }

    fboManager->RemoveRef();
}

bool CubeMapRenderTexture::MakeCurrent(CubeFace face)
{
    if (!avoidStalls) {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &savedFrameBuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &savedRenderBuffer);
    }

    GLuint glFace;
    switch (face) {
    case POSX:
        glFace = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        break;

    case NEGX:
        glFace = GL_TEXTURE_CUBE_MAP_NEGATIVE_X;
        break;

    case POSY:
        glFace = GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
        break;

    case NEGY:
        glFace = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
        break;

    case POSZ:
        glFace = GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
        break;

    case NEGZ:
        glFace = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
        break;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, glFace, texture.TextureID, 0);

    OpenGLUtils::CheckError(__LINE__);

    return true;
}

bool CubeMapRenderTexture::Bind()
{
    if (!avoidStalls) {
        glBindFramebuffer(GL_FRAMEBUFFER, savedFrameBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, savedRenderBuffer);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    return true;
}

bool CubeMapRenderTexture::GetTextureHandle(SilverLining::TextureHandle* texHandle)
{
    *texHandle = (SilverLining::TextureHandle)(&texture);
    return true;
}
}