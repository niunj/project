/// @file Viewport3DView.cpp
#include "Viewport3DView.h"

#include "FeatureRegistry.h"
#include "IViewportFeature.h"
#include "VsgQtWidget.h"

#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QVBoxLayout>

#include <vsg/app/Camera.h>
#include <vsg/app/Viewer.h>
#include <vsg/nodes/Group.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace vsgraysim { namespace viewport3d {

Viewport3DView::Viewport3DView(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    widget_ = new gui::VsgQtWidget(this);
    lay->addWidget(widget_);

    ctx_.widget_ = widget_;
    ctx_.feature_lookup_ = [this](const std::string& id) {
        return feature(id);
    };

    connect(widget_, &gui::VsgQtWidget::vsgReady, this, &Viewport3DView::onVsgReady);
    connect(&frame_timer_, &QTimer::timeout, this, &Viewport3DView::onTimerTick);
    widget_->installEventFilter(this);   // 交互事件 → 按需渲染唤醒源
}

Viewport3DView::~Viewport3DView()
{
    frame_timer_.stop();
    // 场景仍存活时卸载特性 (摘节点/断回调)。
    for (auto& f : features_)
        if (f->attached()) f->onDetach(ctx_);
}

// ---------------------------------------------------------------------------
// Feature assembly
// ---------------------------------------------------------------------------
void Viewport3DView::applyConfig(const ViewportConfig& cfg)
{
    for (auto& f : features_)
        if (f->attached()) f->onDetach(ctx_);
    features_.clear();
    ctx_.setTerrainQuery(nullptr);   // terrain_layer 重建时重新注册

    for (const auto& e : cfg.entries()) {
        auto f = FeatureRegistry::instance().create(e.id);
        if (!f) continue;
        f->setEnabled(e.enabled);
        if (e.params.is_object() && !e.params.empty()) f->configure(e.params);
        if (scene_ready_) attachFeature(f.get());
        features_.push_back(std::move(f));
    }
    spdlog::info("Viewport3DView: config applied ({} features, ready={})",
                 features_.size(), scene_ready_);
}

bool Viewport3DView::addFeature(const std::string& id, const nlohmann::json& params)
{
    if (feature(id)) {   // 已存在 → 仅更新参数
        configureFeature(id, params);
        return true;
    }
    auto f = FeatureRegistry::instance().create(id);
    if (!f) return false;
    if (params.is_object() && !params.empty()) f->configure(params);
    if (scene_ready_) attachFeature(f.get());
    features_.push_back(std::move(f));
    return true;
}

void Viewport3DView::removeFeature(const std::string& id)
{
    auto it = std::find_if(features_.begin(), features_.end(),
                           [&](const std::unique_ptr<IViewportFeature>& f) {
                               return f->id() == id;
                           });
    if (it == features_.end()) return;
    if ((*it)->attached()) (*it)->onDetach(ctx_);
    features_.erase(it);
}

void Viewport3DView::setFeatureEnabled(const std::string& id, bool on)
{
    if (auto* f = feature(id)) f->setEnabled(on);
}

void Viewport3DView::configureFeature(const std::string& id,
                                      const nlohmann::json& params)
{
    if (auto* f = feature(id)) f->configure(params);
}

IViewportFeature* Viewport3DView::feature(const std::string& id) const
{
    for (const auto& f : features_)
        if (f->id() == id) return f.get();
    return nullptr;
}

void Viewport3DView::attachFeature(IViewportFeature* f)
{
    if (!f || f->attached()) return;
    f->onAttach(ctx_);
    f->attached_ = true;
}

void Viewport3DView::onVsgReady()
{
    ctx_.world_root_ = widget_->rootGroup();
    scene_ready_ = true;
    for (auto& f : features_)
        if (!f->attached()) attachFeature(f.get());
    emit vsgReady();
}

// ---------------------------------------------------------------------------
// Frame drive
// ---------------------------------------------------------------------------
void Viewport3DView::setAutoFrame(bool on, int target_fps)
{
    auto_frame_ = on;
    if (on) {
        frame_timer_.setTimerType(Qt::PreciseTimer);
        frame_timer_.setInterval(std::max(1, 1000 / std::max(1, target_fps)));
        frame_timer_.start();
    } else {
        frame_timer_.stop();
    }
}

void Viewport3DView::setIdleSuspend(bool on, int idle_ms)
{
    idle_suspend_ = on;
    idle_ms_ = std::max(200, idle_ms);
    last_activity_ms_ = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if (on && suspended_ && auto_frame_) {
        frame_timer_.start();
        suspended_ = false;
    }
}

void Viewport3DView::markActivity()
{
    last_activity_ms_ = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if (suspended_ && auto_frame_) {
        frame_timer_.start();
        suspended_ = false;
    }
}

bool Viewport3DView::eventFilter(QObject* obj, QEvent* ev)
{
    // 交互事件 (含子控件) 视为活动, 唤醒空闲停帧。
    switch (ev->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::DragEnter:
    case QEvent::Drop:
        markActivity();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(obj, ev);
}

void Viewport3DView::onTimerTick()
{
    renderFrame();
}

void Viewport3DView::renderFrame()
{
    const int64_t now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if (start_ms_ == 0) start_ms_ = now;
    if (last_tick_ms_ == 0) last_tick_ms_ = now;

    emit beforeFrameTick();
    FrameTick tick;
    tick.dt_sec = double(now - last_tick_ms_) * 1e-3;
    tick.elapsed_sec = double(now - start_ms_) * 1e-3;
    const double sim_time = ctx_.simulationTime();
    if (std::isfinite(sim_time)) {
        tick.simulation_clock = true;
        tick.elapsed_sec = sim_time;
        tick.dt_sec = std::max(0.0, sim_time - last_sim_time_);
        last_sim_time_ = sim_time;
    }
    tick.frame_index = ++frame_index_;
    last_tick_ms_ = now;

    for (auto& f : features_)
        if (f->attached() && f->enabled()) f->onFrame(ctx_, tick);
    emit frameTick(tick);

    if (widget_ && widget_->isReady()) widget_->frame();

    // FPS 统计 (500 ms 窗口)。
    if (fps_state_.last_ms == 0) fps_state_.last_ms = now;
    fps_state_.frames++;
    const int64_t delta = now - fps_state_.last_ms;
    if (delta >= 500) {
        fps_state_.fps = double(fps_state_.frames) * 1000.0 / double(delta);
        fps_state_.frames = 0;
        fps_state_.last_ms = now;
        emit fpsUpdated(fps_state_.fps);
    }

    // 按需渲染: 无交互/外部活动超过阈值 → 停帧释放 GPU。
    if (idle_suspend_ && !suspended_ &&
        now - last_activity_ms_ > idle_ms_) {
        frame_timer_.stop();
        suspended_ = true;
    }
}

// ---------------------------------------------------------------------------
// Pass-through (兼容迁移)
// ---------------------------------------------------------------------------
bool Viewport3DView::isReady() const
{
    return widget_ && widget_->isReady();
}

vsg::ref_ptr<vsg::Options> Viewport3DView::options() const
{
    return widget_ ? widget_->options() : nullptr;
}

vsg::Viewer* Viewport3DView::viewer() const
{
    return widget_ ? widget_->viewer() : nullptr;
}

vsg::Camera* Viewport3DView::camera() const
{
    return widget_ ? widget_->camera() : nullptr;
}

vsg::Group* Viewport3DView::rootGroup() const
{
    return widget_ ? widget_->rootGroup() : nullptr;
}

void Viewport3DView::resetCamera()
{
    if (widget_) widget_->resetCamera();
}

void Viewport3DView::focusViewOn(const vsg::dvec3& target)
{
    if (!widget_) return;
    auto* cam = widget_->camera();
    if (!cam) return;
    auto* lookAt = dynamic_cast<vsg::LookAt*>(cam->viewMatrix.get());
    if (!lookAt) return;

    // 保留当前视距; 视距过小时给一个合适的默认值。
    const vsg::dvec3 off = lookAt->eye - lookAt->center;
    double dist = vsg::length(off);
    if (dist < 50.0) dist = 90.0;
    // 保留当前视线方向 (退化时取俯视 45° 的斜上方)。
    vsg::dvec3 dir = off;
    if (vsg::length(dir) < 1e-6)
        dir = vsg::normalize(vsg::dvec3(0.45, 0.55, 0.70));
    else
        dir = vsg::normalize(dir);

    lookAt->center = target;
    lookAt->eye    = target + dir * dist;
}

QImage Viewport3DView::grabFrame()
{
    return widget_ ? widget_->grabFrame() : QImage();
}

bool Viewport3DView::mouseRay(const QPoint& pos, vsg::dvec3& origin,
                              vsg::dvec3& dir) const
{
    return widget_ && widget_->mouseRay(pos, origin, dir);
}

// ---------------------------------------------------------------------------
// 拖放 (外来模型资产 → 场景)
// ---------------------------------------------------------------------------
void Viewport3DView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData() &&
        event->mimeData()->hasFormat("application/x-vsgray-model")) {
        setAcceptDrops(true);
        event->acceptProposedAction();
    } else {
        QWidget::dragEnterEvent(event);
    }
}

void Viewport3DView::dropEvent(QDropEvent* event)
{
    if (event->mimeData() &&
        event->mimeData()->hasFormat("application/x-vsgray-model")) {
        const QString path = QString::fromUtf8(
            event->mimeData()->data("application/x-vsgray-model"));
        if (!path.isEmpty())
            emit modelDrop(path, event->position().toPoint());
        event->acceptProposedAction();
    } else {
        QWidget::dropEvent(event);
    }
}

}} // namespace vsgraysim::viewport3d
