#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
using namespace vsgraysim;
using namespace vsgraysim::earth;
using json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
template <class F> double timed(F work) {
    const auto start = Clock::now();
    work();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
int main(int argc, char **argv) try {
    if (argc < 3)
        throw std::invalid_argument(
            "scene_package inspect PACKAGE | pack TOPIC OUTPUT | benchmark TOPIC OUTPUT");
    const std::string mode = argv[1];
    if (mode == "inspect") {
        if (argc != 3)
            throw std::invalid_argument("inspect takes one package");
        std::cout << VoxelScenePackage::inspect(argv[2]).dump(2) << '\n';
        return 0;
    }
    if (argc != 4 || (mode != "pack" && mode != "benchmark"))
        throw std::invalid_argument("pack/benchmark require TOPIC and OUTPUT.vsimscene");
    const auto project = VoxelSceneProject::open(argv[2]);
    std::unique_ptr<VoxelScene> scene;
    const auto prepareMs = timed([&] { scene = project.createScene(); });
    const auto state = scene->checkpoint();
    json manifest;
    const double saveMs =
        timed([&] { manifest = VoxelScenePackage::save(argv[3], project.document, state); });
    uint64_t compressed = fs::file_size(argv[3]), expanded = 0;
    for (const auto &asset : manifest["assets"].items()) {
        compressed += asset.value().at("stored_bytes").get<uint64_t>();
        expanded += asset.value().at("expanded_bytes").get<uint64_t>();
    }
    json report = {{"source", fs::absolute(argv[2]).string()},
                   {"package", fs::absolute(argv[3]).string()},
                   {"source_prepare_ms", prepareMs},
                   {"package_save_ms", saveMs},
                   {"package_referenced_bytes", compressed},
                   {"binary_expanded_bytes", expanded},
                   {"manifest_bytes", fs::file_size(argv[3])},
                   {"checkpoint_json_bytes", state.dump().size()},
                   {"scope", "CPU format/decode/preparation; excludes rendering, simulation stepping and "
                             "cold OS file-cache guarantees"}};
    if (mode == "benchmark") {
        const auto baseline = fs::path(argv[3]).string() + ".baseline.json";
        {
            std::ofstream out(baseline);
            out << state.dump();
            if (!out)
                throw std::runtime_error("cannot write baseline");
        }
        report["samples"] = json::array();
        for (int i = 0; i < 3; ++i) {
            json metadata, decoded, parsed;
            json sample;
            sample["manifest_inspect_ms"] = timed([&] { metadata = VoxelScenePackage::inspect(argv[3]); });
            sample["package_decode_ms"] = timed([&] { decoded = VoxelScenePackage::checkpoint(argv[3]); });
            sample["json_decode_ms"] = timed([&] {
                std::ifstream in(baseline);
                in >> parsed;
            });
            if (parsed != state || decoded != state)
                throw std::runtime_error("benchmark checkpoint equality failed");
            std::unique_ptr<VoxelScene> restored;
            sample["canonical_world_prepare_ms"] =
                timed([&] { restored = VoxelScene::load(decoded, argv[3]); });
            if (restored->checkpoint() != state)
                throw std::runtime_error("benchmark world equality failed");
            report["samples"].push_back(sample);
        }
        report["exact_checkpoint_equal"] = true;
    }
    std::cout << report.dump(2) << '\n';
    return 0;
} catch (const std::exception &e) {
    std::cerr << "scene_package: " << e.what() << '\n';
    return 1;
}
