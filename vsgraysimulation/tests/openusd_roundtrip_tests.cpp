#include <vsgraysim/products/SceneProduct.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace usd = PXR_NS;
namespace {
struct Workspace {
    fs::path path = fs::temp_directory_path() / ("vsgraysim-openusd-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Workspace() { fs::create_directory(path); }
    ~Workspace() { std::error_code error; fs::remove_all(path, error); }
};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        Workspace directory;
        vsgraysim::SceneProduct scene;
        scene.name = "OpenUSD scene";
        vsgraysim::Entity entity;
        entity.entity_id = "asset-1";
        entity.name = "Test mesh";
        entity.world_transform.translation = {10.0, 20.0, 30.0};
        scene.entities.push_back(entity);
        for (const auto* extension : {".usda", ".usdc"}) {
            const auto path = (directory.path / (std::string("scene") + extension)).string();
            require(scene.exportUSD(path), "USD export failed");
            const auto stage = usd::UsdStage::Open(path);
            require(bool(stage), "USD cannot read the exported file");
            require(stage->GetDefaultPrim().GetPath().GetString() == "/Scene", "missing default prim");
            require(usd::UsdGeomGetStageMetersPerUnit(stage) == 1.0, "scene is not in metres");
            const usd::UsdGeomXform xform(stage->GetPrimAtPath(usd::SdfPath("/Scene/Entity_0")));
            require(bool(xform), "missing entity prim");
            usd::GfMatrix4d matrix;
            bool resets = false;
            require(xform.GetLocalTransformation(&matrix, &resets), "cannot read exported transform");
            require(matrix.ExtractTranslation() == usd::GfVec3d(10, 20, 30), "entity transform changed");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
