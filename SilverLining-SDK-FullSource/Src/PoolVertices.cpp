// Copyright (c) 2004-2020  Sundog Software, LLC. All rights reserved worldwide.

#include "PoolVertices.h"
#include "Vertex.h"

namespace SilverLining
{
PoolVertices::PoolVertices()
{

}

PoolVertices::~PoolVertices()
{
    for (VerticesPool::iterator it = verticesPool.begin(); it != verticesPool.end(); ++it) {
        Vertices vertices = *it;
        SL_DELETE[] vertices.vertices;
    }

    verticesPool.clear();
}
Vertices PoolVertices::Get(unsigned int numVerts)
{
    for (VerticesPool::iterator it = verticesPool.begin(); it != verticesPool.end(); ++it) {
        Vertices vertices = *it;
        if (vertices.numVerts >= numVerts) {
            verticesPool.erase(it);
            return vertices;
        }
    }
    Vertices vertices;
    vertices.vertices = SL_NEW Vertex[numVerts];
    vertices.numVerts = numVerts;
    return vertices;
}
void PoolVertices::Return(Vertices vertices)
{
    verticesPool.push_back(vertices);
}

}

