// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"
#include "BufferProperties.h"

namespace SilverLining
{
    class OpenGlStream;
    class SL_Buffer : public MemObject
    {
    public:
        //! constructor
        SL_Buffer(GLenum _type, int _sizeInBytes, GLenum _usage, const char* _name, const BufferProperties& _bufferProperties, bool _bindless);

        //! destructor
        ~SL_Buffer();
    public:
        virtual GLubyte* GetStagingBuffer(void);

        virtual bool SyncToGPU(int offset, int sizeInBytes, OpenGlStream* stream);
        virtual int GetSizeInBytes(void) const;

        virtual void InitBuffers(void);

        virtual void DeInitBuffers(OpenGlStream* stream);
        virtual void Respec(int sizeInBytes);

        //! immediate mode method
        virtual bool SyncToGPU(int offset, int sizeInBytes, void* pSrcData);
        //! immediate mode method
        virtual void GetData(int offset, int sizeInBytes, void* pDstData);

        //! immediate mode method
        virtual void* Lock(int offset, int sizeInBytes);
        //! immediate mode method
        virtual bool UnLock();

        //! deferred method
        virtual bool SyncToGPU(int offset, int sizeInBytes, void* pSrcData, OpenGlStream* stream, bool justColors);

        //! type
        virtual GLenum Type(void) const;

        virtual void MakeBufferResident(bool makeResident);
        virtual bool IsBufferResident(void) const;

    public:
        GLuint handle;
        GLuint64EXT address;
    protected:
        const GLenum type;

        GLsizeiptr sizeInBytes;

        const GLenum usage;

        SL_STRING name;

        GLubyte* stagingBuffer;

        const BufferProperties bufferProperties;
        const bool bindless;

        bool locked;
    };
}