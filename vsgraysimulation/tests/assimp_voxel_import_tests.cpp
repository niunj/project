#include "../src/earth/VoxelModelVoxelizer.h"
#include "../src/earth/VoxelTerrain.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace vsgraysim::earth;

namespace {
struct Workspace {
    fs::path path = fs::temp_directory_path() / ("voxel-mesh-formats-" +
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
        {
            std::ofstream ply(directory.path / "cube.ply");
            ply << "ply\nformat ascii 1.0\nelement vertex 8\n"
                   "property float x\nproperty float y\nproperty float z\n"
                   "property uchar red\nproperty uchar green\nproperty uchar blue\n"
                   "element face 6\nproperty list uchar int vertex_indices\nend_header\n"
                   "0 0 0 255 0 0\n1 0 0 255 0 0\n1 1 0 255 0 0\n0 1 0 255 0 0\n"
                   "0 0 1 255 0 0\n1 0 1 255 0 0\n1 1 1 255 0 0\n0 1 1 255 0 0\n"
                   "4 0 3 2 1\n4 4 5 6 7\n4 0 1 5 4\n4 3 7 6 2\n4 0 4 7 3\n4 1 2 6 5\n";
        }
        VoxelModelConfig config;
        config.maxDimension = 16;
        const auto model = voxelizeFile((directory.path / "cube.ply").string(), config);
        require(model.terrain && model.terrain->occupiedCount() > 0, "Assimp PLY did not voxelize");
        require(bool(model.terrain->appearance()), "Assimp import lost surface data");
        {
            const std::array<float, 9> positions = {0,0,0, 2,0,0, 0,2,0};
            std::ofstream bytes(directory.path / "triangle.bin", std::ios::binary);
            bytes.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
        }
        {
            std::ofstream gltf(directory.path / "translated.gltf");
            gltf << R"({
              "asset":{"version":"2.0"},
              "scene":0,
              "scenes":[{"nodes":[0]}],
              "nodes":[{"translation":[10,20,30],"children":[1]},{"mesh":0}],
              "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
              "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0,1,0,1],"metallicFactor":0.5,"roughnessFactor":0.25}}],
              "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,2,0]}],
              "bufferViews":[{"buffer":0,"byteLength":36,"target":34962}],
              "buffers":[{"uri":"triangle.bin","byteLength":36}]
            })";
        }
        const auto gltf = voxelizeFile((directory.path / "translated.gltf").string(), config);
        require(gltf.terrain && gltf.occupiedCount > 0 && gltf.origin.x > 9 &&
                gltf.origin.y > 19 && gltf.origin.z > 29, "Assimp glTF node transform did not survive voxelization");
        require(bool(gltf.terrain->appearance()), "Assimp glTF lost the material");
        try {
            voxelizeFile((directory.path / "missing.ive").string(), config);
            throw std::runtime_error("legacy IVE loaded without a converted asset");
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()).find("Convert legacy OSG/IVE") != std::string::npos,
                    "unconverted IVE should report migration instructions");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
