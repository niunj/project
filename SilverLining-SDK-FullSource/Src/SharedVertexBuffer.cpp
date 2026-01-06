// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SharedVertexBuffer.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Configuration.h"
#include "SLAssert.h"
#include "Renderer.h"
#include "Billboard.h"
#include "Atmosphere.h"

namespace SilverLining
{

std::atomic<int> SharedVertexBuffer::s_totalCount{0};

void SharedVertexBuffer::SetListener(const SharedVertexBufferListener* _listener)
{
    listener = _listener;
}

SharedVertexBuffer::SharedVertexBuffer(ThreadCameraStreamData* _tcsData) : vb(0), quadIndices(0), nextAvail(0), numReferences(0), listener(0)
    , tcsData(_tcsData)
{

    ++s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(c): Num of Shared Vertex Buffers: " << s_totalCount << std::endl;


    // Have vulkan have larger buffers
    // These helps allocation count limit on windows (408
    if (tcsData->GetRenderer()->GetType() == Atmosphere::VULKAN) {
        numVertices = 1000000;
    }
}

SharedVertexBuffer::~SharedVertexBuffer()
{
    SL_ASSERT(listener);
    if (listener) {
        listener->Deleted(this);
    }
    Clear();

    --s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(d): Num of Shared Vertex Buffers: " << s_totalCount << std::endl;

}

void SharedVertexBuffer::Clear()
{
    if (vb) {
        SL_DELETE vb;
    }

    vb = 0;

    if (quadIndices) {
        SL_DELETE quadIndices;
    }

    quadIndices = 0;
}

void SharedVertexBuffer::AddReference()
{
    numReferences++;
}

void SharedVertexBuffer::RemoveReference()
{
    numReferences--;

    if (numReferences <= 0) {

        Clear();
        SL_DELETE this;
    }
}

bool SharedVertexBuffer::Reserve(int nVerts, int& vertIdx)
{
    //static int totalVerts = 0;
    if (!vb) {

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "Billboard::SharedVertexBuffer::VertexBuffer" : (const char *)0;

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = true;
        bufferProperties.dynamic = true;

        // These are mappable (last parameter) for vulkan.
        // This is so that the buffer pointer can be directly gotten and vertices changed
        vb = SL_NEW VertexBuffer(numVertices, name, tcsData, bufferProperties);
        //totalVerts += vbSize;
        //printf("Total verts %d\n", totalVerts);
    }

    Renderer* renderer = tcsData->GetRenderer();

    if (!quadIndices && !renderer->HasQuads()) {
        SL_ASSERT(numVertices % 4 == 0);

        bool enableObjectLabeling = false;
        Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);
        const char* name = (enableObjectLabeling) ? "Billboard::SharedVertexBuffer::IndexBuffer" : (const char *)0;

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = true;
        bufferProperties.dynamic = true;

        quadIndices = SL_NEW IndexBuffer(numVertices, name, tcsData, bufferProperties);
        if (quadIndices && quadIndices->LockBuffer()) {
            // th changed ( use Index here instead of unsigned short, then we can quickly change datatype )
            Index *indices = quadIndices->GetIndices();
            if (indices) {
                for (int i = 0; i < numVertices; i += 4) {
                    indices[0] = (Index)(i + 3);
                    indices[1] = (Index)(i + 0);
                    indices[2] = (Index)(i + 2);
                    indices[3] = (Index)(i + 1);

                    indices += 4;
                }
            } else {
#ifdef _DEBUG
                printf("Couldn't get index buffer pointer.\n");
#endif
                return false;
            }
        } else {
#ifdef _DEBUG
            printf("Couldn't lock index buffer.\n");
#endif
            return false;
        }

        quadIndices->UnlockBuffer();
    }

    if ((int)nextAvail + nVerts < numVertices) {
        vertIdx = nextAvail;
        nextAvail += nVerts;
        return true;
    } else {
        vertIdx = -1;
        return false;
    }
}
}
