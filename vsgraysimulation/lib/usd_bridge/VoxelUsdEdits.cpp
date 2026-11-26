#include <vsgraysim/earth/VoxelUsdEdits.h>

#include <stdexcept>

#if VSGRAYSIM_HAVE_USD
#include "../../src/earth/VoxelSensorMaterials.h"
#include "../../src/earth/VoxelTerrain.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/xform.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <unistd.h>
#endif

namespace vsgraysim::earth {

bool voxelUsdEditsAvailable() noexcept {
#if VSGRAYSIM_HAVE_USD
    return true;
#else
    return false;
#endif
}

#if VSGRAYSIM_HAVE_USD
namespace {
using namespace pxr;
using json = nlohmann::json;
namespace fs = std::filesystem;

const SdfPath scenePath("/Scene");
const SdfPath targetsPath("/Scene/Targets");
constexpr auto idAttr = "vsgraysim:objectId";
constexpr auto chartAttr = "vsgraysim:sensor:cellChart";
constexpr auto versionAttr = "vsgraysim:editSchemaVersion";
constexpr auto modelAttr = "vsgraysim:radar:dielectric_model";
constexpr std::array fields{
    "temperature_k", "emissivity", "rho_tv", "rho_swir", "eps_mwir", "eps_lwir"
};
constexpr std::array thermalFields{
    "solar_gain_k", "inertia_h", "wet_cooling_k", "source_gain_k",
    "source_start_s", "source_run_s", "source_tau_s"
};
constexpr std::array radarFields{
    "dielectric_real", "dielectric_imag", "sigma0_db", "surface_rough", "rcs_scale"
};

bool editable(const VoxelSceneObject& object) {
    return (object.role == "target" || object.role == "structure") && object.parentId.empty();
}

void checkPath(const fs::path& path) {
    const auto ext = path.extension();
    if (ext != ".usda" && ext != ".usdc" && ext != ".usd")
        throw std::invalid_argument("USD edit layer requires .usda, .usdc or .usd");
}

TfToken primName(const std::string& id) {
    // Encode bytes so punctuation, Unicode and IDs beginning with digits remain distinct.
    std::string name = "id_";
    for (unsigned char c : id) {
        name += "0123456789abcdef"[c >> 4];
        name += "0123456789abcdef"[c & 15];
    }
    return TfToken(name);
}

template<class T>
void writeAttr(const UsdPrim& prim, const std::string& name, const SdfValueTypeName& type, const T& value) {
    if (!prim.CreateAttribute(TfToken(name), type, true, SdfVariabilityUniform).Set(value))
        throw std::runtime_error("cannot write USD field: " + name);
}

template<class T>
T readAttr(const UsdAttribute& attr) {
    T value{};
    if (!attr.Get(&value) || attr.GetNumTimeSamples() != 0 || attr.HasAuthoredConnections())
        throw std::invalid_argument("invalid static USD field: " + attr.GetPath().GetString());
    return value;
}

std::array<double, 4> anchorValues(const VoxelSceneSnapshot& scene) {
    return {scene.anchor.lat_deg, scene.anchor.lon_deg, scene.anchor.alt_m, scene.anchor.heading_deg};
}

constexpr std::array anchorFields{
    "vsgraysim:geo:latitude", "vsgraysim:geo:longitude",
    "vsgraysim:geo:ellipsoidHeight", "vsgraysim:geo:heading"
};

std::string sensorName(const char* key) { return std::string("vsgraysim:sensor:") + key; }
std::string radarName(const char* key) { return std::string("vsgraysim:radar:") + key; }
std::string thermalName(const char* key) { return std::string("vsgraysim:thermal:") + key; }

void writeMaterial(const UsdPrim& prim, const json& material) {
    for (const auto* key : fields) {
        if (material.contains(key))
            writeAttr(prim, sensorName(key), SdfValueTypeNames->Double, material.at(key).get<double>());
        else
            writeAttr(prim, sensorName(key), SdfValueTypeNames->Double, SdfValueBlock());
    }
    const auto thermal = material.find("thermal_response");
    for (const auto* key : thermalFields) {
        if (thermal != material.end() && thermal->contains(key))
            writeAttr(prim, thermalName(key), SdfValueTypeNames->Double, thermal->at(key).get<double>());
        else
            writeAttr(prim, thermalName(key), SdfValueTypeNames->Double, SdfValueBlock());
    }
    if (material.contains("radar")) {
        const auto& radar = material.at("radar");
        writeAttr(prim, modelAttr, SdfValueTypeNames->Int, radar.at("dielectric_model").get<int>());
        for (const auto* key : radarFields)
            writeAttr(prim, radarName(key), SdfValueTypeNames->Double, radar.at(key).get<double>());
    } else {
        writeAttr(prim, modelAttr, SdfValueTypeNames->Int, SdfValueBlock());
        for (const auto* key : radarFields)
            writeAttr(prim, radarName(key), SdfValueTypeNames->Double, SdfValueBlock());
    }
}

void readScalar(const UsdAttribute& attr, const char* key, json& material) {
    if (!attr) return;
    if (attr.GetResolveInfo().ValueIsBlocked() && attr.GetNumTimeSamples() == 0 && !attr.HasAuthoredConnections())
        material.erase(key);
    else
        material[key] = readAttr<double>(attr);
}

json readMaterial(const UsdPrim& prim, const json& base) {
    auto material = base;
    for (const auto* key : fields)
        readScalar(prim.GetAttribute(TfToken(sensorName(key))), key, material);
    auto thermal = material.value("thermal_response", json::object());
    for (const auto* key : thermalFields)
        readScalar(prim.GetAttribute(TfToken(thermalName(key))), key, thermal);
    if (thermal.empty()) material.erase("thermal_response");
    else material["thermal_response"] = std::move(thermal);
    auto radar = material.value("radar", json::object());
    const auto model = prim.GetAttribute(TfToken(modelAttr));
    if (model) {
        if (model.GetResolveInfo().ValueIsBlocked() && model.GetNumTimeSamples() == 0 && !model.HasAuthoredConnections())
            radar.erase("dielectric_model");
        else radar["dielectric_model"] = readAttr<int>(model);
    }
    for (const auto* key : radarFields)
        readScalar(prim.GetAttribute(TfToken(radarName(key))), key, radar);
    if (radar.empty()) material.erase("radar");
    else material["radar"] = std::move(radar);
    return normalizeVoxelMaterial(material);
}

void checkChartGrid(const json& chart, const VoxelSceneObject& object) {
    const auto origin = object.volume->gridOrigin();
    if (chart.at("origin_m") != json::array({origin.x, origin.y, origin.z}) ||
        chart.at("dimensions") != json(object.volume->dimensions()) ||
        chart.at("cell_size_m") != object.volume->resolution_m)
        throw std::invalid_argument("USD sensor chart does not match voxel grid for " + object.id);
}

void removeInherited(json& opinions, const json& base) {
    for (auto it = opinions.begin(); it != opinions.end();) {
        const auto inherited = base.find(it.key());
        if (inherited != base.end() && *inherited == *it) it = opinions.erase(it);
        else ++it;
    }
}

// Express each local material relative to the saved object material. This lets
// a stronger object-level opinion flow through cells with no local override.
json sparseChart(json chart, const json& base) {
    if (chart.is_null()) return chart;
    for (auto& entry : chart.at("palette")) {
        if (entry.contains("radar") && base.contains("radar")) {
            auto& radar = entry.at("radar");
            removeInherited(radar, base.at("radar"));
            if (radar.empty()) entry.erase("radar");
        }
        removeInherited(entry, base);
    }
    return chart;
}

json resolveChart(json chart, const json& base, const VoxelSceneObject& object) {
    if (chart.is_null()) return chart;
    checkChartGrid(chart, object);
    auto& palette = chart.at("palette");
    if (!palette.is_array() || palette.empty() || palette.size() > 256)
        throw std::invalid_argument("invalid USD sensor palette for " + object.id);
    for (auto& entry : palette) {
        if (!entry.is_object() || entry.contains("density_kg_m3") || entry.contains("color_rgba"))
            throw std::invalid_argument("invalid USD local spectral material for " + object.id);
        auto material = base;
        material.update(entry);
        if (entry.contains("radar") && base.contains("radar")) {
            auto radar = base.at("radar");
            radar.update(entry.at("radar"));
            material["radar"] = std::move(radar);
        }
        entry = normalizeVoxelMaterial(material);
        entry.erase("density_kg_m3");
        entry.erase("color_rgba");
    }
    return chart;
}

struct StagingDirectory {
    fs::path path;

    explicit StagingDirectory(const fs::path& destination) {
        auto temporary = (destination.parent_path() / ".voxel-usd-XXXXXX").string();
        if (!::mkdtemp(temporary.data())) throw std::runtime_error("cannot stage USD edit layer beside destination");
        path = temporary;
    }

    ~StagingDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
}
#endif

void saveVoxelUsdEdits(const std::string& path, const VoxelSceneSnapshot& scene) {
#if VSGRAYSIM_HAVE_USD
    checkPath(path);
    const auto stage = UsdStage::CreateInMemory("target-edits.usda");
    const auto root = UsdGeomScope::Define(stage, scenePath).GetPrim();
    stage->SetDefaultPrim(root);
    UsdGeomScope::Define(stage, targetsPath);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    writeAttr(root, versionAttr, SdfValueTypeNames->Int, 1);
    const auto anchor = anchorValues(scene);
    for (size_t i = 0; i < anchor.size(); ++i)
        writeAttr(root, anchorFields[i], SdfValueTypeNames->Double, anchor[i]);

    bool exported = false;
    for (const auto& [id, object] : scene.objects) {
        if (!editable(object)) continue;
        exported = true;
        const auto xform = UsdGeomXform::Define(stage, targetsPath.AppendChild(primName(id)));
        const auto prim = xform.GetPrim();
        writeAttr(prim, idAttr, SdfValueTypeNames->String, id);
        const auto pose = object.pose(scene.tick, scene.fixedStep);
        GfMatrix4d usd;
        // USD multiplies row vectors. VSG stores the equivalent columns.
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                usd[col][row] = pose[col][row];
        if (!xform.AddTransformOp().Set(usd)) throw std::runtime_error("cannot write USD pose for " + id);
        writeMaterial(prim, object.material);
        const auto chart = object.sensorMaterials ? object.sensorMaterials->encode() : json(nullptr);
        if (!chart.is_null()) checkChartGrid(chart, object);
        writeAttr(prim, chartAttr, SdfValueTypeNames->String, sparseChart(chart, object.material).dump());
    }
    if (!exported) throw std::invalid_argument("no authored voxel targets to export");
    const fs::path destination(path);
    const StagingDirectory staging(destination);
    const auto temporary = staging.path / destination.filename();
    if (!stage->GetRootLayer()->Export(temporary.string()))
        throw std::runtime_error("cannot export USD edit layer");
    fs::rename(temporary, destination);
#else
    throw std::runtime_error("built without OpenUSD support");
#endif
}

void applyVoxelUsdEdits(const std::string& path, VoxelScene& scene) {
#if VSGRAYSIM_HAVE_USD
    checkPath(path);
    if (!scene.paused()) throw std::logic_error("pause the voxel scene before applying a USD edit layer");
    const auto snapshot = scene.snapshot();
    const auto stage = UsdStage::Open(path, UsdStage::LoadNone);
    if (!stage || !stage->GetCompositionErrors().empty())
        throw std::invalid_argument("cannot compose USD edit layer: " + path);
    const auto root = stage->GetPrimAtPath(scenePath);
    const auto targets = stage->GetPrimAtPath(targetsPath);
    if (!root || !targets || readAttr<int>(root.GetAttribute(TfToken(versionAttr))) != 1 ||
        UsdGeomGetStageMetersPerUnit(stage) != 1.0 || UsdGeomGetStageUpAxis(stage) != UsdGeomTokens->y)
        throw std::invalid_argument("unsupported voxel USD edit layer schema, units or up axis");
    const auto anchor = anchorValues(*snapshot);
    for (size_t i = 0; i < anchor.size(); ++i) {
        const auto value = readAttr<double>(root.GetAttribute(TfToken(anchorFields[i])));
        if (value != anchor[i]) throw std::invalid_argument("USD edit layer uses a different geographic anchor");
    }

    auto commands = json::array();
    std::set<std::string> used;
    for (const auto& prim : targets.GetChildren()) {
        const auto id = readAttr<std::string>(prim.GetAttribute(TfToken(idAttr)));
        const auto it = snapshot->objects.find(id);
        const UsdGeomXformable xform(prim);
        if (!xform || !used.insert(id).second || it == snapshot->objects.end() || !editable(it->second))
            throw std::invalid_argument("unknown, duplicate or ineligible voxel USD target: " + id);
        const auto& object = it->second;
        json change = {{"kind", "target_properties"}, {"object", id}};
        const auto order = xform.GetXformOpOrderAttr();
        if (order.HasAuthoredValueOpinion()) {
            const auto opNames = readAttr<VtTokenArray>(order);
            bool reset = false;
            const auto ops = xform.GetOrderedXformOps(&reset);
            if (ops.size() + size_t(reset) != opNames.size())
                throw std::invalid_argument("invalid USD transform order for " + id);
            for (const auto& op : ops) readAttr<VtValue>(op.GetAttr());
            GfMatrix4d transform;
            if (!UsdGeomXformable::GetLocalTransformation(&transform, ops, UsdTimeCode::Default()))
                throw std::invalid_argument("invalid static USD transform for " + id);
            const auto previous = object.pose(snapshot->tick, snapshot->fixedStep);
            auto pose = json::array();
            bool changed = false;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    const double value = transform[col][row];
                    changed |= value != previous[col][row];
                    pose.push_back(value);
                }
            }
            if (changed) change["pose"] = std::move(pose);
        }

        auto material = readMaterial(prim, object.material);
        const bool materialChanged = material != object.material;
        const auto chartOpinion = prim.GetAttribute(TfToken(chartAttr));
        if (chartOpinion || materialChanged) {
            const auto previous = object.sensorMaterials ? object.sensorMaterials->encode() : json(nullptr);
            auto chart = chartOpinion ? json::parse(readAttr<std::string>(chartOpinion)) :
                sparseChart(previous, object.material);
            chart = resolveChart(std::move(chart), material, object);
            if (chart != previous) change["sensor_materials"] = std::move(chart);
        }
        if (materialChanged) change["material"] = std::move(material);
        if (change.size() > 2) commands.push_back(std::move(change));
    }
    if (used.empty()) throw std::invalid_argument("USD edit layer contains no active voxel targets");
    if (!commands.empty()) scene.edit(commands, snapshot->revision);
#else
    throw std::runtime_error("built without OpenUSD support");
#endif
}

}
