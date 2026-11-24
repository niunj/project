#include "vsgraysim/workspace/Workspace.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace vsgraysim {

namespace fs = std::filesystem;

Workspace::Workspace(std::string root)
    : root_(std::move(root))
{
}

void Workspace::ensureDirs() const
{
    std::error_code ec;
    fs::create_directories(terrainDir(), ec);
    fs::create_directories(sceneDir(), ec);
}

std::string Workspace::previewPathFor(const std::string& file)
{
    fs::path p(file);
    p.replace_extension();
    p += ".preview.png";
    return p.string();
}

std::string Workspace::scenarioTerrainRef(const std::string& scenario_path)
{
    std::ifstream in(scenario_path);
    if (!in) return {};
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception&) {
        return {};
    }
    return j.value("terrain_ref", std::string{});
}

std::string Workspace::resolveTerrainRef(const std::string& scenario_path,
                                         const std::string& workspace_root)
{
    const std::string ref = scenarioTerrainRef(scenario_path);
    if (ref.empty()) return {};

    const fs::path ref_path(ref);
    // 1) 原样 (绝对或相对 cwd)
    if (fs::exists(ref_path)) return ref;
    // 2) 相对工作区根: <root>/<ref> 或 <root>/<ref>.vsgterrain
    const std::string rooted = (fs::path(workspace_root) / ref_path).string();
    if (fs::exists(rooted)) return rooted;
    if (fs::exists(rooted + ".vsgterrain")) return rooted + ".vsgterrain";
    // 2b) 工作区 terrains/ 子目录: <root>/terrains/<ref> 或 +.vsgterrain
    const std::string rooted_terrain =
        (fs::path(workspace_root) / "terrains" / ref_path).string();
    if (fs::exists(rooted_terrain)) return rooted_terrain;
    if (fs::exists(rooted_terrain + ".vsgterrain")) return rooted_terrain + ".vsgterrain";
    // 3) 场景同目录
    const std::string sibling = (fs::path(scenario_path).parent_path() / ref_path).string();
    if (fs::exists(sibling)) return sibling;
    if (fs::exists(sibling + ".vsgterrain")) return sibling + ".vsgterrain";
    return {};
}

std::string Workspace::terrainDisplayName(const std::string& project_path)
{
    std::ifstream in(project_path);
    if (in) {
        nlohmann::json j;
        try {
            in >> j;
            if (j.is_object() && j.contains("name"))
                return j["name"].get<std::string>();
        } catch (const std::exception&) {
        }
    }
    fs::path p(project_path);
    return p.stem().string();
}

std::vector<WorkspaceEntry> Workspace::listTerrains() const
{
    std::vector<WorkspaceEntry> out;
    std::error_code ec;
    if (!fs::is_directory(terrainDir(), ec)) return out;
    for (const auto& it : fs::directory_iterator(terrainDir(), ec)) {
        if (!it.is_regular_file()) continue;
        const std::string ext = it.path().extension().string();
        if (ext != ".vsgterrain" && ext != ".vsgproject") continue;

        WorkspaceEntry e;
        e.path = it.path().string();
        e.name = it.path().stem().string();
        // 读取工程 JSON 元数据 (网格/范围/水面等), 供启动页详情面板展示。
        std::ifstream in(it.path());
        nlohmann::json j;
        try {
            in >> j;
        } catch (const std::exception&) {
            j = nullptr;
        }
        if (j.is_object()) {
            e.meta = j;
            if (j.contains("name") && j["name"].is_string() &&
                !j["name"].get<std::string>().empty())
                e.name = j["name"].get<std::string>();
        }
        const std::string prev = previewPathFor(e.path);
        if (fs::exists(prev)) e.preview = prev;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const WorkspaceEntry& a, const WorkspaceEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

std::vector<WorkspaceEntry> Workspace::listScenes() const
{
    std::vector<WorkspaceEntry> out;
    std::error_code ec;
    if (!fs::is_directory(sceneDir(), ec)) return out;
    for (const auto& it : fs::directory_iterator(sceneDir(), ec)) {
        if (!it.is_regular_file()) continue;
        if (it.path().extension().string() != ".json" && it.path().extension().string() != ".vsimscene") continue;
        // Both existing scenarios and saved Earth topics are runnable projects.
        if(it.file_size()>256ULL*1024*1024)continue;
        std::ifstream in(it.path());
        nlohmann::json j;
        try {
            in >> j;
        } catch (const std::exception&) {
            continue;
        }
        if(!j.is_object()||(j.contains("format")&&!j["format"].is_string()))continue;
        const bool packaged=j.value("format",std::string())=="vsgraysim-scene-package-v1";
        if(packaged){try{if(j.value("version",0)!=1||!j.at("topic").is_object()||!j.at("checkpoint").is_object()||!j.at("objects").is_array()||j.at("objects").size()>256)continue;auto topic=j.at("topic");if(topic.value("format",std::string())!="earth-scene-topic-v1")continue;topic["checkpoint"]=j.at("checkpoint");topic["checkpoint"]["objects"]=j.at("objects");j=std::move(topic);}catch(const std::exception&){continue;}}
        const bool topic=j.value("format",std::string())=="earth-scene-topic-v1";
        if(!topic&&!j.contains("objects"))continue;

        WorkspaceEntry e;
        e.path = it.path().string();
        e.name = j.value("scenario_name",j.value("name",it.path().stem().string()));
        if(topic) {
            // Startup cards must not retain every model/DEM checkpoint in RAM.
            e.meta={{"format","earth-scene-topic-v1"},{"scenario_name",e.name},{"objects",nlohmann::json::array()},{"terrain_ref",j.value("earth_map",std::string())}};
            auto state=j.value("checkpoint",nlohmann::json::object());
            if(state.empty()&&j.contains("scene")&&j["scene"].is_string())try{auto source=fs::path(j["scene"].get<std::string>());if(source.is_relative())source=it.path().parent_path()/source;std::ifstream(source)>>state;}catch(...){}
            for(const auto& object:state.value("objects",nlohmann::json::array()))e.meta["objects"].push_back({{"object_id",object.value("id",std::string())}});
            auto equipment=j.value("equipment",nlohmann::json::object());
            if(equipment.is_string())try{auto source=fs::path(equipment.get<std::string>());if(source.is_relative())source=it.path().parent_path()/source;equipment=nlohmann::json::parse(std::ifstream(source));}catch(...){equipment=nlohmann::json::object();}
            e.meta["sensors"]["sensor_list"]=nlohmann::json::array();
            if(equipment.is_object())for(const auto& sensor:equipment.value("equipment",nlohmann::json::array()))e.meta["sensors"]["sensor_list"].push_back({{"type",sensor.value("band",std::string("?"))}});
        } else e.meta = j;
        const std::string prev = previewPathFor(e.path);
        if (fs::exists(prev)) e.preview = prev;
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const WorkspaceEntry& a, const WorkspaceEntry& b) {
                  return a.name < b.name;
              });
    return out;
}

} // namespace vsgraysim
