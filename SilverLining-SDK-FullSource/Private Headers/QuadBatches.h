// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file QuadBatches.h
\brief A collection of QuadBatches (a vector, to be precise) that are drawn together using the same rendering state
*/

#pragma once

#ifdef SWIG
%module SilverLiningQuadBatches
#define SILVERLINING_API
%{
#include "QuadBatches.h"
    using namespace SilverLining;
    % }
#endif

#include "SLVector.h"
#include "SilverLiningTypes.h"
#include "DrawIndirectCommands.h"

namespace SilverLining
{
    class QuadBatch;
    class ThreadCameraStreamData;
    class BillboardShader;
    class Camera;
    class OverriddenBillboardMatrix;
    class Atmosphere;

    /** A collection of QuadBatches (a vector, to be precise) that are drawn
    together using the same rendering state. This object is returned by
    Billboard::EndBatchDraw() to enable you to quickly re-draw a batch
    of billboards that share the same texture map.*/
    class QuadBatches : public MemObject
    {
    public:
        /** Default constructor; doesn't really do much of anything. */
        QuadBatches();

        /** The destructor deletes all QuadBatch objects that have been added to
        this object. */
        virtual ~QuadBatches();

        /** Add a QuadBatch object to this collection. Once added, the
        QuadBatches object will be responsible for its destruction. */
        void Add(QuadBatch *qb) {
            batches.push_back(qb);
        }

        /** Clears out all batches and empties this object to start anew. */
        void Clear();

        /** Draw all attached QuadBatch objects using the texture
        specified. Sets up and tears down any required shaders and
        state. "Fade" controls the transparency of the quads and ranges
        from 0 (transparent) to 1 (fully visible). */
        void Draw(TextureHandle tex, double fade, const Atmosphere* atmosphere, unsigned long millis, const Camera* renderCamera, const OverriddenBillboardMatrix& overriddenBillboardMatrix
            , bool indirect, ThreadCameraStreamData* tcsData) const;

        /** Marks this object as "dirty". Internally, all this does is set
        a boolean flag. Externally, you may query the IsInavlid() method
        to see if you set this flag earlier. */
        void Invalidate() {
            dirty = true;
        }

        /** Queries the "dirty" flag, which will only be set if the Invalidate()
        method has been called on this object. */
        bool IsInvalid() const {
            return dirty;
        }

		virtual void CompileForIndirect(ThreadCameraStreamData* tcsData);
		virtual bool IsCompiledForIndirect(void) const;
    private:
        SL_VECTOR(QuadBatch*) batches;
        bool dirty;

		mutable std::vector<DrawElementsIndirectBindlessCommandNV> commandBufferForIndirect;
		bool compiledForIndirect;
		bool drawIndirect;

        //! cache value to avoid repeated gets from config
        bool spinEnabledOverrideDisabled;
    };
}
