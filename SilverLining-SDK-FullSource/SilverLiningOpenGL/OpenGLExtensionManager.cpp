#include "OpenGLExtensionManager.h"
#include "SilverLiningOpenGLPreamble.h"
#include "OpenGLUtils.h"

#include <iostream>
#include <cstring>
#include <cstdlib>

namespace SilverLining
{
bool OpenGLExtensionManager::usingVB = false;
bool OpenGLExtensionManager::usingPB = false;
bool OpenGLExtensionManager::usingFramebuffers = false;
bool OpenGLExtensionManager::usingNVPointSprite = false;
bool OpenGLExtensionManager::usingFloatBuffers = false;
bool OpenGLExtensionManager::usingBindlessGraphics = false;
bool OpenGLExtensionManager::usingOcclusionQuery = false;
bool OpenGLExtensionManager::usingGLSL = false;
bool OpenGLExtensionManager::usingMultisample = false;
bool OpenGLExtensionManager::usingDepthRangedNV = false;

bool OpenGLExtensionManager::isATI = false;
bool OpenGLExtensionManager::hasBindlessGraphics = false;

int OpenGLExtensionManager::ExtensionSupported(const char *extension)
{
    const GLubyte *extensions = NULL;
    const GLubyte *start;
    GLubyte *where, *terminator;

    if (!extensions) {
        extensions = glGetString(GL_EXTENSIONS);
        if (!extensions) return 0;
    }
    start = extensions;
    for (;;) {
        where = (GLubyte *)strstr((const char *)start, extension);
        if (!where)
            break;
        terminator = where + strlen(extension);
        if (where == start || *(where - 1) == ' ') {
            if (*terminator == ' ' || *terminator == '\0') {
                return 1;
            }
        }
        start = terminator;
    }
    return 0;
}

bool OpenGLExtensionManager::LoadGLExtensions(void)
{
    // Load extensions.
#if defined(WIN32) || defined(WIN64)
    {
        HGLRC glContext = wglGetCurrentContext();
        if (glContext == NULL) return 0;
    }
#endif

#ifndef NATIVE_1_3
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
    if (!glActiveTexture) return 0;

    glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREARBPROC)wglGetProcAddress("glClientActiveTextureARB");
#endif

#ifndef NATIVE_1_2
    glTexImage3D = (PFNGLTEXIMAGE3DPROC)wglGetProcAddress("glTexImage3D");
    glTexSubImage3D = (PFNGLTEXSUBIMAGE3DPROC)wglGetProcAddress("glTexSubImage3D");
#endif

    const GLubyte *str;
    str = glGetString(GL_VENDOR);
    isATI = strstr((const char *)str, "ATI") != 0;
    str = glGetString(GL_VERSION);
    //float vernum = (float)atof((const char *)str);


#if (_MSC_VER <= 1310 || (!defined(WIN32) && !defined(WIN64)))
    bool VBOSupportQuestionable = (getenv("SILVERLINING_NO_VBO") != NULL);
#else
    char *buf = 0;
    size_t bufSize = 0;
    bool VBOSupportQuestionable = false;
    if (_dupenv_s(&buf, &bufSize, "SILVERLINING_NO_VBO") == 0) {
        if (buf != NULL) {
            VBOSupportQuestionable = true;
            free(buf);
        }
    }
#endif

    //VBOSupportQuestionable = true;

    if (ExtensionSupported("GL_NV_depth_buffer_float")) {
        glDepthRangedNV = (PFNGLDEPTHRANGEDNVPROC)wglGetProcAddress("glDepthRangedNV");
        if (glDepthRangedNV) {
            usingDepthRangedNV = true;
        }
    }

    if (ExtensionSupported("GL_ARB_vertex_buffer_object") && !VBOSupportQuestionable) {
        usingVB = true;

        if (ExtensionSupported("GL_ARB_pixel_buffer_object")) {
            usingPB = true;
        }

        glGenBuffersARB = (PFNGLGENBUFFERSARBPROC)wglGetProcAddress("glGenBuffersARB");
        glBindBufferARB = (PFNGLBINDBUFFERARBPROC)wglGetProcAddress("glBindBufferARB");
        glBufferDataARB = (PFNGLBUFFERDATAARBPROC)wglGetProcAddress("glBufferDataARB");
        glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC)wglGetProcAddress("glBufferSubDataARB");
        glGetBufferSubDataARB = (PFNGLGETBUFFERSUBDATAARBPROC)wglGetProcAddress("glGetBufferSubDataARB");
        glMapBufferARB = (PFNGLMAPBUFFERARBPROC)wglGetProcAddress("glMapBufferARB");
        glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC)wglGetProcAddress("glUnmapBufferARB");
        glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC)wglGetProcAddress("glDeleteBuffersARB");
        glIsBufferARB = (PFNGLISBUFFERARBPROC)wglGetProcAddress("glIsBufferARB");
        if (!(glGenBuffersARB && glBindBufferARB && glBufferDataARB && glMapBufferARB
                && glUnmapBufferARB && glDeleteBuffersARB && glBufferSubDataARB
                && glIsBufferARB)) {
            usingVB = usingPB = false;
        }
    }

    if (ExtensionSupported("GL_EXT_framebuffer_object")) {
        usingFramebuffers = true;

        glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
        glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
        glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
        glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
        glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
        glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
        glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
        glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
        glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
        glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
        glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");

        if (!(glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D
                && glGenerateMipmap && glDeleteFramebuffers && glGenRenderbuffers
                && glRenderbufferStorage && glFramebufferRenderbuffer && glDeleteRenderbuffers)) {
            usingFramebuffers = false;
        }
    }

    bool disableBindlessGraphics = true;
    /*
    #if (_MSC_VER <= 1310 || (!defined(WIN32) && !defined(WIN64)))
    bool disableBindlessGraphics = (getenv("SILVERLINING_NO_BINDLESS") != NULL);
    #else
    bool disableBindlessGraphics = false;
    if (_dupenv_s(&buf, &bufSize, "SILVERLINING_NO_BINDLESS") == 0) {
        if (buf != NULL) {
            disableBindlessGraphics = true;
            free(buf);
        }
    }
    #endif
    */
#if (_MSC_VER <= 1310 || (!defined(WIN32) && !defined(WIN64)))
    OpenGLUtils::enableDebugOutput = (getenv("SILVERLINING_ENABLE_DEBUG") != NULL);
#else
    OpenGLUtils::enableDebugOutput = false;
    if (_dupenv_s(&buf, &bufSize, "SILVERLINING_ENABLE_DEBUG") == 0) {
        if (buf != NULL) {
            OpenGLUtils::enableDebugOutput = true;
            free(buf);
        }
    }
#endif

    if (OpenGLUtils::enableDebugOutput) {
        std::cout << "SilverLining OpenGL Debug Output Enabled." << std::endl;
    }

    if (ExtensionSupported("GL_ARB_multisample")) {
        usingMultisample = true;
    }

    if (ExtensionSupported("GL_NV_vertex_buffer_unified_memory") &&
            ExtensionSupported("GL_NV_shader_buffer_load")) {
        hasBindlessGraphics = true;
    }

    if (hasBindlessGraphics && usingVB && !disableBindlessGraphics) {
        usingBindlessGraphics = true;
    }

    glBufferAddressRangeNV = (PFNGLBUFFERADDRESSRANGENVPROC)wglGetProcAddress("glBufferAddressRangeNV");
    glVertexFormatNV = (PFNGLVERTEXFORMATNVPROC)wglGetProcAddress("glVertexFormatNV");
    glColorFormatNV = (PFNGLCOLORFORMATNVPROC)wglGetProcAddress("glColorFormatNV");
    glTexCoordFormatNV = (PFNGLTEXCOORDFORMATNVPROC)wglGetProcAddress("glTexCoordFormatNV");
    if (!(glBufferAddressRangeNV && glVertexFormatNV && glColorFormatNV && glTexCoordFormatNV)) {
        usingBindlessGraphics = false;
    }

    glGetBufferParameterui64vNV = (PFNGLGETBUFFERPARAMETERUI64VNVPROC)wglGetProcAddress("glGetBufferParameterui64vNV");

    glIsBufferResidentNV = (PFNGLISBUFFERRESIDENTNVPROC)wglGetProcAddress("glIsBufferResidentNV");
    glMakeBufferResidentNV = (PFNGLMAKEBUFFERRESIDENTNVPROC)wglGetProcAddress("glMakeBufferResidentNV");
    glMakeBufferNonResidentNV = (PFNGLMAKEBUFFERNONRESIDENTNVPROC)wglGetProcAddress("glMakeBufferNonResidentNV");

    if (!(glGetBufferParameterui64vNV && glMakeBufferResidentNV && glMakeBufferNonResidentNV)) {
        usingBindlessGraphics = false;
    }

    glIsNamedBufferResidentNV = (PFNGLISNAMEDBUFFERRESIDENTNVPROC)wglGetProcAddress("glIsNamedBufferResidentNV");
    glMakeNamedBufferResidentNV = (PFNGLMAKENAMEDBUFFERRESIDENTNVPROC)wglGetProcAddress("glMakeNamedBufferResidentNV");
    glMakeNamedBufferNonResidentNV = (PFNGLMAKENAMEDBUFFERNONRESIDENTNVPROC)wglGetProcAddress("glMakeNamedBufferNonResidentNV");

    glGetNamedBufferParameterui64vNV = (PFNGLGETNAMEDBUFFERPARAMETERUI64VNVPROC)wglGetProcAddress("glGetNamedBufferParameterui64vNV");

    glVertexAttribFormatNV = (PFNGLVERTEXATTRIBFORMATNVPROC)wglGetProcAddress("glVertexAttribFormatNV");
#ifndef NATIVE_4_3
    glBindVertexBuffer = (PFNGLBINDVERTEXBUFFERPROC)wglGetProcAddress("glBindVertexBuffer");
    glVertexAttribFormat = (PFNGLVERTEXATTRIBFORMATPROC)wglGetProcAddress("glVertexAttribFormat");
    glVertexAttribBinding = (PFNGLVERTEXATTRIBBINDINGPROC)wglGetProcAddress("glVertexAttribBinding");
#endif
    glMultiDrawElementsIndirectBindlessNV = (PFNGLMULTIDRAWELEMENTSINDIRECTBINDLESSNVPROC)wglGetProcAddress("glMultiDrawElementsIndirectBindlessNV");
#ifndef NATIVE_2_0
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glDisableVertexAttribArray");
#endif

    glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)wglGetProcAddress("glGetTextureHandleARB");
    glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleResidentARB");
    glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleNonResidentARB");
    glIsTextureHandleResidentARB = (PFNGLISTEXTUREHANDLERESIDENTARBPROC)wglGetProcAddress("glIsTextureHandleResidentARB");


    if (ExtensionSupported("GL_NV_point_sprite")) {
        usingNVPointSprite = true;

        glPointParameteriNV = (PFNGLPOINTPARAMETERINVPROC)wglGetProcAddress("glPointParameteriNV");
    }

    if (ExtensionSupported("GL_ATI_texture_float")) {
        usingFloatBuffers = true;
    }

    if (ExtensionSupported("GL_ARB_occlusion_query")) {
        glGenQueriesARB = (PFNGLGENQUERIESARBPROC)wglGetProcAddress("glGenQueriesARB");
        glDeleteQueriesARB = (PFNGLDELETEQUERIESARBPROC)wglGetProcAddress("glDeleteQueriesARB");
        glBeginQueryARB = (PFNGLBEGINQUERYARBPROC)wglGetProcAddress("glBeginQueryARB");
        glEndQueryARB = (PFNGLENDQUERYARBPROC)wglGetProcAddress("glEndQueryARB");
        glGetQueryObjectuivARB = (PFNGLGETQUERYOBJECTUIVARBPROC)wglGetProcAddress("glGetQueryObjectuivARB");

        usingOcclusionQuery = glGenQueriesARB && glDeleteQueriesARB && glBeginQueryARB && glEndQueryARB &&
                              glGetQueryObjectuivARB;
    }

    if (ExtensionSupported("GL_ARB_color_buffer_float")) {
        glClampColorARB = (PFNGLCLAMPCOLORARBPROC)wglGetProcAddress("glClampColorARB");
    }

#ifndef NATIVE_1_4
    glBlendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)wglGetProcAddress("glBlendFuncSeparate");
#endif

#ifndef NATIVE_2_0
    glBlendEquationSeparate = (PFNGLBLENDEQUATIONSEPARATEPROC)wglGetProcAddress("glBlendEquationSeparate");
#endif

#ifndef NATIVE_3_0
    glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)wglGetProcAddress("glBindBufferBase");
#endif

    bool disableGLSL = false;
    usingGLSL = false;
#if (_MSC_VER <= 1310 || (!defined(WIN32) && !defined(WIN64)))
    disableGLSL = (getenv("SILVERLINING_NO_GLSL") != NULL);
#else
    buf = 0;
    bufSize = 0;
    if (_dupenv_s(&buf, &bufSize, "SILVERLINING_NO_GLSL") == 0) {
        if (buf != NULL) {
            disableGLSL = true;
            usingGLSL = false;
            free(buf);
        }
    }
#endif

#ifndef NATIVE_3_1
    glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)wglGetProcAddress("glDrawArraysInstanced");
    glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)wglGetProcAddress("glDrawElementsInstanced");
#endif

    if (!disableGLSL && ExtensionSupported("GL_ARB_fragment_shader") && ExtensionSupported("GL_ARB_vertex_shader") && ExtensionSupported("GL_ARB_shader_objects")) {
        glCreateShaderObjectARB = (PFNGLCREATESHADEROBJECTARBPROC)wglGetProcAddress("glCreateShaderObjectARB");
        glShaderSourceARB = (PFNGLSHADERSOURCEARBPROC)wglGetProcAddress("glShaderSourceARB");
        glCompileShaderARB = (PFNGLCOMPILESHADERARBPROC)wglGetProcAddress("glCompileShaderARB");
        glCreateProgramObjectARB = (PFNGLCREATEPROGRAMOBJECTARBPROC)wglGetProcAddress("glCreateProgramObjectARB");
        glAttachObjectARB = (PFNGLATTACHOBJECTARBPROC)wglGetProcAddress("glAttachObjectARB");
        glLinkProgramARB = (PFNGLLINKPROGRAMARBPROC)wglGetProcAddress("glLinkProgramARB");
        glUseProgramObjectARB = (PFNGLUSEPROGRAMOBJECTARBPROC)wglGetProcAddress("glUseProgramObjectARB");
        glDetachObjectARB = (PFNGLDETACHOBJECTARBPROC)wglGetProcAddress("glDetachObjectARB");
        glDeleteObjectARB = (PFNGLDELETEOBJECTARBPROC)wglGetProcAddress("glDeleteObjectARB");
        glGetUniformLocationARB = (PFNGLGETUNIFORMLOCATIONARBPROC)wglGetProcAddress("glGetUniformLocationARB");
        glUniform4fARB = (PFNGLUNIFORM4FARBPROC)wglGetProcAddress("glUniform4fARB");
        glUniformMatrix4fvARB = (PFNGLUNIFORMMATRIX4FVARBPROC)wglGetProcAddress("glUniformMatrix4fvARB");
        glGetObjectParameterivARB = (PFNGLGETOBJECTPARAMETERIVARBPROC)wglGetProcAddress("glGetObjectParameterivARB");
        glGetInfoLogARB = (PFNGLGETINFOLOGARBPROC)wglGetProcAddress("glGetInfoLogARB");
        glUniform1iARB = (PFNGLUNIFORM1IARBPROC)wglGetProcAddress("glUniform1iARB");
        glGetHandleARB = (PFNGLGETHANDLEARBPROC)wglGetProcAddress("glGetHandleARB");

        if (glCreateShaderObjectARB && glShaderSourceARB && glCompileShaderARB && glCreateProgramObjectARB &&
                glAttachObjectARB && glLinkProgramARB && glUseProgramObjectARB && glDetachObjectARB &&
                glDeleteObjectARB && glGetUniformLocationARB && glUniform4fARB && glUniformMatrix4fvARB &&
                glGetObjectParameterivARB && glGetInfoLogARB && glUniform1iARB) {
            usingGLSL = true;
        }
    }

#ifndef NATIVE_4_1
    glProgramUniform1i = (PFNGLPROGRAMUNIFORM1IPROC)wglGetProcAddress("glProgramUniform1i");

    glProgramUniform1f = (PFNGLPROGRAMUNIFORM1FPROC)wglGetProcAddress("glProgramUniform1f");
    glProgramUniform2f = (PFNGLPROGRAMUNIFORM2FPROC)wglGetProcAddress("glProgramUniform2f");
    glProgramUniform3f = (PFNGLPROGRAMUNIFORM3FPROC)wglGetProcAddress("glProgramUniform3f");
    glProgramUniform4f = (PFNGLPROGRAMUNIFORM4FPROC)wglGetProcAddress("glProgramUniform4f");

    glProgramUniform1d = (PFNGLPROGRAMUNIFORM1DPROC)wglGetProcAddress("glProgramUniform1d");
    glProgramUniform2d = (PFNGLPROGRAMUNIFORM2DPROC)wglGetProcAddress("glProgramUniform2d");
    glProgramUniform3d = (PFNGLPROGRAMUNIFORM3DPROC)wglGetProcAddress("glProgramUniform3d");
    glProgramUniform4d = (PFNGLPROGRAMUNIFORM4DPROC)wglGetProcAddress("glProgramUniform4d");

    glProgramUniformMatrix4fv = (PFNGLPROGRAMUNIFORMMATRIX4FVPROC)wglGetProcAddress("glProgramUniformMatrix4fv");
    glProgramUniformMatrix3fv = (PFNGLPROGRAMUNIFORMMATRIX3FVPROC)wglGetProcAddress("glProgramUniformMatrix3fv");
#endif

#ifndef NATIVE_4_3
    glPushDebugGroup = (PFNGLPUSHDEBUGGROUPPROC)wglGetProcAddress("glPushDebugGroup");
    glPopDebugGroup = (PFNGLPOPDEBUGGROUPPROC)wglGetProcAddress("glPopDebugGroup");
    glObjectLabel = (PFNGLOBJECTLABELPROC)wglGetProcAddress("glObjectLabel");
    glGetObjectLabel = (PFNGLGETOBJECTLABELPROC)wglGetProcAddress("glGetObjectLabel");
    glObjectPtrLabel = (PFNGLOBJECTPTRLABELPROC)wglGetProcAddress("glObjectPtrLabel");
    glGetObjectPtrLabel = (PFNGLGETOBJECTPTRLABELPROC)wglGetProcAddress("glGetObjectPtrLabel");

    glGetProgramInterfaceiv = (PFNGLGETPROGRAMINTERFACEIVPROC)wglGetProcAddress("glGetProgramInterfaceiv");
    glGetProgramResourceiv = (PFNGLGETPROGRAMRESOURCEIVPROC)wglGetProcAddress("glGetProgramResourceiv");
    glGetProgramResourceName = (PFNGLGETPROGRAMRESOURCENAMEPROC)wglGetProcAddress("glGetProgramResourceName");
#endif

#ifndef NATIVE_4_5
    glNamedBufferData = (PFNGLNAMEDBUFFERDATAPROC)wglGetProcAddress("glNamedBufferData");
    glNamedBufferSubData = (PFNGLNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glNamedBufferSubData");
    glGetNamedBufferSubData = (PFNGLGETNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glGetNamedBufferSubData");
    glGetNamedBufferParameteriv = (PFNGLGETNAMEDBUFFERPARAMETERIVPROC)wglGetProcAddress("glGetNamedBufferParameteriv");

    glNamedBufferStorage = (PFNGLNAMEDBUFFERSTORAGEPROC)wglGetProcAddress("glNamedBufferStorage");
    glMapNamedBufferRange = (PFNGLMAPNAMEDBUFFERRANGEPROC)wglGetProcAddress("glMapNamedBufferRange");
    glUnmapNamedBuffer = (PFNGLUNMAPNAMEDBUFFERPROC)wglGetProcAddress("glUnmapNamedBuffer");
#endif

    return true;
}

bool OpenGLExtensionManager::HasUBOs()
{
    return ExtensionSupported("GL_ARB_uniform_buffer_object") > 0
           && ExtensionSupported("GL_ARB_program_interface_query") > 0
           && usingVB
           && glBindBufferBase
           && glGenBuffersARB
           && glGetProgramInterfaceiv
           && glGetProgramResourceiv
           && glGetProgramResourceName
           ;
}

bool OpenGLExtensionManager::HasProgramUniform(void)
{
    const bool hasExtension = ExtensionSupported("GL_ARB_separate_shader_objects") > 0;

    if (hasExtension == false) {
        return false;
    }

    const bool hasProgramUniformFunctions = glProgramUniform1i
                                            && glProgramUniform1f
                                            && glProgramUniform2f
                                            && glProgramUniform3f
                                            && glProgramUniform4f
                                            && glProgramUniformMatrix4fv;

    return hasProgramUniformFunctions;
}
}