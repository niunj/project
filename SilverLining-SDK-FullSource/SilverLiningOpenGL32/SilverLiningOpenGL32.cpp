// SilverLiningOpenGL.cpp : Defines the entry point for the DLL application.
//

// For leak checking
//#define VLD_FORCE_ENABLE 1
//#include <vld.h>

#include "SLAssert.h"
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif

#include "MemAlloc.h"
#include "SilverLiningOpenGL32.h"
#include "SLAssert.h"

#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <stack>
#include <algorithm>

#include "SilverLiningOpenGLPreamble.h"
#include "OpenGLUtils.h"
#include "OpenGLExtensionManager.h"
#include "Context.h"
#include "BufferGL.h"
#include "VAOManagerOpenGL.h"
#include "CubeMapRenderTexture.h"
#include "CubeMapFboManager.h"

#include "TextureRenderer.h"
#include "TextureLoader.h"
#include "Shader.h"
#include "LineShader.h"
#include "SL_Buffer.h"

#include "OpenGLStream.h"

using namespace SilverLining;
using namespace std;

static TextureLoader texLoader;
static ResourceLoader *resourceLoader = NULL;

static void SetUserVertFileName(const char *userString)
{
    Shader::userVertFileName = userString;
}

static void SetUserFragFileName(const char *userString)
{
    Shader::userFragFileName = userString;
}

static const char * GetUserVertFileName()
{
    return Shader::userVertFileName;
}

static const char * GetUserFragFileName()
{
    return Shader::userFragFileName;
}

#ifndef SILVERLINING_STATIC_RENDERER_OPENGL32
BOOL APIENTRY DllMain(HANDLE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
#endif

SILVERLININGDLL_API void DeviceLost(void *) {}
SILVERLININGDLL_API void DeviceReset(void *) {}

SILVERLININGDLL_API bool BackfaceCullClockwise(bool cullCW, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetCullClockWise(cullCW);
    return true;
}

SILVERLININGDLL_API void *SetEnvironment(bool rightHanded, void *env, SilverLining::ResourceLoader *pResourceLoader,
        SilverLining::Allocator *pAllocator, const char* userVertString, const char* userFragString, bool avoidStalls,
        const char *userVertFilename, const char *userFragFilename, bool useUBOs, bool deferred, const SL_VECTOR(unsigned int)& userShaders)
{
    // Clear any errors from the app
    while (glGetError() != GL_NO_ERROR);

    resourceLoader = pResourceLoader;
    Allocator::SetAllocator(pAllocator);

    // Test GL version
    int nMajorVersion = -1, nMinorVersion = -1;
    glGetIntegerv(GL_MAJOR_VERSION, &nMajorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &nMinorVersion);

    if (nMajorVersion < 3) return 0;
    if (nMajorVersion == 3 && nMinorVersion < 2) return 0;

    GLint profile;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (profile & GL_CONTEXT_CORE_PROFILE_BIT) {
        OpenGLExtensionManager::usingCoreProfile = true;
    }

    // Set user define strings
    SetUserDefinedVertString(userVertString);
    SetUserDefinedFragString(userFragString);
    SetUserVertFileName(userVertFilename);
    SetUserFragFileName(userFragFilename);

#if (_MSC_VER <= 1310 || (!defined(WIN32) && !defined(WIN64)))
    OpenGLUtils::enableDebugOutput = (getenv("SILVERLINING_ENABLE_DEBUG") != NULL);
#else
    char *buf = 0;
    size_t bufSize = 0;
    OpenGLUtils::enableDebugOutput = false;
    if (_dupenv_s(&buf, &bufSize, "SILVERLINING_ENABLE_DEBUG") == 0) {
        if (buf != NULL) {
            OpenGLUtils::enableDebugOutput = true;
            free(buf);
        }
    }
#endif

    if (OpenGLUtils::enableDebugOutput) {
        printf("SilverLining OpenGL Debug Output Enabled.\n");
    }

    // Load extensions.
#if defined(WIN32) || defined(WIN64)
    HGLRC glContext = wglGetCurrentContext();
    if (glContext == NULL) return 0;
#endif

    int extensionsLoaded = OpenGLExtensionManager::LoadGLExtensions();
    if (extensionsLoaded==0) {
        return 0;
    }

    OpenGLUtils::CheckError(__LINE__);

    Context* context = SL_NEW Context(useUBOs && OpenGLExtensionManager::HasUBOs(), OpenGLExtensionManager::HasProgramUniform(), avoidStalls, deferred);

    SetUserShaders(userShaders, context);

    // Preload Line Shader
    GetLineShader(context);

    OpenGLUtils::CheckError(__LINE__);

    return (void*)context;
}

SILVERLININGDLL_API void SetContext(void *pContext)
{
}

SILVERLININGDLL_API void ClearScreen(const Color& c, void* _context)
{
    OpenGlStream* stream = static_cast<Context*>(_context)->GetStream();
    stream->glClearColor((GLclampf)c.r, (GLclampf)c.g, (GLclampf)c.b, (GLclampf)c.a);
    stream->glClear(GL_COLOR_BUFFER_BIT);
    stream->checkGlError(__LINE__);
}

SILVERLININGDLL_API bool HasInstancing(void)
{
    return (glDrawElementsInstanced != 0);
}
SILVERLININGDLL_API bool HasBindlessTextures(void)
{
    return OpenGLExtensionManager::ExtensionSupported("GL_ARB_bindless_texture")
           && (glGetTextureHandleARB != 0);
}
SILVERLININGDLL_API bool HasUBOs(void)
{
    return OpenGLExtensionManager::HasUBOs();
}
SILVERLININGDLL_API bool HasSSBOs(void)
{
    return OpenGLExtensionManager::ExtensionSupported("GL_ARB_shader_storage_buffer_object");
}
SILVERLININGDLL_API bool Has64BitSupport(void)
{
    return OpenGLExtensionManager::ExtensionSupported("GL_ARB_gpu_shader_int64");
}

SILVERLININGDLL_API bool HasBindlessIndirectRendering(void)
{
    return glBindVertexBuffer != 0
           && glVertexAttribFormat != 0
           && glVertexAttribBinding != 0
           && glMultiDrawElementsIndirectBindlessNV != 0
           && OpenGLExtensionManager::ExtensionSupported("GL_ARB_shader_storage_buffer_object")
           && OpenGLExtensionManager::ExtensionSupported("GL_NV_bindless_multi_draw_indirect")
           && OpenGLExtensionManager::ExtensionSupported("GL_ARB_shader_draw_parameters");
}

SILVERLININGDLL_API Buffer* CreateBuffer(const char* name, BufferType bufferType, int size, const BufferProperties& _bufferProperties, void* _context)
{
    return new BufferGL(name, bufferType, size, _bufferProperties);
}

SILVERLININGDLL_API void DestroyBuffer(Buffer* buffer, void* _context)
{
    delete buffer;
}

SILVERLININGDLL_API GPUAddressType GetTextureAddress(TextureHandle textureHandle)
{
    glTexture* texture = (glTexture*)textureHandle;

    SL_ASSERT(sizeof(GLuint64) == sizeof(GPUAddressType));
    GLuint64 textureAddress = glGetTextureHandleARB(texture->TextureID);

    OpenGLUtils::CheckError(__LINE__);

    return (GPUAddressType)textureAddress;
}

SILVERLININGDLL_API void MakeTextureAddressResident(GPUAddressType address)
{
    if (glIsTextureHandleResidentARB(address) == false) {
        glMakeTextureHandleResidentARB(address);
    }
    OpenGLUtils::CheckError(__LINE__);
}

SILVERLININGDLL_API GPUAddressType GetBufferAddress(BufferHandle buffer)
{
    SL_ASSERT(sizeof(GLuint64) == sizeof(GPUAddressType));

    GLuint64 bufferAddress = 0;
    const SL_Buffer* slBuffer = (const SL_Buffer*)(buffer);
    if (slBuffer) {
        glGetNamedBufferParameterui64vNV(slBuffer->handle, GL_BUFFER_GPU_ADDRESS_NV, &bufferAddress);
    }
    OpenGLUtils::CheckError(__LINE__);

    return (GPUAddressType)bufferAddress;
}

SILVERLININGDLL_API void MakeBufferResident(BufferHandle buffer, bool makeResident)
{
    SL_Buffer* slBuffer = (SL_Buffer*)(buffer);
    if (slBuffer) {
        slBuffer->MakeBufferResident(makeResident);
    }
}

SILVERLININGDLL_API bool IsBufferResident(BufferHandle buffer)
{
    const SL_Buffer* slBuffer = (const SL_Buffer*)(buffer);
    if (slBuffer == 0) {
        return false;
    }
    return slBuffer->IsBufferResident();
}

SILVERLININGDLL_API void ClearDepth(void* _context)
{
    OpenGlStream* stream = static_cast<Context*>(_context)->GetStream();
#ifdef WIREFRAME
    stream->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#else
    stream->glClear(GL_DEPTH_BUFFER_BIT);
#endif
    stream->checkGlError(__LINE__);
}

class Matrix4x4 : public MemObject
{
public:
    Matrix4x4()
    {
    }

    Matrix4x4(GLdouble *mat)
    {
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                m[row][col] = *mat++;
            }
        }
    }

    Matrix4x4 Transpose()
    {
        Matrix4x4 out;

        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                out.m[row][col] = m[col][row];
            }
        }

        return out;
    }

    Matrix4x4 operator * (const Matrix4x4 &mat) const
    {
        Matrix4x4 out;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                out.m[row][col] =
                    m[row][0] * mat.m[0][col] +
                    m[row][1] * mat.m[1][col] +
                    m[row][2] * mat.m[2][col] +
                    m[row][3] * mat.m[3][col];
            }
        }

        return out;
    }

    Vector3 GetRow(int row) const
    {
        Vector3 out;
        out.x = m[row][0];
        out.y = m[row][1];
        out.z = m[row][2];
        return out;
    }

    double operator () (int x, int y) const
    {
        return m[x][y];
    }

private:
    double m[4][4];
};

SILVERLININGDLL_API ShaderHandle     LoadShaderFromSource(const char *shaderSource)
{
    return 0;
}

SILVERLININGDLL_API void DeleteShader(ShaderHandle shader, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (context) {
        context->DeleteShader(shader);
    }
}

void SetUserDefinedVertString(const char *userString)
{
    Shader::userDefinedVertString = userString;
}

void SetUserDefinedFragString(const char *userString)
{
    Shader::userDefinedFragString = userString;
}

const char * GetUserDefinedVertString()
{
    return Shader::userDefinedVertString;
}

const char * GetUserDefinedFragString()
{
    return Shader::userDefinedFragString;
}


SILVERLININGDLL_API void             SetUserShaders(const SL_VECTOR(unsigned int)& shaders, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (context) {
        context->userShaderList.clear();

        SL_VECTOR(unsigned int)::const_iterator iter;
        for (iter = shaders.begin(); iter != shaders.end(); iter++) {
            context->userShaderList.push_back(*iter);
        }
    }
}

SILVERLININGDLL_API unsigned int GetShaderProgramObject(ShaderHandle shader)
{
    Shader *s = (Shader *)shader;
    if (s) {
        return s->glProgram;
    }

    return 0;
}

SILVERLININGDLL_API ShaderHandle     LoadShaderFromFile(const char *fileName, const char *userVertShader, const char *userFragShader, bool enableObjectLabeling, const SL_VECTOR(SL_STRING)& defines, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->LoadShaderFromFile(fileName, userVertShader, userFragShader, enableObjectLabeling, defines, resourceLoader);
}

SILVERLININGDLL_API int GetConstantLocation(ShaderHandle shader, const char *varName)
{
    if (shader == 0) {
        return -1;
    }
    int loc = ((Shader*)(shader))->GetUniformLocation(varName);
    return loc;
}

SILVERLININGDLL_API void SetConstantVector4AtLocation(SilverLining::ShaderHandle shader, int loc, const float* data, void* _context)
{
    if (!shader) return;
    Shader *s = (Shader *)shader;
    s->SetConstantVector4AtLocation(loc, data);
}

SILVERLININGDLL_API void SetConstantMatrix4AtLocation(SilverLining::ShaderHandle shader, int loc, float* data, void* _context)
{
    if (!shader) return;
    Shader *s = (Shader *)shader;
    s->SetConstantMatrix4AtLocation(loc, data);
}

SILVERLININGDLL_API bool SetConstantVector4(ShaderHandle shader, const char *varName, const float *data)
{
    if (!shader) return false;

    Shader *s = (Shader *)shader;
    return s->SetConstantVector4(varName, data);
}

SILVERLININGDLL_API bool SetConstantInt(ShaderHandle shader, const char *varName, int val)
{
    if (!shader) return false;

    Shader *s = (Shader *)shader;

    return s->SetConstantInt(varName, val);
}

SILVERLININGDLL_API bool SetConstantMatrix4(ShaderHandle shader, const char *varName, float *data)
{
    if (!shader) return false;

    Shader *s = (Shader*)shader;
    return s->SetConstantMatrix4(varName, data);
}

SILVERLININGDLL_API bool PreConstantsSet(ShaderHandle shader)
{
    Shader *s = (Shader*)shader;
    return s && s->PreConstantsSet();
}

SILVERLININGDLL_API bool PostConstantsSet(ShaderHandle shader)
{
    Shader *s = (Shader*)shader;
    return s && s->PostConstantsSet();
}

SILVERLININGDLL_API bool BindShader(ShaderHandle shader, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->BindShader(shader);
}

SILVERLININGDLL_API bool UnbindShader(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->UnbindShader();
}

SILVERLININGDLL_API bool IncrementConstantBuffersOffset(ShaderHandle shader, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool             ShutdownShaderSystem(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (context) {
        context->ShutdownShaderSystem();

        SL_DELETE context;
        context = 0;
    }
    return true;
}

SILVERLININGDLL_API bool SetVertexBuffer(VertexBufferHandle vbh, bool vertexColorsNotUsed, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->SetVertexBuffer(vbh);
}

SILVERLININGDLL_API bool UnsetVertexBuffer(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->UnsetVertexBuffer();
}

SILVERLININGDLL_API bool IncrementOffsetIndex(VertexBufferHandle vbh, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool            DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->DrawStrip(ibh, startIdx, nIndices, nVerts, preDraw);
}

SILVERLININGDLL_API bool   DrawMultiStrips(void* _commandBuffer, void* _context)
{
    SL_ASSERT(_commandBuffer&&_context);
    Context* context = static_cast<Context*>(_context);
    const std::vector<DrawElementsIndirectBindlessCommandNV>& commandBuffer = (*(std::vector<DrawElementsIndirectBindlessCommandNV>*)_commandBuffer);
    context->DrawMultiStrips(commandBuffer);
    return true;
}
SILVERLININGDLL_API bool            DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->DrawStripInstanced(ibh, startIdx, nIndices, nVerts, instanceCount);
}

SILVERLININGDLL_API bool             DrawPoints(double pointSize, int nPoints, int start, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->DrawPoints(pointSize, nPoints, start);
}

SILVERLININGDLL_API bool            HasQuads()
{
    return false;
}

SILVERLININGDLL_API bool            DrawQuads(int nPoints, int start, void* _context)
{
    return false;
}

SILVERLININGDLL_API void *           AllocateVertexBuffer(int numVertices, const char* name, const SilverLining::BufferProperties& bufferProperties, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    SL_Buffer* vb = context->GetAVertexBuffer(numVertices, name, bufferProperties);
    context->GetStream()->checkGlError(__LINE__);
    return (void *)vb;
}

SILVERLININGDLL_API bool             LockVertexBuffer(void *buffer)
{
    return true;
}

SILVERLININGDLL_API bool GetVertexBuffer(void* vbh, int offset, Vertex *verts, int nVerts, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    SL_Buffer* buf = (SL_Buffer*)vbh;

    if (stream->isImmediate()) {
        buf->GetData(offset*sizeof(Vertex), nVerts*sizeof(Vertex), verts);
    } else {
        // Invalidate the colors to figure out which ones were modified
        // (this is used during sync)
        for(int i = 0; i < nVerts; ++i) {
            verts[i].r = verts[i].g  = verts[i].b  = verts[i].a = -1;
        }
#if  0
        // PPP: This happens only via quad compile, and is handled by pseudo sync
        // so, staging buffer is not needed
        GLubyte* stagingBuffer = buf->GetStagingBuffer();
        SL_ASSERT(stagingBuffer);
        memcpy(verts, stagingBuffer + (offset*sizeof(Vertex)), nVerts*sizeof(Vertex));
#endif
    }
    return true;
}

SILVERLININGDLL_API bool UpdateVertexBuffer(void* vbh, int offset, Vertex *verts, int nVerts, bool justColors, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    SL_Buffer* buf = (SL_Buffer*)vbh;

    bool ok = false;
    if (stream->isImmediate()) {
        if (buf->handle == 0) {
            buf->InitBuffers();
        }
        ok = buf->SyncToGPU(offset * sizeof(Vertex), nVerts * sizeof(Vertex), verts);
    } else {
        ok = buf->SyncToGPU(offset * sizeof(Vertex), nVerts * sizeof(Vertex), verts, stream, justColors);
    }

    return ok;
}

SILVERLININGDLL_API SilverLining::Vertex * GetVertices(void* vbh, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    SL_Buffer *buf = (SL_Buffer*)vbh;
    SL_ASSERT(buf);
    if (buf == 0) {
        return 0;
    }
    if (stream->isImmediate()) {
        if (buf->handle == 0) {
            buf->InitBuffers();
        }
        return (Vertex*)(buf->Lock(0, buf->GetSizeInBytes()));
    } else {
        return (Vertex*)(buf->GetStagingBuffer());
    }
}

SILVERLININGDLL_API bool             UnlockVertexBuffer(void* vbh, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate()? context->GetImmediateStream() : context->GetStream();

    SL_Buffer* buf = (SL_Buffer *)vbh;
    if (stream->isImmediate()) {
        return buf->UnLock();
    } else {
        return buf->SyncToGPU(0, buf->GetSizeInBytes(), stream);
    }
}

SILVERLININGDLL_API bool             ReleaseVertexBuffer(void* buffer, void* _context)
{
    SL_Buffer* slBuffer = (SL_Buffer*)buffer;

    GLuint handle = (GLuint)(slBuffer->handle);

    VAOManagerOpenGL* vaoManager = VAOManagerOpenGL::instance();
    vaoManager->DeleteVAOsForBuffer(handle);

    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    context->ReturnAVertexBuffer(slBuffer);

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API void* AllocateIndexBuffer(int nIndices, const char* name, const SilverLining::BufferProperties& bufferProperties, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    SL_Buffer* ib = context->GetAnIndexBuffer(nIndices, name, bufferProperties);
    context->GetStream()->checkGlError(__LINE__);
    return (void *)ib;
}

SILVERLININGDLL_API bool LockIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    return true;
}

SILVERLININGDLL_API Index* GetIndices(IndexBufferHandle ibh, void* _context)
{
    SL_ASSERT(ibh);
    SL_Buffer* slBuffer = (SL_Buffer*)ibh;
    SL_ASSERT(slBuffer);
    if (slBuffer == 0) {
        return 0;
    }
    return (Index*)(slBuffer->GetStagingBuffer());
}

SILVERLININGDLL_API bool UnlockIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    SL_ASSERT(ibh);
    SL_Buffer* slBuffer = (SL_Buffer *)ibh;
    SL_ASSERT(slBuffer);

    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();
    bool ok = slBuffer->SyncToGPU(0, slBuffer->GetSizeInBytes(), stream);
    return ok;
}

SILVERLININGDLL_API bool ReleaseIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    SL_ASSERT(ibh);
    SL_Buffer* slBuffer = (SL_Buffer*)ibh;

    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    context->ReturnAnIndexBuffer(slBuffer);

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool SetIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetIndexBuffer(ibh);
    return true;
}

SILVERLININGDLL_API bool UnsetIndexBuffer(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->UnsetIndexBuffer();
    return true;
}

SILVERLININGDLL_API bool EnableDepthWrites(bool enable, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    stream->glDepthMask(enable ? GL_TRUE : GL_FALSE);
    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool EnableDepthReads(bool enable, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    if (enable) {
        stream->glEnable(GL_DEPTH_TEST);
    } else {
        stream->glDisable(GL_DEPTH_TEST);
    }
    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool EnableTexture2D(bool enable, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool EnableTexture3D(bool enable, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool EnableBackfaceCulling(bool enable, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->EnableBackfaceCulling(enable);
    return true;
}

SILVERLININGDLL_API bool EnableFog(bool enable, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool ConfigureFog(double density, double start, double end, const Color& c, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool EnableLighting(bool enable, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool SetCurrentColor(const Color& c, void* _context)
{
    return true;
}

static void ConvertMatrix(const GLdouble *in, Matrix4 *out)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out->elem[row][col] = in[col * 4 + row];
        }
    }
}

static void ConvertMatrix(const Matrix4 *in, GLdouble *out)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out[col * 4 + row] = in->elem[row][col];
        }
    }
}

SILVERLININGDLL_API bool             SetProjectionMatrix(const SilverLining::Matrix4& m, void* _context)
{
    GLdouble glmat[16];
    ConvertMatrix(&m, glmat);

    Context* context = static_cast<Context*>(_context);
    memcpy(context->mProjection.m, glmat, 16 * sizeof(GLdouble));

    OpenGlStream* stream = context->GetStream();
    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool             GetTextureMatrix(SilverLining::Matrix4 *m, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (!context) return false;

    ConvertMatrix(context->mTexture.m, m);
    OpenGlStream* stream = context->GetStream();
    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool             SetTextureMatrix(const SilverLining::Matrix4& m, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (!context) return false;

    OpenGlStream* stream = context->GetStream();

    GLdouble glmat[16];
    ConvertMatrix(&m, glmat);

    memcpy(context->mTexture.m, glmat, 16 * sizeof(GLdouble));

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool             GetModelviewMatrix(SilverLining::Matrix4 *m, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (!context) return false;

    OpenGlStream* stream = context->GetStream();

    ConvertMatrix(context->mModelview.m, m);
    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool             SetModelviewMatrix(const SilverLining::Matrix4& m, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    if (!context) return false;

    OpenGlStream* stream = context->GetStream();

    GLdouble glmat[16];
    ConvertMatrix(&m, glmat);

    memcpy(context->mModelview.m, glmat, 16 * sizeof(GLdouble));

    stream->checkGlError(__LINE__);
    return true;
}



SILVERLININGDLL_API bool             PushAllState(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->PushAllState();
    return true;
}

SILVERLININGDLL_API bool             PopAllState(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->PopAllState();
    return true;
}

SILVERLININGDLL_API bool            SetDefaultState(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetDefaultState();
    return true;
}

SILVERLININGDLL_API bool HeartBeat(void)
{
    VAOManagerOpenGL* vaoManager = VAOManagerOpenGL::instance();
    if (vaoManager) {
        vaoManager->HeartBeat();
    }
    CubeMapFboManager* fboManager = CubeMapFboManager::findInstance();
    if (fboManager) {
        fboManager->HeartBeat();
    }
    return true;
}

SILVERLININGDLL_API bool ContextBeingDeleted(void* context)
{
    VAOManagerOpenGL* vaoManager = VAOManagerOpenGL::instance();
    if (vaoManager) {
        vaoManager->ContextBeingDeleted(context);
    }
    return true;
}

SILVERLININGDLL_API bool        SetReverseZ(bool reverse, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();
    context->SetIsReverseDepth(reverse);
    stream->glDepthFunc(context->GetIsReverseDepth() ? GL_GEQUAL : GL_LEQUAL);

    return true;
}

static GLenum ConvertBlendFactor(int incomingValue)
{
    GLenum factor = GL_ZERO;

    switch (incomingValue) {
    case ZERO:
        factor = GL_ZERO;
        break;

    case ONE:
        factor = GL_ONE;
        break;

    case SRCCOLOR:
        factor = GL_SRC_COLOR;
        break;

    case INVSRCCOLOR:
        factor = GL_ONE_MINUS_SRC_COLOR;
        break;

    case SRCALPHA:
        factor = GL_SRC_ALPHA;
        break;

    case INVSRCALPHA:
        factor = GL_ONE_MINUS_SRC_ALPHA;
        break;

    case DSTCOLOR:
        factor = GL_DST_COLOR;
        break;

    case INVDSTCOLOR:
        factor = GL_ONE_MINUS_DST_COLOR;
        break;

    case DSTALPHA:
        factor = GL_DST_ALPHA;
        break;

    case INVDSTALPHA:
        factor = GL_ONE_MINUS_DST_ALPHA;
        break;

    case SRCALPHASAT:
        factor = GL_SRC_ALPHA_SATURATE;
        break;
    }

    return factor;
}

SILVERLININGDLL_API bool             EnableBlending(int srcFactor, int dstFactor, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    stream->glEnable(GL_BLEND);

    if (stream->HasBlendFuncSeparate() && srcFactor == SRCALPHA && dstFactor == INVSRCALPHA) {
        stream->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        stream->glBlendFunc(ConvertBlendFactor(srcFactor), ConvertBlendFactor(dstFactor));
    }

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool             DisableBlending(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    stream->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    stream->glDisable(GL_BLEND);

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API void* GetNativeTexture(TextureHandle tex)
{
    if (tex) {
        glTexture *texObj = (glTexture *)tex;
        return (void*)(texObj->TextureID);
    }

    return 0;
}

SILVERLININGDLL_API bool            LoadTextureFromFile(const char *imgPath, TextureHandle *tex, bool repeatU, bool repeatV, const char* name, void* _context)
{
    glTexture * texture = SL_NEW glTexture;

    texLoader.SetTextureFilter(txBilinear);
    int result = texLoader.LoadTextureFromDisk(imgPath, texture, resourceLoader);
    OpenGLUtils::CheckError(__LINE__);

    if (result == FALSE) {
        SL_DELETE texture;
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texture->TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    *tex = (TextureHandle)texture;

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texture->TextureID, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            HasFloatTextures()
{
    return OpenGLExtensionManager::usingFloatBuffers;
}

SILVERLININGDLL_API bool            LoadFloatTextureRGB(const float *data, int width, int height, TextureHandle *texHandle, const char* name, void* _context)
{
    if (OpenGLExtensionManager::usingFloatBuffers) {
        glTexture *texture = SL_NEW glTexture;
        texture->Width = width;
        texture->Height = height;
        texture->Type = GL_RGB_FLOAT32_ATI;
        texture->Bpp = 128;

        GLuint texName = 0;
        glGenTextures(1, &texName);
        texture->TextureID = texName;
        glBindTexture(GL_TEXTURE_2D, texName);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB_FLOAT32_ATI, width, height, 0, GL_RGB, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // PPP: This actually has no effect. I don't think you want to set this anyway for a floating point look up texture
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        *texHandle = (TextureHandle)texture;

        if (name != 0 && glObjectLabel) {
            glObjectLabel(GL_TEXTURE, texName, -1, name);
        }

        OpenGLUtils::CheckError(__LINE__);
        return true;
    }
    OpenGLUtils::CheckError(__LINE__);
    return false;
}

SILVERLININGDLL_API bool            LoadFloatTexture(const float *data, int width, int height, TextureHandle *texHandle, const char* name)
{
    glTexture *texture = SL_NEW glTexture;
    texture->Width = width;
    texture->Height = height;
    texture->Type = GL_FLOAT;
    texture->Bpp = 64;

    GLuint texName = 0;
    glGenTextures(1, &texName);
    texture->TextureID = texName;
    glBindTexture(GL_TEXTURE_2D, texName);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
#ifdef NO_GLU
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width, height, 0, GL_LUMINANCE_ALPHA, GL_FLOAT, data);
#else
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_LUMINANCE_ALPHA, width, height, GL_LUMINANCE_ALPHA, GL_FLOAT, data);
#endif

    *texHandle = (TextureHandle)texture;

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texName, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            LoadTexture(const unsigned char *data, int width, int height, TextureHandle *texHandle,
        bool repeatU, bool repeatV, const char* name, void* _context)
{
    glTexture *texture = SL_NEW glTexture;
    texture->Width = width;
    texture->Height = height;
    texture->Type = GL_UNSIGNED_BYTE;
    texture->Bpp = 8;

    GLuint texName = 0;
    glGenTextures(1, &texName);
    texture->TextureID = texName;
    glBindTexture(GL_TEXTURE_2D, texName);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);

    //glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
    //glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    unsigned char *buf = SL_NEW unsigned char[width * height * 4];
    unsigned char *src = (unsigned char *)data;
    unsigned char *dst = (unsigned char *)buf;

    for (int texel = 0; texel < width * height; texel++) {
        unsigned char L = *src++;
        unsigned char A = *src++;
        *dst++ = L;
        *dst++ = L;
        *dst++ = L;
        *dst++ = A;
    }

#ifdef NO_GLU
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
#else
    //glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf);
#endif

    SL_DELETE[] buf;

    *texHandle = (TextureHandle)texture;

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texName, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            Load3DTexture(const unsigned char *data, int width, int height, int depth, TextureHandle *texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    if (!glTexImage3D) return false;

    glTexture *texture = SL_NEW glTexture;
    texture->Width = width;
    texture->Height = height;
    texture->Type = GL_UNSIGNED_BYTE;
    texture->Bpp = 8;

    GLuint texName = 0;
    glGenTextures(1, &texName);
    texture->TextureID = texName;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_3D, texName);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, repeatR ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_R, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_G, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_A, GL_RED);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, width, height, depth, 0, GL_RED, GL_UNSIGNED_BYTE, data);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    *texHandle = (TextureHandle)texture;

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texName, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            Load3DTextureRGB(const unsigned char *data, int width, int height, int depth, TextureHandle *texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name, void* _context)
{
    if (!glTexImage3D) return false;

    glTexture *texture = SL_NEW glTexture;
    texture->Width = width;
    texture->Height = height;
    texture->Type = GL_UNSIGNED_BYTE;
    texture->Bpp = 16;

    GLuint texName = 0;
    glGenTextures(1, &texName);
    texture->TextureID = texName;

    glPixelStorei(GL_UNPACK_ROW_LENGTH, width);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_3D, texName);

    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, repeatR ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    // Make sure error code is reset
    GLenum errCode = glGetError();
    glTexImage3D(GL_TEXTURE_3D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, width, height, depth, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    errCode = glGetError();
    if (errCode != GL_NO_ERROR) // Error, try RGBA as a fallback
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, width, height, depth, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

    *texHandle = (TextureHandle)texture;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texName, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            Load3DTextureLA(const unsigned char *data, int width, int height, int depth, TextureHandle *texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name, void* _context)
{
    if (!glTexImage3D) return false;
    glTexture *texture = SL_NEW glTexture;
    texture->Width = width;
    texture->Height = height;
    texture->Type = GL_UNSIGNED_BYTE;
    texture->Bpp = 16;

    GLuint texName = 0;
    glGenTextures(1, &texName);
    texture->TextureID = texName;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindTexture(GL_TEXTURE_3D, texName);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, repeatU ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, repeatV ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, repeatR ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_R, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_G, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RG, width, height, depth, 0, GL_RG, GL_UNSIGNED_BYTE, data);
    *texHandle = (TextureHandle)texture;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, texName, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool          SubLoad3DTextureLA(const unsigned char *data, int width, int height, int depth,
        int x, int y, int z, int rowPitch, int slicePitch,
        TextureHandle texHandle, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* actualStream = context->GetForceImmediate() ? context->GetImmediateStream() : context->GetStream();

    if (!context->GetImmediateStream()->HasTexSubImage3D()) return false;

    if (rowPitch < int(width * 2 * sizeof(unsigned char)))
        rowPitch = int(width * 2 * sizeof(unsigned char));

    if (slicePitch < int(height * width * 2 * sizeof(unsigned char)))
        slicePitch = int(height * width * 2 * sizeof(unsigned char));

    const glTexture* texture = (glTexture*)texHandle;
    const GLuint textureId = texture->TextureID;

    actualStream->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    actualStream->glPixelStorei(GL_UNPACK_ROW_LENGTH, int(rowPitch / 2 * sizeof(unsigned char)));
    actualStream->glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, int(slicePitch / rowPitch));

    actualStream->glBindTexture(GL_TEXTURE_3D, textureId);
    actualStream->glTexSubImage3D(GL_TEXTURE_3D, 0, x, y, z, width, height, depth, GL_RG, GL_UNSIGNED_BYTE, data);

    actualStream->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    actualStream->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    actualStream->glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);

    actualStream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            EnableTexture(TextureHandle tex, int stage, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->EnableTexture(tex, stage);
    return true;
}

SILVERLININGDLL_API bool            Enable3DTexture(TextureHandle tex, int stage, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->Enable3DTexture(tex, stage);
    return true;
}

SILVERLININGDLL_API bool            DisableTexture(int stage, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->DisableTexture(stage);
    return true;
}

SILVERLININGDLL_API bool            ReleaseTexture(TextureHandle tex)
{
    glTexture* texture = (glTexture*)tex;
    texLoader.FreeTexture(texture);
    SL_DELETE texture;

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            HasPointSprites(void)
{
    return false;
}

SILVERLININGDLL_API bool            EnablePointSprites(double pointSize, void* _context)
{
    return false;
}

SILVERLININGDLL_API bool            DisablePointSprites(void* _context)
{
    return false;
}

SILVERLININGDLL_API bool            GetViewport(int& x, int& y, int& w, int& h)
{
    OpenGLUtils::CheckError(__LINE__);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    x = vp[0];
    y = vp[1];
    w = vp[2];
    h = vp[3];

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            GetDepthRange(float &zmin, float &zmax)
{
    GLfloat depthRange[2];
    glGetFloatv(GL_DEPTH_RANGE, depthRange);

    zmin = depthRange[0];
    zmax = depthRange[1];

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool            GetFOV(double &fov, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    double h = context->mProjection.m[5];
    fov = 2.0 * atan(1.0 / h);
    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool InitRenderTarget(int w, int h, RenderTargetHandle *tgtHandle)
{
    return false;
}

SILVERLININGDLL_API bool InitRenderTexture(int w, int h, RenderTextureHandle *texHandle, const char* name, void* _context)
{
    TextureRenderer *tr = TextureRendererFactory::MakeTextureRenderer();
    if (tr) {
        Context* context = static_cast<Context*>(_context);
        if (tr->Initialize(w, h, context, name)) {
            *texHandle = (void *)tr;
            return true;
        }
    }

    OpenGLUtils::CheckError(__LINE__);
    return false;
}

SILVERLININGDLL_API bool MakeRenderTargetCurrent(RenderTargetHandle tgtHandle)
{
    return false;
}

SILVERLININGDLL_API bool MakeRenderTextureCurrent(RenderTextureHandle texHandle, bool clear, void* _context)
{
    TextureRenderer *tr = (TextureRenderer *)texHandle;
    if (tr) {
        return tr->MakeCurrent(clear);
    }

    OpenGLUtils::CheckError(__LINE__);
    return false;

}

SILVERLININGDLL_API bool RestoreRenderTarget(RenderTargetHandle tgt)
{
    return false;
}

SILVERLININGDLL_API bool BindRenderTexture(RenderTextureHandle texHandle, void* camera, void* _context)
{
    TextureRenderer *tr = (TextureRenderer *)texHandle;
    if (tr) {
        return tr->BindToTexture(camera);
    }

    OpenGLUtils::CheckError(__LINE__);
    return false;
}

SILVERLININGDLL_API bool GetRenderTextureTextureHandle(RenderTextureHandle renTexHandle,
        TextureHandle *texHandle, void* _context)
{
    TextureRenderer *tr = (TextureRenderer *)renTexHandle;
    if (tr) {
        *texHandle = (TextureHandle)(tr->GetTextureHandle());
        return true;
    }

    return false;
}

SILVERLININGDLL_API bool ReleaseRenderTarget(RenderTargetHandle tgt)
{
    return false;
}

SILVERLININGDLL_API bool ReleaseRenderTexture(RenderTextureHandle texHandle, void* _context)
{
    TextureRenderer *tr = (TextureRenderer *)texHandle;

    if (tr) {
        SL_DELETE tr;
        return true;
    }

    return false;
}

SILVERLININGDLL_API bool InitRenderTextureCube(int w, int h, SilverLining::RenderTextureHandle *texHandle, bool floatingPoint, bool generateMipMaps, const char* name, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    CubeMapRenderTexture *cubeMap = SL_NEW CubeMapRenderTexture(w, h, floatingPoint, generateMipMaps, name, context&&context->avoidStalls);
    *texHandle = (RenderTextureHandle *)cubeMap;

    return true;
}

SILVERLININGDLL_API bool MakeRenderTextureCubeCurrent(SilverLining::RenderTextureHandle texHandle, bool clear, CubeFace face, void* _context)
{
    if (texHandle) {
        CubeMapRenderTexture *cubeMap = (CubeMapRenderTexture *)texHandle;
        return cubeMap->MakeCurrent(face);
    }
    return false;
}

SILVERLININGDLL_API bool BindRenderTextureCube(SilverLining::RenderTextureHandle texHandle, void* _context)
{
    CubeMapRenderTexture *cubeMap = (CubeMapRenderTexture *)texHandle;
    return cubeMap->Bind();
}

SILVERLININGDLL_API bool GetRenderTextureCubeTextureHandle(SilverLining::RenderTextureHandle renTexHandle, SilverLining::TextureHandle *texHandle, void* _context)
{
    if (renTexHandle && texHandle) {
        CubeMapRenderTexture *cubeMap = (CubeMapRenderTexture *)renTexHandle;
        return cubeMap->GetTextureHandle(texHandle);
    }
    return false;
}

SILVERLININGDLL_API bool ReleaseRenderTextureCube(SilverLining::RenderTextureHandle texHandle, void* _context)
{
    if (texHandle) {
        CubeMapRenderTexture *cubeMap = (CubeMapRenderTexture *)texHandle;
        SL_DELETE cubeMap;
        return true;
    }
    return false;
}

SILVERLININGDLL_API bool GetPixels(int x, int y, int w, int h, void *pixels, bool, void*)
{
    return false;
}

SILVERLININGDLL_API bool SetPixels(int x, int y, int w, int h, void *pixels, void* _context)
{
    return false;
}

SILVERLININGDLL_API bool            DrawLineStrip(VertexBufferHandle vbh, const Color& c, int nVerts, float width, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    stream->glDisable(GL_LINE_SMOOTH);

    const LineShader & ls = context->GetLineShader(GetUserVertFileName(), GetUserFragFileName(), resourceLoader);
    if (!ls.sh) // It does not make sense to draw if shader still not set at this point
        return false;

    if (ls.sh != context->GetCurrentShader()) {
        // Bind and set model view proj if not active
        context->BindShader(ls.sh);

        Matrix4 modelview, proj, modelviewProj;

        ConvertMatrix(context->mModelview.m, &modelview);
        ConvertMatrix(context->mProjection.m, &proj);
        modelviewProj = proj * modelview;

        float m[16];
        int i = 0;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                m[i++] = (float)(modelviewProj.elem[row][col]);
            }
        }

        stream->glUniformMatrix4fv(ls.modelViewProjUniformLocation, 1, 1, m);

        stream->checkGlError(__LINE__);
    }

    context->SetVertexBuffer(vbh);

    stream->glUniform4f(ls.colorUniformLocation, c.r, c.g, c.b, c.a);

    // WL note: glLineWidth is a valid core OpenGL 3.2 call
    // Nevertheless NVidia Drivers 275.33 generate invalid value error here
    stream->glLineWidth((GLfloat)width);
    // So as a workaround we reset the error bit by calling glGetError
    if (context->GetStream()->isImmediate() || context->GetForceImmediate()) {
        glGetError();
    }

    if (context->GetCurrentShader()) {
        context->GetCurrentShader()->PreDraw();
    }

    stream->glDrawArrays(GL_LINE_STRIP, 0, nVerts);

    context->UnbindShader();

    context->UnsetVertexBuffer();

    stream->checkGlError(__LINE__);

    return true;
}

static bool DrawLineStripInternal(const Color& c, double width, const Vector3 * pp, unsigned int count, Context* context)
{
    const LineShader & ls = context->GetLineShader(GetUserVertFileName(), GetUserFragFileName(), resourceLoader);
    if (!ls.sh) // It does not make sense to draw if shader still not set at this point
        return false;

    OpenGlStream* stream = context->GetStream();

    if (ls.sh != context->GetCurrentShader()) {
        // Bind and set model view proj if not active
        context->BindShader(ls.sh);

        Matrix4 modelview, proj, modelviewProj;

        ConvertMatrix(context->mModelview.m, &modelview);
        ConvertMatrix(context->mProjection.m, &proj);
        modelviewProj = proj * modelview;

        float m[16];
        int i = 0;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                m[i++] = (float)(modelviewProj.elem[row][col]);
            }
        }

        stream->glUniformMatrix4fv(ls.modelViewProjUniformLocation, 1, 1, m);

        OpenGLUtils::CheckError(__LINE__);
    }

    stream->glBindVertexArrayFor(0, context->GetCurrentShader());

    float *verts = SL_NEW float[count * 3];
    int idx = 0;
    for (unsigned int i = 0; i < count; i++) {
        verts[idx++] = (float)pp[i].x;
        verts[idx++] = (float)pp[i].y;
        verts[idx++] = (float)pp[i].z;
    }

    stream->glEnableVertexAttribArray(ls.vertexLocation);
    stream->glVertexAttribPointer(ls.vertexLocation, 3, GL_FLOAT, GL_FALSE, 0, verts);
    stream->glUniform4f(ls.colorUniformLocation, c.r, c.g, c.b, c.a);

    // WL note: glLineWidth is a valid core OpenGL 3.2 call
    // Nevertheless NVidia Drivers 275.33 generate invalid value error here
    stream->glLineWidth((GLfloat)width);
    // So as a workaround we reset the error bit by calling glGetError
    if (context->GetStream()->isImmediate() || context->GetForceImmediate()) {
        glGetError();
    }

    stream->glBindBuffer(GL_ARRAY_BUFFER, (SL_Buffer*)0);

    if (context->GetCurrentShader()) {
        context->GetCurrentShader()->PreDraw();
    }

    stream->glDrawArrays(GL_LINE_STRIP, 0, count);

    context->UnbindShader();

    SL_DELETE[] verts;

    stream->checkGlError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool DrawAALine(const Color& c, double width, const Vector3& p1, const Vector3& p2, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    stream->glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    stream->glEnable(GL_LINE_SMOOTH);

    const Vector3 av[2] = { p1, p2 };
    bool result = DrawLineStripInternal(c, width, av, 2, context);

    stream->glDisable(GL_LINE_SMOOTH);
    stream->checkGlError(__LINE__);

    return result;
}

SILVERLININGDLL_API bool DrawAALines(const Color& c, double width, const SL_VECTOR(Vector3)& points, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();

    // Some systems have a real hard time drawing anti-aliased lines, and they aren't
    // really necessary for lightning to look good, so we disable them for performance now.
    stream->glDisable(GL_LINE_SMOOTH);
    stream->checkGlError(__LINE__);

    return DrawLineStripInternal(c, width, &points.front(), (unsigned int)points.size(), context);
}


SILVERLININGDLL_API bool SetViewport(int x, int y, int w, int h, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetViewport(x, y, w, h);
    return true;
}

SILVERLININGDLL_API bool SetDepthRange(float zmin, float zmax, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetDepthRange(zmin, zmax);
    return true;
}

SILVERLININGDLL_API bool CreateLuminanceTexture(int w, int h, TextureHandle *texture, const char* name)
{
    glTexture* glTex = SL_NEW glTexture;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTex->TextureID = tex;
    glTex->Width = w;
    glTex->Height = h;
    glTex->Bpp = 32;

    *texture = glTex;

    if (name != 0 && glObjectLabel) {
        glObjectLabel(GL_TEXTURE, tex, -1, name);
    }

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool CopyLuminanceIntoTexture(TextureHandle texture, int w, int h, unsigned char *buf)
{
    glTexture *glTex = (glTexture *)texture;
    glBindTexture(GL_TEXTURE_2D, glTex->TextureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

SILVERLININGDLL_API bool CopyLuminanceFromScreen(int x, int y, int w, int h, unsigned char *buf)
{
    glRasterPos2i(0, 0);

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    OpenGLUtils::CheckError(__LINE__);
    return true;
}

class OcclusionQuery
{
public:
    OcclusionQuery(GLuint q) : id(q)
    {
    }
    GLuint id;
};

SILVERLININGDLL_API bool StartOcclusionQuery(SilverLining::QueryHandle* queryHandle)
{
    if (OpenGLExtensionManager::usingOcclusionQuery) {
        GLuint q;
        glGenQueries(1, &q);

        OcclusionQuery* occlusionQuery = SL_NEW OcclusionQuery(q);

        *queryHandle = occlusionQuery;

        glBeginQuery(GL_SAMPLES_PASSED, q);

        return OpenGLUtils::QueryError();
    }
    return false;
}

SILVERLININGDLL_API bool EndOcclusionQuery(SilverLining::QueryHandle queryHandle)
{
    if (OpenGLExtensionManager::usingOcclusionQuery) {
        glEndQuery(GL_SAMPLES_PASSED);
        return OpenGLUtils::QueryError();
    }
    return false;
}

SILVERLININGDLL_API unsigned int GetOcclusionQueryResult(SilverLining::QueryHandle queryHandle)
{
    if (OpenGLExtensionManager::usingOcclusionQuery) {
        OcclusionQuery* occlusionQuery = (OcclusionQuery *)queryHandle;
        GLuint q = occlusionQuery->id;

        GLuint nSamples = 0;
        glGetQueryObjectuiv(q, GL_QUERY_RESULT, &nSamples);
        OpenGLUtils::CheckError(__LINE__);

        glDeleteQueries(1, &q);
        OpenGLUtils::CheckError(__LINE__);

        SL_DELETE occlusionQuery;

        return nSamples;
    }
    return 0;
}

SILVERLININGDLL_API unsigned int GetLineShader(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    const LineShader & ls = context->GetLineShader(GetUserVertFileName(), GetUserFragFileName(), resourceLoader);
    const Shader *s = (const Shader*)(ls.sh);
    if (s->glProgram) {
        return s->glProgram;
    }
    return 0;
}

SILVERLININGDLL_API bool            Load3DTextureFloat(const float *data, int width, int height, int depth, TextureHandle *texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    return false;
}

SILVERLININGDLL_API void SetForceImmediate(bool val, void* _context)
{
    Context* context = static_cast<Context*>(_context);
    context->SetForceImmediate(val);
}

SILVERLININGDLL_API bool GetForceImmediate(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    return context->GetForceImmediate();
}

SILVERLININGDLL_API void ExecuteStream(void* _context)
{
    Context* context = static_cast<Context*>(_context);
    OpenGlStream* stream = context->GetStream();
    stream->execute();
    stream->reset();
}

SILVERLININGDLL_API void SetStream(void* _stream, int frameIndex, void* _context)
{
    // nop
}

SILVERLININGDLL_API void SettingsChanged(void* _environment, void* _context)
{
    // nop
}