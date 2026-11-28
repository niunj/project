#include "ModelLibrary.h"
#include "WebWorkspace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <QUuid>
#include <cmath>
#include <filesystem>

namespace vsgraysim::service {
namespace {
using Json = ModelLibrary::Json;

Json read(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return nullptr;
    const auto bytes = file.readAll();
    auto result = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
    return result.is_discarded() ? Json(nullptr) : result;
}

void write(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw WorkspaceError{503,"model_storage","无法写入模型库"};
}

QString text(const Json& json, const char* field) {
    return QString::fromStdString(json.at(field).get<std::string>());
}

bool modelId(const std::string& id) {
    return id.size() <= 64 && !id.empty() && id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == id.npos;
}

Json sensorProperties(const Json& input) {
    if (!input.is_object()) throw WorkspaceError{400,"model_sensor_properties","传感材质必须是对象"};
    for (const auto& [field, value] : input.items()) {
        double min = 0., max = 1.;
        if (field == "temperature_k") { min = 1.; max = 5000.; }
        else if (field == "rcs_scale") max = 1e8;
        else if (field != "rho_tv" && field != "eps_mwir" && field != "eps_lwir")
            throw WorkspaceError{400,"model_sensor_properties","未知的传感材质字段"};
        if (!value.is_number() || !std::isfinite(value.get<double>()) ||
            value.get<double>() < min || value.get<double>() > max)
            throw WorkspaceError{400,"model_sensor_properties","传感材质参数超出范围"};
    }
    return input;
}

void requireEmbeddedGlb(const QByteArray& bytes) {
    const auto fail = [] { throw WorkspaceError{400,"invalid_glb","请选择材质和纹理均包含在文件中的 GLB 2.0 模型"}; };
    if (bytes.size() < 28 || bytes.size() > 24 * 1024 * 1024 || !bytes.startsWith("glTF")) fail();
    const auto integer = [&](size_t offset) { return qFromLittleEndian<quint32>(bytes.constData() + offset); };
    if (integer(4) != 2 || integer(8) != quint32(bytes.size()) || integer(16) != 0x4e4f534a) fail();
    const auto length = integer(12);
    if (length == 0 || length > 1024 * 1024 || length + 28 > quint32(bytes.size())) fail();
    const auto begin = bytes.constData() + 20;
    const auto scene = Json::parse(begin, begin + length, nullptr, false);
    if (!scene.is_object() || !scene.contains("meshes") || !scene["meshes"].is_array() || scene["meshes"].empty()) fail();
    if (integer(24 + length) != 0x004e4942 || integer(20 + length) != quint32(bytes.size()) - length - 28) fail();
    for (const auto* field : {"buffers","images"}) {
        const auto resources = scene.value(field, Json::array());
        if (!resources.is_array()) fail();
        for (const auto& resource : resources) if (!resource.is_object() || resource.contains("uri")) fail();
    }
}
}

ModelLibrary::ModelLibrary(QString root) : root_(std::move(root)),
    directory_(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/model-library") {}

ModelLibrary::Json ModelLibrary::entry(const std::string& id) const {
    if (!modelId(id)) throw WorkspaceError{404,"model_not_found","找不到模型"};
    const auto builtins = read(QDir(root_).filePath("config/service/model-library.json"));
    for (const auto& item : builtins.value("models", Json::array()))
        if (item.at("id") == id) return item;
    if (id.rfind("import-",0)==0) {
        auto imported = read(QDir(directory_).filePath(QString::fromStdString(id) + "/model.json"));
        if (imported.is_object()) return imported;
    }
    throw WorkspaceError{404,"model_not_found","找不到模型"};
}

ModelLibrary::Json ModelLibrary::publicEntry(const Json& entry) const {
    auto result = entry;
    result["format"] = QFileInfo(text(entry,"asset")).suffix().toUpper().toStdString();
    result["has_voxel"] = entry.contains("voxel_scene");
    result["rotation_deg"] = {0.,0.,0.};
    result["revision"] = 0;
    result["sensor_properties"] = Json::object();
    for (const auto* field : {"asset","up_axis","voxel_scene","voxel_object"}) result.erase(field);
    const auto profile = read(QDir(directory_).filePath("profiles/" + text(entry,"id") + ".json"));
    if (profile.is_object()) result.update(profile);
    return result;
}

ModelLibrary::Json ModelLibrary::list() const {
    const auto config = read(QDir(root_).filePath("config/service/model-library.json"));
    auto results = Json::array();
    if (config.is_object()) for (const auto& item : config.at("models")) results.push_back(publicEntry(item));
    for (const auto& dir : QDir(directory_).entryList({"import-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto imported = read(QDir(directory_).filePath(dir + "/model.json"));
        if (imported.is_object()) results.push_back(publicEntry(imported));
    }
    return results;
}

ModelLibrary::Json ModelLibrary::get(const std::string& id) const { return publicEntry(entry(id)); }

ModelLibrary::Json ModelLibrary::update(const std::string& id, const Json& request) {
    auto current = get(id);
    if (request.at("expected_revision") != current.at("revision"))
        throw WorkspaceError{409,"model_conflict","模型档案有新版本，请重新打开"};
    const auto name = text(request,"name").trimmed();
    const auto rotation = request.at("rotation_deg");
    if (name.isEmpty() || name.size() > 64 || name.contains(QRegularExpression("[\\x00-\\x1f]")) ||
        !rotation.is_array() || rotation.size() != 3)
        throw WorkspaceError{400,"model_profile","请输入有效的名称和姿态"};
    for (const auto& angle : rotation)
        if (!angle.is_number() || !std::isfinite(angle.get<double>()) || std::abs(angle.get<double>()) > 180)
            throw WorkspaceError{400,"model_profile","角度范围为 −180° 至 180°"};
    Json profile = {{"name",name.toStdString()},{"rotation_deg",rotation},
                    {"revision",current.at("revision").get<uint64_t>()+1},
                    {"sensor_properties",sensorProperties(request.value("sensor_properties",current.at("sensor_properties")))}};
    const auto folder = QDir(directory_).filePath("profiles");
    if (!QDir().mkpath(folder)) throw WorkspaceError{503,"model_storage","无法创建模型档案目录"};
    write(QDir(folder).filePath(QString::fromStdString(id)+".json"), QByteArray::fromStdString(profile.dump()));
    current.update(profile);
    return current;
}

std::string ModelLibrary::matchingModel(const QString& scene, const Json& object) const {
    const auto builtins = read(QDir(root_).filePath("config/service/model-library.json"));
    if (!builtins.is_object()) return {};
    for (const auto& item : builtins.at("models")) {
        if (!item.contains("voxel_scene") || item.at("voxel_object") != object) continue;
        if (QFileInfo(QDir(root_).filePath(text(item,"voxel_scene"))).canonicalFilePath() == scene)
            return item.at("id").get<std::string>();
    }
    return {};
}

ModelLibrary::Json ModelLibrary::sensorBinding(const std::string& id, const Json& expectedRevision) const {
    const auto profile = get(id);
    if (profile.at("revision") != expectedRevision)
        throw WorkspaceError{409,"model_conflict","模型档案有新版本，请重试"};
    auto material = sensorProperties(profile.at("sensor_properties"));
    if (material.empty()) throw WorkspaceError{409,"model_sensor_empty","请先在模型工作台填写传感材质"};
    if (material.contains("rcs_scale")) {
        material["radar"] = {{"rcs_scale", material.at("rcs_scale")}};
        material.erase("rcs_scale");
    }
    return {{"model_id",id},{"revision",expectedRevision},{"material",material}};
}

ModelLibrary::Json ModelLibrary::importGlb(const QByteArray& data) {
    requireEmbeddedGlb(data);
    QDir store(directory_);
    if (store.entryList({"import-*"},QDir::Dirs | QDir::NoDotAndDotDot).size() >= 32)
        throw WorkspaceError{409,"model_limit","本机导入模型已达 32 个"};
    const auto id = QString("import-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto folder = store.filePath(id);
    if (!QDir().mkpath(folder)) throw WorkspaceError{503,"model_storage","无法创建导入目录"};
    try {
        const auto path = QDir(folder).filePath("asset.glb");
        write(path,data);
        Json model = {{"id",id.toStdString()},{"name","导入模型"},{"category","本机导入"},
            {"description","自包含 GLB · 原模型预览"},{"asset",path.toStdString()},{"up_axis","y"}};
        write(QDir(folder).filePath("model.json"), QByteArray::fromStdString(model.dump()));
        return publicEntry(model);
    } catch (...) {
        QDir(folder).removeRecursively();
        throw;
    }
}

ModelLibrary::Json ModelLibrary::renderSpec(const std::string& id, const std::string& representation) const {
    const auto asset = entry(id);
    const auto profile = publicEntry(asset);
    Json spec = {{"representation",representation},{"rotation_deg",profile.at("rotation_deg")},
                 {"up_axis",asset.at("up_axis")}};
    if (representation == "original") {
        const auto path = QFileInfo(QDir(root_).filePath(text(asset,"asset"))).canonicalFilePath();
        if (path.isEmpty()) throw WorkspaceError{404,"model_missing","模型原件不可用"};
        spec["path"] = path.toStdString();
        return spec;
    }
    if (representation != "voxel" || !asset.contains("voxel_scene"))
        throw WorkspaceError{400,"model_representation","该模型没有已登记的体素版本"};
    const auto scenePath = QDir(root_).filePath(text(asset,"voxel_scene"));
    const auto definition = read(scenePath);
    for (auto object : definition.at("objects")) {
        if (object.at("id") != asset.at("voxel_object")) continue;
        auto& source = object.at("source");
        source["path"] = QFileInfo(QFileInfo(scenePath).dir().filePath(text(source,"path"))).canonicalFilePath().toStdString();
        object["id"]="preview";
        object["position_m"]={0.,0.,0.};
        object["velocity_mps"]={0.,0.,0.};
        object["physics"]={{"motion","static"}};
        spec["definition"]={{"format","voxel-scene-v1"},{"geo_anchor",definition.at("geo_anchor")},
                            {"objects",Json::array({object})}};
        return spec;
    }
    throw WorkspaceError{404,"model_missing","体素版本不可用"};
}
}
