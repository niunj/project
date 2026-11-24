/// Headless cloud/sky viewpoint renderer.
///
/// Renders the native volume weather field (VoxelWeather planet pass) to an
/// offscreen RGBA32F render target and writes a PPM per viewpoint. Runs
/// without a window/X11 on any Vulkan device (including SwiftShader), so it
/// is usable in CI to judge cloud realism across camera heights and times
/// of day.
///
/// Usage:
///   cloud_viewpoints --out DIR [--cover 0..1] [--width W] [--height H]
///   cloud_viewpoints --out DIR --time noon|dusk|night --view orbit|high|cloud_top|cloud_inside|cloud_base|ground

#include <rocky_env/VoxelWeather.h>
#include <rocky_env/GpuContext.h>
#include <rocky_env/EnvApi.h>
#include <simcore/GeoFrame.h>

#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/RecordAndSubmitTask.h>
#include <vsg/ui/FrameStamp.h>
#include <vsg/app/CompileTraversal.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/commands/Command.h>
#include <vsg/commands/Commands.h>
#include <vsg/core/Exception.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/CommandPool.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/Queue.h>
#include <vsg/vk/RenderPass.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <array>
#include <vector>

namespace fs = std::filesystem;
using rockyenv::VoxelWeather;
using rockyenv::VoxelWeatherFrame;

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
    a.format = fmt;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    return a;
}
vsg::ref_ptr<vsg::RenderGraph> makeOffscreenRG(vsg::ref_ptr<vsg::Framebuffer> fbo) {
    auto rg = vsg::RenderGraph::create();
    rg->framebuffer = fbo;
    rg->renderArea = VkRect2D{{0, 0}, fbo->extent2D()};
    rg->viewportState = vsg::ViewportState::create(fbo->extent2D());
    // Alpha-zero clear: discarded fragments (no cloud on this ray) keep
    // alpha=0 so the background sky/ground composite shows through in
    // writePpm. The old opaque-black clear produced the black void.
    rg->setClearValues(VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}},
                       VkClearDepthStencilValue{0.0f, 0});   // reverse-Z
    return rg;
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
    si.commandBufferCount = 1;
    si.pCommandBuffers = &raw;
    device->getQueue(uint32_t(device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)))->submit(si, fence);
    fence->wait(30000000000ull);
    std::vector<float> out(sizeof(float)*4*w*h/sizeof(float));
    void* mapped = nullptr;
    if (mem->map(0, out.size()*sizeof(float), 0, &mapped) != VK_SUCCESS) return {};
    std::memcpy(out.data(), mapped, out.size()*sizeof(float));
    mem->unmap();
    return out;
}

void writePpm(const fs::path& out, const std::vector<float>& rgba, uint32_t w, uint32_t h,
              const std::array<float,3>& sky, bool ground) {
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t i = 0; i < w*h; ++i) {
        const float alpha = rgba[i*4+3];
        // Composite the cloud alpha over a background: daylight sky in the
        // upper half, a dim ground plane in the lower half. This is what a
        // real scene compositing the same pass would produce.
        const float* bg = ground && i/4/w > h/2 ? (const float[]){0.18f,0.20f,0.16f} : sky.data();
        for (int c = 0; c < 3; ++c) {
            float v = rgba[i*4+c] + bg[c]*(1.f-alpha);
            v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            f.put(char(std::pow(v, 1.f/2.2f)*255.f + .5f));
        }
    }
}

struct PixelStats {
    double mean = 0, sd = 0, brightPct = 0, darkPct = 0;
    long pxSum = 0;
};
PixelStats stats(const std::vector<float>& rgba) {
    double sum = 0, sum2 = 0; long bright = 0, dark = 0;
    const double n = double(rgba.size()/4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        const float l = 0.2126f*rgba[i] + 0.7152f*rgba[i+1] + 0.0722f*rgba[i+2];
        sum += l; sum2 += l*l;
        if (l > .6f) ++bright; if (l < .04f) ++dark;
    }
    PixelStats s;
    s.mean = sum/n; s.sd = std::sqrt(std::max(0., sum2/n - s.mean*s.mean));
    s.brightPct = 100.*bright/n; s.darkPct = 100.*dark/n;
    s.pxSum = long(sum*255);
    return s;
}
} // namespace

int main(int argc, char** argv) try {
    std::string outDir = ".";
    uint32_t width = 800, height = 600;
    float cover = .55f;
    std::string onlyTime, onlyView;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&](const char* d){ return i+1<argc ? argv[++i] : d; };
            if (a=="--out") outDir = val(outDir.c_str());
        else if (a=="--width") width = uint32_t(std::stoul(val("800")));
        else if (a=="--height") height = uint32_t(std::stoul(val("600")));
        else if (a=="--cover") cover = std::stof(val(".55"));
        else if (a=="--time") onlyTime = val("");
        else if (a=="--view") onlyView = val("");
    }
    fs::create_directories(outDir);

    auto instance = vsg::Instance::create(vsg::Names{}, vsg::Names{}, VK_API_VERSION_1_1);
    auto [physical, family] = instance->getPhysicalDeviceAndQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    if (!physical || family < 0) { std::cerr << "no Vulkan graphics device\n"; return 6; }
    auto device = vsg::Device::create(physical.get(), vsg::QueueSettings{{family,{1.f}}}, vsg::Names{}, vsg::Names{});
    std::cerr << "device: " << physical->getProperties().deviceName << '\n';

    VoxelWeather weather(device.get());
    const float cloudBase = 900.f, cloudHeight = 3200.f;
    const double lat = 25.0, lon = 122.0;

    struct ViewDef { const char* name; double altitude; bool lookUp; };
    const std::vector<ViewDef> views = {
        {"orbit",        300000., false},
        {"high",         30000.,  false},
        {"cloud_top",    double(cloudHeight)+800., false},
        {"cloud_inside", .5*(cloudBase+cloudHeight), true},
        {"cloud_base",   std::max(120., double(cloudBase)-300.), true},
        {"ground",       30., false},   // ground looks across at the cloud deck
    };
    struct TimeDef { const char* name; double hoursUtc; };
    const std::vector<TimeDef> times = {
        {"noon", 4.},
        {"dusk", 10.},
        {"night",16.},
    };
    // Subsolar point approximation (late September, UTC hour).
    auto sunEcefFor = [&](double hoursUtc) {
        const double subLon = 180.0 - 15.0*hoursUtc;
        return simcore::geo::geodeticToECEF(-3.0, subLon, 1.5e11);
    };

    const double cloudMid = .5*(cloudBase+cloudHeight);

    // Anchor at the densest resolved cloud column within +-5 deg so every
    // fixed viewpoint actually intersects cloud rather than clear air.
    double anchorLat = lat, anchorLon = lon;
    {
        VoxelWeatherFrame probe;
        probe.eye = simcore::geo::geodeticToECEF(lat, lon, 4000.);
        probe.windAxis = {0,0,1}; probe.sun = {0,1,0}; probe.seconds = 1200.;
        probe.windSpeed = 8.f; probe.cover = cover;
        probe.cloudBase = cloudBase; probe.cloudHeight = cloudHeight;
        // updateSparseCloud is lazy: run the sparse commit pipeline once so
        // the page table is populated before querying densities.
        // Sparse commit is asynchronous across frames: pump the weather
        // update like the real frame loop until the local revision is live.
        float bestDensity = 0;
        for (int frame = 0; frame < 60 && bestDensity < .0002; ++frame) {
            probe.seconds = 1200. + frame * .05;
            weather.update(probe);
            // Scan a 20-degree neighbourhood, not +-5, so the fixed viewpoints
            // actually land inside a synoptic cloud system.
            for (double dlat = -10.; dlat <= 10.; dlat += .5)
                for (double dlon = -10.; dlon <= 10.; dlon += .5) {
                    const auto p = simcore::geo::geodeticToECEF(lat+dlat, lon+dlon, cloudMid);
                    const float d = rockyenv::cloudDensity(probe, p);
                    if (d > bestDensity) { bestDensity = d; anchorLat = lat+dlat; anchorLon = lon+dlon; }
                }
        }
        std::cerr << "anchor lat=" << anchorLat << " lon=" << anchorLon
                  << " midDensity=" << bestDensity << '\n';
        // Global macro scan: find the densest macro-cloud cell anywhere so
        // the fixed viewpoints actually contain visible cloud.
        double macroBest = 0, mlat = lat, mlon = lon;
        // Restrict to the daytime hemisphere: subsolar longitude is 120 deg
        // at 4 UTC; accept anchors within +-55 deg so the sun is well up.
        for (double glat = -30.; glat <= 30.; glat += 4.)
            for (double glon = 65.; glon <= 175.; glon += 6.) {
                const auto p = simcore::geo::geodeticToECEF(glat, glon, cloudMid);
                const float d = rockyenv::cloudDensity(probe, p);
                if (d > macroBest) { macroBest = d; mlat = glat; mlon = glon; }
            }
        std::cerr << "macro best lat=" << mlat << " lon=" << mlon << " density=" << macroBest << '\n';
        if (macroBest > .0002) { anchorLat = mlat; anchorLon = mlon; }
    }
    const auto cloudPoint = simcore::geo::geodeticToECEF(anchorLat, anchorLon, cloudMid);
    const auto groundPoint = simcore::geo::geodeticToECEF(anchorLat, anchorLon, 0.);
    const auto anchorUp = simcore::geo::geoUp(anchorLat, anchorLon);

    auto color = makeColorTarget(device.get(), width, height);
    auto depth = makeDepthTarget(device.get(), width, height);
    vsg::SubpassDescription sub{};
    sub.colorAttachments = std::vector<vsg::AttachmentReference>{{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    sub.depthStencilAttachments = std::vector<vsg::AttachmentReference>{{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    vsg::SubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    auto renderPass = vsg::RenderPass::create(device,
        vsg::RenderPass::Attachments{attachment(VK_FORMAT_R32G32B32A32_SFLOAT), attachment(VK_FORMAT_D32_SFLOAT)},
        vsg::RenderPass::Subpasses{sub}, vsg::RenderPass::Dependencies{dep});
    auto fbo = vsg::Framebuffer::create(renderPass, vsg::ImageViews{color, depth}, width, height, 1u);
    auto rg = makeOffscreenRG(fbo);

    for (const auto& tdef : times) {
        if (!onlyTime.empty() && tdef.name != onlyTime) continue;
        const auto sunEcef = sunEcefFor(tdef.hoursUtc);
        const auto sunDir = vsg::vec3(vsg::normalize(sunEcef - groundPoint));
        for (const auto& vdef : views) {
            if (!onlyView.empty() && vdef.name != onlyView) continue;
            VoxelWeatherFrame f;
            f.eye = simcore::geo::geodeticToECEF(anchorLat, anchorLon, vdef.altitude);
            f.windAxis = {0,0,1};
            f.sun = sunDir;
            f.moon = -sunDir;
            f.moonPhase = .8f;
            f.exposure = 1.f;
            f.turbidity = 1.f;
            f.seconds = 1200.;
            f.windSpeed = 8.f;
            f.cover = cover;
            f.cloudBase = cloudBase;
            f.cloudHeight = cloudHeight;
            f.clouds = true;
            f.ocean = false;
            weather.update(f);

            auto world = vsg::Group::create();
            world->addChild(vsg::ref_ptr<vsg::Node>(weather.node.get()));
            auto lookAt = vsg::LookAt::create();
            lookAt->eye = f.eye;
            // cloud_inside / cloud_base / ground look across the column, not
            // straight at the eye position (a degenerate lookAt).
            if (vdef.lookUp)
                lookAt->center = f.eye + simcore::geo::geoEast(anchorLat, anchorLon) * 1500.
                                        + anchorUp * (vdef.name == std::string("cloud_inside") ? 500. : 1200.);
            else if (std::string(vdef.name) == "ground")
                lookAt->center = cloudPoint;
            else
                lookAt->center = groundPoint;
            // Avoid up-vector / view-direction collinearity: near-vertical
            // views use a tilted up instead of the local geodetic up.
            const auto viewDir = vsg::normalize(lookAt->center - lookAt->eye);
            lookAt->up = std::abs(vsg::dot(viewDir, anchorUp)) > .96
                ? vsg::normalize(anchorUp * .88f + simcore::geo::geoEast(anchorLat, anchorLon) * .48f)
                : anchorUp;
            auto camera = vsg::Camera::create(
                vsg::Perspective::create(50., double(width)/height, 1., 2e7),
                lookAt, vsg::ViewportState::create(VkExtent2D{width, height}));
            auto view = vsg::View::create(camera);
            view->addChild(world);
            auto requirements = vsg::CollectResourceRequirements::create();
            view->accept(*requirements);
            auto compile = vsg::CompileTraversal::create(device, requirements->requirements);
            auto& ctx = *compile->contexts.front();
            ctx.renderPass = renderPass;
            ctx.defaultPipelineStates.push_back(vsg::ViewportState::create(VkExtent2D{width, height}));
            compile->compile(view);

            // GPU init + a few solver steps so the cloud field is physically
            // seeded before the first raster.
            // GPU solver warmup (offscreen compute, no window).
            static rockyenv::sensor::GpuContext computeCtx;
            static bool computeReady = false;
            if (!computeReady) { computeCtx.initialize(false, device.get()); computeReady = true; }
            if (auto* init = weather.gpuInitializationCommands()) {
                auto cg = vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device.get()), uint32_t(family));
                cg->addChild(vsg::ref_ptr<vsg::Commands>(static_cast<vsg::Commands*>(init)));
                computeCtx.compile(cg);
                computeCtx.execute(cg);
            }
            if (weather.gpuStepRequired()) {
                if (auto* step = weather.gpuStepCommands()) {
                    auto cg = vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device.get()), uint32_t(family));
                    cg->addChild(vsg::ref_ptr<vsg::Commands>(static_cast<vsg::Commands*>(step)));
                    computeCtx.compile(cg);
                    for (int i = 0; i < 8; ++i) computeCtx.execute(cg);
                }
            }

            rg->children.clear();
            rg->addChild(view);
            auto cg = vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device.get()), uint32_t(family));
            cg->addChild(rg);
            computeCtx.compile(cg);
            computeCtx.execute(cg);

            auto rgba = readback(device.get(), color, width, height);
            fs::path out = fs::path(outDir) / (std::string(tdef.name) + "-" + vdef.name + ".ppm");
            // Daylight sky composite; dusk/night use a dimmer background.
            const std::array<float,3> sky = tdef.name == std::string("noon")
                ? std::array<float,3>{0.32f, 0.48f, 0.66f}
                : (tdef.name == std::string("dusk")
                    ? std::array<float,3>{0.30f, 0.16f, 0.09f}
                    : std::array<float,3>{0.008f, 0.012f, 0.030f});
            const bool showGround = std::string(vdef.name) == "ground" || std::string(vdef.name) == "cloud_base";
            writePpm(out, rgba, width, height, sky, showGround);
            const auto s = stats(rgba);
            std::cout << tdef.name << "/" << vdef.name << " alt=" << vdef.altitude
                      << " mean=" << int(s.mean*255) << " sd=" << int(s.sd*255)
                      << " bright=" << int(s.brightPct) << "% dark=" << int(s.darkPct)
                      << "% -> " << out << '\n';
        }
    }
    return 0;
} catch (const vsg::Exception& e) {
    std::cerr << "vsg: " << e.message << " (VkResult " << e.result << ")\n"; return 7;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n'; return 1;
}
