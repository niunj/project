#version 450

struct Material {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
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
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform samplerCube environmentMap;

uniform vec3 cameraPosition;

void main()
{
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(inPosition, 1.0);
    outPosition = inPosition.xyz;
    outNormal = normalize(inNormal.xyz);
}