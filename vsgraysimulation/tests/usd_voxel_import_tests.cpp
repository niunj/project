#include "../src/earth/VoxelModelVoxelizer.h"
#include "../src/earth/VoxelTerrain.h"
#include "../src/earth/VoxelDynamics.h"
#include "../src/earth/VoxelSensorMaterials.h"
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/VoxelUsdEdits.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace vsgraysim::earth;

namespace {
void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error(message);
}

struct Workspace {
    fs::path path = fs::temp_directory_path() / ("usd-voxel-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Workspace() { fs::create_directory(path); }
    ~Workspace() { std::error_code error; fs::remove_all(path, error); }
};

void authoredAsset(const fs::path& dir) {
    std::ofstream texture(dir / "paint.ppm", std::ios::binary);
    constexpr unsigned char pixels[] = {240,16,16, 16,24,240, 240,16,16, 16,24,240};
    texture << "P6\n2 2\n255\n";
    texture.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
    std::ofstream scene(dir / "cube.usda");
    scene << R"usda(#usda 1.0
(
  metersPerUnit = 0.01
  upAxis = "Z"
)
def Xform "Translated" {
  double3 xformOp:translate = (200, 0, 300)
  uniform token[] xformOpOrder = ["xformOp:translate"]
  def Mesh "Box" (prepend apiSchemas = ["MaterialBindingAPI"]) {
    uniform token subdivisionScheme = "none"
    point3f[] points = [(0,0,0), (100,0,0), (100,100,0), (0,100,0),
                       (0,0,100), (100,0,100), (100,100,100), (0,100,100)]
    int[] faceVertexCounts = [4,4,4,4,4,4]
    int[] faceVertexIndices = [0,3,2,1, 4,5,6,7, 0,1,5,4, 1,2,6,5, 2,3,7,6, 3,0,4,7]
    texCoord2f[] primvars:texcoord = [(0,0), (1,0), (1,1), (0,1)] (interpolation = "faceVarying")
    int[] primvars:texcoord:indices = [0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3]
    rel material:binding = </Paint>
  }
}
def Material "Paint" {
  token outputs:surface.connect = </Paint/PBR.outputs:surface>
  def Shader "PBR" {
    uniform token info:id = "UsdPreviewSurface"
    color3f inputs:diffuseColor.connect = </Paint/Image.outputs:rgb>
    float inputs:metallic = 0.25
    float inputs:roughness = 0.70
    token outputs:surface
  }
  def Shader "Image" {
    uniform token info:id = "UsdUVTexture"
    asset inputs:file = @paint.ppm@
    float2 inputs:st.connect = </Paint/UV.outputs:result>
    float3 outputs:rgb
  }
  def Shader "UV" {
    uniform token info:id = "UsdPrimvarReader_float2"
    token inputs:varname = "texcoord"
    float2 outputs:result
  }
}
)usda";
}

void writeLayer(const fs::path& directory, const char* body, const char* filename = "edited.usda") {
    std::ofstream layer(directory / filename);
    layer << R"usda(#usda 1.0
(
    metersPerUnit = 1
    upAxis = "Y"
    subLayers = [@authored.usda@]
)
over "Scene" {
    over "Targets" {
        over "id_7573642d746172676574" {
)usda" << body << "\n        }\n    }\n}\n";
}

void checkEdits(VoxelScene& scene, const fs::path& directory) {
    using json = nlohmann::json;
    constexpr auto id = "usd-target";
    scene.edit(json::array({{{"kind", "sensor_paint"}, {"object", id},
        {"region", {{"min_m", {2, 3, -1}}, {"max_m", {2.49, 4, 0}}}},
        {"material", {{"rho_tv", .06}, {"eps_mwir", .77}, {"radar", {{"sigma0_db", -18}}}}}}}),
        scene.snapshot()->revision);
    const auto initial = scene.snapshot();
    const auto initialObject = initial->objects.at(id);
    const auto layer = directory / "authored.usda";
    saveVoxelUsdEdits(layer.string(), *initial);
    saveVoxelUsdEdits((directory / "authored.usdc").string(), *initial);
    applyVoxelUsdEdits(layer.string(), scene);
    applyVoxelUsdEdits((directory / "authored.usdc").string(), scene);
    require(scene.snapshot() == initial, "applying an identical USD layer must be a no-op");

    writeLayer(directory, R"usda(
            matrix4d xformOp:transform = ((0,0,-1,0), (0,1,0,0), (1,0,0,0), (20,5,-3,1))
            custom uniform double vsgraysim:sensor:temperature_k = 301.5
            custom uniform double vsgraysim:sensor:rho_tv = 0.61
            custom uniform double vsgraysim:sensor:rho_swir = 0.29
            custom uniform double vsgraysim:sensor:eps_lwir = 0.89
            custom uniform double vsgraysim:thermal:inertia_h = 3.5
            custom uniform double vsgraysim:thermal:source_gain_k = 12
            custom uniform double vsgraysim:radar:sigma0_db = -11
    )usda");
    applyVoxelUsdEdits((directory / "edited.usda").string(), scene);
    const auto current = scene.snapshot();
    const auto& object = current->objects.at(id);
    const vsg::dvec3 transformed = object.basePose * vsg::dvec3(2, 3, -1);
    require(current->revision == initial->revision + 1 && transformed == vsg::dvec3(19,8,-5),
            "USD sublayer did not change the rigid pose in one transaction");
    require(object.volume == initialObject.volume && object.collision == initialObject.collision &&
            object.material.at("color_rgba") == initialObject.material.at("color_rgba") &&
            object.material.at("density_kg_m3") == initialObject.material.at("density_kg_m3"),
            "USD edit changed geometry, collision, display color or physics density");
    const auto& local = object.sensorMaterials->at({2.2,3.2,-.8});
    const auto& unpainted = object.sensorMaterials->at({2.8,3.2,-.8});
    require(object.material.at("temperature_k") == 301.5 && object.material.at("rho_tv") == .61 &&
            object.material.at("eps_lwir") == .89 && object.material.at("radar").at("sigma0_db") == -11 &&
            object.material.at("thermal_response").at("inertia_h") == 3.5 &&
            unpainted.at("thermal_response").at("source_gain_k") == 12 &&
            local.at("rho_tv") == .06 && local.at("eps_mwir") == .77 && local.at("radar").at("sigma0_db") == -18 &&
            unpainted.at("rho_tv") == .61 && unpainted.at("temperature_k") == 301.5,
            "USD layer lost target properties or local paint");
    const auto hit = scene.raycast(object.basePose * vsg::dvec3(2.25,5,-.75), {0,-1,0}, 4);
    require(hit && hit->object == id && std::abs(hit->distance - 1.) < 1e-8,
            "collision did not move with the USD transform");
    const auto sensor = makeVoxelSceneSensorData(current);
    require(sensor.instances.size() == 1 && sensor.instances.front().transform == object.basePose &&
            sensor.materials.size() == 2 && std::abs(sensor.materials.front().sigma0_db + 11) < 1e-6 &&
            std::abs(sensor.materials.back().sigma0_db + 18) < 1e-6 &&
            std::abs(sensor.materials.front().rho_tv - .61) < 1e-6 &&
            std::abs(sensor.materials.front().rho_swir - .29) < 1e-6 &&
            std::abs(sensor.materials.front().eps_lwir - .89) < 1e-6 &&
            sensor.materials.front().thermal_surface[2] == 3.5f &&
            sensor.materials.front().thermal_source[0] == 12.f &&
            std::abs(sensor.materials.back().eps_mwir - .77) < 1e-6,
            "sensor did not use edited pose or multiband material");
    const auto thermalLayer = directory / "thermal.usda";
    saveVoxelUsdEdits(thermalLayer.string(), *current);
    applyVoxelUsdEdits(thermalLayer.string(), scene);
    require(scene.snapshot() == current, "USD export omitted thermal material opinions");

    // Keep a real Bullet manifold check separate from the precision rays.
    const auto probePosition = object.basePose * vsg::dvec3(2.5, 4.04, -.5);
    auto probe = VoxelScene::load({{"format", "voxel-scene-v1"}, {"objects", json::array({
        {{"id", "probe"}, {"source", {{"type", "box"}, {"dimensions", {1,1,1}},
            {"origin_m", {-.05,-.05,-.05}}, {"cell_size_m", .10}}},
            {"position_m", {probePosition.x, probePosition.y, probePosition.z}},
            {"physics", {{"motion", "dynamic"}}}}
    })}});
    auto contactState = *current;
    contactState.objects.emplace("probe", probe->snapshot()->objects.at("probe"));
    VoxelDynamics dynamics;
    dynamics.detect(contactState);
    require(std::any_of(contactState.contacts.begin(), contactState.contacts.end(), [](const auto& contact) {
        return (contact.a == "usd-target" && contact.b == "probe") ||
               (contact.b == "usd-target" && contact.a == "probe");
    }), "Bullet contacts did not follow the edited USD target");

    // Authored scale and a valid material opinion must fail as one unit.
    writeLayer(directory, R"usda(
            matrix4d xformOp:transform = ((2,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1))
            custom uniform double vsgraysim:sensor:temperature_k = 350
    )usda", "invalid.usda");
    bool rejected = false;
    try { applyVoxelUsdEdits((directory / "invalid.usda").string(), scene); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && scene.snapshot() == current, "invalid USD transform partially changed live scene");

    auto mismatch = *current;
    mismatch.anchor.lon_deg += 1;
    saveVoxelUsdEdits((directory / "foreign.usda").string(), mismatch);
    rejected = false;
    try { applyVoxelUsdEdits((directory / "foreign.usda").string(), scene); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && scene.snapshot() == current, "foreign USD geographic anchor was accepted");

    writeLayer(directory, R"usda(
            custom uniform string vsgraysim:sensor:cellChart = "{\"origin_m\":[0,0,0],\"dimensions\":[8,8,8],\"cell_size_m\":0.125,\"palette\":[{}]}"
    )usda", "wrong-grid.usda");
    rejected = false;
    try { applyVoxelUsdEdits((directory / "wrong-grid.usda").string(), scene); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && scene.snapshot() == current, "USD cell chart mapped onto a different voxel grid");

    // Test an exported local cell chart restoring an earlier scene state.
    applyVoxelUsdEdits(layer.string(), scene);
    const auto& restored = scene.snapshot()->objects.at(id);
    require(restored.material == initialObject.material && restored.basePose == initialObject.basePose &&
            restored.sensorMaterials->encode() == initialObject.sensorMaterials->encode(),
            "applying a saved layer failed to restore explicit defaults or local paint");
    applyVoxelUsdEdits((directory / "edited.usda").string(), scene);
}

void checkPublication(VoxelScene& scene, const std::string& id) {
    const auto state = scene.snapshot();
    const auto& object = state->objects.at(id);
    const auto& surface = object.surface();
    const auto pose = object.pose(state->tick, state->fixedStep);
    const auto display = makeVoxelSceneDisplay(*state);
    const auto sensors = makeVoxelSceneSensorData(state);
    require(display.drawings.at(id).source.get() == &surface && display.placements.at(id)->matrix == pose &&
            display.contentRevision == state->contentRevision && sensors.snapshot == state,
            "display and sensors must consume the published snapshot");
    const auto sensor = std::find_if(sensors.instances.begin(), sensors.instances.end(),
                                     [&](const auto& item) { return item.id == id; });
    require(sensor != sensors.instances.end() && sensor->geometry && sensor->transform == pose &&
            sensor->geometry->indexCount == surface.indices().size() && !sensors.textures.layers.empty(),
            "published sensor geometry, transform or albedo differs from the visual surface");

    const auto origin = object.volume->gridOrigin();
    const auto n = object.volume->dimensions();
    const auto voxel = object.volume->resolution_m;
    const auto top = origin.y + n[1] * voxel + 1;
    const auto distance = n[1] * voxel + 2;
    unsigned matched = 0;
    double maximumError = 0;
    for (int i = 1; i <= 11; ++i) for (int j = 1; j <= 11; ++j) {
        const vsg::dvec3 local(origin.x + n[0] * voxel * i / 12, top,
                               origin.z + n[2] * voxel * j / 12);
        const auto world = pose * local;
        const auto direction = pose * (local + vsg::dvec3(0,-1,0)) - world;
        const auto canonical = scene.raycast(world, direction, distance);
        const auto collision = scene.rigidRaycast(world, direction, distance);
        const bool visible = canonical && canonical->object == id;
        const bool collidable = collision && collision->object == id;
        if (visible != collidable) {
            std::cerr << "Local ray origin (" << local.x << ", " << local.y << ", " << local.z
                      << "): canonical " << (canonical ? canonical->object : "miss")
                      << ", Bullet " << (collision ? collision->object : "miss") << '\n';
            throw std::runtime_error("USD target surface and Bullet rays hit different geometry");
        }
        if (!visible) continue;
        ++matched;
        maximumError = std::max(maximumError, std::abs(canonical->distance - collision->distance));
    }
    require(matched > 0 && maximumError < .001, "USD canonical and Bullet distances disagree");
    std::cout << "Target " << id << ", triangles " << surface.indices().size() / 3
              << ", matched rays " << matched << ", largest distance deviation " << maximumError << " m\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 3) {
            auto scene = VoxelScene::open(argv[1]);
            checkPublication(*scene, argv[2]);
            return 0;
        }
        Workspace dir;
        authoredAsset(dir.path);
        VoxelModelConfig settings;
        settings.voxelSize = .125;
        settings.margin = 0;
        settings.sourceSurface = true;
        const auto model = voxelizeFile((dir.path / "cube.usda").string(), settings);
        require(model.origin == vsg::dvec3(2, 3, -1), "USD stage units, up axis or Xform were ignored");
        require(model.cellSize == .125 && model.nx == 8 && model.ny == 8 && model.nz == 8, "USD changed voxel spacing");
        bool red = false, blue = false, material = false;
        for (size_t i = 0; i < model.terrain->imagery_rgba.size(); i += 4) {
            const auto& p = model.terrain->imagery_rgba;
            red |= p[i] > 200 && p[i + 2] < 60;
            blue |= p[i + 2] > 200 && p[i] < 60;
        }
        for (size_t i = 0; i < model.terrain->mr_rgba.size(); i += 4) {
            const auto& p = model.terrain->mr_rgba;
            material |= std::abs(int(p[i+1]) - 179) <= 1 && std::abs(int(p[i+2]) - 64) <= 1;
        }
        require(red && blue && material && model.terrain->appearance()->detailed(),
                "USD texture, indexed UVs, surface patches or material scalars were lost");

        const auto source = nlohmann::json{{"type","mesh"},{"path",(dir.path/"cube.usda").string()},
            {"voxel_size_m",.125},{"margin_m",0},{"surface_detail","source_clipped"}};
        const auto document = nlohmann::json{{"format","voxel-scene-v1"},{"objects",nlohmann::json::array({
            {{"id","usd-target"},{"source",source},{"physics",{{"motion","static"}}}}
        })}};
        auto prepared = VoxelScene::prepareImportAsync(document);
        auto scene = VoxelScene::load({{"format","voxel-scene-v1"},{"objects",nlohmann::json::array()}});
        scene->setPaused(true);
        scene->importPrepared(prepared.snapshot.get(), scene->snapshot()->revision);
        checkPublication(*scene,"usd-target");
        checkEdits(*scene, dir.path);
        checkPublication(*scene,"usd-target");

        const auto checkpoint = scene->checkpoint();
        const auto package = (dir.path / "published.vsimscene").string();
        VoxelScenePackage::save(package, {{"format","earth-scene-topic-v1"}}, checkpoint);
        fs::remove(dir.path / "cube.usda");
        fs::remove(dir.path / "paint.ppm");
        auto restored = VoxelScene::open(package);
        require(restored->checkpoint() == checkpoint, "USD package round-trip changed the published snapshot");
        checkPublication(*restored,"usd-target");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
