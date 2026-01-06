#pragma once
#include "Buffer.h"
#include "SilverLiningOpenGLPreamble.h"

namespace SilverLining
{
    class OpenGlStream;
    class BufferGL : public Buffer
    {
    public:
        BufferGL(const char* name, BufferType type, int size, const BufferProperties& _bufferProperties);
        virtual ~BufferGL();

        virtual void* Lock(int offset, int sizeInBytes, void* _context);
        virtual void  Unlock(int offset, int sizeInBytes, void* _context);

        virtual void  Bind(int location, void* _context);
        virtual void  UnBind(int location, void* _context);

        virtual void DeInitBuffers(void* _context);
        virtual void Respec(int sizeInBytes, void* _context);
        virtual void SyncToGpu(int offset, int sizeInBytes, void* pSrcData, void* _context);

        virtual GLuint Handle(void) const { return glHandle; }
    private:
        void InitBuffers(void);

    private:
        GLuint glHandle;
        GLenum glBufferType;
        unsigned char* stagingBuffer;
    };
}