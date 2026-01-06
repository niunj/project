// Copyright (c) 2004-2020  Sundog Software, LLC All rights reserved worldwide.

/**
    \file Buffer.h
    \brief Classes that define a generic buffer (SSBO, UBO, etc)
 */

#ifndef BUFFER_H
#define BUFFER_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "IndexBuffer.h"
#include "Color.h"
#include "Vertex.h"

namespace SilverLining
{
enum BufferType
{
    SHADER_STORAGE_BUFFER = 0
,   UNIFORM_BUFFER
};

class Buffer : public MemObject
{
public:
    Buffer(const char* name, BufferType type, int size, const BufferProperties& _bufferProperties);

    virtual ~Buffer();

    virtual void* Lock(int offset, int sizeInBytes, void*) = 0;
    virtual void  Unlock(int offset, int sizeInBytes, void*) = 0;

    virtual void  Bind(int location, void*) = 0;
    virtual void  UnBind(int location, void*) = 0;

    virtual void DeInitBuffers(void*) = 0;
    virtual void Respec(int sizeInBytes, void*) = 0;
    virtual void SyncToGpu(int offset, int sizeInBytes, void* pSrcData, void*) = 0;

    virtual int Size(void) const
    {
        return size;
    }

protected:
    std::string name;
    BufferType type;
    int size;

    const BufferProperties bufferProperties;
private:
    Buffer()
    {
    }
};
}

#endif
