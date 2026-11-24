#include "SceneUsd.h"
#include <vsgraysim/products/SceneProduct.h>

#if VSGRAYSIM_HAVE_USD
#include <pxr/base/gf/quatd.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>

#include <filesystem>

namespace vsgraysim {
bool writeSceneUsd(const SceneProduct& scene, const std::string& path) {
    namespace usd = PXR_NS;
    const auto extension = std::filesystem::path(path).extension();
    if (extension != ".usd" && extension != ".usda" && extension != ".usdc") return false;
    const auto stage = usd::UsdStage::CreateInMemory();
    if (!stage) return false;
    const auto rootPath = usd::SdfPath("/Scene");
    const auto root = usd::UsdGeomXform::Define(stage, rootPath);
    if (!root) return false;
    stage->SetDefaultPrim(root.GetPrim());
    usd::UsdGeomSetStageMetersPerUnit(stage, 1.0);
    usd::UsdGeomSetStageUpAxis(stage, usd::UsdGeomTokens->y);
    root.GetPrim().SetDisplayName(scene.name);
    size_t index = 0;
    for (const auto& entity : scene.entities) {
        const auto primPath = rootPath.AppendChild(usd::TfToken("Entity_" + std::to_string(index++)));
        auto xform = usd::UsdGeomXform::Define(stage, primPath);
        if (!xform) return false;
        const auto& transform = entity.world_transform;
        const auto& p = transform.translation;
        const auto& q = transform.rotation;
        const auto& s = transform.scale;
        if (!xform.AddTranslateOp().Set(usd::GfVec3d(p.x, p.y, p.z)) ||
            !xform.AddOrientOp(usd::UsdGeomXformOp::PrecisionDouble).Set(usd::GfQuatd(q.w, q.x, q.y, q.z)) ||
            !xform.AddScaleOp(usd::UsdGeomXformOp::PrecisionDouble).Set(usd::GfVec3d(s.x, s.y, s.z))) return false;
        auto prim = xform.GetPrim();
        prim.SetDisplayName(entity.name);
        if (!prim.CreateAttribute(usd::TfToken("vsgraysim:entityId"), usd::SdfValueTypeNames->String).Set(entity.entity_id)) return false;
        if (const auto* prefab = scene.getPrefab(entity.prefab_ref); prefab && !prefab->gltf_path.empty()) {
            if (!prim.CreateAttribute(usd::TfToken("vsgraysim:sourceAsset"), usd::SdfValueTypeNames->Asset).Set(usd::SdfAssetPath(prefab->gltf_path))) return false;
        }
    }
    return stage->GetRootLayer()->Export(path);
}
}
#else
namespace vsgraysim {
bool writeSceneUsd(const SceneProduct&, const std::string&) { return false; }
}
#endif
