#include "Shader.h"
#include "SilverLiningTypes.h"
#include "OpenGLUtils.h"
#include "Vertex.h"
#include "ResourceLoader.h"
#include "OpenGLExtensionManager.h"
#include "VAOManagerOpenGL.h"
#include "Context.h"
#include "OpenGLStream.h"
#include "SLAssert.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <string>

using namespace std;

namespace SilverLining
{
Shader::Shader(Context *pContext, bool pUseUBOs, bool pUseProgramUniforms) : context(pContext), glVertShader(0), glFragShader(0), glProgram(0),
    vertexAttribLocations(0, 0, 0), glUserVertShader(0), glUserFragShader(0),
    ubo(0), uboSize(0), uboBinding(0), uboData(NULL), useUBOs(pUseUBOs), useProgramUniforms(pUseProgramUniforms)
{
    vaoManager = VAOManagerOpenGL::instance();
    vaoManager->AddRef();

    stream = context->GetStream();
}

Shader::~Shader()
{
    glDetachShader(glProgram, glVertShader);
    if (glFragShader) {
        glDetachShader(glProgram, glFragShader);
        glDeleteShader(glFragShader);
    }
    if (glUserVertShader) {
        glDetachShader(glProgram, glUserVertShader);
        glDeleteShader(glUserVertShader);
    }
    if (glUserFragShader) {
        glDetachShader(glProgram, glUserFragShader);
        glDeleteShader(glUserFragShader);
    }
    glDeleteShader(glVertShader);
    glDeleteProgram(glProgram);

    vaoManager->RemoveRef();

    if (ubo) {
        glDeleteBuffers(1, &ubo);
        uboSize = 0;
        ubo = 0;
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    if (uboData) {
        SL_FREE(uboData);
        uboData = 0;
    }
}

void Shader::DeleteVAOsForBuffer(GLuint buffNum)
{
    vaoManager->DeleteVAOsForBuffer(buffNum);
}

GLuint Shader::GetOrCreateVAOForContextAndBuffer(GLuint bufNum)
{
    return vaoManager->GetOrCreateVAOForContextAndBuffer(bufNum, vertexAttribLocations);
}

void Shader::Bind()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    actualStream->glUseProgram(glProgram);
    if (ubo) {
        actualStream->glBindBufferBase(GL_UNIFORM_BUFFER, uboBinding, ubo);
    }
    actualStream->checkGlError(__LINE__);
}

void Shader::PreDraw()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    if (ubo && uboData) {
        actualStream->glNamedBufferSubData(ubo, 0, uboSize, uboData, true);
    }
    actualStream->checkGlError(__LINE__);
}

void Shader::Unbind()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    if (ubo) {
        actualStream->glBindBufferBase(GL_UNIFORM_BUFFER, uboBinding, 0);
    }

    actualStream->glUseProgram(0);
    actualStream->checkGlError(__LINE__);
}

GLint Shader::GetUboOffset(const char* name)
{
    if (ubo) {
        TUBOMap::const_iterator found = uboMap.find(name);
        if (found != uboMap.end()) {
            stream->checkGlError(__LINE__);
            return found->second;
        }
    }
    return -1;
}

GLint Shader::GetOrCreateUniformEntry(const char* varName)
{
    // check cached uniforms; if not found, fetch & cache
    GLint loc = -1;
    TUniformMap::const_iterator found = uniformMap.find(varName);
    if (found != uniformMap.end()) {
        loc = found->second;
    } else {
        loc = glGetUniformLocation(glProgram, varName);
        uniformMap.insert(Shader::TUniformMap::value_type(varName, loc));
    }
    return loc;
}

int Shader::GetUniformLocation(const char* varName)
{
    if (ubo) {
        return GetUboOffset(varName);
    }
    return GetOrCreateUniformEntry(varName);
}

bool Shader::PreConstantsSet() const
{
    if (useUBOs == false && useProgramUniforms == false && glProgram) {
        stream->glUseProgram(glProgram);
    }
    return true;
}
bool Shader::PostConstantsSet() const
{
    if (useUBOs == false && useProgramUniforms == false && glProgram) {
        stream->glUseProgram(0);
    }
    return true;
}

void Shader::SetConstantVector4AtLocation(int loc, const float *data)
{
    if (loc == -1) {
        return;
    }
    if (ubo && uboData) {
        memcpy(uboData + loc, data, sizeof(float)* 4);
    } else {
        if (useProgramUniforms) {
            stream->glProgramUniform4f(glProgram, loc, data[0], data[1], data[2], data[3]);
        } else {
            stream->glUniform4f(loc, data[0], data[1], data[2], data[3]);
        }
    }
}
void Shader::SetConstantMatrix4AtLocation(int loc, const float *data)
{
    if (loc == -1) {
        return;
    }

    if (ubo && uboData) {
        float t[16] = { data[0], data[4], data[8], data[12],
                        data[1], data[5], data[9], data[13],
                        data[2], data[6], data[10], data[14],
                        data[3], data[7], data[11], data[15]
                      };
        memcpy(uboData + loc, t, sizeof(float)* 16);
    } else {
        if (useProgramUniforms) {
            stream->glProgramUniformMatrix4fv(glProgram, loc, 1, 1, data);
        } else {
            stream->glUniformMatrix4fv(loc, 1, 1, data);
        }
    }
}

bool Shader::SetConstantVector4(const char *varName, const float *data)
{
    GLint offset = GetUboOffset(varName);
    if (offset >= 0) {
        memcpy(uboData + offset, data, sizeof(float)* 4);
    } else {
        GLint loc = GetOrCreateUniformEntry(varName);
        stream->checkGlError(__LINE__);
        if (loc != -1) {
            if (useProgramUniforms) {
                stream->glProgramUniform4f(glProgram, loc, data[0], data[1], data[2], data[3]);
            } else {
                stream->glUniform4f(loc, data[0], data[1], data[2], data[3]);
            }
            stream->checkGlError(__LINE__);
        }
    }

    return true;
}

bool Shader::SetConstantInt(const char *varName, int val)
{
    GLint offset = GetUboOffset(varName);
    if (offset >= 0) {
        memcpy(uboData + offset, &val, sizeof(int));
    } else {
        GLint loc = GetOrCreateUniformEntry(varName);
        stream->checkGlError(__LINE__);
        if (loc != -1) {
            if (useProgramUniforms) {
                stream->glProgramUniform1i(glProgram, loc, val);
            } else {
                stream->glUniform1i(loc, val);
            }
            stream->checkGlError(__LINE__);
        }
    }
    return true;
}

bool Shader::SetConstantMatrix4(const char *varName, const float *data)
{
    GLint offset = GetUboOffset(varName);
    if (offset >= 0) {
        float t[16] = { data[0], data[4], data[8], data[12],
                        data[1], data[5], data[9], data[13],
                        data[2], data[6], data[10], data[14],
                        data[3], data[7], data[11], data[15]
                      };
        memcpy(uboData + offset, t, sizeof(float)* 16);
    } else {
        GLint loc = GetOrCreateUniformEntry(varName);
        stream->checkGlError(__LINE__);
        if (loc != -1) {
            if (useProgramUniforms) {
                stream->glProgramUniformMatrix4fv(glProgram, loc, 1, 1, data);
            } else {
                stream->glUniformMatrix4fv(loc, 1, 1, data);
            }

            stream->checkGlError(__LINE__);
        }
    }
    return true;
}

const char * Shader::userDefinedVertString = 0;
const char * Shader::userDefinedFragString = 0;
const char * Shader::userVertFileName = 0;
const char * Shader::userFragFileName = 0;

static char* MakeFilename(const char *cgFileName, bool fragFile)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return 0;

    int extraChars = fragFile ? 9 : 4;
    char *glslFileName = SL_NEW char[len + extraChars + 1];
    memset(glslFileName, 0, len + extraChars + 1);

#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
    strncpy_s(glslFileName, len + extraChars + 1, cgFileName, len - 3);
    if (fragFile) {
        strcat_s(glslFileName, len + extraChars + 1, "-frag.glsl15");
    } else {
        strcat_s(glslFileName, len + extraChars + 1, ".glsl15");
    }
#else
    strncpy(glslFileName, cgFileName, len - 3);
    if (fragFile) {
        strcat(glslFileName, "-frag.glsl15");
    } else {
        strcat(glslFileName, ".glsl15");
    }
#endif

    return glslFileName;
}

static std::string CommonFileName(const char* cgFileName)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return 0;

    struct LocalChar {
        public:
        LocalChar(int size)
        {
            data = SL_NEW char[size];
        }
        ~LocalChar()
        {
            SL_DELETE [] data;
        }
        char* data;
    };

    const int extraChars = 11;

    LocalChar localChar(len + extraChars + 1);
    char* commonFileName = localChar.data;
    memset(commonFileName, 0, len + extraChars + 1);

#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
    strncpy_s(commonFileName, len + extraChars + 1, cgFileName, len - 3);
    strcat_s(commonFileName, len + extraChars + 1, "-common.glsl15");
#else
    strncpy(commonFileName, cgFileName, len - 3);
    strcat(commonFileName, "-common.glsl15");
#endif

    return  std::string(commonFileName);
}

static void DumpShaderObjectSource(GLuint obj, const char *fname)
{
    if (OpenGLUtils::enableDebugOutput) {
        // Dump shader source to stdout to aid debugging
        int sourceLen = 0;
        glGetShaderiv(obj, GL_SHADER_SOURCE_LENGTH, &sourceLen);
        OpenGLUtils::CheckError(__LINE__);

        char* buf = (char*)malloc(sourceLen+1);
        memset(buf, 0, sourceLen+1);
        int actualSize = 0;
        glGetShaderSource(obj, sourceLen, &actualSize, buf);
        OpenGLUtils::CheckError(__LINE__);

        printf("-------- BEGIN SHADER SOURCE:  '%s' --------\n", fname);
        printf("%s\n", buf);
        printf("-------- END SHADER SOURCE:    '%s' --------\n", fname);
        free(buf);
    }
}

static void PrintGlslCompilationError(GLuint obj, const char *fname)
{
    if (OpenGLUtils::enableDebugOutput) {
        printf("GLSL compilation error detected in %s!\n", fname);

        int infologLength = 0;
        glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB,
                                  &infologLength);

        if (infologLength > 0) {
            char* infoLog = (char *)SL_MALLOC(infologLength);
            int charsWritten = 0;
            glGetShaderInfoLog(obj, infologLength, &charsWritten, infoLog);
            printf("%s\n", infoLog);
            SL_FREE(infoLog);
        }
        OpenGLUtils::CheckError(__LINE__);

        DumpShaderObjectSource(obj, fname);
    }
}

static void PrintGlslLinkError(GLuint program, const char* fname)
{
    if (OpenGLUtils::enableDebugOutput) {
        printf("GLSL link error detected in %s!\n", fname);

        int infologLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infologLength);

        if (infologLength > 0) {
            char *infoLog = (char *)SL_MALLOC(infologLength);
            int charsWritten = 0;
            glGetProgramInfoLog(program, infologLength, &charsWritten, infoLog);
            printf("%s\n", infoLog);
            SL_FREE(infoLog);
        }
        OpenGLUtils::CheckError(__LINE__);

        DumpShaderObjectSource(program, fname);
    }
}

static std::string RemoveVersionAndExtensionsLines(std::string& version,
        SL_VECTOR(std::string)& extensions,
        const std::string& shaderSource)
{
    std::stringstream ss;
    ss << shaderSource;

    std::stringstream ssNew;

    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty() == false) {
            if (line.find("#version") != std::string::npos && line.find("#version") == 0) {
                version = line;
            } else if (line.find("#extension") != std::string::npos && line.find("#extension") == 0) {
                extensions.push_back(line);
            } else {
                ssNew << line << std::endl;
            }
        }
    }

    return ssNew.str();
}

static GLuint LoadShader(const char *glslFileName
                         , const std::string& glslFileNameCommon
                         , int shaderType
                         , bool userShader
                         , const char* userString
                         , const SL_VECTOR(SL_STRING)& topLevelDefines
                         , ResourceLoader* resourceLoader
                         , bool useUBOs)
{
    if (!glslFileName) return 0;

    const char *userDefines = (userString && userShader) ? userString : "";

    char* shaderSource = 0;
    unsigned int sourceLength = 0;
    const bool shaderLoaded = resourceLoader->LoadResource(glslFileName, shaderSource, sourceLength, true);

    if (shaderLoaded == false) {
        std::cout << "could not shader file: " << std::string(glslFileName)<< std::endl;
        OpenGLUtils::CheckError(__LINE__);
        return 0;
    }

    // try to load the common source
    char* shaderSourceCommon = 0;
    unsigned int commonSourceLength = 0;
    const bool shaderLoadedCommon = resourceLoader->LoadResource(glslFileNameCommon.c_str(), shaderSourceCommon, commonSourceLength, true);

    if (shaderLoaded) {
        GLuint shaderObject;
        switch (shaderType) {
        case VERTEX_PROGRAM:
            shaderObject = glCreateShader(GL_VERTEX_SHADER);
            break;
        case FRAGMENT_PROGRAM:
            shaderObject = glCreateShader(GL_FRAGMENT_SHADER);
            break;
        default:
            break;
        }

        std::string defines;
        if (shaderLoadedCommon) {
            std::string strShaderSource(shaderSource);

            const bool isInstanceParticleShader = glslFileNameCommon.find("Particle-instanced") != std::string::npos;
            const bool isInstancedBillboardShader = glslFileNameCommon.find("Billboard-common") != std::string::npos
                                                    && topLevelDefines.empty() == false && topLevelDefines[0] == SL_STRING("BILLBOARD_SHADER_INSTANCED");
            if (isInstanceParticleShader) {
                defines += "#version 450\n";
                defines += "#extension GL_ARB_bindless_texture : require\n";
                defines += "#extension GL_ARB_gpu_shader_int64 : require\n";
            } else {
                if (isInstancedBillboardShader) {
                    defines += "#version 460\n";
                } else {
                    defines += "#version 450\n";
                }
            }

            if (isInstancedBillboardShader) {
                defines += "#extension GL_ARB_shader_storage_buffer_object : enable\n#define BILLBOARD_SHADER_INSTANCED\n";
                defines += "#extension GL_ARB_shader_draw_parameters : enable\n";
                defines += "#extension GL_ARB_gpu_shader_int64 : enable\n";
            } else {
                if (useUBOs) {
                    defines += "#extension GL_ARB_uniform_buffer_object : enable\n#define USE_UBO\n";
                }
            }

            if (userShader) {
                std::string userVersion;//not used
                SL_VECTOR(std::string) userExtensions;
                strShaderSource = RemoveVersionAndExtensionsLines(userVersion, userExtensions, std::string(shaderSource));

                defines += "#define OPENGL32\n";
                defines += std::string(userDefines);
                defines += "\n";

                for(const std::string& userExtension : userExtensions) {
                    defines += userExtension;
                    defines += "\n";
                }
            }

            // const char *sources[3] = { defines.c_str(), shaderSourceCommon, shaderSource };
            const char *sources[3] = { defines.c_str(), shaderSourceCommon, strShaderSource.c_str() };
            glShaderSource(shaderObject, 3, sources, NULL);
        } else {
            std::string strShaderSource(shaderSource);
            if (userShader) {
                std::string userVersion;
                SL_VECTOR(std::string) userExtensions;

                strShaderSource = RemoveVersionAndExtensionsLines(userVersion, userExtensions, std::string(shaderSource));

                if(userVersion.empty()) {
                    defines += "#version 450\n";
                }

                defines += "#define OPENGL32\n";
                defines += std::string(userDefines);
                defines += "\n";

                for(const std::string& extensionString : userExtensions) {
                    defines += extensionString;
                    defines += "\n";
                }

                const bool isParticleShader = glslFileNameCommon.find("Particle") != std::string::npos;
                if (isParticleShader) {
                    defines += "#define PARTICLE_SHADER\n";
                }
            }

            // const char *sources[2] = { defines.c_str(), shaderSource };
            const char *sources[2] = { defines.c_str(), strShaderSource.c_str() };
            glShaderSource(shaderObject, 2, sources, NULL);
        }

        glCompileShader(shaderObject);

        resourceLoader->FreeResource(shaderSource);

        if (shaderLoadedCommon && shaderSourceCommon) {
            resourceLoader->FreeResource(shaderSourceCommon);
        }

        GLint ok;
        glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            PrintGlslCompilationError(shaderObject, glslFileName);
            return 0;
        } else {
            OpenGLUtils::CheckError(__LINE__);
            return shaderObject;
        }
    }

    OpenGLUtils::CheckError(__LINE__);
    return 0;
}

bool Shader::CreateUboMap(GLuint program)
{
    bool hasUBO = false;

    GLint numBlocks = 0;
    glGetProgramInterfaceiv(program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &numBlocks);

    const int numBlockProperties = 4;
    const GLenum blockPropertiesToGet[numBlockProperties] = { GL_NUM_ACTIVE_VARIABLES, GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING, GL_NAME_LENGTH };
    const GLenum uniformPropertiesToGet[4] = { GL_NAME_LENGTH, GL_TYPE, GL_LOCATION, GL_OFFSET };
    const GLenum activeUnifProp[1] = { GL_ACTIVE_VARIABLES };

    for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex) {

        GLint blockProperties[numBlockProperties];
        glGetProgramResourceiv(program, GL_UNIFORM_BLOCK, blockIndex, numBlockProperties, blockPropertiesToGet, numBlockProperties, NULL, blockProperties);

        // name invalid
        const int uboNameLength = blockProperties[3];
        if (uboNameLength == 0) {
            continue;
        }

        std::vector<char> uboNameData(uboNameLength);
        glGetProgramResourceName(program, GL_UNIFORM_BLOCK, blockIndex, uboNameData.size(), NULL, &uboNameData[0]);
        const std::string uboName(uboNameData.begin(), uboNameData.end() - 1);

        // skip if it is not an sl ubo that we are looking for
        if (uboName != "sl_ShaderParameters_UBO") {
            continue;
        }

        // confirm that we have some active variables
        const GLint numActiveUnifs = blockProperties[0];
        if (numActiveUnifs == 0) {
            continue;
        }

        uboSize = blockProperties[1];

        uboBinding = blockProperties[2];

        std::vector<GLint> blockUnifs(numActiveUnifs);
        glGetProgramResourceiv(program, GL_UNIFORM_BLOCK, blockIndex, 1, activeUnifProp, numActiveUnifs, NULL, &blockUnifs[0]);

        for (int uniformIndex = 0; uniformIndex < numActiveUnifs; ++uniformIndex) {
            GLint uniformProperties[4];
            glGetProgramResourceiv(program, GL_UNIFORM, blockUnifs[uniformIndex], 4, uniformPropertiesToGet, 4, NULL, uniformProperties);

            // Get the name. Must use a std::vector rather than a std::string for C++03 standards issues.
            // C++11 would let you use a std::string directly.
            std::vector<char> uniformNameData(uniformProperties[0]);
            glGetProgramResourceName(program, GL_UNIFORM, blockUnifs[uniformIndex], (GLsizei)uniformNameData.size(), NULL, &uniformNameData[0]);

            const GLint location = uniformProperties[2];
            const GLint offset = uniformProperties[3];

            if (location == -1) {
#if ((defined(WIN32) || defined(WIN64)) && (_MSC_VER < 1800)) || defined(LINUX)
                std::string uniformName(uniformNameData.begin(), uniformNameData.end() - 1);
#else
                SL_STRING uniformName(uniformNameData.begin(), uniformNameData.end() - 1);
#endif
                uboMap[uniformName] = offset;

                hasUBO = true;
            }
        }

        // we can break out because we found and processed our UBO at this point
        break;
    }
    OpenGLUtils::CheckError(__LINE__);
    return hasUBO;
}

bool Shader::LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling
                                , const SL_VECTOR(SL_STRING)& defines
                                , ResourceLoader* resourceLoader
                                , const SL_VECTOR(unsigned int)& userShaderList)
{
    // We automatically look for a companion -frag.glsl15 source file too, since GLSL doesn't have effect
    // files to keep it neat...
    char *vertexShaderFileName = MakeFilename(fileName, false);
    char *fragmentShaderFileName = MakeFilename(fileName, true);

    std::string commonFileName = CommonFileName(fileName);

    GLuint vertexShader = LoadShader(vertexShaderFileName, commonFileName, VERTEX_PROGRAM, false, userDefinedVertString, defines, resourceLoader, useUBOs);
    GLuint fragmentShader = LoadShader(fragmentShaderFileName, commonFileName, FRAGMENT_PROGRAM, false, userDefinedFragString, defines, resourceLoader, useUBOs);

    GLuint userVertexShader = LoadShader(userVertShader, commonFileName, VERTEX_PROGRAM, true, userDefinedVertString, defines, resourceLoader, useUBOs);
    if (userVertexShader == 0 && OpenGLUtils::enableDebugOutput) {
        std::cout << "could not load user vertex shader for: " << std::endl;
        std::cout << "\t vertex  shader: " << std::string(vertexShaderFileName) << std::endl;
        std::cout << "\t fragment shader: " << std::string(fragmentShaderFileName) << std::endl;
    }
    GLuint userFragmentShader = LoadShader(userFragShader, commonFileName, FRAGMENT_PROGRAM, true, userDefinedFragString, defines, resourceLoader, useUBOs);
    if (userFragmentShader == 0 && OpenGLUtils::enableDebugOutput) {
        std::cout << "could not load user fragment shader for: " << std::endl;
        std::cout << "\t vertex  shader: " << std::string(vertexShaderFileName) << std::endl;
        std::cout << "\t fragment shader: " << std::string(fragmentShaderFileName) << std::endl;
    }

    if (enableObjectLabeling && glObjectLabel) {
        SL_STRING strName;

        strName = SL_STRING("SilverLining Vertex Shader: ") + SL_STRING(vertexShaderFileName);
        glObjectLabel(GL_SHADER, *((unsigned int*)(&vertexShader)), -1, strName.c_str());

        strName = SL_STRING("SilverLining Fragment Shader: ") + SL_STRING(fragmentShaderFileName);
        glObjectLabel(GL_SHADER, *((unsigned int*)(&fragmentShader)), -1, strName.c_str());

        if (userVertexShader) {
            strName = SL_STRING("SilverLining User Vertex Shader: ") + SL_STRING(userVertShader);
            glObjectLabel(GL_SHADER, *((unsigned int *)(&userVertexShader)), -1, strName.c_str());
        }

        if (userFragmentShader) {
            strName = SL_STRING("SilverLining User Fragment Shader: ") + SL_STRING(userFragShader);
            glObjectLabel(GL_SHADER, *((unsigned int*)(&userFragmentShader)), -1, strName.c_str());
        }
    }

    SL_DELETE[] vertexShaderFileName;
    SL_DELETE[] fragmentShaderFileName;

    GLuint programObject = glCreateProgram();
    glAttachShader(programObject, vertexShader);

    if (fragmentShader) {
        glAttachShader(programObject, fragmentShader);
    }

    if (userVertexShader) {
        glAttachShader(programObject, userVertexShader);
    }

    if (userFragmentShader) {
        glAttachShader(programObject, userFragmentShader);
    }

    if (context) {
        for (SL_VECTOR(unsigned int)::const_iterator it = userShaderList.begin(); it != userShaderList.end(); it++) {
            glAttachShader(programObject, *it);
        }
    }

    glBindFragDataLocation(programObject, 0, "fragColor");
    glLinkProgram(programObject);

    GLint ok;

    glGetProgramiv(programObject, GL_LINK_STATUS, &ok);

    if (!ok) {
        PrintGlslLinkError(programObject, fileName);
        return false;
    }

    if (useProgramUniforms == false) {
        stream->glUseProgram(programObject);
    }

    GLint texLoc = glGetUniformLocation(programObject, "sl_tex2D");
    if (texLoc != -1) {

        if (useProgramUniforms) {
            glProgramUniform1i(programObject, texLoc, 0);
        } else {
            glUniform1i(texLoc, 0);
        }

        texLoc = glGetUniformLocation(programObject, "sl_tex2D2");
        if (texLoc != -1) {
            if (useProgramUniforms) {
                glProgramUniform1i(programObject, texLoc, 1);
            } else {
                glUniform1i(texLoc, 1);
            }
        }
    } else {
        // Stratocumulus shader uses 2 stages
        texLoc = glGetUniformLocation(programObject, "sl_tex3D");
        if (texLoc != -1) {
            if (useProgramUniforms) {
                glProgramUniform1i(programObject, texLoc, 0);
            } else {
                glUniform1i(texLoc, 0);
            }
        }
        texLoc = glGetUniformLocation(programObject, "sl_tex3D2");
        if (texLoc != -1) {
            if (useProgramUniforms) {
                glProgramUniform1i(programObject, texLoc, 1);
            } else {
                glUniform1i(texLoc, 1);
            }
        }
    }

    if (useProgramUniforms == false) {
        stream->glUseProgram(0);
    }

    glProgram = programObject;
    glVertShader = vertexShader;
    glFragShader = fragmentShader;
    glUserVertShader = userVertexShader;
    glUserFragShader = userFragmentShader;

    if (useUBOs && CreateUboMap(glProgram)) {
        glGenBuffers(1, &ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(GL_UNIFORM_BUFFER, uboSize, 0, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        uboData = (GLbyte*)SL_MALLOC(uboSize);
    }

    // initialize the vertex attrib locdations
    vertexAttribLocations.InitFromProgram(glProgram);

    return true;
}
const VertexAttribLocations& Shader::GetVertexAttribLocations(void) const
{
    return vertexAttribLocations;
}
}
