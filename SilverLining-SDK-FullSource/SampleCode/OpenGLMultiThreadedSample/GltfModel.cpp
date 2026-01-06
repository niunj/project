#include "GltfModel.h"
#include "Buffer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tinygltf/tiny_gltf.h"

namespace Sample
{
GltfModel::GltfModel()
{

}

GltfModel::~GltfModel()
{

}

bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
{
    // KTX files will be handled by our own code
    if (image->uri.find_last_of(".") != std::string::npos) {
        if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
            return true;
        }
    }

    return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
}

bool loadImageDataFuncEmpty(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
{
    // This function will be used for samples that don't require images to be loaded
    return true;
}

void GltfModel::loadMaterials(tinygltf::Model& glTfModel, bool srgbProcessing)
{
    const size_t totalImagesInModel = glTfModel.images.size() + 1; // +1 for default
    _materials.reserve(glTfModel.materials.size() + 1); // 1 for the default
    for (const auto& glTfMaterial : glTfModel.materials) {
        Material currentMaterial;

        // We only read the most basic properties required for our sample
        // Get the base color factor
        auto baseColorFactorIter = glTfMaterial.values.find("baseColorFactor");
        if (baseColorFactorIter != glTfMaterial.values.end()) {
            currentMaterial.baseColorFactor = glm::make_vec4(baseColorFactorIter->second.ColorFactor().data());
        }

        // Get base color texture index
        auto baseColorTextureIter = glTfMaterial.values.find("baseColorTexture");

        if (baseColorTextureIter != glTfMaterial.values.end()) {
            currentMaterial.baseColorTextureIndex = glTfModel.textures[baseColorTextureIter->second.TextureIndex()].source;
            if (currentMaterial.baseColorTextureIndex >= totalImagesInModel) {
                std::cout << "Warning: " << __FUNCTION__ << ": " << "currentMaterial.baseColorTextureIndex >= _textures.size()" << std::endl;
                std::cout << "\t Material: " << std::string(glTfMaterial.name) << std::endl;
                std::cout << "\t setting base color texture index to -1" << std::endl;
                currentMaterial.baseColorTextureIndex = -1;
            }
        } else {
            // assign the last (white) texture
            currentMaterial.baseColorTextureIndex = -1;
        }

        // emissive
        currentMaterial.emissiveFactor = glm::vec3(glTfMaterial.emissiveFactor[0], glTfMaterial.emissiveFactor[1], glTfMaterial.emissiveFactor[2]);
        currentMaterial.emissiveTextureIndex = glTfMaterial.emissiveTexture.index;
        if (currentMaterial.emissiveTextureIndex >= (int)totalImagesInModel) {
            std::cout << "Warning: " << __FUNCTION__ << ": " << "currentMaterial.emissiveTextureIndex >= _textures.size()" << std::endl;
            std::cout << "\t Material: " << std::string(glTfMaterial.name) << std::endl;
            std::cout << "\t setting emmissive color texture index to -1" << std::endl;
            currentMaterial.emissiveTextureIndex = -1;
        }
        // Roughness and Metallic
        currentMaterial.roughness = (float)glTfMaterial.pbrMetallicRoughness.roughnessFactor;
        currentMaterial.metalness = (float)glTfMaterial.pbrMetallicRoughness.metallicFactor;
        currentMaterial.occlusionRoughnessMetalnessTextureIndex = glTfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index;
        if (currentMaterial.occlusionRoughnessMetalnessTextureIndex >= (int)totalImagesInModel) {
            std::cout << "Warning: " << __FUNCTION__ << ": " << "currentMaterial.occlusionRoughnessMetalnessTextureIndex >= _textures.size()" << std::endl;
            std::cout << "\t Material: " << std::string(glTfMaterial.name) << std::endl;
            std::cout << "\t setting base color texture index to -1" << std::endl;
            currentMaterial.occlusionRoughnessMetalnessTextureIndex = -1;
        }

        auto transmissionIter = glTfMaterial.extensions.find("KHR_materials_transmission");
        if (transmissionIter != glTfMaterial.extensions.end()) {
            currentMaterial.transmissionFactor = (float)transmissionIter->second.Get("transmissionFactor").GetNumberAsDouble();
            currentMaterial.transmissionTextureIndex = (int)transmissionIter->second.Get("transmissionTexture").GetNumberAsInt();
        }

        // Normals
        currentMaterial.normalTextureIndex = glTfMaterial.normalTexture.index;

        _materials.push_back(currentMaterial);
    }

    // default material
    Material material;
    _materials.push_back(material);
}

void GltfModel::loadMesh(Node* node, const tinygltf::Mesh& srcMesh, tinygltf::Model& gltfModel, uint32_t fileLoadingFlags)
{
    if (srcMesh.primitives.size() > 0) {
        node->_mesh = new Mesh();
    }

    // Iterate through all primitives of this node's mesh
    for (size_t i = 0; i < srcMesh.primitives.size(); i++) {
        const tinygltf::Primitive& glTFPrimitive = srcMesh.primitives[i];
        const uint32_t materialIndex = (glTFPrimitive.material == -1) ? (uint32_t)_materials.size() - 1 : glTFPrimitive.material;
        uint32_t firstIndex = static_cast<uint32_t>(_indexBuffer.size());
        uint32_t vertexStart = static_cast<uint32_t>(_vertexBuffer.size());
        uint32_t indexCount = 0;
        size_t vertexCount = 0;
        // Vertices
        {
            const float* positionBuffer = nullptr;
            const float* normalsBuffer = nullptr;
            const float* texCoordsBuffer = nullptr;

            // Get buffer data for vertex normals
            if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
                const tinygltf::Accessor& accessor = gltfModel.accessors[glTFPrimitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
                positionBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
                vertexCount = accessor.count;
            }
            // Get buffer data for vertex normals
            if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
                const tinygltf::Accessor& accessor = gltfModel.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
                const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
                normalsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
            }
            // Get buffer data for vertex texture coordinates
            // glTF supports multiple sets, we only load the first one
            if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
                const tinygltf::Accessor& accessor = gltfModel.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
                const tinygltf::BufferView& view = gltfModel.bufferViews[accessor.bufferView];
                texCoordsBuffer = reinterpret_cast<const float*>(&(gltfModel.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
            }

            // Append data to model's vertex buffer
            for (size_t v = 0; v < vertexCount; v++) {
                Vertex vertex{};
                vertex.position = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f);
                vertex.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
                vertex.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
                vertex.color = glm::vec4(1.0f);

                const glm::mat4x4 fullTransform = node->fullTransform();
                if (fileLoadingFlags & FileLoadingFlags::PreTransformVertices) {
                    vertex.position = glm::vec3(fullTransform * glm::vec4(vertex.position, 1.0f));
                    vertex.normal = glm::normalize(glm::mat3(fullTransform) * vertex.normal);
                }
                if (fileLoadingFlags & FlipY) {
                    vertex.position.y *= -1.0f;
                    vertex.normal.y *= -1.0f;
                }
                if (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) {
                    vertex.color = glm::vec4(_materials[materialIndex].baseColorFactor.x * vertex.color.x
                                             , _materials[materialIndex].baseColorFactor.y * vertex.color.y
                                             , _materials[materialIndex].baseColorFactor.z * vertex.color.z
                                             , 1.0f
                                            );
                }

                _vertexBuffer.push_back(vertex);
            }
        }
        // Indices
        {
            const tinygltf::Accessor& accessor = gltfModel.accessors[glTFPrimitive.indices];
            const tinygltf::BufferView& bufferView = gltfModel.bufferViews[accessor.bufferView];
            const tinygltf::Buffer& buffer = gltfModel.buffers[bufferView.buffer];

            indexCount += static_cast<uint32_t>(accessor.count);

            // glTF supports different component types of indices
            switch (accessor.componentType) {
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
                uint32_t* buf = new uint32_t[accessor.count];
                memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
                for (size_t index = 0; index < accessor.count; index++) {
                    _indexBuffer.push_back(buf[index] + vertexStart);
                }
                break;
            }
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
                uint16_t* buf = new uint16_t[accessor.count];
                memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
                for (size_t index = 0; index < accessor.count; index++) {
                    _indexBuffer.push_back(buf[index] + vertexStart);
                }
                break;
            }
            case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
                uint8_t* buf = new uint8_t[accessor.count];
                memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
                for (size_t index = 0; index < accessor.count; index++) {
                    _indexBuffer.push_back(buf[index] + vertexStart);
                }
                break;
            }
            default:
                std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
                return;
            }
        }
        Primitive primitive{};
        primitive.firstIndex = firstIndex;
        primitive.indexCount = indexCount;
        primitive.firstVertex = vertexStart;
        primitive.vertexCount = (uint32_t)vertexCount;
        primitive.materialIndex = materialIndex;
        node->_mesh->primitives.push_back(primitive);
    }
}

void GltfModel::loadNode(const tinygltf::Node& inputNode, tinygltf::Model& gltfModel, Node* parent, uint32_t fileLoadingFlags)
{
    Node* node = new Node{};
    node->_matrix = glm::mat4(1.0f);
    node->_parent = parent;

    // Get the local node matrix
    // It's either made up from translation, rotation, scale or a 4x4 matrix
    if (inputNode.translation.size() == 3) {
        node->_matrix = glm::translate(node->_matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
    }
    if (inputNode.rotation.size() == 4) {
        glm::quat q = glm::make_quat(inputNode.rotation.data());
        node->_matrix *= glm::mat4(q);
    }
    if (inputNode.scale.size() == 3) {
        node->_matrix = glm::scale(node->_matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
    }
    if (inputNode.matrix.size() == 16) {
        node->_matrix = glm::make_mat4x4(inputNode.matrix.data());
    };

    // Load node's children
    if (inputNode.children.size() > 0) {
        for (size_t i = 0; i < inputNode.children.size(); i++) {
            loadNode(gltfModel.nodes[inputNode.children[i]], gltfModel, node, fileLoadingFlags);
        }
    }

    if (inputNode.mesh > -1) {
        const tinygltf::Mesh& mesh = gltfModel.meshes[inputNode.mesh];
        loadMesh(node, mesh, gltfModel, fileLoadingFlags);
    }

    if (parent) {
        parent->_children.push_back(node);
    } else {
        _linearNodes.push_back(node);
    }
}

void GltfModel::loadScenes(tinygltf::Model& gltfModel, uint32_t fileLoadingFlags)
{
    const tinygltf::Scene& scene = gltfModel.scenes[0];
    for (int i = 0; i < scene.nodes.size(); ++i) {
        const tinygltf::Node& node = gltfModel.nodes[scene.nodes[i]];
        loadNode(node, gltfModel, nullptr, fileLoadingFlags);
    }
}

glm::mat4 Node::fullTransform() const
{
    glm::mat4 m = _matrix;
    Node* p = _parent;
    while (p) {
        m = p->_matrix * m;
        p = p->_parent;
    }
    return m;
}

void GltfModel::loadFromFile(const std::string& fileName, uint32_t fileLoadingFlags)
{
    tinygltf::Model glTfModel;
    tinygltf::TinyGLTF gltfContext;
    std::string error, warning;

    if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
        gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
    } else {
        gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
    }

    bool fileLoaded = false;
    if (fileName.find(".gltf") != std::string::npos) {
        fileLoaded = gltfContext.LoadASCIIFromFile(&glTfModel, &error, &warning, fileName);
    } else if (fileName.find(".glb") != std::string::npos) {
        fileLoaded = gltfContext.LoadBinaryFromFile(&glTfModel, &error, &warning, fileName);
    }

    if (fileLoaded == false) {
        std::cout << "Warning: " << __FUNCTION__ << ": " << "could not load file: " << fileName << std::endl;
        return;
    }

    _basePath = fileName.substr(0, fileName.find_last_of("/\\"));

    loadScenes(glTfModel, fileLoadingFlags);
}
}

