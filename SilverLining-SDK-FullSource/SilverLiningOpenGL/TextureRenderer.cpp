#include "TextureRenderer.h"
#include "OpenGLExtensionManager.h"
#include "SilverLiningOpenGLPreamble.h"
#include "OpenGLStream.h"
#include "Context.h"
#include "Camera.h"

#include <string.h>

using namespace SilverLining;
using namespace std;

//#define MIPMAP

#if defined(WIN32) || defined(WIN64)
static PFNWGLCREATEPBUFFERARBPROC wglCreatePbufferARB = 0;
static PFNWGLRELEASEPBUFFERDCARBPROC wglReleasePbufferDCARB = 0;
static PFNWGLDESTROYPBUFFERARBPROC wglDestroyPbufferARB = 0;
static PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = 0;
static PFNWGLGETPBUFFERDCARBPROC wglGetPbufferDCARB = 0;
static PFNWGLBINDTEXIMAGEARBPROC wglBindTexImageARB = 0;
static PFNWGLRELEASETEXIMAGEARBPROC wglReleaseTexImageARB = 0;
#elif defined LINUX
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
#else
#error "Target platform not defined"
#endif



bool TextureRendererFactory::hasFBO = false;
bool TextureRendererFactory::hasPbuffers = false;
bool TextureRendererFactory::initialized = false;

TextureRenderer *TextureRendererFactory::MakeTextureRenderer()
{
    TextureRenderer *ren = NULL;

    if (!initialized) {
        hasFBO = SetupFramebufferObjects();
        hasPbuffers = SetupPbuffer();
        initialized = true;
    }

    char *fboStr = getenv("SILVERLINING_DISABLE_FBO");
    char *pbufferStr = getenv("SILVERLINING_DISABLE_PBUFFERS");

    bool noFBO = fboStr ? _stricmp(fboStr, "true") == 0 : false;
    bool noPbuffer = pbufferStr ? _stricmp(pbufferStr, "true") == 0 : true;

    if (hasFBO && !noFBO) {
        ren = SL_NEW TextureRendererFBO();
    } else if (hasPbuffers && !noPbuffer) {
        ren = SL_NEW TextureRendererPbuffer();
    }

    return ren;
}

bool TextureRendererFactory::SetupFramebufferObjects()
{
    if (OpenGLExtensionManager::ExtensionSupported("GL_EXT_framebuffer_object")) {
        glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)wglGetProcAddress("glGenFramebuffersEXT");
        glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)wglGetProcAddress("glBindFramebufferEXT");
        glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)wglGetProcAddress("glFramebufferTexture2DEXT");
        glGenerateMipmapEXT = (PFNGLGENERATEMIPMAPEXTPROC)wglGetProcAddress("glGenerateMipmapEXT");
        glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSEXTPROC)wglGetProcAddress("glDeleteFramebuffersEXT");
        glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)wglGetProcAddress("glCheckFramebufferStatusEXT");
        glBlendEquationEXT = (PFNGLBLENDEQUATIONEXTPROC)wglGetProcAddress("glBlendEquationEXT");
        if (glGenFramebuffersEXT && glBindFramebufferEXT && glFramebufferTexture2DEXT
                && glGenerateMipmapEXT && glDeleteFramebuffersEXT) {
            return true;
        }
    }

    return false;
}

bool TextureRendererFactory::SetupPbuffer()
{
#if defined(WIN32) || defined(WIN64)
    PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = 0;
    wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
    if (wglGetExtensionsStringARB) {
        const char *winsys_extensions = wglGetExtensionsStringARB(wglGetCurrentDC());
        if (strstr(winsys_extensions, "WGL_ARB_render_texture")) {
            wglBindTexImageARB = (PFNWGLBINDTEXIMAGEARBPROC)wglGetProcAddress("wglBindTexImageARB");
            wglReleaseTexImageARB = (PFNWGLRELEASETEXIMAGEARBPROC)wglGetProcAddress("wglReleaseTexImageARB");
            wglCreatePbufferARB = (PFNWGLCREATEPBUFFERARBPROC)wglGetProcAddress("wglCreatePbufferARB");
            wglReleasePbufferDCARB = (PFNWGLRELEASEPBUFFERDCARBPROC)wglGetProcAddress("wglReleasePbufferDCARB");
            wglDestroyPbufferARB = (PFNWGLDESTROYPBUFFERARBPROC)wglGetProcAddress("wglDestroyPbufferARB");
            wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
            wglGetPbufferDCARB = (PFNWGLGETPBUFFERDCARBPROC)wglGetProcAddress("wglGetPbufferDCARB");
            return true;
        }
    }
#endif
    return false;
}


TextureRendererPbuffer::TextureRendererPbuffer() : bound(false), cleared(false)
{

}

TextureRendererPbuffer::~TextureRendererPbuffer()
{
#if defined(WIN32) || defined(WIN64)
    wglDeleteContext(context);
    wglReleasePbufferDCARB(buffer, dc);
    wglDestroyPbufferARB(buffer);

    glDeleteTextures(1, &render_texture.TextureID);
#endif
}

bool TextureRendererPbuffer::Initialize(int w, int h, Context* _context)
{
    TextureRenderer::context = _context;
#if defined(WIN32) || defined(WIN64)
    // Create a render texture object
    glGenTextures( 1, &render_texture.TextureID );
    glBindTexture( GL_TEXTURE_2D, render_texture.TextureID );
#ifdef MIPMAP
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
#else
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#endif
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
#ifdef MIPMAP
    glTexParameteri( GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
#endif

    render_texture.Width = w;
    render_texture.Height = h;
    render_texture.Bpp = 32;
    render_texture.Type = GL_RGBA;

    HDC hdc = wglGetCurrentDC();

    HGLRC hglrc = wglGetCurrentContext();

    int iattributes[] = {WGL_DRAW_TO_PBUFFER_ARB, true, WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
                         WGL_SUPPORT_OPENGL_ARB, true, WGL_BIND_TO_TEXTURE_RGBA_ARB, true, WGL_DEPTH_BITS_ARB, 0,
                         0, 0
                        };

    int format;
    int pformat[256];
    unsigned int nformats;

    wglChoosePixelFormatARB(hdc, iattributes, NULL, 256, pformat, &nformats);
    format = pformat[0];

    iattributes[0] = WGL_TEXTURE_FORMAT_ARB;
    iattributes[1] = WGL_TEXTURE_RGBA_ARB;
    iattributes[2] = WGL_TEXTURE_TARGET_ARB;
    iattributes[3] = WGL_TEXTURE_2D_ARB;
    iattributes[4] = WGL_PBUFFER_LARGEST_ARB;
    iattributes[5] = 0;
    iattributes[6] = WGL_MIPMAP_TEXTURE_ARB;
#ifdef MIPMAP
    iattributes[7] = 1;
#else
    iattributes[7] = 0;
#endif
    iattributes[8] = iattributes[9] = 0;

    buffer = wglCreatePbufferARB(hdc, format, w, h, iattributes);

    if (!buffer) {
        return false;
    }

    dc = wglGetPbufferDCARB(buffer);

    context = wglCreateContext(dc);

    // This is key for being able to transfer the texture between contexts
    wglShareLists(hglrc, context);

    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
#else
    return false;
#endif
}

bool TextureRendererPbuffer::MakeCurrent(bool clear)
{
#if defined(WIN32) || defined(WIN64)
    oldDC = wglGetCurrentDC();
    oldContext = wglGetCurrentContext();

    if (bound) {
        wglReleaseTexImageARB(buffer, WGL_FRONT_LEFT_ARB);
        bound = false;
    }

    wglMakeCurrent(dc, context);
    //wglMakeCurrent(dc, oldContext);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    if (clear) {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    return true;
#else
    return false;
#endif
}

bool TextureRendererPbuffer::BindToTexture(void* camera)
{
#if defined(WIN32) || defined(WIN64)
    wglMakeCurrent(oldDC, oldContext);

    glBindTexture(GL_TEXTURE_2D, render_texture.TextureID);

    wglBindTexImageARB(buffer, WGL_FRONT_LEFT_ARB);

    bound = true;

    return true;
#else
    return false;
#endif
}

TextureHandle TextureRendererPbuffer::GetTextureHandle()
{
    return (TextureHandle)(&render_texture);
}

TextureRendererFBO::TextureRendererFBO()
{
    attachment = GL_COLOR_ATTACHMENT0_EXT;
    fbo = savedFbo = 0;
}

TextureRendererFBO::~TextureRendererFBO()
{
    glDeleteTextures(1, &render_texture.TextureID);
    //glDeleteFramebuffersEXT(1, &fbo);
}

bool TextureRendererFBO::Initialize(int w, int h, Context* _context)
{
    context = _context;
    //TODO: Recycle fbo's for multiple textures
    //glGenFramebuffersEXT(1, &fbo);

    GLint iFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &iFbo);
    GLuint savedFbo = iFbo;

    FramebufferManager::GetInstance()->GetFramebuffer(w, h, context, fbo, attachment);

    glGenTextures( 1, &render_texture.TextureID );

    glBindTexture(GL_TEXTURE_2D, render_texture.TextureID );
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
#ifdef MIPMAP
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
#else
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#endif
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

    render_texture.Width = w;
    render_texture.Height = h;
    render_texture.Bpp = 32;
    render_texture.Type = GL_RGBA;
#ifdef MIPMAP
    glGenerateMipmapEXT(GL_TEXTURE_2D);
#endif
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment, GL_TEXTURE_2D,
                              render_texture.TextureID, 0);

    if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT) {
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, savedFbo);
        return true;
    }

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, savedFbo);

    glBindTexture(GL_TEXTURE_2D, 0);

    return false;
}

bool TextureRendererFBO::MakeCurrent(bool clear)
{
    OpenGlStream* stream = context->GetStream();
    stream->glBindTexture(GL_TEXTURE_2D, 0);
    GLint iFbo = 0;
    if (context->avoidStalls == false) {
        stream->glPushAttrib(GL_VIEWPORT_BIT);

        glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &iFbo);
        savedFbo = iFbo;
    }

    stream->glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
    stream->glDrawBuffer(attachment);

    stream->glViewport(0, 0, render_texture.Width, render_texture.Height);

    if (clear) {
        stream->glClearColor(0, 0, 0, 0);
        stream->glClear(GL_COLOR_BUFFER_BIT);
    }

    return true;
}

bool TextureRendererFBO::BindToTexture(void* _camera)
{
    OpenGlStream* stream = context->GetStream();
    if (context->avoidStalls == false) {
        stream->glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, savedFbo);
        stream->glPopAttrib();
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
    return true;
}

TextureHandle TextureRendererFBO::GetTextureHandle()
{
    return (TextureHandle)&render_texture;
}

FrameBuffer::FrameBuffer(int pw, int ph, Context* _context) : w(pw), h(ph), context(_context), nAttachments(0)
{
    glGenFramebuffersEXT(1, &fbo);
}

FrameBuffer::~FrameBuffer()
{
    glDeleteFramebuffersEXT(1, &fbo);
}

FramebufferManager *FramebufferManager::GetInstance()
{
    static FramebufferManager instance;
    return &instance;
}

FramebufferManager::FramebufferManager()
{
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &maxAttachments);
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
            if ( (*it)->nAttachments < maxAttachments ) {
                fb = *it;
                //printf("Reusing framebuffer %d for %d x %d context %d\n", (*it)->fbo, w, h, context);
            }
        }
    }

    // Create one if necessary.
    if (!fb) {
        fb = SL_NEW FrameBuffer(w, h, _context);
        //printf("Created new FBO %d for %d x %d context %d\n", fb->fbo, w, h, context);
        framebuffers.push_back(fb);
    }

    attachment = GL_COLOR_ATTACHMENT0_EXT + fb->nAttachments;
    fbo = fb->fbo;
    fb->nAttachments++;

    return true;
}
