#pragma once

#include "SilverLiningOpenGLPreamble.h"
#include "SilverLiningTypes.h"
#include "DrawIndirectCommands.h"
#include "BufferProperties.h"
#include "MemAlloc.h"
#include "MatrixGL.h"
#include "SLVector.h"
#include "SLList.h"
#include <stack>
#include <stddef.h>

namespace SilverLining
{
	class Shader;
    class ResourceLoader;
    class OpenGlStream;
    class SL_Buffer;

	#define NUMR_PBO 2
	class Context : public MemObject
	{
	public:
        Context(bool _useUBOs, bool _useProgramUniforms, bool _avoidStalls, bool _deferred);

    public:
        //! shader functions

        //! delete
        virtual void DeleteShader(ShaderHandle shaderHandle);

        //! load
        virtual ShaderHandle LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
            , const SL_VECTOR(SL_STRING)& defines
            , ResourceLoader* resourceLoader);

        //! shutdown
        virtual void ShutdownShaderSystem(void);

        //! get active
        virtual Shader* GetActiveShader(void);

        //! bind/unbind
        virtual bool BindShader(ShaderHandle shader);
        virtual bool UnbindShader(void);

        virtual void EnableBackfaceCulling(bool enable);
        virtual void EnableTexture(TextureHandle tex, int stage);
        virtual void Enable3DTexture(TextureHandle tex, int stage);
        virtual void DisableTexture(int stage);

        virtual bool GetCullClockWise(void) const;
        virtual void SetCullClockWise(bool _cullClockWise);

        virtual bool GetIsReverseDepth(void) const;
        virtual void SetIsReverseDepth(bool reverse);

        virtual void InitReadback();

        virtual OpenGlStream* GetStream(void){ return stream; }
        virtual OpenGlStream* GetImmediateStream(void){ return immediateStream; }

        virtual void SetDepthRange(float zmin, float zmax);

        virtual void SetDefaultState(void);
        virtual void SetViewport(int x, int y, int w, int h);

        virtual void SetForceImmediate(bool val);
        virtual bool GetForceImmediate(void) const;

        virtual bool SetVertexBuffer(VertexBufferHandle vbh, bool vertexColors);
        virtual bool UnsetVertexBuffer();

        virtual bool SetIndexBuffer(IndexBufferHandle ibh);
        virtual bool UnsetIndexBuffer();

        virtual bool DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw);
        virtual bool DrawMultiStrips(const std::vector<DrawElementsIndirectBindlessCommandNV>& commandBuffer);
        virtual bool DrawPoints(double pointSize, int nPoints, int start);
        virtual bool DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int count);
        virtual bool DrawQuads(int nPoints, int start);

        virtual void PushAllState(void);
        virtual void PopAllState(void);

        SL_Buffer* GetAnIndexBuffer(int nIndices, const char* name, const BufferProperties& bufferProperties);
        void ReturnAnIndexBuffer(SL_Buffer* indexBuffer);

        SL_Buffer* GetAVertexBuffer(int numVertices, const char* name, const BufferProperties& bufferProperties);
        void ReturnAVertexBuffer(SL_Buffer* vertexBuffer);

    public:
		unsigned int contextId;

		MatrixGL mProjection;
		MatrixGL mModelview;
		MatrixGL mTexture;

		bool hasProjection, hasModelview, hasTexture, reverse_z;

        bool useUBOs;
        bool useProgramUniforms;
        bool avoidStalls;

        const bool deferred;

		// PBO stuff for pixel read-back
		GLuint m_pbos[NUMR_PBO]; // PBO pool
		int vram2sys;        // index of PBO used to copy from vram to sysmem
		int gpu2vram;        // index of PBO used to copy framebuffer to vram
		bool inited;
		int pboX, pboY;

		SL_VECTOR(GLhandleARB) userShaderList;

        SL_LAZY<SL_VECTOR(Shader*)> shaderList;

        unsigned int contextID;

        static unsigned int runningContextId;

        Shader* activeShader;

        bool cullClockWise;
        bool reverseDepth;


        SL_DEQUE(SL_Buffer*) indexBuffersPool;
        SL_DEQUE(SL_Buffer*) vertexBuffersPool;

    private:
        OpenGlStream* stream;

        OpenGlStream* immediateStream;
        bool forceImmediate;


        int iMaxClipPlanes;
        int iMaxTextures;

        std::stack<GLhandleARB> shaderStack;
        std::stack<GLuint> fboStack;
	};
}