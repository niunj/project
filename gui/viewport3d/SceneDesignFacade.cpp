/// @file SceneDesignFacade.cpp
/// @brief 场景设计门面实现 (见 SceneDesignFacade.h)。
#include "SceneDesignFacade.h"

#include "TerrainLayerFeature.h"
#include "TrajectoryFeature.h"
#include "Viewport3DView.h"
#include "ViewportKit.h"
#include "VsgQtWidget.h"
#include "EarthView.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <fstream>
#include <filesystem>
#include <vsg/core/Visitor.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/MatrixTransform.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

namespace vsgraysim { namespace viewport3d {

namespace {

/// 统计 VSG 场景图中的三角形总数 (用于生成 face_count)。
struct TriangleCounter : public vsg::Visitor {
    uint64_t count = 0;
    void apply(vsg::Node& node) override { node.traverse(*this); }
    void apply(vsg::Geometry& g) override { g.traverse(*this); }
    void apply(vsg::VertexIndexDraw& vid) override {
        if (vid.indices && vid.indices->data)
            count += vid.indices->data->dataSize() / sizeof(uint32_t) / 3;
        vid.traverse(*this);
    }
    using vsg::Visitor::apply;
};

} // anonymous namespace

SceneDesignFacade::SceneDesignFacade(Viewport3DView* view, QObject* parent)
    : QObject(parent), view_(view)
{
    if (!view_) return;
    // 就绪后: 冲刷排队操作 + 安装点击摆放射线回调。
    connect(view_, &Viewport3DView::vsgReady, this, [this] {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& fn : pending) fn();
        if (!view_ || !view_->widget()) return;
        view_->widget()->setEditRayCallback(
            [this](const vsg::dvec3& origin, const vsg::dvec3& dir) {
                if (placement_enabled_) {
                    // 地面平面 y = 0 求交。
                    if (std::abs(dir.y) < 1e-9) return;
                    const double t = -origin.y / dir.y;
                    if (t <= 0.0) return;
                    emit groundPickClicked(origin.x + t * dir.x,
                                           origin.z + t * dir.z);
                    return;
                }
                // 非摆放模式: 点击视口拾取实体 (含空串 → 取消选中)。
                emit entityPicked(pickEntity(origin, dir));
            });
    });
}

void SceneDesignFacade::whenReady(std::function<void()> fn)
{
    if (view_ && view_->isReady()) fn();
    else pending_.push_back(std::move(fn));
}

void SceneDesignFacade::ensureEntityRoot()
{
    if (entities_root_ || !view_) return;
    entities_root_ = vsg::Group::create();
    // rocky 地球底座激活时挂到锚点 ENU 内容根 (实体落到地球表面)。
    if (auto root = view_->context().contentRoot()) root->addChild(entities_root_);
}

void SceneDesignFacade::focusOnEntity(const QString& id)
{
    whenReady([this, id] {
        EntityRec* rec = findEntity(id);
        if (!rec || !rec->mt || !view_) return;
        const vsg::dvec4 o = rec->mt->matrix * vsg::dvec4(0, 0, 0, 1);
        if (std::abs(o.w) < 1e-12) return;
        view_->focusViewOn(vsg::dvec3(o.x / o.w, o.y / o.w, o.z / o.w));
    });
}

double SceneDesignFacade::terrainSurfaceHeight(const QString& terrain_entity_id,
                                                double x, double z,
                                                double from_y, double to_y)
{
    EntityRec* rec = findEntity(terrain_entity_id);
    if (!rec || !rec->mt) return -std::numeric_limits<double>::max();
    // 垂直线段求交: 只对地形实体子树, 不含其他实体。
    auto lsi = vsg::LineSegmentIntersector::create(
        vsg::dvec3(x, from_y, z), vsg::dvec3(x, to_y, z));
    try { rec->mt->accept(*lsi); } catch (...) { return -std::numeric_limits<double>::max(); }
    if (lsi->intersections.empty()) return -std::numeric_limits<double>::max();
    // 取最高命中 (从上往下第一条即最近, intersections 按距离升序)。
    return lsi->intersections.front()->worldIntersection.y;
}

QString SceneDesignFacade::pickEntity(const vsg::dvec3& origin,
                                      const vsg::dvec3& dir)
{
    QString best;
    double best_t = std::numeric_limits<double>::max();
    for (auto& e : entities_) {
        if (!e.mt || e.mt->children.empty()) continue;
        // 世界包围盒 (ComputeBounds 已累积全部 Transform)。
        vsg::ComputeBounds cb;
        e.mt->accept(cb);
        const vsg::dvec3 c   = (cb.bounds.min + cb.bounds.max) * 0.5;
        const vsg::dvec3 ext = (cb.bounds.max - cb.bounds.min) * 0.5;
        const double rw = std::max(vsg::length(ext), 0.5);
        // 射线-球求交, 取最近命中。
        const vsg::dvec3 oc = origin - c;
        const double b  = vsg::dot(oc, dir);
        const double cc = vsg::dot(oc, oc) - rw * rw;
        const double disc = b * b - cc;
        if (disc < 0.0) continue;
        const double t = -b - std::sqrt(disc);
        if (t > 0.0 && t < best_t) {
            best_t = t;
            best = e.id;
        }
    }
    return best;
}

SceneDesignFacade::EntityRec* SceneDesignFacade::findEntity(const QString& id)
{
    for (auto& e : entities_)
        if (e.id == id) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// 模板地形
// ---------------------------------------------------------------------------
bool SceneDesignFacade::loadTerrainTemplate(const std::string& project_path)
{
    if (project_path.empty()) return false;
    if (!view_ || !view_->isReady()) {
        // 启动注入: 排队待场景就绪。
        pending_.push_back([this, project_path] { loadTerrainTemplate(project_path); });
        return true;
    }
    namespace fs = std::filesystem;

    if (view_->context().geoActive()) {
        if(project_path.size()>=9 && project_path.substr(project_path.size()-9)==".map.json")
            return loadEarthPackage(project_path);
        auto* globe=view_->featureAs<EarthView>(EarthView::kId);
        const auto anchor=globe->anchor();
        nlohmann::json scenario={{"terrain_ref",project_path},{"geo_anchor",{{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",anchor.alt_m},{"heading_deg",anchor.heading_deg}}}};
        std::string error;
        if(!globe->loadEarthScenario(scenario,fs::absolute(project_path).string(),error)) {
            spdlog::error("Terrain handoff rejected: {}",error);return false;
        }
    }

    fs::path ter(project_path);
    ter.replace_extension(".ter");   // 地形工程 JSON 的兄弟 .ter 高度图
    if (!fs::exists(ter)) {
        spdlog::warn("SceneDesignFacade: terrain heightmap '{}' not found",
                     ter.string());
        return false;
    }
    auto* layer = view_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    if (!layer || !layer->loadHeightmapFile(ter.string())) return false;

    terrain_ref_ = project_path;   // 保存场景时写入 JSON
    spdlog::info("SceneDesignFacade: terrain template loaded '{}'", project_path);
    return true;
}

void SceneDesignFacade::resetTerrain()
{
    terrain_ref_.clear();
    if (view_) {
        if(auto* globe=view_->featureAs<EarthView>(EarthView::kId)) {
            std::string error;
            if(!globe->loadEarthScenario(nlohmann::json::object(),{},error))
                spdlog::warn("Earth reset failed: {}",error);
        }
        if (auto* layer =
                view_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId))
            layer->clearTerrain();
    }
}

bool SceneDesignFacade::loadEarthPackage(const std::string& map_json_path)
{
    if (!view_) return false;
    auto* globe = view_->featureAs<EarthView>(EarthView::kId);
    if (!globe) {
        spdlog::warn("loadEarthPackage: rocky_map feature not present");
        return false;
    }
    nlohmann::json manifest;std::ifstream input(map_json_path);
    try {input>>manifest;}catch(const std::exception& error){spdlog::error("Terrain map read failed: {}",error.what());return false;}
    const auto a=globe->anchor();
    nlohmann::json scenario={{"terrain_ref",std::filesystem::absolute(map_json_path).string()},
        {"geo_anchor",manifest.value("geo_anchor",nlohmann::json{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}})}};
    std::string error;
    if (!globe->loadEarthScenario(scenario,std::filesystem::absolute(map_json_path).string(),error)) {
        spdlog::error("Earth package {}: {}", map_json_path, error);
        return false;
    }
    terrain_ref_ = map_json_path;   // 记录, 保存场景时写入以便下次自动恢复
    spdlog::info("Earth package loaded: {}", map_json_path);
    return true;
}

// ---------------------------------------------------------------------------
// 实体
// ---------------------------------------------------------------------------
bool SceneDesignFacade::addEntity(const QString& id, const QString& mesh_path,
                                  const double pos[3], double yaw_deg,
                                  double scale, Faction faction)
{
    if (!view_ || !view_->isReady()) {
        // 拷贝位置数据, 避免捕获调用方栈上数组指针。
        const double px = pos[0], py = pos[1], pz = pos[2];
        pending_.push_back([=] {
            const double q[3] = {px, py, pz};
            addEntity(id, mesh_path, q, yaw_deg, scale, faction);
        });
        return true;
    }
    ensureEntityRoot();

    const std::string resolved =
        gui::ViewportKit::resolveAssetPath(mesh_path.toStdString());
    auto node = gui::ViewportKit::loadModelNode(resolved, view_->options());
    if (!node) {
        spdlog::warn("SceneDesignFacade: model '{}' not loaded", resolved);
        return false;
    }

    // 贴地兜底: 地形已加载且实体低于地表时抬升至表面
    // (场景文件 initial_position.y 常为 0, 直接放置会陷入起伏地形)。
    double py = pos[1];
    if (auto* t = terrain()) {
        const double h = t->heightAtWorld(pos[0], pos[2]);
        if (py < h - 0.05) py = h;
    }

    auto mt = vsg::MatrixTransform::create();
    mt->matrix = gui::ViewportKit::makeEntityTransform(
        vsg::dvec3(pos[0], py, pos[2]), yaw_deg, scale);
    mt->addChild(node);
    entities_root_->addChild(mt);

    EntityRec rec;
    rec.id = id;
    rec.faction = faction;
    rec.scale = scale;
    rec.mt = mt;
    entities_.push_back(std::move(rec));

    view_->context().requestCompile();
    emit triangleCountChanged(triangleCount());
    return true;
}

void SceneDesignFacade::removeEntity(const QString& id)
{
    auto it = std::find_if(entities_.begin(), entities_.end(),
                           [&](const EntityRec& e) { return e.id == id; });
    if (it == entities_.end() || !entities_root_) return;
    auto& kids = entities_root_->children;
    kids.erase(std::remove(kids.begin(), kids.end(),
                           vsg::ref_ptr<vsg::Node>(it->mt)), kids.end());
    entities_.erase(it);
    refreshTrajectories();
    if (view_) view_->context().requestCompile();
    emit triangleCountChanged(triangleCount());
}

void SceneDesignFacade::clearEntities()
{
    if (entities_root_) entities_root_->children.clear();
    entities_.clear();
    refreshTrajectories();
    if (view_) view_->context().requestCompile();
    emit triangleCountChanged(0);
}

void SceneDesignFacade::setEntityTransform(const QString& id, const double pos[3],
                                           double yaw_deg, double scale)
{
    auto* rec = findEntity(id);
    if (!rec || !rec->mt) return;
    rec->scale = scale;
    rec->mt->matrix = gui::ViewportKit::makeEntityTransform(
        vsg::dvec3(pos[0], pos[1], pos[2]), yaw_deg, scale);
    emit entityTransformChanged(id);
}

void SceneDesignFacade::setFaction(const QString& id, Faction faction)
{
    auto* rec = findEntity(id);
    if (!rec) return;
    rec->faction = faction;
    refreshTrajectories();
    if (view_) view_->context().requestCompile();
}

vsg::MatrixTransform* SceneDesignFacade::entityTransform(const QString& id) const
{
    for (const auto& e : entities_)
        if (e.id == id) return e.mt.get();
    return nullptr;
}

// ---------------------------------------------------------------------------
// 航迹
// ---------------------------------------------------------------------------
void SceneDesignFacade::setWaypoints(const QString& id,
                                     const std::vector<Waypoint>& wps)
{
    auto* rec = findEntity(id);
    if (!rec) return;
    rec->waypoints = wps;
    refreshTrajectories();
}

void SceneDesignFacade::refreshTrajectories()
{
    if (!view_) return;
    auto* traj = view_->featureAs<TrajectoryFeature>(TrajectoryFeature::kId);
    if (!traj) return;
    std::vector<TrajectoryFeature::Track> tracks;
    for (const auto& e : entities_) {
        if (e.waypoints.size() < 2) continue;
        tracks.push_back({e.waypoints, factionColor(e.faction)});
    }
    traj->setTracks(tracks);
}

// ---------------------------------------------------------------------------
// 点击摆放 / 统计
// ---------------------------------------------------------------------------
void SceneDesignFacade::setPlacementEnabled(bool on)
{
    placement_enabled_ = on;
    // 放置模式下左键重定向到摆放射线 (关闭相机旋转), 与笔刷拖拽同通道。
    if (view_ && view_->widget()) view_->widget()->setBrushDragActive(on);
}

uint32_t SceneDesignFacade::triangleCount() const
{
    if (!entities_root_) return 0;
    TriangleCounter tc;
    entities_root_->accept(tc);
    return uint32_t(std::min<uint64_t>(tc.count, 0xFFFFFFFEu));
}

uint32_t SceneDesignFacade::entityTriangleCount(const QString& id) const
{
    for (const auto& e : entities_) {
        if (e.id != id || !e.mt || e.mt->children.empty()) continue;
        TriangleCounter tc;
        e.mt->children[0]->accept(tc);   // 首子节点 = 模型 (视锥组另挂)
        return uint32_t(std::min<uint64_t>(tc.count, 0xFFFFFFFEu));
    }
    return 0;
}

vsgraysim::TerrainSystem* SceneDesignFacade::terrain() const
{
    if (!view_) return nullptr;
    auto* layer = view_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId);
    return layer ? layer->terrain() : nullptr;
}

}} // namespace vsgraysim::viewport3d
