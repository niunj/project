// Copyright (c) 2004-2018  Sundog Software, LLC. All rights reserved worldwide.

/** \file SharedVertexBuffer.h
*/

#include "MemAlloc.h"
#include "SLMap.h"
#include "SLList.h"
#include "ObjectListener.h"

#include <atomic>

namespace SilverLining
{
    class VertexBuffer;
    class IndexBuffer;
    class Atmosphere;
    class ThreadCameraStreamData;

    /** A large pool of vertices and indices (to minimize state changes) from which
    vertices for individual quads are vended. */
    class SharedVertexBuffer : public MemObject
    {
    public:
        /** Default constructor. */
        SharedVertexBuffer(ThreadCameraStreamData* _tcsData);


        /** The destructor deletes this object's vertex and index buffer objects. */
        ~SharedVertexBuffer();

        /** Allocate a given number of vertices from this SharedVertexBuffer.

        \param nVerts The number of vertices you desire.
        \param vertIdx A reference to an int. If successful, this will be the
        index into the object's vertex buffer from which your nVerts vertices
        begin.
        \return true if nVerts vertices were successfully allocated from this
        SharedVertexBuffer; false if the SharedVertexBuffer ran out of vertices.
        If false is returned, then you'll need to create a new SharedVertexBuffer
        to create more vertices.
        */
        bool Reserve(int nVerts, int& vertIdx);

        /** Destroys all attached vertex and index buffers. They will be recreated
        if necessary by future Reserve() calls.*/
        void Clear();

        /** Returns a pointer to this object's vertex buffer. Do not delete this pointer. */
        VertexBuffer *GetVertexBuffer() const {
            return vb;
        }

        /** Returns a pointer to this object's index buffer. Do not delete this pointer. */
        IndexBuffer *GetIndexBuffer() const {
            return quadIndices;
        }

        /** When creating an object that consumes vertices from this SharedVertexBuffer
        object, call AddReference() to increment its reference count. */
        void AddReference();

        /** When destroying an object that consumes vertices from this SharedVertexBuffer
        object, call RemoveReference() to decrement its reference count. When the
        reference count reaches zero, the SharedVertexBuffer will self-destruct. */
        void RemoveReference();

        typedef ObjectListener<SharedVertexBuffer> SharedVertexBufferListener;

        void SetListener(const SharedVertexBufferListener* listener);

    private:
        SharedVertexBuffer();

        SharedVertexBuffer(const SharedVertexBuffer& svb);
        
    private:
        VertexBuffer *vb;
        IndexBuffer *quadIndices;
        unsigned int nextAvail;
        unsigned int numReferences;

        const SharedVertexBufferListener* listener;
        ThreadCameraStreamData* tcsData;

        //! Optimal number of vertices per vertex buffer object. May vary depending on your
        //! target hardware platform.
        int numVertices = 10000;

        static std::atomic<int> s_totalCount;
    };
}