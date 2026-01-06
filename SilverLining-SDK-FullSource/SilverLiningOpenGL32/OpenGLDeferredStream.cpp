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

void OpenGLDeferredStream::glUseProgram(GLuint program)
{
    GLUSEPROGRAM* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUSEPROGRAM>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUSEPROGRAM::execute);
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


bool OpenGLDeferredStream::HasNamedBufferSubData(void) const
{
    return true;
}
bool OpenGLDeferredStream::HasBlendFuncSeparate(void) const
{
    return true;
}
bool OpenGLDeferredStream::HasTexSubImage3D(void) const
{
    return true;
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
        void* dstData = GetNextDataStore(size);
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
    int startIndexColors = (int)scratchVerticesWithColors.size();
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
        void* dstData = GetNextDataStore(size);
        memcpy(dstData, data, size);
        pPacket->data = dstData;
    } else {
        pPacket->data = data;
    }
}

void OpenGLDeferredStream::glBindVertexArrayFor(GLuint vboID, Shader* shader)
{
    GLBINDVERTEXARRAYFOR* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDVERTEXARRAYFOR>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDVERTEXARRAYFOR::execute);
    pPacket->vboID = vboID;
    pPacket->shader = shader;
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

void OpenGLDeferredStream::glPointSize(GLfloat size)
{
    GLPOINTSIZE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPOINTSIZE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPOINTSIZE::execute);
    pPacket->size = size;
}

void OpenGLDeferredStream::checkGlError(int line)
{
    // nop
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

void OpenGLDeferredStream::glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    GLBUFFERSUBDATA* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBUFFERSUBDATA>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBUFFERSUBDATA::execute);

    pPacket->target = target;
    pPacket->offset = offset;
    pPacket->size = size;
    pPacket->data = data;
}

void OpenGLDeferredStream::glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    GLBUFFERDATA* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBUFFERDATA>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBUFFERDATA::execute);

    pPacket->target = target;
    pPacket->size = size;
    pPacket->data = data;
    pPacket->usage = usage;
}

void OpenGLDeferredStream::glDeleteBuffer(GLuint handle)
{
    GLDELETEBUFFER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDELETEBUFFER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDELETEBUFFER::execute);

    pPacket->handle = handle;
}

void OpenGLDeferredStream::glProgramUniform1f(GLuint program, GLint location, GLfloat v0)
{
    GLPROGRAMUNIFORM1F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM1F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM1F::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;

}
void OpenGLDeferredStream::glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1)
{
    GLPROGRAMUNIFORM2F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM2F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM2F::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
}
void OpenGLDeferredStream::glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    GLPROGRAMUNIFORM3F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM3F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM3F::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
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

void OpenGLDeferredStream::glProgramUniform1d(GLuint program, GLint location, GLdouble v0)
{
    GLPROGRAMUNIFORM1D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM1D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM1D::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
}
void OpenGLDeferredStream::glProgramUniform2d(GLuint program, GLint location, GLdouble v0, GLdouble v1)
{
    GLPROGRAMUNIFORM2D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM2D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM1D::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
}
void OpenGLDeferredStream::glProgramUniform3d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2)
{
    GLPROGRAMUNIFORM3D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM3D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM3D::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
}
void OpenGLDeferredStream::glProgramUniform4d(GLuint program, GLint location, GLdouble v0, GLdouble v1, GLdouble v2, GLdouble v3)
{
    GLPROGRAMUNIFORM4D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM4D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM4D::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
    pPacket->v3 = v3;
}

void OpenGLDeferredStream::glProgramUniform1i(GLuint program, GLint location, GLint v0)
{
    GLPROGRAMUNIFORM1I* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORM1I>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORM1I::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->v0 = v0;
}


void OpenGLDeferredStream::glUniform1f(GLint location, GLfloat v0)
{
    GLUNIFORM1F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM1F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM1F::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
}
void OpenGLDeferredStream::glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    GLUNIFORM2F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM2F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM2F::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
}
void OpenGLDeferredStream::glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    GLUNIFORM3F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM3F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM3F::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
}
void OpenGLDeferredStream::glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
    GLUNIFORM4F* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM4F>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM4F::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
    pPacket->v1 = v1;
    pPacket->v2 = v2;
    pPacket->v3 = v3;
}

void OpenGLDeferredStream::glUniform1d(GLint location, GLdouble x)
{
    GLUNIFORM1D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM1D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM1D::execute);

    pPacket->location = location;
    pPacket->v0 = x;
}
void OpenGLDeferredStream::glUniform2d(GLint location, GLdouble x, GLdouble y)
{
    GLUNIFORM2D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM2D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM2D::execute);

    pPacket->location = location;
    pPacket->v0 = x;
    pPacket->v1 = y;
}
void OpenGLDeferredStream::glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z)
{
    GLUNIFORM3D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM3D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM3D::execute);

    pPacket->location = location;
    pPacket->v0 = x;
    pPacket->v1 = y;
    pPacket->v2 = z;
}
void OpenGLDeferredStream::glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
    GLUNIFORM4D* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM4D>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM4D::execute);

    pPacket->location = location;
    pPacket->v0 = x;
    pPacket->v1 = y;
    pPacket->v2 = z;
    pPacket->v3 = w;
}

void OpenGLDeferredStream::glUniform1i(GLint location, GLint v0)
{
    GLUNIFORM1I* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORM1I>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORM1I::execute);

    pPacket->location = location;
    pPacket->v0 = v0;
}


void OpenGLDeferredStream::glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GLUNIFORMMATRIX3FV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORMMATRIX3FV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORMMATRIX3FV::execute);

    pPacket->location = location;
    pPacket->count = count;
    pPacket->transpose = transpose;
    memcpy(pPacket->value, value, 9 * sizeof(GLfloat));
}
void OpenGLDeferredStream::glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GLUNIFORMMATRIX4FV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLUNIFORMMATRIX4FV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLUNIFORMMATRIX4FV::execute);

    pPacket->location = location;
    pPacket->count = count;
    pPacket->transpose = transpose;
    memcpy(pPacket->value, value, 16 * sizeof(GLfloat));
}

void OpenGLDeferredStream::glProgramUniformMatrix3fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    GLPROGRAMUNIFORMMATRIX3FV* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLPROGRAMUNIFORMMATRIX3FV>();
    pPacket->pfnExecute = PFN_EXECUTE(GLPROGRAMUNIFORMMATRIX3FV::execute);

    pPacket->program = program;
    pPacket->location = location;
    pPacket->count = count;
    pPacket->transpose = transpose;
    memcpy(pPacket->value, value, 9 * sizeof(GLfloat));
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

void OpenGLDeferredStream::glBindVertexArray(GLuint array)
{
    GLBINDVERTEXARRAY* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDVERTEXARRAY>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDVERTEXARRAY::execute);
    pPacket->array = array;
}

void OpenGLDeferredStream::glBindBuffer(GLenum target, SL_Buffer* slBuffer)
{
    GLBINDBUFFER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDBUFFER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDBUFFER::execute);
    pPacket->target = target;
    pPacket->slBuffer = slBuffer;
}

void OpenGLDeferredStream::glBindBuffer(GLenum target, BufferGL* slBuffer)
{
    GLBINDBUFFER2* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLBINDBUFFER2>();
    pPacket->pfnExecute = PFN_EXECUTE(GLBINDBUFFER2::execute);
    pPacket->target = target;
    pPacket->slBuffer = slBuffer;
}

void OpenGLDeferredStream::glFrontFace(GLenum mode)
{
    GLFRONTFACE* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLFRONTFACE>();
    pPacket->pfnExecute = PFN_EXECUTE(GLFRONTFACE::execute);

    pPacket->mode = mode;
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

void OpenGLDeferredStream::glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    GLVIEWPORT* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVIEWPORT>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVIEWPORT::execute);

    pPacket->x = x;
    pPacket->y = y;
    pPacket->width = width;
    pPacket->height = height;
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

void OpenGLDeferredStream::glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    GLVERTEXATTRIBPOINTER* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLVERTEXATTRIBPOINTER>();
    pPacket->pfnExecute = PFN_EXECUTE(GLVERTEXATTRIBPOINTER::execute);

    pPacket->index = index;
    pPacket->size = size;
    pPacket->type = type;
    pPacket->normalized = normalized;
    pPacket->stride = stride;
    pPacket->pointer = pointer;
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

void OpenGLDeferredStream::glEnableVertexAttribArray(GLuint index)
{
    GLENABLEVERTEXATTRIBARRAY* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLENABLEVERTEXATTRIBARRAY>();
    pPacket->pfnExecute = PFN_EXECUTE(GLENABLEVERTEXATTRIBARRAY::execute);
    pPacket->index = index;
}

void OpenGLDeferredStream::glDisableVertexAttribArray(GLuint index)
{
    GLDISABLEVERTEXATTRIBARRAY* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDISABLEVERTEXATTRIBARRAY>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDISABLEVERTEXATTRIBARRAY::execute);
    pPacket->index = index;
}

void OpenGLDeferredStream::glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    GLDRAWARRAYS* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDRAWARRAYS>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDRAWARRAYS::execute);

    pPacket->mode = mode;
    pPacket->first = first;
    pPacket->count = count;
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

void OpenGLDeferredStream::glDepthMask(GLboolean flag)
{
    GLDEPTHMASK* SILVERLINING_RESTRICT_KEYWORD pPacket = NextPacket<GLDEPTHMASK>();
    pPacket->pfnExecute = PFN_EXECUTE(GLDEPTHMASK::execute);

    pPacket->flag = flag;
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
