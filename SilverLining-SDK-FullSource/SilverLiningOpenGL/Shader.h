#pragma once
#include "MemAlloc.h"
#include "SilverLiningOpenGLPreamble.h"
#include "SLVector.h"
#include <map>
#include <string>

namespace SilverLining
{
	class Context;
	class ResourceLoader;
    class OpenGlStream;

	class Shader : public MemObject
	{
	public:
        Shader(Context* _context, bool useUBOs, bool pUseProgramUniforms);

		virtual ~Shader();

		void PreDraw();

		GLhandleARB glShader, glFragShader, glProgram, glUserShader, glUserFragShader;

		int GetUniformLocation(const char* name);
		void SetConstantVector4AtLocation(int loc, const float *data);
		void SetConstantMatrix4AtLocation(int loc, const float *data);

		bool SetConstantVector4(const char* varName, const float* data);

		bool SetConstantMatrix4(const char *varName, float *data);

        bool PreConstantsSet(void) const;
        bool PostConstantsSet(void) const;

		bool LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
            , const SL_VECTOR(SL_STRING)& defines
            , ResourceLoader* resourceLoader, Shader*& activeShader
			, const SL_VECTOR(GLhandleARB)& userShaderList);

		void Bind();

		void Unbind();

		static const char* userDefinedVertString;
		static const char* userDefinedFragString;

		Context* GetContext(void) const;

	private:
        Context *context;

#if ((defined(WIN32) || defined(WIN64)) && (_MSC_VER < 1800)) || defined(LINUX)
		typedef SL_MAP(std::string, GLint) TUniformMap;
		typedef SL_MAP(std::string, GLint) TUBOMap;
#else
		typedef SL_MAP(SL_STRING, GLint) TUniformMap;
		typedef SL_MAP(SL_STRING, GLint) TUBOMap;
#endif
		TUniformMap uniformMap;

		const bool useUBOs;
        const bool useProgramUniforms;

		TUBOMap uboMap;
        GLint uboBinding;
		GLuint ubo;
		GLbyte* uboData;
		int uboSize;

		bool CreateUboMap(GLuint program);
		GLint GetUboOffset(const char* name);
		GLint GetOrCreateUniformEntry(const char* name);

        // de-reference to improve caching
        OpenGlStream* stream;
	};
}
