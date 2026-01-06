#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef MAC
// Use the latest glext.h that we include
#define GL_GLEXT_LEGACY
#include <OpenGL/gl.h>
#define NO_GLU
#ifdef GL_VERSION_1_2
#define NATIVE_1_2
#endif
#ifdef GL_VERSION_1_3
#define NATIVE_1_3
#endif
#ifdef GL_VERSION_4_3
#define NATIVE_4_3
#endif
#ifdef GL_VERSION_2_0
#define NATIVE_2_0
#endif
#ifdef GL_VERSION_3_0
#define NATIVE_3_0
#endif
#include "glext.h"
#else
#ifdef LINUX
#define GL_GLEXT_LEGACY
#endif
#include "GL/gl.h"
#ifndef NO_GLU
#include "GL/glu.h"
#endif
#ifdef GL_VERSION_1_2
#define NATIVE_1_2
#endif
#ifdef GL_VERSION_1_3
#define NATIVE_1_3
#endif
#ifdef GL_VERSION_1_5
#define NATIVE_1_5
#endif
#ifdef GL_VERSION_4_3
#define NATIVE_4_3
#endif
#ifdef GL_VERSION_2_0
#define NATIVE_2_0
#endif
#ifdef GL_VERSION_3_0
#define NATIVE_3_0
#endif
#ifdef GL_VERSION_3_1
#define NATIVE_3_1
#endif
#include "glext.h"
#endif

#if defined(WIN32) || defined(WIN64)
#include "wglext.h"
#elif defined(LINUX)
#include "GL/glx.h"
#define wglGetProcAddress(A) glXGetProcAddress((GLubyte *)A)
#elif defined(MAC)
#include <dlfcn.h>
static void * MyGLGetProcAddress(const char *name)
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

namespace SilverLining
{
    extern PFNGLGETBUFFERPARAMETERUI64VNVPROC glGetBufferParameterui64vNV;
    extern PFNGLGETNAMEDBUFFERPARAMETERUI64VNVPROC glGetNamedBufferParameterui64vNV;

    extern PFNGLISBUFFERRESIDENTNVPROC glIsBufferResidentNV;
    extern PFNGLMAKEBUFFERRESIDENTNVPROC glMakeBufferResidentNV;
    extern PFNGLMAKEBUFFERNONRESIDENTNVPROC glMakeBufferNonResidentNV;

    extern PFNGLISNAMEDBUFFERRESIDENTNVPROC glIsNamedBufferResidentNV;
    extern PFNGLMAKENAMEDBUFFERRESIDENTNVPROC glMakeNamedBufferResidentNV;
    extern PFNGLMAKENAMEDBUFFERNONRESIDENTNVPROC glMakeNamedBufferNonResidentNV;

    extern PFNGLVERTEXATTRIBFORMATNVPROC glVertexAttribFormatNV;
    extern PFNGLBUFFERADDRESSRANGENVPROC glBufferAddressRangeNV;
	extern PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer;
	extern PFNGLVERTEXATTRIBFORMATPROC glVertexAttribFormat;
	extern PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding;
	extern PFNGLMULTIDRAWELEMENTSINDIRECTBINDLESSNVPROC glMultiDrawElementsIndirectBindlessNV;

    extern PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB;
    extern PFNGLGETTEXTURESAMPLERHANDLEARBPROC glGetTextureSamplerHandleARB;
    extern PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB;
    extern PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB;
    extern PFNGLISTEXTUREHANDLERESIDENTARBPROC glIsTextureHandleResidentARB;
    extern PFNGLUNIFORMHANDLEUI64ARBPROC glUniformHandleui64ARB;
    extern PFNGLUNIFORMHANDLEUI64VARBPROC glUniformHandleui64vARB;
    extern PFNGLPROGRAMUNIFORMHANDLEUI64ARBPROC glProgramUniformHandleui64ARB;
    extern PFNGLPROGRAMUNIFORMHANDLEUI64VARBPROC glProgramUniformHandleui64vARB;

    extern PFNGLGETOBJECTPARAMETERIVARBPROC glGetObjectParameterivARB;
    extern PFNGLGETINFOLOGARBPROC glGetInfoLogARB;

    extern PFNGLDEPTHRANGEDNVPROC glDepthRangedNV;

#ifndef NATIVE_4_5
#ifndef GL_VERSION_4_5
    typedef void (APIENTRYP PFNGLGENERATETEXTUREMIPMAPPROC) (GLuint texture);
    typedef void (APIENTRYP PFNGLNAMEDBUFFERDATAPROC) (GLuint buffer, GLsizeiptr size, const void *data, GLenum usage);
    typedef void (APIENTRYP PFNGLNAMEDBUFFERSUBDATAPROC) (GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data);
    typedef void (APIENTRYP PFNGLGETNAMEDBUFFERSUBDATAPROC) (GLuint buffer, GLintptr offset, GLsizeiptr size, void *data);
    typedef void (APIENTRYP PFNGLGETNAMEDBUFFERPARAMETERIVPROC) (GLuint buffer, GLenum pname, GLint *params);
    typedef void (APIENTRYP PFNGLTEXTUREPARAMETERIPROC) (GLuint texture, GLenum pname, GLint param);
    typedef void (APIENTRYP PFNGLGETTEXTUREPARAMETERIVPROC) (GLuint texture, GLenum pname, GLint *params);
    typedef void (APIENTRYP PFNGLCREATEBUFFERSPROC) (GLsizei n, GLuint *buffers);
    typedef void (APIENTRYP PFNGLCREATESAMPLERSPROC) (GLsizei n, GLuint *samplers);

#endif
    extern PFNGLGENERATETEXTUREMIPMAPPROC glGenerateTextureMipmap;
    extern PFNGLNAMEDBUFFERDATAPROC glNamedBufferData;
    extern PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData;
    extern PFNGLGETNAMEDBUFFERSUBDATAPROC glGetNamedBufferSubData;
    extern PFNGLGETNAMEDBUFFERPARAMETERIVPROC glGetNamedBufferParameteriv;
    extern PFNGLTEXTUREPARAMETERIPROC glTextureParameteri;
    extern PFNGLGETTEXTUREPARAMETERIVPROC glGetTextureParameteriv;
    extern PFNGLCREATEBUFFERSPROC glCreateBuffers;
    extern PFNGLCREATESAMPLERSPROC glCreateSamplers;
    extern PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage;
    extern PFNGLMAPNAMEDBUFFERRANGEPROC glMapNamedBufferRange;
    extern PFNGLUNMAPNAMEDBUFFERPROC glUnmapNamedBuffer;
#endif

#ifndef NATIVE_4_4
#ifndef GL_VERSION_4_4
    typedef void (APIENTRYP PFNGLBUFFERSTORAGEPROC) (GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
#endif
    extern PFNGLBUFFERSTORAGEPROC glBufferStorage;
#endif

#ifndef NATIVE_4_3
#ifndef GL_VERSION_4_3

    typedef void (APIENTRYP PFNGLMULTIDRAWARRAYSINDIRECTPROC) (GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride);
    typedef void (APIENTRYP PFNGLMULTIDRAWELEMENTSINDIRECTPROC) (GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride);

    typedef void (APIENTRYP PFNGLDEBUGMESSAGECONTROLPROC) (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled);
    typedef void (APIENTRYP PFNGLDEBUGMESSAGEINSERTPROC) (GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf);
    typedef void (APIENTRYP PFNGLDEBUGMESSAGECALLBACKPROC) (GLDEBUGPROC callback, const void *userParam);
    typedef GLuint(APIENTRYP PFNGLGETDEBUGMESSAGELOGPROC) (GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog);
    typedef void (APIENTRYP PFNGLPUSHDEBUGGROUPPROC) (GLenum source, GLuint id, GLsizei length, const GLchar *message);
    typedef void (APIENTRYP PFNGLPOPDEBUGGROUPPROC) (void);
    typedef void (APIENTRYP PFNGLOBJECTLABELPROC) (GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
    typedef void (APIENTRYP PFNGLGETOBJECTLABELPROC) (GLenum identifier, GLuint name, GLsizei bufSize, GLsizei *length, GLchar *label);
    typedef void (APIENTRYP PFNGLOBJECTPTRLABELPROC) (const void *ptr, GLsizei length, const GLchar *label);
    typedef void (APIENTRYP PFNGLGETOBJECTPTRLABELPROC) (const void *ptr, GLsizei bufSize, GLsizei *length, GLchar *label);

    typedef void (APIENTRYP PFNGLGETPROGRAMINTERFACEIVPROC) (GLuint program, GLenum programInterface, GLenum pname, GLint* params);
    typedef void (APIENTRYP PFNGLGETPROGRAMRESOURCEIVPROC) (GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum* props, GLsizei bufSize, GLsizei* length, GLint* params);
    typedef void (APIENTRYP PFNGLGETPROGRAMRESOURCENAMEPROC) (GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei* length, GLchar* name);

#endif
    extern PFNGLMULTIDRAWARRAYSINDIRECTPROC glMultiDrawArraysIndirect;
    extern PFNGLMULTIDRAWELEMENTSINDIRECTPROC glMultiDrawElementsIndirect;

    extern PFNGLDEBUGMESSAGECONTROLPROC glDebugMessageControl;
    extern PFNGLDEBUGMESSAGEINSERTPROC glDebugMessageInsert;
    extern PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;
    extern PFNGLGETDEBUGMESSAGELOGPROC glGetDebugMessageLog;
    extern PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup;
    extern PFNGLPOPDEBUGGROUPPROC glPopDebugGroup;
    extern PFNGLOBJECTLABELPROC glObjectLabel;
    extern PFNGLGETOBJECTLABELPROC glGetObjectLabel;
    extern PFNGLOBJECTPTRLABELPROC glObjectPtrLabel;
    extern PFNGLGETOBJECTPTRLABELPROC glGetObjectPtrLabel;

    extern PFNGLGETPROGRAMINTERFACEIVPROC glGetProgramInterfaceiv;
    extern PFNGLGETPROGRAMRESOURCEIVPROC glGetProgramResourceiv;
    extern PFNGLGETPROGRAMRESOURCENAMEPROC glGetProgramResourceName;
#endif

#ifndef NATIVE_4_1
#ifndef GL_VERSION_4_1
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1IPROC) (GLuint program, GLint location, GLint v0);

    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1FPROC) (GLuint program, GLint location, GLfloat v0);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM2FPROC) (GLuint program, GLint location, GLfloat v0, GLfloat v1);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM3FPROC) (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM4FPROC) (GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM1DPROC) (GLuint program, GLint location, GLdouble v0);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM2DPROC) (GLuint program, GLint location, GLdouble v0, GLdouble v1);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM3DPROC) (GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORM4DPROC) (GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3);

    typedef void (APIENTRYP PFNGLPROGRAMUNIFORMMATRIX3FVPROC) (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    typedef void (APIENTRYP PFNGLPROGRAMUNIFORMMATRIX4FVPROC) (GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
#endif
    extern PFNGLPROGRAMUNIFORM1IPROC glProgramUniform1i;

    extern PFNGLPROGRAMUNIFORM1FPROC glProgramUniform1f;
    extern PFNGLPROGRAMUNIFORM2FPROC glProgramUniform2f;
    extern PFNGLPROGRAMUNIFORM3FPROC glProgramUniform3f;
    extern PFNGLPROGRAMUNIFORM4FPROC glProgramUniform4f;

    extern PFNGLPROGRAMUNIFORM1DPROC glProgramUniform1d;
    extern PFNGLPROGRAMUNIFORM2DPROC glProgramUniform2d;
    extern PFNGLPROGRAMUNIFORM3DPROC glProgramUniform3d;
    extern PFNGLPROGRAMUNIFORM4DPROC glProgramUniform4d;

    extern PFNGLPROGRAMUNIFORMMATRIX4FVPROC glProgramUniformMatrix4fv;
    extern PFNGLPROGRAMUNIFORMMATRIX3FVPROC glProgramUniformMatrix3fv;
#endif

#ifndef NATIVE_4_0
#ifndef GL_VERSION_4_0
    typedef void (APIENTRYP PFNGLUNIFORM1DPROC) (GLint location, GLdouble x);
    typedef void (APIENTRYP PFNGLUNIFORM2DPROC) (GLint location, GLdouble x, GLdouble y);
    typedef void (APIENTRYP PFNGLUNIFORM3DPROC) (GLint location, GLdouble x, GLdouble y, GLdouble z);
    typedef void (APIENTRYP PFNGLUNIFORM4DPROC) (GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w);

    typedef void (APIENTRYP PFNGLDRAWARRAYSINDIRECTPROC) (GLenum mode, const void *indirect);
    typedef void (APIENTRYP PFNGLDRAWELEMENTSINDIRECTPROC) (GLenum mode, GLenum type, const void *indirect);
#endif

    extern PFNGLUNIFORM1DPROC glUniform1d;
    extern PFNGLUNIFORM2DPROC glUniform2d;
    extern PFNGLUNIFORM3DPROC glUniform3d;
    extern PFNGLUNIFORM4DPROC glUniform4d;

    extern PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect;
    extern PFNGLDRAWELEMENTSINDIRECTPROC glDrawElementsIndirect;

#endif

#ifndef NATIVE_3_3
    typedef void (APIENTRYP PFNGLGENSAMPLERSPROC) (GLsizei count, GLuint *samplers);
    typedef void (APIENTRYP PFNGLDELETESAMPLERSPROC) (GLsizei count, const GLuint *samplers);
    typedef GLboolean(APIENTRYP PFNGLISSAMPLERPROC) (GLuint sampler);
    typedef void (APIENTRYP PFNGLBINDSAMPLERPROC) (GLuint unit, GLuint sampler);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERIPROC) (GLuint sampler, GLenum pname, GLint param);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERIVPROC) (GLuint sampler, GLenum pname, const GLint *param);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERFPROC) (GLuint sampler, GLenum pname, GLfloat param);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERFVPROC) (GLuint sampler, GLenum pname, const GLfloat *param);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERIIVPROC) (GLuint sampler, GLenum pname, const GLint *param);
    typedef void (APIENTRYP PFNGLSAMPLERPARAMETERIUIVPROC) (GLuint sampler, GLenum pname, const GLuint *param);
    typedef void (APIENTRYP PFNGLGETSAMPLERPARAMETERIVPROC) (GLuint sampler, GLenum pname, GLint *params);
    typedef void (APIENTRYP PFNGLGETSAMPLERPARAMETERIIVPROC) (GLuint sampler, GLenum pname, GLint *params);
    typedef void (APIENTRYP PFNGLGETSAMPLERPARAMETERFVPROC) (GLuint sampler, GLenum pname, GLfloat *params);
    typedef void (APIENTRYP PFNGLGETSAMPLERPARAMETERIUIVPROC) (GLuint sampler, GLenum pname, GLuint *params);
    typedef void (APIENTRYP PFNGLVERTEXATTRIBDIVISORARBPROC) (GLuint index, GLuint divisor);
#endif

    extern PFNGLGENSAMPLERSPROC glGenSamplers;
    extern PFNGLDELETESAMPLERSPROC glDeleteSamplers;
    extern PFNGLISSAMPLERPROC glIsSampler;
    extern PFNGLBINDSAMPLERPROC glBindSampler;

    extern PFNGLSAMPLERPARAMETERIPROC       glSamplerParameteri;
    extern PFNGLSAMPLERPARAMETERIVPROC      glSamplerParameteriv;
    extern PFNGLSAMPLERPARAMETERFPROC       glSamplerParameterf;
    extern PFNGLSAMPLERPARAMETERFVPROC      glSamplerParameterfv;
    extern PFNGLSAMPLERPARAMETERIIVPROC     glSamplerParameterIiv;
    extern PFNGLSAMPLERPARAMETERIUIVPROC    glSamplerParameterIuiv;
    extern PFNGLGETSAMPLERPARAMETERIVPROC   glGetSamplerParameteriv;
    extern PFNGLGETSAMPLERPARAMETERIIVPROC  glGetSamplerParameterIiv;
    extern PFNGLGETSAMPLERPARAMETERFVPROC   glGetSamplerParameterfv;
    extern PFNGLGETSAMPLERPARAMETERIUIVPROC glGetSamplerParameterIuiv;
    extern PFNGLVERTEXATTRIBDIVISORARBPROC glVertexAttribDivisor;

#ifndef NATIVE_3_2
    extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
    extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
    extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
    extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
    extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
    extern PFNGLCLIENTWAITSYNCPROC glClientWaitSync;
    extern PFNGLFENCESYNCPROC glFenceSync;
    extern PFNGLDELETESYNCPROC glDeleteSync;
#endif

#ifndef NATIVE_3_1
    extern PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex;
    extern PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding;
    extern PFNGLGETACTIVEUNIFORMBLOCKIVPROC glGetActiveUniformBlockiv;
    extern PFNGLGETACTIVEUNIFORMSIVPROC glGetActiveUniformsiv;
    extern PFNGLGETUNIFORMINDICESPROC glGetUniformIndices;

    extern PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
    extern PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
#endif

#ifndef NATIVE_3_0
    extern PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
    extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
    extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
    extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
    extern PFNGLGETSTRINGIPROC glGetStringi;
    extern PFNGLBINDBUFFERBASEPROC glBindBufferBase;
    
    extern PFNGLBINDFRAGDATALOCATIONPROC glBindFragDataLocation;
    extern PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;
    
    extern PFNGLISRENDERBUFFERPROC glIsRenderbuffer;
    extern PFNGLBINDBUFFERPROC glBindRenderbuffer;
    extern PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
    extern PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
    extern PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
    extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
#endif

#ifndef NATIVE_1_2
	extern PFNGLTEXIMAGE3DPROC glTexImage3D;
	extern PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D;
#endif

#ifndef NATIVE_1_3
#ifndef GL_VERSION_1_3
    typedef void (APIENTRYP PFNGLACTIVETEXTUREPROC) (GLenum texture);
    typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREPROC) (GLenum texture);
#endif
    extern PFNGLACTIVETEXTUREPROC glActiveTexture;
    extern PFNGLCLIENTACTIVETEXTUREPROC glClientActiveTexture;
#endif

#ifndef NATIVE_1_4
    extern PFNGLPOINTPARAMETERFPROC glPointParameterf;
    extern PFNGLPOINTPARAMETERFVPROC glPointParameterfv;
    extern PFNGLMULTIDRAWARRAYSPROC glMultiDrawArrays;
    extern PFNGLMULTIDRAWELEMENTSPROC glMultiDrawElements;
#endif

#ifndef NATIVE_1_5
    extern PFNGLGENQUERIESPROC glGenQueries;
    extern PFNGLDELETEQUERIESPROC glDeleteQueries;
    extern PFNGLISQUERYPROC glIsQuery;
    extern PFNGLBEGINQUERYPROC glBeginQuery;
    extern PFNGLENDQUERYPROC glEndQuery;
    extern PFNGLGETQUERYIVPROC glGetQueryiv;
    extern PFNGLGETQUERYOBJECTIVPROC glGetQueryObjectiv;
    extern PFNGLGETQUERYOBJECTUIVPROC glGetQueryObjectuiv;

    extern PFNGLISBUFFERPROC glIsBuffer;
    extern PFNGLGETBUFFERPARAMETERIVPROC glGetBufferParameteriv;
#endif

#ifndef NATIVE_2_0
    extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
    extern PFNGLUNIFORM1IPROC glUniform1i;
    extern PFNGLUNIFORM1FPROC glUniform1f;
    extern PFNGLUNIFORM2FPROC glUniform2f;
    extern PFNGLUSEPROGRAMPROC glUseProgram;
    extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
    extern PFNGLDETACHSHADERPROC glDetachShader;
    extern PFNGLGETSHADERIVPROC glGetShaderiv;
    extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
    extern PFNGLGETSHADERSOURCEPROC glGetShaderSource;
    extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
    extern PFNGLCREATESHADERPROC glCreateShader;
    extern PFNGLCOMPILESHADERPROC glCompileShader;
    extern PFNGLSHADERSOURCEPROC glShaderSource;
    extern PFNGLCREATEPROGRAMPROC glCreateProgram;
    extern PFNGLATTACHSHADERPROC glAttachShader;
    extern PFNGLLINKPROGRAMPROC glLinkProgram;
    extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
    extern PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
    extern PFNGLUNIFORM3FPROC glUniform3f;
    extern PFNGLUNIFORM4FPROC glUniform4f;
    extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
    extern PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
    extern PFNGLBINDBUFFERPROC glBindBuffer;
    extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
    extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
    extern PFNGLGENBUFFERSPROC glGenBuffers;
    extern PFNGLBUFFERDATAPROC glBufferData;
    extern PFNGLDELETESHADERPROC glDeleteShader;
    extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
    extern PFNGLMAPBUFFERPROC glMapBuffer;
    extern PFNGLUNMAPBUFFERPROC glUnmapBuffer;
    extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
    extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
    extern PFNGLGETBUFFERSUBDATAPROC glGetBufferSubData;
    extern PFNGLMAPBUFFERRANGEPROC glMapBufferRange;
    extern PFNGLISSHADERPROC glIsShader;
    extern PFNGLDRAWBUFFERSPROC glDrawBuffers;
#endif

    extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffersEXT;
    extern PFNGLBINDFRAMEBUFFERPROC glBindFramebufferEXT;
    extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2DEXT;
    extern PFNGLGENERATEMIPMAPPROC glGenerateMipmapEXT;
    extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffersEXT;
    extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatusEXT;
    
    extern PFNGLBLENDEQUATIONEXTPROC glBlendEquationEXT;
}