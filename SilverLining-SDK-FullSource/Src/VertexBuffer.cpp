// Copyright (c) 2004-2023  Sundog Software, LLC All rights reserved worldwide

#include "VertexBuffer.h"
#include "Renderer.h"
#include "SLAssert.h"

using namespace SilverLining;

std::atomic<int> VertexBuffer::s_totalCount{0};

VertexBuffer::VertexBuffer(int _numVertices, const char* _name, ThreadCameraStreamData* _tcsData, const BufferProperties& _bufferProperties)
    : tcsData(_tcsData)
    , numVertices(0)
    , internalHandle(0)
    , bufferProperties(_bufferProperties)
{
#if SILVERLINING_TRACK_CONTEXT_OPERATIONS_DURING_DRAW
    if ((tcsData->GetIsDrawingObjects() || tcsData->GetIsDrawingSky())) {
        std::cout << "vertex buffer allocation during rendering!!" << std::endl;
    }
#endif

    // cache it, to avoid repeated gets
    renderer = tcsData->GetRenderer();
    ReInit(_numVertices, _name);

    ++s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(c): Num of Vertex Buffers: " << s_totalCount << std::endl;
}

void VertexBuffer::ReInit(int newNumVertices, const char* name)
{
    SL_ASSERT(internalHandle == 0 && numVertices == 0);

    numVertices = newNumVertices;
    internalHandle = renderer->AllocateVertexBuffer(numVertices, name, bufferProperties);
}
void VertexBuffer::DeInit(void)
{
    if (internalHandle) {
        renderer->ReleaseVertexBuffer(internalHandle);
    }
    numVertices = 0;
    internalHandle = 0;
}

VertexBuffer::~VertexBuffer()
{
    --s_totalCount;
    tcsData->log(LogLevel::SL_LOG_LEVEL_DEBUG) << "(d): Num of Vertex Buffers: " << s_totalCount << std::endl;
    DeInit();
}

bool VertexBuffer::DrawIndexedStrip(const IndexBuffer& ib, bool useVertColors)
{
    return renderer->DrawStrip(internalHandle, ib.internalHandle, 0, ib.GetNumIndices(),
                               numVertices, useVertColors, true);
}

bool VertexBuffer::LockBuffer()
{
    return renderer->LockVertexBuffer(internalHandle);
}

Vertex *VertexBuffer::GetVertices() const
{
    return renderer->GetVertices(internalHandle);
}

bool VertexBuffer::UnlockBuffer()
{
    return renderer->UnlockVertexBuffer(internalHandle);
}

bool VertexBuffer::Update(int offset, Vertex *verts, int nVerts, bool justColors)
{
    return renderer->UpdateVertexBuffer(internalHandle, offset, verts, nVerts, justColors);
}

bool VertexBuffer::Get(int offset, Vertex *verts, int nVerts)
{
    return renderer->GetVertexBuffer(internalHandle, offset, verts, nVerts);
}

bool VertexBuffer::IncrementOffsetIndex()
{
    return renderer->IncrementOffsetIndex(internalHandle);
}

void Vertex::SetColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
#ifdef FLOATING_POINT_COLOR
    Vertex::r = (float)r / 255.0f;
    Vertex::g = (float)g / 255.0f;
    Vertex::b = (float)b / 255.0f;
    Vertex::a = (float)a / 255.0f;
#else

    Vertex::color = (a << 24) | (b << 16) | (g << 8) | r;

#endif
}

union uif32 {
        uif32() :
        i(0)
{}

uif32(float f) :
f(f)
{}

uif32(unsigned int i) :
i(i)
{}

float f;
unsigned int i;
};

static short to16(float f)
{

    uif32 Entry;
    Entry.f = f;
    int i = (int)Entry.i;

    int s = (i >> 16) & 0x00008000;
    int e = ((i >> 23) & 0x000000ff) - (127 - 15);
    int m = i & 0x007fffff;

    if (e <= 0) {
        if (e < -10) {
            return short(s);
        }

        m = (m | 0x00800000) >> (1 - e);

        if (m & 0x00001000)
            m += 0x00002000;

        return short(s | (m >> 13));
    } else if (e == 0xff - (127 - 15)) {
        if (m == 0) {
            return short(s | 0x7c00);
        } else {
            m >>= 13;

            return short(s | 0x7c00 | m | (m == 0));
        }
    } else {
        if (m & 0x00001000) {
            m += 0x00002000;

            if (m & 0x00800000) {
                m = 0;
                e += 1;
            }
        }

        if (e > 30) {
            //overflow();

            return short(s | 0x7c00);

        }

        return short(s | (e << 10) | (m >> 13));
    }
}

float to32(short value)
{
    int s = (value >> 15) & 0x00000001;
    int e = (value >> 10) & 0x0000001f;
    int m = value & 0x000003ff;

    if (e == 0) {
        if (m == 0) {
            uif32 result;
            result.i = (unsigned int)(s << 31);
            return result.f;
        } else {
            while (!(m & 0x00000400)) {
                m <<= 1;
                e -= 1;
            }

            e += 1;
            m &= ~0x00000400;
        }
    } else if (e == 31) {
        if (m == 0) {
            uif32 result;
            result.i = (unsigned int)((s << 31) | 0x7f800000);
            return result.f;
        } else {
            uif32 result;
            result.i = (unsigned int)((s << 31) | 0x7f800000 | (m << 13));
            return result.f;
        }
    }

    e = e + (127 - 15);
    m = m << 13;

    uif32 Result;
    Result.i = (unsigned int)((s << 31) | (e << 23) | m);
    return Result.f;
}

Vertex::Vertex(float _x, float _y, float _z, float _w)
{
    x = _x;
    y = _y;
    z = _z;
    w = _w;
}

void Vertex::SetUV(float pu, float pv)
{
#ifdef HALF_FLOAT_TEXCOORDS
    u = to16(pu);
    v = to16(pv);
    s = t = 0;
#else
    u = pu;
    v = pv;
    t = 0;
    s = 0;
#endif
}

void Vertex::SetU(float pu)
{
#ifdef HALF_FLOAT_TEXCOORDS
    u = to16(pu);
#else
    u = pu;
#endif
}

void Vertex::SetV(float pv)
{
#ifdef HALF_FLOAT_TEXCOORDS
    v = to16(pv);
#else
    v = pv;
#endif
}

void Vertex::SetS(float ps)
{
#ifdef HALF_FLOAT_TEXCOORDS
    s = to16(ps);
#else
    s = ps;
#endif
}

void Vertex::SetT(float pt)
{
#ifdef HALF_FLOAT_TEXCOORDS
    t = to16(pt);
#else
    t = pt;
#endif
}

float Vertex::GetU() const
{
#ifdef HALF_FLOAT_TEXCOORDS
    return to32(u);
#else
    return u;
#endif
}

float Vertex::GetV() const
{
#ifdef HALF_FLOAT_TEXCOORDS
    return to32(v);
#else
    return v;
#endif
}

float Vertex::GetS() const
{
#ifdef HALF_FLOAT_TEXCOORDS
    return to32(s);
#else
    return s;
#endif
}

float Vertex::GetT() const
{
#ifdef HALF_FLOAT_TEXCOORDS
    return to32(t);
#else
    return t;
#endif
}

void Vertex::SetColor(const Color& c)
{
#ifdef FLOATING_POINT_COLOR
    r = c.r;
    g = c.g;
    b = c.b;
    a = c.a;
#else
    unsigned char r, g, b, a;
    r = (unsigned char)(c.r * 255.0);
    g = (unsigned char)(c.g * 255.0);
    b = (unsigned char)(c.b * 255.0);
    a = (unsigned char)(c.a * 255.0);

    SetColor(r, g, b, a);
#endif
}
