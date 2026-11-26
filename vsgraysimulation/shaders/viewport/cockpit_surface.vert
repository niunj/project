#version 450
layout(push_constant) uniform ViewMatrices {
    mat4 projection;
    mat4 modelView;
} view;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 color;
layout(location=0) out vec3 localPosition;
layout(location=1) out vec3 surfaceNormal;
layout(location=2) out vec2 texcoord;
layout(location=3) out vec4 finish;
void main() {
    localPosition = position;
    surfaceNormal = normal;
    texcoord = uv;
    finish = color;
    gl_Position = view.projection * view.modelView * vec4(position, 1.0);
}
