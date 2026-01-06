// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file PoolVertices.h
\brief A class for describing a pool of vertices. Used by QuadBatch
*/

#pragma once

#ifdef SWIG
%module SilverLiningPoolVertices
#define SILVERLINING_API
%{
#include "PoolVertices.h"
    using namespace SilverLining;
    % }
#endif

#include "SLVector.h"
#include "SLList.h"
#include "SilverLiningTypes.h"
#include "PoolVertices.h"


namespace SilverLining
{
    class Vertex;

    class Vertices
    {
    public:
        Vertex* vertices;
        unsigned int numVerts;
    };

    class PoolVertices
    {
    public:
        PoolVertices();
        ~PoolVertices();
    public:
        Vertices Get(unsigned int numVerts);
        void Return(Vertices vertices);
    private:
        typedef std::list<Vertices> VerticesPool;
        VerticesPool verticesPool;
    };
}
