#include "Shader.h"
#include "OpenGLUtils.h"
#include "SilverLiningTypes.h"
#include "ResourceLoader.h"
#include "OpenGLUtils.h"
#include "Context.h"
#include "OpenGLStream.h"
#include "SLAssert.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdio>

extern int ExtensionSupported(const char *extension);

using namespace std;

namespace SilverLining
{
Shader::Shader(Context* _context, bool _useUBOs, bool _useProgramUniforms)
    : glShader(0), glFragShader(0), glUserShader(0), glUserFragShader(0), glProgram(0)
    , useUBOs(_useUBOs)
    , useProgramUniforms(_useProgramUniforms)
    , ubo(0)
    , uboSize(0)
    , uboBinding(0)
    , uboData(0)
    , context(_context)
{
    stream = context->GetStream();
}

Shader::~Shader()
{
    glDetachObjectARB(glProgram, glShader);
    if (glFragShader) {
        glDetachObjectARB(glProgram, glFragShader);
        glDeleteObjectARB(glFragShader);
    }
    if (glUserShader) {
        glDetachObjectARB(glProgram, glUserShader);
        glDeleteObjectARB(glUserShader);
    }
    if (glUserFragShader) {
        glDetachObjectARB(glProgram, glUserFragShader);
        glDeleteObjectARB(glUserFragShader);
    }
    glDeleteObjectARB(glShader);
    glDeleteObjectARB(glProgram);

    if (ubo) {
        glDeleteBuffersARB(1, &ubo);
        uboSize = 0;
        ubo = 0;
        glBindBufferARB(GL_UNIFORM_BUFFER, 0);
    }

    if (uboData) {
        SL_FREE(uboData);
        uboData = 0;
    }
}

void Shader::Bind()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    actualStream->glUseProgramObjectARB(*((unsigned int *)(&glProgram)));
    if (ubo) {
        actualStream->glBindBufferBase(GL_UNIFORM_BUFFER, uboBinding, ubo);
    }
}

void Shader::PreDraw()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    if (ubo && uboData) {
        actualStream->glNamedBufferSubData(ubo, 0, uboSize, uboData, true);
    }
}

void Shader::Unbind()
{
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    if (ubo) {
        actualStream->glBindBufferBase(GL_UNIFORM_BUFFER, uboBinding, 0);
    }
    actualStream->glUseProgramObjectARB(0);
}

GLint Shader::GetUboOffset(const char* name)
{
    if (ubo) {
        TUBOMap::const_iterator found = uboMap.find(name);
        if (found != uboMap.end()) {
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
        loc = glGetUniformLocationARB(glProgram, varName);
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
        stream->glUseProgramObjectARB(*((unsigned int *)(&glProgram)));
    }
    return true;
}
bool Shader::PostConstantsSet() const
{
    if (useUBOs == false && useProgramUniforms == false && glProgram) {
        stream->glUseProgramObjectARB(0);
    }
    return true;
}

void Shader::SetConstantVector4AtLocation(int loc, const float *data)
{
    stream->checkGlError(__LINE__);
    if (loc == -1) {
        return;
    }
    if (ubo && uboData) {
        memcpy(uboData + loc, data, sizeof(float)* 4);
    } else {
        if (useProgramUniforms) {
            stream->glProgramUniform4f(*((unsigned int *)(&glProgram)), loc, data[0], data[1], data[2], data[3]);
        } else {
            stream->glUniform4fARB(loc, data[0], data[1], data[2], data[3]);
        }
    }
    stream->checkGlError(__LINE__);
}
void Shader::SetConstantMatrix4AtLocation(int loc, const float *data)
{
    stream->checkGlError(__LINE__);
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
            stream->glProgramUniformMatrix4fv(*((unsigned int *)(&glProgram)), loc, 1, 1, data);
        } else {
            stream->glUniformMatrix4fvARB(loc, 1, 1, data);
        }
    }
    stream->checkGlError(__LINE__);
}

bool Shader::SetConstantVector4(const char* varName, const float* data)
{
    stream->checkGlError(__LINE__);
    GLint offset = GetUboOffset(varName);
    if (offset >= 0) {
        memcpy(uboData + offset, data, sizeof(float)* 4);
    } else {
        GLint loc = GetOrCreateUniformEntry(varName);
        if (loc != -1) {
            if (useProgramUniforms) {
                stream->glProgramUniform4f(*((unsigned int *)(&glProgram)), loc, data[0], data[1], data[2], data[3]);
            } else {
                stream->glUniform4fARB(loc, data[0], data[1], data[2], data[3]);
            }
        }
    }
    stream->checkGlError(__LINE__);
    return true;
}

bool Shader::SetConstantMatrix4(const char *varName, float *data)
{
    stream->checkGlError(__LINE__);
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
        if (loc != -1) {
            if (useProgramUniforms) {
                stream->glProgramUniformMatrix4fv(*((unsigned int *)(&glProgram)), loc, 1, 1, data);
            } else {
                stream->glUniformMatrix4fvARB(loc, 1, 1, data);
            }
        }
    }
    stream->checkGlError(__LINE__);
    return true;
}

const char* Shader::userDefinedVertString;
const char* Shader::userDefinedFragString;

static char *MakeFilename(const char *cgFileName, ShaderTypes type)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return 0;

    int extraChars = type == VERTEX_PROGRAM ? 2 : 7;
    char *glslFileName = SL_NEW char[len + extraChars + 1];
    memset(glslFileName, 0, len + extraChars + 1);

#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
    strncpy_s(glslFileName, len + extraChars + 1, cgFileName, len - 3);
    switch (type) {
    case FRAGMENT_PROGRAM:
        strcat_s(glslFileName, len + extraChars + 1, "-frag.glsl");
        break;
    case VERTEX_PROGRAM:
        strcat_s(glslFileName, len + extraChars + 1, ".glsl");
        break;
    case GEOMETRY_PROGRAM:
        strcat_s(glslFileName, len + extraChars + 1, "-geom.glsl");
    }
#else
    strncpy(glslFileName, cgFileName, len - 3);
    switch (type) {
    case FRAGMENT_PROGRAM:
        strcat(glslFileName, "-frag.glsl");
        break;
    case VERTEX_PROGRAM:
        strcat(glslFileName, ".glsl");
        break;
    case GEOMETRY_PROGRAM:
        strcat(glslFileName, "-geom.glsl");
        break;
    }
#endif

    return glslFileName;
}

static char*  CommonFileName(const char* cgFileName)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return 0;

    const int extraChars = 11;
    char *commonFileName = SL_NEW char[len + extraChars + 1];
    memset(commonFileName, 0, len + extraChars + 1);

#if (defined(WIN32) || defined(WIN64)) && (_MSC_VER > 1310)
    strncpy_s(commonFileName, len + extraChars + 1, cgFileName, len - 3);
    strcat_s(commonFileName, len + extraChars + 1, "-common.glsl15");
#else
    strncpy(commonFileName, cgFileName, len - 3);
    strcat(commonFileName, "-common.glsl15");
#endif

    return commonFileName;
}

static void PrintGLSLInfoLog(GLhandleARB obj, const char *fname)
{
    if (OpenGLUtils::enableDebugOutput) {
        int infologLength = 0;
        int charsWritten = 0;
        char *infoLog;

        printf("GLSL error detected in %s!\n", fname);

        glGetObjectParameterivARB(obj, GL_OBJECT_INFO_LOG_LENGTH_ARB,
                                  &infologLength);

        if (infologLength > 0) {
            infoLog = (char *)SL_MALLOC(infologLength);
            glGetInfoLogARB(obj, infologLength, &charsWritten, infoLog);
            printf("%s\n", infoLog);
            SL_FREE(infoLog);
        }
    }
}

// These are not glsl15
static bool rerouteToGlsl15(const std::string& strGlslFileName)
{
    if (strGlslFileName == std::string("Shaders/Atmosphere.glsl")) {
        return false;
    }
    if (strGlslFileName == std::string("Shaders/Atmosphere-frag.glsl")) {
        return false;
    }
    if (strGlslFileName == std::string("Shaders/Atmosphere-noshadow.glsl")) {
        return false;
    }

    if (strGlslFileName == std::string("Shaders/Particle.glsl")) {
        return false;
    }
    if (strGlslFileName == std::string("Shaders/Particle-frag.glsl")) {
        return false;
    }

    return true;
}

static std::string RemoveVersionAndExtensionsLines(
    std::string& version
    , SL_VECTOR(std::string)& extensions
    , const std::string& shaderSource
)
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

static GLhandleARB LoadShader(const char *glslFileName
                              , const std::string& glslFileNameCommon
                              , int shaderType
                              , bool userShader
                              , const SL_VECTOR(SL_STRING)& topLevelDefines
                              , ResourceLoader* resourceLoader
                              , bool useUBOs)
{


    if (!glslFileName) {
        std::cout << "incoming file name is empty" << std::endl;
        return 0;
    }

    std::string strGlslFileName = glslFileName;

    const bool reroute = userShader == false && rerouteToGlsl15(strGlslFileName);
    if (reroute) {
        strGlslFileName += "15";
    }

    char* shaderSource = 0;
    unsigned int dataLen = 0;
    bool shaderLoaded = resourceLoader->LoadResource(strGlslFileName.c_str(), shaderSource, dataLen, true);
    if (shaderLoaded == false || shaderSource == 0) {
        std::cout << "Could not load file: " << strGlslFileName << std::endl;
        return 0;
    }

    const bool isInstanceParticleShader = glslFileNameCommon.find("Particle-instanced") != std::string::npos;
    const bool isInstancedBillboardShader = glslFileNameCommon.find("Billboard-common") != std::string::npos
                                            && topLevelDefines.empty() == false && topLevelDefines[0] == SL_STRING("BILLBOARD_SHADER_INSTANCED");

    std::string strShaderSource;
    std::string defines;
    if (userShader) {
        std::string version;
        SL_VECTOR(std::string) extensions;

        strShaderSource = RemoveVersionAndExtensionsLines(version, extensions, std::string(shaderSource));
        if (version.empty() == false) {
            if (isInstancedBillboardShader) {
                defines += "#version 460\n";
            } else if (isInstanceParticleShader) {
                defines += "#version 450\n";
            } else {
                defines += version;
            }
            defines += "\n";
        }
        if (isInstancedBillboardShader) {
            defines += "#extension GL_ARB_shader_storage_buffer_object : enable\n#define BILLBOARD_SHADER_INSTANCED\n";
            defines += "#extension GL_ARB_shader_draw_parameters : enable\n";
        } else if (isInstanceParticleShader) {
            defines += "#extension GL_ARB_bindless_texture : require\n";
            defines += "#extension GL_ARB_gpu_shader_int64 : require\n";
        }
        for (unsigned int i = 0; i < extensions.size(); ++i) {
            defines += extensions[i];
            defines += "\n";
        }
    } else {
        if (isInstancedBillboardShader) {
            defines += "#version 460\n";
            defines += "#extension GL_ARB_shader_storage_buffer_object : enable\n#define BILLBOARD_SHADER_INSTANCED\n";
            defines += "#extension GL_ARB_shader_draw_parameters : enable\n";
            defines += "#extension GL_ARB_gpu_shader_int64 : enable\n";
        } else if (isInstanceParticleShader) {
            defines += "#version 450\n";
            defines += "#extension GL_ARB_bindless_texture : require\n";
            defines += "#extension GL_ARB_gpu_shader_int64 : require\n";
        }

        defines += "\n";
        strShaderSource = std::string(shaderSource);
    }

    if (reroute) {
        defines += "#define OPENGL 1\n";
    }

    if (useUBOs) {
        defines += "#extension GL_ARB_uniform_buffer_object : enable\n#define USE_UBO\n";
    }

    if (userShader && glslFileNameCommon.find("Particle") != std::string::npos) {
        defines += "#define PARTICLE_SHADER\n";
    }

#ifdef MAC
    defines += "#define MAC\n";
#endif

#ifdef WIN32
    defines += "#define WIN32\n";
#endif

#ifdef LINUX
    defines += "#define LINUX\n";
#endif


    GLhandleARB shaderObject = 0;

    switch (shaderType) {
    case VERTEX_PROGRAM:
        shaderObject = glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB);
        break;

    case FRAGMENT_PROGRAM:
        shaderObject = glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB);
        break;
    }

    const char* userString = (shaderType == VERTEX_PROGRAM) ?
                             Shader::userDefinedVertString : Shader::userDefinedFragString;

    std::string strUserDefines = (userString && userShader) ? userString : "";

    char* shaderSourceCommon = 0;
    unsigned int dataLenCommon = 0;
    bool shaderLoadedCommon = resourceLoader->LoadResource(glslFileNameCommon.c_str(), shaderSourceCommon, dataLenCommon, true);

    if (shaderSourceCommon) {
        const char *sources[4] = { defines.c_str(), shaderSourceCommon, strUserDefines.c_str(), strShaderSource.c_str() };
        glShaderSourceARB(shaderObject, 4, sources, NULL);
    } else {
        const char *sources[3] = { defines.c_str(), strUserDefines.c_str(), strShaderSource.c_str() };
        glShaderSourceARB(shaderObject, 3, sources, NULL);
    }

    glCompileShaderARB(shaderObject);

    resourceLoader->FreeResource(shaderSource);
    resourceLoader->FreeResource(shaderSourceCommon);

    GLint ok;
    glGetObjectParameterivARB(shaderObject, GL_OBJECT_COMPILE_STATUS_ARB, &ok);
    if (!ok) {
        PrintGLSLInfoLog(shaderObject, glslFileName);
        return 0;
    } else {
        return shaderObject;
    }

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
        glGetProgramResourceName(program, GL_UNIFORM_BLOCK, blockIndex, (GLsizei)(uboNameData.size()), NULL, &uboNameData[0]);
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

bool Shader::LoadShaderFromFile(const char *fileName
                                , const char *userVertShader, const char *userFragShader
                                , bool enableObjectLabeling
                                , const SL_VECTOR(SL_STRING)& defines
                                , ResourceLoader* resourceLoader
                                , Shader*& activeShader
                                , const SL_VECTOR(GLhandleARB)& userShaderList)
{
    // We automatically look for a companion -frag.glsl source file too, since GLSL doesn't have effect
    // files to keep it neat...
    char *baseFileName = MakeFilename(fileName, VERTEX_PROGRAM);
    char *fragFileName = MakeFilename(fileName, FRAGMENT_PROGRAM);
    //char *geomFileName = MakeFilename(fileName, GEOMETRY_PROGRAM);

    std::string commonFileName = CommonFileName(fileName);

    GLhandleARB shaderObject = LoadShader(baseFileName, commonFileName, VERTEX_PROGRAM, false, defines, resourceLoader, useUBOs);
    GLhandleARB fragShaderObject = LoadShader(fragFileName, commonFileName, FRAGMENT_PROGRAM, false, defines, resourceLoader, useUBOs);
    //GLhandleARB geomShaderObject = LoadShader(geomFileName, GEOMETRY_PROGRAM);

    GLhandleARB userVertObject = LoadShader(userVertShader, commonFileName, VERTEX_PROGRAM, true, defines, resourceLoader, useUBOs);
    GLhandleARB userFragObject = LoadShader(userFragShader, commonFileName, FRAGMENT_PROGRAM, true, defines, resourceLoader, useUBOs);

    if (enableObjectLabeling && glObjectLabel) {
        SL_STRING strName;

        strName = SL_STRING("SilverLining Vertex Shader: ") + SL_STRING(baseFileName);
        glObjectLabel(GL_SHADER, *((unsigned int*)(&shaderObject)), -1, strName.c_str());

        strName = SL_STRING("SilverLining Fragment Shader: ") + SL_STRING(fragFileName);
        glObjectLabel(GL_SHADER, *((unsigned int*)(&fragShaderObject)), -1, strName.c_str());

        if (userVertObject) {
            strName = SL_STRING("SilverLining User Vertex Shader: ") + SL_STRING(userVertShader);
            glObjectLabel(GL_SHADER, *((unsigned int *)(&userVertObject)), -1, strName.c_str());
        }

        if (userFragObject) {
            strName = SL_STRING("SilverLining User Fragment Shader: ") + SL_STRING(userFragShader);
            glObjectLabel(GL_SHADER, *((unsigned int*)(&userFragObject)), -1, strName.c_str());
        }

    }

    SL_DELETE[] baseFileName;
    SL_DELETE[] fragFileName;

    if (shaderObject) {
        GLhandleARB programObject = glCreateProgramObjectARB();
        glAttachObjectARB(programObject, shaderObject);

        if (fragShaderObject) {
            glAttachObjectARB(programObject, fragShaderObject);
        }

        if (userVertObject) {
            glAttachObjectARB(programObject, userVertObject);
        }

        if (userFragObject) {
            glAttachObjectARB(programObject, userFragObject);
        }

        if (context) {
            SL_VECTOR(GLhandleARB)::const_iterator it;
            for (it = userShaderList.begin(); it != userShaderList.end(); it++) {
                glAttachObjectARB(programObject, *it);
            }
        }

        glLinkProgramARB(programObject);

        GLint ok;
        glGetObjectParameterivARB(programObject, GL_OBJECT_LINK_STATUS_ARB, &ok);
        if (!ok) {
            PrintGLSLInfoLog(programObject, fileName);
        } else {
            GLint texLoc = glGetUniformLocationARB(programObject, "sl_tex2D");
            if (texLoc != -1) {
                glUseProgramObjectARB(programObject);
                glUniform1iARB(texLoc, 0);
                glUseProgramObjectARB(0);
                activeShader = 0;

                texLoc = glGetUniformLocationARB(programObject, "sl_tex2D2");
                if (texLoc != -1) {
                    glUseProgramObjectARB(programObject);
                    glUniform1iARB(texLoc, 1);
                    glUseProgramObjectARB(0);
                    activeShader = 0;
                }
            } else {
                // Stratocumulus shader uses 3 stages
                GLint texLoc = glGetUniformLocationARB(programObject, "sl_tex3D");
                if (texLoc != -1) {
                    glUseProgramObjectARB(programObject);
                    glUniform1iARB(texLoc, 0);
                    glUseProgramObjectARB(0);
                    activeShader = 0;
                }
                texLoc = glGetUniformLocationARB(programObject, "sl_tex3D2");
                if (texLoc != -1) {
                    glUseProgramObjectARB(programObject);
                    glUniform1iARB(texLoc, 1);
                    glUseProgramObjectARB(0);
                    activeShader = 0;
                }
                texLoc = glGetUniformLocationARB(programObject, "sl_tex3D3");
                if (texLoc != -1) {
                    glUseProgramObjectARB(programObject);
                    glUniform1iARB(texLoc, 2);
                    glUseProgramObjectARB(0);
                    activeShader = 0;
                }
            }

            glProgram = programObject;
            glShader = shaderObject;
            glFragShader = fragShaderObject;
            glUserShader = userVertObject;
            glUserFragShader = userFragObject;

            if (useUBOs && CreateUboMap(*((unsigned int *)(&programObject)))) {
                glGenBuffersARB(1, &ubo);
                glBindBufferARB(GL_UNIFORM_BUFFER, ubo);
                glBufferDataARB(GL_UNIFORM_BUFFER, uboSize, 0, GL_DYNAMIC_DRAW);
                glBindBufferARB(GL_UNIFORM_BUFFER, 0);

#ifndef MAP_DATA
                uboData = (GLbyte*)SL_MALLOC(uboSize);
#endif
            }

            OpenGLUtils::CheckError(__LINE__);

            return true;
        }
    }
    return false;
}


Context* Shader::GetContext(void) const
{
    return context;
}

}
