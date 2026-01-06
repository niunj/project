#pragma once

#include "MemAlloc.h"
#include "SilverLiningOpenGLPreamble.h"
#include "SLVector.h"
#include "VertexAttribLocations.h"
#include "SLMap.h"
#include <string>

namespace SilverLining
{
    class Context;
    class ResourceLoader;
    class VAOManagerOpenGL;
    class OpenGlStream;

    class Shader : public MemObject
    {
    public:
        Shader(Context *pContext, bool pUseUBOs, bool pUseProgramUniforms);

        ~Shader();

        void Bind();
        void PreDraw();
        void Unbind();

        void DeleteVAOsForBuffer(GLuint buffNum);
        GLuint GetOrCreateVAOForContextAndBuffer(GLuint buffNum);

		int GetUniformLocation(const char* name);
		void SetConstantVector4AtLocation(int loc, const float *data);
		void SetConstantMatrix4AtLocation(int loc, const float *data);

        bool SetConstantVector4(const char *varName, const float *data);
        bool SetConstantInt(const char *varName, int val);
        bool SetConstantMatrix4(const char *varName, const float *data);

        bool PreConstantsSet(void) const;
        bool PostConstantsSet(void) const;

        bool LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
            , const SL_VECTOR(SL_STRING)& defines
            , ResourceLoader* resourceLoader
            , const SL_VECTOR(unsigned int)& userShaderList);

        const VertexAttribLocations& GetVertexAttribLocations(void) const;

        GLuint glProgram;
        Context *context;
        
        static const char * userDefinedVertString;
        static const char * userDefinedFragString;
        static const char * userVertFileName;
        static const char * userFragFileName;

    private:

#if ((defined(WIN32) || defined(WIN64)) && (_MSC_VER < 1800)) || defined(LINUX)
        typedef SL_MAP(std::string, GLint) TUBOMap;
		typedef SL_MAP(std::string, GLint) TUniformMap;
#else
		typedef SL_MAP(SL_STRING, GLint) TUBOMap;
		typedef SL_MAP(SL_STRING, GLint) TUniformMap;
#endif

		TUniformMap uniformMap;

		TUBOMap uboMap;
		GLint uboBinding;
        GLuint ubo;
        GLbyte* uboData;
        int uboSize;

        const bool useUBOs;
        const bool useProgramUniforms;

        GLuint glVertShader, glFragShader;
        GLuint glUserVertShader, glUserFragShader;

        bool CreateUboMap(GLuint program);
        GLint GetUboOffset(const char* name);
		GLint GetOrCreateUniformEntry(const char* name);

        VertexAttribLocations vertexAttribLocations;

        VAOManagerOpenGL* vaoManager;

        // de-reference to improve caching
        OpenGlStream* stream;
    };
}
