// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
// SilverLiningVulkan.cpp : Defines the entry point for the DLL application.
//

#include "SLAssert.h"
#if defined(WIN32) || defined(WIN64)
#include <windows.h>
#endif

#include "MemAlloc.h"
#include "SilverLiningVulkan.h"
#include "SLAssert.h"

#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <stack>
#include <algorithm>

#include "VulkanInitInfo.h"
#include "VulkanContext.h"
#include "Device.h"
#include "Pipeline.h"
#include "VulkanBuffer.h"
#include "Texture.h"
#include "RenderToTexture.h"
#include "BufferVk.h"
#include "FrameIndexedBuffer.h"

using namespace SilverLining;
using namespace SilverLining::Vulkan;

//static TextureLoader texLoader;

static void SetUserVertFileName(const char* userString)
{
    //Shader::userVertFileName = userString;
}

static void SetUserFragFileName(const char* userString)
{
    //Shader::userFragFileName = userString;
}

static const char* GetUserVertFileName()
{
    //return Shader::userVertFileName;
}

static const char* GetUserFragFileName()
{
    //return Shader::userFragFileName;
}

#ifndef SILVERLINING_STATIC_RENDERER_VULKAN
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

SILVERLININGDLL_API void DeviceLost(void*) {}
SILVERLININGDLL_API void DeviceReset(void*) {}

SILVERLININGDLL_API bool BackfaceCullClockwise(bool cullCW, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->BackfaceCullClockwise(cullCW);
    return true;
}

SILVERLININGDLL_API void* SetEnvironment(bool rightHanded, void* env, SilverLining::ResourceLoader* pResourceLoader,
        SilverLining::Allocator* pAllocator, const char* userVertString, const char* userFragString, bool avoidStalls,
        const char* userVertFilename, const char* userFragFilename, bool useUBOs, bool deferred, const SL_VECTOR(unsigned int)& userShaders)
{
    // Set user define strings
    SetUserDefinedVertString(userVertString);
    SetUserDefinedFragString(userFragString);
    SetUserVertFileName(userVertFilename);
    SetUserFragFileName(userFragFilename);

    VulkanInitInfo* info = static_cast<VulkanInitInfo*>(env);
    VulkanContext* vulkanContext = new VulkanContext(*info, pResourceLoader);

    return (void*)(vulkanContext);
}

SILVERLININGDLL_API void SetContext(void* pContext)
{
}

SILVERLININGDLL_API void ClearScreen(const Color& c, void* _context)
{
}

SILVERLININGDLL_API bool HasInstancing(void)
{
    return true;
}
SILVERLININGDLL_API bool HasBindlessTextures(void)
{
    return true;
}
SILVERLININGDLL_API bool HasUBOs(void)
{
    return true;
}
SILVERLININGDLL_API bool HasSSBOs(void)
{
    return true;
}
SILVERLININGDLL_API bool Has64BitSupport(void)
{
    return true;
}

SILVERLININGDLL_API bool HasBindlessIndirectRendering(void)
{
    return false;
}

SILVERLININGDLL_API SilverLining::Buffer* CreateBuffer(const char* _name, SilverLining::BufferType bufferType, int sizeInBytes, const BufferProperties& _bufferProperties, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    BufferVk* bufferVk = new BufferVk(_name, bufferType, sizeInBytes, _bufferProperties, vulkanContext);
    return bufferVk;
}

SILVERLININGDLL_API void DestroyBuffer(SilverLining::Buffer* bufferVk, void* _context)
{
    if (bufferVk) {
        delete bufferVk;
    }
}

SILVERLININGDLL_API GPUAddressType GetTextureAddress(TextureHandle textureHandle)
{
    return NULL;// (GPUAddressType)textureAddress;
}

SILVERLININGDLL_API void MakeTextureAddressResident(GPUAddressType address)
{
}

SILVERLININGDLL_API GPUAddressType GetBufferAddress(BufferHandle buffer)
{
    return NULL;// (GPUAddressType)bufferAddress;
}

SILVERLININGDLL_API void MakeBufferResident(BufferHandle buffer, bool makeResident)
{
}

SILVERLININGDLL_API bool IsBufferResident(BufferHandle buffer)
{
    return false;
}

SILVERLININGDLL_API void ClearDepth(void* _context)
{
}

class Matrix4x4 : public MemObject
{
public:
    Matrix4x4()
    {
    }

    Matrix4x4(double* mat)
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

    Matrix4x4 operator * (const Matrix4x4& mat) const
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

SILVERLININGDLL_API ShaderHandle     LoadShaderFromSource(const char* shaderSource)
{
    return 0;
}

SILVERLININGDLL_API void DeleteShader(ShaderHandle shader, void* _context)
{
}

void SetUserDefinedVertString(const char* userString)
{
    //Shader::userDefinedVertString = userString;
}

void SetUserDefinedFragString(const char* userString)
{
    //Shader::userDefinedFragString = userString;
}

const char* GetUserDefinedVertString()
{
    return "";// Shader::userDefinedVertString;
}

const char* GetUserDefinedFragString()
{
    return "";// Shader::userDefinedFragString;
}


SILVERLININGDLL_API void             SetUserShaders(const SL_VECTOR(unsigned int)& shaders, void* _context)
{

}

SILVERLININGDLL_API unsigned int GetShaderProgramObject(ShaderHandle shader)
{
    return 0;
}

SILVERLININGDLL_API ShaderHandle     LoadShaderFromFile(const char* fileName, const char* userVertShader, const char* userFragShader, bool enableObjectLabeling, const SL_VECTOR(SL_STRING)& defines, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    return vulkanContext->LoadShaderFromFile(fileName, userVertShader, userFragShader, enableObjectLabeling, defines);
}

SILVERLININGDLL_API int GetConstantLocation(ShaderHandle shader, const char* varName)
{
    Pipeline* pipeline = static_cast<Pipeline*>(shader);
    return pipeline->GetVariableLocation(varName);
}

SILVERLININGDLL_API void SetConstantVector4AtLocation(SilverLining::ShaderHandle shader, int loc, const float* data, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    vulkanContext->SetConstantVector4AtLocation(shader, loc, data);
}

SILVERLININGDLL_API void SetConstantMatrix4AtLocation(SilverLining::ShaderHandle shader, int loc, float* data, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    vulkanContext->SetConstantMatrix4AtLocation(shader, loc, data);
}

SILVERLININGDLL_API bool SetConstantVector4(ShaderHandle shader, const char* varName, const float* data)
{
    return true;
}

SILVERLININGDLL_API bool SetConstantInt(ShaderHandle shader, const char* varName, int val)
{
    return true;
}

SILVERLININGDLL_API bool SetConstantMatrix4(ShaderHandle shader, const char* varName, float* data)
{
    return true;
}

SILVERLININGDLL_API bool PreConstantsSet(ShaderHandle shader)
{
    return true;
}

SILVERLININGDLL_API bool PostConstantsSet(ShaderHandle shader)
{
    return true;
}

SILVERLININGDLL_API bool BindShader(ShaderHandle shader, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->BindPipeline(shader);
    return true;
}

SILVERLININGDLL_API bool UnbindShader(void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->UnBindPipeline();
    return true;
}

SILVERLININGDLL_API bool IncrementConstantBuffersOffset(ShaderHandle shader, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->IncrementConstantBuffersOffset(shader);
    return true;
}

SILVERLININGDLL_API bool             ShutdownShaderSystem(void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    delete vulkanContext;
    return true;
}

SILVERLININGDLL_API bool SetVertexBuffer(VertexBufferHandle vbh, bool vertexColorsNotUsed, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    vulkanContext->SetVertexBuffer(vbh);
    return true;
}

SILVERLININGDLL_API bool UnsetVertexBuffer(void* _context)
{
    return true;
}

SILVERLININGDLL_API bool IncrementOffsetIndex(VertexBufferHandle vbh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->IncrementOffsetIndex(vbh);
    return true;
}

SILVERLININGDLL_API bool            DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, bool preDraw, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DrawStrip(ibh, startIdx, nIndices, nVerts, 1, 0);
    return true;
}

SILVERLININGDLL_API bool   DrawMultiStrips(void* _commandBuffer, void* _context)
{
    return true;
}
SILVERLININGDLL_API bool            DrawStripInstanced(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DrawStrip(ibh, startIdx, nIndices, nVerts, instanceCount, firstInstance);
    return true;
}

SILVERLININGDLL_API bool             DrawPoints(double pointSize, int nPoints, int start, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DrawPoints(pointSize, nPoints, start);
    return true;
}

SILVERLININGDLL_API bool            HasQuads()
{
    return false;
}

SILVERLININGDLL_API bool            DrawQuads(int nPoints, int start, void* _context)
{
    return true;
}

SILVERLININGDLL_API void* AllocateVertexBuffer(int numVertices, const char* _name, const SilverLining::BufferProperties& bufferProperties, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    std::string name = (_name) ? _name : "";
    return context->GetAVertexBuffer(numVertices, name, bufferProperties);
}

SILVERLININGDLL_API bool             LockVertexBuffer(void* buffer)
{
    return true;
}

SILVERLININGDLL_API bool GetVertexBuffer(void* vbh, int offset, Vertex* verts, int nVerts, void* _context)
{
    SilverLining::Vulkan::Buffer* buffer = static_cast<SilverLining::Vulkan::Buffer*>(vbh);

    if (buffer->Dynamic() == false) {
        buffer->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "Buffer is not mappable!" << std::endl;
        return false;
    }
    RawBuffer* rawBuffer = buffer->GetBuffer();
    void* mappedData = rawBuffer->MappedPtr();
    if (mappedData == nullptr) {
        buffer->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "mappedData == nullptr!" << std::endl;
        return false;
    }
    uint8_t* pSrc = (uint8_t*)mappedData + (offset * sizeof(Vertex));
    const int bytesToCopy = nVerts * sizeof(Vertex);
    memcpy(verts, pSrc, bytesToCopy);

    return true;
}

SILVERLININGDLL_API bool UpdateVertexBuffer(void* vbh, int offset, Vertex* verts, int nVerts, bool justColors, void* _context)
{
    SilverLining::Vulkan::Buffer* buffer = static_cast<SilverLining::Vulkan::Buffer*>(vbh);

    if (buffer->Dynamic() == false) {
        buffer->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "Buffer is not mappable!" << std::endl;
        return false;
    }
    RawBuffer* rawBuffer = buffer->GetBuffer();
    void* mappedData = rawBuffer->MappedPtr();
    if (mappedData == nullptr) {
        buffer->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "mappedData == nullptr!" << std::endl;
        return false;
    }
    uint8_t* pDst = (uint8_t*)mappedData + (offset * sizeof(Vertex));
    const int bytesToCopy = nVerts * sizeof(Vertex);
    memcpy(pDst, verts, bytesToCopy);

    return true;
}

SILVERLININGDLL_API SilverLining::Vertex* GetVertices(void* vbh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    void* mappedData = context->GetMappedData(vbh);
    return static_cast<SilverLining::Vertex*>(mappedData);
}

SILVERLININGDLL_API bool             UnlockVertexBuffer(void* vbh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    return context->UnlockBuffer(vbh);
}

SILVERLININGDLL_API bool             ReleaseVertexBuffer(void* vbh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    SilverLining::Vulkan::Buffer* vulkanBuffer = static_cast<SilverLining::Vulkan::Buffer*>(vbh);
    context->ReturnAVertexBuffer(vulkanBuffer);

    return true;
}

SILVERLININGDLL_API void* AllocateIndexBuffer(int nIndices, const char* _name, const SilverLining::BufferProperties& bufferProperties, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    std::string name = (_name) ? _name : "";
    return context->GetAnIndexBuffer(nIndices, name, bufferProperties);
}

SILVERLININGDLL_API bool LockIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    return true;
}

SILVERLININGDLL_API Index* GetIndices(IndexBufferHandle ibh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    void* mappedData = context->GetMappedData(ibh);
    return static_cast<SilverLining::Index*>(mappedData);
}

SILVERLININGDLL_API bool UnlockIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    return context->UnlockBuffer(ibh);
}

SILVERLININGDLL_API bool ReleaseIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    SilverLining::Vulkan::Buffer* vulkanBuffer = static_cast<SilverLining::Vulkan::Buffer*>(ibh);
    context->ReturnAnIndexBuffer(vulkanBuffer);

    return true;
}

SILVERLININGDLL_API bool SetIndexBuffer(IndexBufferHandle ibh, void* _context)
{
    VulkanContext* vulkanContext = static_cast<VulkanContext*>(_context);
    VkDeviceSize offsets[1] = { 0 };
    SilverLining::Vulkan::Buffer* indexBuffer = static_cast<SilverLining::Vulkan::Buffer*>(ibh);
    VkBuffer buffer = indexBuffer->VulkanBuffer();
    vkCmdBindIndexBuffer(vulkanContext->commandBuffer, buffer, 0, VK_INDEX_TYPE_UINT32);
    return true;
}

SILVERLININGDLL_API bool UnsetIndexBuffer(void* _context)
{
    return true;
}

// This matches what we are doing for OpenGL
SILVERLININGDLL_API bool EnableDepthWrites(bool enable, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    VkBool32 vkEnable = (enable) ? VK_TRUE : VK_FALSE;
    vkCmdSetDepthWriteEnable(context->commandBuffer, vkEnable);
    return true;
}

// This matches what we are doing for OpenGL
SILVERLININGDLL_API bool EnableDepthReads(bool enable, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    VkBool32 vkEnable = (enable) ? VK_TRUE : VK_FALSE;
    vkCmdSetDepthTestEnable(context->commandBuffer, vkEnable);
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
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    VkCullModeFlags cullMode = (enable) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    vkCmdSetCullMode(context->commandBuffer, cullMode);
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

static void ConvertMatrix(const double* in, Matrix4* out)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out->elem[row][col] = in[col * 4 + row];
        }
    }
}

static void ConvertMatrix(const Matrix4* in, double* out)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            out[col * 4 + row] = in->elem[row][col];
        }
    }
}

SILVERLININGDLL_API bool             SetProjectionMatrix(const SilverLining::Matrix4& m, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->SetProjectionMatrix(m);
    return true;
}

SILVERLININGDLL_API bool             GetTextureMatrix(SilverLining::Matrix4* m, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool             SetTextureMatrix(const SilverLining::Matrix4& m, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool             GetModelviewMatrix(SilverLining::Matrix4* m, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool             SetModelviewMatrix(const SilverLining::Matrix4& m, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->SetModelviewMatrix(m);
    return true;
}



SILVERLININGDLL_API bool             PushAllState(void* _context)
{
    return true;
}

SILVERLININGDLL_API bool             PopAllState(void* _context)
{
    return true;
}

SILVERLININGDLL_API bool            SetDefaultState(void* _context)
{
    return true;
}

SILVERLININGDLL_API bool HeartBeat(void)
{
    return true;
}

SILVERLININGDLL_API bool ContextBeingDeleted(void* context)
{
    return true;
}

SILVERLININGDLL_API bool        SetReverseZ(bool reverse, void* _context)
{
    return true;
}



// This matches what we are doing for OpenGL
SILVERLININGDLL_API bool             EnableBlending(int srcFactor, int dstFactor, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->EnableBlending((BlendFactor)srcFactor, (BlendFactor)dstFactor);
    return true;
}

SILVERLININGDLL_API bool             DisableBlending(void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DisableBlending();
    return true;
}

SILVERLININGDLL_API void* GetNativeTexture(TextureHandle tex)
{
    return tex;
}

SILVERLININGDLL_API bool            LoadTextureFromFile(const char* imgPath, TextureHandle* textureHandle, bool repeatU, bool repeatV, const char* _name, void* _context)
{
    std::string imageFileName(imgPath);
    std::string name = (_name) ? _name : "";

    *textureHandle = 0;
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = context->LoadTextureFromFile(imageFileName, repeatU, repeatV, name);
    *textureHandle = texture;

    return texture!=nullptr;
}

SILVERLININGDLL_API bool            HasFloatTextures()
{
    return true;
}

SILVERLININGDLL_API bool            LoadFloatTextureRGB(const float* data, int width, int height, TextureHandle* textureHandle, const char* _name, void* _context)
{
    std::string name = (_name) ? _name : "";

    *textureHandle = 0;
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = context->LoadFloatTextureRGB(data, width, height, name);
    *textureHandle = texture;

    return texture != nullptr;
}

SILVERLININGDLL_API bool            LoadFloatTexture(const float* data, int width, int height, TextureHandle* texHandle, const char* name)
{
    return false;
}

SILVERLININGDLL_API bool            LoadTexture(const unsigned char* data, int width, int height, TextureHandle* textureHandle,
        bool repeatU, bool repeatV, const char* _name, void* _context)
{
    std::string name = (_name) ? _name : "";

    *textureHandle = 0;
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = context->LoadTexture(data, width, height, repeatU, repeatV, name);
    *textureHandle = texture;

    return texture != nullptr;
}

SILVERLININGDLL_API bool            Load3DTexture(const unsigned char* data, int width, int height, int depth, TextureHandle* texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    SL_ASSERT(false && "Implement this");
    return false;
}

SILVERLININGDLL_API bool            Load3DTextureRGB(const unsigned char* data, int width, int height, int depth, TextureHandle* textureHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* _name, void* _context)
{
    std::string name = (_name) ? _name : "";

    *textureHandle = 0;
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = context->Load3DTextureRGB(data
                       , width, height, depth
                       , repeatU, repeatV, repeatR
                       , name);
    *textureHandle = texture;

    return texture != nullptr;
}

SILVERLININGDLL_API bool            Load3DTextureLA(const unsigned char* data, int width, int height, int depth, TextureHandle* textureHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* _name, void* _context)
{
    std::string name = (_name) ? _name : "";

    *textureHandle = 0;
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = context->Load3DTextureLA(data
                       , width, height, depth
                       , repeatU, repeatV, repeatR
                       , name);
    *textureHandle = texture;

    return texture != nullptr;
}

SILVERLININGDLL_API bool          SubLoad3DTextureLA(const unsigned char* data, int width, int height, int depth,
        int x, int y, int z, int rowPitch, int slicePitch,
        TextureHandle textureHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    Texture* texture = (Texture*)(textureHandle);
    context->SubLoad3DTextureLA(texture, data, width, height, depth
                                , x, y, z, rowPitch, slicePitch);
    return false;
}

SILVERLININGDLL_API bool            EnableTexture(TextureHandle tex, int stage, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->EnableTexture(tex, stage);
    return true;
}

SILVERLININGDLL_API bool            Enable3DTexture(TextureHandle tex, int stage, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->EnableTexture(tex, stage);
    return true;
}

SILVERLININGDLL_API bool            DisableTexture(int stage, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DisableTexture(stage);
    return true;
}

SILVERLININGDLL_API bool            ReleaseTexture(TextureHandle tex)
{
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
    return true;
}

SILVERLININGDLL_API bool            GetDepthRange(float& zmin, float& zmax)
{
    zmin = 0.0;
    zmax = 1.0;
    return true;
}

SILVERLININGDLL_API bool            GetFOV(double& fov, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool InitRenderTarget(int w, int h, RenderTargetHandle* tgtHandle)
{
    return false;
}

SILVERLININGDLL_API bool MakeRenderTargetCurrent(RenderTargetHandle tgtHandle)
{
    return false;
}


SILVERLININGDLL_API bool RestoreRenderTarget(RenderTargetHandle tgt)
{
    return false;
}

SILVERLININGDLL_API bool ReleaseRenderTarget(RenderTargetHandle tgt)
{
    return false;
}



SILVERLININGDLL_API bool InitRenderTexture(int w, int h, RenderTextureHandle* rttHandle, const char* _name, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    std::string name = (_name) ? _name : "";
    RenderToTexture* rtt = context->CreateRenderToTexture(w, h, name);

    *rttHandle = rtt;

    return true;
}

SILVERLININGDLL_API bool MakeRenderTextureCurrent(RenderTextureHandle rttHandle, bool clear, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    RenderToTexture* rtt = static_cast<RenderToTexture*>(rttHandle);
    context->MakeCurrent(rtt);

    return true;
}


SILVERLININGDLL_API bool BindRenderTexture(RenderTextureHandle rttHandle, void* camera, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    RenderToTexture* rtt = static_cast<RenderToTexture*>(rttHandle);
    context->Bind(rtt);
    return true;
}

SILVERLININGDLL_API bool GetRenderTextureTextureHandle(RenderTextureHandle rttHandle, TextureHandle* textureHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    RenderToTexture* rtt = static_cast<RenderToTexture*>(rttHandle);
    *textureHandle = context->GetImage(rtt);
    return true;
}

SILVERLININGDLL_API bool ReleaseRenderTexture(RenderTextureHandle rttHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    RenderToTexture* rtt = static_cast<RenderToTexture*>(rttHandle);
    return true;
}

SILVERLININGDLL_API bool InitRenderTextureCube(int width, int height, SilverLining::RenderTextureHandle* rttHandle, bool floatingPoint, bool generateMipMaps, const char* _name, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    std::string name = (_name) ? _name : "";
    CubeMapRenderTexture* cubeMapRtt = context->CreateRenderToTextureCube(width, height, floatingPoint, generateMipMaps, name);
    *rttHandle = cubeMapRtt;
    return true;
}

SILVERLININGDLL_API bool MakeRenderTextureCubeCurrent(SilverLining::RenderTextureHandle rttHandle, bool clear, CubeFace face, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    CubeMapRenderTexture* rtt = static_cast<CubeMapRenderTexture*>(rttHandle);
    context->MakeCurrent(rtt, face);
    return true;
}

SILVERLININGDLL_API bool BindRenderTextureCube(SilverLining::RenderTextureHandle rttHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    CubeMapRenderTexture* rtt = static_cast<CubeMapRenderTexture*>(rttHandle);
    context->Bind(rtt);
    return true;
}

SILVERLININGDLL_API bool ReleaseRenderTextureCube(SilverLining::RenderTextureHandle rttHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    CubeMapRenderTexture* rtt = static_cast<CubeMapRenderTexture*>(rttHandle);
    context->Release(rtt);
    return true;
}
SILVERLININGDLL_API bool GetRenderTextureCubeTextureHandle(SilverLining::RenderTextureHandle rttHandle, SilverLining::TextureHandle* textureHandle, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    CubeMapRenderTexture* rtt = static_cast<CubeMapRenderTexture*>(rttHandle);
    *textureHandle = context->GetImage(rtt);
    return true;
}

SILVERLININGDLL_API bool GetPixels(int x, int y, int w, int h, void* pixels, bool, void*)
{
    return false;
}

SILVERLININGDLL_API bool SetPixels(int x, int y, int w, int h, void* pixels, void* _context)
{
    return false;
}

SILVERLININGDLL_API bool            DrawLineStrip(VertexBufferHandle vbh, const Color& c, int nVerts, float width, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->DrawLineStrip(vbh, c, nVerts, width);
    return true;
}

SILVERLININGDLL_API bool DrawAALine(const Color& c, double width, const Vector3& p1, const Vector3& p2, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool DrawAALines(const Color& c, double width, const SL_VECTOR(Vector3)& points, void* _context)
{
    return true;
}


SILVERLININGDLL_API bool SetViewport(int x, int y, int w, int h, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool SetDepthRange(float zmin, float zmax, void* _context)
{
    return true;
}

SILVERLININGDLL_API bool CreateLuminanceTexture(int w, int h, TextureHandle* texture, const char* name)
{
    return true;
}

SILVERLININGDLL_API bool CopyLuminanceIntoTexture(TextureHandle texture, int w, int h, unsigned char* buf)
{
    return true;
}

SILVERLININGDLL_API bool CopyLuminanceFromScreen(int x, int y, int w, int h, unsigned char* buf)
{
    return true;
}


SILVERLININGDLL_API bool StartOcclusionQuery(SilverLining::QueryHandle* queryHandle)
{
    return false;
}

SILVERLININGDLL_API bool EndOcclusionQuery(SilverLining::QueryHandle queryHandle)
{
    return false;
}

SILVERLININGDLL_API unsigned int GetOcclusionQueryResult(SilverLining::QueryHandle queryHandle)
{
    return 0;
}

SILVERLININGDLL_API unsigned int GetLineShader(void* _context)
{
    return 0;
}

SILVERLININGDLL_API bool            Load3DTextureFloat(const float* data, int width, int height, int depth, TextureHandle* texHandle,
        bool repeatU, bool repeatV, bool repeatR, const char* name)
{
    return false;
}

SILVERLININGDLL_API void SetForceImmediate(bool val, void* _context)
{

}

SILVERLININGDLL_API bool GetForceImmediate(void* _context)
{
    return false;
}

SILVERLININGDLL_API void ExecuteStream(void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->ExecuteOneTimeOps();
}

SILVERLININGDLL_API void SetStream(void* _stream, int frameIndex, void* _context)
{
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->SetCurrentCommandBuffer(static_cast<VkCommandBuffer>(_stream), frameIndex);
}

SILVERLININGDLL_API void SettingsChanged(void* _environment, void* _context)
{
    VulkanInitInfo* newInfo = static_cast<VulkanInitInfo*>(_environment);
    SL_ASSERT(newInfo);
    VulkanContext* context = static_cast<VulkanContext*>(_context);
    context->SettingsChanged(*newInfo);
}
