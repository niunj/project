#pragma once
/// @file Workspace.h
/// @brief 工作区: 磁盘文件对接三个模块的总枢纽。
///
/// 目录约定
/// --------
///   workspace/
///   ├── terrains/            # 地形编辑器的产物 (xxx.vsgterrain + xxx.ter + xxx.preview.png)
///   └── scenes/              # 场景设计器的产物 (xxx.scenario.json + xxx.preview.png)
///
/// 场景文件内通过 "terrain_ref" 引用地形工程, 仿真控制从场景文件直接初始化。
/// 预览图命名: 主文件名去掉扩展名后加 ".preview.png"。
#ifndef VSGRAYSIM_WORKSPACE_WORKSPACE_H
#define VSGRAYSIM_WORKSPACE_WORKSPACE_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace vsgraysim {

/// 工作区中的一个可浏览条目 (地形工程或场景文件)。
struct WorkspaceEntry {
    std::string path;          ///< 工程文件完整路径
    std::string name;          ///< 显示名称
    std::string preview;       ///< 预览图完整路径 (不存在则为空)
    nlohmann::json meta;       ///< 元数据 (场景名/统计/地形参数等)
};

/// 工作区扫描与元数据读取。
class Workspace {
public:
    explicit Workspace(std::string root);

    const std::string& root() const { return root_; }
    std::string terrainDir() const { return root_ + "/terrains"; }
    std::string sceneDir()    const { return root_ + "/scenes"; }

    /// 确保 terrains/ scenes/ 目录存在 (递归创建)。
    void ensureDirs() const;

    /// 扫描 terrains/ 下的地形工程 (*.vsgterrain), 附带元数据与预览路径。
    std::vector<WorkspaceEntry> listTerrains() const;

    /// 扫描 scenes/ 下的传统场景与地球体素专题 (*.json)。
    std::vector<WorkspaceEntry> listScenes() const;

    // ---- 路径工具 (静态) ----
    /// 预览图路径: "a/b/name.ext" -> "a/b/name.preview.png"。
    static std::string previewPathFor(const std::string& file);

    /// 场景文件的 terrain_ref 值 (未写则空串)。
    static std::string scenarioTerrainRef(const std::string& scenario_path);

    /// 将场景的 terrain_ref 解析为磁盘上实际存在的地形工程路径。
    /// 依次尝试: 相对工作区根 / 场景同目录 / 原样。找不到返回空串。
    static std::string resolveTerrainRef(const std::string& scenario_path,
                                         const std::string& workspace_root);

    /// 从地形工程 JSON 读取显示名 (无则用文件主名)。
    static std::string terrainDisplayName(const std::string& project_path);

private:
    std::string root_;
};

} // namespace vsgraysim

#endif // VSGRAYSIM_WORKSPACE_WORKSPACE_H
