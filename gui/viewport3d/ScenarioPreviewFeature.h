#pragma once
/// @file ScenarioPreviewFeature.h
/// @brief 场景装载预览特性: 场景 JSON → 实体模型 + 地形引用 + 传感器视锥
///        (迁移自 SimulationControlWindow::buildPreviewScene)。
///
/// 地形经 `terrain_layer` 特性装载 (模板地形不居中), `reference_grid`
/// 提供可贴地参考网格; 实体变换注册到 `playback` 特性供回放运动。
#ifndef VSGRAYSIM_VIEWPORT3D_SCENARIOPREVIEWFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_SCENARIOPREVIEWFEATURE_H

#include "IViewportFeature.h"

#include <vsg/core/ref_ptr.h>

#include <map>
#include <string>

namespace vsg { class Group; class MatrixTransform; }

namespace vsgraysim { namespace viewport3d {

class ScenarioPreviewFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "scenario_preview";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;

    /// 装载场景文件: 清旧实体 → 地形引用 → 实体模型 → 世界空间视锥。
    /// `workspace_root` 用于 terrain_ref 解析。失败返回 false。
    bool loadScenario(const std::string& scenario_path,
                      const std::string& workspace_root);
    /// 清空实体与视锥 (地形保留)。
    void clear();

    /// 已装载实体的变换节点 (回放/位姿更新用), 不存在返回空。
    vsg::MatrixTransform* entityTransform(const std::string& object_id) const;

private:
    vsg::ref_ptr<vsg::Group> entities_root_;   ///< 实体 mt 组
    std::map<std::string, vsg::ref_ptr<vsg::MatrixTransform>> plat_mt_;
    ViewportContext* ctx_ = nullptr;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_SCENARIOPREVIEWFEATURE_H
