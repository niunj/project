#include "WeatherReflection.h"
#include <cmath>
#include <cstdlib>
#include <vsg/all.h>
namespace rockyenv {
vsg::ref_ptr<vsg::ColorBlendState> weatherLayerBlend(bool premultiplied) {
    auto blend = vsg::ColorBlendState::create();
    blend->configureAttachments(true);
    auto &a = blend->attachments[0];
    a.srcColorBlendFactor = premultiplied ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
    a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return blend;
}
vsg::ref_ptr<vsg::ColorBlendState> weatherReflectionBlend() { return weatherLayerBlend(true); }

namespace {
struct Recorded : vsg::Inherit<vsg::Command, Recorded> {
    std::shared_ptr<std::atomic<uint64_t>> count;
    void record(vsg::CommandBuffer &) const override { count->fetch_add(1); }
};
} // namespace

WeatherReflection::WeatherReflection(vsg::Device *device, std::shared_ptr<CloudGpuResources> resources)
    : resources_(std::move(resources)), device_(device) {
    commands = vsg::Switch::create();
    if (!device || std::getenv("VSGRAYSIM_OCEAN_NO_REFLECTION")) {
        device_ = nullptr;
        return;
    }
    auto image = vsg::Image::create();
    image->imageType = VK_IMAGE_TYPE_2D;
    image->format = VK_FORMAT_R16G16B16A16_SFLOAT;
    image->extent = {128, 64, 1};
    image->mipLevels = 1;
    image->arrayLayers = 1;
    image->samples = VK_SAMPLE_COUNT_1_BIT;
    image->tiling = VK_IMAGE_TILING_OPTIMAL;
    image->usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    auto view = vsg::createImageView(device, image, VK_IMAGE_ASPECT_COLOR_BIT);
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod = 0;
    resources_->reflection->sampler = sampler;
    resources_->reflection->imageView = view;
    resources_->reflection->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
}
void WeatherReflection::setDraw(vsg::ref_ptr<vsg::Node> draw) {
    if (!device_ || !resources_->reflection || !resources_->reflection->imageView)
        return;
    auto view = resources_->reflection->imageView;
    auto device = device_;
    vsg::RenderPass::Attachments attachments(1);
    auto &a = attachments[0];
    a.format = view->image->format;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    vsg::SubpassDescription sub;
    sub.colorAttachments.push_back({0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    vsg::SubpassDependency before{};
    before.srcSubpass = VK_SUBPASS_EXTERNAL;
    before.dstSubpass = 0;
    before.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    before.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    before.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vsg::SubpassDependency after{};
    after.srcSubpass = 0;
    after.dstSubpass = VK_SUBPASS_EXTERNAL;
    after.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    after.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    after.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    auto pass = vsg::RenderPass::create(device, attachments, vsg::RenderPass::Subpasses{sub},
                                        vsg::RenderPass::Dependencies{before, after});
    auto graph = vsg::RenderGraph::create();
    graph->framebuffer = vsg::Framebuffer::create(pass, vsg::ImageViews{view}, 128, 64, 1);
    graph->renderArea = {{0, 0}, {128, 64}};
    graph->viewportState = vsg::ViewportState::create(VkExtent2D{128, 64});
    graph->clearValues.resize(1);
    graph->clearValues[0].color = {{0, 0, 0, 0}};
    auto group = vsg::Group::create();
    group->addChild(graph);
    auto mark = Recorded::create();
    mark->count = recorded_;
    group->addChild(mark);
    graph->addChild(draw);
    commands->addChild(true, group);
}
void WeatherReflection::update(const VoxelWeatherFrame &f) {
    auto recorded = recorded_->load();
    if (recorded != lastRecord_) {
        previous_ = pending_;
        valid_ = true;
        lastRecord_ = recorded;
    }
    const bool enabled =
        f.ocean && !commands->children.empty() && !std::getenv("VSGRAYSIM_OCEAN_NO_REFLECTION");
    const bool changed =
        !valid_ || std::abs(f.seconds - previous_.seconds) >= .25 || f.seconds < previous_.seconds ||
        vsg::length(f.eye - previous_.eye) > 12. || vsg::length(f.sun - previous_.sun) > .00002f ||
        vsg::length(f.moon - previous_.moon) > .00002f ||
        std::abs(f.moonPhase - previous_.moonPhase) > 1e-5f || f.cover != previous_.cover ||
        f.clouds != previous_.clouds || f.cloudAnchor != previous_.cloudAnchor ||
        f.overcast != previous_.overcast || f.overcastAmount != previous_.overcastAmount || f.displayAtmosphere != previous_.displayAtmosphere ||
        f.turbidity != previous_.turbidity || f.windSpeed != previous_.windSpeed ||
        f.windAxis != previous_.windAxis || f.cloudBase != previous_.cloudBase ||
        f.cloudHeight != previous_.cloudHeight || f.opticalDepth != previous_.opticalDepth ||
        f.asymmetry != previous_.asymmetry || f.fogVisibility != previous_.fogVisibility ||
        f.fogHeight != previous_.fogHeight ||
        (f.fogVisibility > 0 && vsg::length(f.fogLight - previous_.fogLight) > .002f) ||
        f.rain != previous_.rain || f.seaLevel != previous_.seaLevel ||
        f.cloudRadius != previous_.cloudRadius || f.cloudObjects != previous_.cloudObjects;
    commands->setAllChildren(enabled && changed);
    if (enabled && changed)
        pending_ = f;
    (*resources_->reflectionState)[0] = vsg::vec4(enabled ? 1.f : 0.f, 128, 64, float(recorded));
    (*resources_->reflectionState)[1] = vsg::vec4(vsg::vec3(enabled && changed ? f.eye : previous_.eye), 0);
    resources_->reflectionState->dirty();
    if (!enabled)
        valid_ = false;
}
} // namespace rockyenv
