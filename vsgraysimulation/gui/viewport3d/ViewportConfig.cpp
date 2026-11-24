#include <vsgraysim/broker/VoxelEquipmentSource.h>
/// @file ViewportConfig.cpp
#include "ViewportConfig.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace vsgraysim { namespace viewport3d {

ViewportConfig ViewportConfig::fromPreset(const std::string& name)
{
    ViewportConfig cfg;
    // 通用基础两件套: 灯光 / 相机限高。(轴向指示 axis_gizmo 默认不显示,
    // 以免遮挡视口; 需要时可在此 add("axis_gizmo") 显式启用。)
    auto add_common = [&cfg]() {
        cfg.add("lights");
        cfg.add("camera_guard");
    };

    if(name=="voxel_workshop") {
        cfg.add("rocky_map",{{"lat",25.078},{"lon",121.772},{"alt",182.},{"view_height",100.},
            {"map_json","config/earth/native-volume.map.json"},{"scene_context",false}});
        cfg.add("rocky_sky",{{"hours_utc",4.0}});
        add_common();
        cfg.add("voxel_scene",{{"scene_json","config/voxel/workshop.vscene.json"},{"paused",true}});
    } else if (name == "scene_designer") {
        // 场景设计: rocky 真实地球为底座 (首位挂载, 建立地理锚点内容根),
        // 模型/标注/特效按局部米制摆放在锚点 ENU 切平面上 → 落到地球表面。
        // 地形编辑能力已从独立"地形编辑软件"并入: terrain_layer 高度场 +
        // brush_edit 画刷 + analysis_overlay 分析覆盖层, 支持程序化生成/
        // 笔刷雕刻/水面/分析/工程存取/rocky 地形包导出的完整地形工作流。
        cfg.add("rocky_map", {
            {"lat", 25.078}, {"lon", 121.772}, {"alt", 0.0},
            {"view_height", 900.0}, {"stream_camera",true}
        });
        // 环境: 大气天空 + 云层 + 近海面 (台北作业区靠海), 提供真实光照/海天背景。
        cfg.add("rocky_sky", {
            {"hours_utc", 4.0}, {"sun_el", 62.0}, {"sun_az", 175.0}});
        cfg.add("rocky_cloud", {
            {"sun_el", 62.0}, {"sun_az", 175.0}, {"cover", 0.5}});
        cfg.add("rocky_ocean", {
            {"sun_el", 62.0}, {"sun_az", 175.0}, {"ocean_size", 8000.0}});
        // 以世界原点为中心，专注于模型摆放与资产部署，不做航迹/时间编辑。
        cfg.add("reference_grid");   // 无限参考网格 (锚点切平面)
        cfg.add("terrain_layer", {{"auto_generate", false}, {"centered", true}});
        add_common();
        cfg.add("axis_gizmo");     // 右下角 XYZ 轴向指示
        cfg.add("brush_edit");             // 地形笔刷 (与摆放模式互斥)
        cfg.add("analysis_overlay");       // 坡度/坡向/材质分析覆盖层
        cfg.add("detection_effect");       // 装备探测范围/搜索角度/视口范围特效
        cfg.add("relationship_visual");   // 3D 平台标注 (可经工具栏切换)
        cfg.add("imgui_toolbar", {   // 场景设计软件工具条
            {"title", "场景显示控制"},
            {"toggles", nlohmann::json::array({
                {{"id", "relationship_visual"}, {"label", "平台标注"}},
                {{"id", "detection_effect"}, {"label", "探测范围"}},
                {{"id", "reference_grid"}, {"label", "参考网格"}},
            })}
        });
    } else if (name == "simulation_display") {
        // 仿真显示: 与场景设计共享同一 rocky 地球 (WYSIWYG), 场景在锚点 ENU 推演。
        cfg.add("rocky_map", {
            {"lat", 25.078}, {"lon", 121.772}, {"alt", 0.0},
            {"view_height", 1400.0}
        });
        // 环境: 大气天空 + 云层 + 海面 (与场景设计同一地球, WYSIWYG)。
        cfg.add("rocky_sky", {
            {"hours_utc", 4.0}, {"sun_el", 62.0}, {"sun_az", 175.0}});
        cfg.add("rocky_cloud", {
            {"sun_el", 62.0}, {"sun_az", 175.0}, {"cover", 0.5}});
        cfg.add("rocky_ocean", {
            {"sun_el", 62.0}, {"sun_az", 175.0}, {"ocean_size", 8000.0}});
        // 参考网格 + 地形层 + 视锥 + 场景装载 + 回放运动 + 轴向指示。
        cfg.add("reference_grid");   // 无限参考网格 (锚点切平面)
        cfg.add("terrain_layer", {{"auto_generate", false}, {"centered", false}});
        add_common();
        cfg.add("axis_gizmo");    // 右下角 XYZ 轴向指示
        cfg.add("sensor_fov");
        cfg.add("detection_effect");       // 装备探测范围/搜索角度/视口范围特效
        cfg.add("scenario_preview");
        cfg.add("playback");
        cfg.add("relationship_visual");   // 3D 平台标注 (可经工具栏切换)
        cfg.add("engagement_visual");     // 交战过程: 制导连线/弹道/命中爆炸/摧毁残骸
        cfg.add("trail");                 // 实体运动轨迹拖尾
        cfg.add("imgui_toolbar", {   // 仿真控制软件工具条
            {"title", "仿真显示控制"},
            {"toggles", nlohmann::json::array({
                {{"id", "relationship_visual"}, {"label", "平台标注"}},
                {{"id", "sensor_fov"}, {"label", "传感器视锥"}},
                {{"id", "detection_effect"}, {"label", "探测范围"}},
                {{"id", "engagement_visual"}, {"label", "交战过程"}},
                {{"id", "trail"}, {"label", "运动轨迹"}},
                {{"id", "reference_grid"}, {"label", "参考网格"}},
            })}
        });
    } else if (name == "earth_display") {
        cfg.add("rocky_map", {{"lat",25.078},{"lon",121.772},{"alt",0.0},{"view_height",1400.0}});
        cfg.add("rocky_sky", {{"hours_utc",4.0},{"sun_el",62.0},{"sun_az",175.0}});
        cfg.add("rocky_cloud", {{"cover",0.5}});
        cfg.add("rocky_ocean", {{"ocean_size",8000.0}});
        add_common();
    } else if (name == "env_lab") {
        // 环境模块验证台 (env_lab): Rocky 地球基座 —— 先挂 rocky_map (默认锚点,
        // 建立 VSGContext 与 ECEF 世界根), sky/cloud/ocean 以锚点 ENU 为基准
        // 独立验证。模块的加载/卸载由 EnvLabWindow 的面板复选框动态控制,
        // 此处只给基础两件套 + 默认三模块。
        // 注意: 不启用 axis_gizmo / imgui_toolbar (依赖 vsgImGui 前景层),
        // 验证台以 Qt 侧控件面板为主, 避免额外 ImGui 依赖与生命周期耦合。
        add_common();
        cfg.add("rocky_map");    // 默认锚点 + 内置 TMS 底图 (天空需其 VSGContext)
        cfg.add("rocky_sky", {
            {"hours_utc", 4.0}, {"time_scale", 1.0}});
        cfg.add("rocky_cloud", {
            {"cover", 0.5}, {"cumulus_altitude", 900.0}});
        cfg.add("rocky_ocean", {
            {"ocean_size", 4000.0}, {"wind_speed", 10.0}, {"wind_az", 80.0}});
    } else {
        add_common();   // 未知预设 → 仅基础三件套
    }
    return cfg;
}

ViewportConfig ViewportConfig::fromTopicFile(const std::string& path,const std::string& basePreset){
    const auto project=VoxelSceneProject::open(path);const auto& topic=project.document;
    auto resolve=[&](const std::string& value){auto p=std::filesystem::path(value);if(p.is_relative())p=std::filesystem::path(project.path).parent_path()/p;return std::filesystem::weakly_canonical(p).string();};
    const auto scenePath=project.scenePath;nlohmann::json scene;
    if(!project.packageManifest.is_null())scene=project.packageManifest.at("checkpoint");else if(topic.contains("checkpoint"))scene=topic.at("checkpoint");else std::ifstream(scenePath)>>scene;
    const auto anchor=scene.at("geo_anchor");auto cfg=fromPreset(basePreset);cfg.remove("rocky_ocean");
    auto earth=topic.value("earth",nlohmann::json::object());earth.update({{"map_json",resolve(topic.at("earth_map"))},{"lat",anchor.at("lat")},{"lon",anchor.at("lon")},{"alt",anchor.at("alt")},{"cloud_objects",true},{"scene_context",true}});cfg.add("rocky_map",earth);
    auto sky=scene.value("utc_origin",nlohmann::json::object());sky.update(topic.value("sky",nlohmann::json::object()));cfg.add("rocky_sky",sky);
    auto clouds=topic.value("clouds",nlohmann::json::object());cfg.add("rocky_cloud",clouds);
    nlohmann::json runtime={{"scene_json",scenePath},{"background_loading",true},{"paused",topic.value("paused",true)},{"frame_scene",!topic.contains("camera")}};
    for(const auto* visualSetting:{"clustered_rendering","cluster_error_pixels"})
        if(earth.contains(visualSetting))runtime[visualSetting]=earth.at(visualSetting);
    if(topic.contains("camera"))runtime["camera_local"]=topic.at("camera");
    if(topic.contains("checkpoint")){runtime["checkpoint"]=topic.at("checkpoint");runtime["background_loading"]=false;}
    cfg.add("voxel_scene",runtime);return cfg;
}

ViewportConfig ViewportConfig::fromJson(const nlohmann::json& j)
{
    ViewportConfig cfg;
    if (!j.is_object()) return cfg;
    for (const auto& je : j.value("features", nlohmann::json::array())) {
        if (!je.is_object() || !je.contains("id")) continue;
        FeatureEntry e;
        e.id = je.value("id", std::string());
        e.params = je.value("params", nlohmann::json::object());
        e.enabled = je.value("enabled", true);
        if (!e.id.empty()) cfg.entries_.push_back(std::move(e));
    }
    return cfg;
}

nlohmann::json ViewportConfig::toJson() const
{
    nlohmann::json j = nlohmann::json::object();
    j["features"] = nlohmann::json::array();
    for (const auto& e : entries_) {
        j["features"].push_back({
            {"id", e.id},
            {"params", e.params},
            {"enabled", e.enabled},
        });
    }
    return j;
}

void ViewportConfig::add(const std::string& id, const nlohmann::json& params)
{
    for (auto& e : entries_) {
        if (e.id == id) {          // 已有 → 合并参数
            e.params.update(params);
            return;
        }
    }
    entries_.push_back({id, params, true});
}

bool ViewportConfig::remove(const std::string& id)
{
    const size_t n = entries_.size();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->id == id) it = entries_.erase(it);
        else ++it;
    }
    return entries_.size() != n;
}

}} // namespace vsgraysim::viewport3d
