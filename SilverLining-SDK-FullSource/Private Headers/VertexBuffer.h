// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved worldwide.

/**
    \file VertexBuffer.h
    \brief Classes that define a Vertex and buffers of vertices that are rendered efficiently.
 */

#ifndef VERTEXBUFFER_H
#define VERTEXBUFFER_H

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "IndexBuffer.h"
#include "Color.h"
#include "Vertex.h"
#include "BufferProperties.h"

#include <atomic>

namespace SilverLining
{
class ThreadCameraStreamData;
class Renderer;
/** An array of vertices that must be locked to be access directly. While unlocked, it may
   reside on the graphics hardware for efficient use while rendering. */
class VertexBuffer : public MemObject
{
public:
/** Constructs a VertexBuffer that contains nVertices Vertex objects. */
    VertexBuffer(int nVertices, const char* name, ThreadCameraStreamData* tcsData, const BufferProperties& _bufferProperties);

/** Destroys the VertexBuffer and releases its resources. */
    virtual ~VertexBuffer();

/** Renders a triangle strip with this VertexBuffer using the indices in the
   IndexBuffer provided. */
    bool DrawIndexedStrip(const IndexBuffer& ib, bool useVertexColors = true);

/** Locks the VertexBuffer, so that its vertices may be read from and written
   to directly. This permits use of the GetVertices() method.
   \return True if the buffer was successfully locked.
 */
    bool LockBuffer();

/** Obtains a pointer to the vertices within this VertexBuffer; this will succeed
   only if the buffer has been successfully locked by calling LockBuffer(). Otherwise,
   it will return NULL. Once finished accessing the vertices, call UnlockBuffer() to
   allow these vertices to be re-uploaded to the graphics hardware for rendering. */
    Vertex *GetVertices() const;

/** Unlocks a buffer such that any pointer to its vertices obtained by GetVertices()
   is no longer valid. Once unlocked, the VertexBuffer may be uploaded to the graphics
   hardware for efficient rendering.
   \return True if the buffer was successfully unlocked.
 */
    bool UnlockBuffer();

/** Efficiently retrieves a copy of a range of vertices inside this buffer.
   \param offset The vertex within the buffer to start retrieving from.
   \param verts A pointer to an array of Vertex objects that will receive the copied
   vertex information. This array must be at least of size nVerts * sizeof(Vertex).
   \param nVerts The number of vertices to receive beginning with the offset
   specified.
   \return True if the operation succeeded.
 */
    bool Get(int offset, Vertex* verts, int nVerts);

/** Efficiently modifies a range of the vertices within the buffer.
   \param offset The vertex within the buffer to start modifying from.
   \param verts A pointer to an array of Vertex objects that will be copied into
   the VertexBuffer. This array must be at least of size nVerts * sizeof(Vertex).
   \param nVerts The number of vertices, from offset, that will be copied in.
   \param nVerts The number of vertices, from offset, that will be copied in.
   \return justColors Use incoming verts' colors only for the update
 */
    bool Update(int offset, Vertex* verts, int nVerts, bool justColors);

/** Returns a handle to refer to this object by. */
    VertexBufferHandle GetHandle() const {
        return internalHandle;
    }

/** Returns the number of vertices this VertexBuffer was constructed with. */
    int GetNumVertices() const {
        return numVertices;
    }

    void ReInit(int newNumVertices, const char* name);
    void DeInit(void);

    //! Only for vulkan.
    //! Similar to mapped ubos/ssbos that can be changed/bound at a given offset.
    bool IncrementOffsetIndex();
private:
    VertexBufferHandle internalHandle;
    int numVertices;

    ThreadCameraStreamData* tcsData;
    Renderer* renderer;

    const BufferProperties bufferProperties;

    static std::atomic<int> s_totalCount;
};
}

#endif
