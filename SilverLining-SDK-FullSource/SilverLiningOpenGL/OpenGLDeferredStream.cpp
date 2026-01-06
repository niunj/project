// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "OpenGLDeferredStream.h"
#include "OpenGLPacket.h"
#include "Context.h"
#include "SLAssert.h"
#include <iostream>

namespace SilverLining
{
OpenGLDeferredStream::OpenGLDeferredStream(Context* _context)
    : OpenGlStream(_context)
    , maxPackets(0)
    , packets(0)
{
#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
    numPackets.store(0);
#else
    numPackets = 0;
#endif

    maxPackets = 2048*16*2*2*16;
    numPackets = 0;
    packets = new ALL_PACKETS[maxPackets];
    memset(packets, 0, maxPackets * sizeof(ALL_PACKETS));

#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
#else
    packetMutex = SL_NEW Mutex;
#endif

    hasBindBufferARB = context->GetImmediateStream()->HasBindBufferARB();
    hasBlendFuncSeparate = context->GetImmediateStream()->HasBlendFuncSeparate();
    hasBlendEquationSeparate = context->GetImmediateStream()->HasBlendEquationSeparate();
    hasNamedBufferSubData = context->GetImmediateStream()->HasNamedBufferSubData();
}
OpenGLDeferredStream::~OpenGLDeferredStream()
{
    delete[] packets;
    packets = 0;
    maxPackets = 0;
#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
#else
    SL_DELETE packetMutex;
#endif
    DeleteAllInDataStore();
}
bool OpenGLDeferredStream::isImmediate(void) const
{
    return false;
}
int OpenGLDeferredStream::numCommands(void) const
{
    return numPackets;
}


void* OpenGLDeferredStream::GetNextDataStore(int size)
{
    SL_DEQUE(void*)& poolList = mapSizeToDataStoreListPool[size];
    if (poolList.empty()) {
        poolList.push_back((char*)SL_MALLOC(size));
    }
    void*& data = poolList.front();
    poolList.pop_front();
    SL_DEQUE(void*)& inUseList = mapSizeToDataStoreListInUse[size];
    inUseList.push_back(data);
    return data;
}

void OpenGLDeferredStream::ReturnAllToDataStore(void)
{
    for (MapSizeToDataStoreList::const_iterator it = mapSizeToDataStoreListInUse.begin(); it != mapSizeToDataStoreListInUse.end(); ++it) {
        SL_DEQUE(void*)& inUseList = mapSizeToDataStoreListInUse[it->first];
        SL_DEQUE(void*)& poolList = mapSizeToDataStoreListPool[it->first];
        for (SL_DEQUE(void*)::const_iterator listIter = inUseList.begin(); listIter != inUseList.end(); ++listIter) {
            poolList.push_back(*listIter);
        }
        inUseList.clear();
    }
}
static const bool trackDataStore = false;
void OpenGLDeferredStream::DeleteAllInDataStore(void)
{
    if (trackDataStore) {
        std::cout << "mapSizeToDataStoreListInUse.size(): " << mapSizeToDataStoreListInUse.size() << std::endl;
    }

    for (MapSizeToDataStoreList::const_iterator it = mapSizeToDataStoreListInUse.begin(); it != mapSizeToDataStoreListInUse.end(); ++it) {
        SL_DEQUE(void*)& inUseList = mapSizeToDataStoreListInUse[it->first];
        SL_ASSERT(inUseList.empty());
    }
    mapSizeToDataStoreListInUse.clear();

    for (MapSizeToDataStoreList::const_iterator it = mapSizeToDataStoreListPool.begin(); it != mapSizeToDataStoreListPool.end(); ++it) {
        SL_DEQUE(void*)& poolList = mapSizeToDataStoreListPool[it->first];
        if (trackDataStore) {
            std::cout << "deleting pool list datas for size: " << it->first << " , count: " << poolList.size() << std::endl;
        }
        for (SL_DEQUE(void*)::const_iterator listIter = poolList.begin(); listIter != poolList.end(); ++listIter) {
            void* data = *listIter;
            SL_FREE(data);
        }
        poolList.clear();
    }
}
void OpenGLDeferredStream::execute(void)
{
    if (!numPackets)
        return;

    if (numPackets > maxPackets) {
        std::cout << "num_packets > max_packets" << std::endl;
    }
    SL_ASSERT(numPackets <= maxPackets);
    const ALL_PACKETS* SILVERLINING_RESTRICT_KEYWORD pPacket = packets;
    for (unsigned int i = 0; i < numPackets; ++i, ++pPacket) {
        pPacket->execute((base*)pPacket);
    }
    ReturnAllToDataStore();
    ReturnAllVericesToPool();
}

void OpenGLDeferredStream::reset(void)
{
#if ((defined(WIN32) || defined(WIN64)) && _MSC_VER >= 1800)
    numPackets.store(0);
#else
    ScopedMutex scopedMutex(packetMutex);
    numPackets = 0;
#endif
}

void OpenGLDeferredStream::glEnable(GLenum cap)
{
    GLENABLE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLENABLE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLENABLE::execute);
    pPacket->cap = cap;
}
void OpenGLDeferredStream::glDisable(GLenum cap)
{
    GLDISABLE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDISABLE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDISABLE::execute);
    pPacket->cap = cap;
}


void OpenGLDeferredStream::glBegin(GLenum cap)
{
    GLBEGIN* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBEGIN>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBEGIN::execute);
    pPacket->cap = cap;
}

void OpenGLDeferredStream::glEnd()
{
    GLEND* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLEND>();
    pPacket->pfnExecute = PFN_EXECUTE(GLEND::execute);
}

void OpenGLDeferredStream::glPushMatrix(void)
{
    GLPUSHMATRIX* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPUSHMATRIX>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPUSHMATRIX::execute);
}
void OpenGLDeferredStream::glPopMatrix(void)
{
    GLPOPMATRIX* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPOPMATRIX>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPOPMATRIX::execute);
}


void OpenGLDeferredStream::glPushAttrib(GLbitfield mask)
{
    GLPUSHATTRIB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPUSHATTRIB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPUSHATTRIB::execute);
    pPacket->mask = mask;
}
void OpenGLDeferredStream::glPopAttrib(void)
{
    GLPOPATTRIB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPOPATTRIB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPOPATTRIB::execute);
}

void OpenGLDeferredStream::glVertex3d(GLdouble x, GLdouble y, GLdouble z)
{
    GLVERTEX3D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVERTEX3D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVERTEX3D::execute);
    pPacket->x = x;
    pPacket->y = y;
    pPacket->z = z;
}
void OpenGLDeferredStream::glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha)
{
    GLCOLOR4D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCOLOR4D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCOLOR4D::execute);
    pPacket->red = red;
    pPacket->green = green;
    pPacket->blue = blue;
    pPacket->alpha = alpha;
}

void OpenGLDeferredStream::glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    GLCOLOR4F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCOLOR4F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCOLOR4F::execute);
    pPacket->red = red;
    pPacket->green = green;
    pPacket->blue = blue;
    pPacket->alpha = alpha;
}
void OpenGLDeferredStream::glTexCoord2d(GLdouble s, GLdouble t)
{
    GLTEXCOORD2D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLTEXCOORD2D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLTEXCOORD2D::execute);
    pPacket->s = s;
    pPacket->t = t;
}


void OpenGLDeferredStream::glMatrixMode(GLenum mode)
{
    GLMATRIXMODE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLMATRIXMODE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLMATRIXMODE::execute);
    pPacket->mode = mode;
}
void OpenGLDeferredStream::glLoadIdentity(void)
{
    GLLOADIDENTITY* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLLOADIDENTITY>();
    pPacket->pfnExecute = PFN_EXECUTE(GLLOADIDENTITY::execute);
}
void OpenGLDeferredStream::glLoadMatrixd(const GLdouble* m)
{
    GLLOADMATRIXD* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLLOADMATRIXD>();
    pPacket->pfnExecute = PFN_EXECUTE(GLLOADMATRIXD::execute);
    memcpy(pPacket->m, m, 16 * sizeof(double));
}
void OpenGLDeferredStream::gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
    GLUORTHO2D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUORTHO2D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUORTHO2D::execute);
    pPacket->left = left;
    pPacket->right = right;
    pPacket->bottom = bottom;
    pPacket->top = top;
}

void OpenGLDeferredStream::glRasterPos2i(GLint x, GLint y)
{
    GLRASTERPOS2I* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLRASTERPOS2I>();
    pPacket->pfnExecute = PFN_EXECUTE(GLRASTERPOS2I::execute);
    pPacket->x = x;
    pPacket->y = y;
}
void OpenGLDeferredStream::glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    GLDRAWPIXELS* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWPIXELS>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWPIXELS::execute);
    pPacket->width = width;
    pPacket->height = height;
    pPacket->format = format;
    pPacket->type = type;
    pPacket->pixels = pixels;
}

void OpenGLDeferredStream::glActiveTexture(GLenum texture)
{
    GLACTIVETEXTURE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLACTIVETEXTURE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLACTIVETEXTURE::execute);

    pPacket->texture = texture;
}

void OpenGLDeferredStream::glBindTexture(GLenum target, GLuint texture)
{
    GLBINDTEXTURE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDTEXTURE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDTEXTURE::execute);

    pPacket->target = target;
    pPacket->texture = texture;
}

bool OpenGLDeferredStream::HasTexSubImage3D(void) const
{
    return true;
}
void OpenGLDeferredStream::glPixelStorei(GLenum pname, GLint param)
{
    GLPIXELSTOREI* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPIXELSTOREI>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPIXELSTOREI::execute);

    pPacket->pname = pname;
    pPacket->param = param;
}
void OpenGLDeferredStream::glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)
{
    GLTEXSUBIMAGE3D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLTEXSUBIMAGE3D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLTEXSUBIMAGE3D::execute);

    pPacket->target = target;
    pPacket->level = level;

    pPacket->xoffset = xoffset;
    pPacket->yoffset = yoffset;
    pPacket->zoffset = zoffset;

    pPacket->width = width;
    pPacket->height = height;
    pPacket->depth = depth;

    pPacket->format = format;
    pPacket->type = type;
    pPacket->pixels = pixels;
}


void OpenGLDeferredStream::glUseProgramObjectARB(GLuint program)
{
    GLUSEPROGRAMOBJECTARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUSEPROGRAMOBJECTARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUSEPROGRAMOBJECTARB::execute);
    pPacket->program = program;
}

void OpenGLDeferredStream::glBindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
    GLBINDBUFFERBASE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDBUFFERBASE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDBUFFERBASE::execute);

    pPacket->target = target;
    pPacket->index = index;
    pPacket->buffer = buffer;
}

void OpenGLDeferredStream::glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    GLNAMEDBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLNAMEDBUFFERSUBDATA>();
    pPacket->pfnExecute = PFN_EXECUTE(GLNAMEDBUFFERSUBDATA::execute);

    pPacket->buffer = buffer;
    pPacket->offset = offset;
    pPacket->size = size;
    if (makeCopy) {
        if (offset != 0) {
            std::cout << "offset!=0";
        }
        void* dstData = GetNextDataStore((int)size);
        memcpy(dstData, data, size);
        pPacket->data = dstData;
    } else {
        pPacket->data = data;
    }
}

void OpenGLDeferredStream::ReturnAllVericesToPool(void)
{
    scratchVerticesWithColors.clear();
    scratchVerticesToGetFromVertexBufferAndCopyColorsInto.clear();
}

void OpenGLDeferredStream::glPseudoSync(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool justColors)
{
    GLPSEUDOSYNC* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPSEUDOSYNC>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPSEUDOSYNC::execute);

    pPacket->slBuffer = slBuffer;
    pPacket->offset = offset;
    pPacket->size = size;
    pPacket->justColors = justColors;

    SL_ASSERT(slBuffer->Type() == GL_ARRAY_BUFFER);
    int numVerts = (int)(size / sizeof(Vertex));

    Vertex vertex;
    vertex.x = vertex.y = vertex.z = vertex.w = -9999.0f;
    vertex.r = vertex.g = vertex.b = vertex.a = -9999.0f;
    vertex.s = vertex.t = vertex.u = vertex.v = -9999.0f;

    Vertex scratchVertex;
    const int startIndexColors = (int)scratchVerticesWithColors.size();
    for (int i = 0; i < numVerts; ++i) {
        memcpy(&scratchVertex, (unsigned char*)data + i*sizeof(Vertex), sizeof(Vertex));
        scratchVerticesWithColors.push_back(scratchVertex);
    }
    pPacket->scratchVerticesWithColors = &scratchVerticesWithColors;
    pPacket->startIndexColors = startIndexColors;


    if (justColors == false) {
        pPacket->scratchVerticesToGetFromVertexBufferAndCopyColorsInto = 0;
        pPacket->startIndexCopyInto = 0;
    } else {
        int startIndexColorsBufferAndCopyColorsInto = (int)scratchVerticesToGetFromVertexBufferAndCopyColorsInto.size();
        for (int i = 0; i < numVerts; ++i) {
            scratchVerticesToGetFromVertexBufferAndCopyColorsInto.push_back(vertex);
        }

        pPacket->scratchVerticesToGetFromVertexBufferAndCopyColorsInto = &scratchVerticesToGetFromVertexBufferAndCopyColorsInto;
        pPacket->startIndexCopyInto = startIndexColorsBufferAndCopyColorsInto;
    }
}


bool OpenGLDeferredStream::HasNamedBufferSubData(void) const
{
    return hasNamedBufferSubData;
}

void OpenGLDeferredStream::glPseudoNamedBufferSubData(SL_Buffer* slBuffer, GLintptr offset, GLsizeiptr size, const void *data, bool makeCopy)
{
    GLPSEUDONAMEDBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPSEUDONAMEDBUFFERSUBDATA>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPSEUDONAMEDBUFFERSUBDATA::execute);

    pPacket->slBuffer = slBuffer;
    pPacket->offset = offset;
    pPacket->size = size;
    if (makeCopy) {
        if (offset != 0) {
            std::cout << "offset!=0";
        }
        void* dstData = GetNextDataStore((int)size);
        memcpy(dstData, data, size);
        pPacket->data = dstData;
    } else {
        pPacket->data = data;
    }
}

void OpenGLDeferredStream::checkGlError(int line)
{
    // nop
}


void OpenGLDeferredStream::glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLVIEWPORT* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVIEWPORT>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVIEWPORT::execute);

    pPacket->x = x;
    pPacket->y = y;
    pPacket->width = width;
    pPacket->height = height;
}

void OpenGLDeferredStream::glDepthMask(GLboolean flag)
{
    GLDEPTHMASK* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDEPTHMASK>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDEPTHMASK::execute);

    pPacket->flag = flag;
}


void OpenGLDeferredStream::glHint(GLenum target, GLenum mode)
{
    GLHINT* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLHINT>();
    pPacket->pfnExecute = PFN_EXECUTE(GLHINT::execute);

    pPacket->target = target;
    pPacket->mode = mode;
}


void OpenGLDeferredStream::glLineWidth(GLfloat width)
{
    GLLINEWIDTH* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLLINEWIDTH>();
    pPacket->pfnExecute = PFN_EXECUTE(GLLINEWIDTH::execute);

    pPacket->width = width;
}


void OpenGLDeferredStream::glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    GLTEXENVI* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLTEXENVI>();
    pPacket->pfnExecute = PFN_EXECUTE(GLTEXENVI::execute);

    pPacket->target = target;
    pPacket->pname = pname;
    pPacket->param = param;

}
void OpenGLDeferredStream::glPointParameteriNV(GLenum pname, GLint param)
{
    GLPOINTPARAMETERINV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPOINTPARAMETERINV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPOINTPARAMETERINV::execute);

    pPacket->pname = pname;
    pPacket->param = param;
}

void OpenGLDeferredStream::glPointSize(GLfloat size)
{
    GLPOINTSIZE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPOINTSIZE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPOINTSIZE::execute);
    pPacket->size = size;
}

void OpenGLDeferredStream::glFrontFace(GLenum mode)
{
    GLFRONTFACE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLFRONTFACE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLFRONTFACE::execute);

    pPacket->mode = mode;
}

bool OpenGLDeferredStream::HasBindBufferARB() const
{
    return hasBindBufferARB;
}

void OpenGLDeferredStream::glBindBufferARB(GLenum target, SL_Buffer* slBuffer)
{
    GLBINDBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDBUFFERARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDBUFFERARB::execute);
    pPacket->target = target;
    pPacket->slBuffer = slBuffer;
}
void OpenGLDeferredStream::glPseudoBindBufferARB(GLenum target, SL_Buffer* slBuffer)
{
    GLPSEUDOBINDBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPSEUDOBINDBUFFERARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPSEUDOBINDBUFFERARB::execute);
    pPacket->target = target;
    pPacket->slBuffer = slBuffer;
}

void OpenGLDeferredStream::glBindBufferARB(GLenum target, BufferGL* slBuffer)
{
    GLBINDBUFFERARB2* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDBUFFERARB2>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDBUFFERARB2::execute);
    pPacket->target = target;
    pPacket->slBuffer = slBuffer;
}

void OpenGLDeferredStream::glBufferSubDataARB(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    GLBUFFERSUBDATAARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBUFFERSUBDATAARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBUFFERSUBDATAARB::execute);

    pPacket->target = target;
    pPacket->offset = offset;
    pPacket->size = size;
    pPacket->data = data;
}

void OpenGLDeferredStream::glBufferDataARB(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    GLBUFFERDATAARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBUFFERDATAARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBUFFERDATAARB::execute);

    pPacket->target = target;
    pPacket->size = size;
    pPacket->data = data;
    pPacket->usage = usage;
}

void OpenGLDeferredStream::glDeleteBufferARB(GLuint handle)
{
    GLDELETEBUFFERARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDELETEBUFFERARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDELETEBUFFERARB::execute);

    pPacket->handle = handle;
}

void OpenGLDeferredStream::glEnableClientState(GLenum array)
{
    GLENABLECLIENTSTATE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLENABLECLIENTSTATE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLENABLECLIENTSTATE::execute);
    pPacket->array = array;
}

void OpenGLDeferredStream::glDisableClientState(GLenum array)
{
    GLDISABLECLIENTSTATE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDISABLECLIENTSTATE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDISABLECLIENTSTATE::execute);
    pPacket->array = array;
}

void OpenGLDeferredStream::glClientActiveTexture(GLenum texture)
{
    GLCLIENTACTIVETEXTURE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCLIENTACTIVETEXTURE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCLIENTACTIVETEXTURE::execute);
    pPacket->texture = texture;
}

void OpenGLDeferredStream::glVertexFormatNV(GLint size, GLenum type, GLsizei stride)
{
    GLVERTEXFORMATNV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVERTEXFORMATNV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVERTEXFORMATNV::execute);
    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
}
void OpenGLDeferredStream::glColorFormatNV(GLint size, GLenum type, GLsizei stride)
{
    GLCOLORFORMATNV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCOLORFORMATNV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCOLORFORMATNV::execute);
    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
}
void OpenGLDeferredStream::glTexCoordFormatNV(GLint size, GLenum type, GLsizei stride)
{
    GLTEXCOORDFORMATNV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLTEXCOORDFORMATNV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLTEXCOORDFORMATNV::execute);
    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
}

void OpenGLDeferredStream::glDepthFunc(GLenum func)
{
    GLDEPTHFUNC* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDEPTHFUNC>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDEPTHFUNC::execute);

    pPacket->func = func;
}

void OpenGLDeferredStream::glDepthRangedNV(float zmin, float zmax)
{
    GLDEPTHRANGEDNV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDEPTHRANGEDNV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDEPTHRANGEDNV::execute);

    pPacket->zmin = zmin;
    pPacket->zmax = zmax;
}
void OpenGLDeferredStream::glDepthRange(float zmin, float zmax)
{
    GLDEPTHRANGE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDEPTHRANGE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDEPTHRANGE::execute);

    pPacket->zmin = zmin;
    pPacket->zmax = zmax;
}


void OpenGLDeferredStream::glClear(GLbitfield mask)
{
    GLCLEAR* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCLEAR>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCLEAR::execute);

    pPacket->mask = mask;
}

void OpenGLDeferredStream::glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    GLCLEARCOLOR* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCLEARCOLOR>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCLEARCOLOR::execute);

    pPacket->red = red;
    pPacket->green = green;
    pPacket->blue = blue;
    pPacket->alpha = alpha;
}

void OpenGLDeferredStream::glBufferAddressRangeNV(GLenum pname, GLuint index, GLuint64EXT address, GLsizeiptr length)
{
    GLBUFFERADDRESSRANGENV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBUFFERADDRESSRANGENV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBUFFERADDRESSRANGENV::execute);

    pPacket->pname = pname;
    pPacket->index = index;
    pPacket->address = address;
    pPacket->length = length;
}

void OpenGLDeferredStream::glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    GLVERTEXPOINTER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVERTEXPOINTER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVERTEXPOINTER::execute);

    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
    pPacket->pointer = pointer;
}
void OpenGLDeferredStream::glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    GLCOLORPOINTER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLCOLORPOINTER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLCOLORPOINTER::execute);

    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
    pPacket->pointer = pointer;
}
void OpenGLDeferredStream::glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    GLTEXCOORDPOINTER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLTEXCOORDPOINTER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLTEXCOORDPOINTER::execute);

    pPacket->size = size;
    pPacket->type = type;
    pPacket->stride = stride;
    pPacket->pointer = pointer;
}

void OpenGLDeferredStream::glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    GLDRAWARRAYS* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWARRAYS>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWARRAYS::execute);

    pPacket->mode = mode;
    pPacket->first = first;
    pPacket->count = count;
}

void OpenGLDeferredStream::glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    GLDRAWELEMENTS* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWELEMENTS>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWELEMENTS::execute);
    pPacket->mode = mode;
    pPacket->count = count;
    pPacket->type = type;
    pPacket->indices = indices;
}

void OpenGLDeferredStream::glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount)
{
    GLDRAWELEMENTSINSTANCED* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWELEMENTSINSTANCED>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWELEMENTSINSTANCED::execute);
    pPacket->mode = mode;
    pPacket->count = count;
    pPacket->type = type;
    pPacket->indices = indices;
    pPacket->instancecount = instancecount;
}

bool OpenGLDeferredStream::HasBlendFuncSeparate(void) const
{
    return hasBlendFuncSeparate;
}

void OpenGLDeferredStream::glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    GLBLENDFUNC* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBLENDFUNC>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBLENDFUNC::execute);
    pPacket->sfactor = sfactor;
    pPacket->dfactor = dfactor;
}

void OpenGLDeferredStream::glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    GLBLENDFUNCSEPARATE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBLENDFUNCSEPARATE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBLENDFUNCSEPARATE::execute);
    pPacket->sfactorRGB = sfactorRGB;
    pPacket->dfactorRGB = dfactorRGB;
    pPacket->sfactorAlpha = sfactorAlpha;
    pPacket->dfactorAlpha = dfactorAlpha;
}

bool OpenGLDeferredStream::HasBlendEquationSeparate(void) const
{
    return hasBlendEquationSeparate;
}
void OpenGLDeferredStream::glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
    GLBLENDEQUATIONSEPARATE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBLENDEQUATIONSEPARATE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBLENDEQUATIONSEPARATE::execute);
    pPacket->modeRGB = modeRGB;
    pPacket->modeAlpha = modeAlpha;
}

void OpenGLDeferredStream::glFogf(GLenum pname, GLfloat param)
{
    GLFOGF* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLFOGF>();
    pPacket->pfnExecute = PFN_EXECUTE(GLFOGF::execute);
    pPacket->pname = pname;
    pPacket->param = param;
}
void OpenGLDeferredStream::glFogfv(GLenum pname, const GLfloat *params)
{
    GLFOGFV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLFOGFV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLFOGFV::execute);
    pPacket->pname = pname;
    memcpy(pPacket->params, params, 3 * sizeof(float));
}
void OpenGLDeferredStream::glFogi(GLenum pname, GLint param)
{
    GLFOGI* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLFOGI>();
    pPacket->pfnExecute = PFN_EXECUTE(GLFOGI::execute);
    pPacket->pname = pname;
    pPacket->param = param;
}

void OpenGLDeferredStream::glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    GLPROGRAMUNIFORM4F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM4F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM4F::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
    pPacket->v3 = v3;
}

void OpenGLDeferredStream::glUniform4fARB(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    GLUNIFORM4FARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM4FARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM4FARB::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
    pPacket->v3 = v3;
}

void OpenGLDeferredStream::glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GLPROGRAMUNIFORMMATRIX4FV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORMMATRIX4FV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORMMATRIX4FV::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->count = count;
    pPacket->transpose = transpose;
    memcpy(pPacket->value, value, 16 * sizeof(GLfloat));
}

void OpenGLDeferredStream::glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GLUNIFORMMATRIX4FVARB* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORMMATRIX4FVARB>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORMMATRIX4FVARB::execute);

    pPacket->location = location;
    pPacket->count = count;
    pPacket->transpose = transpose;
    memcpy(pPacket->value, value, 16 * sizeof(GLfloat));
}

void OpenGLDeferredStream::glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    GLBINDFRAMEBUFFEREXT* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDFRAMEBUFFEREXT>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDFRAMEBUFFEREXT::execute);

    pPacket->target = target;
    pPacket->framebuffer = framebuffer;
}

void OpenGLDeferredStream::glDrawBuffer(GLenum target)
{
    GLDRAWBUFFER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWBUFFER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWBUFFER::execute);

    pPacket->target = target;
}
}
