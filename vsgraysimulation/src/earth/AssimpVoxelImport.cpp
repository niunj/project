#include "AssimpVoxelImport.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <vsg/all.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace vsgraysim::earth {
namespace {
constexpr unsigned maxPixels = 16'777'216;
constexpr size_t maxTriangles = 2'000'000;

bool validImage(unsigned width, unsigned height) {
    return width && height && uint64_t(width) * height <= maxPixels;
}

vsg::ref_ptr<vsg::Data> decodeTexture(const std::string& path, const aiTexture* embedded, VkFormat format) {
    const auto makeImage = [format](unsigned w, unsigned h) {
        return vsg::ubvec4Array2D::create(w, h, vsg::Data::Properties{format});
    };
    if (embedded && embedded->mHeight) {
        if (!validImage(embedded->mWidth, embedded->mHeight)) return {};
        auto data = makeImage(embedded->mWidth, embedded->mHeight);
        for (unsigned y = 0; y < embedded->mHeight; ++y) {
            for (unsigned x = 0; x < embedded->mWidth; ++x) {
                const auto& texel = embedded->pcData[y * embedded->mWidth + x];
                (*data)(x, embedded->mHeight - 1 - y) = vsg::ubvec4(texel.r, texel.g, texel.b, texel.a);
            }
        }
        return data;
    }
    const auto* bytes = embedded ? reinterpret_cast<const stbi_uc*>(embedded->pcData) : nullptr;
    const auto size = embedded ? embedded->mWidth : 0u;
    if (size > 128u * 1024u * 1024u) return {};
    int width = 0, height = 0, channels = 0;
    const int readable = bytes ? stbi_info_from_memory(bytes, size, &width, &height, &channels)
                              : stbi_info(path.c_str(), &width, &height, &channels);
    if (!readable || !validImage(width, height)) return {};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        bytes ? stbi_load_from_memory(bytes, size, &width, &height, &channels, STBI_rgb_alpha)
              : stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels || !validImage(width, height)) return {};
    auto data = makeImage(width, height);
    const auto* rows = reinterpret_cast<const vsg::ubvec4*>(pixels.get());
    for (int y = 0; y < height; ++y) {
        std::copy_n(rows + y * width, width, data->data() + (height - 1 - y) * width);
    }
    return data;
}

struct Material {
    vsg::vec4 diffuse{1, 1, 1, 1};
    vsg::vec3 emission{};
    float metal = 0;
    float roughness = .65f;
    vsg::ref_ptr<vsg::Data> albedo, normal, metallicRoughness;
};

class MeshReader {
public:
    MeshReader(const aiScene& scene, std::filesystem::path directory)
        : scene_(scene), directory_(std::move(directory)) {
        materials_.reserve(scene_.mNumMaterials);
        for (unsigned index = 0; index < scene_.mNumMaterials; ++index) {
            materials_.push_back(readMaterial(*scene_.mMaterials[index]));
        }
    }

    void append(const aiNode& node, const aiMatrix4x4& parent, vsg::Group& result, unsigned depth = 0) {
        if (depth > 256) throw std::runtime_error("Assimp scene hierarchy exceeds 256 levels");
        const auto world = parent * node.mTransformation;
        for (unsigned index = 0; index < node.mNumMeshes; ++index) {
            auto draw = mesh(*scene_.mMeshes[node.mMeshes[index]], world);
            if (draw) result.addChild(draw);
        }
        for (unsigned index = 0; index < node.mNumChildren; ++index) {
            append(*node.mChildren[index], world, result, depth + 1);
        }
    }

private:
    vsg::ref_ptr<vsg::Data> texture(const aiMaterial& material, aiTextureType type, VkFormat format) {
        aiString source;
        unsigned uvSet = 0;
        if (material.GetTexture(type, 0, &source, nullptr, &uvSet) != AI_SUCCESS || uvSet != 0) return {};
        const std::string name = source.C_Str();
        auto [entry, inserted] = images_.try_emplace({name, format});
        if (inserted) {
            std::string localName = name;
            std::replace(localName.begin(), localName.end(), '\\', '/');
            entry->second = decodeTexture((directory_ / localName).string(),
                                          scene_.GetEmbeddedTexture(name.c_str()), format);
        }
        return entry->second;
    }

    Material readMaterial(const aiMaterial& source) {
        Material output;
        aiColor4D color;
        if (source.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
            source.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            output.diffuse = vsg::vec4(color.r, color.g, color.b, color.a);
        }
        aiColor3D emission;
        if (source.Get(AI_MATKEY_COLOR_EMISSIVE, emission) == AI_SUCCESS) {
            output.emission = {emission.r, emission.g, emission.b};
        }
        source.Get(AI_MATKEY_METALLIC_FACTOR, output.metal);
        float shine = 0;
        if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, output.roughness) != AI_SUCCESS &&
            source.Get(AI_MATKEY_SHININESS, shine) == AI_SUCCESS) {
            output.roughness = std::clamp(std::sqrt(2.f / (std::max(shine, 0.f) + 2.f)), .08f, 1.f);
        }
        output.albedo = texture(source, aiTextureType_BASE_COLOR, VK_FORMAT_R8G8B8A8_SRGB);
        if (!output.albedo) output.albedo = texture(source, aiTextureType_DIFFUSE, VK_FORMAT_R8G8B8A8_SRGB);
        output.normal = texture(source, aiTextureType_NORMALS, VK_FORMAT_R8G8B8A8_UNORM);
        // Assimp's glTF importer records the packed green/blue texture in UNKNOWN.
        output.metallicRoughness = texture(source, aiTextureType_UNKNOWN, VK_FORMAT_R8G8B8A8_UNORM);
        return output;
    }

    vsg::ref_ptr<vsg::VertexIndexDraw> mesh(const aiMesh& mesh, const aiMatrix4x4& world) {
        if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) return {};
        if (mesh.mNumFaces > maxTriangles - triangles_ || mesh.mNumVertices > 3 * maxTriangles) {
            throw std::runtime_error("Assimp import exceeds the triangle or vertex budget");
        }
        triangles_ += mesh.mNumFaces;
        auto positions = vsg::dvec3Array::create(mesh.mNumVertices);
        auto normals = mesh.HasNormals() ? vsg::vec3Array::create(mesh.mNumVertices) : nullptr;
        auto uv = mesh.HasTextureCoords(0) ? vsg::vec2Array::create(mesh.mNumVertices) : nullptr;
        auto colors = mesh.HasVertexColors(0) ? vsg::vec4Array::create(mesh.mNumVertices) : nullptr;
        aiMatrix3x3 normalMatrix(world);
        const float determinant = normalMatrix.Determinant();
        if (!std::isfinite(determinant) || std::abs(determinant) < 1.e-20f) {
            throw std::runtime_error("Assimp mesh has a singular transform");
        }
        normalMatrix.Inverse().Transpose();
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            const auto p = world * mesh.mVertices[i];
            (*positions)[i] = {p.x, p.y, p.z};
            if (normals) {
                const auto n = (normalMatrix * mesh.mNormals[i]).NormalizeSafe();
                (*normals)[i] = {n.x, n.y, n.z};
            }
            if (uv) (*uv)[i] = {mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y};
            if (colors) {
                const auto& color = mesh.mColors[0][i];
                (*colors)[i] = vsg::vec4(color.r, color.g, color.b, color.a);
            }
        }
        auto indices = vsg::uintArray::create(3 * mesh.mNumFaces);
        const unsigned second = determinant < 0 ? 2 : 1;
        const unsigned third = 3 - second;
        for (unsigned face = 0; face < mesh.mNumFaces; ++face) {
            const auto& triangle = mesh.mFaces[face];
            if (triangle.mNumIndices != 3) throw std::runtime_error("Assimp produced a non-triangle face");
            (*indices)[3 * face] = triangle.mIndices[0];
            (*indices)[3 * face + 1] = triangle.mIndices[second];
            (*indices)[3 * face + 2] = triangle.mIndices[third];
        }
        const auto& material = materials_.at(mesh.mMaterialIndex);
        auto draw = vsg::VertexIndexDraw::create();
        draw->assignArrays({positions});
        draw->assignIndices(indices);
        draw->indexCount = indices->size();
        draw->instanceCount = 1;
        if (normals) draw->setObject("voxel.import.normal", normals);
        if (uv) draw->setObject("voxel.import.uv", uv);
        if (colors) draw->setObject("voxel.import.color", colors);
        if (uv && material.albedo) draw->setObject("voxel.import.image", material.albedo);
        if (uv && material.normal) draw->setObject("voxel.import.normal_image", material.normal);
        if (uv && material.metallicRoughness) draw->setObject("voxel.import.mr_image", material.metallicRoughness);
        draw->setValue("voxel.import.diffuse", material.diffuse);
        draw->setValue("voxel.import.emissive", material.emission);
        draw->setValue("voxel.import.metallic", material.metal);
        draw->setValue("voxel.import.roughness", material.roughness);
        draw->setValue("voxel.import.appearance", true);
        return draw;
    }

    const aiScene& scene_;
    std::filesystem::path directory_;
    std::vector<Material> materials_;
    std::map<std::pair<std::string, VkFormat>, vsg::ref_ptr<vsg::Data>> images_;
    size_t triangles_ = 0;
};
}

vsg::ref_ptr<vsg::Data> loadVoxelTexture(const std::string& path) {
    return decodeTexture(path, nullptr, VK_FORMAT_R8G8B8A8_SRGB);
}

vsg::ref_ptr<vsg::Node> loadAssimpVoxelMesh(const std::string& path) {
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_SortByPType | aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        throw std::runtime_error("Assimp could not load model " + path + ": " + importer.GetErrorString());
    }
    auto result = vsg::Group::create();
    MeshReader(*scene, std::filesystem::path(path).parent_path()).append(*scene->mRootNode, {}, *result);
    if (result->children.empty()) throw std::runtime_error("Assimp model has no triangle geometry: " + path);
    return result;
}
}
