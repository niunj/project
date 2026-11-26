#include "SensorShaderCache.h"
/// @file SensorPipeline.cpp —— GPU RTT 级联编排: 蓝方物理辐射 → 红方信号级 → 显示。
///
/// 级联 (全离屏 RGBA32F, GPU 直到出口, 零回传):
///   P0 rad (传感器视角辐射亮度, 传感器分辨率)
///   vis (观察口可见光近似, 窗口分辨率, 与 P0 并列)
///   P2 optics → P3 detector(raw) → P4 electronics(image), 均传感器分辨率
///   display: 左观察口/右传感器口 → swapchain
///
/// VSG 实现要点 (全部踩过坑):
///   - 离屏 Image 必须显式 usage=COLOR_ATTACHMENT|SAMPLED, 经
///     createImageView(device,...) 分配显存; RenderPass finalLayout=
///     SHADER_READ_ONLY_OPTIMAL 供下级采样。
///   - 管线不写 ViewportState → RenderGraph(DYNAMIC_VIEWPORTSTATE) 按
///     renderArea 同步动态视口 (与 env 模块/window 管线同一机制)。
///   - 视图参数 112B (mat4+3×vec4) 使用独立 set1 UBO；避免 VSG 自动矩阵 push 覆盖。
///   - LUT 纹理 usage 必须含 TRANSFER_DST, 否则 VSG 上传跳过 → 采样恒 0
///     (与 AtmosphereLUT 同一坑)。
#include "EnvCommon.h"          // resolveEnvShader (src/ 私有)
#include "rocky_env/SensorPipeline.h"
#include "rocky_env/BandRT.h"
#include "rocky_env/GBufferPool.h"
#include "rocky_env/SharedSceneResources.h"

#include <vsg/all.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace rockyenv {
namespace sensor {

// ===========================================================================
// 程序化热场景: 地面 290K / 坦克 305K / 引擎舱 450K / 尾焰 700K
// (MWIR: 引擎/尾焰极亮; LWIR: 常温目标清晰 —— 多波段差异验证锚点)
// ===========================================================================
namespace {

struct SceneBuilder {
    std::vector<vsg::vec3> pos;
    std::vector<vsg::vec2> props;
    std::vector<uint16_t> idx;

    void quad(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c,
              const vsg::vec3& d, float T, float eps) {
        const auto base = static_cast<uint16_t>(pos.size());
        vsg::vec3 corners[4] = {a, b, c, d};
        for (int i = 0; i < 4; ++i) {
            pos.push_back(corners[i]);
            props.push_back(vsg::vec2(T, eps));
        }
        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 3);
    }
    void box(const vsg::vec3& c, const vsg::vec3& h, float T, float eps) {
        const float x0 = c.x - h.x, x1 = c.x + h.x;
        const float y0 = c.y - h.y, y1 = c.y + h.y;
        const float z0 = c.z - h.z, z1 = c.z + h.z;
        quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}, T, eps);   // 底
        quad({x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}, T, eps);   // 顶
        quad({x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0}, T, eps);   // -z
        quad({x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}, T, eps);   // +z
        quad({x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}, T, eps);   // -x
        quad({x1,y0,z0},{x1,y0,z1},{x1,y1,z1},{x1,y1,z0}, T, eps);   // +x
    }
};

} // namespace

vsg::ref_ptr<vsg::Node> createThermalScene() {
    SceneBuilder sb;
    sb.quad({-100.0f,0,-100},{100.0f,0,-100},{100.0f,0,100},{-100.0f,0,100}, 290.0f, 0.92f);
    sb.box({0.0f, 1.0f, -40.0f},  {3.0f, 1.0f, 1.75f}, 305.0f, 0.93f);   // 坦克
    sb.box({-2.6f,1.1f, -40.0f},  {0.8f, 0.5f, 0.7f}, 450.0f, 0.90f);    // 引擎舱
    sb.box({-3.9f,1.0f, -40.0f},  {0.5f, 0.35f,0.4f}, 700.0f, 0.85f);    // 尾焰

    auto posArr = vsg::vec3Array::create(sb.pos.size());
    auto propsArr = vsg::vec2Array::create(sb.props.size());
    auto matArr = vsg::uintArray::create(sb.pos.size());
    auto uvArr = vsg::vec2Array::create(sb.pos.size());
    auto slotArr = vsg::uintArray::create(sb.pos.size());
    auto idxArr = vsg::ushortArray::create(sb.idx.size());
    std::memcpy(posArr->dataPointer(), sb.pos.data(), sb.pos.size() * sizeof(vsg::vec3));
    std::memcpy(propsArr->dataPointer(), sb.props.data(), sb.props.size() * sizeof(vsg::vec2));
    // #2: mat/uv/slot 全 0 (程序化场景无贴图; 顶点契约 binding 0..4 必须齐,
    //     否则 binding 错位 —— binding3=uv/binding4=slot)
    std::memset(matArr->dataPointer(), 0, sb.pos.size() * sizeof(uint32_t));
    std::memset(uvArr->dataPointer(), 0, sb.pos.size() * sizeof(vsg::vec2));
    std::memset(slotArr->dataPointer(), 0, sb.pos.size() * sizeof(uint32_t));
    std::memcpy(idxArr->dataPointer(), sb.idx.data(), sb.idx.size() * sizeof(uint16_t));

    auto draw = vsg::VertexIndexDraw::create();
    // binding0=pos, binding1=props, binding2=mat_idx, binding3=uv, binding4=tex_slot
    draw->assignArrays({posArr, propsArr, matArr, uvArr, slotArr});
    draw->assignIndices(idxArr);
    draw->indexCount = static_cast<uint32_t>(sb.idx.size());
    draw->instanceCount = 1;
    return draw;
}

// ===========================================================================
// 内部辅助
// ===========================================================================
namespace {

constexpr uint32_t kUboFloats = 56;      // 224 B, trailing thermal state used by raster IR

bool loadStages(const char* vertName, const char* fragName,
                vsg::ref_ptr<vsg::ShaderStage>& vs, vsg::ref_ptr<vsg::ShaderStage>& fs,
                const std::string& defines = {}) {
    vs = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT, "main", resolveEnvShader(vertName),vsg::Options::create());
    fs = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT, "main", resolveEnvShader(fragName),vsg::Options::create());
    if (!vs || !fs) {
        std::fprintf(stderr, "[SensorPipeline] shader 加载失败: %s / %s\n", vertName, fragName);
        return false;
    }
    if(!defines.empty())fs->module->source.insert(fs->module->source.find('\n',fs->module->source.find("#version"))+1,defines);
    vsg::ShaderStages stages{vs, fs};
    if (!compileSensorStages(stages)) {
        std::fprintf(stderr, "[SensorPipeline] shader 编译失败: %s / %s\n", vertName, fragName);
        return false;
    }
    return true;
}

/// 采样器 (clamp + linear)
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

/// 离屏颜色目标: RGBA32F color attachment + sampled
vsg::ref_ptr<vsg::ImageView> makeColorTarget(vsg::Device* device, uint32_t w, uint32_t h) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    img->extent = VkExtent3D{w, h, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device, img, VK_IMAGE_ASPECT_COLOR_BIT);
}
vsg::ref_ptr<vsg::ImageView> makeDepthTarget(vsg::Device* device,uint32_t width,uint32_t height) {
    auto image=vsg::Image::create();image->imageType=VK_IMAGE_TYPE_2D;image->format=VK_FORMAT_D32_SFLOAT;
    image->extent={width,height,1};image->mipLevels=1;image->arrayLayers=1;image->samples=VK_SAMPLE_COUNT_1_BIT;
    image->tiling=VK_IMAGE_TILING_OPTIMAL;image->usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image->initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device,image,VK_IMAGE_ASPECT_DEPTH_BIT);
}

vsg::AttachmentDescription colorAttachment(VkFormat fmt) {
    vsg::AttachmentDescription a{};
    a.format = fmt;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return a;
}

/// 外部依赖: 上级 color 输出 → 本级 fragment 采样 (级联同步)
vsg::SubpassDependency feedForwardDependency() {
    vsg::SubpassDependency d{};
    d.srcSubpass = VK_SUBPASS_EXTERNAL;
    d.dstSubpass = 0;
    d.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    d.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    d.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    d.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    return d;
}

/// 场景管线 (rad/vis 共用顶点输入; frag 不同)
vsg::ref_ptr<vsg::StateGroup> buildScenePipeline(
        const char* fragName, vsg::PipelineLayout* layout,
        vsg::ref_ptr<vsg::BindDescriptorSet> bindDesc,
        vsg::ref_ptr<SensorPushValue> pushValue,
        vsg::ref_ptr<vsg::ViewportState> viewport,const std::string& defines = {}) {
    vsg::ref_ptr<vsg::ShaderStage> vs, fs;
    if (!loadStages("sensor_rad.vert", fragName, vs, fs,defines)) return {};

    auto vis = vsg::VertexInputState::create();
    // 顶点契约 (与 SceneSource/createThermalScene 5 数组一致):
    //   binding0=pos(vec3) 1=props(vec2) 2=mat_idx(uint, 管线不读) 3=uv(vec2) 4=slot(uint)
    vis->vertexBindingDescriptions.resize(5);
    vis->vertexBindingDescriptions[0].binding = 0;
    vis->vertexBindingDescriptions[0].stride = sizeof(vsg::vec3);
    vis->vertexBindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vis->vertexBindingDescriptions[1].binding = 1;
    vis->vertexBindingDescriptions[1].stride = sizeof(vsg::vec2);
    vis->vertexBindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vis->vertexBindingDescriptions[2].binding = 2;
    vis->vertexBindingDescriptions[2].stride = sizeof(uint32_t);
    vis->vertexBindingDescriptions[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vis->vertexBindingDescriptions[3].binding = 3;
    vis->vertexBindingDescriptions[3].stride = sizeof(vsg::vec2);
    vis->vertexBindingDescriptions[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vis->vertexBindingDescriptions[4].binding = 4;
    vis->vertexBindingDescriptions[4].stride = sizeof(uint32_t);
    vis->vertexBindingDescriptions[4].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vis->vertexAttributeDescriptions.resize(5);
    vis->vertexAttributeDescriptions[0].location = 0;
    vis->vertexAttributeDescriptions[0].binding = 0;
    vis->vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[0].offset = 0;
    vis->vertexAttributeDescriptions[1].location = 3;
    vis->vertexAttributeDescriptions[1].binding = 1;
    vis->vertexAttributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    vis->vertexAttributeDescriptions[1].offset = 0;
    // #2: loc4=uv / loc5=slot (sensor_rad.vert 输出, sensor_vis.frag 消费)
    vis->vertexAttributeDescriptions[2].location = 4;
    vis->vertexAttributeDescriptions[2].binding = 3;
    vis->vertexAttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vis->vertexAttributeDescriptions[2].offset = 0;
    vis->vertexAttributeDescriptions[3].location = 5;
    vis->vertexAttributeDescriptions[3].binding = 4;
    vis->vertexAttributeDescriptions[3].format = VK_FORMAT_R32_UINT;
    vis->vertexAttributeDescriptions[3].offset = 0;
    vis->vertexAttributeDescriptions[4] = {6,2,VK_FORMAT_R32_UINT,0};

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE;
    auto bs = vsg::ColorBlendState::create();
    bs->attachments.resize(1);
    bs->attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = VK_TRUE;
    ds->depthWriteEnable = VK_TRUE;
    ds->depthCompareOp = VK_COMPARE_OP_GREATER; // VSG perspective is reverse-Z: near=1, far=0.

    vsg::GraphicsPipelineStates gp_states{vis, ias, rs, bs, ds};
    if (viewport) gp_states.push_back(viewport);
    auto gp = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vs, fs}, gp_states);
    auto sg = vsg::StateGroup::create();
    sg->add(vsg::BindGraphicsPipeline::create(gp));
    if (bindDesc) sg->add(bindDesc);
    if (pushValue)
        sg->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layout,1,
            vsg::Descriptors{vsg::DescriptorBuffer::create(pushValue,0)}));
    return sg;
}

/// 全屏链管线 (chain/display): 无顶点输入, 3 顶点大三角
vsg::ref_ptr<vsg::StateGroup> buildChainPipeline(
        const char* vertName, const char* fragName, vsg::PipelineLayout* layout,
        vsg::ref_ptr<vsg::BindDescriptorSet> bindDesc,
        vsg::ref_ptr<vsg::ViewportState> viewport,const std::string& defines = {}) {
    vsg::ref_ptr<vsg::ShaderStage> vs, fs;
    if (!loadStages(vertName, fragName, vs, fs,defines)) return {};

    auto vis = vsg::VertexInputState::create();   // 无绑定
    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE;
    auto bs = vsg::ColorBlendState::create();
    bs->attachments.resize(1);
    bs->attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = VK_FALSE;
    ds->depthWriteEnable = VK_FALSE;

    vsg::GraphicsPipelineStates gp_states{vis, ias, rs, bs, ds};
    if (viewport) gp_states.push_back(viewport);
    auto gp = vsg::GraphicsPipeline::create(layout, vsg::ShaderStages{vs, fs}, gp_states);
    auto sg = vsg::StateGroup::create();
    sg->add(vsg::BindGraphicsPipeline::create(gp));
    if (bindDesc) sg->add(bindDesc);
    sg->addChild(vsg::Draw::create(3, 1, 0, 0));
    return sg;
}

vsg::ref_ptr<vsg::DescriptorImage> makeSampledImage(vsg::ref_ptr<vsg::Sampler> sampler,
                                                    vsg::ref_ptr<vsg::ImageView> view,
                                                    uint32_t binding) {
    auto info = vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return vsg::DescriptorImage::create(info, binding);
}

vsg::ref_ptr<vsg::RenderGraph> makeOffscreenRG(vsg::ref_ptr<vsg::Framebuffer> fbo) {
    auto rg = vsg::RenderGraph::create();
    rg->framebuffer = fbo;
    rg->renderArea = VkRect2D{VkOffset2D{0, 0}, fbo->extent2D()};
    // 离屏 RG 默认构造不带 viewportState: DYNAMIC 视口机制下管线拿不到视口
    // → 不绘制 (全黑)。显式指定与 renderArea 同步的 ViewportState。
    rg->viewportState = vsg::ViewportState::create(fbo->extent2D());
    rg->setClearValues(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}},
                       VkClearDepthStencilValue{0.0f, 0});
    return rg;
}

/// 1×1 RGBA32F 占位 (观察口 skyTex 在无 RT 模块时绑定; skyValid=0 时不采样)
vsg::ref_ptr<vsg::ImageView> makeDummySkyView(vsg::ref_ptr<vsg::vec4Array2D> data = {}) {
    if(!data){data = vsg::vec4Array2D::create(1, 1);(*data)(0, 0) = vsg::vec4(0.0f, 0.0f, 0.0f, 0.0f);}
    data->properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_R32G32B32A32_SFLOAT;
    img->extent = VkExtent3D{data->width(), data->height(), 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img->data = data;
    auto view = vsg::ImageView::create(img);
    view->viewType = VK_IMAGE_VIEW_TYPE_2D;
    return view;
}

/// 1×1 占位 albedo 数组 (池无贴图; hasTex=0 → shader 走 Kd 分支)
vsg::ref_ptr<vsg::ImageView> makeDummyTexArrayView() {
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
// SensorPipeline::build
// ===========================================================================
SensorPipeline::SensorPipeline() = default;
SensorPipeline::~SensorPipeline() = default;

void SensorPipeline::updateSkyMap(const float* data,uint32_t width,uint32_t height) {
    if(bandRT_){bandRT_->updateSkyMap(data,width,height);return;}
    if(!tvBand_||!rasterSky_)return;
    if(!data||width!=rasterSky_->width()||height!=rasterSky_->height())throw std::invalid_argument("raster TV spectral sky requires a 256x128 four-channel map");
    const size_t count=size_t(width)*height*4;
    for(size_t index=0;index<count;++index)if(!std::isfinite(data[index])||data[index]<0)throw std::invalid_argument("spectral sky radiance must be finite and nonnegative");
    if(std::memcmp(rasterSky_->dataPointer(),data,count*sizeof(float))!=0){std::memcpy(rasterSky_->dataPointer(),data,count*sizeof(float));rasterSky_->dirty();}
}

void SensorPipeline::setEnv(const EnvAnswer& env) {
    double sun[3];dirFromAzEl(env.sun_az_deg,env.sun_el_deg,sun);setSunDir({sun[0],sun[1],sun[2]});
    if (bandRT_) {
        // P0.3: RT 模式 → 全部转发共享/本地 env UBO (rchit/rmiss 消费);
        // 已核实 env 段仅 sensor_rad.frag (光栅回退路径) 消费, RT 模式跳过
        // 192B UBO [20..23] 更新安全 —— 消除每帧冗余 memcpy。
        bandRT_->setEnv(env);
        return;
    }
    if (ubo) {
        (*ubo)[20] = float(env.sigma_per_km);   // env: σ/km
        (*ubo)[21] = float(env.sky_radiance);   // 天空辐亮度 (miss/vis 背景)
        (*ubo)[22] = float(env.sun_irradiance); // 直射辐照度
        (*ubo)[23] = 0.0f;
        // 批次1: env2 = 太阳 ENU 指向 [44..47] (sensor_rad.frag 光栅回退链太阳反射)
        double dir[3];
        dirFromAzEl(env.sun_az_deg, env.sun_el_deg, dir);
        (*ubo)[44] = float(dir[0]);
        (*ubo)[45] = float(dir[1]);
        (*ubo)[46] = float(dir[2]);
        (*ubo)[47] = 0.0f;
        (*ubo)[48] = float(env.thermal_local_hour);
        (*ubo)[49] = float(env.thermal_air_delta_k);
        (*ubo)[50] = float(env.thermal_wetness);
        (*ubo)[51] = float(env.thermal_scene_seconds);
        (*ubo)[52] = float(env.cloud_coverage);
        (*ubo)[54] = float(env.is_day);
        ubo->dirty();   // DYNAMIC_DATA: modifiedCount 变化触发 record 时上传
    }
}

SensorPipeline::BuildResult SensorPipeline::build(vsg::ref_ptr<vsg::Window> window,
                                                  const SensorProfile& profile,
                                                  const BandRadianceLut& lut,
                                                  const EnvAnswer& envAnswer,
                                                  vsg::ref_ptr<vsg::Node> scene,
                                                  const std::vector<RadarMaterial>* rtMaterials,
                                                  bool allowRT,
                                                  SharedSceneResources* shared,
                                                  GBufferPool* gbuffer) {
    return buildImpl(window->getOrCreateDevice(),window,profile,lut,envAnswer,scene,rtMaterials,allowRT,shared,gbuffer);
}
SensorPipeline::BuildResult SensorPipeline::buildOffscreen(vsg::Device& device,
        const SensorProfile& profile,const BandRadianceLut& lut,const EnvAnswer& environment,
        vsg::ref_ptr<vsg::Node> scene,SharedSceneResources* shared,GBufferPool* gbuffer) {
    return buildOffscreen(device,profile,lut,environment,scene,shared,gbuffer,true);
}
SensorPipeline::BuildResult SensorPipeline::buildOffscreen(vsg::Device& device,
        const SensorProfile& profile,const BandRadianceLut& lut,const EnvAnswer& environment,
        vsg::ref_ptr<vsg::Node> scene,SharedSceneResources* shared,GBufferPool* gbuffer,bool allowRT) {
    return buildImpl(vsg::ref_ptr<vsg::Device>(&device),{},profile,lut,environment,scene,nullptr,allowRT,shared,gbuffer);
}
SensorPipeline::BuildResult SensorPipeline::buildImpl(vsg::ref_ptr<vsg::Device> device,
        vsg::ref_ptr<vsg::Window> window,const SensorProfile& profile,const BandRadianceLut& lut,
        const EnvAnswer& envAnswer,vsg::ref_ptr<vsg::Node> scene,const std::vector<RadarMaterial>* rtMaterials,
        bool allowRT,SharedSceneResources* shared,GBufferPool* gbuffer) {
    BuildResult result;
    const VkFormat colorFmt = VK_FORMAT_R32G32B32A32_SFLOAT;
    const VkExtent2D winExt = window ? window->extent2D() : VkExtent2D{profile.width,profile.height};
    sensorW_ = profile.width;
    sensorH_ = profile.height;
    tvBand_=profile.band.type==BandType::TV;
    bandRT_.reset();rasterSky_={};

    // ---- B2/P1.1: P0 光源 = band_shade compute 着色 (求交收敛至 GBufferPool),
    //      构建失败回退光栅化 rad pass ----
    // allowRT=false (如 --no-rt 或无 RT 扩展窗口) 直接跳过, 避免在无 RT 特性
    // 设备上创建 G-buffer RT 管线崩溃 (host 侧 glslang 编译成功 ≠ 设备支持)。
    bool useRT = false;
    if (allowRT) {
        bandRT_ = std::make_unique<BandRT>();
        auto rtRes = bandRT_->build(*device, profile, lut, envAnswer, gbuffer, shared);
        useRT = rtRes.ok;
        if (!useRT) {
            if (!window) { result.error = "offscreen BandRT: " + rtRes.error; return result; }
            // Raster depth resolves visibility; ray-traced shadows and secondary
            // rays remain capabilities of the G-buffer/RT path.
            std::fprintf(stderr, "[SensorPipeline] BandRT 失败 (%s) → 光栅化回退 "
                                 "(P0 深度遮挡; 无光追阴影/二次射线)\n",
                         rtRes.error.c_str());
            bandRT_.reset();
        }
    }

    // ---- UBO (56 floats, legacy fields keep their offsets) ----
    ubo = vsg::floatArray::create(kUboFloats);
    ubo->properties.dataVariance = vsg::DYNAMIC_DATA;
    for (auto& f : *ubo) f = 0.0f;
    (*ubo)[16] = 0.5f;                                        // cam.near
    (*ubo)[17] = 2000.0f;                                     // cam.far
    (*ubo)[18] = float(std::tan(profile.fov_deg * M_PI / 180.0 / 2.0) * (double(profile.height) / double(profile.width)));
    (*ubo)[19] = float(double(profile.width) / double(profile.height));
    (*ubo)[20] = float(envAnswer.sigma_per_km);               // env
    (*ubo)[21] = float(envAnswer.sky_radiance);
    (*ubo)[22] = float(envAnswer.sun_irradiance);
    (*ubo)[23] = 0.0f;
    {   // 批次1: env2 = 太阳 ENU 指向 [44..47] (光栅回退链太阳反射)
        double dir[3];
        dirFromAzEl(envAnswer.sun_az_deg, envAnswer.sun_el_deg, dir);
        (*ubo)[44] = float(dir[0]);
        (*ubo)[45] = float(dir[1]);
        (*ubo)[46] = float(dir[2]);
        (*ubo)[47] = 0.0f;
    }
    (*ubo)[48] = float(envAnswer.thermal_local_hour);
    (*ubo)[49] = float(envAnswer.thermal_air_delta_k);
    (*ubo)[50] = float(envAnswer.thermal_wetness);
    (*ubo)[51] = float(envAnswer.thermal_scene_seconds);
    (*ubo)[52] = float(envAnswer.cloud_coverage);
    (*ubo)[53] = float(profile.band.type == BandType::MWIR ? 2 : profile.band.type == BandType::LWIR ? 3 : 0);
    (*ubo)[54] = float(envAnswer.is_day);
    (*ubo)[24] = lut.tMin();                                  // band
    (*ubo)[25] = lut.tMax();
    (*ubo)[26] = float((profile.band.lambda_min + profile.band.lambda_max) * 0.5 * 1e6);
    (*ubo)[27] = 300.0f;                                      // T_ref (NETD 基准)
    (*ubo)[28] = float(profile.optics.f_number);              // optics
    (*ubo)[29] = float(profile.optics.transmission);
    (*ubo)[30] = float(profile.optics.psf_sigma_px);
    (*ubo)[31] = float(profile.optics.vignetting);
    (*ubo)[32] = float(profile.detector.qe);                  // det
    (*ubo)[33] = float(profile.detector.integration_ms * 1e-3);
    (*ubo)[34] = float(profile.detector.netd_mk * 1e-3);      // mK → K
    (*ubo)[35] = float(profile.detector.well_ke);
    (*ubo)[36] = float(profile.detector.read_noise_e);        // det2
    (*ubo)[37] = float(profile.detector.pixel_pitch_um);
    (*ubo)[38] = float(profile.detector.bits);
    (*ubo)[39] = float(profile.electronics.agc_low_pct);
    (*ubo)[40] = float(profile.electronics.agc_high_pct);     // elec
    (*ubo)[41] = float(profile.electronics.gamma);
    (*ubo)[42] = float(sensorW_);
    (*ubo)[43] = float(sensorH_);

    // ---- LUT 纹理 (512×1 R32_SFLOAT, TRANSFER_DST 上传) ----
    auto lutImg = vsg::Image::create();
    lutImg->imageType = VK_IMAGE_TYPE_2D;
    lutImg->format = VK_FORMAT_R32_SFLOAT;
    lutImg->extent = VkExtent3D{512, 1, 1};
    lutImg->mipLevels = 1;
    lutImg->arrayLayers = 1;
    lutImg->samples = VK_SAMPLE_COUNT_1_BIT;
    lutImg->tiling = VK_IMAGE_TILING_OPTIMAL;
    lutImg->usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto lutData = vsg::floatArray2D::create(512, 1);
    lutData->properties.format = VK_FORMAT_R32_SFLOAT;
    const auto& table = lut.table();
    for (uint32_t i = 0; i < 512 && i < table.size(); ++i) (*lutData)(i, 0) = table[i];
    lutImg->data = lutData;
    auto lutView = vsg::ImageView::create(lutImg);
    lutView->viewType = VK_IMAGE_VIEW_TYPE_2D;
    auto sampler = makeSampler();

    // ---- 描述符布局 ----
    auto dslScene = vsg::DescriptorSetLayout::create();   // rad: UBO + LUT
    dslScene->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslScene->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslScene->addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto dslChain = vsg::DescriptorSetLayout::create();   // optics/elec: UBO + in
    dslChain->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslChain->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto dslDet = vsg::DescriptorSetLayout::create();     // det: UBO + in + LUT
    dslDet->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslDet->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslDet->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto dslDisp = vsg::DescriptorSetLayout::create();    // display: vis + sensor
    dslDisp->addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslDisp->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    auto dslView=vsg::DescriptorSetLayout::create();
    dslView->addBinding(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT);

    // ---- Per-view dynamic UBO values (legacy member names retained) ----
    sensorPush_ = SensorPushValue::create();
    observerPush_ = SensorPushValue::create();
    sensorPush_->properties.dataVariance=vsg::DYNAMIC_DATA;
    observerPush_->properties.dataVariance=vsg::DYNAMIC_DATA;
    std::memset(&sensorPush_->value(), 0, sizeof(SensorPushBlock));
    std::memset(&observerPush_->value(), 0, sizeof(SensorPushBlock));
    setSunDir({(*ubo)[44],(*ubo)[45],(*ubo)[46]});

    // ---- RenderPass / Framebuffer ----
    const vsg::SubpassDependency feed = feedForwardDependency();
    // Scene passes resolve the nearest surface in a dedicated depth attachment;
    // signal-chain fullscreen passes retain their color-only render pass.
    auto offColorAtt = colorAttachment(colorFmt);
    vsg::SubpassDescription offSub{};
    offSub.colorAttachments = std::vector<vsg::AttachmentReference>{{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    const vsg::RenderPass::Attachments offAtts{offColorAtt};
    const vsg::RenderPass::Subpasses offSubs{offSub};
    const vsg::RenderPass::Dependencies offDeps{feed};
    auto rpOff = vsg::RenderPass::create(device, offAtts, offSubs, offDeps);
    auto depthAtt=offColorAtt;depthAtt.format=VK_FORMAT_D32_SFLOAT;
    depthAtt.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    auto sceneSub=offSub;sceneSub.depthStencilAttachments={{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    auto rpScene=vsg::RenderPass::create(device,vsg::RenderPass::Attachments{offColorAtt,depthAtt},vsg::RenderPass::Subpasses{sceneSub},offDeps);

    auto radColor = makeColorTarget(device, sensorW_, sensorH_);
    auto visColor = makeColorTarget(device, winExt.width, winExt.height);
    auto opticsColor = makeColorTarget(device, sensorW_, sensorH_);
    auto detColor = makeColorTarget(device, sensorW_, sensorH_);
    auto elecColor = makeColorTarget(device, sensorW_, sensorH_);
    auto agcBounds = makeColorTarget(device, 1, 1);
    // B5 产品级回读: 保留级目标引用 (P0 光栅回退 / P2 / P3 / P4)
    radColor_ = radColor;
    opticsColor_ = opticsColor;
    detColor_ = detColor;
    elecColor_ = elecColor;
    p0FromRT_ = useRT;

    auto fboRad = vsg::Framebuffer::create(rpScene, vsg::ImageViews{radColor,makeDepthTarget(device,sensorW_,sensorH_)}, sensorW_, sensorH_, 1u);
    auto fboVis = vsg::Framebuffer::create(rpScene, vsg::ImageViews{visColor,makeDepthTarget(device,winExt.width,winExt.height)}, winExt.width, winExt.height, 1u);
    auto fboOptics = vsg::Framebuffer::create(rpOff, vsg::ImageViews{opticsColor}, sensorW_, sensorH_, 1u);
    auto fboDet = vsg::Framebuffer::create(rpOff, vsg::ImageViews{detColor}, sensorW_, sensorH_, 1u);
    auto fboAgc = vsg::Framebuffer::create(rpOff, vsg::ImageViews{agcBounds}, 1u, 1u, 1u);
    auto fboElec = vsg::Framebuffer::create(rpOff, vsg::ImageViews{elecColor}, sensorW_, sensorH_, 1u);

    // ---- 描述符集 ----
    auto uboDesc = vsg::DescriptorBuffer::create(ubo, 0);
    auto lutDesc = makeSampledImage(sampler, lutView, 1);
    // P0 采样源 (B2): RT → bandRT->p0 (GENERAL 布局); 光栅 → radColor (SHADER_READ_ONLY)
    auto p0ImageInfo = useRT
        ? vsg::ImageInfo::create(sampler, bandRT_->p0, VK_IMAGE_LAYOUT_GENERAL)
        : vsg::ImageInfo::create(sampler, radColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto opticsSet = vsg::DescriptorSet::create(dslChain, vsg::Descriptors{
        uboDesc, vsg::DescriptorImage::create(p0ImageInfo, 1)});
    auto detSet = vsg::DescriptorSet::create(dslDet, vsg::Descriptors{uboDesc, makeSampledImage(sampler, opticsColor, 1), makeSampledImage(sampler, lutView, 2)});
    auto agcSet = vsg::DescriptorSet::create(dslChain, vsg::Descriptors{uboDesc, makeSampledImage(sampler, detColor, 1)});
    auto elecSet = vsg::DescriptorSet::create(dslDet, vsg::Descriptors{
        uboDesc, makeSampledImage(sampler, detColor, 1), makeSampledImage(sampler, agcBounds, 2)});
    auto dispSet = vsg::DescriptorSet::create(dslDisp, vsg::Descriptors{
        makeSampledImage(sampler, visColor, 1), makeSampledImage(sampler, elecColor, 2)});

    auto layoutRad = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslScene,dslView}, vsg::PushConstantRanges{});
    // #2 观察口: albedo 数组 + 贴图槽表 + RGB 天空图 (#1) + 体积云 UBO (#5)
    auto dslVis = vsg::DescriptorSetLayout::create();
    dslVis->addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslVis->addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslVis->addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    // #5: 旧 b3 云覆盖图删除 → b4 体积云 UBO (3 vec4, 与 band_shade BandUbo 云段同约定)
    dslVis->addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslVis->addBinding(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    dslVis->addBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto cloud=shared ? shared->voxelCloud : nullptr;
    if(cloud && cloud->resources->ownerDevice && cloud->resources->ownerDevice!=device.get()) {
        result.error="voxel cloud belongs to a different Vulkan device";return result;
    }
    vsg::DescriptorSetLayouts visLayouts{dslVis,dslView};
    if(cloud)visLayouts.push_back(cloud->layout);
    auto layoutVis = vsg::PipelineLayout::create(visLayouts, vsg::PushConstantRanges{});
    auto layoutOptics = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslChain,dslView}, vsg::PushConstantRanges{});
    auto layoutDet = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslDet,dslView}, vsg::PushConstantRanges{});
    auto layoutAgc = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslChain,dslView}, vsg::PushConstantRanges{});
    auto layoutElec = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslDet,dslView}, vsg::PushConstantRanges{});
    // All custom graphics stages use UBOs. No push range means VSG must not
    // inject its unrelated projection/modelview matrices into sensor parameters.
    auto layoutDisp = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dslDisp,dslView}, vsg::PushConstantRanges{});

    // #2/#3 观察口描述符: albedo/槽表取场景资源池, 天空图/云图取 BandRT (共享
    // 同一 GPU 纹理, 与 RT TV 链同源); RT 模块不存在 (光栅回退) 时绑 1×1 占位。
    vsg::ref_ptr<vsg::ImageView> visAlbedo;
    vsg::ref_ptr<vsg::BufferInfo> visSlotInfo;
    if (shared) {
        visAlbedo = shared->texArrayView;
        visSlotInfo = shared->texSlotDataInfo;
    }
    if (!visAlbedo) visAlbedo = makeDummyTexArrayView();
    if (!visSlotInfo) {
        auto defSlot = vsg::floatArray::create(4);
        float* d = defSlot->data(0);
        d[0] = 0.5f; d[1] = 0.5f; d[2] = 0.5f; d[3] = 0.0f;   // 灰 Kd, hasTex=0
        visSlotInfo = vsg::BufferInfo::create(defSlot);
    }
    if(!bandRT_&&tvBand_){
        rasterSky_=vsg::vec4Array2D::create(256,128);rasterSky_->properties.dataVariance=vsg::DYNAMIC_DATA;
        // Without a supplied spectral sky, retain a neutral isotropic radiance
        // with the declared physical magnitude, rather than invent display RGB.
        for(auto& pixel:*rasterSky_)pixel=vsg::vec4(float(envAnswer.sky_radiance),float(envAnswer.sky_radiance),float(envAnswer.sky_radiance),float(envAnswer.sky_radiance));
    }
    auto visSkyView = bandRT_ ? bandRT_->skyView() : makeDummySkyView(rasterSky_);
    auto visSkySampler = bandRT_ ? bandRT_->skySampler() : sampler;   // 占位时 clamp 即可
    vsg::ref_ptr<vsg::BufferInfo> spectral=shared?shared->spectralDataInfo:vsg::ref_ptr<vsg::BufferInfo>{};
    if(!spectral){
        const auto count=rtMaterials&&!rtMaterials->empty()?rtMaterials->size():size_t(1);
        auto data=vsg::floatArray::create(count*16);for(auto& value:*data)value=0;
        for(size_t index=0;index<count;++index){
            auto* fields=data->data(index*16);fields[0]=fields[2]=fields[3]=-1;fields[5]=.5;
            if(rtMaterials&&!rtMaterials->empty()){
                const auto& material=(*rtMaterials)[index];
                fields[0]=material.rho_tv>=0?material.rho_tv:(material.emissivity>0?1-material.emissivity:-1);
                fields[2]=material.eps_mwir;fields[3]=material.eps_lwir;
                fields[4]=material.spec_strength;fields[5]=material.spec_roughness;fields[6]=material.dielectric_model==1?1:0;
                std::copy(material.thermal_surface.begin(),material.thermal_surface.end(),fields+8);
                std::copy(material.thermal_source.begin(),material.thermal_source.end(),fields+12);
            }
        }
        spectral=vsg::BufferInfo::create(data);
    }
    auto radSet = vsg::DescriptorSet::create(dslScene, vsg::Descriptors{uboDesc, lutDesc,
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{spectral},2,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto bindRad = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layoutRad,0,radSet);
    // #5 体积云 UBO (12 float = 3 vec4; setCloudVolume 每帧数值去重注入)
    uboVolume_ = vsg::floatArray::create(12);
    uboVolume_->properties.dataVariance = vsg::DYNAMIC_DATA;
    for (auto& f : *uboVolume_) f = 0.0f;
    auto visSet = vsg::DescriptorSet::create(dslVis, vsg::Descriptors{
        makeSampledImage(sampler, visAlbedo, 0),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{visSlotInfo}, 1, 0,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeSampledImage(visSkySampler, visSkyView, 2),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(uboVolume_)}, 4),
        vsg::DescriptorBuffer::create(ubo,5),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{spectral},6,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto bindVis = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutVis, 0, visSet);
    auto bindOptics = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutOptics, 0, opticsSet);
    auto bindDet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutDet, 0, detSet);
    auto bindAgc = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutAgc, 0, agcSet);
    auto bindElec = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutElec, 0, elecSet);
    auto bindDisp = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, layoutDisp, 0, dispSet);

    // ---- 管线 / RenderGraph ----
    // 静态 ViewportState: 本 VSG 版本下 RenderGraph.viewportState 的 DYNAMIC 视口
    // 推送不生效 (管线无 ViewportState → vkCreateGraphicsPipelines 视口为空 →
    // 不绘制/全黑, xwd 已证实)。故管线内直接烘焙与 FBO 同尺寸的静态视口。
    const VkExtent2D senExt{sensorW_, sensorH_};
    const bool rasterTV=tvBand_&&!useRT;
    const std::string cloudDefines=cloud?"#define USE_VOXEL_CLOUD_FIELD 1\n":"";
    auto sgRad = rasterTV
        ? buildScenePipeline("sensor_vis.frag",layoutVis,bindVis,sensorPush_,vsg::ViewportState::create(senExt),cloudDefines+"#define SENSOR_RADIANCE_OUTPUT 1\n")
        : buildScenePipeline("sensor_rad.frag", layoutRad, bindRad, sensorPush_,vsg::ViewportState::create(senExt));
    auto sgSky=rasterTV?buildChainPipeline("sensor_chain.vert","sensor_vis.frag",layoutVis,bindVis,vsg::ViewportState::create(senExt),cloudDefines+"#define SENSOR_RADIANCE_OUTPUT 1\n#define SENSOR_SKY_BACKGROUND 1\n"):vsg::ref_ptr<vsg::StateGroup>{};
    if(sgSky)sgSky->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layoutVis,1,vsg::Descriptors{vsg::DescriptorBuffer::create(sensorPush_,0)}));
    auto sgVis = buildScenePipeline("sensor_vis.frag", layoutVis, bindVis, observerPush_,
                                    vsg::ViewportState::create(winExt),cloudDefines);
    auto sgOptics = buildChainPipeline("sensor_chain.vert", "sensor_optics.frag", layoutOptics, bindOptics,
                                       vsg::ViewportState::create(senExt));
    auto sgDet = buildChainPipeline("sensor_chain.vert", "sensor_detector.frag", layoutDet, bindDet,
                                    vsg::ViewportState::create(senExt));
    // Detector noise reads the exact measurement seed through the same view UBO.
    if (sgDet)
        sgDet->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layoutDet,1,
            vsg::Descriptors{vsg::DescriptorBuffer::create(sensorPush_,0)}));
    // Compute the shared percentile bounds once per exposure at full float precision.
    auto sgAgc = buildChainPipeline("sensor_chain.vert", "sensor_electronics.frag", layoutAgc, bindAgc,
                                    vsg::ViewportState::create(VkExtent2D{1,1}), "#define SENSOR_AGC_STATS 1\n");
    auto sgElec = buildChainPipeline("sensor_chain.vert", "sensor_electronics.frag", layoutElec, bindElec,
                                     vsg::ViewportState::create(senExt), "#define SENSOR_SHARED_AGC 1\n");
    auto sgDisp = buildChainPipeline("sensor_chain.vert", "sensor_display.frag", layoutDisp, bindDisp,
                                     vsg::ViewportState::create(winExt));
    if (!sgRad || (rasterTV&&!sgSky) || !sgVis || !sgOptics || !sgDet || !sgAgc || !sgElec || !sgDisp) {
        result.error = "pipeline build failed";
        return result;
    }
    if(cloud)for(const auto& group:{sgVis,sgSky,rasterTV?sgRad:vsg::ref_ptr<vsg::StateGroup>{}})
        if(group)group->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,layoutVis,2,cloud->descriptors));

    // ★ 根因修复: 场景几何必须挂在 StateGroup 内部 (push 与 draw 之间)。
    //   VSG 的 StateGroup::apply 是 push→traverse→pop 配对; 若 scene 与 sg 并列
    //   挂在 RG 上, pop 先于 scene 遍历发生, draw 时状态栈为空 → 管线/描述符
    //   不绑定 → GPU 沿用上一 pass (暖身 RG) 的 optics 管线 (readback 实锤:
    //   rad/vis 执行 optics 行为自反馈)。chain 管线 (det/elec/disp) 因 draw
    //   在 SG 内部而幸免。
    auto rgRad = makeOffscreenRG(fboRad);
    if(sgSky)rgRad->addChild(sgSky);
    sgRad->addChild(scene);
    rgRad->addChild(sgRad);
    // 背景像素 (无几何 → 无片元执行) 由 clear 值直接给出天空背景辐亮度
    // (rad.frag 的 r 通道 = 波段辐亮度 W/m2/sr; sky_radiance 由晴空模型给出)。
    rgRad->setClearValues(VkClearColorValue{{float(envAnswer.sky_radiance), 0.0f, 0.0f, 0.0f}},
                          VkClearDepthStencilValue{0.0f, 0});
    auto rgVis = makeOffscreenRG(fboVis);
    sgVis->addChild(scene);
    rgVis->addChild(sgVis);
    // 观察口背景 = 近天空蓝 (初始静态值; #2 起 setObserverEnv 每帧写天顶 RGB 动态 clear)
    rgVis->setClearValues(VkClearColorValue{{0.55f, 0.62f, 0.75f, 1.0f}},
                          VkClearDepthStencilValue{0.0f, 0});
    rgVis_ = rgVis;
    auto rgOptics = makeOffscreenRG(fboOptics);
    rgOptics->addChild(sgOptics);
    auto rgDet = makeOffscreenRG(fboDet);
    rgDet->addChild(sgDet);
    auto rgAgc = makeOffscreenRG(fboAgc);
    rgAgc->addChild(sgAgc);
    auto rgElec = makeOffscreenRG(fboElec);
    rgElec->addChild(sgElec);

    // 显示 pass: camera 带 ViewportState + view 挂入 RG (左观察口/右传感器口)
    auto dispProj = vsg::Perspective::create(30.0, 1.0, 0.1, 10.0);
    auto dispLookAt = vsg::LookAt::create(vsg::dvec3(0.0, 0.0, 5.0), vsg::dvec3(0.0, 0.0, 0.0),
                                          vsg::dvec3(0.0, 1.0, 0.0));
    auto dispCam = vsg::Camera::create(dispProj, dispLookAt,
                                       vsg::ViewportState::create(winExt));
    auto dispView = vsg::View::create(dispCam);
    dispView->addChild(sgDisp);
    auto rgDisp = window ? vsg::RenderGraph::create(window, dispView) : vsg::ref_ptr<vsg::RenderGraph>{};
    if(rgDisp) rgDisp->setClearValues(VkClearColorValue{{0.05f, 0.05f, 0.05f, 1.0f}},
                           VkClearDepthStencilValue{1.0f, 0});

    // ------------------------------------------------------------------
    // 暖身 RG: 本 VSG 版本 (1.1.15) + NVIDIA 下, scene 路径 GraphicsPipeline
    // (gpRad/gpVis, 带顶点输入) 若为 command buffer 中**首个**执行的
    // 管线, vkQueueSubmit 成功但 GPU 永不执行 (fence 超时, 二分 M3-M23 证实)。
    // chain 管线 (rgOptics) 先执行则一切正常 (M24/M25)。
    // 因此级联最前放一个 4×4 常量颜色 RP 暖管线，不采样尚未生成的 P0；时序保持
    // rad→vis→optics→det→elec→disp 不变 (输出写入无人采样的 dummyColor)。
    // ------------------------------------------------------------------
    auto dummyColor = makeColorTarget(device.get(), 4, 4);
    auto fboDummy = vsg::Framebuffer::create(rpOff, vsg::ImageViews{dummyColor}, 4, 4, 1u);
    auto rgWarmup = makeOffscreenRG(fboDummy);
    auto warmLayout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{},vsg::PushConstantRanges{});
    auto warmState=buildChainPipeline("sensor_chain.vert","sensor_warmup.frag",warmLayout,{},vsg::ViewportState::create(VkExtent2D{4,4}));
    if(!warmState){result.error="warmup pipeline build failed";return result;}
    rgWarmup->addChild(warmState);

    commandGraph = window ? vsg::CommandGraph::create(window) : vsg::CommandGraph::create(device,device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    if(window) commandGraph->addChild(rgWarmup);
    // P0 光源 (P1.1): G-buffer 唯一 TraceRays → band_shade compute 着色;
    // 或光栅回退 (G-buffer 池缺失/构建失败)
    if (useRT) {
        commandGraph->addChild(gbuffer->commands);   // ★ 全场景唯一 TraceRays (P1.1)
        commandGraph->addChild(bandRT_->commands);   // per-波段着色 compute
    } else
        commandGraph->addChild(rgRad);
    if(window) commandGraph->addChild(rgVis);
    commandGraph->addChild(rgOptics);
    commandGraph->addChild(rgDet);
    commandGraph->addChild(rgAgc);
    commandGraph->addChild(rgElec);
    if(rgDisp) commandGraph->addChild(rgDisp);

    result.ok = true;
    return result;
}

// ===========================================================================
// 每帧更新
// ===========================================================================
void SensorPipeline::setSensorView(const vsg::dmat4& proj, const vsg::dmat4& view,
                                   const vsg::dvec3& eye, double nearZ, double farZ,
                                   double vfovDeg, double aspect) {
    // P1.1: 视角收敛至 GBufferPool (band_shade 着色与视角无关), 本处不再转发
    if (!sensorPush_) return;
    auto& p = sensorPush_->value();
    p.vp = vsg::mat4(proj * view);
    p.cam = vsg::vec4(float(nearZ), float(farZ),
                      float(std::tan(vfovDeg * M_PI / 180.0 / 2.0)), float(aspect));
    p.eye = vsg::vec4(float(eye.x), float(eye.y), float(eye.z), 1.0f);
    sensorPush_->dirty();
}

void SensorPipeline::setObserverView(const vsg::dmat4& proj, const vsg::dmat4& view,
                                     const vsg::dvec3& eye) {
    if (!observerPush_) return;
    auto& p = observerPush_->value();
    p.vp = vsg::mat4(proj * view);
    p.eye = vsg::vec4(float(eye.x), float(eye.y), float(eye.z), 1.0f);
    observerPush_->dirty();
}

// ===========================================================================
// #2/#3 观察口环境注入: 天顶 RGB → rgVis 动态 clear 色; 云底/影强/TV esun/
// skyValid → 视图 UBO cam 复用槽 (frag 照明与 cloudShadow 消费)
// ===========================================================================
void SensorPipeline::setObserverEnv(const vsg::vec3& zenithSrgb, float cloudBaseM,
                                    float shadowStrength, float esun, bool skyValid) {
    if (observerPush_) {
        observerPush_->value().cam = vsg::vec4(skyValid ? 1.0f : 0.0f, cloudBaseM,shadowStrength,esun);
        observerPush_->dirty();
    }
    if (rgVis_)
        rgVis_->setClearValues(VkClearColorValue{{zenithSrgb.r, zenithSrgb.g, zenithSrgb.b, 1.0f}},
                               VkClearDepthStencilValue{0.0f, 0});
}

// ===========================================================================
// #5 观察口体积云注入: 3 vec4 → uboVolume_ (DYNAMIC_DATA, 数值去重)。
// 布局与 sensor_vis.frag VolUbo / band_shade.comp BandUbo 云段同约定:
//   a=(云底, 影强, 云顶, 覆盖率) b=(密度, 风速, 风向x, 风向z)
//   c=(时间, IR云发射, r10 云型 0-4, pad)
// ===========================================================================
void SensorPipeline::setCloudVolume(const vsg::vec4& a, const vsg::vec4& b, const vsg::vec4& c) {
    if (!uboVolume_) return;
    if (a == volCacheA_ && b == volCacheB_ && c == volCacheC_) return;   // 数值去重
    volCacheA_ = a; volCacheB_ = b; volCacheC_ = c;
    float* d = uboVolume_->data(0);
    d[0] = a.x; d[1] = a.y; d[2] = a.z; d[3] = a.w;
    d[4] = b.x; d[5] = b.y; d[6] = b.z; d[7] = b.w;
    d[8] = c.x; d[9] = c.y; d[10] = c.z; d[11] = c.w;
    uboVolume_->dirty();
}

// ===========================================================================
// B5 产品级回读 (stages 发布的数据源)
// ===========================================================================
bool SensorPipeline::hasStage(ProductStage stage) const {
    switch (stage) {
    case ProductStage::Scene:  return p0FromRT_ ? bool(bandRT_ && bandRT_->p0) : bool(radColor_);
    case ProductStage::Pupil:  return false;                 // P1 未产出 (P0+投影几何, 后续)
    case ProductStage::Focal:  return bool(opticsColor_);
    case ProductStage::Raw:    return bool(detColor_);
    case ProductStage::Image:  return bool(elecColor_);
    }
    return false;
}

vsg::ref_ptr<vsg::CommandGraph> SensorPipeline::offscreenStageGraph(ProductStage stage)const {
    if(!commandGraph||commandGraph->window||!hasStage(stage))throw std::invalid_argument("offscreen product graph requires an available stage and no window");
    if(stage==ProductStage::Image)return commandGraph;
    size_t count=p0FromRT_?2:1;
    if(stage==ProductStage::Focal)++count;
    else if(stage==ProductStage::Raw)count+=2;
    if(count>commandGraph->children.size())throw std::logic_error("offscreen product stages are incomplete");
    auto graph=vsg::CommandGraph::create(commandGraph->device,commandGraph->queueFamily);
    for(size_t i=0;i<count;++i)graph->addChild(commandGraph->children[i]);
    return graph;
}

vsg::ref_ptr<vsg::ImageView> SensorPipeline::stageImageView(ProductStage stage,
                                                            VkImageLayout& curLayout) const {
    vsg::ref_ptr<vsg::ImageView> view;
    curLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    switch (stage) {
    case ProductStage::Scene:
        if (p0FromRT_) {
            if (!bandRT_ || !bandRT_->p0) return {};
            view = bandRT_->p0;
            curLayout = VK_IMAGE_LAYOUT_GENERAL;   // RT 输出全程 GENERAL
        } else {
            view = radColor_;
        }
        break;
    case ProductStage::Pupil:  return {};          // P1 未产出
    case ProductStage::Focal:  view = opticsColor_; break;
    case ProductStage::Raw:    view = detColor_;    break;
    case ProductStage::Image:  view = elecColor_;   break;
    }
    if (!view || !view->image) return {};
    return view;
}

bool SensorPipeline::readbackStage(vsg::Window& window, ProductStage stage,
                                   std::vector<float>& out) { return readbackStage(*window.getOrCreateDevice(),stage,out); }

bool SensorPipeline::readbackStage(vsg::Device& deviceArg, ProductStage stage,
                                   std::vector<float>& out) {
    VkImageLayout curLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    auto view = stageImageView(stage, curLayout);
    if (!view) return false;

    const VkExtent3D ext = view->image->extent;
    const VkDeviceSize bytes = VkDeviceSize(ext.width) * ext.height * 4 * sizeof(float);

    auto device = vsg::ref_ptr<vsg::Device>(&deviceArg);
    const int gfxFamily = device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    auto queue = device->getQueue(uint32_t(gfxFamily));

    // host visible 接收缓冲 (与 BandRT::dumpP0 同模式)
    auto buf = vsg::Buffer::create(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE);
    buf->compile(device.get());
    auto mem = vsg::DeviceMemory::create(device.get(), buf->getMemoryRequirements(device->deviceID),
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf->bind(mem, 0);

    auto pool = vsg::CommandPool::create(device, uint32_t(gfxFamily));
    auto cb = pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb->vk(), &bi);

    // 布局往返: 当前布局 → TRANSFER_SRC → 拷贝 → 还原 (离屏级 finalLayout=
    // SHADER_READ_ONLY, 非法拷贝源; RT P0 为 GENERAL 同样需转 TRANSFER_SRC)
    VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toSrc.oldLayout = curLayout;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = view->image->vk(device->deviceID);
    toSrc.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toSrc.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb->vk(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region{};
    region.imageSubresource = VkImageSubresourceLayers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = ext;
    vkCmdCopyImageToBuffer(cb->vk(), view->image->vk(device->deviceID),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf->vk(device->deviceID), 1, &region);

    VkImageMemoryBarrier toBack = toSrc;
    toBack.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toBack.newLayout = curLayout;
    toBack.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toBack.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb->vk(), VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toBack);
    vkEndCommandBuffer(cb->vk());

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    VkCommandBuffer vkCb = cb->vk();
    si.pCommandBuffers = &vkCb;
    auto fence = vsg::Fence::create(device);
    if (queue->submit(si, fence.get()) != VK_SUCCESS || fence->wait(UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "[SensorPipeline] readbackStage: 提交失败\n");
        return false;
    }

    void* mapped = nullptr;
    if (mem->map(0, bytes, 0, &mapped) != VK_SUCCESS || !mapped) {
        std::fprintf(stderr, "[SensorPipeline] readbackStage: map 失败\n");
        return false;
    }
    out.resize(size_t(ext.width) * ext.height * 4);
    std::memcpy(out.data(), mapped, size_t(bytes));
    mem->unmap();
    return true;
}

} // namespace sensor
} // namespace rockyenv
