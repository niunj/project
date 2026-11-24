#version 450
layout(push_constant) uniform Camera { mat4 projection; mat4 modelView; } camera;
layout(location=0) in vec3 position;
layout(location=1) in vec2 atlasUV;
layout(location=2) in vec3 surfaceNormal;
layout(location=0) out vec2 uv;
layout(location=1) out vec3 normalECEF;
void main() {
    normalECEF=normalize(surfaceNormal);
    uv=atlasUV;
    gl_Position=camera.projection*camera.modelView*vec4(position,1);
}
