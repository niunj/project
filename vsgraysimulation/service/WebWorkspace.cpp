#include "WebWorkspace.h"
#include "ModelLibrary.h"
#include <Deployment.h>
#include <simcore/GeoFrame.h>
#include <vsgraysim/broker/VoxelEquipmentSource.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <set>

namespace vsgraysim::service {
namespace {
using Json = WebWorkspace::Json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

Json read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return nullptr;
    const auto bytes = file.readAll();
    return Json::parse(bytes.begin(), bytes.end(), nullptr, false);
}

void saveFile(const QString& path, const Json& json) {
    QSaveFile file(path);
    const auto data = QByteArray::fromStdString(json.dump());
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw WorkspaceError{503, "workspace_storage", "工作区写入失败"};
}

Json validatedCamera(const Json& camera) {
    if (!camera.is_object()) throw WorkspaceError{400, "invalid_camera", "无效的视口相机"};
    const auto mode = camera.value("preset", "tb");
    const double yaw = camera.value("yaw_deg", 0.);
    const double pitch = camera.value("pitch_deg", 0.);
    const double zoom = camera.value("zoom", 1.);
    const double panX = camera.value("pan_x", 0.), panY = camera.value("pan_y", 0.);
    const bool workspace = mode == "workspace";
    if ((!workspace && mode != "tb" && mode != "area" && mode != "scene" && mode != "globe") || !std::isfinite(yaw) ||
        std::abs(yaw) > 360 || !std::isfinite(pitch) || std::abs(pitch) > 89 ||
        !std::isfinite(zoom) || zoom < (workspace ? .004 : .25) || zoom > (workspace ? 1600 : 4) ||
        !std::isfinite(panX) || !std::isfinite(panY) || std::abs(panX) > 4 || std::abs(panY) > 4)
        throw WorkspaceError{400, "invalid_camera", "视口相机参数超出范围"};
    Json result = {{"preset", mode == "area" ? "tb" : mode}, {"yaw_deg", yaw}, {"pitch_deg", pitch},
                   {"zoom", zoom}, {"pan_x", panX}, {"pan_y", panY}};
    if (camera.contains("input_revision")) {
        const auto& revision = camera.at("input_revision");
        if (!revision.is_number_unsigned() || revision.get<uint64_t>() > 9007199254740991ULL)
            throw WorkspaceError{400,"invalid_camera","无效的相机输入版本"};
        result["input_revision"] = revision;
    }
    if (workspace && camera.contains("focus_m")) {
        const auto& focus = camera.at("focus_m");
        if (!focus.is_array() || focus.size() != 3 ||
            !std::all_of(focus.begin(),focus.end(),[](const Json& value) {
                return value.is_number() && std::isfinite(value.get<double>()) && std::abs(value.get<double>()) <= 1e6;
            })) throw WorkspaceError{400,"invalid_camera","无效的场景定位坐标"};
        result["focus_m"] = focus;
    }
    return result;
}

Json validatePositions(const Json& items, const Json& positions) {
    if (!positions.is_object() || positions.size() > 96)
        throw WorkspaceError{400, "invalid_layout", "目标坐标必须以对象 ID 为键"};
    for (const auto& [id, triple] : positions.items()) {
        const auto known = std::any_of(items.begin(), items.end(), [&](const Json& item) {
            return item.at("id") == id && item.at("editable") == true;
        });
        if (!known || !triple.is_array() || triple.size() != 3)
            throw WorkspaceError{400, "invalid_layout", "目标不支持位置编辑或坐标不完整"};
        for (const auto& component : triple) {
            if (!component.is_number() || std::abs(component.get<double>()) > 1000000)
                throw WorkspaceError{400, "invalid_layout", "局部坐标必须是有效的米制数值"};
        }
    }
    return positions;
}

void appendDeployedObject(Json& items, const Json& action) {
    items.push_back({{"id",action.at("id")},{"role","target"},{"position_m",action.at("position").at("local_m")},
                     {"editable",true},{"label",action.value("name",action.at("model").get<std::string>())}});
}

Json localPosition(const Json& position, const Json& anchor) {
    if (position.size() == 1 && position.contains("local_m")) return position.at("local_m");
    if (position.size() == 1 && position.contains("wgs84") && anchor.contains("lat")) {
        const auto& point = position.at("wgs84");
        const simcore::geo::GeoAnchor origin{anchor.at("lat").get<double>(),anchor.at("lon").get<double>(),
            anchor.at("alt").get<double>(),anchor.value("heading_deg",0.)};
        const auto local = simcore::geo::ecefToLocal(origin,simcore::geo::geodeticToECEF(
            point[0].get<double>(),point[1].get<double>(),point[2].get<double>()));
        return Json::array({local.x,local.y,local.z});
    }
    throw WorkspaceError{400,"deployment_position","请输入东、上、南米制坐标，或 WGS84 经纬度与椭球高"};
}

void recordEdit(Json& layout, const Json& expected, const std::set<std::string>& fields, const std::string& author) {
    const auto current = layout.at("revision").get<uint64_t>();
    if (!expected.is_number_unsigned() || expected.get<uint64_t>() > current ||
        expected.get<uint64_t>() < layout.at("collaboration_base").get<uint64_t>())
        throw WorkspaceError{409,"layout_conflict","工程版本不可用，请刷新后重试"};
    const auto base = expected.get<uint64_t>();
    auto& versions = layout.at("field_versions");
    for (const auto& field : fields)
        if (versions.value(field,uint64_t(0)) > base)
            throw WorkspaceError{409,"field_conflict","该对象的同一属性已被其他编辑更新，请重新预检"};
    for (const auto& field : fields) versions[field] = current + 1;
    layout["last_edit"] = {{"author",author},{"base_revision",base},{"revision",current + 1},{"fields",fields}};
}
}

struct WebWorkspace::View {
    std::string id;
    QString directory, topic, statusPath;
    Json state;
    QProcess process;
    Clock::time_point restartRequested{};
    bool restarting = false;
    Clock::time_point used = Clock::now();
};

WebWorkspace::WebWorkspace(QString projectRoot, QString scratch, const ModelLibrary& models)
    : root_(std::move(projectRoot)), scratch_(std::move(scratch)),
      renderer_(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../app/vsgraysim_web_view")),
      drafts_(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/layouts"), models_(models) {}

WebWorkspace::~WebWorkspace() {
    for (auto& [owner, view] : views_) {
        view->restarting = false;
        view->process.closeWriteChannel();
    }
    for (auto& [owner, view] : views_) {
        if (view->process.state() != QProcess::NotRunning && !view->process.waitForFinished(1500)) {
            view->process.kill();
            view->process.waitForFinished(1000);
        }
    }
}

bool WebWorkspace::available() const { return QFileInfo(renderer_).isExecutable(); }

void WebWorkspace::setFrameNotification(std::function<void()> notify) {
    QObject::connect(&viewWatcher_, &QFileSystemWatcher::directoryChanged, &viewWatcher_,
                     [notify = std::move(notify)](const QString&) { notify(); });
}

QString WebWorkspace::layoutPath(const std::string& project, const std::string& revision) const {
    const auto key = QByteArray::fromStdString(project) + '\0' + QByteArray::fromStdString(revision);
    return QDir(drafts_).filePath(QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) + ".json");
}

WebWorkspace::Json WebWorkspace::layout(const QString& topic, const std::string& project, const std::string& revision) const {
    const auto source = VoxelSceneProject::open(topic.toStdString());
    auto scene = source.packageManifest.is_object() ? source.packageManifest.at("checkpoint")
        : source.document.contains("checkpoint") ? source.document.at("checkpoint") : read(QString::fromStdString(source.scenePath));
    const bool editable = source.packageManifest.is_null() && !source.document.contains("checkpoint") &&
        scene.is_object() && scene.value("format", "") == std::string("voxel-scene-v1");
    auto items = Json::array();
    const auto canonicalScene = QFileInfo(QString::fromStdString(source.scenePath)).canonicalFilePath();
    if (scene.is_object()) {
        for (const auto& entry : scene.value("objects", Json::array())) {
            const auto role = entry.value("role", "target");
            Json item = {{"id",entry.at("id")},{"role",role},
                {"position_m",entry.value("position_m",Json::array({0,0,0}))},
                {"editable",editable && role=="target" && !entry.contains("pose_track") &&
                    !entry.contains("rotor_drive") && !entry.contains("parent")}};
            if (editable && role == "target") {
                const auto model = models_.matchingModel(canonicalScene, entry.at("id"));
                if (!model.empty()) item["model_id"] = model;
            }
            items.push_back(std::move(item));
        }
    }
    const auto stored = read(layoutPath(project, revision));
    const auto deployments = stored.is_object() ? stored.value("deployments",Json::array()) : Json::array();
    const auto current = stored.is_object() ? stored.at("revision").get<uint64_t>() : 0;
    const auto platforms = source.equipmentDocument.is_object()
        ? source.equipmentDocument.value("platforms",Json::array()).size() : size_t(0);
    for (const auto& action : deployments) appendDeployedObject(items,action);
    return {{"project_id",project},{"published_revision_id",revision},{"editable",editable},
            {"revision",current},
            {"collaboration_base",stored.is_object()?stored.value("collaboration_base",current):current},
            {"field_versions",stored.is_object()?stored.value("field_versions",Json::object()):Json::object()},
            {"last_edit",stored.is_object()?stored.value("last_edit",Json(nullptr)):Json(nullptr)},
            {"added_target_budget",64 - std::min(size_t(64),platforms)},
            {"environment",{{"ocean",scene.is_object() && scene.contains("ocean")}}},
            {"positions",stored.is_object()?stored.at("positions"):Json::object()},
            {"model_bindings",stored.is_object()?stored.value("model_bindings",Json::object()):Json::object()},
            {"deployments",deployments},
            {"objects",items},{"geo_anchor",scene.is_object()?scene.value("geo_anchor",Json::object()):Json::object()}};
}

WebWorkspace::Json WebWorkspace::save(const QString& topic, const std::string& project, const std::string& revision,
                                      const Json& data, const std::string& author) {
    if (!QDir().mkpath(drafts_)) throw WorkspaceError{503,"workspace_storage","无法创建草稿目录"};
    QLockFile lock(layoutPath(project,revision) + ".lock");
    if (!lock.tryLock()) throw WorkspaceError{409,"layout_busy","工程正在提交，请重试"};
    auto result = layout(topic, project, revision);
    if (!result.at("editable").get<bool>()) throw WorkspaceError{409, "layout_read_only", "该版本仅可浏览"};
    std::set<std::string> fields;
    if (data.contains("plan")) {
        result = previewDeployment(std::move(result),data.at("plan"));
        result.erase("preview_plan");
        for (const auto& action : data.at("plan").at("actions")) {
            if (action.at("op") == "add_target") {
                const auto id = action.at("id").get<std::string>();
                fields.insert("placement/" + id + "/definition");
                fields.insert("placement/" + id + "/position");
            } else fields.insert("placement/" + action.at("object").get<std::string>() + "/position");
        }
    } else {
        if (data.at("expected_revision") != result.at("revision"))
            throw WorkspaceError{409,"layout_conflict","工程已更新，请刷新后重试"};
        const auto positions = validatePositions(result.at("objects"), data.at("positions"));
        for (const auto& [id,value] : positions.items())
            if (value != result["positions"].value(id,Json(nullptr))) fields.insert("placement/" + id + "/position");
        for (const auto& [id,value] : result["positions"].items())
            if (!positions.contains(id)) fields.insert("placement/" + id + "/position");
        result["positions"] = positions;
    }
    recordEdit(result,data.at("expected_revision"),fields,author);
    saveLayout(project, revision, result);
    publishLayout(topic,result);
    return result;
}

WebWorkspace::Json WebWorkspace::previewDeployment(Json layout, const Json& plan) const {
    if (!layout.at("editable").get<bool>()) throw WorkspaceError{409,"layout_read_only","该版本仅可浏览"};
    Json validated;
    try { validated = scene_deployment::validate(plan); }
    catch (const std::invalid_argument&) { throw WorkspaceError{400,"invalid_plan","智能部署方案无效"}; }
    auto& actions = validated.at("actions");
    if (actions.empty()) throw WorkspaceError{400,"empty_plan","请至少提供一项部署操作"};
    auto& objects = layout.at("objects");
    auto& deployments = layout.at("deployments");
    auto& positions = layout.at("positions");
    const auto catalog = read(QDir(root_).filePath("config/deployment/catalog.json"));
    const auto models = catalog.is_object() ? catalog.at("models") : Json::object();
    for (auto& action : actions) {
        const auto op = action.at("op").get<std::string>();
        if (op != "add_target" && op != "place")
            throw WorkspaceError{400,"unsupported_plan","网页部署支持添加目录模型和移动目标"};
        const auto position = localPosition(action.at("position"),layout.at("geo_anchor"));
        if (std::any_of(position.begin(),position.end(),[](const Json& value){ return std::abs(value.get<double>()) > 1e6; }))
            throw WorkspaceError{400,"deployment_position","坐标超出工程可编辑范围"};
        action["position"] = {{"local_m",position}};
        if (op == "add_target") {
            if (deployments.size() >= layout.at("added_target_budget").get<size_t>())
                throw WorkspaceError{400,"deployment_limit","此工程的目标容量已满"};
            const auto id = action.at("id").get<std::string>();
            if (std::any_of(objects.begin(),objects.end(),[&](const Json& item){ return item.at("id") == id; }))
                throw WorkspaceError{409,"duplicate_target","目标编号已存在，请为新目标指定不同编号"};
            const auto model = action.at("model").get<std::string>();
            if (!models.contains(model) || !QFileInfo(QDir(root_).filePath(QString::fromStdString(
                    models.at(model).at("source").at("path").get<std::string>()))).isFile())
                throw WorkspaceError{400,"deployment_model","目录模型不可用。可选择 survey-vessel、skywalker-x8 或 3dr-iris"};
            auto entry = action;
            if (!entry.contains("name")) entry["name"] = models.at(model).at("label");
            appendDeployedObject(objects,entry);
            deployments.push_back(std::move(entry));
        } else {
            positions[action.at("object").get<std::string>()] = position;
        }
    }
    validatePositions(objects,positions);
    layout["preview_plan"] = std::move(validated);
    return layout;
}

WebWorkspace::Json WebWorkspace::deployment(const Json& layout) const {
    if (layout.at("deployments").empty()) return nullptr;
    return {{"catalog",QDir(root_).filePath("config/deployment/catalog.json").toStdString()},
            {"plan",{{"format",scene_deployment::format},{"actions",layout.at("deployments")}}}};
}

WebWorkspace::Json WebWorkspace::bindModel(const QString& topic, const std::string& project, const std::string& revision,
                                           const Json& data, const std::string& author) {
    if (!QDir().mkpath(drafts_)) throw WorkspaceError{503,"workspace_storage","无法创建草稿目录"};
    QLockFile lock(layoutPath(project,revision) + ".lock");
    if (!lock.tryLock()) throw WorkspaceError{409,"layout_busy","工程正在提交，请重试"};
    auto result = layout(topic, project, revision);
    const auto id = data.at("object").get<std::string>();
    const auto& objects = result.at("objects");
    const auto found = std::find_if(objects.begin(), objects.end(), [&](const Json& item) {
        return item.at("id") == id && item.contains("model_id");
    });
    if (found == objects.end()) throw WorkspaceError{400,"model_binding","目标没有可用的模型档案"};
    recordEdit(result,data.at("expected_revision"),{"materials/" + id},author);
    auto& bindings = result.at("model_bindings");
    if (data.at("model_revision").is_null()) bindings.erase(id);
    else bindings[id] = models_.sensorBinding(found->at("model_id").get<std::string>(), data.at("model_revision"));
    saveLayout(project, revision, result);
    publishLayout(topic,result);
    return result;
}

void WebWorkspace::saveLayout(const std::string& project, const std::string& revision, Json& result) {
    result["revision"] = result.at("revision").get<uint64_t>() + 1;
    if (!QDir().mkpath(drafts_)) throw WorkspaceError{503, "workspace_storage", "无法创建草稿目录"};
    saveFile(layoutPath(project, revision), {{"positions", result.at("positions")},{"revision", result.at("revision")},
        {"model_bindings",result.at("model_bindings")},{"deployments",result.at("deployments")},
        {"collaboration_base",result.at("collaboration_base")},{"field_versions",result.at("field_versions")},
        {"last_edit",result.at("last_edit")}});
}

WebWorkspace::Json WebWorkspace::open(const std::string& owner, const QString& topic,
                                     const QString& statusPath, const Json& camera, const Json& positions, const Json& model,
                                     const Json& bindings, const Json& deployment, const Json& sharedLayout) {
    if (!available()) throw WorkspaceError{503, "renderer_unavailable", "请构建 VSG 浏览器视口进程"};
    if (qEnvironmentVariableIsEmpty("DISPLAY"))
        throw WorkspaceError{503, "renderer_display", "Vulkan 渲染进程需要本机 X11 显示"};
    auto found = views_.find(owner);
    if (found == views_.end()) {
        if (views_.size() >= 3) throw WorkspaceError{429, "view_limit", "渲染工作进程已满，请稍后重试"};
        found = views_.emplace(owner, std::make_unique<View>()).first;
        auto& view = *found->second;
        view.directory = scratch_ + "/web-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        if (!QDir().mkpath(view.directory)) {
            views_.erase(found);
            throw WorkspaceError{503, "workspace_storage", "无法创建视口目录"};
        }
        viewWatcher_.addPath(view.directory);
        view.process.setProgram(renderer_);
        view.process.setArguments({view.directory});
        view.process.setWorkingDirectory(root_);
        view.process.setProcessChannelMode(QProcess::ForwardedChannels);
        QObject::connect(&view.process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished),
                         &view.process, [&view](int, QProcess::ExitStatus) {
            if (!view.restarting) return;
            view.restarting = false;
            view.process.start();
        });
    }
    auto& view = *found->second;
    view.used = Clock::now();
    const auto next = validatedCamera(camera);
    const bool shared = sharedLayout.is_object();
    if (shared != (next.at("preset") == "workspace"))
        throw WorkspaceError{400,"invalid_camera","协作视口需要选择场景工程"};
    const auto project = shared ? sharedLayout.at("project_id") : Json(nullptr);
    const auto revision = shared ? sharedLayout.at("published_revision_id") : Json(nullptr);
    const bool changed = view.id.empty() || view.topic != topic || view.statusPath != statusPath ||
        view.state.value("project_id",Json(nullptr)) != project || view.state.value("published_revision_id",Json(nullptr)) != revision ||
        view.state.value("shared_earth",false) != shared || view.state.value("model", Json(nullptr)) != model ||
        (!shared && (view.state.value("positions", Json::object()) != positions ||
        view.state.value("model_bindings", Json::object()) != bindings || view.state.value("deployment",Json(nullptr)) != deployment));
    if (changed || view.process.state() == QProcess::NotRunning) {
        view.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        view.topic = topic;
        view.statusPath = statusPath;
        view.state = {{"id",view.id},{"topic",topic.toStdString()},{"status_path",statusPath.toStdString()},
                      {"camera",next},{"camera_revision",0},{"positions",positions},{"model",model},
                      {"model_bindings",bindings},{"deployment",deployment},
                      {"shared_earth",shared},{"layout_revision",shared ? sharedLayout.at("revision") : Json(-1)},
                      {"project_id",project},{"published_revision_id",revision}};
        restart(view);
    } else if (view.state.at("camera") != next) {
        this->camera(owner, view.id, next);
    }
    updateFrameIntervals();
    return {{"view_id",view.id}};
}

void WebWorkspace::publishLayout(const QString& topic, const Json& layout) {
    for (auto& [owner,view] : views_) {
        if (view->topic != topic || !view->state.value("shared_earth",false) || !view->statusPath.isEmpty() ||
            view->state.at("project_id") != layout.at("project_id") ||
            view->state.at("published_revision_id") != layout.at("published_revision_id")) continue;
        view->state["positions"] = layout.at("positions");
        view->state["model_bindings"] = layout.at("model_bindings");
        view->state["deployment"] = deployment(layout);
        view->state["layout_revision"] = layout.at("revision");
        try { write(*view); } catch (const WorkspaceError&) {
            // The committed draft is durable. Reopening the view rehydrates it.
            view->process.closeWriteChannel();
        }
    }
}

WebWorkspace::View& WebWorkspace::view(const std::string& owner, const std::string& id) {
    const auto found = views_.find(owner);
    if (found == views_.end() || found->second->id != id)
        throw WorkspaceError{404, "view_expired", "视口已切换或已过期"};
    found->second->used = Clock::now();
    return *found->second;
}

void WebWorkspace::write(View& view) { saveFile(view.directory + "/request.json", view.state); }

void WebWorkspace::updateFrameIntervals() {
    const int interval = views_.size() >= 3 ? 80 : 33;
    for (auto& [owner, view] : views_) {
        if (view->state.value("frame_interval_ms",33) == interval) continue;
        auto old = view->state;
        view->state["frame_interval_ms"] = interval;
        try {
            write(*view);
        } catch (const WorkspaceError&) {
            view->state = std::move(old);
        }
    }
}

void WebWorkspace::restart(View& view) {
    view.state["epoch"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    write(view);
    if (view.process.state() == QProcess::NotRunning) {
        view.restarting = false;
        view.process.start();
        return;
    }
    if (!view.restarting) {
        view.restarting = true;
        view.restartRequested = Clock::now();
        view.process.closeWriteChannel();
    }
}

void WebWorkspace::camera(const std::string& owner, const std::string& id, const Json& data) {
    auto& current = view(owner, id);
    const auto next = validatedCamera(data);
    const auto& previous = current.state.at("camera");
    if (current.state.value("shared_earth",false) != (next.at("preset")=="workspace"))
        throw WorkspaceError{400,"invalid_camera","此视口使用当前工程的地球相机"};
    if (previous.contains("input_revision") && next.contains("input_revision") &&
        next.at("input_revision") <= previous.at("input_revision")) return;
    if (previous == next) return;
    const bool changeRange = (previous.at("preset") == "globe") != (next.at("preset") == "globe");
    current.state["camera"] = next;
    current.state["camera_revision"] = current.state.at("camera_revision").get<uint64_t>() + 1;
    if (changeRange) restart(current);
    else write(current);
}

WebWorkspace::Frame WebWorkspace::frame(const std::string& owner, const std::string& id, const QByteArray& after) {
    auto& current = view(owner, id);
    if (current.restarting) return {};
    if (current.process.state() == QProcess::NotRunning)
        throw WorkspaceError{503, "renderer_exited", "三维渲染进程已退出"};
    QFile file(current.directory + "/latest.frame");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) return {};
    const auto line = file.readLine(2048);
    auto stamp = Json::parse(line.begin(), line.end(), nullptr, false);
    if (!stamp.is_object() || stamp.value("view_id", "") != id || stamp.at("epoch") != current.state.at("epoch")) return {};
    const auto token = stamp.at("epoch").get<std::string>() + "/" + stamp.at("sequence").get<std::string>();
    if (token == after.toStdString()) return {};
    stamp["token"] = token;
    return {file.readAll(),std::move(stamp)};
}

void WebWorkspace::close(const std::string& owner) {
    const auto it = views_.find(owner);
    if (it == views_.end()) return;
    auto& view = *it->second;
    view.used = Clock::now() - 15s;
    view.restarting = false;
    view.process.closeWriteChannel();
}

void WebWorkspace::sweep() {
    const auto now = Clock::now();
    const auto viewCount = views_.size();
    for (auto it = views_.begin(); it != views_.end();) {
        auto& view = *it->second;
        if (view.restarting && now - view.restartRequested > 4s && view.process.state() != QProcess::NotRunning)
            view.process.kill();
        if (now - view.used < 15s) { ++it; continue; }
        view.restarting = false;
        if (view.process.state() != QProcess::NotRunning) {
            if (now - view.used > 20s) view.process.kill();
            else view.process.closeWriteChannel();
            ++it;
            continue;
        }
        viewWatcher_.removePath(view.directory);
        QDir(view.directory).removeRecursively();
        it = views_.erase(it);
    }
    if (viewCount != views_.size()) updateFrameIntervals();
}
}
