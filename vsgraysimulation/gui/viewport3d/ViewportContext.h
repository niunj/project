#pragma once
/// @file ViewportContext.h
/// @brief 特性运行时上下文: 宿主控件 / 场景根 / 地形查询 / 特性互查。
#ifndef VSGRAYSIM_VIEWPORT3D_VIEWPORTCONTEXT_H
#define VSGRAYSIM_VIEWPORT3D_VIEWPORTCONTEXT_H

#include "ViewportTypes.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/io/Options.h>
#include <vsg/maths/mat4.h>

#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <cmath>
#include <limits>

namespace vsg { class Viewer; class Camera; class Node; class Window; }

namespace vsgraysim { namespace gui { class VsgQtWidget; } }

namespace vsgraysim { namespace viewport3d {

class IViewportFeature;

class ViewportContext {
public:
    /// Thread-safe publication from the simulation worker. NaN = editor wall clock.
    void setSimulationTime(double seconds) { simulation_time_.store(seconds); }
    double simulationTime() const { return simulation_time_.load(); }
    // ---- 宿主与场景 ----
    gui::VsgQtWidget* widget() const { return widget_; }
    /// 世界场景根组 (所有特性节点挂这里)。
    vsg::ref_ptr<vsg::Group> worldRoot() const { return world_root_; }
    /// 场景"内容根": 需要参与地理定位的实体/特效挂这里。
    /// rocky 地球底座启用时, 它指向锚点 ENU 变换节点 (局部米制 → 地球 ECEF);
    /// 未启用时回退为 worldRoot() (传统局部平面工作流)。
    vsg::ref_ptr<vsg::Group> contentRoot() const {
        return content_root_ ? content_root_ : world_root_;
    }
    /// 由 rocky_map 特性设置/清除内容根。
    void setContentRoot(vsg::ref_ptr<vsg::Group> node) { content_root_ = std::move(node); }
    /// rocky 地球底座是否激活 (内容根被重定向到地理锚点)。
    bool geoActive() const { return static_cast<bool>(content_root_); }
    /// 内容根的"局部→世界"矩阵: 地球模式下为锚点 ENU→ECEF (供自定义 shader
    /// 管线把局部米制几何定位到地球); 传统平面模式下为单位矩阵。
    vsg::dmat4 contentModel() const {
        if (auto mt = dynamic_cast<vsg::MatrixTransform*>(content_root_.get()))
            return mt->matrix;
        return vsg::dmat4(1.0);
    }
    /// 从世界场景根移除特性节点 (存在才移除)。
    void removeFromWorld(vsg::ref_ptr<vsg::Node> node);
    /// 向屏幕空间覆盖层添加节点 (宿主托管)。
    void addToOverlay(vsg::ref_ptr<vsg::Node> node);

    vsg::ref_ptr<vsg::Options> options() const;
    vsg::Viewer* viewer() const;
    vsg::Window* window() const;
    vsg::Group* preRenderRoot() const;
    vsg::Camera* camera() const;
    /// 增量编译 (新增节点后调用; 异常内部消化)。
    void requestCompile();
    /// 相机复位到默认观察位。
    void resetCamera();

    // ---- 地形查询 (terrain_layer 特性注册) ----
    void setTerrainQuery(std::shared_ptr<ITerrainQuery> q) { terrain_query_ = std::move(q); }
    ITerrainQuery* terrainQuery() const { return terrain_query_.get(); }

    // ---- 地理(地球)查询 (rocky_map 特性注册) ----
    void setGeoQuery(std::shared_ptr<IGeoTerrainQuery> q) { geo_query_ = std::move(q); }
    IGeoTerrainQuery* geoQuery() const { return geo_query_.get(); }

    // ---- 特性互查 (Viewport3DView 注入查询函数) ----
    IViewportFeature* findFeature(const std::string& id) const;
    template <class T>
    T* featureAs(const std::string& id) const {
        return dynamic_cast<T*>(findFeature(id));
    }

private:
    std::atomic<double> simulation_time_{std::numeric_limits<double>::quiet_NaN()};
    friend class Viewport3DView;
    gui::VsgQtWidget* widget_ = nullptr;
    vsg::ref_ptr<vsg::Group> world_root_;
    vsg::ref_ptr<vsg::Group> content_root_;   ///< rocky 地理锚点 ENU 根 (空=world_root_)
    std::shared_ptr<ITerrainQuery> terrain_query_;
    std::shared_ptr<IGeoTerrainQuery> geo_query_;
    std::function<IViewportFeature*(const std::string&)> feature_lookup_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_VIEWPORTCONTEXT_H
