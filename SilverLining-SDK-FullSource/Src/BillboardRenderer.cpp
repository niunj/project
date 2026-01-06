// Copyright (c) 2004-2023  Sundog Software, LLC. All rights reserved worldwide.

#include "BillboardRenderer.h"
#include "BillboardShader.h"
#include "Renderer.h"
#include "QuadBatches.h"
#include "QuadBatch.h"
#include "IndexBuffer.h"
#include "ThreadCameraStreamData.h"
#include "TextureManager.h"
#include "SLAssert.h"

namespace SilverLining
{
BillboardRenderer::BillboardRenderer(ThreadCameraStreamData* _tcsData)
    : tcsData(_tcsData)
    , billboardParamsBuffer(0)
    , texturesBuffer(0)
{
    poolVertices = SL_NEW PoolVertices();
}

BillboardRenderer::~BillboardRenderer()
{
    for (unsigned int i = 0; i < indexBuffersPool.size(); ++i) {
        IndexBuffer* indexBuffer = indexBuffersPool.at(i);
        SL_DELETE indexBuffer;
    }

    for (unsigned int i = 0; i < vertexBuffersPool.size(); ++i) {
        VertexBuffer* vertexBuffer = vertexBuffersPool.at(i);
        SL_DELETE vertexBuffer;
    }

    billboardQuads.Get().clear();
    SL_DELETE poolVertices;

    if (billboardParamsBuffer) {
        tcsData->GetRenderer()->DestroyBuffer(billboardParamsBuffer);
    }
}

void BillboardRenderer::StartBatchDraw(TextureHandle tex, double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix* overriddenBillboardMatrix, bool& savedFog)
{
    Renderer* renderer = tcsData->GetRenderer();
    if (renderer == 0) {
        return;
    }

    renderer->EnableBackfaceCulling(false);

    billboardQuads.Get().clear();

    renderer->EnableTexture(tex, 0);

    const BillboardShader* billboardShader = tcsData->GetBillboardShader();
    BillboardShadingUtilities shadingUtils(billboardShader->GetShaderHandle(), billboardShader->GetConstantLocations(), tcsData);
    if (renderCamera && overriddenBillboardMatrix) {
        shadingUtils.BindAndSaveFogAndSetupConstants(fade, outputScale, objectSpace, millis, renderCamera, *overriddenBillboardMatrix, savedFog, false);
    }
}

QuadBatches* BillboardRenderer::EndBatchDraw(const Camera* renderCamera, bool savedFog, bool actuallyDraw, QuadBatches *existingQB)
{
    Renderer* renderer = tcsData->GetRenderer();

    renderer->EnableBackfaceCulling(false);

    QuadBatches *qbs = 0;

    const BillboardShader* billboardShader = tcsData->GetBillboardShader();

    if (billboardShader) {
        qbs = DrawQuadBatches(actuallyDraw, existingQB);

        BillboardShadingUtilities shadingUtils(billboardShader->GetShaderHandle(), billboardShader->GetConstantLocations(), tcsData);
        shadingUtils.UnbindAndRestoreFog(savedFog);
    }

    if (actuallyDraw) {
        if (renderer->DisableTexture(0)) {
            if (renderer->DisableBlending()) {
                return qbs;
            }
        }
    }

    return qbs;
}

QuadBatches* BillboardRenderer::DrawQuadBatches(bool actuallyDraw, QuadBatches *existingQB)
{
    Renderer* renderer = tcsData->GetRenderer();
    renderer->SetCurrentColor(Color(1, 1, 1, 1));

    indices.clear();
    colors.clear();

    VertexBuffer* lastVb = 0;

    QuadBatches* quadBatches = existingQB ? existingQB : SL_NEW QuadBatches();
    quadBatches->Clear();

    // Stitch quads together with degenerate tris.
    for (SL_VECTOR(BillboardQuad) ::iterator quadIt = billboardQuads.Get().begin(); quadIt != billboardQuads.Get().end(); quadIt++) {
        if ((*quadIt).vb != lastVb) {
            if (lastVb != 0) {
                // Vertex buffer changed, draw everything so far and start over.
                QuadBatch* quadBatch = SL_NEW QuadBatch(indices, lastVb, colors, *poolVertices, tcsData);
                if (actuallyDraw) {
                    quadBatch->Draw();
                }
                quadBatches->Add(quadBatch);
            }

            lastVb = (*quadIt).vb;
            indices.clear();
            colors.clear();
        }

        indices.push_back((Index)((*quadIt).index + 3));
        indices.push_back((Index)((*quadIt).index + 3));
        indices.push_back((Index)((*quadIt).index + 2));
        indices.push_back((Index)((*quadIt).index + 0));
        indices.push_back((Index)((*quadIt).index + 1));
        indices.push_back((Index)((*quadIt).index + 1));

        for (int i = 0; i < 6; i++) {
            colors.push_back((*quadIt).color);
        }
    }

    // Draw last batch.

    QuadBatch* quadBatch = SL_NEW QuadBatch(indices, lastVb, colors, *poolVertices, tcsData);
    if (actuallyDraw) {
        quadBatch->Draw();
    }
    quadBatches->Add(quadBatch);

    indices.clear();
    colors.clear();

    return quadBatches;
}

void BillboardRenderer::AddBillboardQuad(const BillboardQuad& billboardQuad)
{
    billboardQuads.Get().push_back(billboardQuad);
}

IndexBuffer* BillboardRenderer::GetAnIndexBuffer(int nIndices, const char* name)
{
    if (indexBuffersPool.empty()) {

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = false;
        bufferProperties.dynamic = true;

        return SL_NEW IndexBuffer(nIndices, name, tcsData, bufferProperties);
    }
    IndexBuffer* indexBuffer = indexBuffersPool.front();
    indexBuffersPool.pop_front();

    indexBuffer->ReInit(nIndices, name);
    return indexBuffer;
}

void BillboardRenderer::ReturnAnIndexBuffer(IndexBuffer* indexBuffer)
{
    indexBuffer->DeInit();
    indexBuffersPool.push_back(indexBuffer);
}


VertexBuffer* BillboardRenderer::GetAVertexBuffer(int numVertices, const char* name)
{
    if (vertexBuffersPool.empty()) {

        BufferProperties bufferProperties;
        bufferProperties.genBuffer = false;
        bufferProperties.dynamic = true;

        return SL_NEW VertexBuffer(numVertices, name, tcsData, bufferProperties);
    }
    VertexBuffer* vertexBuffer = vertexBuffersPool.front();
    vertexBuffersPool.pop_front();

    vertexBuffer->ReInit(numVertices, name);
    return vertexBuffer;
}

void BillboardRenderer::ReturnAVertexBuffer(VertexBuffer* vertexBuffer)
{
    vertexBuffer->DeInit();
    vertexBuffersPool.push_back(vertexBuffer);
}

void BillboardRenderer::incrementMaxInstancedBillboardsBy(int additionalSize)
{
    if (billboardParamsBuffer == 0) {
        SilverLining::BufferProperties bufferProperties;
        bufferProperties.useStagingBuffer = false;
        bufferProperties.useMapUnmap = false;
        billboardParamsBuffer = tcsData->GetRenderer()->CreateBuffer("billboard_param_buffer", SHADER_STORAGE_BUFFER, additionalSize*sizeof(BillboardParams), bufferProperties);
    } else {
        int currentSizeInBytes = billboardParamsBuffer->Size();
        int newSizeInBytes = currentSizeInBytes + additionalSize*sizeof(BillboardParams);
        if (newSizeInBytes > currentSizeInBytes) {
            billboardParamsBuffer->DeInitBuffers(tcsData->GetRenderer()->GetContext());
            billboardParamsBuffer->Respec(newSizeInBytes, tcsData->GetRenderer()->GetContext());
        }
    }

    int currentCapacity = (int)billboardCommandBuffer.capacity();
    int newCapacity = currentCapacity + additionalSize;
    if (newCapacity > currentCapacity) {
        billboardCommandBuffer.reserve(additionalSize + billboardCommandBuffer.capacity());
        billboardParamsVector.reserve(additionalSize + billboardCommandBuffer.capacity());
    }

    SL_ASSERT(billboardParamsBuffer->Size()/sizeof(BillboardParams) == billboardCommandBuffer.capacity());
}

int BillboardRenderer::textureIndex(TextureHandle texture) const
{
    SL_MAP(TextureHandle, int)::const_iterator it = mapTexturesToIndex.find(texture);
    if (it == mapTexturesToIndex.end()) {
        std::cout << "it == mapTexturesToIndex.end()" << std::endl;
        return 0;
    }
    return it->second;
}

const Buffer* BillboardRenderer::GetTexturesBuffer(void) const
{
    return texturesBuffer;
}

Buffer* BillboardRenderer::GetTexturesBuffer(void)
{
    return texturesBuffer;
}

void BillboardRenderer::UpdateTexturesBufferIfNecessary(const TextureManager* textureManager)
{
    const SL_VECTOR(TextureHandle)& namedAtlasTexturesVector = textureManager->GetNamedAtlasTexturesVector();

    const int numTextures = (int)namedAtlasTexturesVector.size() + 2; // +2 for the default atlas, wisp texture
    const int sizeRequired = numTextures * sizeof(GPUAddressType);

    Renderer* renderer = tcsData->GetRenderer();

    if (texturesBuffer && texturesBuffer->Size() != sizeRequired) {
        renderer->DestroyBuffer(texturesBuffer);
        texturesBuffer = 0;
    }

    if (texturesBuffer) {
        return;
    }

    mapTexturesToIndex.clear();

    SilverLining::BufferProperties bufferProperties;
    bufferProperties.useStagingBuffer = false;
    bufferProperties.useMapUnmap = true;
    texturesBuffer = renderer->CreateBuffer("textures_buffer", UNIFORM_BUFFER, sizeRequired, bufferProperties);
    void* context = renderer->GetContext();

    GPUAddressType* data = static_cast<GPUAddressType*>(texturesBuffer->Lock(0, sizeRequired, context));
    for (int i = 0; i < numTextures; ++i) {

        TextureHandle textureHandle = 0;
        if (i == 0) {
            textureHandle = textureManager->GetAtlasTexture();
        } else if (i == 1) {
            textureHandle = textureManager->GetWispTexture();
        } else {
            textureHandle = namedAtlasTexturesVector[i - 2];
        }
        if (textureHandle == 0) {
            std::cout << "textureHandle == 0!" << std::endl;
            continue;
        }

        GPUAddressType address = renderer->GetTextureAddress(textureHandle);
        renderer->MakeTextureAddressResident(address);

        data[i] = address;

        mapTexturesToIndex[textureHandle] = i;
    }
    texturesBuffer->Unlock(0, sizeRequired, context);
}

void BillboardRenderer::ClearTexturesBuffer(void)
{
    if (texturesBuffer && tcsData && tcsData->GetRenderer()) {
        tcsData->GetRenderer()->DestroyBuffer(texturesBuffer);
        texturesBuffer = 0;
    }
}

}