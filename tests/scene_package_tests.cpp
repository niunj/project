#include "../src/earth/VoxelSensorMaterials.h"
#include "../src/earth/VoxelTerrain.h"
#include "voxel_earth_fixture.h"
#include <iostream>
#include <unistd.h>
#include <vsg/core/Array.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/earth/VoxelScene.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/workspace/Workspace.h>
using namespace vsgraysim;
using namespace vsgraysim::earth;
using json = nlohmann::json;
namespace fs = std::filesystem;
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "invalid operation accepted");
}
struct Directory {
    std::string path = "/tmp/vsg_scene_package_XXXXXX";
    Directory() { require(mkdtemp(path.data()), "mkdtemp"); }
    ~Directory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
int main() try {
    Directory dir;
    const fs::path root = dir.path;
    require(VoxelScenePackage::sha256({}) ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty SHA256");
    require(VoxelScenePackage::sha256({'a', 'b', 'c'}) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "abc SHA256");
    require(VoxelScenePackage::sha256(std::vector<uint8_t>(1000000, 'a')) ==
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "large SHA256");
    ::setenv("VSGRAYSIM_PORTABLE_SHA256", "1", 1);
    require(VoxelScenePackage::sha256(std::vector<uint8_t>(1000000, 'a')) ==
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "portable SHA256");
    ::unsetenv("VSGRAYSIM_PORTABLE_SHA256");
    const auto source = earth_fixture::create(root);
    auto scene = VoxelScene::open((root / "earth.vscene.json").string());
    auto revision = [&] { return scene->snapshot()->revision; };
    const auto initial = scene->checkpoint();
    {
        auto damaged = initial;
        const auto originalVolume = scene->snapshot()->objects.at("earth").volume;
        auto occupancy = originalVolume->occupancy();
        const auto dims = originalVolume->dimensions();
        const size_t hole = (size_t(4) * dims[1] + 2) * dims[0] + 4;
        occupancy.at(hole) = false;
        damaged["objects"][0]["volume"] =
            VoxelTerrain::fromOccupancy(originalVolume->gridOrigin(), 1, dims[0], dims[1], dims[2], occupancy,
                                        false)
                ->encodeVolume();
        auto edited = VoxelScene::load(damaged);
        json safeBrush = {{"operation", "raise"}, {"center_m", {0, 0, 0}}, {"radius_m", 2}, {"amount_m", .1}};
        edited->edit(json::array({{{"kind", "terrain_brush"}, {"object", "earth"}, {"brush", safeBrush}}}),
                     edited->snapshot()->revision);
        require(!edited->snapshot()->objects.at("earth").volume->occupancy().at(hole),
                "terrain brush restored an existing damage hole");
    }
    {
        auto bottomEdit = VoxelScene::load(initial);
        const auto before = bottomEdit->checkpoint();
        rejects([&] {
            bottomEdit->edit(json::array({{{"kind", "terrain_brush"},
                                           {"object", "earth"},
                                           {"brush",
                                            {{"operation", "flatten"},
                                             {"center_m", {0, 0, 0}},
                                             {"radius_m", 1000000},
                                             {"height_m", -3}}}}}),
                             bottomEdit->snapshot()->revision);
        });
        require(before == bottomEdit->checkpoint(), "terrain bottom constraint rejected after publication");
        auto bonded = initial;
        const auto dims = scene->snapshot()->objects.at("earth").volume->dimensions();
        const uint64_t cell = (uint64_t(4) * dims[1] + 4) * dims[0] + 4;
        bonded["objects"][0]["broken_bonds"] = json::array({cell * 3});
        auto cut = VoxelScene::load(bonded);
        cut->edit(
            json::array(
                {{{"kind", "terrain_brush"},
                  {"object", "earth"},
                  {"brush",
                   {{"operation", "lower"}, {"center_m", {0, 0, 0}}, {"radius_m", 3}, {"amount_m", 1}}}}}),
            cut->snapshot()->revision);
        require(cut->snapshot()->objects.at("earth").brokenBonds.empty(),
                "terrain edit retained a bond to removed voxels");
        require(VoxelScene::load(cut->checkpoint())->checkpoint() == cut->checkpoint(),
                "terrain edit cannot reopen after pruning topology");
    }
    const auto original = scene->raycast({0, 10, 0}, {0, -1, 0}, 20);
    require(bool(original), "initial terrain ray missing");
    json brush = {{"operation", "raise"}, {"center_m", {0, 0, 0}}, {"radius_m", 2}, {"amount_m", .2}};
    scene->edit(json::array({{{"kind", "terrain_brush"}, {"object", "earth"}, {"brush", brush}}}),
                revision());
    auto hit = scene->raycast({0, 10, 0}, {0, -1, 0}, 20),
         rigid = scene->rigidRaycast({0, 10, 0}, {0, -1, 0}, 20);
    require(hit && rigid && std::abs(original->distance - hit->distance - .2) < .003 &&
                std::abs(hit->distance - rigid->distance) < .002,
            "sculpt display/ray/collision mismatch");
    require(scene->snapshot()->objects.at("earth").volume->resolution_m == 1, "sculpt coarsened occupancy");
    const auto sensor = makeVoxelSceneSensorData(scene->snapshot());
    require(sensor.instances.size() == 1 && sensor.instances[0].geometry, "sculpt missing sensor geometry");
    const auto display = makeVoxelSceneDisplay(*scene->snapshot());
    auto draw = display.placements.at("earth")
                    ->children[0]
                    .cast<vsg::StateGroup>()
                    ->children[0]
                    .cast<vsg::VertexIndexDraw>();
    auto positions = draw->arrays[0]->data.cast<vsg::vec3Array>(),
         measured = sensor.instances[0].geometry->arrays[0]->data.cast<vsg::vec3Array>();
    require(positions->size() == measured->size() &&
                draw->indexCount == sensor.instances[0].geometry->indexCount,
            "sculpt display/sensor topology mismatch");
    for (size_t i = 0; i < positions->size(); ++i)
        require((*positions)[i] == (*measured)[i], "sculpt display/sensor geometry mismatch");
    auto before = scene->checkpoint();
    brush["amount_m"] = 100;
    rejects([&] {
        scene->edit(json::array({{{"kind", "terrain_brush"}, {"object", "earth"}, {"brush", brush}}}),
                    revision());
    });
    require(scene->checkpoint() == before, "failed brush mutated world");
    brush["amount_m"] = .1;
    scene->setPreparePublication([](auto) { throw std::runtime_error("consumer rejection"); });
    rejects([&] {
        scene->edit(json::array({{{"kind", "terrain_brush"}, {"object", "earth"}, {"brush", brush}}}),
                    revision());
    });
    require(scene->checkpoint() == before, "failed consumer published sculpt");
    scene->setPreparePublication({});
    for (const auto *operation : {"lower", "flatten", "smooth"}) {
        brush["operation"] = operation;
        brush["height_m"] = 1.35;
        scene->edit(json::array({{{"kind", "terrain_brush"}, {"object", "earth"}, {"brush", brush}}}),
                    revision());
    }
    const json region = {{"min_m", {-2, -3, -2}}, {"max_m", {2, 3, 2}}};
    scene->edit(json::array({{{"kind", "sensor_paint"},
                              {"object", "earth"},
                              {"region", region},
                              {"material",
                               {{"temperature_k", 345}, {"rho_tv", .7}, {"radar", {{"sigma0_db", -4}}}}}}}),
                revision());
    auto chart = scene->snapshot()->objects.at("earth").sensorMaterials;
    require(chart->at({.5, .5, .5})["temperature_k"] == 345 &&
                chart->at({3.5, .5, 3.5})["temperature_k"] == 314,
            "spatial properties leaked outside ROI");
    scene->edit(json::array({{{"kind", "sensor_paint"},
                              {"object", "earth"},
                              {"region", region},
                              {"material", {{"temperature_k", 360}}}}}),
                revision());
    chart = scene->snapshot()->objects.at("earth").sensorMaterials;
    require(chart->at({.5, .5, .5})["rho_tv"] == .7 && chart->at({.5, .5, .5})["radar"]["sigma0_db"] == -4 &&
                chart->at({.5, .5, .5})["temperature_k"] == 360,
            "repainting lost existing properties");
    before = scene->checkpoint();
    rejects([&] {
        scene->edit(json::array({{{"kind", "sensor_paint"},
                                  {"object", "earth"},
                                  {"region", region},
                                  {"material", {{"rho_tv", 2}}}}}),
                    revision());
    });
    require(scene->checkpoint() == before, "invalid paint changed state");
    auto added = source;
    added["objects"] = json::array(
        {{{"id", "imported"}, {"source", {{"type", "box"}, {"dimensions", {2, 2, 2}}, {"cell_size_m", 1}}}}});
    auto preparation = VoxelScene::prepareImportAsync(added);
    auto preparedSnapshot = preparation.snapshot.get();
    require(preparedSnapshot->objects.count("imported"), "isolated async asset preparation lost object");
    auto cancelled = VoxelScene::prepareImportAsync(added);
    cancelled.cancellation->store(true);
    rejects([&] { cancelled.snapshot.get(); });
    auto prepared = VoxelScene::load(added);
    scene->importPrepared(prepared->snapshot(), revision());
    before = scene->checkpoint();
    rejects([&] { scene->importPrepared(prepared->snapshot(), revision()); });
    require(scene->checkpoint() == before, "duplicate import changed state");
    scene->schedule(
        {{"kind", "damage"}, {"object", "earth"}, {"tick", 2}, {"point_m", {0, 0, 0}}, {"radius_m", .5}});
    const auto expected = scene->checkpoint();
    auto equipment = json::parse(std::ifstream("config/equipment/taipei-multispectral.vequipment.json"));
    json topic = {{"format", "earth-scene-topic-v1"},
                  {"name", "包验收"},
                  {"scene", (root / "earth.vscene.json").string()},
                  {"earth_map", fs::absolute("config/earth/default.map.json").string()},
                  {"clouds", {{"cover", 0}}},
                  {"equipment", equipment}};
    const auto file = root / "authored.vsimscene";
    const auto manifest = VoxelScenePackage::save(file, topic, expected);
    require(VoxelScenePackage::checkpoint(file) == expected, "package changed exact canonical checkpoint");
    require(VoxelScene::open(file)->checkpoint() == expected, "runtime package restore differs");
    std::map<fs::path, fs::file_time_type> timestamps;
    for (const auto &asset : manifest["assets"].items()) {
        auto path = root / asset.value()["uri"].get<std::string>();
        timestamps[path] = fs::last_write_time(path);
    }
    VoxelScenePackage::save(file, topic, expected);
    for (const auto &[path, time] : timestamps)
        require(fs::last_write_time(path) == time, "unchanged immutable chunk rewritten");
    fs::create_directory(root / "moved");
    fs::copy(file, root / "moved" / file.filename());
    fs::copy(root / "authored.vsimscene.data", root / "moved/authored.vsimscene.data",
             fs::copy_options::recursive);
    fs::remove(root / "earth.vscene.json");
    auto project = VoxelSceneProject::open((root / "moved" / file.filename()).string());
    require(project.createScene()->checkpoint() == expected,
            "relocated package depends on original source geometry");
    require(project.equipmentConfig().equipment.size() == 4, "package lost equipment");
    Workspace workspace((root / "workspace").string());
    workspace.ensureDirs();
    VoxelScenePackage::save(fs::path(workspace.sceneDir()) / "saved.vsimscene", topic, expected);
    earth_fixture::write(fs::path(workspace.sceneDir()) / "broken.vsimscene",
                         {{"format", "vsgraysim-scene-package-v1"}, {"version", 1}});
    const auto entries = workspace.listScenes();
    require(entries.size() == 1 && entries[0].meta["objects"].size() == 2 &&
                !entries[0].meta.contains("checkpoint"),
            "startup index did not read lightweight package metadata");
    auto damaged = manifest;
    const auto digest = damaged["assets"].begin().key();
    damaged["assets"][digest]["uri"] = "../escape";
    earth_fixture::write(root / "bad.vsimscene", damaged);
    rejects([&] { VoxelScenePackage::checkpoint((root / "bad.vsimscene").string()); });
    damaged = manifest;
    damaged["version"] = 2;
    earth_fixture::write(root / "bad.vsimscene", damaged);
    rejects([&] { VoxelScenePackage::inspect((root / "bad.vsimscene").string()); });
    const auto asset = root / manifest["assets"][digest]["uri"].get<std::string>();
    std::ofstream(asset, std::ios::binary | std::ios::trunc) << "corrupt";
    rejects([&] { VoxelScenePackage::checkpoint(file); });
    rejects([&] { VoxelScenePackage::save(file, topic, expected); });
    require(json::parse(std::ifstream(file)) == manifest, "failed save replaced committed manifest");
    std::cout << "PASS scene package SHA256, exact state, immutable assets, relocation, index, integrity, "
                 "sculpt, spatial materials, prepared imports and rollback\n";
} catch (const std::exception &e) {
    std::cerr << "scene_package_tests: " << e.what() << '\n';
    return 1;
}
