/// Headless voxel-model state renderer.
///
/// Loads a mesh (obj/osgb/vsgb), voxelises it, applies damage/collapse, and
/// renders the voxel model at three stages to PPM images for visual review:
///   1. intact  — the original voxel model
///   2. damaged — after a blast crater
///   3. collapsed — after unsupported voxels have fallen
///
/// Usage:
///   voxel_model_states --model models/Bunker/Bunker.obj --out DIR [--size N]

#include "../src/earth/VoxelTerrainPhysics.h"
#include "../src/earth/VoxelModelVoxelizer.h"
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsgraysim/earth/SurfaceMesh.h>
#include <rocky_env/GpuContext.h>

#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileTraversal.h>
#include <vsg/app/RecordAndSubmitTask.h>
#include <vsg/ui/FrameStamp.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/commands/Command.h>
#include <vsg/core/Exception.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <vsg/utils/Builder.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/CommandPool.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/Queue.h>
#include <vsg/vk/RenderPass.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using vsgraysim::earth::VoxelTerrainPhysics;
using vsgraysim::earth::VoxelModelConfig;
using vsgraysim::earth::VoxelTerrain;
using vsgraysim::earth::voxelizeFile;

namespace {

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
vsg::ref_ptr<vsg::ImageView> makeDepthTarget(vsg::Device* device, uint32_t w, uint32_t h) {
    auto img = vsg::Image::create();
    img->imageType = VK_IMAGE_TYPE_2D;
    img->format = VK_FORMAT_D32_SFLOAT;
    img->extent = VkExtent3D{w, h, 1};
    img->mipLevels = 1;
    img->arrayLayers = 1;
    img->samples = VK_SAMPLE_COUNT_1_BIT;
    img->tiling = VK_IMAGE_TILING_OPTIMAL;
    img->usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    img->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device, img, VK_IMAGE_ASPECT_DEPTH_BIT);
}
vsg::AttachmentDescription attachment(VkFormat fmt) {
    vsg::AttachmentDescription a{};
    a.format = fmt; a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return a;
}

std::vector<float> readback(vsg::Device* device, vsg::ImageView* view, uint32_t w, uint32_t h) {
    auto buf = vsg::Buffer::create(sizeof(float)*4*w*h, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_SHARING_MODE_EXCLUSIVE);
    buf->compile(device);
    auto mem = vsg::DeviceMemory::create(device, buf->getMemoryRequirements(device->deviceID),
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf->bind(mem, 0);
    auto pool = vsg::CommandPool::create(device, uint32_t(device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)));
    auto cmd = pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd->vk(), &bi);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd->vk(), view->image->vk(device->deviceID), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buf->vk(device->deviceID), 1, &copy);
    vkEndCommandBuffer(cmd->vk());
    VkCommandBuffer raw = cmd->vk();
    auto fence = vsg::Fence::create(device);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &raw;
    device->getQueue(uint32_t(device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)))->submit(si, fence);
    fence->wait(30000000000ull);
    std::vector<float> out(sizeof(float)*4*w*h/sizeof(float));
    void* mapped = nullptr;
    if (mem->map(0, out.size()*sizeof(float), 0, &mapped) != VK_SUCCESS) return {};
    std::memcpy(out.data(), mapped, out.size()*sizeof(float));
    mem->unmap();
    return out;
}

void writePpm(const fs::path& out, const std::vector<float>& rgba, uint32_t w, uint32_t h) {
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t i = 0; i < w*h; ++i)
        for (int c = 0; c < 3; ++c) {
            float v = rgba[i*4+c];
            v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            f.put(char(std::pow(v, 1.f/2.2f)*255.f + .5f));
        }
}

struct Target {
    vsg::ref_ptr<vsg::ImageView> color, depth;
    vsg::ref_ptr<vsg::RenderPass> renderPass;
    vsg::ref_ptr<vsg::Framebuffer> fbo;
    vsg::ref_ptr<vsg::RenderGraph> rg;
};

Target makeTarget(vsg::Device* device, uint32_t w, uint32_t h) {
    Target t;
    t.color = makeColorTarget(device, w, h);
    t.depth = makeDepthTarget(device, w, h);
    vsg::SubpassDescription sub{};
    sub.colorAttachments = std::vector<vsg::AttachmentReference>{{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    sub.depthStencilAttachments = std::vector<vsg::AttachmentReference>{{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    vsg::SubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    t.renderPass = vsg::RenderPass::create(device,
        vsg::RenderPass::Attachments{attachment(VK_FORMAT_R32G32B32A32_SFLOAT), attachment(VK_FORMAT_D32_SFLOAT)},
        vsg::RenderPass::Subpasses{sub}, vsg::RenderPass::Dependencies{dep});
    t.fbo = vsg::Framebuffer::create(t.renderPass, vsg::ImageViews{t.color, t.depth}, w, h, 1u);
    t.rg = vsg::RenderGraph::create();
    t.rg->framebuffer = t.fbo;
    t.rg->renderArea = VkRect2D{{0, 0}, {w, h}};
    t.rg->viewportState = vsg::ViewportState::create(VkExtent2D{w, h});
    t.rg->setClearValues(VkClearColorValue{{0.06f, 0.07f, 0.09f, 1.0f}},
                         VkClearDepthStencilValue{1.0f, 0});
    return t;
}

void renderFrame(vsg::Device* device, uint32_t family, const Target& t, vsg::Node* scene,
                 const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up) {
    auto lookAt = vsg::LookAt::create();
    lookAt->eye = eye; lookAt->center = center; lookAt->up = up;
    auto camera = vsg::Camera::create(
        vsg::Perspective::create(45., 1.6, 0.1, 200.),
        lookAt, vsg::ViewportState::create(VkExtent2D{800, 500}));
    auto view = vsg::View::create(camera);
    auto* sg = dynamic_cast<vsg::StateGroup*>(scene);
    if (sg) {
        std::cerr << "scene StateGroup children=" << sg->children.size() << "\n";
        for (size_t i = 0; i < sg->children.size(); ++i) {
            std::cerr << "  child[" << i << "] ptr=" << sg->children[i].get() << " type=" << typeid(*sg->children[i]).name() << "\n";
        }
    } else std::cerr << "scene is not StateGroup\n";
    view->addChild(vsg::ref_ptr<vsg::Node>(scene));
    auto requirements = vsg::CollectResourceRequirements::create();
    view->accept(*requirements);
    auto compile = vsg::CompileTraversal::create(vsg::ref_ptr<vsg::Device>(device), requirements->requirements);
    auto& ctx = *compile->contexts.front();
    ctx.renderPass = t.renderPass;
    ctx.defaultPipelineStates.push_back(vsg::ViewportState::create(VkExtent2D{800, 500}));
    compile->compile(view);

    auto cg = vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device), family);
    t.rg->children.clear();
    t.rg->addChild(view);
    cg->addChild(t.rg);
    static rockyenv::sensor::GpuContext gpuCtx;
    static bool gpuReady = false;
    if (!gpuReady) { gpuCtx.initialize(false, device); gpuReady = true; }
    gpuCtx.compile(cg);
    gpuCtx.execute(cg);
}

} // namespace

int main(int argc, char** argv) try {
    std::string modelPath = "models/Bunker/Bunker.obj";
    std::string outDir = "/tmp/voxel_model_states";
    int maxDim = 64;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&](const char* d){ return i+1<argc ? argv[++i] : d; };
        if (a=="--model") modelPath = val(modelPath.c_str());
        else if (a=="--out") outDir = val(outDir.c_str());
        else if (a=="--size") maxDim = std::stoi(val("64"));
    }
    fs::create_directories(outDir);

    auto instance = vsg::Instance::create(vsg::Names{}, vsg::Names{}, VK_API_VERSION_1_1);
    auto [physical, family] = instance->getPhysicalDeviceAndQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    if (!physical || family < 0) { std::cerr << "no Vulkan graphics device\n"; return 6; }
    auto device = vsg::Device::create(physical.get(), vsg::QueueSettings{{family,{1.f}}}, vsg::Names{}, vsg::Names{});
    std::cerr << "device: " << physical->getProperties().deviceName << '\n';

    const auto model = voxelizeFile(modelPath, VoxelModelConfig{.maxDimension = maxDim});
    std::cerr << "voxelised " << modelPath << " -> " << model.nx << "x" << model.ny << "x" << model.nz
              << " cells, " << model.occupiedCount << " occupied\n";

    const vsg::dvec3 centre = model.origin + vsg::dvec3(model.nx, model.ny, model.nz) * model.cellSize * .5;
    const double radius = vsg::length(vsg::dvec3(model.nx, model.ny, model.nz) * model.cellSize) * .8;
    const vsg::dvec3 eye = centre + vsg::dvec3(radius, radius*.7, radius);
    const vsg::dvec3 up{0, 1, 0};

    VoxelTerrainPhysics physics(model.terrain);
    const auto target = makeTarget(device.get(), 800, 500);

    // Minimal unlit colour pipeline for voxel geometry (no ViewDependentState).
    auto shaders = vsg::ShaderSet::create(vsg::ShaderStages{
        vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main",
            "#version 450\n"
            "layout(push_constant) uniform Push { mat4 mvp; } pc;\n"
            "layout(location=0) in vec3 pos;\n"
            "void main() { gl_Position = pc.mvp * vec4(pos, 1.0); }\n"),
        vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main",
            "#version 450\n"
            "layout(location=0) out vec4 color;\n"
            "void main() { color = vec4(0.72, 0.68, 0.62, 1.0); }\n")});
    shaders->addAttributeBinding("position", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
    shaders->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 64);
    auto cfg = vsg::GraphicsPipelineConfigurator::create(shaders);
    cfg->enableArray("position", VK_VERTEX_INPUT_RATE_VERTEX, sizeof(vsg::vec3));
    auto pipeline = vsg::StateGroup::create();
    cfg->copyTo(pipeline);

    // Stage 1: intact
    {
        auto draw = vsgraysim::earth::makeSurfaceDraw(*model.terrain, false);
        pipeline->children.clear();
        pipeline->addChild(draw);
        renderFrame(device.get(), uint32_t(family), target, pipeline.get(), eye, centre, up);
    }
    writePpm(fs::path(outDir)/"1-intact.ppm", readback(device.get(), target.color, 800, 500), 800, 500);
    std::cerr << "wrote " << outDir << "/1-intact.ppm\n";

    // Stage 2: damaged
    const std::size_t removed = physics.damage({centre, {0, -1, 0}, 500.});
    std::cerr << "damage removed " << removed << " voxels\n";
    auto damaged = physics.commit();
    {
        auto draw = vsgraysim::earth::makeSurfaceDraw(*damaged, false);
        pipeline->children.clear();
        pipeline->addChild(draw);
        renderFrame(device.get(), uint32_t(family), target, pipeline.get(), eye, centre, up);
    }
    writePpm(fs::path(outDir)/"2-damaged.ppm", readback(device.get(), target.color, 800, 500), 800, 500);
    std::cerr << "wrote " << outDir << "/2-damaged.ppm\n";

    // Stage 3: collapsed
    const std::size_t moved = physics.collapse();
    std::cerr << "collapse moved " << moved << " voxels\n";
    auto collapsed = physics.commit();
    {
        auto draw = vsgraysim::earth::makeSurfaceDraw(*collapsed, false);
        pipeline->children.clear();
        pipeline->addChild(draw);
        renderFrame(device.get(), uint32_t(family), target, pipeline.get(), eye, centre, up);
    }
    writePpm(fs::path(outDir)/"3-collapsed.ppm", readback(device.get(), target.color, 800, 500), 800, 500);
    std::cerr << "wrote " << outDir << "/3-collapsed.ppm\n";

    return 0;
} catch (const vsg::Exception& e) {
    std::cerr << "vsg: " << e.message << " (VkResult " << e.result << ")\n"; return 7;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n'; return 1;
}
