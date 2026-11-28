#include "VsgQtWidget.h"
#include <rocky_env/SwapchainReadback.h>
#include <simcore/GeoNavigation.h>

#include <QApplication>
#include <QGuiApplication>
#include <QShowEvent>
#include <QResizeEvent>
#include <QWindow>
#include <QWidget>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <chrono>

#include <vsg/app/Window.h>
#include <vsg/app/WindowTraits.h>
#include <rocky_env/GpuContext.h>
#include <rocky_env/RockyGraphicsFeatures.h>
#if VSGRAYSIM_WITH_ROCKY
#include <rocky/vsg/ViewDependentState.h>
#endif
#include <vsg/app/Viewer.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/imgui.h>
#include <vsgImGui/implot.h>

#include "ViewportKit.h"

#include <filesystem>
#include <vsg/app/View.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/app/Camera.h>
#include <vsg/app/CloseHandler.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/read.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/vk/Instance.h>
#include <vsg/vk/Device.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/vk/Surface.h>
#include <vsgXchange/all.h>

// Qt6 platform native interface (for xcb_connection_t* extraction).
#include <qpa/qplatformnativeinterface.h>

// Xcb native handles (only used when the xcb platform plugin is active).
#include <xcb/xcb.h>

// Vulkan Xcb surface extension - used to call vkCreateXcbSurfaceKHR
// directly via the function pointer obtained from vsg::Instance.
#include <vulkan/vulkan_xcb.h>

namespace vsgraysim { namespace gui {
namespace {
// GUI-thread cache: multiple existing console viewports use the same logical
// device/instance. Some deployed VSG builds support only deviceID 0. An observer
// does not keep an otherwise closed viewer/device alive.
vsg::observer_ptr<vsg::Device>& viewportDevice(){static vsg::observer_ptr<vsg::Device> device;return device;}

struct ImGuiScope {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    ImPlotContext* previousPlot=ImPlot::GetCurrentContext();
    ImGuiScope(ImGuiContext* context,ImPlotContext* plot) {
        ImGui::SetCurrentContext(context);ImPlot::SetCurrentContext(plot);
    }
    ~ImGuiScope(){ImGui::SetCurrentContext(previous);ImPlot::SetCurrentContext(previousPlot);}
};

// vsgImGui initializes and destroys the contexts active during its own calls.
// Keep each render graph's renderer/backend attached to its own contexts.
class ViewportImGui final : public vsg::Inherit<vsg::Group,ViewportImGui> {
public:
    explicit ViewportImGui(vsg::Window* window):window_(window) {
        ImGuiScope scope(nullptr,nullptr);
        context_=ImGui::CreateContext();
        auto& io=ImGui::GetIO();
        const auto font=ViewportKit::resolveAssetPath("fonts/NotoSansCJKSC.ttf");
        if(std::filesystem::exists(font))io.Fonts->AddFontFromFileTTF(font.c_str(),17.f,nullptr,io.Fonts->GetGlyphRangesChineseFull());
        ImGui::StyleColorsDark();
        auto& style=ImGui::GetStyle();
        style.WindowRounding=8;style.FrameRounding=5;style.GrabRounding=4;
        style.WindowPadding={10,8};style.FramePadding={8,4};style.ItemSpacing={8,6};
        style.Colors[ImGuiCol_WindowBg]={.055f,.067f,.094f,.90f};
        style.Colors[ImGuiCol_Border]={.35f,.43f,.55f,.45f};
        auto renderer=vsgImGui::RenderImGui::create(window_);
        plot_=ImPlot::GetCurrentContext();
        renderer_=renderer.get();
        addChild(renderer);
    }
    vsgImGui::RenderImGui* renderer()const{return renderer_;}
    void accept(vsg::RecordTraversal& rt)const override {
        ImGuiScope scope(context_,plot_);
        const auto extent=window_->extent2D();
        ImGui::GetIO().DisplaySize={float(extent.width),float(extent.height)};
        renderer_->accept(rt);
    }
private:
    ~ViewportImGui()override {
        ImGuiScope scope(context_,plot_);
        if(scope.previous==context_)scope.previous=nullptr;
        if(scope.previousPlot==plot_)scope.previousPlot=nullptr;
        children.clear();
    }
    vsg::ref_ptr<vsg::Window> window_;
    ImGuiContext* context_=nullptr;
    ImPlotContext* plot_=nullptr;
    vsgImGui::RenderImGui* renderer_=nullptr;
};
}

// ---------------------------------------------------------------------------
// VsgQtWidget
// ---------------------------------------------------------------------------
// Embedding strategy (revised: direct native-window approach)
// ---------------------------------------------------------------------
// Previous approach used QWindow + createWindowContainer, which was fragile:
// the QWindow's surface didn't always render visibly inside the container.
//
// New approach:
// 1.  Make this QWidget a native window (WA_NativeWindow) so it has a
//     real xcb_window_t accessible via winId().
// 2.  Extract the xcb_window_t + xcb_connection_t* from Qt's platform
//     native interface.
// 3.  Build a vsg::Instance ourselves (with VK_KHR_surface +
//     VK_KHR_xcb_surface extensions).
// 4.  Resolve vkCreateXcbSurfaceKHR and call it directly with our
//     vsg::Instance's VkInstance + the QWidget's native xcb window +
//     xcb connection, producing a VkSurfaceKHR bound to *this* widget.
// 5.  Wrap the VkSurfaceKHR in vsg::Surface and inject into a
//     vsg::Window via setInstance()/setSurface() before any
//     getOrCreateDevice() call.
// 6.  releaseWindow()/releaseConnection() so VSG doesn't destroy the
//     Qt-owned xcb window on close.
//
// This is simpler and more reliable than the QWindow approach because
// the xcb_window_t we render to is the *same* window Qt paints - there
// is no intermediate QWindow/container layer.
// ---------------------------------------------------------------------------

VsgQtWidget::VsgQtWidget(QWidget* parent)
    : QWidget(parent)
{
    // CRITICAL: force this widget to be a real native window so winId()
    // returns a valid xcb_window_t immediately after show().
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);   // we paint via Vulkan, not Qt
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setMinimumSize(640, 360);
    setFocusPolicy(Qt::StrongFocus);

    // Note: we do NOT create a QVulkanInstance here.  QVulkanInstance is
    // only useful for QWindow-based Vulkan rendering.  Since we render
    // directly to this QWidget's native xcb_window via a VkSurfaceKHR
    // that we create ourselves (in initVsg()), QVulkanInstance is not
    // needed.  VSG creates its own vsg::Instance/VkInstance.

    // Defer VSG init until the native window has been realised (showEvent).
}

VsgQtWidget::~VsgQtWidget() {
    if (viewer_) {
        try { viewer_->close(); } catch (...) {}
    }
    // The xcb_window_t and xcb_connection_t are owned by Qt.  Release them
    // from VSG's Xcb_Window so its destructor neither destroys Qt's native
    // window nor calls xcb_disconnect() on Qt's connection - the latter
    // kills the whole application ("The X11 connection broke").
    if (window_) {
        try {
            window_->releaseWindow();
            window_->releaseConnection();
        } catch (...) {}
    }
}

void VsgQtWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!init_attempted_) {
        init_attempted_ = true;
        // 无头冒烟开关: 设置 VSGRAYSIM_NO_VIEWPORT 时跳过 Vulkan 渲染初始化,
        // 3D 视口保持空白, 用于无人值守回归验证仿真核心链路 (不依赖显卡/显示)。
        if (std::getenv("VSGRAYSIM_NO_VIEWPORT")) {
            spdlog::info("VsgQtWidget: VSGRAYSIM_NO_VIEWPORT set - viewport rendering disabled");
            return;
        }
        // Defer a few event-loop ticks so the native window handle is
        // fully realised and has a valid size before we hand it to VSG.
        QTimer::singleShot(50, this, [this]() {
            if (!vsg_inited_) initVsg();
        });
    }
}

void VsgQtWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // VSG's Window::resize() reads the native window's size, so we just
    // need to trigger it.  The xcb_window_t we render to is this widget's
    // own native window, which Qt has already resized for us.
    if (window_) {
        try { window_->resize(); } catch (...) {}
    }
}

bool VsgQtWidget::initVsg() try {
    if (vsg_inited_) return true;

    // ---- 1. Resolve the native Xcb window handle & connection ------
    // Force a native window to be created if it hasn't been already.
    WId wid = winId();
    if (wid == 0) {
        spdlog::error("VsgQtWidget: winId() returned 0 - cannot embed VSG");
        return false;
    }

    xcb_connection_t* conn = nullptr;
    auto* native_iface = QGuiApplication::platformNativeInterface();
    if (native_iface) {
        // For a QWidget (not QWindow), we need to get the connection
        // via the platform-native interface.  The "connection" resource
        // is global (same for all widgets on the same display).
        void* raw = native_iface->nativeResourceForIntegration("connection");
        conn = reinterpret_cast<xcb_connection_t*>(raw);
    }
    if (!conn) {
        spdlog::error("VsgQtWidget: could not obtain xcb_connection_t via "
                     "platformNativeInterface - cannot create VkSurfaceKHR");
        return false;
    }

    xcb_window_t xcb_win = static_cast<xcb_window_t>(wid);
    spdlog::info("VsgQtWidget: native xcb_window=0x{:x} conn={} widget size={}x{}",
                static_cast<unsigned long>(xcb_win),
                reinterpret_cast<void*>(conn),
                width(), height());

    // ---- 2. Build vsg::Instance + VkSurfaceKHR for Qt widget ------
    // We create our own VkInstance and VkSurfaceKHR bound to the Qt
    // widget's native xcb_window.  We do NOT use vsg::Window::create()
    // because that would create its own xcb_window_t, which would render
    // to a hidden window instead of our Qt widget.
    vsg::Names instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    };
    vsg::Names layers;
    auto sharedDevice=viewportDevice().ref_ptr();
    auto vsg_instance = sharedDevice?vsg::ref_ptr<vsg::Instance>(sharedDevice->getInstance()):
        vsg::Instance::create(instanceExtensions,layers,VK_API_VERSION_1_2);
    if (!vsg_instance || !vsg_instance->vk()) {
        spdlog::error("VsgQtWidget: vsg::Instance::create failed - "
                     "no Vulkan ICD available (check VK_ICD_FILE_LIST)");
        return false;
    }
    spdlog::info("VsgQtWidget: vsg::Instance created (VkInstance={})",
                reinterpret_cast<void*>(vsg_instance->vk()));

    // Pin the first instance for the process lifetime.  The Vulkan loader
    // heap-corrupts when dlclose()ing the NVIDIA ICD after the last
    // VkInstance is destroyed (loader_unload_preloaded_icds -> malloc
    // corruption on exit / graceful window close).  Keeping one instance
    // alive prevents the loader from unloading ICD shared objects.
    // Deliberately leaked on the heap so it outlives static destructors.
    static auto* s_pinned_instance =
        new vsg::ref_ptr<vsg::Instance>(vsg_instance);
    (void)s_pinned_instance;

    // Resolve vkCreateXcbSurfaceKHR and create a VkSurfaceKHR on *this
    // widget's native xcb_window.
    PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR = nullptr;
    vsg_instance->getProcAddr(vkCreateXcbSurfaceKHR, "vkCreateXcbSurfaceKHR");
    if (!vkCreateXcbSurfaceKHR) {
        spdlog::error("VsgQtWidget: vkCreateXcbSurfaceKHR not available");
        return false;
    }

    VkXcbSurfaceCreateInfoKHR surf_ci{};
    surf_ci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    surf_ci.connection = conn;
    surf_ci.window     = xcb_win;
    VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
    VkResult vr = vkCreateXcbSurfaceKHR(vsg_instance->vk(), &surf_ci,
                                        vsg_instance->getAllocationCallbacks(),
                                        &vk_surface);
    if (vr != VK_SUCCESS || vk_surface == VK_NULL_HANDLE) {
        spdlog::error("VsgQtWidget: vkCreateXcbSurfaceKHR failed (VkResult={})",
                    static_cast<int>(vr));
        return false;
    }
    spdlog::info("VsgQtWidget: VkSurfaceKHR created for Qt widget ({})",
                reinterpret_cast<void*>(vk_surface));

    // Wrap the VkSurfaceKHR in vsg::Surface.
    auto vsg_surface = vsg::Surface::create(vk_surface, vsg_instance);

    // ---- 3. Build VSG WindowTraits with native window injection ------
    // We still create a WindowTraits, but we pass the Qt widget's native
    // window so VSG's Xcb_Window uses it instead of creating its own.
    auto traits = vsg::WindowTraits::create();
    traits->device=sharedDevice;
    traits->windowTitle = "vsgraysim - VSG 3D Scene";
    // This is an existing Qt native child, not a new top-level VSG window.
    // Forcing 640x360 resizes the child beyond a narrow dock's geometry.
    int w = std::max(1, width());
    int h = std::max(1, height());
    traits->width  = static_cast<uint32_t>(w);
    traits->height = static_cast<uint32_t>(h);
    traits->debugLayer  = false;
    traits->apiDumpLayer = false;
    traits->decoration = false;
    // 4x MSAA: smooths the thin grid lines / terrain silhouette.
    traits->samples = VK_SAMPLE_COUNT_4_BIT;

    // Present without vsync throttling.  The default FIFO mode stalled
    // vkQueuePresentKHR ~300 ms/frame against this Qt-owned native child
    // window (the queue appears occluded by the parent's backing-store
    // updates), which froze the GUI-thread render loop and starved mouse
    // input.  vsg::selectSwapPresentMode() falls back to MAILBOX and then
    // FIFO when IMMEDIATE is unsupported, so this is safe everywhere.
    traits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

    // CRITICAL: Pass the Qt widget's native window + connection so VSG's
    // Xcb_Window uses them instead of creating its own xcb_window_t.
    traits->nativeWindow    = std::any(xcb_win);
    traits->systemConnection = std::any(conn);
    traits->instanceExtensionNames.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    traits->instanceExtensionNames.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

    // ---- 4. Construct the VSG Window (Xcb_Window) ----------------
    // With nativeWindow set, Xcb_Window should use our xcb_window_t.
    try {
        window_ = vsg::Window::create(traits);
    } catch (const std::exception& e) {
        spdlog::error("VsgQtWidget: vsg::Window::create failed: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("VsgQtWidget: vsg::Window::create failed (unknown)");
        return false;
    }
    if (!window_) {
        spdlog::error("VsgQtWidget: vsg::Window is null after create");
        return false;
    }

    // Inject our pre-built instance + surface so VSG uses them.
    try {
        window_->setInstance(vsg_instance);
        window_->setSurface(vsg_surface);
        if(sharedDevice)window_->setDevice(sharedDevice);
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(*window_->getOrCreatePhysicalDevice(), vk_surface, &capabilities) == VK_SUCCESS &&
            (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
            traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        // Preserve host graphics capabilities when adding sensor extensions.
        if(!traits->deviceFeatures) {
            traits->deviceFeatures=vsg::DeviceFeatures::create();
            traits->deviceFeatures->get()=window_->getOrCreatePhysicalDevice()->getFeatures();
        }
#if VSGRAYSIM_WITH_ROCKY
        rockyenv::configureRockyGraphicsFeatures(*window_->getOrCreatePhysicalDevice(),traits->deviceExtensionNames,traits->deviceFeatures);
#endif
        rockyenv::sensor::GpuContext::configureDevice(*window_->getOrCreatePhysicalDevice(),traits->deviceExtensionNames,traits->deviceFeatures);
    } catch (const vsg::Exception& e) {
        spdlog::error("VsgQtWidget: setInstance/setSurface failed: {} (VkResult={})",
                     e.message, e.result);
        return false;
    } catch (const std::exception& e) {
        spdlog::error("VsgQtWidget: setInstance/setSurface failed: {}", e.what());
        return false;
    }

    // NOTE: We do NOT call releaseWindow()/releaseConnection() here.
    // Those methods set _window and _connection to 0, which makes valid()
    // return false and prevents rendering. Instead, we'll call them in
    // the destructor to prevent VSG from destroying Qt's window.

    // ---- 5. Build the VSG viewer + camera + view ------------------
    viewer_ = vsg::Viewer::create();
    viewer_->addWindow(window_);

    int actual_w = 800, actual_h = 600;
    try {
        actual_w = static_cast<int>(window_->extent2D().width);
        actual_h = static_cast<int>(window_->extent2D().height);
    } catch (const vsg::Exception& e) {
        spdlog::error("VsgQtWidget: window extent2D() failed: {} (VkResult={})",
                     e.message, e.result);
        throw;
    } catch (const std::exception& e) {
        spdlog::error("VsgQtWidget: window extent2D() failed: {}", e.what());
        return false;
    }

    // Camera positioned closer to see diagnostic objects clearly.
    // Camera at (0, 50, 100) looking at origin (0, 0, 0) so the
    // diagnostic box at (-25, 5, -50) and cylinder at (-10, 5, 50)
    // are both visible in the view.
    auto lookAt = vsg::LookAt::create(
        vsg::dvec3(0.0, 50.0, 100.0),
        vsg::dvec3(0.0,  0.0,   0.0),
        vsg::dvec3(0.0,   1.0,   0.0));
    // 近/远平面放宽: far 提升到 50000 使缩小(远离)时地形不被 far 平面裁掉,
    // near 保持 1.0 提供足够放大细节且避免 z-fighting。
    auto persp = vsg::Perspective::create(60.0, double(actual_w)/double(actual_h),
                                          1.0, 50000.0);
    camera_ = vsg::Camera::create(persp, lookAt);

    view_   = vsg::View::create(camera_);
    // Rocky terrain pipelines use extended view descriptors (render/frustum/
    // decal parameters). A stock VSG VDS leaves that layout unresolved.
#if VSGRAYSIM_WITH_ROCKY
    view_->viewDependentState=rocky::ViewDependentStateEx::create(view_,window_->getOrCreateDevice());
#endif
    view_->viewDependentState->maxShadowDistance=12000.;
    view_->viewDependentState->shadowMapBias=.00005;
    root_   = vsg::Group::create();
    overlay_ = vsg::Group::create();
    view_->addChild(root_);
    view_->addChild(overlay_);

    // Command graph - triggers device/swapchain creation.
    auto cg = vsg::CommandGraph::create(window_);
    pre_render_=vsg::Group::create();
    cg->addChild(pre_render_);
    auto rg = vsg::RenderGraph::create(window_, view_);
    rg->setClearValues({{0.10f, 0.11f, 0.13f, 1.0f}});
    cg->addChild(rg);
    if (traits->swapchainPreferences.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        readback_ = rockyenv::SwapchainReadback::create();
        readback_->window = window_;
        readback_->request = &capture_requested_;
        cg->addChild(readback_);
    }
    viewer_->assignRecordAndSubmitTaskAndPresentation({cg});
    const auto& queueFamilies=window_->getOrCreatePhysicalDevice()->getQueueFamilyProperties();
    supports_clustered_draws_=cg->queueFamily>=0&&(queueFamilies.at(size_t(cg->queueFamily)).queueFlags&VK_QUEUE_COMPUTE_BIT);

    if (imgui_enabled_) {
        auto overlay=ViewportImGui::create(window_.get());
        imgui_=overlay->renderer();
        rg->addChild(overlay);
        spdlog::info("VsgQtWidget: ImGui overlay enabled");
    }

    // Options with vsgXchange readers (for asset loading).
    options_ = vsg::Options::create();
    options_->paths = { "models", "config", "../config", "../models",
                         "../build/models", "build/models" };
    if (auto ax = vsgXchange::all::create()) {
        options_->readerWriters = { ax };
        spdlog::info("VsgQtWidget: vsgXchange::all readers registered");
    } else {
        spdlog::warn("VsgQtWidget: vsgXchange::all::create() returned null - "
                    "only built-in loaders will be used");
    }

    // NOTE: We do NOT compile the viewer here. The viewer will be compiled
    // after models are loaded in MainWindow::loadDefaultModels() to ensure
    // all scene nodes are properly included in the initial compile.

    viewportDevice()=window_->getDevice();
    vsg_inited_ = true;

    // Never let Qt flush its backing-store region onto the native window we
    // render to.  With WA_PaintOnScreen the widget's paint/flush goes
    // straight to the xcb window, so an uninitialised (black) backing store
    // was being blitted over the presented Vulkan frames ("all black"
    // viewport + "QWidget::paintEngine: Should no longer be called").
    // Disabling updates keeps the widget permanently clean, exposing the
    // Vulkan content; input events are unaffected.
    setUpdatesEnabled(false);

    render_clock_.start();
    last_present_ms_ = 0;

    spdlog::info("VsgQtWidget: VSG rendering initialised on native widget ({}x{})",
                actual_w, actual_h);

    emit vsgReady();
    return true;
}catch(const vsg::Exception& error) {
    vsg_inited_=false;const auto message=QString::fromStdString(error.message)+QString(" (VkResult=%1)").arg(int(error.result));
    spdlog::error("VsgQtWidget: initialization failed: {}",message.toStdString());emit initializationFailed(message);return false;
}catch(const std::exception& error) {
    vsg_inited_=false;const auto message=QString::fromUtf8(error.what());
    spdlog::error("VsgQtWidget: initialization failed: {}",error.what());emit initializationFailed(message);return false;
}

void VsgQtWidget::setScene(vsg::ref_ptr<vsg::Node> root) {
    if (!root_) return;
    root_->children.clear();
    if (root) root_->addChild(root);
}

void VsgQtWidget::addToOverlay(vsg::ref_ptr<vsg::Node> node) {
    if (!overlay_ || !node) return;
    overlay_->addChild(node);
}

void VsgQtWidget::addImGuiDraw(const std::function<bool()>& draw)
{
    auto* render_imgui = imgui_;
    if (!render_imgui || !draw) return;
    // 透传内层返回值: 回调失效时返回 false (老版本 RenderImGui 若支持
    // false 自动移除则顺势清退; 不支持则仅为每帧一次 bool 判断, 无害)。
    render_imgui->add([draw]() -> bool { return draw(); });
}

bool VsgQtWidget::frame() {
    static int frame_count = 0;
    frame_count++;

    if (!vsg_inited_ || !viewer_ || !window_) {
        return false;
    }
    if (!window_->valid()) {
        return false;
    }

    const int64_t now_ms = render_clock_.elapsed();
    if (!capture_requested_ && last_present_ms_ > 0 && (now_ms - last_present_ms_) < 16) {
        return true;
    }

    try {
        // Compile on the render tick instead of immediately from the GUI thread.
        // This avoids blocking input and frame delivery when scene updates arrive
        // in bursts.
        if (compile_requested_ || (!viewer_compiled_ && root_ && !root_->children.empty())) {
            try {
                // Full graph recompilation can replace descriptor pools and
                // transfer lists. Complete outgoing submissions before mutation.
                if(viewer_compiled_) viewer_->deviceWaitIdle();
                const auto res = viewer_->compile();
                if(!res) {
                    spdlog::error("VsgQtWidget: compile failed: {}",res.message);
                    return false;
                }
                // Paging compiles against the actual initialized Views. Never
                // append device-only Contexts: their VDS layout is unresolved.
                if(!viewer_->compileManager)
                    viewer_->compileManager=vsg::CompileManager::create(*viewer_,vsg::ref_ptr<vsg::ResourceHints>{});
                viewer_compiled_ = true;
                compile_requested_ = false;
                setProperty("graphCompilationCount",property("graphCompilationCount").toULongLong()+qulonglong(1));
                spdlog::info("VsgQtWidget: compile() result={} ('{}')",
                             int(res.result), res.message);
            } catch (const vsg::Exception& e) {
                // vsg::Exception 不继承 std::exception, 必须单独捕获,
                // 否则 swapchain/资源失败会直接 terminate。
                spdlog::error("VsgQtWidget: compile() failed: {} (VkResult={})",
                              e.message, e.result);
                return false;
            } catch (const std::exception& e) {
                spdlog::error("VsgQtWidget: compile() failed: {}", e.what());
                return false;
            }
        }

        if (!viewer_compiled_) {
            return true;
        }

        if (camera_override_) {
            auto* look = dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get());
            auto* perspective = dynamic_cast<vsg::Perspective*>(camera_->projectionMatrix.get());
            if (look && perspective) {
                const auto clip = simcore::geo::cameraClip({look->eye,look->center,look->up});
                perspective->nearDistance = std::isfinite(near_clip_override_)&&near_clip_override_>0?near_clip_override_:clip.first;
                perspective->farDistance = clip.second;
            }
        }

        const auto renderBegin=std::chrono::steady_clock::now();
        if (!viewer_->advanceToNextFrame()) {
            return false;
        }

        viewer_->handleEvents();
        emit beforeSceneRecord();
        viewer_->update();
        viewer_->recordAndSubmit();
        viewer_->present();
        const bool fenced=std::getenv("VSGRAYSIM_BENCHMARK_FENCES")!=nullptr;
        if(fenced&&viewer_->waitForFences(0,UINT64_MAX)!=VK_SUCCESS)throw std::runtime_error("render benchmark fence failed");
        setProperty("renderFrameMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderBegin).count());
        setProperty("renderFrameFenced",fenced);
        setProperty("renderFrameSerial",property("renderFrameSerial").toULongLong()+qulonglong(1));

        // Limit start-to-start cadence; rendering itself already consumes
        // frame budget. Waiting again after present halves effective throughput.
        last_present_ms_ = now_ms;
        // 一次性调试转储 (VSGRAYSIM_DUMP_GRAPH=<帧号>)
        static int  dump_at   = [] {
            const char* e = std::getenv("VSGRAYSIM_DUMP_GRAPH");
            return e ? std::atoi(e) : 0;
        }();
        static bool dump_done = false;
        if (!dump_done && dump_at > 0 && frame_count >= dump_at) {
            dump_done = true;
            debugDumpGraph();
        }
        const bool active = viewer_->active();
        if (!active) {
            spdlog::warn("VsgQtWidget::frame() #{} - viewer no longer active", frame_count);
        }
        return active;
    } catch (const vsg::Exception& e) {
        // vsg::Exception 不继承 std::exception (vsg/core/Exception.h 是裸 struct),
        // swapchain/present 失败若漏出会直接 terminate 整个进程。
        // 记录后停止本帧, 连续失败由上层节流失效 (窗口重建后自动恢复)。
        spdlog::error("VsgQtWidget: frame() #{} vsg::Exception: {} (VkResult={})",
                      frame_count, e.message, e.result);
        return false;
    } catch (const std::exception& e) {
        spdlog::error("VsgQtWidget: frame() #{} exception: {}", frame_count, e.what());
        return false;
    }
}

void VsgQtWidget::requestCompile() {
    if (!viewer_ || !vsg_inited_) return;
    compile_requested_ = true;
}

// ---------------------------------------------------------------------------
// 场景图调试转储 (VSGRAYSIM_DUMP_GRAPH=<帧号> 触发, 一次性):
// 递归打印节点树; 小型几何 (顶点 ≤ 64) 打印顶点坐标, StateGroup 打印颜色 uniform。
// 用于定位视口中不明绘制来源。
// ---------------------------------------------------------------------------
namespace {
void dumpVsgNode(const vsg::Node* n, int depth, int& budget) {
    if (!n || budget <= 0) return;
    --budget;
    std::string ind(size_t(std::min(depth, 12)) * 2, ' ');
    std::string line = ind + n->className();
    if (const auto* g = dynamic_cast<const vsg::Group*>(n))
        line += " children=" + std::to_string(g->children.size());
    if (const auto* mt = dynamic_cast<const vsg::MatrixTransform*>(n)) {
        const auto& m = mt->matrix;
        line += " t=(" + std::to_string(m(3, 0)) + "," + std::to_string(m(3, 1)) +
                "," + std::to_string(m(3, 2)) + ")";
    }
    if (const auto* vid = dynamic_cast<const vsg::VertexIndexDraw*>(n)) {
        size_t nv = 0;
        const vsg::Data* pos_data = nullptr;
        if (!vid->arrays.empty() && vid->arrays[0] && vid->arrays[0]->data) {
            pos_data = vid->arrays[0]->data.get();
            nv = pos_data->valueCount();
        }
        line += " verts=" + std::to_string(nv);
        spdlog::info("DUMP {}", line);
        if (nv <= 64) {   // 小几何: 打印顶点
            if (const auto* pos = dynamic_cast<const vsg::vec3Array*>(pos_data)) {
                for (size_t i = 0; i < nv && budget > 0; ++i) {
                    const auto& v = (*pos)[i];
                    spdlog::info("DUMP {}   v[{}] = ({:.2f}, {:.2f}, {:.2f})",
                                 ind, i, v.x, v.y, v.z);
                    --budget;
                }
            }
        }
        return;
    }
    spdlog::info("DUMP {}", line);
    if (const auto* sg = dynamic_cast<const vsg::StateGroup*>(n)) {
        for (const auto& sc : sg->stateCommands) {
            const auto* bds = dynamic_cast<const vsg::BindDescriptorSet*>(sc.get());
            if (!bds || !bds->descriptorSet) continue;
            for (const auto& d : bds->descriptorSet->descriptors) {
                if (const auto* v4 = dynamic_cast<const vsg::vec4Value*>(d.get())) {
                    const auto& c = v4->value();
                    spdlog::info("DUMP {}   color=({:.3f}, {:.3f}, {:.3f}, {:.3f})",
                                 ind, c.x, c.y, c.z, c.w);
                    --budget;
                }
            }
        }
    }
    if (const auto* g = dynamic_cast<const vsg::Group*>(n))
        for (const auto& c : g->children) dumpVsgNode(c.get(), depth + 1, budget);
}
} // namespace

void VsgQtWidget::debugDumpGraph() {
    if (!root_) return;
    spdlog::info("DUMP ===== scene graph begin =====");
    int budget = 40000;
    dumpVsgNode(root_, 0, budget);
    spdlog::info("DUMP ===== scene graph end (budget left {}) =====", budget);
}

QImage VsgQtWidget::grabFrame() {
    auto unavailable=[&](const QString& reason){setProperty("frameCaptureStatus",reason);return QImage{};};
    if (!vsg_inited_ || !isVisible() || !window_ || !window_->valid()) {
        return unavailable(QStringLiteral("widget or native surface is not visible/ready"));
    }

    // An explicit readback schedules its own fenced frame. A continuously
    // rendering viewport must not starve screenshots behind a 32 ms gate.

    if (!readback_) return unavailable(QStringLiteral("no swapchain readback command"));
    if (capture_requested_) return unavailable(QStringLiteral("a frame capture is already active"));
    try {
        // A model preview can capture before the viewer has drawn its first frame.
        if (!window_->getOrCreateSwapchain()) return unavailable(QStringLiteral("no swapchain"));
    } catch (const vsg::Exception& error) {
        return unavailable(QString::fromStdString(error.message));
    }
    const auto format = window_->surfaceFormat().format;
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    if (!bgra && format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB) return unavailable(QStringLiteral("unsupported swapchain format"));
    auto device = window_->getDevice();
    const auto extent = window_->extent2D();
    const auto size = VkDeviceSize(extent.width) * extent.height * 4;
    if (!size) return unavailable(QStringLiteral("empty swapchain extent"));
    auto& buffer = readback_->dstBuffer;
    if (!buffer || buffer->size < size) {
        auto allocation = vsg::createBufferAndMemory(device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!allocation) return unavailable(QStringLiteral("readback allocation failed"));
        buffer = std::move(allocation);
    }
    readback_->recordedExtent = {};
    capture_requested_ = true;
    const bool active = frame();
    // Keep both request and destination alive until all recording/submission
    // work and the GPU copy are complete, including threaded viewer setups.
    const auto result = viewer_->waitForFences(0, UINT64_MAX);
    capture_requested_ = false;
    const auto captured = readback_->recordedExtent;
    if (!captured.width || !captured.height) return unavailable(QStringLiteral("copy not recorded; native visible=%1, active=%2").arg(window_->visible()).arg(active));
    if (!active || result != VK_SUCCESS) return unavailable(QStringLiteral("readback fence failed: %1").arg(int(result)));
    auto memory = buffer->getDeviceMemory(device->deviceID);
    void* bytes = nullptr;
    if (memory->map(buffer->getMemoryOffset(device->deviceID), size, 0, &bytes) != VK_SUCCESS) return unavailable(QStringLiteral("readback host mapping failed"));
    QImage image(static_cast<const uchar*>(bytes), int(captured.width), int(captured.height), int(captured.width * 4),
                 bgra ? QImage::Format_ARGB32 : QImage::Format_RGBA8888);
    QImage pixels = image.copy();
    memory->unmap();
    setProperty("frameCaptureStatus",QStringLiteral("captured"));
    return pixels;
}

void VsgQtWidget::setWorkspaceEarth(const simcore::geo::GeoAnchor& anchor, bool globe) {
    const bool changed = !workspace_anchor_ || workspace_globe_ != globe ||
        workspace_anchor_->lat_deg != anchor.lat_deg || workspace_anchor_->lon_deg != anchor.lon_deg ||
        workspace_anchor_->alt_m != anchor.alt_m || workspace_anchor_->heading_deg != anchor.heading_deg;
    workspace_anchor_ = anchor;
    workspace_globe_ = globe;
    if (changed) {
        workspace_camera_ = {};
        applyWorkspaceCamera();
    }
}

bool VsgQtWidget::applyWorkspaceCamera() {
    if (!camera_ || !workspace_anchor_) return false;
    auto look = camera_->viewMatrix.cast<vsg::LookAt>();
    if (!look) return false;
    const auto pose = simcore::geo::workspaceEarthCamera(*workspace_anchor_,workspace_globe_,workspace_camera_);
    look->eye = pose.eye;
    look->center = pose.center;
    look->up = pose.up;
    if (auto perspective = camera_->projectionMatrix.cast<vsg::Perspective>()) {
        const auto clip = simcore::geo::cameraClip(pose);
        perspective->nearDistance = clip.first;
        perspective->farDistance = clip.second;
    }
    return true;
}

void VsgQtWidget::resetCamera() {
    if (!camera_) return;
    auto lookAt0 = dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get());
    // [临时-天空标定钩子] VSGRAYSIM_SKY_VIEW="方位角,高度角[,视距]" 强制看向该方位。
    if (lookAt0) {
        if (const char* cv = std::getenv("VSGRAYSIM_SKY_VIEW")) {
            double az = 0.0, el = 20.0, dd = 300.0;
            if (sscanf(cv, "%lf,%lf,%lf", &az, &el, &dd) >= 2) {
                const double ar = az * 3.14159265 / 180.0;
                const double er = el * 3.14159265 / 180.0;
                const vsg::dvec3 sd(std::cos(er)*std::sin(ar), std::sin(er),
                                   -std::cos(er)*std::cos(ar));
                lookAt0->center = vsg::dvec3(0.0, 0.0, 0.0);
                lookAt0->eye    = lookAt0->center + sd * dd;
                lookAt0->up     = vsg::dvec3(0.0, 1.0, 0.0);
                spdlog::info("[SKY_CALIB] camera -> az={} el={}", az, el);
                return;
            }
        }
    }
    workspace_camera_ = {};
    if (applyWorkspaceCamera()) return;
    // 地球底座 (rocky_map) 自定义相机: 命中则跳过局部包围盒自适应。
    if (camera_override_ && camera_override_(camera_.get())) return;
    auto lookAt = dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get());
    if (!lookAt) return;

    // 自适应场景包围盒: 以场景中心为注视点, 视距按包围球半径取值
    // (60° 视场下 dist = r / tan(30°) ≈ 1.73r, 取 2.2r 留边距), 大小场景
    // 均能一屏收全 —— 不再依赖为 1000m 地形硬编码的固定观察位。
    vsg::dvec3 target(0.0, 0.0, 0.0);
    double     dist   = 1000.0;   // 空场景兜底 (与旧默认观感一致)
    double     radius = 500.0;    // 兜底包围球半径
    if (root_ && !root_->children.empty()) {
        try {
            vsg::ComputeBounds cb;
            root_->accept(cb);
            const vsg::dvec3 mn(cb.bounds.min), mx(cb.bounds.max);
            const bool finite =
                std::isfinite(mn.x) && std::isfinite(mn.y) && std::isfinite(mn.z) &&
                std::isfinite(mx.x) && std::isfinite(mx.y) && std::isfinite(mx.z);
            if (finite) {
                target = (mn + mx) * 0.5;
                const double r = vsg::length(mx - target);
                // 半径异常 (非有限坐标/超大值, 如未初始化顶点) 时保持兜底。
                if (r > 1e-3 && r < 1e7) radius = r;
            }
            dist = radius * 2.2;
            spdlog::info("VsgQtWidget: resetCamera bounds=({:.1f},{:.1f},{:.1f})..({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) dist={:.1f}",
                         mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, target.x, target.y, target.z, dist);
        } catch (const std::exception& e) {
            spdlog::warn("VsgQtWidget: ComputeBounds failed: {}", e.what());
        }
    }
    // 自适应近/远平面: far 必须覆盖 dist + 包围球半径, 否则超大场景
    // (如 55km 城市模型) 会整体落在 far 之外 —— 场景打开一片空白,
    // 缩放后才可见。near 随视距缩放, 保证近处细节与深度精度。
    if (auto persp = dynamic_cast<vsg::Perspective*>(camera_->projectionMatrix.get())) {
        persp->farDistance  = std::clamp(dist + radius * 1.6, 20000.0, 500000.0);
        persp->nearDistance = std::clamp(dist * 0.002, 0.1, 5.0);
    }
    // 观察方向保持原俯视角 (约 35°): (0, 0.6, 0.8) 单位化。
    const vsg::dvec3 dir = vsg::normalize(vsg::dvec3(0.0, 0.6, 0.8));
    lookAt->eye    = target + dir * dist;
    lookAt->center = target;
    lookAt->up     = vsg::dvec3(0.0, 1.0, 0.0);
}

// ---------------------------------------------------------------------------
// Qt mouse/keyboard event handling (Blender-style camera navigation)
// ---------------------------------------------------------------------------
// All navigation (orbit/pan/zoom) is implemented directly on the LookAt
// matrix in mouseMoveEvent/wheelEvent; no VSG event handlers are involved.
//
// When terrain brush editing is active (`brush_drag_active_`), left-drag
// is redirected to the host's edit callback (world-space ray) instead of
// rotating the camera.
// ---------------------------------------------------------------------------

bool VsgQtWidget::mouseRay(const QPoint& pos, vsg::dvec3& origin,
                           vsg::dvec3& dir) const
{
    if (!camera_ || !camera_->viewMatrix || !camera_->projectionMatrix)
        return false;
    if (width() <= 0 || height() <= 0) return false;

    const double ndc_x = 2.0 * double(pos.x()) / double(width()) - 1.0;
    const double ndc_y = 1.0 - 2.0 * double(pos.y()) / double(height());

    const vsg::dmat4 inv_proj = camera_->projectionMatrix->inverse();
    const vsg::dmat4 inv_view = camera_->viewMatrix->inverse();

    const vsg::dvec4 pn = inv_proj * vsg::dvec4(ndc_x, ndc_y, -1.0, 1.0);
    const vsg::dvec4 pf = inv_proj * vsg::dvec4(ndc_x, ndc_y,  1.0, 1.0);
    if (std::abs(pn.w) < 1e-12 || std::abs(pf.w) < 1e-12) return false;

    const vsg::dvec3 wn(pn.x / pn.w, pn.y / pn.w, pn.z / pn.w);
    const vsg::dvec3 wf(pf.x / pf.w, pf.y / pf.w, pf.z / pf.w);

    const vsg::dvec4 o = inv_view * vsg::dvec4(wn, 1.0);
    const vsg::dvec4 f = inv_view * vsg::dvec4(wf, 1.0);
    if (std::abs(o.w) < 1e-12 || std::abs(f.w) < 1e-12) return false;

    origin = vsg::dvec3(o.x / o.w, o.y / o.w, o.z / o.w);
    const vsg::dvec3 far_pt(f.x / f.w, f.y / f.w, f.z / f.w);
    dir = far_pt - origin;
    const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-12) return false;
    dir /= len;
    return true;
}

void VsgQtWidget::mousePressEvent(QMouseEvent* event) {
    last_mouse_pos_    = event->pos();
    last_mouse_button_ = event->button();
    drag_modifiers_    = event->modifiers();

    // Analysis pick tool (two-point analyses): left-press reports one ray.
    // Takes precedence over brush editing on the left button.
    if (pick_mode_active_ && event->button() == Qt::LeftButton && pick_ray_cb_) {
        vsg::dvec3 origin, dir;
        if (mouseRay(event->pos(), origin, dir)) pick_ray_cb_(origin, dir);
        event->accept();
        return;
    }

    // Host tool (terrain brush / scene placement): left-press ray-casts.
    if (brush_drag_active_ && event->button() == Qt::LeftButton && edit_ray_cb_) {
        emit brushDragStarted();
        vsg::dvec3 origin, dir;
        if (mouseRay(event->pos(), origin, dir)) edit_ray_cb_(origin, dir);
        event->accept();
        return;
    }

    if (!camera_) return;

    // Accept the event to receive subsequent mouse move events
    event->accept();
}

void VsgQtWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (brush_drag_active_ && last_mouse_button_ == Qt::LeftButton) {
        last_mouse_button_ = Qt::NoButton;
        event->accept();
        return;
    }
    last_mouse_button_ = Qt::NoButton;
    drag_modifiers_    = Qt::NoModifier;
    event->accept();
}

// ---------------------------------------------------------------------------
// Blender-style camera navigation
// ---------------------------------------------------------------------------
namespace {

/// Orbit the eye around `center` using azimuth/elevation spherical coords.
void orbitLookAt(vsg::LookAt* lookAt, double d_azimuth, double d_elevation, bool geographic)
{
    if (geographic) {
        const auto pose = simcore::geo::orbitCamera({lookAt->eye,lookAt->center,lookAt->up},d_azimuth,d_elevation);
        lookAt->eye=pose.eye;lookAt->center=pose.center;lookAt->up=pose.up;
        return;
    }
    const vsg::dvec3 offset = lookAt->eye - lookAt->center;
    const double dist = vsg::length(offset);
    if (dist < 1e-6) return;
    double azimuth   = std::atan2(offset.x, offset.z);
    double elevation = std::asin(std::clamp(offset.y / dist, -1.0, 1.0));
    azimuth  += d_azimuth;
    elevation = std::clamp(elevation + d_elevation, -1.54, 1.54);
    const double ce = std::cos(elevation);
    lookAt->eye = lookAt->center +
        dist * vsg::dvec3(ce * std::sin(azimuth), std::sin(elevation),
                          ce * std::cos(azimuth));
}

/// Pan eye+center along the camera's screen-plane axes.
void panLookAt(vsg::LookAt* lookAt, double dx_px, double dy_px, bool geographic)
{
    if (geographic) {
        const double scale=vsg::length(lookAt->center-lookAt->eye)*.0016;
        const auto pose=simcore::geo::panCamera({lookAt->eye,lookAt->center,lookAt->up},-dx_px*scale,dy_px*scale);
        lookAt->eye=pose.eye;lookAt->center=pose.center;lookAt->up=pose.up;
        return;
    }
    const vsg::dvec3 fwd = vsg::normalize(lookAt->center - lookAt->eye);
    vsg::dvec3 right     = vsg::cross(fwd, lookAt->up);
    if (vsg::length(right) < 1e-9) right = vsg::dvec3(1.0, 0.0, 0.0);
    right = vsg::normalize(right);
    const vsg::dvec3 up = vsg::normalize(vsg::cross(right, fwd));
    // Blender-like feel: pan speed grows with distance to the pivot.
    const double dist = vsg::length(lookAt->center - lookAt->eye);
    const double s    = dist * 0.0016;
    const vsg::dvec3 shift = right * (-dx_px * s) + up * (dy_px * s);
    lookAt->eye    += shift;
    lookAt->center += shift;
}

/// 当前投影 far 平面距离 (缩放上限与之对齐, resetCamera 会自适应调整 far)。
double projectionFar(vsg::Camera* cam)
{
    if (cam)
        if (auto persp = dynamic_cast<vsg::Perspective*>(cam->projectionMatrix.get()))
            return persp->farDistance;
    return 50000.0;
}

/// Dolly the eye toward/away from the center by a multiplicative factor.
void zoomLookAt(vsg::LookAt* lookAt, double factor, double max_dist)
{
    const vsg::dvec3 dir = lookAt->center - lookAt->eye;
    double dist = vsg::length(dir);
    if (dist < 1e-6) return;
    // 缩放范围与投影 near/far 对齐: 上限取 far*0.9 (>= near 最小 1.0),
    // 避免放大过近时地形被 near 平面裁剪、拉远过甚时被 far 平面裁剪。
    dist = std::clamp(dist * factor, 1.0, std::max(1000.0, max_dist * 0.9));
    lookAt->eye = lookAt->center - vsg::normalize(dir) * dist;
}

} // anonymous namespace

void VsgQtWidget::mouseMoveEvent(QMouseEvent* event) {
    // Analysis pick tool active: picks are click-based; swallow left-drags
    // so they neither brush the terrain nor orbit the camera.
    if (pick_mode_active_ && last_mouse_button_ == Qt::LeftButton) {
        event->accept();
        return;
    }

    // Host tool drag (brush / placement): forward every move while held.
    if (brush_drag_active_ && last_mouse_button_ == Qt::LeftButton &&
        edit_ray_cb_) {
        vsg::dvec3 origin, dir;
        if (mouseRay(event->pos(), origin, dir)) edit_ray_cb_(origin, dir);
        event->accept();
        return;
    }

    if (!camera_) return;
    if (last_mouse_button_ == Qt::NoButton) return;

    const QPoint delta = event->pos() - last_mouse_pos_;
    last_mouse_pos_    = event->pos();

    auto lookAt = dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get());
    if (!lookAt) return;

    const bool ctrl  = drag_modifiers_ & Qt::ControlModifier;
    const bool shift = drag_modifiers_ & Qt::ShiftModifier;
    if (workspace_anchor_) {
        if (last_mouse_button_ == Qt::RightButton)
            workspace_camera_.dolly(std::exp(delta.y() * simcore::geo::EarthNavigation::dollyPerPixel));
        else if (shift || (ctrl && last_mouse_button_ == Qt::LeftButton))
            workspace_camera_.pan(delta.x(),delta.y());
        else workspace_camera_.orbit(delta.x(),delta.y());
        applyWorkspaceCamera();
        event->accept();
        return;
    }

    // Middle drag: orbit; Shift+middle drag: pan (Blender convention).
    if (last_mouse_button_ == Qt::MiddleButton) {
        if (shift) {
            panLookAt(lookAt, delta.x(), delta.y(), bool(camera_override_));
        } else {
            orbitLookAt(lookAt, double(delta.x()) * 0.008,
                                double(delta.y()) * 0.008, bool(camera_override_));
        }
    }
    // Left drag: orbit; Ctrl+left (或 Shift+left) drag: pan. 仅在无宿主工具
    // (笔刷/拾取/摆放) 占用左键时生效 —— 那些工具已在上方分支被消耗。
    else if (last_mouse_button_ == Qt::LeftButton) {
        if (ctrl || shift) {
            panLookAt(lookAt, delta.x(), delta.y(), bool(camera_override_));
        } else {
            orbitLookAt(lookAt, double(delta.x()) * 0.008,
                                double(delta.y()) * 0.008, bool(camera_override_));
        }
    }
    // Right drag: vertical = zoom (kept for convenience).
    else if (last_mouse_button_ == Qt::RightButton) {
        zoomLookAt(lookAt, 1.0 + delta.y() * 0.0035, projectionFar(camera_.get()));
    }

    event->accept();
}

void VsgQtWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-click resets the camera (same as resetCamera())
    resetCamera();
    event->accept();
}

void VsgQtWidget::wheelEvent(QWheelEvent* event) {
    if (!camera_) return;

    auto lookAt = dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get());
    if (!lookAt) return;

    // Blender-style wheel zoom: exponential dolly, 15° per notch.
    const double steps = double(event->angleDelta().y()) / 120.0;
    if (workspace_anchor_) {
        workspace_camera_.dolly(std::pow(simcore::geo::EarthNavigation::zoomPerNotch,steps));
        applyWorkspaceCamera();
        event->accept();
        return;
    }
    zoomLookAt(lookAt, std::pow(0.82, steps), projectionFar(camera_.get()));

    event->accept();
}

void VsgQtWidget::keyPressEvent(QKeyEvent* event) {
    if (workspace_anchor_) {
        switch (event->key()) {
            case Qt::Key_Left: workspace_camera_.orbit(-12,0); break;
            case Qt::Key_Right: workspace_camera_.orbit(12,0); break;
            case Qt::Key_Up: workspace_camera_.orbit(0,12); break;
            case Qt::Key_Down: workspace_camera_.orbit(0,-12); break;
            case Qt::Key_Plus: case Qt::Key_Equal: workspace_camera_.dolly(simcore::geo::EarthNavigation::zoomPerNotch); break;
            case Qt::Key_Minus: workspace_camera_.dolly(1. / simcore::geo::EarthNavigation::zoomPerNotch); break;
            default: QWidget::keyPressEvent(event); return;
        }
        applyWorkspaceCamera();
        event->accept();
        return;
    }
    if(orbital_navigation_ && camera_ && event->modifiers()==Qt::NoModifier) {
        if(event->key()==Qt::Key_End) {resetCamera();event->accept();return;}
        if(event->key()==Qt::Key_Home) {
            if(auto look=dynamic_cast<vsg::LookAt*>(camera_->viewMatrix.get())) {
                const auto geo=simcore::geo::ecefToGeodetic(look->center.x,look->center.y,look->center.z);
                simcore::geo::GeoAnchor focus{geo.x,geo.y,geo.z,0.};
                const auto pose=simcore::geo::initialCamera(focus,15000000.);
                look->eye=pose.eye;look->center=pose.center;look->up=pose.up;
                event->accept();return;
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void VsgQtWidget::keyReleaseEvent(QKeyEvent* event) {
    QWidget::keyReleaseEvent(event);
}

}} // namespace vsgraysim::gui
