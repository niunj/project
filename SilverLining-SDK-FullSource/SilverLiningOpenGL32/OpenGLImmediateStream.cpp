// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#include "OpenGLImmediateStream.h"
#include "Shader.h"
#include "OpenGLUtils.h"
#include "SL_Buffer.h"
#include "BufferGL.h"
#include "SLAssert.h"

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

void OpenGLImmediateStream::glClear(GLbitfield mask)
{
    ::glClear(mask);
}

void OpenGLImmediateStream::glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    ::glClearColor(red, green, blue, alpha);
}


void OpenGLImmediateStream::glUseProgram(GLuint program)
{
    ::glUseProgram(program);
}

void OpenGLImmediateStream::glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    ::glBindBufferBase(target, index, buffer);
}

bool OpenGLImmediateStream::HasNamedBufferSubData(void) const
{
    return ::glNamedBufferSubData != 0;
}
bool OpenGLImmediateStream::HasBlendFuncSeparate(void) const
{
    return ::glBlendFuncSeparate != 0;
}
bool OpenGLImmediateStream::HasTexSubImage3D(void) const
{
    return ::glTexSubImage3D != 0;
}

void OpenGLImmediateStream::glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    ::glNamedBufferSubData(buffer, offset, size, data);
}

void OpenGLImmediateStream::glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    SL_ASSERT(slBuffer&&slBuffer->handle == 0);
    slBuffer->InitBuffers();
    ::glNamedBufferSubData(slBuffer->handle, offset, size, data);
}

void OpenGLImmediateStream::glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    ::glBufferSubData(target, offset, size, data);
}

void OpenGLImmediateStream::glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    ::glBufferData(target, size, data, usage);
}

void OpenGLImmediateStream::glDeleteBuffer(GLuint handle)
{
    ::glDeleteBuffers(1, &handle);
}

void OpenGLImmediateStream::glProgramUniform1f(GLuint program, GLint location, GLfloat v0)
{
    ::glProgramUniform1f(program, location, v0);
}
void OpenGLImmediateStream::glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1)
{
    ::glProgramUniform2f(program, location, v0, v1);
}
void OpenGLImmediateStream::glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    ::glProgramUniform3f(program, location, v0, v1, v2);
}
void OpenGLImmediateStream::glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    ::glProgramUniform4f(program, location, v0, v1, v2, v3);
}


void OpenGLImmediateStream::glProgramUniform1d(GLuint program, GLint location, GLdouble v0)
{
    ::glProgramUniform1d(program, location, v0);
}
void OpenGLImmediateStream::glProgramUniform2d(GLuint program, GLint location, GLdouble v0, GLdouble v1)
{
    ::glProgramUniform2d(program, location, v0, v1);
}
void OpenGLImmediateStream::glProgramUniform3d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2)
{
    ::glProgramUniform3d(program, location, v0, v1, v2);
}
void OpenGLImmediateStream::glProgramUniform4d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3)
{
    ::glProgramUniform4d(program, location, v0, v1, v2, v3);
}

void OpenGLImmediateStream::glProgramUniform1i(GLuint program, GLint location, GLint v0)
{
    ::glProgramUniform1i(program, location, v0);
}


void OpenGLImmediateStream::glUniform1f(GLint location, GLfloat v0)
{
    ::glUniform1f(location, v0);
}
void OpenGLImmediateStream::glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    ::glUniform2f(location, v0, v1);
}
void OpenGLImmediateStream::glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    ::glUniform3f(location, v0, v1, v2);
}
void OpenGLImmediateStream::glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    ::glUniform4f(location, v0, v1, v2, v3);
}

void OpenGLImmediateStream::glUniform1d(GLint location, GLdouble x)
{
    ::glUniform1d(location, x);
}
void OpenGLImmediateStream::glUniform2d(GLint location, GLdouble x, GLdouble y)
{
    ::glUniform2d(location, x, y);
}
void OpenGLImmediateStream::glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z)
{
    ::glUniform3d(location, x, y, z);
}
void OpenGLImmediateStream::glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    ::glUniform4d(location, x, y, z, w);
}

void OpenGLImmediateStream::glUniform1i(GLint location, GLint v0)
{
    ::glUniform1i(location, v0);
}


void OpenGLImmediateStream::glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glUniformMatrix3fv(location, count, transpose, value);
}
void OpenGLImmediateStream::glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glUniformMatrix4fv(location, count, transpose, value);
}

void OpenGLImmediateStream::glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glProgramUniformMatrix3fv(program, location, count, transpose, value);
}
void OpenGLImmediateStream::glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    ::glProgramUniformMatrix4fv(program, location, count, transpose, value);
}

void OpenGLImmediateStream::glBindVertexArray(GLuint vao)
{
    if (::glBindVertexArray) {
        ::glBindVertexArray(vao);
    }
}
void OpenGLImmediateStream::glBindBuffer(GLenum target, SL_Buffer* slBuffer)
{
    if (::glBindBuffer) {
        const GLuint handle = (slBuffer) ? slBuffer->handle : 0;
        ::glBindBuffer(target, handle);
    }
}

void OpenGLImmediateStream::glBindBuffer(GLenum target, BufferGL* slBuffer)
{
    if (::glBindBuffer) {
        const GLuint handle = (slBuffer) ? slBuffer->Handle() : 0;
        ::glBindBuffer(target, handle);
    }
}

void OpenGLImmediateStream::glFrontFace(GLenum mode)
{
    ::glFrontFace(mode);
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
void OpenGLImmediateStream::glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    ::glViewport(x, y, width, height);
}

void OpenGLImmediateStream::glHint(GLenum target, GLenum mode)
{
    ::glHint(target, mode);
}

void OpenGLImmediateStream::glLineWidth(GLfloat width)
{
    ::glLineWidth(width);
}

void OpenGLImmediateStream::glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    ::glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

void OpenGLImmediateStream::glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    ::glVertexPointer(size, type, stride, pointer);
}

void OpenGLImmediateStream::glEnableVertexAttribArray(GLuint index)
{
    ::glEnableVertexAttribArray(index);
}

void OpenGLImmediateStream::glDisableVertexAttribArray(GLuint index)
{
    ::glDisableVertexAttribArray(index);
}

void OpenGLImmediateStream::glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    ::glDrawArrays(mode, first, count);
}

void OpenGLImmediateStream::glActiveTexture(GLenum texture)
{
    ::glActiveTexture(texture);
}
void OpenGLImmediateStream::glBindTexture(GLenum target, GLuint texture)
{
    ::glBindTexture(target, texture);
}

void OpenGLImmediateStream::glPixelStorei(GLenum pname, GLint param)
{
    ::glPixelStorei(pname, param);
}
void OpenGLImmediateStream::glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
    ::glTexSubImage3D(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}

void OpenGLImmediateStream::glBindVertexArrayFor(GLuint vboID, Shader* shader)
{
    GLuint vao = shader->GetOrCreateVAOForContextAndBuffer(vboID);
    ::glBindVertexArray(vao);
}

void OpenGLImmediateStream::glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    ::glDrawElements(mode, count, type, indices);
}
void OpenGLImmediateStream::glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount)
{
    ::glDrawElementsInstanced(mode, count, type, indices, instancecount);
}

void OpenGLImmediateStream::glPointSize(GLfloat size)
{
    ::glPointSize(size);
}

void OpenGLImmediateStream::checkGlError(int line)
{
    OpenGLUtils::CheckError(line);
}

void OpenGLImmediateStream::glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    ::glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

void OpenGLImmediateStream::glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    ::glBlendFunc(sfactor, dfactor);
}

void OpenGLImmediateStream::glDepthMask(GLboolean flag)
{
    ::glDepthMask(flag);
}
void OpenGLImmediateStream::glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    ::glBindFramebufferEXT(target, framebuffer);
}
void OpenGLImmediateStream::glDrawBuffer(GLenum target)
{
    ::glDrawBuffer(target);
}
}
