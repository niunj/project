// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "MemAlloc.h"
#include "SLMap.h"
#include "SLList.h"
#include "SilverLiningTypes.h"
#include "SilverLiningOpenGLPreamble.h"

namespace SilverLining
{
    class CubeMapFboManager : public MemObject
    {
    public:
        //! Get the instance. Create one if it does not
        //! exist
        static CubeMapFboManager* instance(void);

        //! Get the instance. Can be zero.
        static CubeMapFboManager* findInstance(void);

        // Heart beat function
        virtual void HeartBeat(void);

        //! add a ref
        virtual void AddRef();

        //! remove a ref
        virtual void RemoveRef();

        //! create an fbo & track the context it was created in
        unsigned int CreateFbo(void*& contextCreatedIn);

        //! delete an fbo
        void DestroyFbo(unsigned int fbo, void* contextCreatedIn);

    protected:
        //! constructor. protected, client cannot instantiate directly. use instance()
        CubeMapFboManager();

        //! constructor. protected, client cannot destruct directly. use addRef()/removeRef()
        //! when internal count gets to 0, self-destructs
        virtual ~CubeMapFboManager();

    protected:
#if defined(WIN32) || defined(WIN64)
        typedef SL_MAP(HGLRC, SL_SET(GLuint)) MapContextsToCubeMapFBOs;
#elif defined(LINUX)
        typedef SL_MAP(GLXContext, SL_SET(GLuint)) MapContextsToCubeMapFBOs;
#else
#error "Target platform not defined"
#endif

        MapContextsToCubeMapFBOs mapContextsToCubeMapFbos;

        MapContextsToCubeMapFBOs mapContextsToCubeMapFbosToDelete;

        // The internal ref count
        int refCount;

        // the solitary instance
        static CubeMapFboManager* theInstance;
    };
}