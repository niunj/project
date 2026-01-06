// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file BillboardQuad.h
\brief  A class embodying everything needed to draw a single quad
*/

#pragma once

#ifdef SWIG
%module SilverLiningBillboardQuad
#define SILVERLINING_API
%{
#include "BillboardQuad.h"
    using namespace SilverLining;
    % }
#endif

#include "MemAlloc.h"
#include "Color.h"

namespace SilverLining
{
    class VertexBuffer;
    /** A class embodying everything needed to draw a single quad; an index, a
    pointer into a vertex buffer, and a color. It also provides a less-than
    operator to enable "sorting" of BillboardQuad objects, just based on
    the address of the vertex buffer passed in. Sorting by vertex buffer
    is useful for minimizing state changes when drawing. */
    class BillboardQuad : public MemObject
    {
    public:
        /** Constructor; just assigns data members to the values passed in.
        \param pIndex An index into the vertex buffer passed in, which marks
        the beginning of the four vertices that make up this quad.
        \param pVb A pointer to a VertexBuffer that pIndex refers to. This
        object will not delete this pointer.
        \param pColor The color to modulate this billboard by.
        */
        BillboardQuad(int pIndex, VertexBuffer *pVb, Color pColor) 
            : index(pIndex), color(pColor), vb(pVb) {
        }

        bool operator < (const BillboardQuad& bq) const {
            return vb < bq.vb;
        }

    public:
        // Data members are public for convenience. Normally this would just be a struct,
        // but we needed the less-than operator for sorting, thereby requiring a class.
        int index;
        Color color;
        VertexBuffer *vb;
    };
}