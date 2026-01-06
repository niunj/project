// Copyright (c) 2009-2016 Sundog Software, LLC All rights reserved worldwide.

#ifndef VERTEX_H
#define VERTEX_H

#include "MemAlloc.h"
#include "Color.h"

#if (defined(WIN32) || defined(WIN64))
#include <SilverLining_Vertex_Solution_Defines.h>
#endif

#ifndef MAC // Macs are low on VRAM so we will never use floating point color
#ifndef NO_FLOATING_POINT_COLOR
// Comment this out to save memory if you know you'll never use HDR mode.
#define FLOATING_POINT_COLOR
#endif
#endif

#if (defined(HALF_PRECISION_TEXCOORDS) || defined(MAC))
#define HALF_FLOAT_TEXCOORDS
#endif

#pragma pack(push)
#pragma pack(8)

namespace SilverLining
{

/** A single vertex containing a position, RGBA color, and 3D texture coordinates. */
class Vertex : public MemObject
{
public:
    float x, y, z, w;

	Vertex(){}

	Vertex(float _x, float _y, float _z, float _w);

/** Sets the 4-byte color of the vertex in the manner expected by the underlying Renderer,
   based on the 0-255 RGBA values provided. */
    void SILVERLINING_API SetColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

/** Sets the u,v texture coordinates of the vertex in the manner expected by the
   underlying Renderer, based on the uv coordinates provided. The r texture coordinate
   is set to 0. */
    void SILVERLINING_API SetUV(float u, float v);
    void SILVERLINING_API SetU(float u);
    void SILVERLINING_API SetV(float v);
    void SILVERLINING_API SetS(float s);
    void SILVERLINING_API SetT(float t);

    float SILVERLINING_API GetU() const;
    float SILVERLINING_API GetV() const;
    float SILVERLINING_API GetS() const;
    float SILVERLINING_API GetT() const;

/** Sets the 4-byte color of the vertex in the manner expected by the underlying Renderer,
   based on the Color provided. */
    void SILVERLINING_API SetColor(const Color& c);

#ifdef FLOATING_POINT_COLOR
    float r, g, b, a;
#else
    unsigned int color;
#endif

#ifdef HALF_FLOAT_TEXCOORDS
    short u, v, t, s;
#else
    float u, v, t, s;
#endif

};

}

#pragma pack(pop)

#endif
