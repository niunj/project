// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file QuadBatch.h
\brief A collection of billboard quads drawn as a single triangle strip
*/

#pragma once

#ifdef SWIG
%module SilverLiningQuadBatch
#define SILVERLINING_API
%{
#include "QuadBatch.h"
    using namespace SilverLining;
    % }
#endif

#include "SLVector.h"
#include "SilverLiningTypes.h"
#include "PoolVertices.h"

namespace SilverLining
{
    class VertexBuffer;
    class IndexBuffer;
    class ThreadCameraStreamData;
    class Color;
    
    /** A collection of billboard quads drawn as a single triangle strip. */
    class QuadBatch : public MemObject
    {
    public:
        /** Destructor; cleans up the index buffer associated with this
        object. */
        virtual ~QuadBatch();

        /** Constructor; takes a vector of indices into the vertex buffer object
        provided, and vertex colors associated with each index.

        \param indices A vector of indices into the vertex buffer to be drawn
        as a triangle strip.
        \param pvb A pointer to a VertexBuffer object containing vertex data
        for the triangle strip to be drawn. This QuadBatch object
        will not delete this pointer.
        \param colors A vector of colors that matches each index passed in via
        the indices parameter.
        */
        QuadBatch(const SL_VECTOR(Index)& indices
            , VertexBuffer *pvb
            , const SL_VECTOR(Color)& colors
            , PoolVertices& poolVertices, ThreadCameraStreamData* _tcsData);

        /** Draws the QuadBatch. Does not do any setup of shaders or any other
        rendering state. */
        void Draw() const;

		const VertexBuffer* GetVertexBuffer(void) const { return vertexBuffer; }
		const IndexBuffer* GetIndexBuffer(void) const { return indexBuffer; }
    private:
        //! not allowed
        QuadBatch();
    private:
        VertexBuffer* vertexBuffer;
        IndexBuffer* indexBuffer;

        ThreadCameraStreamData* tcsData;

        bool enableObjectLabeling;
    };
}