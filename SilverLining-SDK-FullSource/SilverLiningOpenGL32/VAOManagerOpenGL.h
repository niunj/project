// Copyright (c) 2011-2020 Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#include "SLMap.h"
#include "SLList.h"
#include "SilverLiningOpenGLPreamble.h"
#include "VertexAttribLocations.h"

namespace SilverLining
{
	class VAOManagerOpenGL : public MemObject
	{
	public:
		//! delete a vao for a given buffer and locations key
        virtual void DeleteVAOsForBuffer(GLuint buffNum);

        //! delete all the vaos for a given context
        virtual void ContextBeingDeleted(void* context);

		//! get or create and record a vao for a given buffer and locations key
        virtual GLuint GetOrCreateVAOForContextAndBuffer(GLuint buffNum, const VertexAttribLocations& locations);

		virtual GLuint GetOrCreateVAOForContext(const VertexAttribLocations& locations);

        //! add a ref
        virtual void AddRef();

        //! remove a ref
        virtual void RemoveRef();
    public:
        //! Get the instance
        static VAOManagerOpenGL* instance(void);

    public:
        //! tick function
        virtual void HeartBeat(void);
	protected:
        //! constructor. protected, client cannot instantiate directly. use instance()
        VAOManagerOpenGL();

        //! constructor. protected, client cannot destruct directly. use addRef()/removeRef()
        //! when internal count gets to 0, self-destructs
        ~VAOManagerOpenGL();

		//! internal. record a vao
        virtual void RecordVAO(GLuint vao, GLuint buffNum, const VertexAttribLocations& locations);

		//! internal. record a vao
		virtual void RecordVAO(GLuint vao, const VertexAttribLocations& locations);

        //! internal. print stats
        virtual void PrintStats(void) const;

	protected:

        typedef SL_MAP(GLuint, GLuint) MapVBOsToVAOs;
#if defined(WIN32) || defined(WIN64)
        typedef SL_MAP(HGLRC, MapVBOsToVAOs) MapContextsToMapVBOsToVAOs;
#elif defined(LINUX)
        typedef SL_MAP(GLXContext, MapVBOsToVAOs) MapContextsToMapVBOsToVAOs;
#elif defined(MAC)
        typedef SL_MAP(void*, MapVBOsToVAOs) MapContextsToMapVBOsToVAOs;
#endif
		// Map of contexts to a map of VBOs to VAOs for that context
		MapContextsToMapVBOsToVAOs mapGLContextsToMapVBOsToVAOs;

#if defined(WIN32) || defined(WIN64)
		typedef SL_MAP(HGLRC, GLuint) MapGlContextsToVaos;
#elif defined(LINUX)
		typedef SL_MAP(GLXContext, GLuint) MapGlContextsToVaos;
#elif defined(MAC)
		typedef SL_MAP(void*, GLuint) MapGlContextsToVaos;
#endif

		MapGlContextsToVaos mapGlContextsToVaos;

#if defined(WIN32) || defined(WIN64)
        typedef SL_MAP(HGLRC, SL_LIST(GLuint)) MapContextsToVAOQueueToDelete;
#elif defined(LINUX)
        typedef SL_MAP(GLXContext, SL_LIST(GLuint)) MapContextsToVAOQueueToDelete;
#elif defined(MAC)
        typedef SL_MAP(void*, MapVBOsToVAOs) MapContextsToVAOQueueToDelete;
#endif
        // Map of contexts to a queue of VAOs to delete for that context
        // which could not be deleted earlier
        MapContextsToVAOQueueToDelete mapContextsToVAOQueueToDelete;

        // The internal ref count
        int refCount;

        // the solitary instance
        static VAOManagerOpenGL* theInstance;
	};
}