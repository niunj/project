#pragma once

#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif

#include <stddef.h>

// Causes problems in some deferred setups:
#define USE_PBO_READBACK

#ifdef MAC
// Use the latest glext.h that we include
#define GL_GLEXT_LEGACY
#include <OpenGL/gl.h>
#ifdef GL_VERSION_1_2
#define NATIVE_1_2
#endif
#ifdef GL_VERSION_1_3
#define NATIVE_1_3
#endif
#ifdef GL_VERSION_1_4
#define NATIVE_1_4
#endif
#ifdef GL_VERSION_2_0
#define NATIVE_2_0
#endif
#ifdef GL_VERSION_3_0
#define NATIVE_3_0
#endif
#ifdef GL_VERSION_4_1
#define NATIVE_4_1
#endif
#ifdef GL_VERSION_4_3
#define NATIVE_4_3
#endif
#ifdef GL_VERSION_4_5
#define NATIVE_4_5
#endif
#include "glext.h"
#define NO_GLU
#else
#ifdef LINUX
#define GL_GLEXT_LEGACY
#endif
#include "GL/gl.h"
//#include "GL/glu.h"
#ifdef GL_VERSION_1_2
#define NATIVE_1_2
#endif
#ifdef GL_VERSION_1_3
#define NATIVE_1_3
#endif
#ifdef GL_VERSION_1_4
#define NATIVE_1_4
#endif
#ifdef GL_VERSION_2_0
#define NATIVE_2_0
#endif
#ifdef GL_VERSION_3_0
#define NATIVE_3_0
#endif
#ifdef GL_VERSION_4_1
#define NATIVE_4_1
#endif
#ifdef GL_VERSION_4_3
#define NATIVE_4_3
#endif
#ifdef GL_VERSION_4_5
#define NATIVE_4_5
#endif
#include "glext.h"
#endif

#if defined(WIN32) || defined(WIN64)
#include "wglext.h"
#define COMPRESS_TEXTURES
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
    extern PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
    extern PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
    extern PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;
    extern PFNGLGENERATEMIPMAPEXTPROC glGenerateMipmapEXT;
    extern PFNGLDELETEFRAMEBUFFERSEXTPROC glDeleteFramebuffersEXT;
    extern PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;

    extern PFNGLBLENDEQUATIONEXTPROC glBlendEquationEXT;

	extern PFNGLGENBUFFERSARBPROC glGenBuffersARB;
	extern PFNGLBINDBUFFERARBPROC glBindBufferARB;
	extern PFNGLBUFFERDATAARBPROC glBufferDataARB;
	extern PFNGLMAPBUFFERARBPROC glMapBufferARB;
	extern PFNGLISBUFFERARBPROC glIsBufferARB;
	extern PFNGLBUFFERSUBDATAARBPROC glBufferSubDataARB;
	extern PFNGLGETBUFFERSUBDATAARBPROC glGetBufferSubDataARB;
	extern PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB;
	extern PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
	extern PFNGLPOINTPARAMETERINVPROC glPointParameteriNV;
#ifndef NATIVE_1_3
	extern PFNGLACTIVETEXTUREARBPROC glActiveTexture;
	extern PFNGLCLIENTACTIVETEXTUREARBPROC glClientActiveTexture;
#endif
#ifndef NATIVE_1_2
	extern PFNGLTEXIMAGE3DPROC glTexImage3D;
	extern PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D;
#endif
	extern PFNGLBUFFERADDRESSRANGENVPROC glBufferAddressRangeNV;
	extern PFNGLVERTEXFORMATNVPROC glVertexFormatNV;
	extern PFNGLCOLORFORMATNVPROC glColorFormatNV;
	extern PFNGLTEXCOORDFORMATNVPROC glTexCoordFormatNV;
	extern PFNGLGETBUFFERPARAMETERUI64VNVPROC glGetBufferParameterui64vNV;
    extern PFNGLISBUFFERRESIDENTNVPROC glIsBufferResidentNV;
	extern PFNGLMAKEBUFFERRESIDENTNVPROC glMakeBufferResidentNV;
	extern PFNGLMAKEBUFFERNONRESIDENTNVPROC glMakeBufferNonResidentNV;

    extern PFNGLISNAMEDBUFFERRESIDENTNVPROC glIsNamedBufferResidentNV;
    extern PFNGLMAKENAMEDBUFFERRESIDENTNVPROC glMakeNamedBufferResidentNV;
    extern PFNGLMAKENAMEDBUFFERNONRESIDENTNVPROC glMakeNamedBufferNonResidentNV;

	extern PFNGLGENQUERIESARBPROC glGenQueriesARB;
	extern PFNGLDELETEQUERIESARBPROC glDeleteQueriesARB;
	extern PFNGLBEGINQUERYARBPROC glBeginQueryARB;
	extern PFNGLENDQUERYARBPROC glEndQueryARB;
	extern PFNGLGETQUERYOBJECTUIVARBPROC glGetQueryObjectuivARB;
	extern PFNGLCLAMPCOLORARBPROC glClampColorARB;
	extern PFNGLCREATESHADEROBJECTARBPROC glCreateShaderObjectARB;
	extern PFNGLSHADERSOURCEARBPROC glShaderSourceARB;
	extern PFNGLCOMPILESHADERARBPROC glCompileShaderARB;
	extern PFNGLCREATEPROGRAMOBJECTARBPROC glCreateProgramObjectARB;
	extern PFNGLATTACHOBJECTARBPROC glAttachObjectARB;
	extern PFNGLLINKPROGRAMARBPROC glLinkProgramARB;
	extern PFNGLUSEPROGRAMOBJECTARBPROC glUseProgramObjectARB;
	extern PFNGLDETACHOBJECTARBPROC glDetachObjectARB;
	extern PFNGLDELETEOBJECTARBPROC glDeleteObjectARB;
	extern PFNGLGETUNIFORMLOCATIONARBPROC glGetUniformLocationARB;
	extern PFNGLUNIFORM4FARBPROC glUniform4fARB;
	extern PFNGLUNIFORMMATRIX4FVARBPROC glUniformMatrix4fvARB;
	extern PFNGLGETOBJECTPARAMETERIVARBPROC glGetObjectParameterivARB;
	extern PFNGLGETINFOLOGARBPROC glGetInfoLogARB;
	extern PFNGLUNIFORM1IARBPROC glUniform1iARB;
	extern PFNGLGETHANDLEARBPROC glGetHandleARB;
	extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
	extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
	extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
	extern PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
	extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
	extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
	extern PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
	extern PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
	extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
	extern PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
	extern PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
	extern PFNGLDEPTHRANGEDNVPROC glDepthRangedNV;

    extern PFNGLGETNAMEDBUFFERPARAMETERUI64VNVPROC glGetNamedBufferParameterui64vNV;

    extern PFNGLVERTEXATTRIBFORMATNVPROC glVertexAttribFormatNV;
    extern PFNGLMULTIDRAWELEMENTSINDIRECTBINDLESSNVPROC glMultiDrawElementsIndirectBindlessNV;

    extern PFNGLGETTEXTUREHANDLEARBPROC glGetTextureHandleARB;
    extern PFNGLMAKETEXTUREHANDLERESIDENTARBPROC glMakeTextureHandleResidentARB;
    extern PFNGLMAKETEXTUREHANDLENONRESIDENTARBPROC glMakeTextureHandleNonResidentARB;
    extern PFNGLISTEXTUREHANDLERESIDENTARBPROC glIsTextureHandleResidentARB;

#ifndef NATIVE_1_4
	extern PFNGLBLENDFUNCSEPARATEPROC glBlendFuncSeparate;
#endif

#ifndef NATIVE_2_0
#ifndef GL_VERSION_2_0
	typedef void (APIENTRYP PFNGLDISABLEVERTEXATTRIBARRAYPROC) (GLuint index);
	typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index); 
#endif
	extern PFNGLBLENDEQUATIONSEPARATEPROC glBlendEquationSeparate;
	extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
	extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
#endif

#ifndef NATIVE_3_0
	extern PFNGLBINDBUFFERBASEPROC glBindBufferBase;
#endif


#ifndef NATIVE_3_1
    extern PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
    extern PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
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

#ifndef NATIVE_4_3
#ifndef GL_VERSION_4_3
	typedef void (APIENTRYP PFNGLGETPROGRAMINTERFACEIVPROC) (GLuint program, GLenum programInterface, GLenum pname, GLint* params);
	typedef void (APIENTRYP PFNGLGETPROGRAMRESOURCEIVPROC) (GLuint program, GLenum programInterface, GLuint index, GLsizei propCount, const GLenum* props, GLsizei bufSize, GLsizei* length, GLint* params);
	typedef void (APIENTRYP PFNGLGETPROGRAMRESOURCENAMEPROC) (GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize, GLsizei* length, GLchar* name);
#endif
	extern PFNGLPUSHDEBUGGROUPPROC glPushDebugGroup;
	extern PFNGLPOPDEBUGGROUPPROC glPopDebugGroup;
	extern PFNGLOBJECTLABELPROC glObjectLabel;
	extern PFNGLGETOBJECTLABELPROC glGetObjectLabel;
	extern PFNGLOBJECTPTRLABELPROC glObjectPtrLabel;
	extern PFNGLGETOBJECTPTRLABELPROC glGetObjectPtrLabel;

	extern PFNGLGETPROGRAMINTERFACEIVPROC glGetProgramInterfaceiv;
	extern PFNGLGETPROGRAMRESOURCEIVPROC glGetProgramResourceiv;
	extern PFNGLGETPROGRAMRESOURCENAMEPROC glGetProgramResourceName;

	extern PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer;
	extern PFNGLVERTEXATTRIBFORMATPROC glVertexAttribFormat;
	extern PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding;
#endif

#ifndef NATIVE_4_5
#ifndef GL_VERSION_4_5
	typedef void (APIENTRYP PFNGLNAMEDBUFFERDATAPROC) (GLuint buffer, GLsizeiptr size, const void *data, GLenum usage);
	typedef void (APIENTRYP PFNGLNAMEDBUFFERSUBDATAPROC) (GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data);
    typedef void (APIENTRYP PFNGLGETNAMEDBUFFERSUBDATAPROC) (GLuint buffer, GLintptr offset, GLsizeiptr size, void *data);
	typedef void (APIENTRYP PFNGLGETNAMEDBUFFERPARAMETERIVPROC) (GLuint buffer, GLenum pname, GLint *params);

    typedef void (APIENTRYP PFNGLNAMEDBUFFERSTORAGEPROC) (GLuint buffer, GLsizeiptr size, const void *data, GLbitfield flags);
    typedef void *(APIENTRYP PFNGLMAPNAMEDBUFFERRANGEPROC) (GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access);
    typedef GLboolean(APIENTRYP PFNGLUNMAPNAMEDBUFFERPROC) (GLuint buffer);
#endif
	extern PFNGLNAMEDBUFFERDATAPROC glNamedBufferData;
	extern PFNGLNAMEDBUFFERSUBDATAPROC glNamedBufferSubData;
    extern PFNGLGETNAMEDBUFFERSUBDATAPROC glGetNamedBufferSubData;
	extern PFNGLGETNAMEDBUFFERPARAMETERIVPROC glGetNamedBufferParameteriv;
    extern PFNGLNAMEDBUFFERSTORAGEPROC glNamedBufferStorage;
    extern PFNGLMAPNAMEDBUFFERRANGEPROC glMapNamedBufferRange;
    extern PFNGLUNMAPNAMEDBUFFERPROC glUnmapNamedBuffer;
#endif
}
