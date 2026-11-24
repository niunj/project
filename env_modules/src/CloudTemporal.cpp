#include "CloudTemporal.h"
#include "WeatherReflection.h"
#include "EarthShader.h"
#include "WeatherHistory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vsg/all.h>
namespace rockyenv {
namespace {
vsg::ref_ptr<vsg::ImageView> target(vsg::Device *device, uint32_t w, uint32_t h, VkFormat format,
                                    bool attachment) {
    auto image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = format;
    image->extent = {w, h, 1};
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                   (attachment ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_STORAGE_BIT);
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device, image, VK_IMAGE_ASPECT_COLOR_BIT);
}
struct Initialize : vsg::Inherit<vsg::Command, Initialize> {
    vsg::ref_ptr<vsg::PipelineBarrier> barrier;
    mutable std::atomic<bool> done{false};
    void record(vsg::CommandBuffer &command) const override {
        if (!done.exchange(true))
            barrier->record(command);
    }
};
vsg::ref_ptr<vsg::PipelineBarrier> memory(VkPipelineStageFlags from, VkPipelineStageFlags to,
                                          VkAccessFlags read, VkAccessFlags write) {
    return vsg::PipelineBarrier::create(from, to, 0, vsg::MemoryBarrier::create(read, write));
}
struct Recorded : vsg::Inherit<vsg::Command, Recorded> {
    std::shared_ptr<std::atomic<uint64_t>> counter;
    void record(vsg::CommandBuffer &) const override { counter->fetch_add(1); }
};
struct CloudPose {
    vsg::dmat4 world;
    vsg::vec2 seed{};
    bool valid=false;
};
struct Pose {
    vsg::dmat4 view, projection;
    vsg::dvec3 eye;
    VoxelWeatherFrame weather;
    int index = 0;
    vsg::dvec3 anchor;
    std::array<CloudPose,144> clouds{};
    int cloudCount=0;
};
} // namespace
struct CloudTemporal::Impl {
    vsg::ref_ptr<vsg::Device> device;
    vsg::ref_ptr<vsg::Node> direct, raw;
    vsg::ref_ptr<vsg::Switch> resolve, composite;
    uint32_t width = 0, height = 0, rawWidth = 0, rawHeight = 0;
    uint64_t phase = 0;
    uint32_t failedWidth = 0, failedHeight = 0;
    std::shared_ptr<std::atomic<uint64_t>> recorded = std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t lastRecord = 0;
    Pose previous, pending;
    bool previousValid = false, pendingValid = false;
    explicit Impl(vsg::Device *d) : device(d) {}
};
CloudTemporal::CloudTemporal(vsg::Device *device) : impl_(std::make_unique<Impl>(device)) {
    parameters = vsg::vec4Array::create(16, vsg::vec4{});
    parameters->properties.dataVariance = vsg::DYNAMIC_DATA;
    motions=vsg::vec4Array::create(433,vsg::vec4{});
    motions->properties.dataVariance=vsg::DYNAMIC_DATA;
    display = vsg::Switch::create();
    commands = vsg::Switch::create();
}
CloudTemporal::~CloudTemporal() = default;
void CloudTemporal::setDraws(vsg::ref_ptr<vsg::Node> direct, vsg::ref_ptr<vsg::Node> raw) {
    impl_->direct = direct;
    impl_->raw = raw;
    display->children.clear();
    display->addChild(true, direct);
}
bool CloudTemporal::update(vsg::Camera *camera, uint32_t width, uint32_t height, const VoxelWeatherFrame &f) {
    auto &i = *impl_;
    const char *setting = std::getenv("VSGRAYSIM_CLOUD_TEMPORAL");
    const bool enabled = i.device && i.raw && camera && width > 0 && height > 0 && f.cloudObjects &&
                         ((f.clouds && f.cover > 0) || f.fogVisibility > 0) &&
                         !(setting && std::string(setting) == "0");
    if (!enabled || (width == i.failedWidth && height == i.failedHeight)) {
        commands->setAllChildren(false);
        display->setAllChildren(false);
        if (!display->children.empty())
            display->children[0].mask = vsg::MASK_ALL;
        i.previousValid = i.pendingValid = false;
        return false;
    }
    bool rebuilt = false;
    if (i.width != width || i.height != height) {
        // Rare resize only. Retire GPU resources before releasing their views.
        if (i.width)
            vkDeviceWaitIdle(*i.device);
        const auto rawWidth = std::max(1u, (width + 1) / 2), rawHeight = std::max(1u, (height + 1) / 2);
        try {
            auto rawColor = target(i.device, rawWidth, rawHeight, VK_FORMAT_R16G16B16A16_SFLOAT, true);
            auto rawDepth = target(i.device, rawWidth, rawHeight, VK_FORMAT_R32G32B32A32_SFLOAT, true);
            std::array<vsg::ref_ptr<vsg::ImageView>, 2> colors, depths;
            for (int n = 0; n < 2; ++n) {
                colors[n] = target(i.device, width, height, VK_FORMAT_R16G16B16A16_SFLOAT, false);
                depths[n] = target(i.device, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, false);
            }
            vsg::RenderPass::Attachments attachments;
            for (auto format : {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT}) {
                vsg::AttachmentDescription a{};
                a.format = format;
                a.samples = VK_SAMPLE_COUNT_1_BIT;
                a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
                attachments.push_back(a);
            }
            vsg::SubpassDescription sub;
            sub.colorAttachments = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                    {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
            vsg::SubpassDependency before{};
            before.srcSubpass = VK_SUBPASS_EXTERNAL;
            before.dstSubpass = 0;
            before.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            before.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            before.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            auto after = before;
            after.srcSubpass = 0;
            after.dstSubpass = VK_SUBPASS_EXTERNAL;
            after.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            after.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            after.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            auto rp = vsg::RenderPass::create(i.device, attachments, vsg::RenderPass::Subpasses{sub},
                                              vsg::RenderPass::Dependencies{before, after});
            auto framebuffer =
                vsg::Framebuffer::create(rp, vsg::ImageViews{rawColor, rawDepth}, rawWidth, rawHeight, 1);
            auto graph = vsg::RenderGraph::create();
            graph->framebuffer = framebuffer;
            graph->renderArea = {{0, 0}, {rawWidth, rawHeight}};
            graph->viewportState = vsg::ViewportState::create(VkExtent2D{rawWidth, rawHeight});
            graph->clearValues.resize(2);
            for (auto &clear : graph->clearValues)
                clear.color = {{0, 0, 0, 0}};
            graph->addChild(i.raw);
            auto init = Initialize::create();
            init->barrier = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
            for (const auto &view : {colors[0], colors[1], depths[0], depths[1]})
                init->barrier->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
                    0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, view->image,
                    VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
            auto sampler = vsg::Sampler::create();
            sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler->maxLod = 0;
            vsg::DescriptorSetLayoutBindings bindings;
            for (uint32_t b = 0; b < 6; ++b)
                bindings.push_back(
                    {b, b < 4 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                     1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            bindings.push_back(
                {10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
            bindings.push_back({11,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
            auto layout = vsg::DescriptorSetLayout::create(bindings);
            auto pipelineLayout =
                vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{layout}, vsg::PushConstantRanges{});
            auto pipeline = vsg::ComputePipeline::create(
                pipelineLayout, vsgraysim::earth::earthShader("cloud_temporal_resolve.comp"));
            auto resolve = vsg::Switch::create();
            auto composite = vsg::Switch::create();
            for (int n = 0; n < 2; ++n) {
                vsg::Descriptors descriptors;
                uint32_t b = 0;
                for (const auto &view : {rawColor, rawDepth, colors[1 - n], depths[1 - n]})
                    descriptors.push_back(vsg::DescriptorImage::create(
                        vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_GENERAL), b++));
                descriptors.push_back(vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, colors[n], VK_IMAGE_LAYOUT_GENERAL),
                    4, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
                descriptors.push_back(vsg::DescriptorImage::create(
                    vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, depths[n], VK_IMAGE_LAYOUT_GENERAL),
                    5, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
                descriptors.push_back(vsg::DescriptorBuffer::create(parameters, 10));
                descriptors.push_back(vsg::DescriptorBuffer::create(motions,11));
                auto set = vsg::DescriptorSet::create(layout, descriptors);
                auto dispatch = vsg::Commands::create();
                dispatch->addChild(vsg::BindComputePipeline::create(pipeline));
                dispatch->addChild(vsg::BindDescriptorSets::create(
                    VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, vsg::DescriptorSets{set}));
                dispatch->addChild(vsg::Dispatch::create((width + 7) / 8, (height + 7) / 8, 1));
                resolve->addChild(n == 0, dispatch);
                auto fogShader = vsg::ShaderSet::create(
                    vsg::ShaderStages{vsgraysim::earth::earthShader("planet_weather.vert"),
                                      vsgraysim::earth::earthShader("cloud_fog_sky.frag")});
                fogShader->addPushConstantRange("camera", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);
                fogShader->addDescriptorBinding("temporal", "", 0, 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                                VK_SHADER_STAGE_FRAGMENT_BIT, {});
                auto fogCfg = vsg::GraphicsPipelineConfigurator::create(fogShader);
                fogCfg->assignDescriptor("temporal", parameters);
                auto fogDepth = vsg::DepthStencilState::create();
                fogDepth->depthWriteEnable = VK_FALSE;
                fogDepth->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
                auto fogBlend = weatherLayerBlend();
                auto fogRaster = vsg::RasterizationState::create();
                fogRaster->cullMode = VK_CULL_MODE_NONE;
                for (auto &state : fogCfg->pipelineStates) {
                    if (state.cast<vsg::DepthStencilState>())
                        state = fogDepth;
                    else if (state.cast<vsg::ColorBlendState>())
                        state = fogBlend;
                    else if (state.cast<vsg::RasterizationState>())
                        state = fogRaster;
                }
                fogShader->variants.emplace(fogCfg->shaderHints, fogShader->stages);
                fogCfg->init();
                auto fogDraw = vsg::StateGroup::create();
                fogCfg->copyTo(fogDraw);
                fogDraw->addChild(vsg::Draw::create(3, 1, 0, 0));
                auto shader = vsg::ShaderSet::create(
                    vsg::ShaderStages{vsgraysim::earth::earthShader("planet_weather.vert"),
                                      vsgraysim::earth::earthShader("cloud_temporal_composite.frag")});
                shader->addPushConstantRange("camera", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);
                shader->addDescriptorBinding("cloudColor", "", 0, 0,
                                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, {});
                shader->addDescriptorBinding("cloudDepth", "", 0, 1,
                                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, {});
                shader->addDescriptorBinding("temporal", "", 0, 10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                             VK_SHADER_STAGE_FRAGMENT_BIT, {});
                auto cfg = vsg::GraphicsPipelineConfigurator::create(shader);
                cfg->assignTexture("cloudColor", vsg::ImageInfoList{vsg::ImageInfo::create(
                                                     sampler, colors[n], VK_IMAGE_LAYOUT_GENERAL)});
                cfg->assignTexture("cloudDepth", vsg::ImageInfoList{vsg::ImageInfo::create(
                                                     sampler, depths[n], VK_IMAGE_LAYOUT_GENERAL)});
                cfg->assignDescriptor("temporal", parameters);
                auto depth = vsg::DepthStencilState::create();
                depth->depthWriteEnable = VK_FALSE;
                depth->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
                auto blend = weatherLayerBlend();
                auto raster = vsg::RasterizationState::create();
                raster->cullMode = VK_CULL_MODE_NONE;
                for (auto &state : cfg->pipelineStates) {
                    if (state.cast<vsg::DepthStencilState>())
                        state = depth;
                    else if (state.cast<vsg::ColorBlendState>())
                        state = blend;
                    else if (state.cast<vsg::RasterizationState>())
                        state = raster;
                }
                shader->variants.emplace(cfg->shaderHints, shader->stages);
                cfg->init();
                auto draw = vsg::StateGroup::create();
                cfg->copyTo(draw);
                draw->addChild(vsg::Draw::create(3, 1, 0, 0));
                auto composed = vsg::Group::create();
                composed->addChild(fogDraw);
                composed->addChild(draw);
                composite->addChild(n == 0, composed);
            }
            auto counter = std::make_shared<std::atomic<uint64_t>>(0);
            auto recorded = Recorded::create();
            recorded->counter = counter;
            auto sequence = vsg::Group::create();
            sequence->addChild(recorded);
            sequence->addChild(init);
            sequence->addChild(graph);
            sequence->addChild(memory(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
            sequence->addChild(resolve);
            sequence->addChild(memory(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_ACCESS_SHADER_READ_BIT));
            i.width = width;
            i.height = height;
            i.rawWidth = rawWidth;
            i.rawHeight = rawHeight;
            i.resolve = resolve;
            i.composite = composite;
            i.recorded = counter;
            i.lastRecord = 0;
            i.previousValid = i.pendingValid = false;
            i.phase = 0;
            rebuilt = true;
            i.failedWidth = i.failedHeight = 0;
            commands->children.clear();
            commands->addChild(true, sequence);
            display->children.clear();
            display->addChild(false, i.direct);
            display->addChild(true, composite);
        } catch (...) {
            i.failedWidth = width;
            i.failedHeight = height;
            i.previousValid = i.pendingValid = false;
            commands->setAllChildren(false);
            display->setAllChildren(false);
            if (!display->children.empty())
                display->children[0].mask = vsg::MASK_ALL;
            std::fputs("[cloud] temporal resource preparation failed; direct volume renderer retained until "
                       "resize\n",
                       stderr);
            return false;
        }
    }
    const auto recorded = i.recorded->load();
    if (recorded != i.lastRecord) {
        if (i.pendingValid) {
            i.previous = i.pending;
            i.previousValid = true;
            ++i.phase;
        }
        i.lastRecord = recorded;
    }
    const auto view = camera->viewMatrix->transform(), projection = camera->projectionMatrix->transform();
    const double dt = f.seconds - i.previous.weather.seconds;
    const auto chart = planetSparseCloudParameters().cast<vsg::vec4Array>();
    const auto origin = (*chart)[12];
    const vsg::dvec3 anchor(origin.x, origin.y, origin.z);
    const float weatherWeight=weatherHistoryWeight(f,i.previous.weather);
    bool valid =
        i.previousValid && dt >= 0 && dt <= .25 && vsg::length(f.eye - i.previous.eye) < 600. &&
        vsg::dot(f.sun, i.previous.weather.sun) > .9995f && weatherWeight>0 &&
        vsg::length(anchor - i.previous.anchor) < 1. &&
        vsg::length(f.fogLight - i.previous.weather.fogLight) < .04f;
    if (valid) {
        const auto dir = view[2], old = i.previous.view[2];
        valid = dir.x * old.x + dir.y * old.y + dir.z * old.z > .96;
    }
    const int index = i.previousValid ? 1 - i.previous.index : 0;
    auto inverseView = vsg::inverse(view);
    inverseView[3] = vsg::dvec4(0, 0, 0, 1);
    const auto rays = inverseView * vsg::inverse(projection);
    const auto previousClip =
        i.previousValid ? i.previous.projection * i.previous.view * vsg::translate(f.eye) : vsg::dmat4{};
    auto matrix = [&](int offset, const vsg::dmat4 &m) {
        for (int c = 0; c < 4; ++c)
            (*parameters)[offset + c] = vsg::vec4(m[c]);
    };
    matrix(0, rays);
    matrix(4, previousClip);
    const auto up = vsg::normalize(f.eye),
               wind = vsg::cross(f.windAxis, up) * double(f.windSpeed) * std::max(0., dt);
    const auto delta = f.eye - i.previous.eye;
    (*parameters)[8] = vsg::vec4(float(width), float(height), valid ? 1.f : 0.f, float(i.phase % 8));
    (*parameters)[9] = vsg::vec4(vsg::vec3(wind), float(dt));
    (*parameters)[10] = vsg::vec4(vsg::vec3(delta), 0);
    static constexpr vsg::vec2 jitter[] = {
        {-.375f, -.125f}, {.125f, -.375f}, {.375f, .125f}, {-.125f, .375f}};
    const auto j = jitter[i.phase % 4];
    (*parameters)[11] = vsg::vec4(weatherWeight, f.exposure, j.x, j.y);
    (*parameters)[12] =
        vsg::vec4(float(i.rawWidth), float(i.rawHeight), float(projection[2][2]), float(projection[3][2]));
    (*parameters)[13] = vsg::vec4(f.fogVisibility, f.fogHeight, f.seaLevel, 0);
    (*parameters)[14] = vsg::vec4(f.fogLight, 0);
    (*parameters)[15] = vsg::vec4(vsg::vec3(f.eye), 0);
    parameters->dirty();
    i.resolve->setAllChildren(false);
    i.resolve->children[index].mask = vsg::MASK_ALL;
    i.composite->setAllChildren(false);
    i.composite->children[index].mask = vsg::MASK_ALL;
    commands->setAllChildren(true);
    display->children[0].mask = 0;
    display->children[1].mask = vsg::MASK_ALL;
    i.pending = {view, projection, f.eye, f, index, anchor};
    std::fill(motions->begin(),motions->end(),vsg::vec4{});
    const auto xyz=[](const auto& v){return vsg::dvec3(v.x,v.y,v.z);};
    const auto east=xyz((*chart)[13]),upAxis=xyz((*chart)[14]),north=xyz((*chart)[15]);
    const auto chartWorld=vsg::inverse(vsg::transpose(vsg::dmat4(
        vsg::dvec4(east,0),vsg::dvec4(upAxis,0),vsg::dvec4(north,0),vsg::dvec4(0,0,0,1))));
    i.pending.cloudCount=std::clamp(int((*chart)[12].w),0,144);
    (*motions)[0]=vsg::vec4(valid?1.f:0.f,float(i.pending.cloudCount),0,0);
    for(int k=0;k<i.pending.cloudCount;++k){
        const auto& c=(*chart)[16+3*k];const auto& r=(*chart)[17+3*k];const auto& shape=(*chart)[18+3*k];
        if(r.x<=0||r.y<=0||r.z<=0)continue;
        auto& pose=i.pending.clouds[k];
        pose.world=chartWorld*vsg::dmat4(
            vsg::dvec4(shape.x*r.x,0,shape.y*r.x,0),
            vsg::dvec4(-.20*shape.x*r.x,r.y,-.20*shape.y*r.x,0),
            vsg::dvec4(-shape.y*r.z,0,shape.x*r.z,0),vsg::dvec4(c.x,c.y,c.z,1));
        pose.world[3]+=vsg::dvec4(xyz((*chart)[12]),0);
        pose.seed={shape.z,shape.w};pose.valid=true;
        if(!valid||k>=i.previous.cloudCount)continue;
        const auto& old=i.previous.clouds[k];
        if(!old.valid||old.seed!=pose.seed||vsg::length(xyz(pose.world[3]-old.world[3]))>200.)continue;
        const auto relative=vsg::translate(-f.eye);
        const auto transform=(relative*old.world)*vsg::inverse(relative*pose.world);
        for(int row=0;row<3;++row)
            (*motions)[1+3*k+row]=vsg::vec4(float(transform[0][row]),float(transform[1][row]),
                float(transform[2][row]),float(transform[3][row]));
    }
    motions->dirty();
    i.pendingValid = true;
    return rebuilt;
}
} // namespace rockyenv
