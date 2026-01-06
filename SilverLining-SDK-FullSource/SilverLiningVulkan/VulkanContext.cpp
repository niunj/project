// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.
#include "SLAssert.h"
#include "VulkanContext.h"
#include "VulkanInitInfo.h"
#include "Instance.h"
#include "PhysicalDevice.h"
#include "Device.h"
#include "VulkanBuffer.h"
#include "ResourceLoader.h"
#include "Shader.h"
#include "ShaderReflection.h"
#include "Pipeline.h"
#include "TGALoader.h"
#include "Texture.h"
#include "Image.h"
#include "RenderToTexture.h"
#include "CubeMapRenderTexture.h"
#include "TextureDescriptorSetManager.h"
#include "BufferDescriptorSetManager.h"
#include "SamplerManager.h"
#include "VulkanDebug.h"
#include "SubLoadOperation.h"
#include "Vertex.h"
#include "MemoryStats.h"
#include "BlendUtils.h"
#include "BlendState.h"
#include "BufferPool.h"
#include "FrameIndexedBufferManager.h"
#include "FrameIndexedBuffer.h"

#include <algorithm>
#include <iostream>


#if _HAS_CXX23
#define DEBUG_COMMAND_RECORDING 1
#if DEBUG_COMMAND_RECORDING
#include <stacktrace>
#include <sstream>

static const bool IsRecording(void)
{
    std::stringstream ss;
    ss << std::stacktrace::current();
    if (ss.str().find("Atmosphere::DrawSky") != std::string::npos) {
        //std::cout << ss.str() << std::endl;
        return true;
    }
    if (ss.str().find("Atmosphere::DrawObjects") != std::string::npos) {
        //std::cout << ss.str() << std::endl;
        return true;
    }
    return false;
}
#endif
#endif

namespace SilverLining
{
namespace Vulkan
{
VulkanContext::VulkanContext(const VulkanInitInfo& _info, ResourceLoader* _resourceLoader)
    : resourceLoader(_resourceLoader)
    , info(_info)
    , numBufferedFrames(info.numBufferedFrames)
{
    instance = new Instance(info.instance);
    physicalDevice = new PhysicalDevice(instance, info.physicalDevice);

    device = new Device(physicalDevice, info.device, info.graphicsQueue, info.graphicsQueueFamilyIndex, info.vulkanMemoryAllocator);

    textureDescriptorSetManager = new TextureDescriptorSetManager(device);

    bufferDescriptorSetManager = new BufferDescriptorSetManager(device);

    samplerManager = new SamplerManager(device);

    currentColorFormat = info.colorFormat;

    vertexBuffersPool = new BufferPool(device, "vertexBuffersPool");
    vertexBuffersPoolDynamic = new BufferPool(device, "vertexBuffersPoolDynamic");

    indexBuffersPool = new BufferPool(device, "indexBuffersPool");
    indexBuffersPoolDynamic = new BufferPool(device, "indexBuffersPoolDynamic");

    frameIndexedBufferManager = new FrameIndexedBufferManager(info.numBufferedFrames, device);
}

VulkanContext::~VulkanContext()
{
    delete frameIndexedBufferManager;

    for (auto& keyVal : mapImagesToSubLoadInformation) {
        delete keyVal.second;
    }

    for (auto pipeline : pipelines) {
        delete pipeline;
    }

    for (auto renderToTexture : renderToTextures) {
        delete renderToTexture;
    }

    for (auto cubeMapRtt : cubeMapRenderToTextures) {
        delete cubeMapRtt;
    }

    for (auto texture : textures) {
        delete texture;
    }
    for (auto image : images) {
        delete image;
    }

    destroyBuffersAndClearPools();

    delete textureDescriptorSetManager;

    delete bufferDescriptorSetManager;

    delete samplerManager;

    delete device;
    delete physicalDevice;
    delete instance;
}

std::string MakeFilename(const char* cgFileName, bool fragFile, bool spv)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return "";

    int extraChars = fragFile ? 9 : 4;
    std::string glslFileName(cgFileName);
    glslFileName = glslFileName.substr(0, glslFileName.length() - 3);

    if (fragFile) {
        glslFileName += ".frag";
    } else {
        glslFileName += ".vert";
    }

    if (spv) {
        glslFileName += ".spv";
    }

    return glslFileName;
}

static std::string CommonFileName(const char* cgFileName)
{
    // Strip the .cg extension and substitute our own...
    int len = (int)strlen(cgFileName);
    if (len < 3) return "";

    struct LocalChar {
        public:
        LocalChar(int size)
        {
            data = SL_NEW char[size];
        }
        ~LocalChar()
        {
            SL_DELETE[] data;
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

Shader* VulkanContext::LoadShaderSpv(const std::string& shaderFileNameSpv, VkShaderStageFlagBits shaderType)
{
    char* charSource = nullptr;
    unsigned int sourceLength = 0;
    const bool loaded = resourceLoader->LoadResource(shaderFileNameSpv.c_str(), charSource, sourceLength, false);
    if (loaded) {
        std::vector<uint8_t> source;
        uint8_t* pData = (uint8_t*)charSource;
        source.insert(source.end(), &pData[0], &pData[sourceLength]);

        Shader* shader = new Shader(device);
        shader->LoadFromSource(source, shaderType);
        if (shader->Valid()) {
            return shader;
        } else {
            delete shader;
            return nullptr;
        }
    } else {
        return nullptr;
    }
}

ShaderReflection* VulkanContext::LoadShaderReflection(const std::string& shaderFileNameSpv, const std::string& shaderFileNameJson)
{
    char* charSource = nullptr;
    unsigned int sourceLength = 0;
    bool loaded = resourceLoader->LoadResource(shaderFileNameJson.c_str(), charSource, sourceLength, true);

#if SL_SHADER_DEV
    if (loaded == false) {
        bool ok = ReflectShader(shaderFileNameSpv, shaderFileNameJson);
        if (!ok) {
            return nullptr;
        }
    }

    loaded = resourceLoader->LoadResource(shaderFileNameJson.c_str(), charSource, sourceLength, true);
    if (loaded == false) {
        return nullptr;
    }
#endif

    const std::string strJson(charSource, charSource + sourceLength);

    ShaderReflection* shaderReflection = new ShaderReflection();
    shaderReflection->LoadFromSource(strJson, device->log(LogLevel::SL_LOG_LEVEL_WARN));
    if (shaderReflection->Valid() == false) {
        delete shaderReflection;
        return nullptr;
    }

    return shaderReflection;
}

#if SL_SHADER_DEV
bool VulkanContext::ReflectShader(const std::string& shaderFileNameSpv, const std::string& shaderFileNameReflectionJson)
{
#if _WIN32
    const std::string strVulkanDir = getenv("VULKAN_SDK");
    const std::string sprirv_cross = strVulkanDir + "\\bin\\spirv-cross.exe";
#elif defined(__linux__)
//TODO: Figure out the right path here
    const std::string sprirv_cross = "/usr/bin/spirv-cross";
#endif

    std::string pathToShaders = resourceLoader->GetResourceDirPath();
    std::string pathToShaderSpV = pathToShaders + "/" + std::string(shaderFileNameSpv);
    std::string pathToShaderReflectionJson = pathToShaders + "/" + std::string(shaderFileNameReflectionJson);

    std::string command = sprirv_cross + " " + pathToShaderSpV + " --reflect --output " + pathToShaderReflectionJson;
    system(command.c_str());

    return true;
}
#endif

bool VulkanContext::CompileShader(const std::string& shaderFileName, const std::string& shaderFileNameSpv)
{
    char* vsSource = nullptr;
    unsigned int vsLength = 0;
    const bool vertexShaderLoaded = resourceLoader->LoadResource(shaderFileName.c_str(), vsSource, vsLength, true);
    if (!vertexShaderLoaded) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "could not load shader file: " << std::string(shaderFileName) << std::endl;
        return false;
    }

#if _WIN32
    const std::string strVulkanDir = getenv("VULKAN_SDK");
    const std::string glslc = strVulkanDir + "\\bin\\glslc.exe";
#elif defined(__linux__)
//TODO: Get correct path
    const std::string glslc = "/usr/bin/glslc";
#endif

    std::string pathToShaders = resourceLoader->GetResourceDirPath();
    std::string pathToShader = pathToShaders + "/" + std::string(shaderFileName);
    std::string pathToShaderSpV = pathToShaders + "/" + std::string(shaderFileNameSpv);
    std::string command = glslc + " " + "-o " + pathToShaderSpV + " " + pathToShader;
    system(command.c_str());

    return true;
}

static std::string makeGlslFileName(const std::string& shaderFileName)
{
    std::string glslFileName = shaderFileName;
    auto it = glslFileName.find(".vert");
    if (it != std::string::npos) {
        glslFileName.replace(it, glslFileName.length(), ".glsl15");
        return glslFileName;
    }

    it = glslFileName.find(".frag");
    if (it != std::string::npos) {
        glslFileName.replace(it, glslFileName.length(), "-frag.glsl15");
        return glslFileName;
    }
    return "";
}

#if SL_SHADER_DEV
bool VulkanContext::CanUseCacheFile(const std::string& shaderFileName, const std::string& shaderFileNameSpv, bool hasGlslFile) const
{
    time_t timeStampShader = 0;
    bool shaderOK = resourceLoader->ResourceTimeStamp(shaderFileName.c_str(), timeStampShader, resourceLoader->GetResourceDirPath());

    time_t timeStampShaderSpv = 0;
    bool shaderSpvOK = resourceLoader->ResourceTimeStamp(shaderFileNameSpv.c_str(), timeStampShaderSpv, resourceLoader->GetResourceDirPath());

    if (hasGlslFile) {
        time_t timeStampGlslShader = 0;
        std::string glslFileName = makeGlslFileName(shaderFileName);
        bool shaderGlslOK = resourceLoader->ResourceTimeStamp(glslFileName.c_str(), timeStampGlslShader, resourceLoader->GetResourceDirPath());

        return shaderOK && shaderSpvOK && shaderGlslOK && timeStampShaderSpv > timeStampShader && timeStampShaderSpv > timeStampGlslShader;
    } else {
        return shaderOK && shaderSpvOK  && timeStampShaderSpv > timeStampShader;
    }
}
#endif

Shader* VulkanContext::LoadShader(const std::string& shaderFileName, const std::string& shaderFileNameSpv, VkShaderStageFlagBits shaderType)
{
#if SL_SHADER_DEV
    if (CanUseCacheFile(shaderFileName, shaderFileNameSpv, shaderFileName.find("Line")==std::string::npos)) {
        Shader* shader = LoadShaderSpv(shaderFileNameSpv, shaderType);
        if (shader) {
            return shader;
        }
    }

    bool ok = CompileShader(shaderFileName, shaderFileNameSpv);
    if (ok) {
        Shader* shader = LoadShaderSpv(shaderFileNameSpv, shaderType);
        return shader;
    }

    return nullptr;
#else
    Shader* shader = LoadShaderSpv(shaderFileNameSpv, shaderType);
    return shader;
#endif
}

ShaderHandle VulkanContext::LoadShaderFromFile(const char* fileName, const char* userVertShader, const char* userFragShader
        , bool enableObjectLabeling
        , const SL_VECTOR(SL_STRING)& defines)
{
    Shader* vertexShader = nullptr;
    Shader* fragmentShader = nullptr;

    ShaderReflection* vsReflection = nullptr;
    ShaderReflection* psReflection = nullptr;

    auto cleanup = [&]() {
        delete vertexShader;
        delete fragmentShader;
        delete vsReflection;
        delete psReflection;
    };

    // We automatically look for a companion -frag.glsl15 source file too, since GLSL doesn't have effect
    // files to keep it neat...
    std::string vertexShaderFileName = MakeFilename(fileName, false, false);
    std::string fragmentShaderFileName = MakeFilename(fileName, true, false);

    std::string vertexShaderFileNameSpv = MakeFilename(fileName, false, true);
    std::string fragmentShaderFileNameSpv = MakeFilename(fileName, true, true);

    std::string vertexShaderFileNameJson = vertexShaderFileNameSpv + ".json";
    std::string fragmentShaderFileNameJson = fragmentShaderFileNameSpv + ".json";

    char* vsSpvSource = nullptr;
    unsigned int vsSpvLength = 0;

    vertexShader = LoadShader(vertexShaderFileName, vertexShaderFileNameSpv, VK_SHADER_STAGE_VERTEX_BIT);
    if (vertexShader == nullptr) {
        cleanup();
        return nullptr;
    }

    fragmentShader = LoadShader(fragmentShaderFileName, fragmentShaderFileNameSpv, VK_SHADER_STAGE_FRAGMENT_BIT);
    if (fragmentShader == nullptr) {
        cleanup();
        return nullptr;
    }

    vsReflection = LoadShaderReflection(vertexShaderFileNameSpv, vertexShaderFileNameJson);
    if (vsReflection == nullptr) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "Could not load vs reflection" << std::endl;
        cleanup();
        return nullptr;
    }

    psReflection = LoadShaderReflection(fragmentShaderFileNameSpv, fragmentShaderFileNameJson);
    if (psReflection == nullptr) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "Could not load ps reflection" << std::endl;
        cleanup();
        return nullptr;
    }

    const bool dynamicUbo = vertexShaderFileName.find("Billboard") != std::string::npos;
    const bool dynamicSbo = vertexShaderFileName.find("Billboard") != std::string::npos
                            || vertexShaderFileName.find("Sky") != std::string::npos
                            || vertexShaderFileName.find("Cirrus") != std::string::npos
                            || vertexShaderFileName.find("Stratiform") != std::string::npos
                            || vertexShaderFileName.find("Stratocumulus") != std::string::npos
                            || vertexShaderFileName.find("Particle") != std::string::npos;

    const int maxSsboCount = vertexShaderFileName.find("Billboard")!= std::string::npos ? 5000 : 1000;
    const int maxUboCount = vertexShaderFileName.find("Billboard") != std::string::npos ? 5000 : 1000;

    std::string pipelineName = vertexShaderFileName;
    auto it = pipelineName.find(".vert");
    if (it != std::string::npos) {
        pipelineName.erase(it, pipelineName.length());
    }

    VkPrimitiveTopology primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (vertexShaderFileName.find("Line") != std::string::npos) {
        primitiveTopology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    } else if (vertexShaderFileName.find("Stars") != std::string::npos) {
        primitiveTopology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }

    Pipeline* pipeline = new Pipeline(this, dynamicUbo, maxUboCount, dynamicSbo, maxSsboCount
                                      , primitiveTopology
                                      , numBufferedFrames
                                      , pipelineName
                                     );
    pipeline->Add(vertexShader, vsReflection);
    pipeline->Add(fragmentShader, psReflection);
    pipeline->Build();

    pipelines.insert(pipeline);

    return pipeline;
}

Texture* VulkanContext::LoadTexture(const unsigned char* data, int width, int height, bool repeatU, bool repeatV, const std::string& name)
{
    unsigned char* buf = SL_NEW unsigned char[width * height * 4];
    unsigned char* src = (unsigned char*)data;
    unsigned char* dst = (unsigned char*)buf;

    for (int texel = 0; texel < width * height; texel++) {
        unsigned char L = *src++;
        unsigned char A = *src++;
        *dst++ = L;
        *dst++ = L;
        *dst++ = L;
        *dst++ = A;
    }

    Image* image = new Image(device);
    SL_VECTOR(int) dataOffets = { 0 };
    const VkDeviceSize sizeInBytes = width * height * 4;
    bool ok = image->LoadFromBuffer(buf
                                    , sizeInBytes
                                    , VK_FORMAT_R8G8B8A8_UNORM
                                    , width, height
                                    , { 0 }
                                    , true);

    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "could not make the image" << std::endl;
        delete image;
        return nullptr;
    }

    images.insert(image);

    VkSamplerAddressMode addressModeU = (repeatU) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeV = (repeatV) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeWNotUsed = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM; // don't care

    Texture* texture = new Texture(image, VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , addressModeU, addressModeV, addressModeWNotUsed, Texture::DefaultComponentMapping(), samplerManager);

    textures.insert(texture);

    return texture;
}

Texture* VulkanContext::LoadFloatTextureRGB(const float* _data, int width, int height, const std::string& name)
{
    float* data = new float[width * height * 4];

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int offsetInNew = ( width * y + x) * 4;
            int offsetInOld = ( width * y + x) * 3;

            data[offsetInNew] = _data[offsetInOld + 0];
            data[offsetInNew + 1] = _data[offsetInOld + 1];
            data[offsetInNew + 2] = _data[offsetInOld + 2];
            data[offsetInNew + 3] = 0;
        }
    }

    Image* image = new Image(device);
    SL_VECTOR(int) dataOffets = { 0 };
    const VkDeviceSize sizeInBytes = width * height * 4 * sizeof(float);
    bool ok = image->LoadFromBuffer((void*)data
                                    , sizeInBytes
                                    , VK_FORMAT_R32G32B32A32_SFLOAT
                                    , width, height
                                    , { 0 }
                                    , false
                                   );

    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << ": " << "could not make the image" << std::endl;
        delete image;
        return nullptr;
    }

    images.insert(image);

    VkSamplerAddressMode addressModeWNotUsed = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM; // don't care

    Texture* texture = new Texture(image, VK_FILTER_NEAREST, VK_FILTER_NEAREST
                                   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, addressModeWNotUsed
                                   , Texture::DefaultComponentMapping(), samplerManager);

    textures.insert(texture);

    return texture;
}

Texture* VulkanContext::Load3DTextureRGB(const unsigned char* _data, int width, int height, int depth
        , bool repeatU, bool repeatV, bool repeatR, const std::string& name)
{
    unsigned char* data = new std::uint8_t[width * height * depth * 4];
    for (int z = 0; z < depth; ++z) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int offsetInNew = ((width * height) * z + width * y + x) * 4;
                int offsetInOld = ((width * height) * z + width * y + x) * 3;

                data[offsetInNew] = _data[offsetInOld + 0];
                data[offsetInNew + 1] = _data[offsetInOld + 1];
                data[offsetInNew + 2] = _data[offsetInOld + 2];
                data[offsetInNew + 3] = 0;
            }
        }
    }

    Image* image = new Image(device);
    SL_VECTOR(int) dataOffets = { 0 };
    const VkDeviceSize sizeInBytes = width * height * depth * 4 * sizeof(std::uint8_t);
    bool ok = image->LoadFromBuffer((void*)data
                                    , sizeInBytes
                                    , VK_FORMAT_R8G8B8A8_UNORM
                                    , width, height, depth
                                    , { 0 }
                                    , false
                                   );

    delete[] data;

    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << ": " << "could not make the image" << std::endl;
        delete image;
        return nullptr;
    }

    images.insert(image);

    VkSamplerAddressMode addressModeU = (repeatU) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeV = (repeatV) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeW = (repeatR) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    Texture* texture = new Texture(image, VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , addressModeU, addressModeV, addressModeW
                                   , Texture::DefaultComponentMapping(), samplerManager);

    textures.insert(texture);

    return texture;

    return nullptr;
}

Texture* VulkanContext::Load3DTextureLA(const unsigned char* _data, int width, int height, int depth
                                        , bool repeatU, bool repeatV, bool repeatR, const std::string& name)
{
    Image* image = new Image(device);
    SL_VECTOR(int) dataOffets = { 0 };
    const VkDeviceSize sizeInBytes = width * height * depth * 2 * sizeof(std::uint8_t);
    bool ok = image->LoadFromBuffer((void*)_data
                                    , sizeInBytes
                                    , VK_FORMAT_R8G8_UNORM
                                    , width, height, depth
                                    , { 0 }
                                    , false
                                   );

    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << ": " << "could not make the image" << std::endl;
        delete image;
        return nullptr;
    }

    images.insert(image);

    VkSamplerAddressMode addressModeU = (repeatU) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeV = (repeatV) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeW = (repeatR) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkComponentMapping componentMappping = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G };

    Texture* texture = new Texture(image, VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , addressModeU, addressModeV, addressModeW
                                   , componentMappping, samplerManager);

    textures.insert(texture);

    return texture;
}

void VulkanContext::SubLoad3DTextureLA(Texture* texture, const unsigned char* pSrcData, int width, int height, int depth,
                                       int x, int y, int z, int rowPitch, int slicePitch)
{
    if (textures.find(texture) == textures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "textures.find(texture) == textures.end()" << std::endl;
        return;
    }

    SubLoad3DTextureLAOp op;
    op.texture = texture;
    op.pSrcData = pSrcData;
    op.width = width;
    op.height = height;
    op.depth = depth;
    op.x = x;
    op.y = y;
    op.z = z;

    subloadOps.push_back(op);
}

void VulkanContext::ExecuteSubLoadOp(Texture* texture, const unsigned char* pSrcData
                                     , int width, int height, int depth
                                     , int x, int y, int z)
{
    Image* image = const_cast<Image*>(texture->GetImage());

    auto it = mapImagesToSubLoadInformation.find(image);
    if (it == mapImagesToSubLoadInformation.end()) {
        SubLoadOperation* info = new SubLoadOperation(image);
        it = mapImagesToSubLoadInformation.insert({ image, info }).first;
    }

    SubLoadOperation* subloadInformation = it->second;
    subloadInformation->SubLoad(pSrcData, width, height, depth
                                , x, y, z);
}

void VulkanContext::ExecuteSubLoadOps(void)
{
    for (const auto& subloadOp : subloadOps) {
        ExecuteSubLoadOp(subloadOp.texture, subloadOp.pSrcData
                         , subloadOp.width, subloadOp.height, subloadOp.depth
                         , subloadOp.x, subloadOp.y, subloadOp.z);
    }
    subloadOps.clear();
}

void VulkanContext::ExecuteOneTimeOps(void)
{
    ExecuteSubLoadOps();
}

Texture* VulkanContext::LoadTextureFromFile(const std::string& imageFileName, bool repeatU, bool repeatV, const std::string& name)
{
    TGALoader tgaLoader;
    bool ok = tgaLoader.Load(imageFileName.c_str(), resourceLoader);
    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "could not load: " << imageFileName << std::endl;
        return nullptr;
    }

    if (tgaLoader.GetBytesPerPixel() == 3) {
        tgaLoader.ConvertToRGBA();
    }

    VkFormat format = VK_FORMAT_UNDEFINED;
    if (tgaLoader.GetBytesPerPixel() == 4) {
        format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    if (format == VK_FORMAT_UNDEFINED) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "unknown image format: " << imageFileName << std::endl;
        return nullptr;
    }

    Image* image = new Image(device);
    SL_VECTOR(int) dataOffets = { 0 };
    const VkDeviceSize sizeInBytes = tgaLoader.GetWidth() * tgaLoader.GetHeight() * tgaLoader.GetBytesPerPixel();
    ok = image->LoadFromBuffer(tgaLoader.GetPixels()
                               , sizeInBytes
                               , format
                               , tgaLoader.GetWidth(), tgaLoader.GetHeight()
                               , { 0 }
                               , true
                              );
    if (ok == false) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "could not make an image: " << imageFileName << std::endl;
        delete image;
        return nullptr;
    }

    image->SetName(name);

    images.insert(image);

    VkSamplerAddressMode addressModeU = (repeatU) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeV = (repeatV) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressModeWNotUsed = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM; // don't care

    Texture* texture = new Texture(image, VK_FILTER_LINEAR, VK_FILTER_LINEAR
                                   , addressModeU, addressModeV, addressModeWNotUsed
                                   , Texture::DefaultComponentMapping(), samplerManager);

    textures.insert(texture);

    return texture;
}

void VulkanContext::BindPipeline(ShaderHandle shaderHandle)
{
    if (shaderHandle == nullptr) {
        return;
    }
    Pipeline* pipeline = static_cast<Pipeline*>(shaderHandle);
    pipeline->Bind(commandBuffer, currentFrameIndex, enabledTextures, currentBlendState, currentFrontFace);
    currentPipeline = pipeline;
}

void VulkanContext::UnBindPipeline()
{
    currentPipeline = nullptr;
}

void VulkanContext::IncrementConstantBuffersOffset(ShaderHandle shaderHandle)
{
    if (shaderHandle == nullptr) {
        return;
    }
    Pipeline* pipeline = static_cast<Pipeline*>(shaderHandle);
    pipeline->IncrementConstantBuffersOffset(commandBuffer, currentFrameIndex);
}

void VulkanContext::DrawStrip(IndexBufferHandle ibh, int startIdx, int nIndices, int nVerts, int instanceCount, int firstInstance)
{
    vkCmdDrawIndexed(commandBuffer, nIndices, instanceCount, startIdx, 0, firstInstance);
}
void VulkanContext::EnableTexture(TextureHandle textureHandle, int bindingPoint)
{
    Texture* texture = static_cast<Texture*>(textureHandle);
    auto texturesIter = textures.find(texture);
    if (texturesIter != textures.end()) {
        enabledTextures[bindingPoint] = texture;
        return;
    }

    if (currentFrameIndex >= 0 && currentFrameIndex < numBufferedFrames) {
        for (auto rtt : renderToTextures) {
            if (rtt->GetTexture(currentFrameIndex) == texture) {
                enabledTextures[bindingPoint] = rtt->GetTexture(currentFrameIndex);
                return;
            }
        }

        // for finding (and enabling) crepuscular texture correctly
        VkImage image = static_cast<VkImage>(textureHandle);
        for (auto rtt : renderToTextures) {
            if (rtt->GetTexture(currentFrameIndex)->GetImage()->VulkanImage() == image) {
                enabledTextures[bindingPoint] = rtt->GetTexture(currentFrameIndex);
                return;
            }
        }
    }

    // just set the texture. can happen when textures are shared
    enabledTextures[bindingPoint] = texture;

    //log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << ": " << "textures.find(texture) == textures.end()" << std::endl;
    //log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << ": " << "renderToTextures.find(texture) == renderToTextures.end()" << std::endl;
}

void VulkanContext::DisableTexture(int bindingPoint)
{
    enabledTextures.erase(bindingPoint);
}

void VulkanContext::Bind(RawBuffer* buffer, int set, int binding)
{
    if (buffer && currentPipeline) {
        currentPipeline->Bind(commandBuffer, buffer, set, binding);
    }
}

TextureDescriptorSetManager* VulkanContext::GetTextureDescriptorSetManager(void)
{
    return textureDescriptorSetManager;
}

BufferDescriptorSetManager* VulkanContext::GetBufferDescriptorSetManager(void)
{
    return bufferDescriptorSetManager;
}

SamplerManager* VulkanContext::GetSamplerManager(void)
{
    return samplerManager;
}

void VulkanContext::SetCurrentCommandBuffer(VkCommandBuffer _commandBuffer, int frameIndex)
{
    commandBuffer = _commandBuffer;
    for (auto pipeline : pipelines) {
        pipeline->SetCurrentCommandBuffer(_commandBuffer);
    }

    frameIndexedBufferManager->ResetOffsetIndex();

    if (frameIndex != -1) {
        currentFrameIndex = frameIndex;
    } else {
        auto it = mapCommandBufferToFrameIndex.find(_commandBuffer);
        if (it != mapCommandBufferToFrameIndex.end()) {
            currentFrameIndex = it->second;
        } else {
            uint32_t computedFrameIndex = (uint32_t)mapCommandBufferToFrameIndex.size();
            SL_ASSERT(computedFrameIndex < numBufferedFrames);

            mapCommandBufferToFrameIndex.insert({ _commandBuffer, computedFrameIndex });
            SL_ASSERT(mapCommandBufferToFrameIndex.size() <= numBufferedFrames);

            currentFrameIndex = computedFrameIndex;
        }
    }
}

Pipeline* VulkanContext::GetLineShader()
{
    if (linePipelineLoadAttempted == false) {
        linePipeline = static_cast<Pipeline*>(LoadShaderFromFile("Shaders/Line.cg", nullptr, nullptr, false, SL_VECTOR(SL_STRING)()));
        linePipelineLoadAttempted = true;

        sl_modelViewProjLocation = linePipeline->GetVariableLocation("sl_modelViewProj");
        sl_colorConstantLocation = linePipeline->GetVariableLocation("sl_colorConstant");
    }
    return linePipeline;
}


static void ToFloatMatrix(float dst[16], const Matrix4& src)
{
    int i = 0;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            dst[i++] = (float)(src.elem[row][col]);
        }
    }
}

void VulkanContext::DrawLineStrip(VertexBufferHandle vbh, const Color& c, int nVerts, float width)
{
    Pipeline* pipeline = GetLineShader();
    if (pipeline == nullptr) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << __FUNCTION__ << "pipeline == nullptr" << std::endl;
        return;
    }

    pipeline->Bind(commandBuffer, currentFrameIndex, {}, {}, defaultFrontFace);
    if (sl_modelViewProjLocation != -1) {
        float mat[16];
        ToFloatMatrix(mat, projectionMatrix* modelViewMatrix);
        pipeline->SetConstantMatrix4AtLocation(currentFrameIndex, sl_modelViewProjLocation, mat);
    }
    if (sl_colorConstantLocation != -1) {
        float vec[4];
        vec[0] = c.r;
        vec[1] = c.g;
        vec[2] = c.b;
        vec[3] = c.a;
        pipeline->SetConstantVector4AtLocation(currentFrameIndex, sl_colorConstantLocation, vec);
    }

    SetVertexBuffer(vbh);

    //device->Extensions().vkCmdSetLineRasterizationModeEXT(commandBuffer, VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT);
    vkCmdSetLineWidth(commandBuffer,width*0.5f); // PPP: 0.5??
    vkCmdDraw(commandBuffer, nVerts, 1, 0, 0);
}

void VulkanContext::DrawPoints(double pointSize, int nPoints, int start)
{
    // the bound shader(pipeline) is the one with primitive points
    vkCmdDraw(commandBuffer, nPoints, 1, start, 0);
}


void VulkanContext::SetModelviewMatrix(const SilverLining::Matrix4& mat)
{
    modelViewMatrix = mat;
}

void VulkanContext::SetProjectionMatrix(const SilverLining::Matrix4& mat)
{
    projectionMatrix = mat;
}

void VulkanContext::SetVertexBuffer(VertexBufferHandle vbh)
{
    SilverLining::Vulkan::Buffer* vertexBuffer = static_cast<SilverLining::Vulkan::Buffer*>(vbh);
    VkDeviceSize offsets[1] = { 0 };
    if (vertexBuffer->GetUserData()) {
        FrameIndexedBuffer* frameIndexedBuffer = static_cast<FrameIndexedBuffer*>(vertexBuffer->GetUserData());
        if (frameIndexedBuffer) {
            SL_ASSERT(currentFrameIndex < frameIndexedBuffer->GetBuffers().size());
            vertexBuffer = frameIndexedBuffer->GetBuffers()[currentFrameIndex];
            if (frameIndexedBuffer->OffsetSizeInBytes() > 0) {
                offsets[0] = frameIndexedBuffer->CurrentOffsetInBytes();
            }
        }
    }
    VkBuffer buffer = vertexBuffer->VulkanBuffer();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffer, offsets);
}

void VulkanContext::IncrementOffsetIndex(VertexBufferHandle vbh)
{
    SilverLining::Vulkan::Buffer* vertexBuffer = static_cast<SilverLining::Vulkan::Buffer*>(vbh);
    if (vertexBuffer->GetUserData()) {
        FrameIndexedBuffer* frameIndexedBuffer = static_cast<FrameIndexedBuffer*>(vertexBuffer->GetUserData());
        if (frameIndexedBuffer) {
            SL_ASSERT(frameIndexedBuffer->OffsetSizeInBytes() > 0);
            frameIndexedBuffer->IncrementOffsetIndex();
        }
    }
}

std::ostream& VulkanContext::log(LogLevel level) const
{
    return device->log(level);
}

Buffer* VulkanContext::GetAVertexBuffer(int numVertices, const std::string& name, const BufferProperties& bufferProperties)
{
    if (bufferProperties.frameIndexed) {
        return frameIndexedBufferManager->GetAVertexBuffer(numVertices, name, bufferProperties);
    }
    const uint64_t sizeInBytes = numVertices * sizeof(Vertex);

    Buffer* buffer = bufferProperties.dynamic? vertexBuffersPoolDynamic->GetFromPool(sizeInBytes)
                     : vertexBuffersPool->GetFromPool(sizeInBytes);

    if (buffer) {
        return buffer;
    }

    VkBufferUsageFlags additionalFlags = 0;
    buffer = new SilverLining::Vulkan::Buffer(device, BT_VERTEX_BUFFER, sizeInBytes, bufferProperties, additionalFlags, name);
    return buffer;
}

Buffer* VulkanContext::GetAnIndexBuffer(int numIndices, const std::string& name, const BufferProperties& bufferProperties)
{
    const uint64_t sizeInBytes = numIndices * sizeof(Index);

    Buffer* buffer = bufferProperties.dynamic ? indexBuffersPoolDynamic->GetFromPool(sizeInBytes)
                     : indexBuffersPool->GetFromPool(sizeInBytes);

    if (buffer) {
        return buffer;
    }

    VkBufferUsageFlags additionalFlags = 0;
    return new SilverLining::Vulkan::Buffer(device, BT_INDEX_BUFFER, sizeInBytes, bufferProperties, additionalFlags, name);
}

void VulkanContext::ReturnAVertexBuffer(Buffer* vertexBuffer)
{
    if (vertexBuffer->GetUserData()) {
        FrameIndexedBuffer* frameIndexedBuffer = static_cast<FrameIndexedBuffer*>(vertexBuffer->GetUserData());
        if (frameIndexedBuffer) {
            frameIndexedBufferManager->ReturnAVertexBuffer(vertexBuffer);
            return;
        }
    }
    if (vertexBuffer->Dynamic()) {
        vertexBuffersPoolDynamic->Return(vertexBuffer);
    } else {
        vertexBuffersPool->Return(vertexBuffer);
    }
}

void VulkanContext::ReturnAnIndexBuffer(Buffer* indexBuffer)
{
    if (indexBuffer->Dynamic()) {
        indexBuffersPoolDynamic->Return(indexBuffer);
    } else {
        indexBuffersPool->Return(indexBuffer);
    }
}

void VulkanContext::destroyBuffersAndClearPools(void)
{
    delete vertexBuffersPool;
    delete vertexBuffersPoolDynamic;
    delete indexBuffersPool;
    delete indexBuffersPoolDynamic;
}

RawBuffer* VulkanContext::CreateBuffer(BufferType bufferType, VkDeviceSize sizeInBytes, const std::string& name)
{
    VkBufferUsageFlags usageFlags = 0;
    if (bufferType == BT_UBO) {
        usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    } else if (bufferType == BT_SSBO) {
        usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    const VkMemoryPropertyFlags memoryPropertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;// | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return new SilverLining::Vulkan::RawBuffer(device, usageFlags, memoryPropertyFlags, sizeInBytes, name);
}

void VulkanContext::DestroyBuffer(RawBuffer* buffer)
{
    delete buffer;
}

RenderToTexture* VulkanContext::CreateRenderToTexture(int width, int height, const std::string & name)
{
    RenderToTexture* rtt = new RenderToTexture(width, height, info.colorFormat, numBufferedFrames, name, device, samplerManager);
    renderToTextures.insert(rtt);
    return rtt;
}

void VulkanContext::MakeCurrent(RenderToTexture* rtt)
{
    auto it = renderToTextures.find(rtt);
    if (it == renderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }
    rtt->MakeCurrent(commandBuffer, currentFrameIndex);
    currentColorFormat = rtt->GetColorFormat();

}
void VulkanContext::Bind(RenderToTexture* rtt)
{
    auto it = renderToTextures.find(rtt);
    if (it == renderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }

    rtt->Bind(commandBuffer, currentFrameIndex);
    currentColorFormat = info.colorFormat;
}
void VulkanContext::Release(RenderToTexture* rtt)
{
    auto it = renderToTextures.find(rtt);
    if (it == renderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }

    renderToTextures.erase(it);
    delete rtt;
}

Texture* VulkanContext::GetTexture(RenderToTexture* rtt)
{
    auto it = renderToTextures.find(rtt);
    if (it == renderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return nullptr;
    }

    return rtt->GetTexture(currentFrameIndex);
}

VkImage VulkanContext::GetImage(RenderToTexture* rtt) const
{
    auto it = renderToTextures.find(rtt);
    if (it == renderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return nullptr;
    }

    return rtt->GetImage(currentFrameIndex);
}

CubeMapRenderTexture* VulkanContext::CreateRenderToTextureCube(int width, int height, bool floatingPoint, bool generateMipMaps, const std::string& name)
{
    CubeMapRenderTexture* rtt = new CubeMapRenderTexture(width, height, floatingPoint, generateMipMaps, numBufferedFrames, name, device);
    cubeMapRenderToTextures.insert(rtt);
    return rtt;
}
void VulkanContext::MakeCurrent(CubeMapRenderTexture* rtt, CubeFace face)
{
    auto it = cubeMapRenderToTextures.find(rtt);
    if (it == cubeMapRenderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }
    rtt->MakeCurrent(commandBuffer, currentFrameIndex, face);
    // Because our RTT does not have an inverted viewport, we need to switch how backface culling works
    SetFlipWindingOrder(true);
    currentColorFormat = rtt->GetColorFormat();
}
void VulkanContext::Bind(CubeMapRenderTexture* rtt)
{
    auto it = cubeMapRenderToTextures.find(rtt);
    if (it == cubeMapRenderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }

    rtt->Bind(commandBuffer, currentFrameIndex);
    // Restore winding order
    SetFlipWindingOrder(false);
    currentColorFormat = info.colorFormat;
}
void VulkanContext::Release(CubeMapRenderTexture* rtt)
{
    auto it = cubeMapRenderToTextures.find(rtt);
    if (it == cubeMapRenderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return;
    }

    cubeMapRenderToTextures.erase(it);
    delete rtt;
}
VkImage VulkanContext::GetImage(CubeMapRenderTexture* rtt) const
{
    auto it = cubeMapRenderToTextures.find(rtt);
    if (it == cubeMapRenderToTextures.end()) {
        log(LogLevel::SL_LOG_LEVEL_WARN) << "rtt not created here!!" << std::endl;
        return nullptr;
    }

    return rtt->GetImage(currentFrameIndex);
}

void VulkanContext::SetFlipWindingOrder(bool flip)
{
    flipWinding = flip;
}
bool VulkanContext::GetFlipWindingOrder() const
{
    return flipWinding;
}
void VulkanContext::BackfaceCullClockwise(bool cullCW)
{
    bool flip = GetFlipWindingOrder();
    if (flip) cullCW = !cullCW;

    // The cullCW parameter is for the backface winding, not the frontface, which is why the following line looks backwards
    currentFrontFace = cullCW ? SL_FRONT_FACE_COUNTER_CLOCKWISE : SL_FRONT_FACE_CLOCKWISE;
}

void VulkanContext::EnableBlending(BlendFactor srcFactor, BlendFactor dstFactor)
{
    currentBlendState.enabled = true;
    if (srcFactor == SRCALPHA && dstFactor == INVSRCALPHA) {
        currentBlendState.srcColorBlendFactor = SRCALPHA;
        currentBlendState.dstColorBlendFactor = INVSRCALPHA;
        currentBlendState.colorBlendOp = BLEND_OP_ADD;

        currentBlendState.srcAlphaBlendFactor = ONE;
        currentBlendState.dstAlphaBlendFactor = INVSRCALPHA;
        currentBlendState.alphaBlendOp = BLEND_OP_ADD;
    } else {
        currentBlendState.srcColorBlendFactor = srcFactor;
        currentBlendState.dstColorBlendFactor = dstFactor;
        currentBlendState.colorBlendOp = BLEND_OP_ADD;

        currentBlendState.srcAlphaBlendFactor = srcFactor;
        currentBlendState.dstAlphaBlendFactor = dstFactor;
        currentBlendState.alphaBlendOp = BLEND_OP_ADD;
    }
}

void VulkanContext::DisableBlending(void)
{
    currentBlendState.enabled = false;

    currentBlendState.srcColorBlendFactor = ONE;
    currentBlendState.dstColorBlendFactor = ZERO;
    currentBlendState.colorBlendOp = BLEND_OP_ADD;

    currentBlendState.srcAlphaBlendFactor = ONE;
    currentBlendState.dstAlphaBlendFactor = ZERO;
    currentBlendState.alphaBlendOp = BLEND_OP_ADD;
}

void VulkanContext::SetConstantVector4AtLocation(SilverLining::ShaderHandle shader, int loc, const float* data)
{
    Pipeline* pipeline = static_cast<Pipeline*>(shader);
    pipeline->SetConstantVector4AtLocation(currentFrameIndex, loc, data);
}

void VulkanContext::SetConstantMatrix4AtLocation(SilverLining::ShaderHandle shader, int loc, float* data)
{
    Pipeline* pipeline = static_cast<Pipeline*>(shader);
    pipeline->SetConstantMatrix4AtLocation(currentFrameIndex, loc, data);
}

VkFormat VulkanContext::GetCurrentColorFormat(void) const
{
    return currentColorFormat;
}

void* VulkanContext::GetMappedData(void* bh)
{
    SilverLining::Vulkan::Buffer* buffer = static_cast<SilverLining::Vulkan::Buffer*>(bh);

    uint64_t offsetInBytes = 0;
    if (buffer->GetUserData()) {
        FrameIndexedBuffer* frameIndexedBuffer = static_cast<FrameIndexedBuffer*>(buffer->GetUserData());
        if (frameIndexedBuffer && frameIndexedBuffer->OffsetSizeInBytes() > 0) {
            offsetInBytes = frameIndexedBuffer->CurrentOffsetInBytes();
        }

        int frameIndex = (currentFrameIndex == -1) ? 0 : currentFrameIndex;
        SL_ASSERT(frameIndex < frameIndexedBuffer->GetBuffers().size());

        buffer = frameIndexedBuffer->GetBuffers()[frameIndex];
        //std::cout << "frame index: " << frameIndex << ", offset index: " << frameIndexedBuffer->CurrentOffset() << std::endl;
    }

    if (buffer->Dynamic()) {
        RawBuffer* rawBuffer = buffer->GetBuffer();
        void* mappedData = rawBuffer->MappedPtr();
        if (mappedData == nullptr) {
            buffer->GetDevice()->log(LogLevel::SL_LOG_LEVEL_WARN) << "mappedData == nullptr!" << std::endl;
            return nullptr;
        }

        // offset the mapped data with current offset, if there is offsetting (similar to ubo/ssbo)
        return (void*)((uint8_t*)(mappedData)+offsetInBytes);
    } else {
#if DEBUG_COMMAND_RECORDING
        SL_ASSERT(IsRecording() == false);
#endif
        return buffer->MappedStagingBuffer();
    }
}
bool VulkanContext::UnlockBuffer(void* bh)
{
    SilverLining::Vulkan::Buffer* buffer = static_cast<SilverLining::Vulkan::Buffer*>(bh);
    if (buffer->Dynamic()) {
        // Nothing to do. It is mapped as host coherent
    } else {
#if DEBUG_COMMAND_RECORDING
        SL_ASSERT(IsRecording() == false);
#endif
        buffer->SyncToGpu(false);
    }

    return true;
}

const VulkanInitInfo& VulkanContext::GetInfo(void) const
{
    return info;
}

int VulkanContext::GetCurrentFrameIndex(void) const
{
    return currentFrameIndex;
}

void VulkanContext::SettingsChanged(const VulkanInitInfo& newInfo)
{
    info.renderPass = newInfo.renderPass;
    info.sampleCount = newInfo.sampleCount;
    info.colorFormat = newInfo.colorFormat;
    info.depthFormat = newInfo.depthFormat;
    for (auto pipeline : pipelines) {
        pipeline->SettingsChanged();
    }
    if (linePipeline) {
        linePipeline->SettingsChanged();
    }
}

const BlendState& VulkanContext::GetDefaultBlendState(void) const
{
    return defaultBlendState;
}

FrontFace VulkanContext::GetDefaultFrontFace(void) const
{
    return defaultFrontFace;
}
}
}
