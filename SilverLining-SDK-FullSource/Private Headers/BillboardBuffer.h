// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

/** \file BillboardBuffer.h
*/

#pragma once

#include "SLList.h"
#include "ObjectListener.h"

namespace SilverLining
{
    class SharedVertexBuffer;
    class ThreadCameraStreamData;

    class BillboardBuffer
    {
    public:
        //! constructor
        BillboardBuffer();
    public:
        int vbIdx;
        SharedVertexBuffer* sharedVb;
        bool valid;
    };

    class BillboardBufferProvider : public ObjectListener<SharedVertexBuffer>
    {
    public:
        //! constructor
        BillboardBufferProvider(ThreadCameraStreamData* _tcsData);

        //! destructor
        ~BillboardBufferProvider();
    public:
        virtual BillboardBuffer GetABillboardBuffer(void) const;

        virtual void Deleted(const SharedVertexBuffer* object) const;

    private:
        mutable SL_LIST(SharedVertexBuffer*) vbStack;

        /** This method is called by the Billboard() constructor to allocate vertices
        from a collection of SharedVertexBuffers. This way, vertices are allocated from
        optimally-sized vertex buffer pools. This method manages the on-demand creation
        of new SharedVertexBuffer objects as they fill up, and also increments the
        reference count on the SharedVertexBuffer used.

        \param nVerts The number of vertices desired.
        \param vbIdx A reference to an unsigned int, that returns the index into the
        vertex buffer from which these vertices begin.
        \param vb A pointer to a vertex buffer pointer, which will return the VertexBuffer
        object that the requested vertices are within.
        \param ib A pointer to an index buffer pointer, which will return the IndexBuffer
        object that the requested vertices' indices are within.
        \param sib A pointer to a SharedVertexBuffer pointer, which will return the
        SharedVertexBuffer object that these vertices were allocated from.
        */
        bool GetVerts(int nVerts, int& vbIdx, SharedVertexBuffer **sib) const;

    private:
        ThreadCameraStreamData* tcsData;
    };
}