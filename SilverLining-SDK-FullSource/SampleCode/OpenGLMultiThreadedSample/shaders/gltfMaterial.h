#if CPU_SIDE_COMPILATION
#pragma once
namespace Sample {
#endif

#define ALPHA_OPAQUE 0
#define ALPHA_MASK 1
#define ALPHA_BLEND 2

    struct Material
    {
#if CPU_SIDE_COMPILATION
        glm::vec4 baseColorFactor;
        glm::vec3 emissiveFactor;
#else
        vec4 baseColorFactor;
        vec3 emissiveFactor;
#endif
        int emissiveTextureIndex;

        int baseColorTextureIndex;

        float roughness;
        float metalness;

        // When using an image, glTF expects the 
        // metallic values to be encoded in the blue(B) channel
        // and roughness to be encoded in the green(G) channel of the same image
        // https://docs.blender.org/manual/en/2.80/addons/io_scene_gltf2.html
        int occlusionRoughnessMetalnessTextureIndex;

        int normalTextureIndex;

        int  alphaMode;

        float transmissionFactor;
        int transmissionTextureIndex;

#if CPU_SIDE_COMPILATION
        Material()
            : baseColorFactor(glm::vec4(1.0f))
            , emissiveFactor(glm::vec4(0.0f))
            , emissiveTextureIndex(-1)
            , baseColorTextureIndex(-1)
            , roughness(0)
            , metalness(0)
            , occlusionRoughnessMetalnessTextureIndex(-1)
            , normalTextureIndex(-1)
            , alphaMode(ALPHA_OPAQUE)
            , transmissionFactor(0)
            , transmissionTextureIndex(-1)
        {
        }
#endif
    };

#if CPU_SIDE_COMPILATION
}
#endif