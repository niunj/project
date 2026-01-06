// Copyright (c) 2004-2023  Sundog Software All rights reserved worldwide.

/**
    \file IndexBuffer.h
    \brief A buffer of indices into vertex buffers, uploaded to the GPU as a single entity.
 */
#ifndef INDEXBUFFER_H
#define INDEXBUFFER_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "BufferProperties.h"

#include <atomic>

namespace SilverLining
{
class ThreadCameraStreamData;
class Renderer;
/** A buffer of indices into a vertex buffer. This buffer is efficiently handled by the
   graphics hardware. */
class IndexBuffer : public MemObject
{
    friend class VertexBuffer;

public:
/** Constructor; construct it with the number of indices to allocate within this
   index buffer. */
    IndexBuffer(int nIndices, const char* name, ThreadCameraStreamData* _tcsData, const BufferProperties& _bufferProperties);

/** Virtual destructor. */
    virtual ~IndexBuffer();

/** The index buffer may not be accessed unless it is first locked. That is,
   any call to GetIndices() must be surrounded by calls to LockBuffer() and
   UnlockBuffer(). Lock the index buffer sparingly, as locking may have poor
   performance.

   \return True if the index buffer was successfully locked.
 */
    bool LockBuffer();

/** Retrieves a pointer to the array of indices this IndexBuffer contains, which will
   be of the size specified when the IndexBuffer was constructed. GetIndices() will
   succeed only if the buffer has been locked using the LockBuffer() method. As soon as
   you're done accessing or manipulating the indices, call UnlockBuffer(). Once
   UnlockBuffer() has been called, the Index pointer is no longer valid.

   \return A pointer to the indices this IndexBuffer contains, which may be read from
   or written to, or NULL if the indices could not be obtained (perhaps the buffer
   wasn't locked?)
 */
    Index *GetIndices() const;

/** Unlocks the IndexBuffer such that its indices may again be processed efficiently
   by the graphics hardware. Any pointer previously obtained by GetIndices() will
   be invalid once UnlockBuffer has been called. UnlockBuffer() should balance an earlier
   call to LockBuffer(), both of which surround a call to GetIndices() during which
   the buffer's indices may be retrieved or written to.

   \return True if the operation succeeded.
 */
    bool UnlockBuffer();

/** Returns a handle to this index buffer, which may be required for passing into
   VertexBuffer and Renderer methods. */
    IndexBufferHandle GetHandle() const {
        return internalHandle;
    }

/** Retrieves the number of indices this IndexBuffer contains. */
    int GetNumIndices() const {
        return numIndices;
    }

    void ReInit(int newNumIndices, const char* name);
    void DeInit(void);
private:
    IndexBufferHandle internalHandle;
    int numIndices;
    ThreadCameraStreamData* tcsData;
    Renderer* renderer;
    
    const BufferProperties bufferProperties;

    static std::atomic<int> s_totalCount;
};
}

#endif
