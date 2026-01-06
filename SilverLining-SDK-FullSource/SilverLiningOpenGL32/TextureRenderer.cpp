#include "TextureRenderer.h"
#include "OpenGLExtensionManager.h"
#include "Context.h"
#include "OpenGLStream.h"
#include "Camera.h"

#include <string.h>
#include <string>
#include <sstream>
#include <cstdlib>

#if defined LINUX
#include "GL/glx.h"
#define _stricmp strcasecmp
#define wglGetProcAddress(A) glXGetProcAddress((GLubyte *)A)
#elif defined(MAC)
#define _stricmp strcasecmp
#include <dlfcn.h>
static void * MyGLGetProcAddress (const char *name)
{
    static void *glHandle = NULL;
    void **handlePtr;
    void *addr = NULL;

    handlePtr = &glHandle;
    if (NULL == *handlePtr)
        *handlePtr = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_GLOBAL);
    if (NULL != *handlePtr)
        addr = dlsym(*handlePtr, name);

    return addr;
}
#define wglGetProcAddress(A) MyGLGetProcAddress(A)
#endif



namespace SilverLining
{
bool TextureRendererFactory::hasFBO = false;
bool TextureRendererFactory::initialized = false;


static void CheckError(int line)
{
#ifdef _DEBUG
    GLenum errCode = GL_NO_ERROR;
    do {
        errCode = glGetError();
        if (errCode != GL_NO_ERROR) {
            printf("GL Error in TextureRenderer at line %d: %s\n", line, gluErrorString(errCode));
        }
    } while (errCode != GL_NO_ERROR);
#endif
}

TextureRenderer::TextureRenderer()
    : context(0)
{

}
TextureRenderer::~TextureRenderer()
{
    // nothing else to do
}

TextureRenderer* TextureRendererFactory::MakeTextureRenderer()
{
    TextureRenderer* textureRenderer = NULL;

    if (!initialized) {
        hasFBO = SetupFramebufferObjects();
        initialized = true;
    }

    char *fboStr = getenv("SILVERLINING_DISABLE_FBO");

    bool noFBO = fboStr ? _stricmp(fboStr, "true") == 0 : false;

    if (hasFBO && !noFBO) {
        textureRenderer = SL_NEW TextureRendererFBO();
    }

    return textureRenderer;
}

bool TextureRendererFactory::SetupFramebufferObjects()
{
    if (OpenGLExtensionManager::ExtensionSupported("GL_EXT_framebuffer_object")) {
        glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
        glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
        glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
        glGenerateMipmapEXT = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
        glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
        glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
        glBlendEquationEXT = (PFNGLBLENDEQUATIONPROC)wglGetProcAddress("glBlendEquation");
        if (glGenFramebuffersEXT && glBindFramebufferEXT && glFramebufferTexture2DEXT
                && glGenerateMipmapEXT && glDeleteFramebuffersEXT) {
            return true;
        }
    }

    return false;
}

TextureRendererFBO::TextureRendererFBO()
{
    attachment = GL_COLOR_ATTACHMENT0_EXT;

    fbo = savedFbo = 0;
}

TextureRendererFBO::~TextureRendererFBO()
{
    glDeleteTextures(1, &render_texture.TextureID);
}

bool TextureRendererFBO::Initialize(int w, int h, Context* _context, const char* name)
{
    context = _context;
    FramebufferManager::GetInstance()->GetFramebuffer(w, h, context, fbo, attachment);

    glGenTextures(1, &render_texture.TextureID);

    glBindTexture(GL_TEXTURE_2D, render_texture.TextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#ifdef MIPMAP
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CheckError(__LINE__);

    render_texture.Width = w;
    render_texture.Height = h;
    render_texture.Bpp = 32;
    render_texture.Type = GL_RGBA;
#ifdef MIPMAP
    glGenerateMipmapEXT(GL_TEXTURE_2D);
#endif
    GLint incomingFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &incomingFbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D,
                              render_texture.TextureID, 0);
    CheckError(__LINE__);

    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebufferEXT(GL_FRAMEBUFFER, incomingFbo);
        CheckError(__LINE__);
        if (name && glObjectLabel) {
            std::stringstream ssfbo;
            ssfbo << std::string(name) << "_frame_buffer";
            glObjectLabel(GL_FRAMEBUFFER, fbo, -1, ssfbo.str().c_str());

            std::stringstream ssRenderTargetTexture;
            ssRenderTargetTexture << std::string(name) << "_rtt_texture";
            glObjectLabel(GL_TEXTURE, render_texture.TextureID, -1, ssRenderTargetTexture.str().c_str());
        }
        return true;
    }

    glBindFramebufferEXT(GL_FRAMEBUFFER, incomingFbo);
    CheckError(__LINE__);
    return false;
}

bool TextureRendererFBO::MakeCurrent(bool clear)
{
    OpenGlStream* stream = context->GetStream();
    stream->glBindTexture(GL_TEXTURE_2D, 0);

    GLint iFbo = 0;
    if (context->avoidStalls == false) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &iFbo);
        savedFbo = iFbo;
    }

    stream->glBindFramebufferEXT(GL_FRAMEBUFFER, fbo);
    stream->glDrawBuffer(attachment);

    if (context->avoidStalls == false) {
        glGetIntegerv(GL_VIEWPORT, savedViewport);
    }

    stream->glViewport(0, 0, render_texture.Width, render_texture.Height);

    if (clear) {
        stream->glClearColor(0, 0, 0, 0);
        stream->glClear(GL_COLOR_BUFFER_BIT);
    }

    stream->checkGlError(__LINE__);

    return true;
}

bool TextureRendererFBO::BindToTexture(void* _camera)
{
    OpenGlStream* stream = context->GetStream();

    if (context->avoidStalls == false) {
        stream->glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
        stream->glBindFramebufferEXT(GL_FRAMEBUFFER, savedFbo);
    } else {
        if (_camera) {
            const Camera* camera = (Camera*)(_camera);
            int x, y, width, height;
            camera->GetViewPortFast(x, y, width, height);
            stream->glViewport(x, y, width, height);

            int pFBO = camera->GetRenderTarget();
            stream->glBindFramebufferEXT(GL_FRAMEBUFFER, pFBO);
        }
    }

    stream->glBindTexture(GL_TEXTURE_2D, render_texture.TextureID);
#ifdef MIPMAP
    stream->glGenerateMipmapEXT(GL_TEXTURE_2D);
#endif
    stream->checkGlError(__LINE__);
    return true;
}

TextureHandle TextureRendererFBO::GetTextureHandle()
{
    return (TextureHandle)&render_texture;
}

FrameBuffer::FrameBuffer(int pw, int ph, Context* _context) : w(pw), h(ph), context(_context), nAttachments(0)
{
    glGenFramebuffersEXT(1, &fbo);

    CheckError(__LINE__);
}

FrameBuffer::~FrameBuffer()
{
    //glDeleteFramebuffersEXT(1, &fbo);
    //CheckError(__LINE__);
}

FramebufferManager *FramebufferManager::GetInstance()
{
    static FramebufferManager instance;
    return &instance;
}

FramebufferManager::FramebufferManager()
{
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttachments);

    CheckError(__LINE__);
}

FramebufferManager::~FramebufferManager()
{
    SL_VECTOR(FrameBuffer *) ::iterator it;
    for (it = framebuffers.begin(); it != framebuffers.end(); it++) {
        FrameBuffer *fb = *it;
        SL_DELETE fb;
    }

    framebuffers.clear();
}

bool FramebufferManager::GetFramebuffer(int w, int h, Context* _context, GLuint& fbo, GLenum& attachment)
{
    // Search for existing framebuffer
    FrameBuffer *fb = 0;
    SL_VECTOR(FrameBuffer *) ::iterator it;
    for (it = framebuffers.begin(); it != framebuffers.end(); it++) {
        if ((*it)->w == w && (*it)->h == h && (*it)->context == _context) {
            if ((*it)->nAttachments < maxAttachments) {
                fb = *it;
            }
        }
    }

    // Create one if necessary.
    if (!fb) {
        fb = SL_NEW FrameBuffer(w, h, _context);
        framebuffers.push_back(fb);
    }

    attachment = GL_COLOR_ATTACHMENT0 + fb->nAttachments;
    fbo = fb->fbo;
    fb->nAttachments++;

    CheckError(__LINE__);
    return true;
}
}