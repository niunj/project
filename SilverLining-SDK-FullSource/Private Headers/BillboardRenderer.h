// Copyright 2006-2023 Sundog Software, LLC. All rights reserved worldwide.

/** \file BillboardRenderer.h
\brief A class for describing and rendering of billboards in particular thread and associated camera, stream
*/

#pragma once

#ifdef SWIG
%module SilverLiningBillboardRenderer
#define SILVERLINING_API
%{
#include "BillboardRenderer.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"
#include "Color.h"
#include "SilverLiningTypes.h"
#include "BillboardQuad.h"
#include "SLVector.h"
#include "SLList.h"
#include "DrawIndirectCommands.h"
#include "BillboardShader.h"

namespace SilverLining
{
    class Camera;
    class OverriddenBillboardMatrix;
    class ThreadCameraStreamData;
    class BillboardShader;

    class QuadBatches;
    class QuadBatch;
    class VertexBuffer;
    class PoolVertices;

    class IndexBuffer;
    class VertexBuffer;

    class Buffer;
    class TextureManager;

    class BillboardRenderer
    {
    public:
        BillboardRenderer(ThreadCameraStreamData* _tcsData);
        virtual ~BillboardRenderer();
    public:
        /** Begin drawing a series of Billboards that all share the same texture. A call to
        StartBatchDraw() should precede a series of calls to DrawInBatch() calls, which
        then must be followed by a call to EndBatchDraw(). Drawing billboards in this
        manner is much more efficient than calling Draw() on individual Billboard objects.

        As a further optimization, batches of billboards will create a QuadBatches object
        returned with the call to EndBatchDraw(). Calling QuadBatches::Draw() is even
        faster than drawing batches of individual billboards.

        \param tex The texture to draw the billboards with.
        \param fade The "alpha" value for this billboard. 0 is transparent, 1 is fully visible.
        \param objectSpace whether the vertices of the billboard are in object or world space.

        \return true if the operation completed successfully.
        */
        void StartBatchDraw(TextureHandle tex, double fade, const Vector3& outputScale, bool objectSpace, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix* overriddenBillboardMatrix, bool& savedFog);

        /** Signals that you're done drawing a bunch of billboards using DrawInBatch().
        Cleans up any shaders and other state that was set up to draw the billboards.
        Calls to DrawInBatch() must be surrounded by calls to StartBatchDraw()
        and EndBatchDraw().

        \return A QuadBatches object that can be stored to draw this batch again,
        even more quickly. If this group of billboards doesn't change, then you may
        re-use the QuadBatches object to keep drawing it at optimal speed. The caller
        is responsible for the deletion of this object. You may use the
        QuadBatches::Invalidate() method to help you manage when this object is no longer
        valid and must be deleted, and another cycle of StartBatchDraw() /
        DrawInBatch() / EndBatchDraw() is required to generate a fresh QuadBatches
        object.
        */
        QuadBatches* EndBatchDraw(const Camera* renderCamera, bool savedFog, bool actuallyDraw, QuadBatches *existingQB);

        void AddBillboardQuad(const BillboardQuad& billboardQuad);

        //! Get an index buffer from a pool of index buffers
        IndexBuffer* GetAnIndexBuffer(int numIndices, const char* name);

        //! Return an index buffer from a pool of index buffers
        void ReturnAnIndexBuffer(IndexBuffer* indexBuffer);

        //! Get an vertex buffer from a pool of vertex buffers
        VertexBuffer* GetAVertexBuffer(int numberOfVertices, const char* name);

        //! Return a vertex buffer from a pool of vertex buffers
        void ReturnAVertexBuffer(VertexBuffer* vertexBuffer);

        //! get the params buffer for the billboards (this is an SSBO/UBO)
        Buffer* GetBillboardParamsBuffer(void) { return billboardParamsBuffer; }

        //! get the command buffer for the billboards
        std::vector<DrawElementsIndirectBindlessCommandNV>& GetBillboardCommandBuffer(void) { return billboardCommandBuffer; }

        //! get the params vector for the billboards
        std::vector<BillboardParams>& GetBillboardParamsVector(void) { return billboardParamsVector; }

        //! Increment the number of max instances by this much.
        //! This will resize the params buffer and the command buffer, if needed
        void incrementMaxInstancedBillboardsBy(int additionalSize);

        //! update the UBO for the bindless textures for bindless rendering
        void UpdateTexturesBufferIfNecessary(const TextureManager* textureManager);

        //! clear the UBO for the bindless textures for bindless rendering
        void ClearTexturesBuffer(void);

        //! texture index in the ubo, for the given atlas
        int textureIndex(TextureHandle texture) const;

        //! Get the textures buffer const
        const Buffer* GetTexturesBuffer(void) const;

        //! Get the textures buffer non-const
        Buffer* GetTexturesBuffer(void);

    protected:
        QuadBatches* DrawQuadBatches(bool actuallyDraw, QuadBatches *existingQB);

    protected:
        SL_LAZY<SL_VECTOR(BillboardQuad)> billboardQuads;
        PoolVertices* poolVertices;

        SL_DEQUE(IndexBuffer*) indexBuffersPool;

        SL_DEQUE(VertexBuffer*) vertexBuffersPool;

        // th changed (avoid new/delete)
        SL_VECTOR(Index) indices;
        SL_VECTOR(Color) colors;

        ThreadCameraStreamData* tcsData;

        Buffer* billboardParamsBuffer;
        std::vector<DrawElementsIndirectBindlessCommandNV> billboardCommandBuffer;
        std::vector<BillboardParams> billboardParamsVector;

        Buffer* texturesBuffer;
        SL_MAP(TextureHandle, int) mapTexturesToIndex;
    };

}