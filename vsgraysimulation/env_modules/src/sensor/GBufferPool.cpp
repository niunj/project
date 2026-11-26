#include "SensorShaderCache.h"
/// @file GBufferPool.cpp —— P1.1: 全场景唯一求交 (G-buffer 一次 TraceRays) 实现。
///
/// 命令链: BindRayTracingPipeline → [G0/G1 UNDEFINED→GENERAL] → TraceRays
///         → [RT写→compute读] 。G-buffer 全程 GENERAL 布局 (下游 band_shade /
///         radar_scatter compute 以 imageLoad 读, 跨 task 经 semaphore 构成
///         完整内存依赖, 无需二次 barrier)。
///
/// 加速结构: 复用 SharedSceneResources 池的单 BLAS/单实例 TLAS (P0);
/// 池未就绪 → 本类自抽取 (迁移期回退, 与 P0 前单实例路径同构)。
#include "EnvCommon.h"          // resolveEnvShader (src/ 私有)
#include "rocky_env/GBufferPool.h"
#include "rocky_env/SharedSceneResources.h"

#include <vsg/all.h>
#include <vsg/raytracing/DescriptorAccelerationStructure.h>
#include <vsg/raytracing/RayTracingPipeline.h>
#include <vsg/raytracing/TopLevelAccelerationStructure.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/commands/Commands.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace rockyenv {
namespace sensor {

namespace {

bool loadStage(VkShaderStageFlagBits stage, const char* name,
               vsg::ref_ptr<vsg::ShaderStage>& out) {
    out = vsg::ShaderStage::read(stage, "main", resolveEnvShader(name), vsg::Options::create());
    if (!out) {
        std::fprintf(stderr, "[GBuffer] shader 加载失败: %s\n", name);
        return false;
    }
    return true;
}

/// G-buffer 图 (RGBA32F, STORAGE, GENERAL 全程)。
vsg::ref_ptr<vsg::ImageView> makeGImage(vsg::Device* device, uint32_t w, uint32_t h) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    img->extent = VkExtent3D{w, h, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device, img, VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace

// ===========================================================================
// GBufferPool::build
// ===========================================================================
GBufferPool::Result GBufferPool::build(vsg::Window& window,
                                       vsg::ref_ptr<vsg::Node> scene,
                                       const std::vector<RadarMaterial>* materials,
                                       SharedSceneResources* shared,
                                       uint32_t width, uint32_t height) { return build(*window.getOrCreateDevice(), scene, materials, shared, width, height); }

GBufferPool::Result GBufferPool::build(vsg::Device& deviceArg,
                                       vsg::ref_ptr<vsg::Node> scene,
                                       const std::vector<RadarMaterial>* materials,
                                       SharedSceneResources* shared,
                                       uint32_t width, uint32_t height) {
    Result result;
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    if(!device->supportsDeviceExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
        return buildCompute(deviceArg,shared,width,height);
    w_ = width;
    h_ = height;
    shared_ = shared;

    // ---- 几何 SSBO + 加速结构: 池复用 或 自抽取 (迁移期回退路径) ----
    vsg::ref_ptr<vsg::BufferInfo> posFlatInfo, idxInfo, matIdInfo;
    vsg::ref_ptr<vsg::BufferInfo> uvInfo, slotIdInfo;   // TV 彩色: uv/贴图槽号
    vsg::ref_ptr<vsg::BufferInfo> thermalNormalInfo;
    vsg::ref_ptr<vsg::TopLevelAccelerationStructure> tlas;
    uint32_t nVerts = 0, nTris = 0;

    SharedSceneResources* pool = (shared && shared->ready()) ? shared : nullptr;
    if (shared && !shared->ready()) {
        std::fprintf(stderr, "[GBuffer] 共享池未就绪 (%s) → 自抽取回退\n",
                     shared->error().c_str());
    }

    if (pool) {
        posFlatInfo = pool->posFlatInfo;
        idxInfo     = pool->indicesInfo;
        matIdInfo   = pool->matIdxInfo;
        matDataInfo = pool->matDataInfo;
        spectralDataInfo = pool->spectralDataInfo;   // 批次1: 谱材质表同源复用
        uvInfo      = pool->uvFlatInfo;              // TV 彩色: uv/槽号池复用
        slotIdInfo  = pool->slotInfo;
        thermalNormalInfo = pool->thermalNormalInfo;
        tlas        = pool->tlas;
        nVerts = pool->vertexCount;
        nTris  = pool->triangleCount;
    } else {
        // ---- 场景抽取: 单 VertexIndexDraw 契约 (binding0 pos / 1 props / 2 mat_idx) ----
        auto vid = scene ? scene->cast<vsg::VertexIndexDraw>() : nullptr;
        if (!vid || vid->arrays.size() < 2) {
            result.error = "scene 非 VertexIndexDraw 契约 (arrays<2)";
            return result;
        }
        const auto posArr = vid->arrays[0]->data->cast<vsg::vec3Array>();
        if (!posArr) {
            result.error = "顶点契约不符 (期望 vec3 pos)";
            return result;
        }
        const vsg::ref_ptr<vsg::Data> posData = vid->arrays[0]->data;
        nVerts = uint32_t(posArr->size());

        vsg::ref_ptr<vsg::uintArray> idxU32;
        if (vid->indices && vid->indices->data) {
            if (auto* u32 = vid->indices->data->cast<vsg::uintArray>()) {
                idxU32 = u32;
            } else if (auto* u16 = vid->indices->data->cast<vsg::ushortArray>()) {
                idxU32 = vsg::uintArray::create(u16->size());
                for (uint32_t i = 0; i < u16->size(); ++i) (*idxU32)[i] = (*u16)[i];
            }
        }
        if (!idxU32 || idxU32->size() < 3) {
            result.error = "索引数组缺失 (<3)";
            return result;
        }

        vsg::ref_ptr<vsg::uintArray> matArr;
        if (vid->arrays.size() >= 3) {
            if (auto* m = vid->arrays[2]->data->cast<vsg::uintArray>()) matArr = m;
        }
        if (!matArr) {
            matArr = vsg::uintArray::create(nVerts);
            for (auto& m : *matArr) m = 0;
        }

        // TV 彩色: uv (binding3) / 贴图槽号 (binding4) 抽取 (缺失 → 零填充)
        vsg::ref_ptr<vsg::vec2Array> uvArr;
        if (vid->arrays.size() >= 4)
            if (auto* u = vid->arrays[3]->data->cast<vsg::vec2Array>()) uvArr = u;
        if (uvArr && uvArr->size() == nVerts) {
            uvInfo = vsg::BufferInfo::create(uvArr);
        } else {
            auto zeroUv = vsg::vec2Array::create(nVerts);
            for (auto& u : *zeroUv) u = vsg::vec2(0.0f, 0.0f);
            uvInfo = vsg::BufferInfo::create(zeroUv);
        }
        vsg::ref_ptr<vsg::uintArray> slotArr;
        if (vid->arrays.size() >= 5)
            if (auto* s = vid->arrays[4]->data->cast<vsg::uintArray>()) slotArr = s;
        if (slotArr && slotArr->size() == nVerts) {
            slotIdInfo = vsg::BufferInfo::create(slotArr);
        } else {
            auto zeroSlot = vsg::uintArray::create(nVerts);
            for (auto& s : *zeroSlot) s = 0;
            slotIdInfo = vsg::BufferInfo::create(zeroSlot);
        }

        // pos 平铺 float (std430 vec3 数组 16B stride 陷阱规避)
        auto posFlat = vsg::floatArray::create(size_t(nVerts) * 3);
        std::memcpy(posFlat->dataPointer(), posArr->dataPointer(), size_t(nVerts) * sizeof(vsg::vec3));

        // σ 材质表 (回退路径自建; 与 SharedSceneResources 同构, 批次4: 3 vec4/材质)
        const uint32_t nMats = materials ? std::max<uint32_t>(1, uint32_t(materials->size())) : 1;
        auto matData = vsg::floatArray::create(size_t(nMats) * 12);
        if (materials && !materials->empty()) {
            for (uint32_t m = 0; m < materials->size(); ++m) {
                const auto& r = (*materials)[m];
                float* d = matData->data(uint32_t(m) * 12);
                d[0] = r.dielectric_real; d[1] = r.dielectric_imag;
                d[2] = r.sigma0_db;       d[3] = r.surface_rough;
                d[4] = float(r.rcs_lut_id);
                d[5] = float(r.dielectric_model);   // 批次4: 介电模型 0/1/2/3
                d[6] = r.terrain ? 1.f : 0.f;
            }
        } else {
            float* d = matData->data(0);
            d[0] = 6.0f; d[1] = 0.0f; d[2] = -15.0f; d[3] = 0.3f;
            d[4] = -1.0f; d[5] = 0.0f; d[6] = 0.0f;
        }

        // 谱材质表 SSBO (回退路径自建; 打包规则与 SharedSceneResources 同构):
        //   a = (ρ_tv, ρ_swir, ε_mwir, ε_lwir)   b = (镜面强度, GGX 粗糙度, 0, 0)
        {
            auto specData = vsg::floatArray::create(size_t(nMats) * 16);
            const RadarMaterial defMat{};
            for (uint32_t m = 0; m < nMats; ++m) {
                const RadarMaterial& r = (materials && m < materials->size())
                                             ? (*materials)[m] : defMat;
                const float epsBase = r.emissivity > 0.0f ? r.emissivity : 0.90f;
                const float eMw = r.eps_mwir >= 0.0f ? r.eps_mwir : -1.0f;
                const float eLw = r.eps_lwir >= 0.0f ? r.eps_lwir : -1.0f;
                float* s = specData->data(uint32_t(m) * 16);
                s[0] = r.rho_tv >= 0.0f ? r.rho_tv : 1.0f - epsBase;
                s[1] = r.rho_swir >= 0.0f ? r.rho_swir : 1.0f - epsBase;
                s[2] = eMw;
                s[3] = eLw;
                s[4] = r.spec_strength;
                s[5] = r.spec_roughness;
                s[6] = (r.dielectric_model == 1) ? 1.0f : 0.0f;
                s[7] = 0.0f;
                std::copy(r.thermal_surface.begin(), r.thermal_surface.end(), s + 8);
                std::copy(r.thermal_source.begin(), r.thermal_source.end(), s + 12);
            }
            spectralDataInfo = vsg::BufferInfo::create(specData);
        }

        // 单 BLAS + 单实例 TLAS (恒等变换; 视角由 rgen push 下发)
        auto accelGeom = vsg::AccelerationGeometry::create();
        accelGeom->assignVertices(posData);
        accelGeom->assignIndices(idxU32);
        auto blas = vsg::BottomLevelAccelerationStructure::create(device);
        blas->geometries.push_back(accelGeom);
        tlas = vsg::TopLevelAccelerationStructure::create(device);
        auto inst = vsg::GeometryInstance::create();
        inst->accelerationStructure = blas;
        inst->transform = vsg::mat4();
        inst->id = 0;
        inst->mask = 0xff;
        inst->shaderOffset = 0;
        tlas->geometryInstances.push_back(inst);

        posFlatInfo = vsg::BufferInfo::create(posFlat);
        idxInfo     = vsg::BufferInfo::create(idxU32);
        matIdInfo   = vsg::BufferInfo::create(matArr);
        matDataInfo = vsg::BufferInfo::create(matData);
        nTris = uint32_t(idxU32->size() / 3);
    }

    // ---- G-buffer 四图 (RGBA32F STORAGE) ----
    g0 = makeGImage(device.get(), w_, h_);
    g1 = makeGImage(device.get(), w_, h_);
    g2 = makeGImage(device.get(), w_, h_);
    g3 = makeGImage(device.get(), w_, h_);

    // ---- 描述符: b0 tlas / 1 G0 / 2 G1 / 7 G2 / 3 posFlat / 4 idx / 5 props / 6 matIdx
    //              / 10 uv / 11 texSlot (TV 彩色) / 12 G3 ----
    auto dsl = vsg::DescriptorSetLayout::create();
    dsl->addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    dsl->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(12, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
                    VK_SHADER_STAGE_MISS_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    for (uint32_t b = 3; b <= 6; ++b)
        dsl->addBinding(b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    dsl->addBinding(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    vsg::ref_ptr<vsg::BufferInfo> propsInfo;
    if (pool) {
        propsInfo = pool->propsInfo;
    } else {
        auto vid = scene->cast<vsg::VertexIndexDraw>();
        propsInfo = vsg::BufferInfo::create(vid->arrays[1]->data);
        const auto* normals = vid->arrays.size()>5 ? vid->arrays[5]->data->cast<vsg::vec4Array>() : nullptr;
        thermalNormalInfo = vsg::BufferInfo::create(normals && normals->size()==nVerts
            ? vid->arrays[5]->data : vsg::ref_ptr<vsg::Data>(vsg::vec4Array::create(1,vsg::vec4{})));
    }

    auto g0Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, g0, VK_IMAGE_LAYOUT_GENERAL);
    auto g1Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, g1, VK_IMAGE_LAYOUT_GENERAL);
    auto g2Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, g2, VK_IMAGE_LAYOUT_GENERAL);
    auto g3Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, g3, VK_IMAGE_LAYOUT_GENERAL);
    auto descSet = vsg::DescriptorSet::create(dsl, vsg::Descriptors{
        vsg::DescriptorAccelerationStructure::create(vsg::AccelerationStructures{tlas}, 0, 0),
        vsg::DescriptorImage::create(g0Info, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(g1Info, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(g2Info, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(g3Info, 12, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{posFlatInfo}, 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{idxInfo}, 4, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{propsInfo}, 5, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{matIdInfo}, 6, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{uvInfo}, 10, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{slotIdInfo}, 11, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{thermalNormalInfo}, 16, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    // ---- RT shader (rgen/rmiss/rchit, 源码树优先) ----
    vsg::ref_ptr<vsg::ShaderStage> rgen, rmiss, rchit;
    if (!loadStage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, "gbuffer.rgen", rgen) ||
        !loadStage(VK_SHADER_STAGE_MISS_BIT_KHR, "gbuffer.rmiss", rmiss) ||
        !loadStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "gbuffer.rchit", rchit)) {
        result.error = "RT shader 加载失败";
        return result;
    }
    vsg::ShaderStages stages{rgen, rmiss, rchit};
    if (!compileSensorStages(stages)) {
        result.error = "RT shader 编译失败 (glslang)";
        return result;
    }

    // ---- SBT 组: [0] rgen, [1] miss, [2] triangles hit (rchit) ----
    auto rgenGroup = vsg::RayTracingShaderGroup::create();
    rgenGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    rgenGroup->generalShader = 0;
    auto missGroup = vsg::RayTracingShaderGroup::create();
    missGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    missGroup->generalShader = 1;
    auto hitGroup = vsg::RayTracingShaderGroup::create();
    hitGroup->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    hitGroup->closestHitShader = 2;

    const VkPushConstantRange pushRange{VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(GBufferPush)};
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},
                                              vsg::PushConstantRanges{pushRange});
    auto rtp = vsg::RayTracingPipeline::create(layout, stages,
                                               vsg::RayTracingShaderGroups{rgenGroup, missGroup, hitGroup});

    // ---- push 值 (每帧 setSensorView 更新) ----
    push_ = vsg::Value<GBufferPush>::create();
    push_->properties.dataVariance = vsg::DYNAMIC_DATA;

    // ---- 相机位置 UBO (批次1: band_shade 镜面 V 向; setSensorView 每帧更新) ----
    camPosData_ = vsg::floatArray::create(4);
    for (auto& f : *camPosData_) f = 0.0f;
    camPosData_->properties.dataVariance = vsg::DYNAMIC_DATA;
    camPosInfo = vsg::BufferInfo::create(camPosData_);

    // ---- 命令链: bind → [G→GENERAL] → TraceRays → [RT写→compute读] ----
    commands = vsg::Commands::create();
    if(pool && pool->sceneUpdates) commands->addChild(pool->sceneUpdates);
    commands->addChild(vsg::BindRayTracingPipeline::create(rtp));
    commands->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout, 0, vsg::DescriptorSets{descSet}));
    commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, push_));

    auto preBarrier = vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0,
        vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, g0->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}),
        vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, g1->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}),
        vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, g2->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}),
        vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, g3->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
    commands->addChild(preBarrier);

    auto trace = vsg::TraceRays::create();   // ★ 全场景唯一 TraceRays (P1.1)
    trace->raygen = rgenGroup;
    trace->missShader = missGroup;
    trace->hitShader = hitGroup;
    trace->width = w_;
    trace->height = h_;
    trace->depth = 1;
    commands->addChild(trace);

    auto postBarrier = vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
    commands->addChild(postBarrier);

    ready_ = true;
    result.ok = true;
    std::printf("[GBuffer] G-buffer 求交池就绪 (P1.1): %ux%u, %u 顶点/%u 三角, 全场景唯一 "
                "TraceRays, %s\n",
                w_, h_, nVerts, nTris,
                pool ? "几何/BLAS/TLAS/材质表 复用共享池 (P0)" : "单实例自抽取回退");
    return result;
}

GBufferPool::Result GBufferPool::buildCompute(vsg::Device& device,SharedSceneResources* shared,uint32_t width,uint32_t height) {
    if(!shared || !shared->ready() || !shared->instanceInfo) return {false,"compute G-buffer requires scene BVHs"};
    if(!width || !height || uint64_t(width)*height>(1u<<24)) return {false,"invalid G-buffer dimensions"};
    w_=width; h_=height; shared_=shared;
    g0=makeGImage(&device,w_,h_); g1=makeGImage(&device,w_,h_); g2=makeGImage(&device,w_,h_); g3=makeGImage(&device,w_,h_);
    matDataInfo=shared->matDataInfo; spectralDataInfo=shared->spectralDataInfo;
    push_=vsg::Value<GBufferPush>::create();
    camPosData_=vsg::floatArray::create(4); for(auto& value:*camPosData_) value=0;
    camPosData_->properties.dataVariance=vsg::DYNAMIC_DATA; camPosInfo=vsg::BufferInfo::create(camPosData_);
    auto layout=vsg::DescriptorSetLayout::create();
    vsg::Descriptors descriptors;
    for(auto binding:{1u,2u,7u,12u}) layout->addBinding(binding,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT);
    const vsg::ref_ptr<vsg::ImageView> images[]={g0,g1,g2,g3}; const uint32_t imageBindings[]={1,2,7,12};
    for(int i=0;i<4;++i) descriptors.push_back(vsg::DescriptorImage::create(
        vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},images[i],VK_IMAGE_LAYOUT_GENERAL),imageBindings[i],0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
    const std::pair<uint32_t,vsg::ref_ptr<vsg::BufferInfo>> buffers[]={
        {3,shared->posFlatInfo},{4,shared->indicesInfo},{5,shared->propsInfo},{6,shared->matIdxInfo},
        {10,shared->uvFlatInfo},{11,shared->slotInfo},{13,shared->bvhInfo},{14,shared->primitiveInfo},{15,shared->instanceInfo},
        {16,shared->thermalNormalInfo}};
    for(const auto& [binding,buffer]:buffers) {
        layout->addBinding(binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT);
        descriptors.push_back(vsg::DescriptorBuffer::create(vsg::BufferInfoList{buffer},binding,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    }
    vsg::ref_ptr<vsg::ShaderStage> stage;
    if(!loadStage(VK_SHADER_STAGE_COMPUTE_BIT,"gbuffer_compute.comp",stage)) return {false,"compute G-buffer shader missing"};
    vsg::ShaderStages stages{stage};
    if(!compileSensorStages(stages)) return {false,"compute G-buffer shader failed compilation"};
    auto pipelineLayout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{layout},vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(GBufferPush)}});
    auto pipeline=vsg::ComputePipeline::create(pipelineLayout,stage);
    auto descriptorSet=vsg::DescriptorSet::create(layout,descriptors);
    commands=vsg::Commands::create();
    auto before=vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0);
    for(auto image:images) before->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(0,VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,image->image,
        VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}));
    commands->addChild(before);
    commands->addChild(vsg::BindComputePipeline::create(pipeline));
    commands->addChild(vsg::BindDescriptorSets::create(VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,vsg::DescriptorSets{descriptorSet}));
    commands->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,push_));
    commands->addChild(vsg::Dispatch::create((w_+7)/8,(h_+7)/8,1));
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)));
    ready_=true; return {true,{}};
}

// ===========================================================================
// 每帧更新
// ===========================================================================
void GBufferPool::setSensorView(const vsg::dmat4& proj, const vsg::dmat4& view) {
    if (!push_) return;
    auto& p = push_->value();
    p.viewInv = vsg::mat4(vsg::inverse(view));
    p.projInv = vsg::mat4(vsg::inverse(proj));
    // 批次1: 相机 ENU 位置 → band_shade 镜面 V 向 UBO (与活跃视角同源)
    if (camPosData_) {
        const vsg::dvec4 eye = vsg::inverse(view) * vsg::dvec4(0.0, 0.0, 0.0, 1.0);
        (*camPosData_)[0] = float(eye.x);
        (*camPosData_)[1] = float(eye.y);
        (*camPosData_)[2] = float(eye.z);
        camPosData_->dirty();   // DYNAMIC_DATA: record 时上传
    }
}

} // namespace sensor
} // namespace rockyenv
