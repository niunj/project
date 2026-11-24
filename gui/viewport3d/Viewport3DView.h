#pragma once
/// @file Viewport3DView.h
/// @brief 共用 3D 视口控件 (L0 层): 三软件的唯一视口入口。
///
/// 封装 VsgQtWidget (Vulkan 宿主) + 帧循环 + 特性组装:
///   auto* vp = new Viewport3DView;
///   vp->applyConfig(ViewportConfig::fromPreset("scene_designer"));
/// 特性按 ViewportConfig 从 FeatureRegistry 创建并挂载; vsgReady 之前
/// 添加的特性自动排队, 场景就绪后统一挂载。
#ifndef VSGRAYSIM_VIEWPORT3D_VIEWPORT3DVIEW_H
#define VSGRAYSIM_VIEWPORT3D_VIEWPORT3DVIEW_H

#include "ViewportConfig.h"
#include "ViewportContext.h"
#include "ViewportTypes.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <memory>
#include <vector>

namespace vsg { class Group; class Options; class Viewer; class Camera; }
class QDropEvent;
class QDragEnterEvent;

namespace vsgraysim { namespace gui { class VsgQtWidget; } }

namespace vsgraysim { namespace viewport3d {

class IViewportFeature;

class Viewport3DView : public QWidget {
    Q_OBJECT
public:
    explicit Viewport3DView(QWidget* parent = nullptr);
    ~Viewport3DView() override;

    // ------------------------------------------------------------------
    // 特性组装
    // ------------------------------------------------------------------
    /// 按配置重建全部特性 (先卸载旧的)。
    void applyConfig(const ViewportConfig& cfg);
    /// 追加单个特性; vsgReady 前调用则排队待挂载。返回是否创建成功。
    bool addFeature(const std::string& id,
                    const nlohmann::json& params = nlohmann::json::object());
    void removeFeature(const std::string& id);
    void setFeatureEnabled(const std::string& id, bool on);
    void configureFeature(const std::string& id, const nlohmann::json& params);
    IViewportFeature* feature(const std::string& id) const;
    template <class T>
    T* featureAs(const std::string& id) const {
        return dynamic_cast<T*>(feature(id));
    }

    ViewportContext& context() { return ctx_; }
    gui::VsgQtWidget* widget() const { return widget_; }

    // ------------------------------------------------------------------
    // 帧驱动
    // ------------------------------------------------------------------
    /// 启用内部帧循环 (特性 onFrame + 渲染 + FPS); 默认关闭,
    /// 宿主也可沿用自有 QTimer 并调用 renderFrame()。
    void setAutoFrame(bool on, int target_fps = 60);
    bool autoFrame() const { return auto_frame_; }
    /// 驱动一帧: 分发 onFrame → 渲染 → FPS 统计。
    void renderFrame();

    /// 按需渲染 (编辑器模式): 无交互/活动超过 idle_ms 后自动停帧释放 GPU,
    /// 任何交互或 markActivity() 立即恢复。默认关闭 (仿真运行场景需连续渲染)。
    void setIdleSuspend(bool on, int idle_ms = 3000);
    /// 标记活动 (交互/外部内容更新): 帧循环休眠时唤醒重绘。
    void markActivity();

    // ------------------------------------------------------------------
    // 兼容透传 (迁移期窗口直接使用, 语义与 VsgQtWidget 一致)
    // ------------------------------------------------------------------
    bool isReady() const;
    vsg::ref_ptr<vsg::Options> options() const;
    vsg::Viewer* viewer() const;
    vsg::Camera* camera() const;
    vsg::Group* rootGroup() const;
    void resetCamera();
    /// 把相机视点聚焦到世界坐标 target (保持当前视距/视角, 视距过小时取默认)。
    void focusViewOn(const vsg::dvec3& target);
    QImage grabFrame();
    bool mouseRay(const QPoint& pos, vsg::dvec3& origin,
                  vsg::dvec3& dir) const;

signals:
    /// VSG 场景就绪 (特性已挂载, 可构建初始场景)。
    void vsgReady();
    /// 每帧分发 (渲染前)。
    void beforeFrameTick();
    void frameTick(const vsgraysim::viewport3d::FrameTick& tick);
    /// FPS 统计更新 (约 2 Hz)。
    void fpsUpdated(double fps);
    /// 外来模型资产被拖放至视口: 携带模型路径与视口内本地坐标。
    void modelDrop(const QString& model_path, const QPoint& local_pos);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void onVsgReady();
    void onTimerTick();
    void attachFeature(IViewportFeature* f);

    gui::VsgQtWidget* widget_ = nullptr;
    QTimer            frame_timer_;
    ViewportContext   ctx_;
    std::vector<std::unique_ptr<IViewportFeature>> features_;
    bool scene_ready_ = false;
    bool auto_frame_  = false;

    // 按需渲染 (空闲停帧)
    bool    idle_suspend_ = false;      ///< 启用空闲挂起 (编辑器模式)
    int     idle_ms_ = 3000;            ///< 无活动多久后停帧
    int64_t last_activity_ms_ = 0;      ///< 最近活动时刻 (epoch ms)
    bool    suspended_ = false;         ///< 当前已停帧

    // 帧计时 / FPS
    uint64_t frame_index_ = 0;
    int64_t  start_ms_ = 0;
    int64_t  last_tick_ms_ = 0;
    double last_sim_time_ = 0.0;
    struct { int64_t last_ms = 0; int frames = 0; double fps = 0; } fps_state_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_VIEWPORT3DVIEW_H
