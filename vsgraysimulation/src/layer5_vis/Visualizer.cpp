#include "vsgraysim/layer5_vis/Visualizer.h"
#include <rocky_env/GpuContext.h>

#include <spdlog/spdlog.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Window.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/View.h>
#include <vsg/app/Camera.h>
#include <vsg/app/Trackball.h>
#include <vsg/app/CloseHandler.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/utils/CommandLine.h>
#include <vsg/utils/ShaderSet.h>
#include <vsg/io/read.h>
#include <vsg/io/Options.h>
#include <vsg/ui/Keyboard.h>
#include <vsgXchange/all.h>

#include <vsgImGui/RenderImGui.h>

namespace vsgraysim {

struct Visualizer::Impl {
    vsg::ref_ptr<vsg::Viewer>           viewer;
    vsg::ref_ptr<vsg::Window>           window;
    vsg::ref_ptr<vsg::Group>            root;
    vsg::ref_ptr<vsg::Group>            overlay;      // atmospheric overlay
    vsg::ref_ptr<vsg::Group>            cross_paths;  // crosstalk line segments
    vsg::ref_ptr<vsg::Camera>          camera;
    vsg::ref_ptr<vsg::View>            view;
    vsg::ref_ptr<vsg::Options>         options;
    int                                 width = 1280;
    int                                 height = 720;
};

Visualizer::Visualizer() : impl_(std::make_unique<Impl>()) {}
Visualizer::~Visualizer() = default;  // out-of-line so Impl can be incomplete in header

bool Visualizer::init(int width, int height) {
    impl_->width = width;
    impl_->height = height;
    impl_->viewer = vsg::Viewer::create();

    vsg::ref_ptr<vsg::WindowTraits> traits =
        vsg::WindowTraits::create(static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height),
                                  "vsgraysim");
    traits->debugLayer = false;
    traits->vulkanVersion=VK_API_VERSION_1_2;
    traits->apiDumpLayer = false;
    impl_->window = vsg::Window::create(traits);
    traits->deviceFeatures=vsg::DeviceFeatures::create();
    traits->deviceFeatures->get()=impl_->window->getOrCreatePhysicalDevice()->getFeatures();
    rockyenv::sensor::GpuContext::configureDevice(*impl_->window->getOrCreatePhysicalDevice(),traits->deviceExtensionNames,traits->deviceFeatures);
    impl_->viewer->addWindow(impl_->window);

    // Camera: default view -50 degree pitch (prominent top-down view)
    // pitch = atan2(camera_y - target_y, horizontal_distance)
    // With horizontal_distance=200, camera_y = 200 * tan(-50°) ≈ -238.4
    auto lookAt = vsg::LookAt::create(
        vsg::dvec3(0.0, -238.4, 200.0),
        vsg::dvec3(0.0, 0.0, 0.0),
        vsg::dvec3(0.0, 1.0, 0.0));
    auto perspective = vsg::Perspective::create(
        60.0, double(width) / double(height), 0.1, 10000.0);
    impl_->camera = vsg::Camera::create(perspective, lookAt);

    impl_->view = vsg::View::create(impl_->camera);
    impl_->root = vsg::Group::create();
    impl_->overlay = vsg::Group::create();
    impl_->cross_paths = vsg::Group::create();
    impl_->view->addChild(impl_->root);
    impl_->view->addChild(impl_->cross_paths);
    impl_->view->addChild(impl_->overlay);

    // Command graph
    auto cg = vsg::CommandGraph::create(impl_->window);
    auto rg = vsg::RenderGraph::create(impl_->window, impl_->view);
    rg->setClearValues({{0.25f, 0.35f, 0.55f, 1.0f}});
    cg->addChild(rg);
    impl_->viewer->assignRecordAndSubmitTaskAndPresentation({cg});

    // In-viewport ImGui overlay (official vsgImGui pattern: direct child
    // of the RenderGraph, rendered after the 3D scene).
    {
        auto render_imgui = vsgImGui::RenderImGui::create(impl_->window);
        if (render_imgui) {
            rg->addChild(render_imgui);
            imgui_ = render_imgui;
            spdlog::info("Layer5: visualizer ImGui overlay enabled");
        } else {
            spdlog::warn("Layer5: ImGui overlay unavailable");
        }
    }

    // Options with vsgXchange readers
    impl_->options = vsg::Options::create();
    impl_->options->paths = { "models", "config", "../config", "../models" };
    if (auto ax = vsgXchange::all::create()) {
        impl_->options->readerWriters = { ax };
    }

    // Track keyboard for close
    impl_->viewer->addEventHandler(vsg::CloseHandler::create(impl_->viewer));
    impl_->viewer->addEventHandler(vsg::Trackball::create(impl_->camera));

    spdlog::info("Layer5: visualizer initialised ({}x{})", width, height);
    return true;
}

void Visualizer::setScene(vsg::ref_ptr<vsg::Node> root) {
    impl_->root->children.clear();
    if (root) impl_->root->addChild(root);
    impl_->viewer->compile();
}

void Visualizer::setCrossInductionPaths(const std::vector<CrossInductionPath>& paths) {
    impl_->cross_paths->children.clear();
    if (paths.empty()) return;
    // Build a simple line-set Geometry for the crosstalk paths.
    // For brevity we add tiny placeholder spheres at each endpoint.
    // A production version would use a vsg::Geometry with line-list topology
    // and a coloured shader.
    for (const auto& p : paths) {
        // Use vsg read to instantiate a small sphere at the rx end (red tint)
        auto sphere = vsg::read_cast<vsg::Node>("models/sphere.vsgt", impl_->options);
        if (sphere) {
            auto mt = vsg::MatrixTransform::create();
            mt->matrix = vsg::translate(p.rx_pos);
            mt->addChild(sphere);
            impl_->cross_paths->addChild(mt);
        }
    }
}

void Visualizer::updateAtmosphereOverlay(const AtmosphereModel& atm) {
    // The atmospheric overlay is implemented here as a tint on the
    // clear color, derived from the average aerosol content of the
    // atmosphere layers.  A production version would apply a screen-space
    // fog shader.
    if (atm.layerCount() == 0) return;
    float aerosol_avg = 0.0f;
    for (const auto& L : atm.layers()) aerosol_avg += L.aerosol;
    aerosol_avg /= float(atm.layerCount());
    float tint = std::min(1.0f, aerosol_avg * 1e6f);
    // (Re-creating the RenderGraph clear values each frame is expensive;
    // this is a placeholder for the production fog shader.)
    (void)tint;
}

void Visualizer::updateSensorImage(uint32_t /*band_channel*/,
                                   uint32_t /*width*/, uint32_t /*height*/,
                                   const std::vector<float>& /*image*/) {
    // Production: upload the float image to a vsg texture and blit it
    // into a sub-region of the screen via a fullscreen quad.
    // Placeholder: log when called.
}

bool Visualizer::frame() {
    if (!impl_->viewer) return false;
    if (!impl_->viewer->advanceToNextFrame()) return false;
    impl_->viewer->handleEvents();
    impl_->viewer->update();
    impl_->viewer->recordAndSubmit();
    impl_->viewer->present();
    return impl_->viewer->active();
}

void Visualizer::close() {
    if (impl_->viewer) impl_->viewer->close();
}

bool Visualizer::addImGui(std::function<void()> draw) {
    auto render_imgui = dynamic_cast<vsgImGui::RenderImGui*>(imgui_.get());
    if (!render_imgui) return false;
    render_imgui->add([draw]() -> bool { draw(); return true; });
    return true;
}

vsg::Device* Visualizer::device() const {
    if (!impl_->window) return nullptr;
    return impl_->window->getOrCreateDevice();
}

} // namespace vsgraysim
