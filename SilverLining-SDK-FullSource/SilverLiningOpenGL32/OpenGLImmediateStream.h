// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningOpenGLPreamble.h"
#include "OpenGLStream.h"

namespace SilverLining
{
    class OpenGLImmediateStream : public OpenGlStream
    {
    public:
        OpenGLImmediateStream(Context* _context);
        virtual ~OpenGLImmediateStream();
    public:
        virtual void execute(void);
        virtual void reset(void);
        virtual bool isImmediate(void) const;
        int numCommands(void) const;

        virtual void glEnable(GLenum cap);
        virtual void glDisable(GLenum cap);

        virtual void glClear(GLbitfield mask);
        virtual void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);

        // for shaders
        virtual void glUseProgram(GLuint program);
        virtual void glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
        virtual void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy);
        virtual void glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy);

        virtual void glProgramUniform1f(GLuint program, GLint location, GLfloat v0);
        virtual void glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1);
        virtual void glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
        virtual void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

        virtual void glProgramUniform1d(GLuint program, GLint location, GLdouble v0);
        virtual void glProgramUniform2d(GLuint program, GLint location, GLdouble v0, GLdouble v1);
        virtual void glProgramUniform3d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2);
        virtual void glProgramUniform4d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3);

        virtual void glProgramUniform1i(GLuint program, GLint location, GLint v0);

        virtual void glUniform1f(GLint location, GLfloat v0);
        virtual void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
        virtual void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
        virtual void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

        virtual void glUniform1d(GLint location, GLdouble x);
        virtual void glUniform2d(GLint location, GLdouble x, GLdouble y);
        virtual void glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z);
        virtual void glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w);

        virtual void glUniform1i(GLint location, GLint v0);

        virtual void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
        virtual void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

        virtual void glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
        virtual void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

        // for buffers
        virtual void glBindVertexArray(GLuint array);
        virtual void glBindBuffer(GLenum target, SL_Buffer* slBuffer);
        virtual void glBindBuffer(GLenum target, BufferGL* slBuffer);

        // states
        virtual void glFrontFace(GLenum mode);

        virtual void glDepthFunc(GLenum func);
        virtual void glDepthRangedNV(float zmin, float zmax);
        virtual void glDepthRange(float zmin, float zmax);

        virtual void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

        virtual void glHint(GLenum target, GLenum mode);
        virtual void glLineWidth(GLfloat width);

        virtual void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
        virtual void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);

        virtual void glEnableVertexAttribArray(GLuint index);
        virtual void glDisableVertexAttribArray(GLuint index);

        virtual void glDrawArrays(GLenum mode, GLint first, GLsizei count);

        virtual void glActiveTexture(GLenum texture);
        virtual void glBindTexture(GLenum target, GLuint texture);
        virtual void glPixelStorei(GLenum pname, GLint param);
        virtual void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels);

        virtual bool HasNamedBufferSubData(void) const;
        virtual bool HasBlendFuncSeparate(void) const;
        virtual bool HasTexSubImage3D(void) const;

        virtual void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
        virtual void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
        virtual void glDeleteBuffer(GLuint handle);

        virtual void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
        virtual void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);

        virtual void glPointSize(GLfloat size);

        // pseudo-commands
        virtual void glBindVertexArrayFor(GLuint vboID, Shader* shader);

        virtual void checkGlError(int line);

        virtual void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
        virtual void glBlendFunc(GLenum sfactor, GLenum dfactor);

        virtual void glDepthMask(GLboolean flag);

        virtual void glBindFramebufferEXT(GLenum target, GLuint framebuffer);
        virtual void glDrawBuffer(GLenum target);
    };
}