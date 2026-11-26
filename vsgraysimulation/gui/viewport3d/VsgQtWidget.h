#pragma once
/// @file VsgQtWidget.h
/// @brief QWidget that hosts a VSG (VulkanSceneGraph) render surface.
///
/// Embedding strategy (revised: QVulkanInstance + external VkSurfaceKHR
/// injection into vsg::Surface)
/// ---------------------------------------------------------------------
/// 1.  A `QVulkanInstance` is created and assigned to a child `QWindow`
///     whose surface type is set to `QSurface::VulkanSurface`.  This
///     forces Qt to realise the underlying native window with Vulkan
///     rendering in mind.
/// 2.  Once the QWindow is exposed (`showEvent`), the underlying
///     `xcb_window_t` and `xcb_connection_t*` are extracted from Qt's
///     platform-native interface.
/// 3.  A `vsg::Instance` is built *by us* (not by VSG's Xcb_Window)
///     with VK_KHR_surface + VK_KHR_xcb_surface extensions.  Using
///     `vkGetInstanceProcAddr` we resolve `vkCreateXcbSurfaceKHR` and
///     call it directly with our vsg::Instance's VkInstance + Qt's
///     xcb_window + xcb_connection, producing a `VkSurfaceKHR`.
/// 4.  The VkSurfaceKHR is wrapped in a `vsg::Surface(VkSurfaceKHR,
///     Instance*)` and injected into the VSG Window via
///     `setInstance()` + `setSurface()` *before* any code path that
///     would trigger `_initSurface()` (e.g. `CommandGraph::create()`
///     which lazily calls `getOrCreateDevice()` -> `_initDevice()` ->
///     `_initPhysicalDevice()` -> `_initSurface()`).  Because
///     `_surface` is already set, `_initSurface()` is skipped,
///     bypassing the Lavapipe software-Vulkan crash path entirely.
/// 5.  `Window::create(traits)` is still called to construct the
///     Xcb_Window (so it can manage the xcb window/event hookups),
///     but with `decoration=false` and `nativeWindow=Qt's xcb_window`
///     so VSG does not create its own top-level window.
/// 6.  Immediately after `Window::create` returns we call
///     `releaseWindow()` / `releaseConnection()` so that VSG does NOT
///     destroy the Qt-owned xcb window when the viewer is closed.
///
/// The QVulkanInstance itself is *not* shared with VSG (VSG creates its
/// own `vsg::Instance` / VkInstance).  Qt's QVulkanInstance is only used
/// to make the QWindow a proper Vulkan-surface native window.

// IMPORTANT: include Vulkan *before* any Qt header that pulls in
// qvulkaninstance.h, because Qt's header defines VK_NO_PROTOTYPES which
// suppresses the Vulkan function prototypes and breaks VSG's headers
// (which use vkGetInstanceProcAddr / vkGetDeviceProcAddr directly).
#include <vulkan/vulkan.h>

#include <QWidget>
#include <QPoint>
#include <QImage>
#include <QElapsedTimer>
#include <functional>

#include <vsg/core/ref_ptr.h>
#include <vsg/app/Window.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Camera.h>
#include <vsg/app/View.h>
#include <vsg/nodes/Group.h>
#include <vsg/maths/vec3.h>

namespace vsg { class Node; class Options; }
namespace rockyenv { struct SwapchainReadback; }
namespace vsgImGui { class RenderImGui; }

namespace vsgraysim { namespace gui {

class VsgQtWidget : public QWidget {
    Q_OBJECT
public:
    explicit VsgQtWidget(QWidget* parent = nullptr);
    ~VsgQtWidget() override;

    /// Set the scene root rendered into this widget.
    void setScene(vsg::ref_ptr<vsg::Node> root);

    /// Replace the underlying root group (used when adding many models
    /// incrementally without rebuilding the whole scene).
    vsg::Group* rootGroup() const { return root_; }
    /// Compute/transfer commands outside all render passes.
    vsg::Group* preRenderRoot() const { return pre_render_; }
    bool supportsClusteredDraws() const { return supports_clustered_draws_; }

    /// Add a node to the screen-space overlay group (above the 3D scene).
    void addToOverlay(vsg::ref_ptr<vsg::Node> node);

    /// In-viewport ImGui overlay (vsgImGui, rendered after the 3D scene).
    /// Registers a per-frame draw callback that calls Dear ImGui functions.
    /// Safe to call before the widget is ready (no-op then).
    void addImGuiDraw(const std::function<bool()>& draw);
    bool imGuiReady() const { return imgui_ != nullptr; }
    // Qt Earth panels do not need a global ImGui backend in every viewport.
    void setImGuiEnabled(bool on) { if(!vsg_inited_)imgui_enabled_=on; }

    /// VSG options (for vsgXchange readers / search paths).
    vsg::ref_ptr<vsg::Options> options() const { return options_; }

    /// Queue a viewer compile on the next render tick instead of compiling
    /// immediately on the GUI thread. This avoids blocking the UI while scene
    /// updates are being prepared.
    void requestCompile();

    /// Render one VSG frame.  Returns false when the viewer
    /// is no longer active (e.g. lost device).
    bool frame();

    /// Capture the resolved Vulkan color image before presentation.
    /// Returns a null image when the widget is not ready/visible.
    QImage grabFrame();

    /// Reset the camera to a default observer pose.
    void resetCamera();
    void setOrbitalNavigation(bool enabled) { orbital_navigation_=enabled; }

    /// 相机姿态覆盖回调 (由 rocky_map 等地球特性安装)。
    /// resetCamera() 开头调用; 返回 true 表示已自定义相机(跳过自动包围盒适配),
    /// 返回 false 则走默认局部场景自适应逻辑。
    using CameraOverride = std::function<bool(vsg::Camera*)>;
    void setCameraOverride(CameraOverride cb) { camera_override_ = std::move(cb); }
    /// A display feature may need close geometry (e.g. a pilot's instrument
    /// panel) while retaining the globe's automatic far clip. Zero restores
    /// automatic near clipping. This affects projection only.
    void setNearClipOverride(double distance) { near_clip_override_ = distance; }
    /// 调试: 打印场景图结构摘要 (VSGRAYSIM_DUMP_GRAPH=<帧号> 时 frame() 自动触发)。
    void debugDumpGraph();

    /// Accessors for downstream objects that need to push updates.
    vsg::Window*  window()  const { return window_;  }
    vsg::Viewer*  viewer()  const { return viewer_;  }
    vsg::Camera*  camera()  const { return camera_;  }
    vsg::View*    view()    const { return view_;    }

    // ------------------------------------------------------------------
    // Terrain brush editing: when enabled, left-drag is redirected from
    // camera navigation to `edit_ray_cb_`(world ray origin, direction).
    // The host application performs its own height-field picking.
    // ------------------------------------------------------------------
    using EditRayCallback = std::function<void(const vsg::dvec3& origin,
                                               const vsg::dvec3& dir)>;
    /// World-space ray under the mouse cursor (unprojected through the
    /// camera).  Returns false if no camera/valid viewport yet.
    bool mouseRay(const QPoint& pos, vsg::dvec3& origin, vsg::dvec3& dir) const;
    void setEditRayCallback(EditRayCallback cb) { edit_ray_cb_ = std::move(cb); }
    void setBrushDragActive(bool on) { brush_drag_active_ = on; }
    bool brushDragActive() const { return brush_drag_active_; }

    // ------------------------------------------------------------------
    // Terrain analysis pick mode: when enabled, left-clicks (press only,
    // no drag semantics) are redirected to `pick_ray_cb_` as a world ray.
    // Used by two-point analyses (line-of-sight / profile / distance).
    // Pick mode takes precedence over brush drag on the left button.
    // ------------------------------------------------------------------
    using PickRayCallback = std::function<void(const vsg::dvec3& origin,
                                               const vsg::dvec3& dir)>;
    void setPickRayCallback(PickRayCallback cb) { pick_ray_cb_ = std::move(cb); }
    void setPickModeActive(bool on) { pick_mode_active_ = on; }
    bool pickModeActive() const { return pick_mode_active_; }

    bool isReady() const { return vsg_inited_; }

signals:
    /// Emitted after the VSG viewer & window are first initialised.
    void vsgReady();
    void initializationFailed(const QString& message);
    /// Runs on the viewport thread with the final camera for this submission.
    void beforeSceneRecord();
    /// Emitted when a brush stroke begins (left-press while brush active).
    void brushDragStarted();

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    // With WA_PaintOnScreen set, Qt will not paint the widget background.
    // VSG paints directly to the native window, so paintEvent is a no-op.
    void paintEvent(QPaintEvent* event) override {}

    // Qt mouse/keyboard event handling (Blender-style camera navigation
    // implemented internally; no VSG event handlers involved).
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    bool initVsg();
    bool orbital_navigation_ = false;
    vsg::ref_ptr<rockyenv::SwapchainReadback> readback_;
    bool capture_requested_ = false;

    // Note: VSG creates its own vsg::Instance / VkInstance in initVsg();
    // we do not use QVulkanInstance for the direct native-window approach.
    vsg::ref_ptr<vsg::Window>     window_;
    vsg::ref_ptr<vsg::Viewer>     viewer_;
    vsg::ref_ptr<vsg::Camera>    camera_;
    vsg::ref_ptr<vsg::View>      view_;
    vsg::ref_ptr<vsg::Group>     root_;
    vsg::ref_ptr<vsg::Group>     overlay_;
    vsg::ref_ptr<vsg::Group>     pre_render_;
    bool supports_clustered_draws_=false;
    vsg::ref_ptr<vsg::Options>   options_;
    vsgImGui::RenderImGui* imgui_=nullptr; // Owned by the render graph overlay.
    bool imgui_enabled_=true;

    bool                          vsg_inited_ = false;
    bool                          init_attempted_ = false;
    bool                          viewer_compiled_ = false;
    bool                          compile_requested_ = false;
    int64_t                       last_present_ms_ = 0;
    QElapsedTimer                 render_clock_;

    // Mouse interaction state (Blender-style navigation):
    //   left-drag          orbit around the look-at center (default view)
    //   shift+left-drag    pan the view
    //   middle-drag        orbit (kept), shift+middle-drag pan
    //   right-drag / wheel dolly zoom toward/away from the center
    //   left (when a host tool such as brush/pick/placement is active)
    //                     consumed by that tool instead of orbiting
    QPoint                        last_mouse_pos_;
    Qt::MouseButton               last_mouse_button_ = Qt::NoButton;
    Qt::KeyboardModifiers         drag_modifiers_    = Qt::NoModifier;
    bool                          brush_drag_active_ = false;
    EditRayCallback               edit_ray_cb_;
    bool                          pick_mode_active_ = false;
    PickRayCallback               pick_ray_cb_;

    CameraOverride                camera_override_;   ///< 地球相机覆盖 (rocky_map)
    double near_clip_override_ = 0.0;
};

}} // namespace vsgraysim::gui
