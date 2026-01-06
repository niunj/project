// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved worldwide

#include "IndexBuffer.h"
#include "Renderer.h"
#include "SLAssert.h"

#include <iostream>

using namespace SilverLining;

std::atomic<int> IndexBuffer::s_totalCount{0};

IndexBuffer::IndexBuffer(int _numIndices, const char* _name, ThreadCameraStreamData* _tcsData, const BufferProperties& _bufferProperties)
    : tcsData(_tcsData)
    , numIndices(0)
    , internalHandle(0)
    , bufferProperties(_bufferProperties)
{
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
    if ((tcsData->GetIsDrawingObjects() || tcsData->GetIsDrawingSky()) && tcsData->GetIsExecutingCloudCompileOperations() == false) {
        std::cout << "index buffer allocation during rendering!!" << std::endl;
    }
#endif
    // cache it, to avoid repeated gets
    renderer = tcsData->GetRenderer();

    ReInit(_numIndices, _name);

    ++s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(c): Num of Index Buffers: " << s_totalCount << std::endl;
}

void IndexBuffer::ReInit(int newNumIndices, const char* name)
{
    SL_ASSERT(internalHandle == 0 && numIndices==0);

    numIndices = newNumIndices;
    internalHandle = renderer->AllocateIndexBuffer(numIndices, name, bufferProperties);
}
void IndexBuffer::DeInit(void)
{
    if (internalHandle) {
        renderer->ReleaseIndexBuffer(internalHandle);
    }
    numIndices = 0;
    internalHandle = 0;
}

IndexBuffer::~IndexBuffer()
{
    --s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(d): Num of Index Buffers: " << s_totalCount << std::endl;
    DeInit();
}

bool IndexBuffer::LockBuffer()
{
    return renderer->LockIndexBuffer(internalHandle);
}

Index *IndexBuffer::GetIndices() const
{
    return renderer->GetIndices(internalHandle);
}

bool IndexBuffer::UnlockBuffer()
{
    return renderer->UnlockIndexBuffer(internalHandle);
}
