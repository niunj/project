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
        SL_Buffer(GLenum _type, int _sizeInBytes, GLenum _usage, const char* _name, const BufferProperties& _bufferProperties);

        //! destructor
        ~SL_Buffer();
    public:
        virtual GLubyte* GetStagingBuffer() const;

        virtual bool SyncToGPU(int offset, int sizeInBytes, OpenGlStream* stream);
        virtual int GetSizeInBytes(void) const;

        virtual void InitBuffers(void);
        
        virtual void DeInitBuffers(OpenGlStream* stream);
        virtual void Respec(int sizeInBytes);

		virtual void MakeBufferResident(bool makeResident);
		virtual bool IsBufferResident(void) const;

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
        GLenum Type(void) const;
    public:
        GLuint handle;
    private:
        const GLenum type;

        int sizeInBytes;

        const GLenum usage;

        SL_STRING name;

        mutable GLubyte* stagingBuffer;

        const BufferProperties bufferProperties;

        bool locked;
    };
}