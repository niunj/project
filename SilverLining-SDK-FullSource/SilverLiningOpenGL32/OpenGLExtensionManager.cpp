#include "OpenGLExtensionManager.h"
#include "SilverLiningOpenGLPreamble.h"
#include "OpenGLUtils.h"

#include <cstdio>
#include <string.h>

namespace SilverLining
{
bool OpenGLExtensionManager::usingFloatBuffers = false;
bool OpenGLExtensionManager::usingOcclusionQuery = false;
bool OpenGLExtensionManager::usingMultisample = false;
bool OpenGLExtensionManager::usingDepthRangedNV = false;
bool OpenGLExtensionManager::usingCoreProfile = false;

bool OpenGLExtensionManager::ExtensionSupported(const char *extension)
{
    if (glGetStringi) {
        GLint n, i;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        OpenGLUtils::CheckError(__LINE__);

        for (i = 0; i < n; i++) {
            const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
            OpenGLUtils::CheckError(__LINE__);
            if (ext) {
                if (strcmp(ext, extension) == 0)
                    return true;
            }
        }
    }

    return false;
}

bool OpenGLExtensionManager::LoadGLExtensions()
{
#ifdef _WIN32
    if (wglGetCurrentContext() == NULL) {
        return false;
    }
#endif

    glGetBufferParameterui64vNV = (PFNGLGETBUFFERPARAMETERUI64VNVPROC)wglGetProcAddress("glGetBufferParameterui64vNV");
    glGetNamedBufferParameterui64vNV = (PFNGLGETNAMEDBUFFERPARAMETERUI64VNVPROC)wglGetProcAddress("glGetNamedBufferParameterui64vNV");

    glIsBufferResidentNV = (PFNGLISBUFFERRESIDENTNVPROC)wglGetProcAddress("glIsBufferResidentNV");
    glMakeBufferResidentNV = (PFNGLMAKEBUFFERRESIDENTNVPROC)wglGetProcAddress("glMakeBufferResidentNV");
    glMakeBufferNonResidentNV = (PFNGLMAKEBUFFERNONRESIDENTNVPROC)wglGetProcAddress("glMakeBufferNonResidentNV");

    glIsNamedBufferResidentNV = (PFNGLISNAMEDBUFFERRESIDENTNVPROC)wglGetProcAddress("glIsNamedBufferResidentNV");
    glMakeNamedBufferResidentNV = (PFNGLMAKENAMEDBUFFERRESIDENTNVPROC)wglGetProcAddress("glMakeNamedBufferResidentNV");
    glMakeNamedBufferNonResidentNV = (PFNGLMAKENAMEDBUFFERNONRESIDENTNVPROC)wglGetProcAddress("glMakeNamedBufferNonResidentNV");

    glVertexAttribFormatNV = (PFNGLVERTEXATTRIBFORMATNVPROC)wglGetProcAddress("glVertexAttribFormatNV");
    glBufferAddressRangeNV = (PFNGLBUFFERADDRESSRANGENVPROC)wglGetProcAddress("glBufferAddressRangeNV");
    glBindVertexBuffer = (PFNGLBINDVERTEXBUFFERPROC)wglGetProcAddress("glBindVertexBuffer");
    glVertexAttribFormat = (PFNGLVERTEXATTRIBFORMATPROC)wglGetProcAddress("glVertexAttribFormat");
    glVertexAttribBinding = (PFNGLVERTEXATTRIBBINDINGPROC)wglGetProcAddress("glVertexAttribBinding");
    glMultiDrawElementsIndirectBindlessNV = (PFNGLMULTIDRAWELEMENTSINDIRECTBINDLESSNVPROC)wglGetProcAddress("glMultiDrawElementsIndirectBindlessNV");

    glGetTextureHandleARB = (PFNGLGETTEXTUREHANDLEARBPROC)wglGetProcAddress("glGetTextureHandleARB");
    glGetTextureSamplerHandleARB = (PFNGLGETTEXTURESAMPLERHANDLEARBPROC)wglGetProcAddress("glGetTextureSamplerHandleARB");
    glMakeTextureHandleResidentARB = (PFNGLMAKETEXTUREHANDLERESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleResidentARB");
    glMakeTextureHandleNonResidentARB = (PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC)wglGetProcAddress("glMakeTextureHandleNonResidentARB");
    glIsTextureHandleResidentARB = (PFNGLISTEXTUREHANDLERESIDENTARBPROC)wglGetProcAddress("glIsTextureHandleResidentARB");
    glUniformHandleui64ARB = (PFNGLUNIFORMHANDLEUI64ARBPROC)wglGetProcAddress("glUniformHandleui64ARB");
    glUniformHandleui64vARB = (PFNGLUNIFORMHANDLEUI64VARBPROC)wglGetProcAddress("glUniformHandleui64vARB");
    glProgramUniformHandleui64ARB = (PFNGLPROGRAMUNIFORMHANDLEUI64ARBPROC)wglGetProcAddress("glProgramUniformHandleui64ARB");
    glProgramUniformHandleui64vARB = (PFNGLPROGRAMUNIFORMHANDLEUI64VARBPROC)wglGetProcAddress("glProgramUniformHandleui64vARB");


    glGetObjectParameterivARB = (PFNGLGETOBJECTPARAMETERIVARBPROC)wglGetProcAddress("glGetObjectParameterivARB");
    glGetInfoLogARB = (PFNGLGETINFOLOGARBPROC)wglGetProcAddress("glGetInfoLogARB");

    glDepthRangedNV = (PFNGLDEPTHRANGEDNVPROC)wglGetProcAddress("glDepthRangedNV");
    usingDepthRangedNV = glDepthRangedNV ? true : false;

#ifndef NATIVE_4_5
    glGenerateTextureMipmap = (PFNGLGENERATETEXTUREMIPMAPPROC)wglGetProcAddress("glGenerateTextureMipmap");
    glNamedBufferData = (PFNGLNAMEDBUFFERDATAPROC)wglGetProcAddress("glNamedBufferData");
    glNamedBufferSubData = (PFNGLNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glNamedBufferSubData");
    glGetNamedBufferSubData = (PFNGLGETNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glGetNamedBufferSubData");
    glGetNamedBufferParameteriv = (PFNGLGETNAMEDBUFFERPARAMETERIVPROC)wglGetProcAddress("glGetNamedBufferParameteriv");
    glTextureParameteri = (PFNGLTEXTUREPARAMETERIPROC)wglGetProcAddress("glTextureParameteri");
    glGetTextureParameteriv = (PFNGLGETTEXTUREPARAMETERIVPROC)wglGetProcAddress("glGetTextureParameteriv");
    glNamedBufferData = (PFNGLNAMEDBUFFERDATAPROC)wglGetProcAddress("glNamedBufferData");
    glCreateBuffers = (PFNGLCREATEBUFFERSPROC)wglGetProcAddress("glCreateBuffers");
    glCreateSamplers = (PFNGLCREATESAMPLERSPROC)wglGetProcAddress("glCreateSamplers");

    glNamedBufferStorage = (PFNGLNAMEDBUFFERSTORAGEPROC)wglGetProcAddress("glNamedBufferStorage");
    glMapNamedBufferRange = (PFNGLMAPNAMEDBUFFERRANGEPROC)wglGetProcAddress("glMapNamedBufferRange");
    glUnmapNamedBuffer = (PFNGLUNMAPNAMEDBUFFERPROC)wglGetProcAddress("glUnmapNamedBuffer");
#endif

#ifndef NATIVE_4_4
    glBufferStorage = (PFNGLBUFFERSTORAGEPROC)wglGetProcAddress("glBufferStorage");
#endif

#ifndef NATIVE_4_3
    glMultiDrawArraysIndirect = (PFNGLMULTIDRAWARRAYSINDIRECTPROC)wglGetProcAddress("glMultiDrawArraysIndirect");
    glMultiDrawElementsIndirect = (PFNGLMULTIDRAWELEMENTSINDIRECTPROC)wglGetProcAddress("glMultiDrawElementsIndirect");

    glDebugMessageControl = (PFNGLDEBUGMESSAGECONTROLPROC)wglGetProcAddress("glDebugMessageControl");
    glDebugMessageInsert = (PFNGLDEBUGMESSAGEINSERTPROC)wglGetProcAddress("glDebugMessageInsert");
    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)wglGetProcAddress("glDebugMessageCallback");
    glGetDebugMessageLog = (PFNGLGETDEBUGMESSAGELOGPROC)wglGetProcAddress("glGetDebugMessageLog");
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

#ifndef NATIVE_4_0
    glUniform1d = (PFNGLUNIFORM1DPROC)wglGetProcAddress("glUniform1d");
    glUniform2d = (PFNGLUNIFORM2DPROC)wglGetProcAddress("glUniform2d");
    glUniform3d = (PFNGLUNIFORM3DPROC)wglGetProcAddress("glUniform3d");
    glUniform4d = (PFNGLUNIFORM4DPROC)wglGetProcAddress("glUniform4d");

    glDrawArraysIndirect = (PFNGLDRAWARRAYSINDIRECTPROC)wglGetProcAddress("glDrawArraysIndirect");
    glDrawElementsIndirect = (PFNGLDRAWELEMENTSINDIRECTPROC)wglGetProcAddress("glDrawElementsIndirect");
#endif

#ifndef NATIVE_3_3

    glGenSamplers = (PFNGLGENSAMPLERSPROC)wglGetProcAddress("glGenSamplers");
    glDeleteSamplers = (PFNGLDELETESAMPLERSPROC)wglGetProcAddress("glDeleteSamplers");
    glIsSampler = (PFNGLISSAMPLERPROC)wglGetProcAddress("glIsSampler");
    glBindSampler = (PFNGLBINDSAMPLERPROC)wglGetProcAddress("glBindSampler");

    glSamplerParameteri = (PFNGLSAMPLERPARAMETERIPROC)wglGetProcAddress("glSamplerParameteri");
    glSamplerParameteriv = (PFNGLSAMPLERPARAMETERIVPROC)wglGetProcAddress("glSamplerParameteriv");
    glSamplerParameterf = (PFNGLSAMPLERPARAMETERFPROC)wglGetProcAddress("glSamplerParameterf");
    glSamplerParameterfv = (PFNGLSAMPLERPARAMETERFVPROC)wglGetProcAddress("glSamplerParameterfv");
    glSamplerParameterIiv = (PFNGLSAMPLERPARAMETERIIVPROC)wglGetProcAddress("glSamplerParameterIiv");
    glSamplerParameterIuiv = (PFNGLSAMPLERPARAMETERIUIVPROC)wglGetProcAddress("glSamplerParameterIuiv");
    glGetSamplerParameteriv = (PFNGLGETSAMPLERPARAMETERIVPROC)wglGetProcAddress("glGetSamplerParameteriv");
    glGetSamplerParameterIiv = (PFNGLGETSAMPLERPARAMETERIIVPROC)wglGetProcAddress("glGetSamplerParameterIiv");
    glGetSamplerParameterfv = (PFNGLGETSAMPLERPARAMETERFVPROC)wglGetProcAddress("glGetSamplerParameterfv");
    glGetSamplerParameterIuiv = (PFNGLGETSAMPLERPARAMETERIUIVPROC)wglGetProcAddress("glGetSamplerParameterIuiv");
    glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORARBPROC)wglGetProcAddress("glVertexAttribDivisor");
#endif

#ifndef NATIVE_3_2
    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
    glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
    glClientWaitSync = (PFNGLCLIENTWAITSYNCPROC)wglGetProcAddress("glClientWaitSync");
    glFenceSync = (PFNGLFENCESYNCPROC)wglGetProcAddress("glFenceSync");
    glDeleteSync = (PFNGLDELETESYNCPROC)wglGetProcAddress("glDeleteSync");
#endif

#ifndef NATIVE_3_1
    glGetUniformBlockIndex = (PFNGLGETUNIFORMBLOCKINDEXPROC)wglGetProcAddress("glGetUniformBlockIndex");
    glUniformBlockBinding = (PFNGLUNIFORMBLOCKBINDINGPROC)wglGetProcAddress("glUniformBlockBinding");
    glGetActiveUniformBlockiv = (PFNGLGETACTIVEUNIFORMBLOCKIVPROC)wglGetProcAddress("glGetActiveUniformBlockiv");
    glGetActiveUniformsiv = (PFNGLGETACTIVEUNIFORMSIVPROC)wglGetProcAddress("glGetActiveUniformsiv");
    glGetUniformIndices = (PFNGLGETUNIFORMINDICESPROC)wglGetProcAddress("glGetUniformIndices");

    glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)wglGetProcAddress("glDrawArraysInstanced");
    glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)wglGetProcAddress("glDrawElementsInstanced");
#endif

#ifndef NATIVE_3_0
    glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
    glGetStringi = (PFNGLGETSTRINGIPROC)wglGetProcAddress("glGetStringi");
    glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)wglGetProcAddress("glBindBufferBase");
    glBindFragDataLocation = (PFNGLBINDFRAGDATALOCATIONPROC)wglGetProcAddress("glBindFragDataLocation");

    glBlendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)wglGetProcAddress("glBlendFuncSeparate");

    glIsRenderbuffer = (PFNGLISRENDERBUFFERPROC)wglGetProcAddress("glIsRenderbuffer");
    glBindRenderbuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
    glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
    glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
    glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
    glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
#endif

#ifndef NATIVE_1_2
    glTexImage3D = (PFNGLTEXIMAGE3DPROC)wglGetProcAddress("glTexImage3D");
    glTexSubImage3D = (PFNGLTEXSUBIMAGE3DPROC)wglGetProcAddress("glTexSubImage3D");
#endif

#ifndef NATIVE_1_3
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
    glClientActiveTexture = (PFNGLCLIENTACTIVETEXTUREPROC)wglGetProcAddress("glClientActiveTexture");
#endif

#ifndef NATIVE_1_4
    glPointParameterf = (PFNGLPOINTPARAMETERFPROC)wglGetProcAddress("glPointParameterf");
    glPointParameterfv = (PFNGLPOINTPARAMETERFVPROC)wglGetProcAddress("glPointParameterfv");
    glMultiDrawArrays = (PFNGLMULTIDRAWARRAYSPROC)wglGetProcAddress("glMultiDrawArrays");
    glMultiDrawElements = (PFNGLMULTIDRAWELEMENTSPROC)wglGetProcAddress("glMultiDrawElements");
    glBlendFuncSeparate = (PFNGLBLENDFUNCSEPARATEPROC)wglGetProcAddress("glBlendFuncSeparate");
#endif

#ifndef NATIVE_1_5
    glGenQueries = (PFNGLGENQUERIESPROC)wglGetProcAddress("glGenQueries");
    glDeleteQueries = (PFNGLDELETEQUERIESPROC)wglGetProcAddress("glDeleteQueries");
    glIsQuery = (PFNGLISQUERYPROC)wglGetProcAddress("glIsQuery");
    glBeginQuery = (PFNGLBEGINQUERYPROC)wglGetProcAddress("glBeginQuery");
    glEndQuery = (PFNGLENDQUERYPROC)wglGetProcAddress("glEndQuery");
    glGetQueryiv = (PFNGLGETQUERYIVPROC)wglGetProcAddress("glGetQueryiv");
    glGetQueryObjectiv = (PFNGLGETQUERYOBJECTIVPROC)wglGetProcAddress("glGetQueryObjectiv");
    glGetQueryObjectuiv = (PFNGLGETQUERYOBJECTUIVPROC)wglGetProcAddress("glGetQueryObjectuiv");

    usingOcclusionQuery = glGenQueries
                          && glDeleteQueries
                          && glIsQuery
                          && glBeginQuery
                          && glEndQuery
                          && glGetQueryiv
                          && glGetQueryObjectiv
                          && glGetQueryObjectuiv;

    glIsBuffer = (PFNGLISBUFFERPROC)wglGetProcAddress("glIsBuffer");
    glGetBufferParameteriv = (PFNGLGETBUFFERPARAMETERIVPROC)wglGetProcAddress("glGetBufferParameteriv");
#endif

#ifndef NATIVE_2_0
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");

    glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
    glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
    glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");

    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
    glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glGetShaderSource = (PFNGLGETSHADERSOURCEPROC)wglGetProcAddress("glGetShaderSource");
    glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");

    glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)wglGetProcAddress("glGetAttribLocation");
    glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
    glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
    glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC)wglGetProcAddress("glUniformMatrix3fv");

    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
    glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glMapBuffer = (PFNGLMAPBUFFERPROC)wglGetProcAddress("glMapBuffer");
    glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)wglGetProcAddress("glUnmapBuffer");
    glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glDisableVertexAttribArray");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
    glBufferSubData = (PFNGLBUFFERSUBDATAPROC)wglGetProcAddress("glBufferSubData");
    glGetBufferSubData = (PFNGLGETBUFFERSUBDATAPROC)wglGetProcAddress("glGetBufferSubData");
    glMapBufferRange = (PFNGLMAPBUFFERRANGEPROC)wglGetProcAddress("glMapBufferRange");

    glIsShader = (PFNGLISSHADERPROC)wglGetProcAddress("glIsShader");

    glDrawBuffers = (PFNGLDRAWBUFFERSPROC)wglGetProcAddress("glDrawBuffers");
#endif

    usingFloatBuffers = ExtensionSupported("GL_ATI_texture_float");
    usingMultisample = ExtensionSupported("GL_ARB_multisample");

    return true;
}

bool OpenGLExtensionManager::HasUBOs()
{
    const bool hasUBOExtension = OpenGLExtensionManager::ExtensionSupported("GL_ARB_uniform_buffer_object");

    if (hasUBOExtension == false) {
        return false;
    }

    // check that it has the ubo path functions
    const bool hasUboFunctions = glBindBufferBase // we need this to be able to bind the UBOs
                                 // These are not used currently, but good to check
                                 && glGetUniformBlockIndex
                                 && glUniformBlockBinding
                                 && glGetActiveUniformBlockiv
                                 && glGetActiveUniformsiv
                                 && glGetUniformIndices;

    if (hasUboFunctions == false) {
        return false;
    }

    // having ubo extension does not necessarily mean that we have ability to introspect
    // to create ubo map, either, so check for this as well
    const bool canCreateUBOMap = glGetProgramInterfaceiv
                                 && glGetProgramResourceiv
                                 && glGetProgramResourceName;

    return canCreateUBOMap;
}

bool OpenGLExtensionManager::HasProgramUniform()
{
    const bool hasExtension = OpenGLExtensionManager::ExtensionSupported("GL_ARB_separate_shader_objects");

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