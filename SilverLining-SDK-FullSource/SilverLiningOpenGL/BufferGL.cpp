#include "BufferGL.h"
#include "OpenGLUtils.h"
#include "SLAssert.h"
#include "OpenGLStream.h"
#include "Context.h"
#include <cstring>

using namespace std;

namespace SilverLining
{
static GLenum ToGlBufferType(BufferType bufferType)
{
    if (bufferType==SHADER_STORAGE_BUFFER) {
        return GL_SHADER_STORAGE_BUFFER;
    }
    if (bufferType==UNIFORM_BUFFER) {
        return GL_UNIFORM_BUFFER;
    }
    SL_ASSERT(false);
    return GL_UNIFORM_BUFFER;
}

BufferGL::BufferGL(const char* name, BufferType type, int _size, const BufferProperties& _bufferProperties)
    : Buffer(name, type, _size, _bufferProperties)
    , glHandle(0)
    , glBufferType(ToGlBufferType(type))
    , stagingBuffer(0)
{
    InitBuffers();
    OpenGLUtils::CheckError(__LINE__);
}

BufferGL::~BufferGL()
{
    if (stagingBuffer) {
        SL_DELETE[] stagingBuffer;
    }
    glDeleteBuffersARB(1, &glHandle);
    OpenGLUtils::CheckError(__LINE__);
}

static void checkSize(GLuint handle, int expectedSize)
{
    int sizeFromGL = 0;
    //Are we sure there is little to no performance cost of getting this from the driver?
    glGetNamedBufferParameteriv(handle, GL_BUFFER_SIZE, &sizeFromGL);
    //SL_ASSERT(sizeFromGL == expectedSize);
    if (sizeFromGL != expectedSize && OpenGLUtils::enableDebugOutput) {
        std::cout << "BufferGL::BufferGL:" << std::endl;
        std::cout << "sizeFromGL != expectedSize" << std::endl;
    }
}

void BufferGL::InitBuffers(void)
{
    SL_ASSERT(glHandle == 0);
    glGenBuffersARB(1, &glHandle);

    glBindBufferARB(glBufferType, glHandle);

    if (bufferProperties.useStagingBuffer == false && bufferProperties.useMapUnmap) {
        glNamedBufferStorage(glHandle, size, 0, GL_MAP_WRITE_BIT);
    } else {
        if (bufferProperties.useStagingBuffer) {
            SL_ASSERT(stagingBuffer == 0);
            stagingBuffer = SL_NEW unsigned char[size];
            memset(stagingBuffer, 0, size);
            glNamedBufferData(glHandle, size, 0, GL_DYNAMIC_DRAW);
            glNamedBufferSubData(glHandle, 0, size, stagingBuffer);
        } else {
            glNamedBufferData(glHandle, size, 0, GL_DYNAMIC_DRAW);
        }
    }

    glBindBufferARB(glBufferType, 0);

    if (OpenGLUtils::enableDebugOutput) {
        checkSize(glHandle, size);
    }
}

void BufferGL::DeInitBuffers(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    // Invalidate the buffer to force the release of the memory
    actualStream->glBindBufferARB(glBufferType, this);
    glNamedBufferData(glHandle, 0, NULL, GL_DYNAMIC_DRAW);
    actualStream->glBindBufferARB(glBufferType, (BufferGL*)0);

    actualStream->glDeleteBufferARB(glHandle);
    glHandle = 0;

    if (bufferProperties.useStagingBuffer && stagingBuffer) {
        SL_DELETE[] stagingBuffer;
        stagingBuffer = 0;
    }
    OpenGLUtils::CheckError(__LINE__);
}

void BufferGL::Respec(int newSizeInBytes, void* _context)
{
    size = newSizeInBytes;
    InitBuffers();
    OpenGLUtils::CheckError(__LINE__);
}

void BufferGL::SyncToGpu(int offset, int sizeInBytes, void* pSrcData, void* _context)
{
    glNamedBufferSubData(glHandle, offset,sizeInBytes, pSrcData);
}

void* BufferGL::Lock(int offset, int sizeInBytes, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    if (bufferProperties.useStagingBuffer==false) {
        SL_ASSERT(actualStream->isImmediate());
        void* pData = glMapNamedBufferRange(glHandle, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        OpenGLUtils::CheckError(__LINE__);
        return pData;
    } else {
        return (stagingBuffer + offset);
    }
}

void BufferGL::Unlock(int offset, int sizeInBytes, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    if (bufferProperties.useStagingBuffer==false) {
        SL_ASSERT(actualStream->isImmediate());
        glUnmapNamedBuffer(glHandle);
    } else {
        void* pData = stagingBuffer + offset;
        actualStream->glNamedBufferSubData(glHandle, offset, sizeInBytes, pData, true);
    }
}

void BufferGL::Bind(int location, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    actualStream->glBindBufferBase(glBufferType, location, glHandle);
    actualStream->checkGlError(__LINE__);
}
void BufferGL::UnBind(int location, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    actualStream->glBindBufferBase(glBufferType, location, 0);
    actualStream->checkGlError(__LINE__);
}
}