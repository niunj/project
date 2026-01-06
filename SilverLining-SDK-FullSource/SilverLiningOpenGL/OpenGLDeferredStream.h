// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once
#include "OpenGLStream.h"
#include "OpenGLPacket.h"
#include "SLAssert.h"
#include "SLMap.h"
#include "SLList.h"
#include <vector>

#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
#include <atomic>
#else
#include "Mutex.h"
#endif

namespace SilverLining
{
class Context;
class OpenGLDeferredStream : public OpenGlStream
{
public:
    OpenGLDeferredStream(Context* _context);
    virtual ~OpenGLDeferredStream();
public:
    virtual void execute(void);
    virtual void reset(void);
    virtual bool isImmediate(void) const;
    virtual int numCommands(void) const;

    virtual void glEnable(GLenum cap);
    virtual void glDisable(GLenum cap);

    virtual void glBegin(GLenum mode);
    virtual void glEnd();

    virtual void glPushMatrix(void);
    virtual void glPopMatrix(void);

    virtual void glPushAttrib(GLbitfield mask);
    virtual void glPopAttrib(void);

    virtual void glVertex3d(GLdouble x, GLdouble y, GLdouble z);
    virtual void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
    virtual void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    virtual void glTexCoord2d(GLdouble s, GLdouble t);

    virtual void glMatrixMode(GLenum mode);
    virtual void glLoadIdentity(void);
    virtual void glLoadMatrixd(const GLdouble* m);
    virtual void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);

    virtual void glRasterPos2i(GLint x, GLint y);
    virtual void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);

    virtual void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
    virtual void glDepthMask(GLboolean flag);
    virtual void glHint(GLenum target, GLenum mode);
    virtual void glLineWidth(GLfloat width);

    virtual void glTexEnvi(GLenum target, GLenum pname, GLint param);
    virtual void glPointParameteriNV(GLenum pname, GLint param);
    virtual void glPointSize(GLfloat size);

    virtual void glActiveTexture(GLenum texture);
    virtual void glBindTexture(GLenum target, GLuint texture);
    virtual void glPixelStorei(GLenum pname, GLint param);
    virtual void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels);
    virtual bool HasTexSubImage3D(void) const;

    virtual void glFrontFace(GLenum mode);

    virtual bool HasBindBufferARB() const;
    virtual void glBindBufferARB(GLenum target, SL_Buffer* slBuffer);
    virtual void glPseudoBindBufferARB(GLenum target, SL_Buffer* slBuffer);
    virtual void glBindBufferARB(GLenum target, BufferGL* slBuffer);

    virtual void glBufferSubDataARB(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
    virtual void glBufferDataARB(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
    virtual void glDeleteBufferARB(GLuint handle);

    virtual void glEnableClientState(GLenum array);
    virtual void glDisableClientState(GLenum array);

    virtual void glClientActiveTexture(GLenum texture);

    virtual void glVertexFormatNV(GLint size, GLenum type, GLsizei stride);
    virtual void glColorFormatNV(GLint size, GLenum type, GLsizei stride);
    virtual void glTexCoordFormatNV(GLint size, GLenum type, GLsizei stride);

    virtual void glDepthFunc(GLenum func);
    virtual void glDepthRangedNV(float zmin, float zmax);
    virtual void glDepthRange(float zmin, float zmax);

    virtual void glClear(GLbitfield mask);
    virtual void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);

    virtual void glBufferAddressRangeNV(GLenum pname, GLuint index, GLuint64EXT address, GLsizeiptr length);
    virtual void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    virtual void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    virtual void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);


    virtual void glDrawArrays(GLenum mode, GLint first, GLsizei count);
    virtual void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
    virtual void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);

    // For shaders
    virtual void glUseProgramObjectARB(GLuint program);
    virtual void glBindBufferBase(GLenum target, GLuint index, GLuint buffer);

    virtual void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy);
    virtual bool HasNamedBufferSubData(void) const;
    virtual void glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy);
    virtual void glPseudoSync(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool justColors);

    virtual bool HasBlendFuncSeparate(void) const;
    virtual void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
    virtual void glBlendFunc(GLenum sfactor, GLenum dfactor);

    virtual bool HasBlendEquationSeparate(void) const;
    virtual void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);

    virtual void glFogf(GLenum pname, GLfloat param);
    virtual void glFogfv(GLenum pname, const GLfloat *params);
    virtual void glFogi(GLenum pname, GLint param);

    virtual void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
    virtual void glUniform4fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

    virtual void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
    virtual void glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

    virtual void glBindFramebufferEXT(GLenum target, GLuint framebuffer);
    virtual void glDrawBuffer(GLenum target);

    virtual void checkGlError(int line);

private:
    unsigned int            maxPackets;
    ALL_PACKETS*    packets;

#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
    std::atomic_uint        numPackets;
#else
    unsigned int        numPackets;
    Mutex* packetMutex;
#endif

    template <typename T>
    T* NextPacket() {
#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
        SL_ASSERT(numPackets < maxPackets);
        if (numPackets >= maxPackets)
        {
            std::cout<<"numPackets >= maxPackets"<<std::endl;
            return 0;
        }
        return reinterpret_cast<T*>(&packets[numPackets.fetch_add(1)]);
#else
        ScopedMutex scopedMutex(packetMutex);
        SL_ASSERT(numPackets < maxPackets);
        return reinterpret_cast<T*>(&packets[numPackets++]);
#endif
    }

    virtual void* GetNextDataStore(int size);
    virtual void ReturnAllToDataStore(void);
    virtual void DeleteAllInDataStore(void);
    typedef SL_MAP(int, SL_DEQUE(void*)) MapSizeToDataStoreList;

    MapSizeToDataStoreList mapSizeToDataStoreListPool;
    MapSizeToDataStoreList mapSizeToDataStoreListInUse;

    std::vector<Vertex> scratchVerticesWithColors;
    std::vector<Vertex> scratchVerticesToGetFromVertexBufferAndCopyColorsInto;
    virtual void ReturnAllVericesToPool(void);

    bool hasBindBufferARB;
    bool hasBlendFuncSeparate;
    bool hasBlendEquationSeparate;
    bool hasNamedBufferSubData;
};
}