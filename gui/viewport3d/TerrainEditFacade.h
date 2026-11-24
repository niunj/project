#pragma once
/// @file TerrainEditFacade.h
/// @brief 地形编辑门面 (L3 层): 地形编辑软件的视口接口支持与数据回馈。
///
/// 封装 viewport3d 特性组装之上的编辑工作流:
///   - 模板生成 / 高度图与工程存取 (对话框由宿主提供, 门面只管路径);
///   - 笔刷参数下发 / 撤销重做 (高度场快照栈);
///   - 分析模式与两点工具下发;
///   - 数据回馈: heightsChanged / brushStrokeStarted / analysisResult /
///     profileReady / statsReady / undoRedoChanged。
///
/// 前置: Viewport3DView 已 applyConfig (含 terrain_layer / brush_edit /
/// analysis_overlay 特性)。vsgReady 之前的生成/装载/参数调用自动排队,
/// 场景就绪后统一执行 (启动注入)。
#ifndef VSGRAYSIM_VIEWPORT3D_TERRAINEDITFACADE_H
#define VSGRAYSIM_VIEWPORT3D_TERRAINEDITFACADE_H

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <functional>
#include <vector>

namespace vsgraysim {

class TerrainSystem;

namespace viewport3d {

class Viewport3DView;
class TerrainLayerFeature;

class TerrainEditFacade : public QObject {
    Q_OBJECT
public:
    explicit TerrainEditFacade(Viewport3DView* view, QObject* parent = nullptr);

    // ---- 地形生成 / 存取 ----
    /// 按模板重生成地形 (参数来自宿主 UI); water_type < 0 保持现有水面。
    void generateTemplate(int terrain_type, uint32_t grid, double extent,
                          double height_scale, uint32_t seed, int water_type);
    /// 应用水面类型 (WaterType 0..4; 0 = 无水面)。
    void applyWater(int water_type);
    /// 设置水面高度 (m)。
    void setWaterLevel(double level);
    bool saveHeightmap(const QString& path) const;
    bool loadHeightmap(const QString& path);
    /// 保存工程 (.vsgterrain JSON + 同名 .ter); models 为宿主模型数组。
    bool saveProject(const QString& path,
                     const nlohmann::json& models = nlohmann::json::array()) const;
    /// 加载工程; models_out 回填模型数组 (宿主负责渲染)。
    bool loadProject(const QString& path,
                     nlohmann::json* models_out = nullptr);

    /// 导出精细地形地理包 (rocky 底座): 把当前编辑后的高度场写为地理配准
    /// GeoTIFF (高程+影像) 与 rocky .map.json (粗地球底图 + GDAL 精细图层)。
    /// 返回生成的 .map.json 绝对路径 (供场景设计/仿真加载); 失败返回空串。
    QString exportGeoPackage(const QString& out_dir, const QString& name);

    // ---- 笔刷 ----
    /// mode: BrushMode(0..7); strength_pct: 1..100。
    void setBrush(int mode, double radius, double strength_pct,
                  double flatten_target);

    // ---- 分析 ----
    /// mode: 0 正常 1 坡度 2 坡向 3 材质。
    void setAnalysisMode(int mode);
    /// tool: 0 无 1 通视 2 剖面 3 距离。
    void setPickTool(int tool);

    // ---- 编辑历史 ----
    bool canUndo() const { return !undo_stack_.empty(); }
    bool canRedo() const { return !redo_stack_.empty(); }
    void undo();
    void redo();

    // ---- 查询 ----
    TerrainLayerFeature* terrainLayer() const;
    TerrainSystem* terrain() const;
    /// 计算并发射高程统计 (statsReady)。
    void updateStats();
    /// vsgReady 前排队, 就绪后立即执行 (宿主启动注入用)。
    void whenReady(std::function<void()> fn);

signals:
    /// 高度场变化 (生成/笔刷/撤销/加载后)。
    void heightsChanged();
    /// 笔画开始 (宿主可提示撤销可用)。
    void brushStrokeStarted();
    /// 撤销/重做栈变化 (宿主刷新动作可用性)。
    void undoRedoChanged();
    /// 分析结果 (kind: los/profile/distance/pick_start/pick_miss, text 已格式化)。
    void analysisResult(const QString& kind, const QString& text);
    /// 剖面采样序列 (沿程距离 + 高程 + 水面高度)。
    void profileReady(const std::vector<double>& dists,
                      const std::vector<double>& heights, double water_level);
    /// 高程统计 (最低/最高/平均)。
    void statsReady(double min_h, double max_h, double mean_h);

private:
    void pushUndoSnapshot();
    /// 特性分析 JSON → 格式化文本信号。
    void onAnalysisJson(const nlohmann::json& j);

    Viewport3DView* view_ = nullptr;
    std::vector<std::function<void()>> pending_;
    std::vector<std::vector<double>> undo_stack_;
    std::vector<std::vector<double>> redo_stack_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_TERRAINEDITFACADE_H
