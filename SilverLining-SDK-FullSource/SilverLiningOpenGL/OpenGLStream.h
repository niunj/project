// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningOpenGLPreamble.h"

namespace SilverLining
{
    class Context;
    class Shader;
    class SL_Buffer;
    class BufferGL;
class OpenGlStream
{
public:
    OpenGlStream(Context* _context);
    virtual ~OpenGlStream();
public:
    virtual void execute(void) = 0;
    virtual void reset(void) = 0;
    virtual bool isImmediate(void) const = 0;
    virtual int numCommands(void) const = 0;

    virtual void glEnable(GLenum cap) = 0;
    virtual void glDisable(GLenum cap) = 0;

    virtual void glBegin(GLenum mode) = 0;
    virtual void glEnd() = 0;

    virtual void glPushMatrix(void) = 0;
    virtual void glPopMatrix(void) = 0;

    virtual void glPushAttrib(GLbitfield mask) = 0;
    virtual void glPopAttrib(void) = 0;

    virtual void glVertex3d(GLdouble x, GLdouble y, GLdouble z) = 0;
    virtual void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha) = 0;
    virtual void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) = 0;
    virtual void glTexCoord2d(GLdouble s, GLdouble t) = 0;

    virtual void glMatrixMode(GLenum mode) = 0;
    virtual void glLoadIdentity(void) = 0;
    virtual void glLoadMatrixd(const GLdouble* m) = 0;
    virtual void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top) = 0;

    virtual void glRasterPos2i(GLint x, GLint y) = 0;
    virtual void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) = 0;

    virtual void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) = 0;
    virtual void glDepthMask(GLboolean flag) = 0;
    virtual void glHint(GLenum target, GLenum mode) = 0;
    virtual void glLineWidth(GLfloat width) = 0;

    virtual void glTexEnvi(GLenum target, GLenum pname, GLint param) = 0;
    virtual void glPointParameteriNV(GLenum pname, GLint param) = 0;
    virtual void glPointSize(GLfloat size) = 0;

    virtual void glActiveTexture(GLenum texture) = 0;
    virtual void glBindTexture(GLenum target, GLuint texture) = 0;
    virtual void glPixelStorei(GLenum pname, GLint param) = 0;
    virtual void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) = 0;
    virtual bool HasTexSubImage3D(void) const = 0;

    // states
    virtual void glFrontFace(GLenum mode) = 0;

    virtual bool HasBindBufferARB() const = 0;
    virtual void glBindBufferARB(GLenum target, SL_Buffer* slBuffer) = 0;
    virtual void glPseudoBindBufferARB(GLenum target, SL_Buffer* slBuffer) = 0;
    virtual void glBindBufferARB(GLenum target, BufferGL* slBuffer) = 0;

    virtual void glBufferSubDataARB(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) = 0;
    virtual void glBufferDataARB(GLenum target, GLsizeiptr size, const void *data, GLenum usage) = 0;
    virtual void glDeleteBufferARB(GLuint handle) = 0;

    virtual void glEnableClientState(GLenum array) = 0;
    virtual void glDisableClientState(GLenum array) = 0;

    virtual void glClientActiveTexture(GLenum texture) = 0;

    virtual void glVertexFormatNV(GLint size, GLenum type, GLsizei stride) = 0;
    virtual void glColorFormatNV(GLint size, GLenum type, GLsizei stride) = 0;
    virtual void glTexCoordFormatNV(GLint size, GLenum type, GLsizei stride) = 0;

    virtual void glDepthFunc(GLenum func) = 0;
    virtual void glDepthRangedNV(float zmin, float zmax) = 0;
    virtual void glDepthRange(float zmin, float zmax) = 0;

    virtual void glClear(GLbitfield mask) = 0;
    virtual void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) = 0;

    virtual void glBufferAddressRangeNV (GLenum pname, GLuint index, GLuint64EXT address, GLsizeiptr length) = 0;
    virtual void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) = 0;
    virtual void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) = 0;
    virtual void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) = 0;

    virtual void glDrawArrays(GLenum mode, GLint first, GLsizei count) = 0;
    virtual void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) = 0;
    virtual void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) = 0;

    virtual void glUseProgramObjectARB(GLuint program) = 0;
    virtual void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) = 0;

    virtual void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy) = 0;
    virtual bool HasNamedBufferSubData(void) const = 0;
    virtual void glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy) = 0;
    virtual void glPseudoSync(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool justColors){}

    virtual bool HasBlendFuncSeparate(void) const = 0;
    virtual void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) = 0;
    virtual void glBlendFunc(GLenum sfactor, GLenum dfactor) = 0;

    virtual bool HasBlendEquationSeparate(void) const = 0;
    virtual void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) = 0;

    virtual void glFogf(GLenum pname, GLfloat param) = 0;
    virtual void glFogfv(GLenum pname, const GLfloat *params) = 0;
    virtual void glFogi (GLenum pname, GLint param) = 0;

    virtual void checkGlError(int line) = 0;

    virtual void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = 0;
    virtual void glUniform4fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = 0;

    virtual void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;
    virtual void glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;

    virtual void glBindFramebufferEXT(GLenum target, GLuint framebuffer) = 0;
    virtual void glDrawBuffer(GLenum target) = 0;

protected:
    Context* context;
};
}