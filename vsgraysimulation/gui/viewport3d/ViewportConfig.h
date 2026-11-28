#pragma once
/// @file ViewportConfig.h
/// @brief 视口特性组装配置 (L1 层): 特性列表 + 每特性 JSON 参数。
///
/// 三个内置预设对应三软件当前行为:
///   "scene_designer"      场景设计软件 (含地形编辑: 笔刷/分析/工程存取)
///   "simulation_display"  仿真显示 (控制台 3D 预览)
/// 也可 fromJson 加载外部配置 (config/viewport_profiles/).json。
#ifndef VSGRAYSIM_VIEWPORT3D_VIEWPORTCONFIG_H
#define VSGRAYSIM_VIEWPORT3D_VIEWPORTCONFIG_H

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

struct FeatureEntry {
    std::string    id;                    ///< 特性 id (注册表键)
    nlohmann::json params = nlohmann::json::object();  ///< 特性参数
    bool           enabled = true;
};

class ViewportConfig {
public:
    /// 预设: scene_designer / simulation_display / earth_display / env_lab / voxel_workshop。
    static ViewportConfig fromPreset(const std::string& name);
    /// JSON 格式: {"features":[{"id":"lights","params":{...},"enabled":true}, ...]}
    static ViewportConfig fromTopicFile(const std::string& path,const std::string& basePreset="earth_display");
    static ViewportConfig fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;
    void useWorkspaceGlobe();
    void useWorkspaceTerrain();

    /// 追加特性条目 (保持顺序; 重复 id 则更新参数)。
    void add(const std::string& id,
             const nlohmann::json& params = nlohmann::json::object());
    bool remove(const std::string& id);

    const std::vector<FeatureEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<FeatureEntry> entries_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_VIEWPORTCONFIG_H
