#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform samplerCube environmentMap;

uniform vec3 cameraPosition;

void main(void)
{
    vec3 worldPosition = (modelMatrix * vec4(inPosition, 1)).xyz;
    vec3 worldNormal = normalize((modelMatrix * vec4(normalize(inNormal), 1)).xyz);

    vec3 V = normalize(worldPosition - cameraPosition);
    vec3 R = reflect(V, worldNormal);
    outColor.xyz = texture(environmentMap, R.xyz).xyz;

    outColor.a = 1;
}