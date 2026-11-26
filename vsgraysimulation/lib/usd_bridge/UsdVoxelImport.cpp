#include "UsdVoxelImport.h"
#include "../../src/earth/AssimpVoxelImport.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <vsg/all.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace vsgraysim::earth {
namespace {
namespace usd = PXR_NS;
constexpr size_t maxTriangles = 2'000'000;

[[noreturn]] void invalid(const usd::UsdPrim& prim, const std::string& reason) {
    throw std::runtime_error("USD " + prim.GetPath().GetString() + ": " + reason);
}

template<class Array> struct Primvar {
    Array values;
    usd::TfToken interpolation;

    const typename Array::value_type& at(size_t face, size_t corner, size_t point, const usd::UsdPrim& prim) const {
        size_t index = 0;
        if (interpolation == usd::UsdGeomTokens->uniform) index = face;
        else if (interpolation == usd::UsdGeomTokens->faceVarying) index = corner;
        else if (interpolation == usd::UsdGeomTokens->vertex || interpolation == usd::UsdGeomTokens->varying) index = point;
        else if (interpolation != usd::UsdGeomTokens->constant) invalid(prim, "unsupported primvar interpolation");
        if (index >= values.size()) invalid(prim, "primvar sample is outside its array");
        return values[index];
    }
};

template<class Array>
Primvar<Array> readPrimvar(const usd::UsdGeomPrimvarsAPI& api, const usd::TfToken& name) {
    Primvar<Array> result;
    if (const auto primvar = api.FindPrimvarWithInheritance(name); primvar && primvar.HasValue()) {
        if (!primvar.ComputeFlattened(&result.values)) invalid(api.GetPrim(), "invalid primvar " + name.GetString());
        result.interpolation = primvar.GetInterpolation();
    }
    return result;
}

struct Material {
    vsg::vec4 diffuse{1, 1, 1, 1};
    vsg::vec3 emissive{};
    float roughness = .5f, metallic = 0.f;
    usd::TfToken uv{"st"};
    vsg::ref_ptr<vsg::Data> image, normal;
    bool bound = false;
};

vsg::ref_ptr<vsg::Data> readTexture(const usd::UsdShadeInput& input, bool srgb, usd::TfToken& uv,
                                  bool& haveUV, std::map<std::pair<std::string, bool>, vsg::ref_ptr<vsg::Data>>& images) {
    if (!input || !input.HasConnectedSource()) return {};
    usd::UsdShadeConnectableAPI source;
    usd::TfToken output;
    usd::UsdShadeAttributeType type;
    if (!input.GetConnectedSource(&source, &output, &type))
        invalid(input.GetPrim(), "cannot resolve texture connection");
    const auto texture = usd::UsdShadeShader(source.GetPrim());
    usd::TfToken shaderId;
    if (!texture || !texture.GetIdAttr().Get(&shaderId) || shaderId != usd::TfToken("UsdUVTexture") ||
        type != usd::UsdShadeAttributeType::Output || output != usd::TfToken("rgb"))
        invalid(input.GetPrim(), "expected a UsdUVTexture RGB connection for " + input.GetBaseName().GetString());

    usd::TfToken primvarName("st");
    const auto stInput = texture.GetInput(usd::TfToken("st"));
    if (stInput && stInput.HasConnectedSource()) {
        if (!stInput.GetConnectedSource(&source, &output, &type))
            invalid(texture.GetPrim(), "cannot resolve texture coordinates");
        const auto reader = usd::UsdShadeShader(source.GetPrim());
        if (!reader || !reader.GetIdAttr().Get(&shaderId) || shaderId != usd::TfToken("UsdPrimvarReader_float2"))
            invalid(texture.GetPrim(), "expected UsdPrimvarReader_float2 texture coordinates");
        const auto varname = reader.GetInput(usd::TfToken("varname"));
        std::string text;
        if (varname && varname.Get(&text)) primvarName = usd::TfToken(text);
        else if (varname && !varname.Get(&primvarName)) invalid(reader.GetPrim(), "cannot read texture primvar name");
    }
    if (primvarName.IsEmpty() || (haveUV && uv != primvarName)) invalid(texture.GetPrim(), "textures need one shared UV set");
    uv = primvarName;
    haveUV = true;

    usd::TfToken colorSpace;
    if (const auto cs = texture.GetInput(usd::TfToken("sourceColorSpace")); cs && cs.Get(&colorSpace)) {
        if (colorSpace == usd::TfToken("raw")) srgb = false;
        else if (colorSpace == usd::TfToken("sRGB")) srgb = true;
    }
    usd::SdfAssetPath asset;
    if (!texture.GetInput(usd::TfToken("file")).Get(&asset)) invalid(texture.GetPrim(), "missing texture file");
    auto path = asset.GetResolvedPath();
    if (path.empty() && std::filesystem::path(asset.GetAssetPath()).is_absolute()) path = asset.GetAssetPath();
    if (path.empty()) invalid(texture.GetPrim(), "cannot resolve texture " + asset.GetAssetPath());
    auto [entry, inserted] = images.try_emplace({path, srgb});
    if (inserted) entry->second = loadVoxelTexture(path, srgb);
    if (!entry->second) invalid(texture.GetPrim(), "cannot decode texture " + path);
    return entry->second;
}

vsg::dvec3 canonical(const usd::GfVec3d& v, bool zUp) {
    if (zUp) return {v[0], v[2], -v[1]};
    return {v[0], v[1], v[2]};
}

// Project a simple polygon to its dominant plane, then clip concave ears.
std::vector<std::array<size_t, 3>> triangulate(const std::vector<vsg::dvec3>& points, const usd::UsdPrim& prim) {
    if (points.size() == 3) return {{0, 1, 2}};
    vsg::dvec3 normal{};
    for (size_t i = 1; i + 1 < points.size(); ++i)
        normal += vsg::cross(points[i] - points.front(), points[i + 1] - points.front());
    size_t axis = 0;
    for (size_t j = 1; j < 3; ++j) if (std::abs(normal[j]) > std::abs(normal[axis])) axis = j;
    const auto turn = [&](size_t a, size_t b, size_t c) {
        return vsg::cross(points[b] - points[a], points[c] - points[a])[axis] * (normal[axis] < 0 ? -1 : 1);
    };
    const double epsilon = std::abs(normal[axis]) * 1e-12;
    if (!(epsilon > 0)) invalid(prim, "degenerate polygon");
    std::vector<size_t> live(points.size());
    std::iota(live.begin(), live.end(), 0);
    std::vector<std::array<size_t, 3>> triangles;
    while (live.size() > 3) {
        const auto count = live.size();
        bool found = false;
        for (size_t i = 0; i < count; ++i) {
            const size_t a = live[(i + count - 1) % count], b = live[i], c = live[(i + 1) % count];
            if (turn(a,b,c) <= epsilon) continue;
            const bool overlaps = std::any_of(live.begin(), live.end(), [&](size_t p) {
                return p != a && p != b && p != c &&
                    turn(a,b,p) >= -epsilon && turn(b,c,p) >= -epsilon && turn(c,a,p) >= -epsilon;
            });
            if (overlaps) continue;
            triangles.push_back({a,b,c});
            live.erase(live.begin() + i);
            found = true;
            break;
        }
        if (!found) invalid(prim, "cannot triangulate polygon; export a simple tessellated mesh");
    }
    triangles.push_back({live[0], live[1], live[2]});
    return triangles;
}

struct Batch {
    std::vector<vsg::dvec3> points;
    std::vector<vsg::vec3> normals;
    std::vector<vsg::vec2> uv;
    std::vector<vsg::vec4> colors;
};

template<class Array, class Items>
vsg::ref_ptr<Array> copyArray(const Items& source) {
    auto data = Array::create(source.size());
    std::copy(source.begin(), source.end(), data->begin());
    return data;
}

class Importer {
public:
    Importer(double units, bool zUp) : units_(units), zUp_(zUp) {}

    void append(const usd::UsdGeomMesh& mesh, vsg::Group& result) {
        const auto prim = mesh.GetPrim();
        const auto matrix = xforms_.GetLocalToWorldTransform(prim);
        const double determinant = matrix.GetDeterminant3();
        if (!std::isfinite(determinant) || determinant == 0) invalid(prim, "singular mesh transform");
        const auto normalMatrix = matrix.GetInverse().GetTranspose();
        usd::TfToken orientation;
        mesh.GetOrientationAttr().Get(&orientation);
        const bool reverse = (orientation == usd::UsdGeomTokens->leftHanded) != (determinant < 0);

        usd::VtVec3fArray points;
        usd::VtIntArray counts, indices, holes;
        if (!mesh.GetPointsAttr().Get(&points) || !mesh.GetFaceVertexCountsAttr().Get(&counts) ||
            !mesh.GetFaceVertexIndicesAttr().Get(&indices)) invalid(prim, "missing polygon topology");
        std::string reason;
        if (!usd::UsdGeomMesh::ValidateTopology(indices, counts, points.size(), &reason)) invalid(prim, reason);
        usd::TfToken scheme;
        if (mesh.GetSubdivisionSchemeAttr().HasAuthoredValue() &&
            mesh.GetSubdivisionSchemeAttr().Get(&scheme) && scheme != usd::UsdGeomTokens->none)
            invalid(prim, "tessellate authored subdivision before voxel import");

        std::vector<vsg::dvec3> worldPoints;
        worldPoints.reserve(points.size());
        for (const auto& p : points) {
            const auto value = canonical(matrix.Transform(usd::GfVec3d(p)), zUp_) * units_;
            if (!std::isfinite(vsg::length(value))) invalid(prim, "nonfinite mesh point");
            worldPoints.push_back(value);
        }
        usd::UsdGeomPrimvarsAPI primvars(prim);
        auto normals = readPrimvar<usd::VtVec3fArray>(primvars, usd::TfToken("normals"));
        if (normals.values.empty()) {
            mesh.GetNormalsAttr().Get(&normals.values);
            normals.interpolation = mesh.GetNormalsInterpolation();
        }
        const auto color = readPrimvar<usd::VtVec3fArray>(primvars, usd::TfToken("displayColor"));
        const auto binding = usd::UsdShadeMaterialBindingAPI(prim);
        const auto base = binding.ComputeBoundMaterial();
        std::vector<usd::SdfPath> bindings(counts.size(), base.GetPath());
        std::map<usd::SdfPath, Material> materials;
        materials.emplace(base.GetPath(), readMaterial(base));
        std::vector<bool> overridden(counts.size(), false), hidden(counts.size(), false);
        mesh.GetHoleIndicesAttr().Get(&holes);
        for (int id : holes) {
            if (id < 0 || size_t(id) >= counts.size()) invalid(prim, "invalid hole face index");
            hidden[id] = true;
        }
        for (const auto& subset : usd::UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets()) {
            const auto material = usd::UsdShadeMaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial();
            const auto path = material.GetPath();
            usd::VtIntArray faces;
            if (!subset.GetIndicesAttr().Get(&faces)) invalid(subset.GetPrim(), "missing material face indices");
            if (!materials.count(path)) materials.emplace(path, readMaterial(material));
            for (int face : faces) {
                if (face < 0 || size_t(face) >= counts.size() || overridden[face])
                    invalid(subset.GetPrim(), "invalid or overlapping material faces");
                bindings[face] = path;
                overridden[face] = true;
            }
        }

        std::map<usd::TfToken, Primvar<usd::VtVec2fArray>> uvSets;
        std::map<usd::SdfPath, Batch> batches;
        size_t corner = 0;
        for (size_t face = 0; face < counts.size(); ++face) {
            const size_t count = counts[face];
            const size_t firstCorner = corner;
            corner += count;
            if (hidden[face]) continue;
            if (count < 3 || count > 4096 || count - 2 > maxTriangles - triangleCount_)
                invalid(prim, "face or total triangle budget exceeded");
            const auto& path = bindings[face];
            const auto& material = materials.at(path);
            auto& batch = batches[path];
            auto [uvSet, inserted] = uvSets.try_emplace(material.uv);
            if (inserted) uvSet->second = readPrimvar<usd::VtVec2fArray>(primvars, material.uv);
            const auto& uv = uvSet->second;
            if (uv.values.empty() && (material.image || material.normal))
                invalid(prim, "missing texture coordinates " + material.uv.GetString());
            std::vector<vsg::dvec3> polygon;
            polygon.reserve(count);
            for (size_t i = 0; i < count; ++i) polygon.push_back(worldPoints[indices[firstCorner + i]]);
            for (auto triangle : triangulate(polygon, prim)) {
                if (reverse) std::swap(triangle[1], triangle[2]);
                for (size_t i : triangle) {
                    const size_t point = indices[firstCorner + i];
                    batch.points.push_back(polygon[i]);
                    if (!normals.values.empty()) {
                        auto n = canonical(normalMatrix.TransformDir(usd::GfVec3d(normals.at(face, firstCorner+i, point, prim))), zUp_);
                        if (!std::isfinite(vsg::length(n))) invalid(prim, "nonfinite mesh normal");
                        if (vsg::length(n) > 0) n = vsg::normalize(n);
                        batch.normals.emplace_back(n);
                    }
                    if (!uv.values.empty()) {
                        const auto& coord = uv.at(face, firstCorner+i, point, prim);
                        if (!std::isfinite(coord[0]) || !std::isfinite(coord[1])) invalid(prim, "nonfinite texture coordinates");
                        batch.uv.emplace_back(coord[0], coord[1]);
                    }
                    if (!material.bound && !color.values.empty()) {
                        const auto& rgb = color.at(face, firstCorner+i, point, prim);
                        batch.colors.emplace_back(rgb[0], rgb[1], rgb[2], 1);
                    }
                }
                ++triangleCount_;
            }
        }

        for (const auto& [path, batch] : batches) {
            const auto& material = materials.at(path);
            auto draw = vsg::VertexIndexDraw::create();
            draw->assignArrays({copyArray<vsg::dvec3Array>(batch.points)});
            // The voxel collector uses the full position array for unindexed draws.
            draw->indexCount = uint32_t(batch.points.size());
            draw->instanceCount = 1;
            if (!batch.normals.empty()) draw->setObject("voxel.import.normal", copyArray<vsg::vec3Array>(batch.normals));
            if (!batch.uv.empty()) draw->setObject("voxel.import.uv", copyArray<vsg::vec2Array>(batch.uv));
            if (!batch.colors.empty()) draw->setObject("voxel.import.color", copyArray<vsg::vec4Array>(batch.colors));
            if (material.image) draw->setObject("voxel.import.image", material.image);
            if (material.normal) draw->setObject("voxel.import.normal_image", material.normal);
            draw->setValue("voxel.import.diffuse", material.diffuse);
            draw->setValue("voxel.import.emissive", material.emissive);
            draw->setValue("voxel.import.roughness", material.roughness);
            draw->setValue("voxel.import.metallic", material.metallic);
            draw->setValue("voxel.import.appearance", material.bound || !batch.colors.empty());
            result.addChild(draw);
        }
    }

private:
    Material readMaterial(const usd::UsdShadeMaterial& material) {
        if (!material) return {};
        const auto shader = material.ComputeSurfaceSource();
        usd::TfToken id;
        if (!shader || !shader.GetIdAttr().Get(&id) || id != usd::TfToken("UsdPreviewSurface"))
            invalid(material.GetPrim(), "export a UsdPreviewSurface material for voxel import");
        Material result;
        result.bound = true;
        const auto diffuse = shader.GetInput(usd::TfToken("diffuseColor"));
        usd::GfVec3f rgb(.18f);
        if (diffuse) diffuse.Get(&rgb);
        const auto scalar = [&](const char* name, float& value) {
            const auto input = shader.GetInput(usd::TfToken(name));
            if (!input) return;
            if (input.HasConnectedSource() || !input.Get(&value) || !std::isfinite(value))
                invalid(shader.GetPrim(), std::string("expected a scalar value for ") + name);
        };
        float opacity = 1.f;
        scalar("opacity", opacity);
        result.diffuse = vsg::vec4(rgb[0],rgb[1],rgb[2],opacity);
        bool haveUV = false;
        result.image = readTexture(diffuse, true, result.uv, haveUV, images_);
        if (result.image) result.diffuse = vsg::vec4(1,1,1,opacity);
        result.normal = readTexture(shader.GetInput(usd::TfToken("normal")), false, result.uv, haveUV, images_);
        scalar("metallic", result.metallic);
        scalar("roughness", result.roughness);
        if (const auto input = shader.GetInput(usd::TfToken("emissiveColor"))) {
            if (input.HasConnectedSource() || !input.Get(&rgb))
                invalid(shader.GetPrim(), "expected a color value for emissiveColor");
            result.emissive = {rgb[0],rgb[1],rgb[2]};
        }
        return result;
    }

    double units_;
    bool zUp_;
    usd::UsdGeomXformCache xforms_;
    size_t triangleCount_ = 0;
    std::map<std::pair<std::string,bool>, vsg::ref_ptr<vsg::Data>> images_;
};
}

vsg::ref_ptr<vsg::Node> loadUsdVoxelMesh(const std::string& path) {
    const auto stage = usd::UsdStage::Open(path);
    if (!stage) throw std::runtime_error("cannot open USD model: " + path);
    const double units = usd::UsdGeomGetStageMetersPerUnit(stage);
    if (!std::isfinite(units) || units <= 0) throw std::runtime_error("invalid USD metersPerUnit: " + path);
    Importer importer(units, usd::UsdGeomGetStageUpAxis(stage) == usd::UsdGeomTokens->z);
    auto result = vsg::Group::create();
    for (const auto& prim : stage->Traverse(usd::UsdTraverseInstanceProxies())) {
        if (const usd::UsdGeomMesh mesh{prim}; mesh && mesh.ComputeVisibility() != usd::UsdGeomTokens->invisible &&
            mesh.ComputePurpose() != usd::UsdGeomTokens->guide && mesh.ComputePurpose() != usd::UsdGeomTokens->proxy)
            importer.append(mesh, *result);
    }
    if (result->children.empty()) throw std::runtime_error("USD model has no visible render mesh: " + path);
    return result;
}
}
