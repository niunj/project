/// @file TerrainEditFacade.cpp
/// @brief 地形编辑门面实现 (见 TerrainEditFacade.h)。
#include "TerrainEditFacade.h"

#include "AnalysisOverlayFeature.h"
#include "BrushEditFeature.h"
#include "TerrainLayerFeature.h"
#include "Viewport3DView.h"
#include "EarthView.h"
#include "GeoTerrainExporter.h"

#include "TerrainAnalyzer.h"
#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <QFileInfo>

#include <spdlog/spdlog.h>

#include <fstream>

namespace vsgraysim { namespace viewport3d {

namespace {
constexpr size_t kMaxUndo = 24;   ///< 撤销快照栈深度上限
}

TerrainEditFacade::TerrainEditFacade(Viewport3DView* view, QObject* parent)
    : QObject(parent), view_(view)
{
    if (!view_) return;

    // 笔画开始 → 压撤销快照。
    if (auto* brush = view_->featureAs<BrushEditFeature>(BrushEditFeature::kId)) {
        brush->setStrokeStartedCallback([this] {
            pushUndoSnapshot();
            emit brushStrokeStarted();
        });
    }
    // 分析结果 / 剖面序列 → Qt 信号。
    if (auto* an = view_->featureAs<AnalysisOverlayFeature>(
            AnalysisOverlayFeature::kId)) {
        an->setResultCallback([this](const nlohmann::json& j) {
            onAnalysisJson(j);
        });
        an->setProfileCallback(
            [this](const std::vector<double>& dists,
                   const std::vector<double>& heights, double water) {
                emit profileReady(dists, heights, water);
            });
    }
    // 启动注入: 就绪后冲刷排队操作。
    connect(view_, &Viewport3DView::vsgReady, this, [this] {
        auto pending = std::move(pending_);
        pending_.clear();
        for (auto& fn : pending) fn();
    });
}

void TerrainEditFacade::whenReady(std::function<void()> fn)
{
    if (view_ && view_->isReady()) fn();
    else pending_.push_back(std::move(fn));
}

// ---------------------------------------------------------------------------
// 特性访问
// ---------------------------------------------------------------------------
TerrainLayerFeature* TerrainEditFacade::terrainLayer() const
{
    return view_ ? view_->featureAs<TerrainLayerFeature>(TerrainLayerFeature::kId)
                 : nullptr;
}

TerrainSystem* TerrainEditFacade::terrain() const
{
    auto* layer = terrainLayer();
    return layer ? layer->terrain() : nullptr;
}

QString TerrainEditFacade::exportGeoPackage(const QString& outDir, const QString& name)
{
    auto* t = terrain();
    if (!t) { spdlog::warn("exportGeoPackage: no editable terrain present"); return {}; }

    GeoTerrainExportParams p;
    p.heights  = &t->heightsData();
    p.n        = t->grid();
    p.extent_m = t->extent();
    // 锚点取自 rocky 地球特性 (精细地形 footprint 中心在地球上的位置)。
    if (view_) {
        if (auto* rocky = view_->featureAs<EarthView>(EarthView::kId))
            p.anchor = rocky->anchor();
    }
    p.out_dir = outDir.toStdString();
    p.name    = name.isEmpty() ? std::string("detail_terrain") : name.toStdString();

    GeoTerrainExportResult r = exportGeoTerrainPackage(p);
    if (!r.ok) {
        spdlog::error("exportGeoPackage: {}", r.error);
        return {};
    }
    spdlog::info("exportGeoPackage: rocky terrain package -> {}", r.map_json_path);
    return QString::fromStdString(r.map_json_path);
}

// ---------------------------------------------------------------------------
// 地形生成 / 存取
// ---------------------------------------------------------------------------
void TerrainEditFacade::generateTemplate(int terrain_type, uint32_t grid,
                                         double extent, double height_scale,
                                         uint32_t seed, int water_type)
{
    whenReady([=] {
        auto* layer = terrainLayer();
        auto* t = terrain();
        if (!layer || !t) return;

        pushUndoSnapshot();
        t->setGrid(grid);
        t->setExtent(extent);
        t->setHeightScale(height_scale);
        t->setSeed(seed);
        t->generate(TerrainType(terrain_type));
        if (water_type >= 0) t->applyWater(WaterType(water_type));
        // 复合生态模板 (沼泽/海洋) 自带水体, 其余沿用现有水面类型。

        layer->recenter();
        layer->notifyHeightsEdited();
        emit heightsChanged();
        emit undoRedoChanged();
        updateStats();
    });
}

void TerrainEditFacade::applyWater(int water_type)
{
    whenReady([=] {
        auto* layer = terrainLayer();
        auto* t = terrain();
        if (!layer || !t) return;
        pushUndoSnapshot();
        t->applyWater(WaterType(water_type));
        layer->notifyHeightsEdited();
        emit heightsChanged();
        emit undoRedoChanged();
    });
}

void TerrainEditFacade::setWaterLevel(double level)
{
    whenReady([=] {
        auto* layer = terrainLayer();
        auto* t = terrain();
        if (!layer || !t) return;
        t->setWaterLevel(level);
        layer->notifyHeightsEdited();
        emit heightsChanged();
    });
}

bool TerrainEditFacade::saveHeightmap(const QString& path) const
{
    auto* t = terrain();
    return t && t->saveHeightmap(path.toStdString());
}

bool TerrainEditFacade::loadHeightmap(const QString& path)
{
    if (view_ && !view_->isReady()) {
        whenReady([=] { loadHeightmap(path); });
        return true;   // 排队待执行 (启动注入)
    }
    auto* layer = terrainLayer();
    auto* t = terrain();
    if (!layer || !t) return false;

    pushUndoSnapshot();
    if (!t->loadHeightmap(path.toStdString())) return false;
    // 加载不同尺寸高度图后更新居中偏移。
    layer->recenter();
    layer->notifyHeightsEdited();
    emit heightsChanged();
    emit undoRedoChanged();
    updateStats();
    return true;
}

bool TerrainEditFacade::saveProject(const QString& path,
                                    const nlohmann::json& models) const
{
    auto* t = terrain();
    if (!t) return false;

    const QFileInfo fi(path);
    const QString ter_name = fi.completeBaseName() + ".ter";
    const QString ter_path = fi.absolutePath() + "/" + ter_name;
    if (!t->saveHeightmap(ter_path.toStdString())) {
        spdlog::warn("TerrainEditFacade: heightmap write failed '{}'",
                     ter_path.toStdString());
        return false;
    }

    nlohmann::json j;
    j["format"] = "vsgterrain-project";
    j["version"] = 1;
    j["name"] = fi.completeBaseName().toStdString();
    j["heightmap"] = ter_name.toStdString();
    j["terrain_type"] = int(t->terrainType());
    j["water_type"] = int(t->waterType());
    j["water_level"] = t->waterLevel();
    j["grid"] = t->grid();
    j["extent"] = t->extent();
    j["height_scale"] = t->heightScale();
    j["seed"] = t->seed();
    j["models"] = models.is_array() ? models : nlohmann::json::array();

    std::ofstream out(path.toStdString());
    if (!out) {
        spdlog::warn("TerrainEditFacade: project write failed '{}'",
                     path.toStdString());
        return false;
    }
    out << j.dump(2) << std::endl;
    spdlog::info("TerrainEditFacade: project saved '{}' (+ '{}')",
                 path.toStdString(), ter_path.toStdString());
    return true;
}

bool TerrainEditFacade::loadProject(const QString& path,
                                    nlohmann::json* models_out)
{
    if (view_ && !view_->isReady()) {
        whenReady([=] { loadProject(path, models_out); });
        return true;   // 排队待执行 (启动注入)
    }
    std::ifstream in(path.toStdString());
    if (!in) return false;
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        spdlog::warn("TerrainEditFacade: project parse failed: {}", e.what());
        return false;
    }
    if (j.value("format", std::string()) != "vsgterrain-project") return false;

    const QFileInfo fi(path);
    QString ter_path = fi.absolutePath() + "/" +
                       QString::fromStdString(j.value("heightmap", std::string()));
    if (!QFileInfo::exists(ter_path))
        ter_path = QString::fromStdString(j.value("heightmap", std::string()));

    auto* layer = terrainLayer();
    auto* t = terrain();
    if (!layer || !t) return false;

    pushUndoSnapshot();
    if (!t->loadHeightmap(ter_path.toStdString())) {
        spdlog::warn("TerrainEditFacade: heightmap load failed '{}'",
                     ter_path.toStdString());
        return false;
    }
    layer->recenter();
    layer->notifyHeightsEdited();

    if (models_out) *models_out = j.value("models", nlohmann::json::array());
    emit heightsChanged();
    emit undoRedoChanged();
    updateStats();
    spdlog::info("TerrainEditFacade: project loaded '{}'", path.toStdString());
    return true;
}

// ---------------------------------------------------------------------------
// 笔刷 / 分析参数下发
// ---------------------------------------------------------------------------
void TerrainEditFacade::setBrush(int mode, double radius, double strength_pct,
                                 double flatten_target)
{
    whenReady([=] {
        if (!view_) return;
        view_->configureFeature(BrushEditFeature::kId,
                                {{"mode", mode},
                                 {"radius", radius},
                                 {"strength_pct", strength_pct},
                                 {"flatten_target", flatten_target}});
    });
}

void TerrainEditFacade::setAnalysisMode(int mode)
{
    whenReady([=] {
        if (view_)
            view_->configureFeature(AnalysisOverlayFeature::kId,
                                    {{"mode", mode}});
    });
}

void TerrainEditFacade::setPickTool(int tool)
{
    whenReady([=] {
        if (view_)
            view_->configureFeature(AnalysisOverlayFeature::kId,
                                    {{"pick_tool", tool}});
    });
}

// ---------------------------------------------------------------------------
// 编辑历史 (高度场快照)
// ---------------------------------------------------------------------------
void TerrainEditFacade::pushUndoSnapshot()
{
    auto* t = terrain();
    if (!t) return;
    undo_stack_.push_back(t->heightsData());
    if (undo_stack_.size() > kMaxUndo)
        undo_stack_.erase(undo_stack_.begin());
    redo_stack_.clear();
    emit undoRedoChanged();
}

void TerrainEditFacade::undo()
{
    auto* layer = terrainLayer();
    auto* t = terrain();
    if (!layer || !t || undo_stack_.empty()) return;
    redo_stack_.push_back(t->heightsData());
    t->setHeightsData(undo_stack_.back());
    undo_stack_.pop_back();
    layer->notifyHeightsEdited();
    emit heightsChanged();
    emit undoRedoChanged();
    updateStats();
}

void TerrainEditFacade::redo()
{
    auto* layer = terrainLayer();
    auto* t = terrain();
    if (!layer || !t || redo_stack_.empty()) return;
    undo_stack_.push_back(t->heightsData());
    t->setHeightsData(redo_stack_.back());
    redo_stack_.pop_back();
    layer->notifyHeightsEdited();
    emit heightsChanged();
    emit undoRedoChanged();
    updateStats();
}

// ---------------------------------------------------------------------------
// 统计 / 分析结果格式化
// ---------------------------------------------------------------------------
void TerrainEditFacade::updateStats()
{
    auto* t = terrain();
    if (!t) return;
    const auto st = gui::TerrainAnalyzer::elevationStats(t->heightsData(),
                                                         t->grid());
    emit statsReady(st.min_h, st.max_h, st.mean_h);
}

void TerrainEditFacade::onAnalysisJson(const nlohmann::json& j)
{
    const std::string kind = j.value("kind", std::string());

    if (kind == "pick_start") {
        emit analysisResult(
            QStringLiteral("pick_start"),
            tr("起点: (%1, %2) 高 %3 m — 请点击终点")
                .arg(j.value("x", 0.0), 0, 'f', 1)
                .arg(j.value("z", 0.0), 0, 'f', 1)
                .arg(j.value("h", 0.0), 0, 'f', 1));
    } else if (kind == "pick_miss") {
        emit analysisResult(QStringLiteral("pick_miss"),
                            tr("未拾取到地形表面, 请点击地形"));
    } else if (kind == "los") {
        if (j.value("visible", false)) {
            emit analysisResult(
                QStringLiteral("los"),
                tr("✓ 通视: 两点相互可见 (视高 %1 m)")
                    .arg(j.value("eye_lift", 1.7), 0, 'f', 1));
        } else {
            emit analysisResult(
                QStringLiteral("los"),
                tr("✗ 不通视: 遮挡点位于距起点 %1% 处\n"
                   "  遮挡点: (%2, %3) 高 %4 m")
                    .arg(j.value("blocker_t", 0.0) * 100.0, 0, 'f', 1)
                    .arg(j.value("blocker_x", 0.0), 0, 'f', 1)
                    .arg(j.value("blocker_z", 0.0), 0, 'f', 1)
                    .arg(j.value("blocker_h", 0.0), 0, 'f', 1));
        }
    } else if (kind == "profile") {
        emit analysisResult(QStringLiteral("profile"),
                            tr("剖面: 长度 %1 m | 高差 %2 m")
                                .arg(j.value("length", 0.0), 0, 'f', 1)
                                .arg(j.value("dh", 0.0), 0, 'f', 1));
    } else if (kind == "distance") {
        emit analysisResult(QStringLiteral("distance"),
                            tr("距离测量:\n水平 %1 m | 高差 %2 m | 空间直线 %3 m")
                                .arg(j.value("horiz", 0.0), 0, 'f', 1)
                                .arg(j.value("dy", 0.0), 0, 'f', 1)
                                .arg(j.value("direct", 0.0), 0, 'f', 1));
    }
}

}} // namespace vsgraysim::viewport3d
