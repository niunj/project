#include "SensorShaderCache.h"
/// @file BandRT.cpp —— B2/P1.1: 波段辐亮度 P0 模块实现 (per-波段 compute 着色)。
///
/// P1.1 改造: RT 求交收敛至 GBufferPool (全场景唯一 TraceRays → 波段无关
/// G-buffer); 本模块只建 band_shade compute 管线:
///   [P0 UNDEFINED→GENERAL] → dispatch(着色) → [compute写→FS读 barrier]。
/// P0 全程 GENERAL 布局, optics pass 以 GENERAL 布局采样 (采样自 GENERAL
/// 合法, 免布局往返) —— 下游光学/探测器/电子学级联零改动。
#include "EnvCommon.h"          // resolveEnvShader (src/ 私有)
#include "rocky_env/BandRT.h"
#include "rocky_env/GBufferPool.h"
#include "rocky_env/SharedSceneResources.h"

#include <vsg/all.h>
#include <vsg/commands/PipelineBarrier.h>
#include <vsg/commands/Commands.h>

#include <cstdio>
#include <cstring>
#include <cmath>

namespace rockyenv {
namespace sensor {

// ===========================================================================
// 内部辅助
// ===========================================================================
namespace {

constexpr uint32_t kLutSamples = 512;

bool loadStage(VkShaderStageFlagBits stage, const char* name,
               vsg::ref_ptr<vsg::ShaderStage>& out) {
    out = vsg::ShaderStage::read(stage, "main", resolveEnvShader(name),vsg::Options::create());
    if (!out) {
        std::fprintf(stderr, "[BandRT] shader 加载失败: %s\n", name);
        return false;
    }
    return true;
}

vsg::ref_ptr<vsg::Sampler> makeSampler() {
    auto s = vsg::Sampler::create();
    s->magFilter = VK_FILTER_LINEAR;
    s->minFilter = VK_FILTER_LINEAR;
    s->mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s->addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s->addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return s;
}

/// 波段出射度 LUT 纹理 (512×1 R32, TRANSFER_DST 上传 —— 与 SensorPipeline 同坑同解)
vsg::ref_ptr<vsg::ImageView> makeLutView(vsg::Device* device, const BandRadianceLut& lut) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R32_SFLOAT;
    img->extent = VkExtent3D{kLutSamples, 1, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto data = vsg::floatArray2D::create(kLutSamples, 1);
    data->properties.format = VK_FORMAT_R32_SFLOAT;
    const auto& table = lut.table();
    for (uint32_t i = 0; i < kLutSamples && i < table.size(); ++i) (*data)(i, 0) = table[i];
    img->data = data;
    auto view = vsg::ImageView::create(img);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D;
    return view;
}

/// TV 彩色: 占位 1×1×1 albedo 数组 (池无贴图时绑定; hasTex=0 → shader 走 Kd 色分支)
vsg::ref_ptr<vsg::ImageView> makeDummyTexArray() {
    auto arr = vsg::ubvec4Array3D::create(1, 1, 1);
    arr->properties.format = VK_FORMAT_R8G8B8A8_UNORM;
    (*arr)(0, 0, 0) = vsg::ubvec4(128, 128, 128, 255);
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R8G8B8A8_UNORM;
    img->extent = VkExtent3D{1, 1, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img->data = arr;
    auto view = vsg::ImageView::create(img);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    return view;
}

} // namespace

// ===========================================================================
// BandRT::build
// ===========================================================================
BandRT::Result BandRT::build(vsg::Window& window,
                             const SensorProfile& profile,
                             const BandRadianceLut& lut,
                             const EnvAnswer& env,
                             GBufferPool* gbuffer,
                             SharedSceneResources* shared) { return build(*window.getOrCreateDevice(), profile, lut, env, gbuffer, shared); }

BandRT::Result BandRT::build(vsg::Device& deviceArg,
                             const SensorProfile& profile,
                             const BandRadianceLut& lut,
                             const EnvAnswer& env,
                             GBufferPool* gbuffer,
                             SharedSceneResources* shared) {
    Result result;
    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    const uint32_t w = profile.width, h = profile.height;

    if (!gbuffer || !gbuffer->ready()) {
        result.error = "G-buffer 池缺失/未就绪 (P1.1 着色路径依赖)";
        return result;
    }
    if (gbuffer->width() != w || gbuffer->height() != h) {
        result.error = "G-buffer 分辨率与传感器不符 (" +
                       std::to_string(gbuffer->width()) + "x" + std::to_string(gbuffer->height()) +
                       " vs " + std::to_string(w) + "x" + std::to_string(h) + ")";
        return result;
    }
    shared_ = shared;

    // ---- P0 storage image (STORAGE|SAMPLED, GENERAL 全程; 下游 optics 采样) ----
    auto p0Img = vsg::Image::create();
    p0Img->imageType = VK_IMAGE_TYPE_2D;
    p0Img->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    p0Img->extent = VkExtent3D{w, h, 1};
    p0Img->mipLevels = 1;
    p0Img->arrayLayers = 1;
    p0Img->samples = VK_SAMPLE_COUNT_1_BIT;
    p0Img->tiling = VK_IMAGE_TILING_OPTIMAL;
    p0Img->usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    p0Img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    p0 = vsg::createImageView(device, p0Img, VK_IMAGE_ASPECT_COLOR_BIT);

    // ---- Per-band/per-sensor environment UBO: spectral answers must not alias. ----
    // 布局 (与池 setEnv 同构): [σ, sky, Esun, 0, sunDir.x, sunDir.y, sunDir.z, 0]
    vsg::ref_ptr<vsg::BufferInfo> envInfo;
    SharedSceneResources* pool = shared_;
    {
        envData_ = vsg::floatArray::create(8);
        (*envData_)[0] = float(env.sigma_per_km);
        (*envData_)[1] = float(env.sky_radiance);
        (*envData_)[2] = float(env.sun_irradiance);
        (*envData_)[3] = 0.0f;
        double dir[3];
        dirFromAzEl(env.sun_az_deg, env.sun_el_deg, dir);   // 批次1: 太阳 ENU 指向
        (*envData_)[4] = float(dir[0]);
        (*envData_)[5] = float(dir[1]);
        (*envData_)[6] = float(dir[2]);
        (*envData_)[7] = 0.0f;
        envData_->properties.dataVariance = vsg::DYNAMIC_DATA;
        envInfo = vsg::BufferInfo::create(envData_);
    }

    // ---- 波段差量 UBO (动态, 96 float = 24 vec4 与 band_shade.comp BandUbo 对齐):
    //      band   [tMin, tMax, λc_um, bandId]  band2 [kSun, 基准N·L, 云底 m, 云影强度]
    //      band3  [云顶 m, 覆盖率, 密度系数, 风速]  band4 [风向x, 风向z, 时间 s, 云发射辐亮度]
    //      #4 oceanWave[16] (x=L y=A z=cosθ w=sinθ) + oceanOpt [Q, waveCount, pxAng, 0]
    //      #6 thermal [dayRelax, nightRelax, nightDrop K, 云影降温 K] (setEnv 每帧)
    //      r10 band5 [云型 0-4, pad...] (setCloudVolume)
    // bandId (批次1): 0=TV 1=SWIR 2=MWIR 3=LWIR → band_shade ρ_band 列选择
    // B2 kSun: 14K 满幅 × 宽谱加热因子 (is_day·sin太阳高·(1-0.8·云量)) ——
    // 注意不能用 env.sun_irradiance 归一: 红外波段内太阳辐照 ~1 W/m² (加热
    // 来自可见光/近红外宽谱 ~500-1000 W/m²), 用它会使红外 kSun→0 失效;
    // 夜间 is_day=0 → kSun=0 恒等退化。基准 N·L=0.35 (城市平均朝向吸热)
    // #5 体积云: band2.zw + band3/band4 由 setCloudVolume 每帧注入 (数值去重)
    // #4 海面波场: oceanWave/oceanOpt 由 setOceanWave 每帧注入 (数值去重)
    bandData_ = vsg::floatArray::create(96);
    bandData_->properties.dataVariance = vsg::DYNAMIC_DATA;
    (*bandData_)[0] = lut.tMin();
    (*bandData_)[1] = lut.tMax();
    (*bandData_)[2] = float((profile.band.lambda_min + profile.band.lambda_max) * 0.5 * 1e6);
    (*bandData_)[3] = float(int(profile.band.type));
    const double sunHeat = env.is_day * std::max(std::sin(env.sun_el_deg * M_PI / 180.0), 0.0)
                         * (1.0 - 0.8 * std::clamp(env.cloud_coverage, 0.0, 1.0));
    (*bandData_)[4] = 14.0f * float(std::clamp(sunHeat, 0.0, 1.0));
    (*bandData_)[5] = 0.35f;
    (*bandData_)[6] = 0.0f;   // 云底 (<=0 → 云体/云影恒等禁用)
    (*bandData_)[7] = 0.0f;   // 云影强度
    (*bandData_)[8] = 0.0f;   // 云顶
    (*bandData_)[9] = 0.0f;   // 覆盖率
    (*bandData_)[10] = 0.0f;  // 密度系数
    (*bandData_)[11] = 0.0f;  // 风速
    (*bandData_)[12] = 0.0f;  // 风向x
    (*bandData_)[13] = 0.0f;  // 风向z
    (*bandData_)[14] = 0.0f;  // 时间 s
    (*bandData_)[15] = 0.0f;  // IR 云发射辐亮度
    for (int i = 16; i < 96; ++i) (*bandData_)[i] = 0.0f;   // #4/#6/r10 oceanWave/oceanOpt/thermal/band5 初始禁用
    // #4: 像素角 rad/px (视距滤波尺度 = 短波亚像素混叠判据; fov/宽度真值,
    //     0 时 shader 回退近似常量) —— build 期一次, setOceanWave 下发 oceanOpt.z
    pxAngRad_ = float(profile.fov_deg * M_PI / 180.0 / std::max(profile.width, 1u));

    // ---- sampler / LUT: 池共享 或 回退本地创建 ----
    auto sampler = (pool && pool->sampler) ? pool->sampler : makeSampler();
    auto lutView = (pool && pool->ready()) ? pool->lutView(lut) : makeLutView(device.get(), lut);

    // ---- 描述符: b0 G0 / 1 P0 / 2 env / 3 band / 4 LUT / 5 谱材质 / 6 camPos / 7 G1 / 8 G2
    //              / 9 sky / 10 G3 / 11 albedo 数组 / 12 贴图槽表 (TV 彩色)
    //              (#5: 旧 b13 云覆盖图已删除 —— 云体由 3D 密度场 ray march 实时渲染) ----
    auto dsl = vsg::DescriptorSetLayout::create();
    dsl->addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(10, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    dsl->addBinding(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    auto g0Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g0,
                                         VK_IMAGE_LAYOUT_GENERAL);
    auto p0Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, p0, VK_IMAGE_LAYOUT_GENERAL);
    auto g1Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g1,
                                         VK_IMAGE_LAYOUT_GENERAL);
    auto g2Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g2,
                                         VK_IMAGE_LAYOUT_GENERAL);
    auto lutInfo = vsg::ImageInfo::create(sampler, lutView,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ---- 批次2: 谱天空图 (256×128 latlong; CPU bandSky 烘焙经 updateSkyMap
    //      注入, DYNAMIC_DATA → TransferTask 运行时上传, 与 LUT 同机制) ----
    // #1: TV 波段 RGBA32F (rgb=三子带物理真值, a=luma) / 其它 R32F 标量。
    //     格式与数据数组值类型必须严格匹配 (vsg 上传按 properties.format)。
    constexpr uint32_t kSkyW = 256, kSkyH = 128;
    const bool skyRgb = (profile.band.type == BandType::TV);
    skyCh_ = skyRgb ? 4u : 1u;
    if (skyRgb) {
        auto d = vsg::vec4Array2D::create(kSkyW, kSkyH);
        d->properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        d->properties.dataVariance = vsg::DYNAMIC_DATA;
        std::memset(d->dataPointer(), 0, size_t(kSkyW) * kSkyH * sizeof(vsg::vec4));
        skyData_ = d;
    } else {
        auto d = vsg::floatArray2D::create(kSkyW, kSkyH);
        d->properties.format = VK_FORMAT_R32_SFLOAT;
        d->properties.dataVariance = vsg::DYNAMIC_DATA;
        std::memset(d->dataPointer(), 0, size_t(kSkyW) * kSkyH * sizeof(float));
        skyData_ = d;
    }
    skyCache_.assign(size_t(kSkyW) * kSkyH * skyCh_, 0.0f);
    skyW_ = kSkyW;
    skyH_ = kSkyH;
    auto skyImg = vsg::Image::create();
    skyImg->imageType = VK_IMAGE_TYPE_2D;
    skyImg->format = skyRgb ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R32_SFLOAT;
    skyImg->extent = VkExtent3D{kSkyW, kSkyH, 1};
    skyImg->mipLevels = 1;
    skyImg->arrayLayers = 1;
    skyImg->samples = VK_SAMPLE_COUNT_1_BIT;
    skyImg->tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSFER_DST 必须保留: 缺失时 VSG 上传跳过 → 采样恒 0 (与 LUT 同坑)
    skyImg->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    skyImg->data = skyData_;
    skyView_ = vsg::ImageView::create(skyImg);
    skyView_->viewType = VK_IMAGE_VIEW_TYPE_2D;
    // 天空图专用采样器: u 方向 REPEAT (latlong 方位环绕连续; 共享 sampler 为
    // CLAMP_TO_EDGE, az=±180° 分支切割处两侧纹素不连续 → 视场中心出现竖缝)
    skySampler_ = makeSampler();
    skySampler_->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    auto skyInfo = vsg::ImageInfo::create(skySampler_, skyView_,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // ---- TV 彩色: G3 (uv/槽号) + albedo 数组 + 槽表 (池复用; 池缺失 → 占位) ----
    auto g3Info = vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, gbuffer->g3,
                                         VK_IMAGE_LAYOUT_GENERAL);
    vsg::ref_ptr<vsg::ImageView> albedoView;
    vsg::ref_ptr<vsg::BufferInfo> texSlotInfo;
    if (pool && pool->ready()) {
        albedoView = pool->texArrayView ? pool->texArrayView : makeDummyTexArray();
        texSlotInfo = pool->texSlotDataInfo;
    }
    if (!albedoView) albedoView = makeDummyTexArray();
    if (!texSlotInfo) {
        auto defSlot = vsg::floatArray::create(4);
        float* d = defSlot->data(0);
        d[0] = 0.5f; d[1] = 0.5f; d[2] = 0.5f; d[3] = 0.0f;   // 灰 Kd, hasTex=0
        texSlotInfo = vsg::BufferInfo::create(defSlot);
    }
    auto albedoInfo = vsg::ImageInfo::create(sampler, albedoView,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    auto descSet = vsg::DescriptorSet::create(dsl, vsg::Descriptors{
        vsg::DescriptorImage::create(g0Info, 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(p0Info, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{envInfo}, 2),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(bandData_)}, 3),
        vsg::DescriptorImage::create(lutInfo, 4),
        // 批次1: 谱材质表 / 相机位置 / G1 (pos+matId) / G2 (法线) —— 太阳反射+GGX
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{gbuffer->spectralDataInfo}, 5, 0,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{gbuffer->camPosInfo}, 6, 0,
                                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        vsg::DescriptorImage::create(g1Info, 7, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(g2Info, 8, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        // 批次2: 谱天空图 —— 背景与路径辐射逐像素方向化
        vsg::DescriptorImage::create(skyInfo, 9),
        // TV 彩色: G3 (uv/槽号) + albedo texture2DArray + 槽表 (Kd.rgb, hasTex)
        vsg::DescriptorImage::create(g3Info, 10, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(albedoInfo, 11),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{texSlotInfo}, 12, 0,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
    });

    // ---- compute shader (源码树优先) ----
    vsg::ref_ptr<vsg::ShaderStage> sh;
    if (!loadStage(VK_SHADER_STAGE_COMPUTE_BIT, "band_shade.comp", sh)) {
        result.error = "compute shader 加载失败";
        return result;
    }
    auto cloud=shared ? shared->voxelCloud : nullptr;
    if(cloud && cloud->resources->ownerDevice && cloud->resources->ownerDevice!=device.get()) {
        result.error="voxel cloud belongs to a different Vulkan device";return result;
    }
    if(cloud)sh->module->source.insert(sh->module->source.find('\n',sh->module->source.find("#version"))+1,
                                     "#define USE_VOXEL_CLOUD_FIELD 1\n");
    vsg::ShaderStages stages{sh};
    if (!compileSensorStages(stages)) {
        result.error = "compute shader 编译失败 (glslang)";
        return result;
    }
    vsg::DescriptorSetLayouts layouts{dsl};
    if(cloud){layouts.push_back(vsg::DescriptorSetLayout::create());layouts.push_back(cloud->layout);}
    auto layout = vsg::PipelineLayout::create(layouts,
                                              vsg::PushConstantRanges{});
    auto cp = vsg::ComputePipeline::create(layout, sh);

    // ---- 每帧命令: [P0→GENERAL] → dispatch → [compute写→FS读] ----
    commands = vsg::Commands::create();
    commands->addChild(vsg::BindComputePipeline::create(cp));
    commands->addChild(vsg::BindDescriptorSets::create(
        VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, vsg::DescriptorSets{descSet}));
    if(cloud)commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,2,cloud->descriptors));

    auto preBarrier = vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, p0Img,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
    commands->addChild(preBarrier);

    const uint32_t gx = (w + 7) / 8, gy = (h + 7) / 8;
    commands->addChild(vsg::Dispatch::create(gx, gy, 1));

    auto postBarrier = vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
    commands->addChild(postBarrier);

    std::printf("[BandRT] P0 着色 compute 就绪 (P1.1): %ux%u, dispatch %ux%u, "
                "G-buffer 复用池求交 (全场景唯一 TraceRays), %s\n",
                w, h, gx, gy,
                (pool && pool->ready()) ? "独立谱环境 / 共享 LUT 与 sampler" : "本地回退路径");
    result.ok = true;
    return result;
}

// ===========================================================================
// 每帧更新
// ===========================================================================
void BandRT::setEnv(const EnvAnswer& env) {
    // #6 热演化弛豫因子 (工程式单盒模型, 太阳几何解析日进度):
    //   日晒 dayRelax = 1-exp(-t_day/τ_heat)  —— 日出 (az≈90°) 起向日晒平衡渐进,
    //   τ_heat=90min 城市表面层典型; 正午≈1 全幅, 与 B2 静态式衔接。
    //   夜冷 nightRelax = 1-exp(-t_night/τ_cool) —— 日落 (az≈270°) 起辐射冷却
    //   渐入, τ_cool=180min; nightDrop 降温幅度 (水体热惯量大 → shader 端 ×0.15)。
    //   kSun 同式每帧重算 (原 build 静态 → 随太阳高/云量动态)。
    if (bandData_) {
        const bool day = env.is_day > 0.5;
        const double az = std::fmod(env.sun_az_deg + 360.0, 360.0);
        const double tDayH = day ? std::max(az - 90.0, 0.0) / 15.0 : 0.0;
        const double tNightH = day ? 0.0 : std::fmod(az + 90.0, 360.0) / 15.0;
        const double sunHeat = env.is_day * std::max(std::sin(env.sun_el_deg * M_PI / 180.0), 0.0)
                             * (1.0 - 0.8 * std::clamp(env.cloud_coverage, 0.0, 1.0));
        (*bandData_)[4] = 14.0f * float(std::clamp(sunHeat, 0.0, 1.0));
        (*bandData_)[84] = day ? float(1.0 - std::exp(-tDayH * 60.0 / 90.0)) : 0.0f;
        (*bandData_)[85] = day ? 0.0f : float(1.0 - std::exp(-tNightH * 60.0 / 180.0));
        (*bandData_)[86] = 4.0f;   // 夜间辐射冷却降幅 K (城市表面层典型)
        (*bandData_)[89] = float(env.cloud_coverage);
        (*bandData_)[92] = float(env.thermal_local_hour);
        (*bandData_)[93] = float(env.thermal_air_delta_k);
        (*bandData_)[94] = float(env.thermal_wetness);
        (*bandData_)[95] = float(env.thermal_scene_seconds);
        (*bandData_)[87] = 6.0f;   // 白天云影全影降温 K (直射遮挡典型量级)
        bandData_->dirty();
    }
    if (!envData_) return;   // 回退路径: 本地 8-float UBO
    (*envData_)[0] = float(env.sigma_per_km);
    (*envData_)[1] = float(env.sky_radiance);
    (*envData_)[2] = float(env.sun_irradiance);
    double dir[3];
    dirFromAzEl(env.sun_az_deg, env.sun_el_deg, dir);   // 批次1: 太阳 ENU 指向
    (*envData_)[4] = float(dir[0]);
    (*envData_)[5] = float(dir[1]);
    (*envData_)[6] = float(dir[2]);
    envData_->dirty();   // DYNAMIC_DATA: modifiedCount 变化触发 record 时上传
}

// ===========================================================================
// 批次2/#1: 谱天空图注入 (CPU bandSky 烘焙 → 数值去重 → dirty 上传)。
// 通道数 skyCh_ 由 build 按波段定 (TV=4, 其它=1), 与烘焙方约定一致。
// ===========================================================================
void BandRT::updateSkyMap(const float* data, uint32_t w, uint32_t h) {
    if (!skyData_ || w != skyW_ || h != skyH_) {
        std::fprintf(stderr, "[BandRT] updateSkyMap: 尺寸不符 (%ux%u vs %ux%u), 忽略\n",
                     w, h, skyW_, skyH_);
        return;
    }
    const size_t n = size_t(w) * h * skyCh_;
    if (skyCache_.size() == n && std::memcmp(skyCache_.data(), data, n * sizeof(float)) == 0) {
        return;   // 数值去重: env 静止时零上传
    }
    std::memcpy(skyData_->dataPointer(), data, n * sizeof(float));
    skyCache_.assign(data, data + n);
    skyData_->dirty();   // DYNAMIC_DATA: TransferTask 运行时上传
}

// ===========================================================================
// #5 体积云: 每帧注入云体积参数 (band2.zw + band3/band4/band5.x, 数值去重)。
// UBO 布局与 band_shade.comp BandUbo 对齐:
//   [6]=云底 [7]=影强 | [8]=云顶 [9]=覆盖率 [10]=密度系数 [11]=风速
//   [12]=风向x [13]=风向z [14]=时间 s [15]=IR 云发射辐亮度 | [88]=云型 0-4
// ===========================================================================
void BandRT::setCloudVolume(float baseM, float topM, float coverage, float density,
                            float windX, float windZ, float windSpeed, float timeSec,
                            float cloudEmitL, float strength, float cloudType) {
    if (!bandData_) return;
    const float vals[11] = {baseM, strength, topM, coverage, density,
                            windSpeed, windX, windZ, timeSec, cloudEmitL, cloudType};
    if (std::memcmp(volCache_, vals, sizeof(vals)) == 0) return;   // 数值去重
    std::memcpy(volCache_, vals, sizeof(vals));
    (*bandData_)[6] = baseM;
    (*bandData_)[7] = strength;
    (*bandData_)[8] = topM;
    (*bandData_)[9] = coverage;
    (*bandData_)[10] = density;
    (*bandData_)[11] = windSpeed;
    (*bandData_)[12] = windX;
    (*bandData_)[13] = windZ;
    (*bandData_)[14] = timeSec;
    (*bandData_)[15] = cloudEmitL;
    (*bandData_)[88] = cloudType;   // r10: shader 云型参数表索引 (band5.x)
    bandData_->dirty();
}

// ===========================================================================
// #4 海面逐像素波场: CPU WaveField 谐波表快照 → UBO oceanWave/oceanOpt 段
// (数值去重)。布局与 band_shade.comp BandUbo 对齐:
//   [16..79] = oceanWave[16] (x=L y=A z=cosθ w=sinθ)
//   [80]     = Gerstner Q   [81] = waveCount   [82] = 像素角 rad/px  [83] = pad
// Q<=0 / waveCount<=0 → shader 波累加早退, 海面细节恒等禁用。
// ===========================================================================
void BandRT::setOceanWave(const float* wave64, int waveCount, float q) {
    if (!bandData_ || !wave64) return;
    const int n = std::clamp(waveCount, 0, 16);
    float cache[66];
    std::memcpy(cache, wave64, sizeof(float) * 64);
    cache[64] = float(n);
    cache[65] = q + pxAngRad_;   // pxAng 并入去重键 (build 期常量, 首帧即生效)
    if (std::memcmp(waveCache_, cache, sizeof(cache)) == 0) return;   // 数值去重
    std::memcpy(waveCache_, cache, sizeof(cache));
    std::memcpy(&(*bandData_)[16], wave64, sizeof(float) * 64);
    (*bandData_)[80] = (n > 0 ? q : 0.0f);
    (*bandData_)[81] = float(n);
    (*bandData_)[82] = pxAngRad_;   // 像素角 rad/px (shader 视距滤波真值)
    bandData_->dirty();
}

// ===========================================================================
// P0 回读 (调试/验收)
// ===========================================================================
bool BandRT::dumpP0(vsg::Window& window, const char* path) {
    if (!p0 || !p0->image) {
        std::fprintf(stderr, "[BandRT] dumpP0: P0 图像为空\n");
        return false;
    }
    const VkExtent3D ext = p0->image->extent;
    const VkDeviceSize bytes = VkDeviceSize(ext.width) * ext.height * 4 * sizeof(float);

    auto device = window.getOrCreateDevice();
    const int gfxFamily = device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    auto queue = device->getQueue(uint32_t(gfxFamily));

    // host visible 接收缓冲 (compile 创建 VkBuffer → DeviceMemory → bind)
    auto buf = vsg::Buffer::create(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE);
    buf->compile(device.get());
    auto mem = vsg::DeviceMemory::create(device.get(), buf->getMemoryRequirements(device->deviceID),
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf->bind(mem, 0);

    // 一次性 command buffer: compute写→传输读 barrier + copyImageToBuffer
    auto pool = vsg::CommandPool::create(device, uint32_t(gfxFamily));
    auto cb = pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb->vk(), &bi);

    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb->vk(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    VkBufferImageCopy region{};
    region.imageSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = ext;
    vkCmdCopyImageToBuffer(cb->vk(), p0->image->vk(device->deviceID), VK_IMAGE_LAYOUT_GENERAL,
                           buf->vk(device->deviceID), 1, &region);
    vkEndCommandBuffer(cb->vk());

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    VkCommandBuffer vkCb = cb->vk();
    si.pCommandBuffers = &vkCb;
    auto fence = vsg::Fence::create(device);
    if (queue->submit(si, fence.get()) != VK_SUCCESS || fence->wait(UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "[BandRT] dumpP0: 提交失败\n");
        return false;
    }

    void* mapped = nullptr;
    if (mem->map(0, bytes, 0, &mapped) != VK_SUCCESS || !mapped) {
        std::fprintf(stderr, "[BandRT] dumpP0: map 失败\n");
        return false;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[BandRT] dumpP0: 无法打开 %s\n", path);
        mem->unmap();
        return false;
    }
    std::fwrite(mapped, 1, size_t(bytes), f);
    std::fclose(f);
    mem->unmap();
    std::printf("[BandRT] P0 回读: %s (%ux%u RGBA32F)\n", path, ext.width, ext.height);
    return true;
}

} // namespace sensor
} // namespace rockyenv
