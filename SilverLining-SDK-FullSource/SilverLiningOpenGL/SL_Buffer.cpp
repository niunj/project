// Copyright (c) 2011-2021 Sundog Software, LLC. All rights reserved worldwide.

#include "SL_Buffer.h"
#include "SLAssert.h"
#include "OpenGLUtils.h"
#include "OpenGLStream.h"
#include "Vertex.h"

#include <iostream>
#include <cstring>

namespace SilverLining
{

SL_Buffer::SL_Buffer(GLenum _type, int _sizeInBytes, GLenum _usage, const char* _name, const BufferProperties& _bufferProperties, bool _bindless)
    : type(_type)
    , sizeInBytes(_sizeInBytes)
    , usage(_usage)
    , bufferProperties(_bufferProperties)
    , handle(0)
    , address(0)
    , stagingBuffer(0)
    , bindless(_bindless)
    , locked(false)
{
    if (_name) {
        name = SL_STRING(name);
    }
    if (bufferProperties.genBuffer) {
        InitBuffers();
    }
}

SL_Buffer::~SL_Buffer()
{
    if (handle) {
        // Invalidate the buffer to force the release of the memory
        glBindBufferARB(type, handle);
        glBufferDataARB(type, 0, NULL, GL_DYNAMIC_DRAW_ARB);
        glBindBufferARB(type, 0);

        glDeleteBuffersARB(1, &handle);
    }

    if (stagingBuffer) {
        SL_DELETE[] stagingBuffer;
    }
}

GLubyte* SL_Buffer::GetStagingBuffer(void)
{
    if (stagingBuffer==0) {
        stagingBuffer = SL_NEW GLubyte[sizeInBytes];
        memset(stagingBuffer, 0, sizeInBytes);
    }
    return stagingBuffer;
}

int SL_Buffer::GetSizeInBytes(void) const
{
    return (int)sizeInBytes;
}

void SL_Buffer::InitBuffers()
{
    SL_ASSERT(handle == 0);
    glGenBuffersARB(1, &handle);
    if (handle == 0) {
        std::cout << "could not generate buffer name" << std::endl;
        return;
    }
    glBindBufferARB(type, handle);
    glBufferDataARB(type, sizeInBytes, NULL, usage);

    if (bindless && type == GL_ARRAY_BUFFER_ARB) {
        SL_ASSERT(address == 0);
        glGetBufferParameterui64vNV(GL_ARRAY_BUFFER, GL_BUFFER_GPU_ADDRESS_NV, &(address));
        glMakeBufferResidentNV(GL_ARRAY_BUFFER, GL_READ_ONLY);
#ifdef FLOATING_POINT_COLOR
        glColorFormatNV(4, GL_FLOAT, sizeof(Vertex));
#else
        glColorFormatNV(4, GL_UNSIGNED_BYTE, sizeof(Vertex));
#endif
        glVertexFormatNV(4, GL_FLOAT, sizeof(Vertex));

#ifdef HALF_FLOAT_TEXCOORDS
        glTexCoordFormatNV(4, GL_HALF_FLOAT, sizeof(Vertex));
#else
        glTexCoordFormatNV(4, GL_FLOAT, sizeof(Vertex));
#endif
    }

    if (!name.empty() && glObjectLabel) {
        glObjectLabel(GL_BUFFER, handle, -1, name.c_str());
    }
    glBindBufferARB(type, 0);
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
    stream->glBindBufferARB(type, this);
    stream->glBufferDataARB(type, 0, NULL, GL_DYNAMIC_DRAW_ARB);
    stream->glBindBufferARB(type, (SL_Buffer*)0);

    stream->glDeleteBufferARB(handle);
    handle = 0;

    address = 0;
}

bool SL_Buffer::SyncToGPU(int offset, int sizeInBytes, void* pSrcData, OpenGlStream* stream, bool justColors)
{
    stream->glPseudoSync(this, offset, sizeInBytes, pSrcData, justColors);
    return true;
}

bool SL_Buffer::SyncToGPU(int offset, int _sizeInBytes, OpenGlStream* stream)
{
    if (stream->HasNamedBufferSubData()) {
        if (handle == 0) {
            stream->glPseudoNamedBufferSubData(this, offset, _sizeInBytes, stagingBuffer + offset, false);
        } else {
            stream->glNamedBufferSubData(handle, offset, _sizeInBytes, stagingBuffer + offset, false);
        }
    } else {
        SL_ASSERT(offset + _sizeInBytes <= sizeInBytes);
        if (offset + _sizeInBytes > sizeInBytes) {
            std::cout << "(offset + _sizeInBytes > sizeInBytes)" << std::endl;
            return false;
        }
        SL_ASSERT(stagingBuffer != 0);
        if (stagingBuffer == 0) {
            std::cout << "(stagingBuffer==0)" << std::endl;
            return false;
        }
        if (handle == 0) {
            stream->glPseudoBindBufferARB(type, this);
        } else {
            stream->glBindBufferARB(type, this);
        }
        stream->glBufferSubDataARB(type, offset, _sizeInBytes, stagingBuffer + offset);

// PPP: is this actually needed? remove after testing
#if 0
        if (bindless && type == GL_ARRAY_BUFFER_ARB) {
            glGetBufferParameterui64vNV(GL_ARRAY_BUFFER, GL_BUFFER_GPU_ADDRESS_NV, &(address));
        }
#endif
        stream->glBindBufferARB(type, (SL_Buffer*)0);

    }
    return true;
}

bool SL_Buffer::SyncToGPU(int _offset, int _sizeInBytes, void* pSrcData)
{
    if (glNamedBufferSubData) {
        glNamedBufferSubData(handle, _offset, _sizeInBytes, pSrcData);
    } else {
        glBindBufferARB(type, handle);
        glBufferSubDataARB(type, _offset, _sizeInBytes, pSrcData);
        glBindBufferARB(type, 0);
    }

    return true;
}
void SL_Buffer::GetData(int _offset, int _sizeInBytes, void* pDstData)
{
    if (glGetNamedBufferSubData) {
        glGetNamedBufferSubData(handle, _offset, _sizeInBytes, pDstData);
    } else {
        glBindBufferARB(type, handle);
        glGetBufferSubDataARB(type, _offset, _sizeInBytes, pDstData);
        glBindBufferARB(type, 0);
    }
}

void* SL_Buffer::Lock(int offset, int sizeInBytes)
{
    glBindBufferARB(type, handle);
    void* p = glMapBufferARB(type, GL_WRITE_ONLY);
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

    glBindBufferARB(type, handle);
    GLboolean ok = glUnmapBufferARB(type);
    OpenGLUtils::CheckError(__LINE__);
    glBindBufferARB(type, 0);
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