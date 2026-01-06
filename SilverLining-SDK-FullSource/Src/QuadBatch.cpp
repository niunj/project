// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "QuadBatch.h"
#include "ThreadCameraStreamData.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "Configuration.h"
#include "Renderer.h"
#include "SLAssert.h"
#include "PoolVertices.h"
#include "BillboardRenderer.h"

namespace SilverLining
{
QuadBatch::~QuadBatch()
{
    if (indexBuffer) {
        tcsData->GetBillboardRenderer()->ReturnAnIndexBuffer(indexBuffer);
    }
}

// th changed ( use Index here instead of unsigned short, then we can quickly change datatype )
QuadBatch::QuadBatch(const SL_VECTOR(Index)& indices
                     , VertexBuffer *pvb
                     , const SL_VECTOR(Color)& colors
                     , PoolVertices& poolVertices, ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
{
    vertexBuffer = pvb;
    indexBuffer = 0;

    if (vertexBuffer == 0) {
        return;
    }

    int nIndices = (int)indices.size();

    if (!(nIndices > 0)) {
        return;
    }

    enableObjectLabeling = false;
    Configuration::GetBoolValue("enable-renderer-object-names", enableObjectLabeling);

    const char* name = (enableObjectLabeling) ? "Billboard::QuadBatch::IndexBuffer" : (const char *)0;

    indexBuffer = tcsData->GetBillboardRenderer()->GetAnIndexBuffer(nIndices, name);
    SL_ASSERT(indexBuffer);

    bool ok = indexBuffer && indexBuffer->LockBuffer();

    if (!ok) {
        return;
    }
    // th changed ( use Index here instead of unsigned short, then we can quickly change datatype )
    Index *idx = indexBuffer->GetIndices();

    if (idx == 0) {
        return;
    }

    Index minIndex = indices[0];
    Index maxIndex = indices[0];

    int i;
    for (i = 0; i < nIndices; ++i) {
        minIndex = (indices[i] < minIndex) ? indices[i] : minIndex;
        maxIndex = (indices[i] > maxIndex) ? indices[i] : maxIndex;
    }

    const unsigned int numVertsPossiblyModified = maxIndex - minIndex + 1;

    Vertices stagingVertices = poolVertices.Get(numVertsPossiblyModified);
    SL_ASSERT(stagingVertices.vertices && stagingVertices.numVerts >= numVertsPossiblyModified);

    vertexBuffer->Get(minIndex, stagingVertices.vertices, numVertsPossiblyModified);

    for (i = 0; i < nIndices; i++) {
        idx[i] = indices[i];
        stagingVertices.vertices[indices[i] - minIndex].SetColor(colors[i]);
    }
    // we are just updating the colors in this case. so give that hint to the update
    vertexBuffer->Update(minIndex, stagingVertices.vertices, numVertsPossiblyModified, true);

    poolVertices.Return(stagingVertices);

    indexBuffer->UnlockBuffer();
}

void QuadBatch::Draw() const
{
    Renderer* renderer = tcsData->GetRenderer();

    if (vertexBuffer && indexBuffer) {
        renderer->DrawStrip(vertexBuffer->GetHandle(), indexBuffer->GetHandle(), 0, indexBuffer->GetNumIndices(), vertexBuffer->GetNumVertices(),
                            true, true);
    }
}
}
