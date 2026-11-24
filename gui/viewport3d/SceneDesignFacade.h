#pragma once
/// @file SceneDesignFacade.h
/// @brief 场景设计门面 (L3 层): 场景设计软件的视口接口支持 + 数据回馈。
///
/// 前置: view 已 applyConfig(ViewportConfig::fromPreset("scene_designer"))。
/// vsgReady 之前的调用自动排队, 场景就绪后统一执行 (启动注入)。
#ifndef VSGRAYSIM_VIEWPORT3D_SCENEDESIGNFACADE_H
#define VSGRAYSIM_VIEWPORT3D_SCENEDESIGNFACADE_H

#include "SceneEntities.h"

#include <QObject>

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>

#include <functional>
#include <string>
#include <vector>

namespace vsgraysim {

class TerrainSystem;

namespace viewport3d {

class Viewport3DView;

class SceneDesignFacade : public QObject {
    Q_OBJECT
public:
    explicit SceneDesignFacade(Viewport3DView* view, QObject* parent = nullptr);

    // ---- 模板地形 (地形工程 .vsgterrain 的兄弟 .ter) ----
    /// 加载地形模板: 记 terrain_ref (保存场景时写入), 隐藏兜底地面。
    bool loadTerrainTemplate(const std::string& project_path);
    const std::string& terrainRef() const { return terrain_ref_; }
    void setTerrainRef(const std::string& ref) { terrain_ref_ = ref; }
    /// 重置模板地形 (场景重新装载): 移除高度场、恢复兜底地面、清 terrain_ref。
    void resetTerrain();

    /// Bind a Rocky or voxel package and retain its reference for scene handoff.
    bool loadEarthPackage(const std::string& map_json_path);
    bool loadRockyTerrainPackage(const std::string& path) { return loadEarthPackage(path); }

    // ---- 地形访问 (场景验证用) ----
    /// 当前模板地形 (未装载时为空)。
    vsgraysim::TerrainSystem* terrain() const;

    // ---- 实体 ----
    bool addEntity(const QString& id, const QString& mesh_path,
                   const double pos[3], double yaw_deg, double scale,
                   Faction faction = Faction::Neutral);
    void removeEntity(const QString& id);
    void clearEntities();
    void setEntityTransform(const QString& id, const double pos[3],
                            double yaw_deg, double scale);
    void setFaction(const QString& id, Faction faction);
    vsg::MatrixTransform* entityTransform(const QString& id) const;

    // ---- 航迹 ----
    void setWaypoints(const QString& id, const std::vector<Waypoint>& wps);

    // ---- 相机聚焦 (双击场景树联动) ----
    /// 把相机视点定位到指定实体 (保持当前视距与视角方向)。
    void focusOnEntity(const QString& id);

    // ---- 模型地形表面查询 ----
    /// 对指定实体 (模型地形, 如 taipei 城) 在 (x,z) 处垂直 raycast,
    /// 返回表面世界高度; 无命中返回 -DBL_MAX。
    double terrainSurfaceHeight(const QString& terrain_entity_id,
                                double x, double z,
                                double from_y = 50000.0, double to_y = -50000.0);

    // ---- 点击摆放 (地面 y=0 求交, 结果经 groundPickClicked 回馈) ----
    void setPlacementEnabled(bool on);
    bool placementEnabled() const { return placement_enabled_; }

    // ---- 视口内点击拾取实体 (射线条目求交, 返回命中的实体 id; 未命中返回空) ----
    /// 在非摆放模式下, 点击发 vSG 视口回调, 将拾中实体的 id 经 entityPicked 回馈。
    QString pickEntity(const vsg::dvec3& origin, const vsg::dvec3& dir);

    /// 实体模型三角形总数 (场景树统计用)。
    uint32_t triangleCount() const;
    /// 单个实体的模型三角形数 (场景 JSON face_count 字段)。
    uint32_t entityTriangleCount(const QString& id) const;

signals:
    /// 摆放模式点击地面: 世界 (x, z) 交点。
    void groundPickClicked(double x, double z);
    /// 视口内点击拾取实体结果 (空串表示未命中/取消选中)。
    void entityPicked(const QString& id);
    /// 实体变换已应用 (拖拽摆放/数值编辑后)。
    void entityTransformChanged(const QString& id);
    /// 实体增删后三角形总数变化。
    void triangleCountChanged(uint32_t count);

private:
    struct EntityRec {
        QString id;
        Faction faction = Faction::Neutral;
        double scale = 1.0;
        std::vector<Waypoint> waypoints;
        vsg::ref_ptr<vsg::MatrixTransform> mt;
    };

    /// vsgReady 前排队, 就绪后立即执行。
    void whenReady(std::function<void()> fn);
    void ensureEntityRoot();
    EntityRec* findEntity(const QString& id);
    /// 按各实体航路点全量重建航迹线 (阵营色)。
    void refreshTrajectories();

    Viewport3DView* view_ = nullptr;
    vsg::ref_ptr<vsg::Group> entities_root_;
    std::vector<EntityRec> entities_;
    std::string terrain_ref_;
    bool placement_enabled_ = false;
    std::vector<std::function<void()>> pending_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_SCENEDESIGNFACADE_H
