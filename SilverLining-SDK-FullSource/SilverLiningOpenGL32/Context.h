// Copyright (c) 2004-2020  Sundog Software, LLC All rights reserved worldwide.

/**
\file Context.h
\brief Classes that declare a container for the SilverLining OpenGL32 renderer
*/

#pragma once
#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "MatrixGL.h"
#include "SLVector.h"
#include "MutexContainer.h"
#include "LineShader.h"
#include "DrawIndirectCommands.h"
#include "BufferProperties.h"
#include "SLList.h"
#include <stack>

namespace SilverLining
{
    class Shader;
    class ResourceLoader;
    class OpenGlStream;
    class SL_Buffer;
	class VAOManagerOpenGL;
    
    class Context : public MemObject
    {
    public:
        Context(bool _useUBOs, bool _useProgramUniforms, bool _avoidStalls, bool _deferred);
        ~Context();
    private:
        Context(const Context& rhs);
        Context& operator=(const Context& rhs);

    public:
        virtual void DeleteShader(ShaderHandle shaderHandle);
        virtual ShaderHandle LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
            , const SL_VECTOR(SL_STRING)& defines
            , ResourceLoader* resourceLoader);
        virtual void ShutdownShaderSystem(void);

        virtual const LineShader& GetLineShader(const char* userVertFileName, const char* userFragFileName, ResourceLoader* resourceLoader);

        virtual OpenGlStream* GetStream(void){ return stream; }
        virtual OpenGlStream* GetImmediateStream(void){ return immediateStream; }

        virtual void SetDepthRange(float zmin, float zmax);

        virtual bool GetCullClockWise(void) const;
        virtual void SetCullClockWise(bool _cullClockWise);
        
        virtual bool GetIsReverseDepth(void) const;
        virtual void SetIsReverseDepth(bool reverse);

        virtual void SetDefaultState(void);
        virtual void PushAllState(void);
        virtual void PopAllState(void);
        virtual void SetViewport(int x, int y, int w, int h);

        virtual bool SetVertexBuffer(VertexBufferHandle vbh);
        virtual bool UnsetVertexBuffer();

        virtual bool SetIndexBuffer(IndexBufferHandle ibh);
        virtual bool UnsetIndexBuffer();

        virtual bool BindShader(ShaderHandle shader);
        virtual bool UnbindShader(void);

        virtual bool DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw);
		virtual bool DrawMultiStrips(const std::vector<DrawElementsIndirectBindlessCommandNV>& commandBuffer);
        virtual bool DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int count);
        virtual bool DrawPoints(double pointSize, int nPoints, int start);

        virtual void EnableBackfaceCulling(bool enable);
        virtual void EnableTexture(TextureHandle tex, int stage);
        virtual void Enable3DTexture(TextureHandle tex, int stage);
        virtual void DisableTexture(int stage);

        Shader* GetCurrentShader(void) {
            return currentShader;
        }
        virtual void SetForceImmediate(bool val);
        virtual bool GetForceImmediate(void) const;

        SL_Buffer* GetAnIndexBuffer(int nIndices, const char* name, const BufferProperties& bufferProperties);
        void ReturnAnIndexBuffer(SL_Buffer* indexBuffer);

        SL_Buffer* GetAVertexBuffer(int numVertices, const char* name, const BufferProperties& bufferProperties);
        void ReturnAVertexBuffer(SL_Buffer* vertexBuffer);
    public:
        MatrixGL mProjection;
        MatrixGL mModelview;
        MatrixGL mTexture;

        bool useUBOs;
        bool useProgramUniforms;
        bool avoidStalls;

        bool deferred;

        SL_VECTOR(unsigned int) userShaderList;

    private:
        SL_LAZY<SL_VECTOR(Shader *)> shaderList;

        mutable LineShader ls;

        OpenGlStream* stream;

        OpenGlStream* immediateStream;
        bool forceImmediate;

        bool cullClockWise;
        bool reverseDepth;

        Shader* currentShader;

        SL_DEQUE(SL_Buffer*) indexBuffersPool;
        SL_DEQUE(SL_Buffer*) vertexBuffersPool;

		VAOManagerOpenGL* vaoManager;

#define SILVERLINING_TRACK_CONTEXTS_AND_SHADERS 0
#if SILVERLINING_TRACK_CONTEXTS_AND_SHADERS
        static MutexContainer countMutexContainer;
        // overall in the system
        static int totalNumShaders;
        // for this context
        int numShaders;
        
        static int contextCounter;
        int id;
#endif

        typedef struct glState_S {
            unsigned char depthTest, blend, cullFace;
            unsigned char dither, colorLogicOp, polygonOffsetLine, polygonOffsetFill;
            unsigned char polygonOffsetPoint, polygonSmooth, scissorTest, stencilTest;
        } glState;

        std::stack<glState> stateStack;

        unsigned int drawIndirectBuffer = 0;
    };
}