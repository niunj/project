// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#pragma once

#include "MemAlloc.h"
#include "SilverLiningTypes.h"
#include "Color.h"
#include "Matrix4.h"
#include "Matrix3.h"
#include "BlendState.h"

#include "VulkanBuffer.h"
#include "BufferProperties.h"
#include "VulkanInitInfo.h"
#include "LogLevel.h"

#include <string>
#include <vector>
#include <map>

#define SL_SHADER_DEV 0

namespace SilverLining
{
   class ResourceLoader;

   namespace Vulkan
   {
      struct VulkanInitInfo;
      class Instance;
      class PhysicalDevice;
      class Device;
      class Shader;
      class ShaderReflection;
      class Pipeline;
      class Texture;
      class Image;
      class TextureDescriptorSetManager;
      class BufferDescriptorSetManager;
      class SamplerManager;
      class SubLoadOperation;
      class RenderToTexture;
      class CubeMapRenderTexture;
      class BufferPool;
      class FrameIndexedBufferManager;

      //! This is the mirror of the Renderer class on the Vulkan Renderer side
      //! The chaining is like this: Renderer <-> SilverLiningVulkan.cpp <-> VulkanContext
      class VulkanContext
      {
      public:
         //! constructor
         VulkanContext(const VulkanInitInfo& info, ResourceLoader* _resourceLoader);
         //! destructor
         virtual ~VulkanContext();

      public:
         //! Load shader from the file
         ShaderHandle LoadShaderFromFile(const char* fileName, const char* userVertShader, const char* userFragShader
            , bool enableObjectLabeling
            , const SL_VECTOR(SL_STRING)& defines);

         //! Load texture from file
         Texture* LoadTextureFromFile(const std::string& imageFileName, bool repeatU, bool repeatV, const std::string& name);

         //! Load texture from raw data
         Texture* LoadTexture(const unsigned char* data, int width, int height, bool repeatU, bool repeatV, const std::string& name);

         //! Load texture from raw data (float)
         Texture* LoadFloatTextureRGB(const float* data, int width, int height, const std::string& name);

         //! Load 3d texture from raw data (unsigned char)
         Texture* Load3DTextureRGB(const unsigned char* data, int width, int height, int depth
            , bool repeatU, bool repeatV, bool repeatR, const std::string& name);

         //! Load 3d luminance-alpha texture
         Texture* Load3DTextureLA(const unsigned char* data, int width, int height, int depth
            , bool repeatU, bool repeatV, bool repeatR, const std::string& name);

         //! Update 3d luminance-alpha texture
         void SubLoad3DTextureLA(Texture* texture, const unsigned char* data, int width, int height, int depth,
            int x, int y, int z, int rowPitch, int slicePitch);

         //! Bind the pipeline
         void BindPipeline(ShaderHandle shader);

         void UnBindPipeline();

         //! slide ubo offset
         void IncrementConstantBuffersOffset(ShaderHandle shanderHandle);

         //! Draw
         void DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance);

         //! Draw line strip
         void DrawLineStrip(VertexBufferHandle vbh, const Color& c, int nVerts, float width);

         //! Draw points
         void DrawPoints(double pointSize, int nPoints, int start);

         //! Enable the texture
         void EnableTexture(TextureHandle textureHandle, int bindingPoint);

         //! Disable the texture
         void DisableTexture(int bindingPoint);
         
         void Bind(RawBuffer* buffer, int set, int binding);

         //! Get pointer to the texture descriptor set manager
         TextureDescriptorSetManager* GetTextureDescriptorSetManager(void);

         //! Get pointer to the buffer descriptor set manager
         BufferDescriptorSetManager* GetBufferDescriptorSetManager(void);

         //! Get sampler manager
         SamplerManager* GetSamplerManager(void);

         //! Set the current command buffer
         void SetCurrentCommandBuffer(VkCommandBuffer commandBuffer, int frameIndex);

         //! model view matrix
         void SetModelviewMatrix(const SilverLining::Matrix4& mat);

         //! projection matrix
         void SetProjectionMatrix(const SilverLining::Matrix4& mat);

         //! set vertex buffer
         void SetVertexBuffer(VertexBufferHandle vbh);

         //! offsetable vertex buffers.
         //! Similar to mapped ubos/ssbos that can be changed/bound at a given offset.
         void IncrementOffsetIndex(VertexBufferHandle vbh);

         //! vertex buffer pool
         Buffer* GetAVertexBuffer(int numVertices, const std::string& name, const BufferProperties& bufferProperties);
         void ReturnAVertexBuffer(Buffer* vertexBuffer);

         //! index buffer pool
         Buffer* GetAnIndexBuffer(int nIndices, const std::string& name, const BufferProperties& bufferProperties);
         void ReturnAnIndexBuffer(Buffer* indexBuffer);

         RawBuffer* CreateBuffer(BufferType type, VkDeviceSize sizeInBytes, const std::string& name);
         void DestroyBuffer(RawBuffer* buffer);

         //! rtt (texture)
         RenderToTexture* CreateRenderToTexture(int width, int height, const std::string& name);
         void MakeCurrent(RenderToTexture* rtt);
         void Bind(RenderToTexture* rtt);
         void Release(RenderToTexture* rtt);
         Texture* GetTexture(RenderToTexture* rtt);
         VkImage GetImage(RenderToTexture* rtt) const;

         //! rtt (texture cube)
         CubeMapRenderTexture* CreateRenderToTextureCube(int width, int height, bool floatingPoint, bool generateMipMaps, const std::string& name);
         void MakeCurrent(CubeMapRenderTexture* rtt, CubeFace face);
         void Bind(CubeMapRenderTexture* rtt);
         void Release(CubeMapRenderTexture* rtt);
         VkImage GetImage(CubeMapRenderTexture* rtt) const;

         void BackfaceCullClockwise(bool cullCW);

         void EnableBlending(BlendFactor srcFactor, BlendFactor dstFactor);

         void DisableBlending(void);

         void SetConstantVector4AtLocation(SilverLining::ShaderHandle shader, int loc, const float* data);
         void SetConstantMatrix4AtLocation(SilverLining::ShaderHandle shader, int loc, float* data);

         VkFormat GetCurrentColorFormat(void) const;

         void ExecuteOneTimeOps(void);

         void* GetMappedData(void* bh);
         bool UnlockBuffer(void* bh);

         const VulkanInitInfo& GetInfo(void) const;

         int GetCurrentFrameIndex(void) const;

         void SettingsChanged(const VulkanInitInfo& newInfo);

         const BlendState& GetDefaultBlendState(void) const;

         FrontFace GetDefaultFrontFace(void) const;
      public:
         Instance* instance = nullptr;
         PhysicalDevice* physicalDevice = nullptr;
         Device* device = nullptr;
         VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      protected:
         //! internal
         //! Try to create shader from the spv file, if it exists
         //! If not, try to create it from the glsl file, if it exists
         Shader* LoadShader(const std::string& shaderFileName, const std::string& shaderFileNameSpv, VkShaderStageFlagBits shaderType);

         //! compile a shader and write it to a spv file
         bool CompileShader(const std::string& shaderFileName, const std::string& shaderFileNameSpv);

         //! load the shader spv file
         Shader* LoadShaderSpv(const std::string& shaderFileNameSpv, VkShaderStageFlagBits shaderType);

         //! create shader reflection from an spv file
         ShaderReflection* LoadShaderReflection(const std::string& shaderFileName, const std::string& shaderFileNameSpv);

#if SL_SHADER_DEV
         //! Do shader reflection using spirv-cross
         bool ReflectShader(const std::string& shaderFileNameSpv, const std::string& shaderFileNameJson);

         bool CanUseCacheFile(const std::string& shaderFileName, const std::string& shaderFileNameSpv, bool hasGlslFile) const;
#endif

         //! line shader
         Pipeline* GetLineShader(void);

         //! internal
         void destroyBuffersAndClearPools(void);

         //! internal
         std::ostream& log(LogLevel level) const;

         //! winding order state
         void SetFlipWindingOrder(bool flip);
         bool GetFlipWindingOrder() const;

         void ExecuteSubLoadOps(void);
         void ExecuteSubLoadOp(Texture* texture, const unsigned char* pSrcData
             , int width, int height, int depth
             , int x, int y, int z);

      protected:
         ResourceLoader* resourceLoader = nullptr;

         SL_SET(Pipeline*) pipelines;

         Pipeline* linePipeline = nullptr;
         bool linePipelineLoadAttempted = false;
         int sl_modelViewProjLocation = -1;
         int sl_colorConstantLocation = -1;

         VulkanInitInfo info;

         SL_SET(Texture*) textures;
         SL_SET(Image*) images;

         SL_ORDERED_MAP(int, Texture*) enabledTextures;

         TextureDescriptorSetManager* textureDescriptorSetManager = nullptr;

         BufferDescriptorSetManager* bufferDescriptorSetManager = nullptr;

         SamplerManager* samplerManager = nullptr;

         SilverLining::Matrix4 modelViewMatrix;
         SilverLining::Matrix4 projectionMatrix;

         SL_MAP(Image*, SubLoadOperation*) mapImagesToSubLoadInformation;

         BufferPool* vertexBuffersPool = nullptr;
         BufferPool* vertexBuffersPoolDynamic = nullptr;
         
         BufferPool* indexBuffersPool = nullptr;
         BufferPool* indexBuffersPoolDynamic = nullptr;

         SL_SET(RenderToTexture*) renderToTextures;

         SL_SET(CubeMapRenderTexture*) cubeMapRenderToTextures;

         bool flipWinding = false;

         Pipeline* currentPipeline = nullptr;

         const uint32_t numBufferedFrames = 3;

         SilverLining::BlendState currentBlendState;

         int currentFrameIndex = -1;

         SL_MAP(VkCommandBuffer, int) mapCommandBufferToFrameIndex;

         VkFormat currentColorFormat;

         struct SubLoad3DTextureLAOp
         {
             Texture* texture = nullptr;
             const unsigned char* pSrcData = nullptr;
             int width = 0;
             int height = 0;
             int depth = 0;
             int x = 0;
             int y = 0;
             int z = 0;
         };

         SL_VECTOR(SubLoad3DTextureLAOp) subloadOps;

         FrameIndexedBufferManager* frameIndexedBufferManager = nullptr;

         const BlendState defaultBlendState;
         const FrontFace defaultFrontFace = SL_FRONT_FACE_COUNTER_CLOCKWISE;
         FrontFace currentFrontFace = SL_FRONT_FACE_COUNTER_CLOCKWISE;
      };
   }
}
