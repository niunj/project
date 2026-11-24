/// @file ViewportContext.cpp
#include "ViewportContext.h"

#include "IViewportFeature.h"
#include "VsgQtWidget.h"

#include <vsg/app/Camera.h>
#include <vsg/app/Viewer.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace vsgraysim { namespace viewport3d {

void ViewportContext::removeFromWorld(vsg::ref_ptr<vsg::Node> node)
{
    if (!node) return;
    auto erase_from = [&node](vsg::Group* g) {
        if (!g) return;
        auto& ch = g->children;
        ch.erase(std::remove(ch.begin(), ch.end(), node), ch.end());
    };
    erase_from(world_root_.get());
    erase_from(content_root_.get());   // 内容根(锚点 ENU)下的特效同样要摘除
}

void ViewportContext::addToOverlay(vsg::ref_ptr<vsg::Node> node)
{
    if (widget_ && node) widget_->addToOverlay(node);
}

vsg::ref_ptr<vsg::Options> ViewportContext::options() const
{
    return widget_ ? widget_->options() : nullptr;
}

vsg::Viewer* ViewportContext::viewer() const
{
    return widget_ ? widget_->viewer() : nullptr;
}

vsg::Camera* ViewportContext::camera() const
{
    return widget_ ? widget_->camera() : nullptr;
}
vsg::Window* ViewportContext::window() const { return widget_ ? widget_->window() : nullptr; }
vsg::Group* ViewportContext::preRenderRoot() const { return widget_ ? widget_->preRenderRoot() : nullptr; }

void ViewportContext::requestCompile()
{
    if (!widget_) return;
    widget_->requestCompile();
}

void ViewportContext::resetCamera()
{
    if (widget_) widget_->resetCamera();
}

IViewportFeature* ViewportContext::findFeature(const std::string& id) const
{
    return feature_lookup_ ? feature_lookup_(id) : nullptr;
}

}} // namespace vsgraysim::viewport3d
