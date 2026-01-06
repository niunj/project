// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#include "OpenGLImmediateStream.h"


#include "Shader.h"
#include "OpenGLUtils.h"
#include "SL_Buffer.h"
#include "BufferGL.h"
#include "SLAssert.h"

#ifdef MAC
#include <OpenGL/glu.h>
#else
#include "GL/glu.h"
#endif

using namespace SilverLining;

namespace SilverLining
{

OpenGLImmediateStream::OpenGLImmediateStream(Context* _context)
    : OpenGlStream(_context)
{

}
OpenGLImmediateStream::~OpenGLImmediateStream()
{

}
bool OpenGLImmediateStream::isImmediate(void) const
{
    return true;
}
int OpenGLImmediateStream::numCommands(void) const
{
    return 0;
}
void OpenGLImmediateStream::execute(void)
{
    // An immediate stream is never explicitly executed
    SL_ASSERT(false);
}
void OpenGLImmediateStream::reset(void)
{
    // An immediate stream is never explicitly reset
    SL_ASSERT(false);
}

void OpenGLImmediateStream::glEnable(GLenum cap)
{
    ::glEnable(cap);
}
void OpenGLImmediateStream::glDisable(GLenum cap)
{
    ::glDisable(cap);
}


void OpenGLImmediateStream::glBegin(GLenum mode)
{
    ::glBegin(mode);
}
void OpenGLImmediateStream::glEnd()
{
    ::glEnd();
}

void OpenGLImmediateStream::glPushMatrix(void)
{
    ::glPushMatrix();
}
void OpenGLImmediateStream::glPopMatrix(void)
{
    ::glPopMatrix();
}


void OpenGLImmediateStream::glPushAttrib(GLbitfield mask)
{
    ::glPushAttrib(mask);
}
void OpenGLImmediateStream::glPopAttrib(void)
{
    ::glPopAttrib();
}

void OpenGLImmediateStream::glVertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    ::glVertex3d(x, y, z);
}
void OpenGLImmediateStream::glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
    ::glColor4d(red, green, blue, alpha);
}
void OpenGLImmediateStream::glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    ::glColor4f(red, green, blue, alpha);
}
void OpenGLImmediateStream::glTexCoord2d(GLdouble s, GLdouble t)
{
    ::glTexCoord2d(s, t);
}


void OpenGLImmediateStream::glMatrixMode(GLenum mode)
{
    ::glMatrixMode(mode);
}
void OpenGLImmediateStream::glLoadIdentity(void)
{
    ::glLoadIdentity();
}
void OpenGLImmediateStream::glLoadMatrixd(const GLdouble* m)
{
    ::glLoadMatrixd(m);
}
void OpenGLImmediateStream::gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
    ::gluOrtho2D(left, right, bottom, top);
}

void OpenGLImmediateStream::glRasterPos2i(GLint x, GLint y)
{
    ::glRasterPos2i(x, y);
}
void OpenGLImmediateStream::glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    ::glDrawPixels(width, height, format, type, pixels);
}

void OpenGLImmediateStream::glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    ::glViewport(x, y, width, height);
}

void OpenGLImmediateStream::glDepthMask(GLboolean flag)
{
    ::glDepthMask(flag);
}


void OpenGLImmediateStream::glHint(GLenum target, GLenum mode)
{
    ::glHint(target, mode);
}


void OpenGLImmediateStream::glLineWidth(GLfloat width)
{
    ::glLineWidth(width);
}

void OpenGLImmediateStream::glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    ::glTexEnvi(target, pname, param);
}
void OpenGLImmediateStream::glPointParameteriNV(GLenum pname, GLint param)
{
    ::glPointParameteriNV(pname, param);
}


void OpenGLImmediateStream::glPointSize(GLfloat size)
{
    ::glPointSize(size);
}



void OpenGLImmediateStream::glUseProgramObjectARB(GLuint program)
{
    ::glUseProgramObjectARB((GLhandleARB)program);
}

void OpenGLImmediateStream::glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    ::glBindBufferBase(target, index, buffer);
}

void OpenGLImmediateStream::glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    ::glNamedBufferSubData(buffer, offset, size, data);
}

bool OpenGLImmediateStream::HasNamedBufferSubData(void) const
{
    return ::glNamedBufferSubData != 0;
}

void OpenGLImmediateStream::glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    SL_ASSERT(slBuffer&&slBuffer->handle == 0);
    slBuffer->InitBuffers();
    ::glNamedBufferSubData(slBuffer->handle, offset, size, data);
}

void OpenGLImmediateStream::glActiveTexture(GLenum texture)
{
    ::glActiveTexture(texture);
}
void OpenGLImmediateStream::glBindTexture(GLenum target, GLuint texture)
{
    ::glBindTexture(target, texture);
}
bool OpenGLImmediateStream::HasTexSubImage3D(void) const
{
    return ::glTexSubImage3D != 0;
}
void OpenGLImmediateStream::glPixelStorei(GLenum pname, GLint param)
{
    ::glPixelStorei(pname, param);
}
void OpenGLImmediateStream::glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
    ::glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}

void OpenGLImmediateStream::glFrontFace(GLenum mode)
{
    ::glFrontFace(mode);
}

bool OpenGLImmediateStream::HasBindBufferARB() const
{
    return ::glBindBufferARB != 0;
}

void OpenGLImmediateStream::glBindBufferARB(GLenum target, SL_Buffer* slBuffer)
{
    if (::glBindBufferARB) {
        const GLuint handle = (slBuffer) ? slBuffer->handle : 0;
        ::glBindBufferARB(target, handle);
    }
}

void OpenGLImmediateStream::glPseudoBindBufferARB(GLenum target, SL_Buffer* slBuffer)
{
    if (slBuffer && slBuffer->handle == 0) {
        slBuffer->InitBuffers();
    }
    if (::glBindBufferARB) {
        const GLuint handle = (slBuffer) ? slBuffer->handle : 0;
        ::glBindBufferARB(target, handle);
    }
}

void OpenGLImmediateStream::glBindBufferARB(GLenum target, BufferGL* slBuffer)
{
    if (::glBindBufferARB) {
        const GLuint handle = (slBuffer) ? slBuffer->Handle() : 0;
        ::glBindBufferARB(target, handle);
    }
}

void OpenGLImmediateStream::glBufferSubDataARB(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    ::glBufferSubDataARB(target, offset, size, data);
}

void OpenGLImmediateStream::glBufferDataARB(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    ::glBufferDataARB(target, size, data, usage);
}

void OpenGLImmediateStream::glDeleteBufferARB(GLuint handle)
{
    ::glDeleteBuffersARB(1, &handle);
}

void OpenGLImmediateStream::glEnableClientState(GLenum array)
{
    ::glEnableClientState(array);
}
void OpenGLImmediateStream::glDisableClientState(GLenum array)
{
    ::glDisableClientState(array);
}

void OpenGLImmediateStream::glClientActiveTexture(GLenum texture)
{
    ::glClientActiveTexture(texture);
}

void OpenGLImmediateStream::glVertexFormatNV(GLint size, GLenum type, GLsizei stride)
{
    ::glVertexFormatNV(size, type, stride);
}
void OpenGLImmediateStream::glColorFormatNV(GLint size, GLenum type, GLsizei stride)
{
    ::glColorFormatNV(size, type, stride);
}
void OpenGLImmediateStream::glTexCoordFormatNV(GLint size, GLenum type, GLsizei stride)
{
    ::glTexCoordFormatNV(size, type, stride);
}


void OpenGLImmediateStream::glDepthFunc(GLenum func)
{
    ::glDepthFunc(func);
}
void OpenGLImmediateStream::glDepthRangedNV(float zmin, float zmax)
{
    ::glDepthRangedNV(zmin, zmax);
}
void OpenGLImmediateStream::glDepthRange(float zmin, float zmax)
{
    ::glDepthRange(zmin, zmax);
}


void OpenGLImmediateStream::glClear(GLbitfield mask)
{
    ::glClear(mask);
}

void OpenGLImmediateStream::glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    ::glClearColor(red, green, blue, alpha);
}

void OpenGLImmediateStream::glBufferAddressRangeNV(GLenum pname, GLuint index, GLuint64EXT address, GLsizeiptr length)
{
    ::glBufferAddressRangeNV(pname, index, address, length);
}

void OpenGLImmediateStream::glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    ::glVertexPointer(size, type, stride, pointer);
}
void OpenGLImmediateStream::glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    ::glColorPointer(size, type, stride, pointer);
}
void OpenGLImmediateStream::glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    ::glTexCoordPointer(size, type, stride, pointer);
}

void OpenGLImmediateStream::glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    ::glDrawArrays(mode, first, count);
}

void OpenGLImmediateStream::glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    ::glDrawElements(mode, count, type, indices);
}

void OpenGLImmediateStream::glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount)
{
    ::glDrawElementsInstanced(mode, count, type, indices, instancecount);
}

bool OpenGLImmediateStream::HasBlendFuncSeparate(void) const
{
    return ::glBlendFuncSeparate != 0;
}
void OpenGLImmediateStream::glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    ::glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

void OpenGLImmediateStream::glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    ::glBlendFunc(sfactor, dfactor);
}

bool OpenGLImmediateStream::HasBlendEquationSeparate(void) const
{
    return ::glBlendEquationSeparate != 0;
}
void OpenGLImmediateStream::glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
    ::glBlendEquationSeparate(modeRGB, modeAlpha);
}

void OpenGLImmediateStream::glFogf(GLenum pname, GLfloat param)
{
    ::glFogf(pname, param);
}
void OpenGLImmediateStream::glFogfv(GLenum pname, const GLfloat *params)
{
    ::glFogfv(pname, params);
}
void OpenGLImmediateStream::glFogi(GLenum pname, GLint param)
{
    ::glFogi(pname, param);
}

void OpenGLImmediateStream::glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    ::glProgramUniform4f(program, location, v0, v1, v2, v3);
}

void OpenGLImmediateStream::glUniform4fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    ::glUniform4fARB(location, v0, v1, v2, v3);
}

void OpenGLImmediateStream::glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glProgramUniformMatrix4fv(program, location, count, transpose, value);
}


void OpenGLImmediateStream::glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glUniformMatrix4fvARB(location, count, transpose, value);
}

void OpenGLImmediateStream::glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    ::glBindFramebufferEXT(target, framebuffer);
}
void OpenGLImmediateStream::glDrawBuffer(GLenum target)
{
    ::glDrawBuffer(target);
}


void OpenGLImmediateStream::checkGlError(int line)
{
    OpenGLUtils::CheckError(line);
}
}
