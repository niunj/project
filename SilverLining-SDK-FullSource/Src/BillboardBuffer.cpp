// Copyright (c) 2004-2018 Sundog Software, LLC All rights reserved worldwide.

#include "BillboardBuffer.h"
#include "SharedVertexBuffer.h"
#include "SLAssert.h"
#include <iostream>

namespace SilverLining
{
BillboardBuffer BillboardBufferProvider::GetABillboardBuffer(void) const
{
    BillboardBuffer buffer;
    GetVerts(4, buffer.vbIdx, &(buffer.sharedVb));
    return buffer;
}

BillboardBuffer::BillboardBuffer()
{
    vbIdx = -1;
    sharedVb = 0;
}

BillboardBufferProvider::BillboardBufferProvider(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{

}
BillboardBufferProvider::~BillboardBufferProvider()
{

}

bool BillboardBufferProvider::GetVerts(int nVerts, int& vbIdx, SharedVertexBuffer **sib) const
{
    if (vbStack.empty()) {
        SharedVertexBuffer *svb = SL_NEW SharedVertexBuffer(tcsData);
        svb->SetListener(this);
        vbStack.push_back(svb);
    }

    SharedVertexBuffer *svb = vbStack.back();

    bool ok = svb->Reserve(nVerts, vbIdx);

    if (ok == false) {

        SharedVertexBuffer *newSvb = SL_NEW SharedVertexBuffer(tcsData);
        newSvb->SetListener(this);
        vbStack.push_back(newSvb);
        svb = vbStack.back();

        ok = svb->Reserve(nVerts, vbIdx);
    }

    *sib = vbStack.back();

    if (ok) {
        SL_ASSERT(vbIdx >= 0 && (*sib)->GetVertexBuffer());
    }

    return ok;
}


void BillboardBufferProvider::Deleted(const SharedVertexBuffer* object) const
{
    bool deleted = false;
    for (SL_LIST(SharedVertexBuffer*)::iterator it = vbStack.begin(); it != vbStack.end(); ++it) {
        if (*it == object) {
            vbStack.erase(it);
            deleted = true;
            break;
        }
    }
    SL_ASSERT(deleted);
}
}