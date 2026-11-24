#pragma once
/// @file TerrainLayerFeature.h
/// @brief 地形层特性: 持有 TerrainSystem / 居中偏移 / 高度查询注册。
#ifndef VSGRAYSIM_VIEWPORT3D_TERRAINLAYERFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_TERRAINLAYERFEATURE_H

#include "IViewportFeature.h"
#include "ViewportTypes.h"

#include <vsg/core/ref_ptr.h>

#include <memory>

namespace vsg { class MatrixTransform; }

namespace vsgraysim {

class TerrainSystem;

namespace viewport3d {

/// 地形层: 持有 TerrainSystem, 将地形居中到世界原点, 并向上下文注册
/// ITerrainQuery (供网格贴地 / 相机限高 / 笔刷 / 分析等特性消费)。
///
/// 参数 (configure, 均可选):
///   {"grid":256, "extent":1000.0, "height_scale":60.0, "seed":n,
///    "template":int(TerrainType 0..9), "water":int(WaterType 0..4),
///    "water_level":f,
///    "auto_generate":bool (默认 true; false = 不自动生成, 等高度图加载),
///    "centered":bool (默认 true; false = 不居中, 场景/仿真坐标系)}
/// 默认生成 Plains + Lake (与地形编辑器初始场景一致)。
class TerrainLayerFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "terrain_layer";
    const char* id() const override { return kId; }
    ~TerrainLayerFeature() override;   // cpp 中定义 (TerrainSystem 不完整类型)
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;

    // ---- 供 TerrainEditFacade / 其它特性直接访问 ----
    TerrainSystem* terrain() const { return terrain_.get(); }
    /// 地形局部 → 世界偏移 (半宽): world = local - offset。
    double offset() const { return offset_; }
    /// 地形居中变换节点 (分析覆盖层等特性挂在其下共享局部坐标)。
    vsg::MatrixTransform* terrainTransform() const { return terrain_mt_.get(); }
    /// 高度场被编辑 (笔刷/撤销/高度图加载等) 后调用, 递增高度版本。
    void notifyHeightsEdited() { ++edit_version_; }
    uint64_t heightsVersion() const { return edit_version_; }
    /// 按最新 extent 更新居中偏移与变换矩阵。
    void recenter();
    /// 加载 .ter 高度图 (场景设计/仿真预览的模板地形): 成功后建节点挂场景。
    bool loadHeightmapFile(const std::string& ter_path);
    /// 移除当前地形 (场景重新装载时); 查询接口回到无地形状态。
    void clearTerrain();
    /// 高度图是否已装载 (auto_generate=false 时初始为 false)。
    bool hasLoadedTerrain() const { return loaded_; }

private:
    class Query;
    friend class Query;

    /// 按当前参数重建高度场 (含水面), 并更新居中变换。
    void applyParams();

    std::unique_ptr<TerrainSystem>         terrain_;
    std::shared_ptr<Query>                 query_;
    vsg::ref_ptr<vsg::MatrixTransform>     terrain_mt_;
    ViewportContext* ctx_ = nullptr;
    nlohmann::json params_ = nlohmann::json::object();
    double   offset_ = 0.0;
    bool     auto_generate_ = true;   ///< 挂载时自动生成模板地形
    bool     centered_ = true;        ///< 地形居中到世界原点
    bool     loaded_ = false;         ///< 地形节点已挂入场景
    uint64_t gen_seen_ = 0;      ///< 已编译的地形网格代数
    uint64_t edit_version_ = 0;  ///< 高度场编辑版本
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_TERRAINLAYERFEATURE_H
