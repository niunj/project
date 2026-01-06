#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "SampleMath.h"

#define CPU_SIDE_COMPILATION 1
#include "shaders/gltfMaterial.h"

namespace tinygltf
{
    class Model;
    class Node;
    struct Mesh;
}

namespace Sample
{
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        glm::vec4 color;
    };

    struct Primitive
    {
        uint32_t firstIndex;
        uint32_t indexCount;

        uint32_t firstVertex;
        uint32_t vertexCount;

        int32_t materialIndex;
    };

    class Mesh
    {
    public:
        std::vector<Primitive> primitives;
    };

    struct Node
    {
    public:
        glm::mat4 fullTransform() const;
    public:
        Node* _parent = nullptr;
        std::vector<Node*> _children;
        Mesh* _mesh = nullptr;

        glm::mat4 _matrix;
        glm::vec3 _translation{};
        glm::quat _rotation{};
        glm::vec3 _scale{ 1.0f };
    };

    class GltfModel
    {
    public:
        enum FileLoadingFlags {
            None = 0x00000000,
            PreTransformVertices = 0x00000001,
            PreMultiplyVertexColors = 0x00000002,
            FlipY = 0x00000004,
            DontLoadImages = 0x00000008,
            ColorTexturesAreSrgb = 0x00000010
        };
    public:
        GltfModel();
        virtual ~GltfModel();
    public:
        void loadFromFile(const std::string& fileName, uint32_t fileLoadingFlags);

        const std::vector<Vertex>& vertexBuffer(void) const { return _vertexBuffer; }
        const std::vector<uint32_t>& indexBuffer(void) const { return _indexBuffer; }
        int numVertices() const;

        const std::vector<Node*>& linearNodes(void) const;

        void forEachPrimitive(const std::function<void(const Primitive&)>& func) const;
        int numPrimitives(void) const;
    protected:
        void loadMaterials(tinygltf::Model& gltfModel, bool srgbProcessing);
        void loadScenes(tinygltf::Model& gltfModel, uint32_t fileLoadingFlags);
        void loadNode(const tinygltf::Node& inputNode, tinygltf::Model& gltfModel, Node* parent, uint32_t fileLoadingFlags);
        void loadMesh(Node* node, const tinygltf::Mesh& srcMesh, tinygltf::Model& gltfModel, uint32_t fileLoadingFlags);

    protected:

        std::vector<Material> _materials;

        std::vector<Node*> _linearNodes;

        std::string _basePath;

        std::vector<uint32_t> _indexBuffer;
        std::vector<Vertex> _vertexBuffer;
    };
}