// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#include "SL_Buffer.h"
#include "SLAssert.h"
#include "OpenGLUtils.h"
#include "OpenGLStream.h"

#include <iostream>
#include <cstring>

namespace SilverLining
{
SL_Buffer::SL_Buffer(GLenum _type, int _sizeInBytes, GLenum _usage, const char* _name, const BufferProperties& _bufferProperties)
    : type(_type)
    , sizeInBytes(_sizeInBytes)
    , usage(_usage)
    , bufferProperties(_bufferProperties)
    , handle(0)
    , stagingBuffer(0)
    , locked(false)
{
    if (_name) {
        name = SL_STRING(_name);
    }
    if (bufferProperties.genBuffer) {
        InitBuffers();
    }
}

SL_Buffer::~SL_Buffer()
{
    if (handle) {
        // Invalidate the buffer to force the release of the memory
        glBindBuffer(type, handle);
        glBufferData(type, 0, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(type, 0);

        glDeleteBuffers(1, &handle);
    }

    SL_DELETE[] stagingBuffer;
}

GLubyte* SL_Buffer::GetStagingBuffer() const
{
    if (stagingBuffer == 0) {
        stagingBuffer = SL_NEW GLubyte[sizeInBytes];
        memset(stagingBuffer, 0, sizeInBytes);
    }
    return stagingBuffer;
}

int SL_Buffer::GetSizeInBytes(void) const
{
    return sizeInBytes;
}

static void checkSize(GLuint handle, int expectedSize)
{
    int sizeFromGL = 0;
    //Are we sure there is little to no performance cost of getting this from the driver?
    glGetNamedBufferParameteriv(handle, GL_BUFFER_SIZE, &sizeFromGL);
    SL_ASSERT(sizeFromGL == expectedSize);
    if (sizeFromGL != expectedSize && OpenGLUtils::enableDebugOutput) {
        std::cout << "SL_Buffer::SL_Buffer:" << std::endl;
        std::cout << "sizeFromGL != expectedSize" << std::endl;
    }
}

void SL_Buffer::InitBuffers()
{
    SL_ASSERT(handle == 0);
    glGenBuffers(1, &handle);
    glBindBuffer(type, handle);
    glBufferData(type, sizeInBytes, NULL, usage);

    if (!name.empty() && glObjectLabel) {
        glObjectLabel(GL_BUFFER, handle, -1, name.c_str());
    }
    if (OpenGLUtils::enableDebugOutput) {
        checkSize(handle, sizeInBytes);
    }
    glBindBuffer(type, 0);
}

void SL_Buffer::Respec(int newSizeInBytes)
{
    sizeInBytes = newSizeInBytes;
    if (stagingBuffer) {
        SL_DELETE[] stagingBuffer;
        stagingBuffer = 0;
    }
}

void SL_Buffer::DeInitBuffers(OpenGlStream* stream)
{
    // Invalidate the buffer to force the release of the memory
    stream->glBindBuffer(type, this);
    stream->glBufferData(type, 0, NULL, GL_DYNAMIC_DRAW);
    stream->glBindBuffer(type, (SL_Buffer*)0);

    stream->glDeleteBuffer(handle);
    handle = 0;
}

bool SL_Buffer::SyncToGPU(int offset, int sizeInBytes, void* pSrcData, OpenGlStream* stream, bool justColors)
{
    stream->glPseudoSync(this, offset, sizeInBytes, pSrcData, justColors);
    return true;
}

bool SL_Buffer::SyncToGPU(int offset, int _sizeInBytes, OpenGlStream* stream)
{
    SL_ASSERT(stagingBuffer);
    if (stream->HasNamedBufferSubData()) {
        if (handle == 0) {
            stream->glPseudoNamedBufferSubData(this, offset, _sizeInBytes, stagingBuffer + offset, false);
        } else {
            stream->glNamedBufferSubData(handle, offset, _sizeInBytes, stagingBuffer + offset, false);
        }
    } else {
        stream->glBindBuffer(type, this);
        stream->glBufferSubData(type, offset, _sizeInBytes, stagingBuffer + offset);
        stream->glBindBuffer(type, (SL_Buffer*)0);
    }
    stream->checkGlError(__LINE__);
    return true;
}

bool SL_Buffer::SyncToGPU(int _offset, int _sizeInBytes, void* pSrcData)
{
    if (glNamedBufferSubData) {
        glNamedBufferSubData(handle, _offset, _sizeInBytes, pSrcData);
    } else {
        glBindBuffer(type, handle);
        glBufferSubData(type, _offset, _sizeInBytes, pSrcData);
        glBindBuffer(type, 0);
    }

    return true;
}
void SL_Buffer::GetData(int _offset, int _sizeInBytes, void* pDstData)
{
    if (glGetNamedBufferSubData) {
        glGetNamedBufferSubData(handle, _offset, _sizeInBytes, pDstData);
    } else {
        glBindBuffer(type, handle);
        glGetBufferSubData(type, _offset, _sizeInBytes, pDstData);
        glBindBuffer(type, 0);
    }
}

void* SL_Buffer::Lock(int offset, int sizeInBytes)
{
    glBindBuffer(type, handle);
    void* p = glMapBuffer(type, GL_WRITE_ONLY);
    //SL_ASSERT(p);
    if (p == 0) {
        std::cout << "Could not lock" << std::endl;
    }
    OpenGLUtils::CheckError(__LINE__);
    locked = true;
    return p;
}
bool SL_Buffer::UnLock()
{
    SL_ASSERT(locked);

    glBindBuffer(type, handle);
    GLboolean ok = glUnmapBuffer(type);
    OpenGLUtils::CheckError(__LINE__);
    glBindBuffer(type, 0);
    locked = false;

    return ok == GL_TRUE;

}

void SL_Buffer::MakeBufferResident(bool makeResident)
{
    if (handle == 0) {
        std::cout << "handle == 0" << std::endl;
        return;
    }
    // make resident if not already
    if (makeResident) {
        if (glIsNamedBufferResidentNV(handle) == GL_FALSE) {
            glMakeNamedBufferResidentNV(handle, GL_READ_ONLY);
            SL_ASSERT(glIsNamedBufferResidentNV(handle) == GL_TRUE);
        }
    }
    // make non-resident if resident
    else {
        if (glIsNamedBufferResidentNV(handle) == GL_TRUE) {
            glMakeNamedBufferNonResidentNV(handle);
            SL_ASSERT(glIsNamedBufferResidentNV(handle) == GL_FALSE);
        }
    }
}
bool SL_Buffer::IsBufferResident(void) const
{
    return glIsNamedBufferResidentNV(handle) == GL_TRUE;
}
GLenum SL_Buffer::Type(void) const
{
    return type;
}
}