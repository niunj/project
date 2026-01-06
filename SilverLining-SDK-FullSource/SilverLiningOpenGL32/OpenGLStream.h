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

    virtual void glClear(GLbitfield mask) = 0;
    virtual void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) = 0;

    // For shaders
    virtual void glUseProgram(GLuint program) = 0;
    virtual void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) = 0;
    virtual void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy) = 0;
    virtual void glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy) = 0;
    virtual void glPseudoSync(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool justColors){}

    virtual void glProgramUniform1f(GLuint program, GLint location, GLfloat v0) = 0;
    virtual void glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1) = 0;
    virtual void glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) = 0;
    virtual void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = 0;

    virtual void glProgramUniform1d(GLuint program, GLint location, GLdouble v0) = 0;
    virtual void glProgramUniform2d(GLuint program, GLint location, GLdouble v0, GLdouble v1) = 0;
    virtual void glProgramUniform3d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2) = 0;
    virtual void glProgramUniform4d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3) = 0;

    virtual void glProgramUniform1i(GLuint program, GLint location, GLint v0) = 0;


    virtual void glUniform1f(GLint location, GLfloat v0) = 0;
    virtual void glUniform2f(GLint location, GLfloat v0, GLfloat v1) = 0;
    virtual void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) = 0;
    virtual void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) = 0;

    virtual void glUniform1d(GLint location, GLdouble x) = 0;
    virtual void glUniform2d(GLint location, GLdouble x, GLdouble y) = 0;
    virtual void glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z) = 0;
    virtual void glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w) = 0;

    virtual void glUniform1i(GLint location, GLint v0) = 0;

    virtual void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;
    virtual void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;

    virtual void glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;
    virtual void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = 0;

    // for buffers
    virtual void glBindVertexArray(GLuint array) = 0;
    virtual void glBindBuffer(GLenum target, SL_Buffer* slBuffer) = 0;
    virtual void glBindBuffer(GLenum target, BufferGL* slBuffer) = 0;

    // states
    virtual void glFrontFace(GLenum mode) = 0;

    virtual void glDepthFunc(GLenum func) = 0;
    virtual void glDepthRangedNV(float zmin, float zmax) = 0;
    virtual void glDepthRange(float zmin, float zmax) = 0;

    virtual void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) = 0;

    virtual void glHint(GLenum target, GLenum mode) = 0;
    virtual void glLineWidth(GLfloat width) = 0;

    virtual void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) = 0;
    virtual void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) = 0;

    virtual void glEnableVertexAttribArray(GLuint index) = 0;
    virtual void glDisableVertexAttribArray(GLuint index) = 0;

    virtual void glDrawArrays(GLenum mode, GLint first, GLsizei count) = 0;

    virtual void glActiveTexture(GLenum texture) = 0;
    virtual void glBindTexture(GLenum target, GLuint texture) = 0;
    virtual void glPixelStorei(GLenum pname, GLint param) = 0;
    virtual void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels) = 0;

    virtual void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) = 0;
    virtual void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) = 0;
    virtual void glDeleteBuffer(GLuint handle) = 0;

    // pseudo-commands
    virtual void glBindVertexArrayFor(GLuint vboID, Shader* shader) = 0;

    virtual void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) = 0;
    virtual void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) = 0;

    virtual void glPointSize(GLfloat size) = 0;

    virtual void checkGlError(int line) = 0;

    virtual bool HasNamedBufferSubData(void) const = 0;
    virtual bool HasBlendFuncSeparate(void) const = 0;
    virtual bool HasTexSubImage3D(void) const = 0;

    virtual void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) = 0;
    virtual void glBlendFunc(GLenum sfactor, GLenum dfactor) = 0;

    virtual void glDepthMask(GLboolean flag) = 0;

    virtual void glBindFramebufferEXT(GLenum target, GLuint framebuffer) = 0;
    virtual void glDrawBuffer(GLenum target) = 0;
protected:
    Context* context;
};
}