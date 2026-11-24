#version 450
layout(push_constant) uniform Camera { mat4 projection; mat4 modelView; } camera;
layout(location=0) out vec3 rayECEF;
layout(location=1) flat out vec2 depthCoefficients;
void main() {
    vec2 ndc=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.-1.;
    vec4 nearEye=inverse(camera.projection)*vec4(ndc,1,1);
    rayECEF=transpose(mat3(camera.modelView))*(nearEye.xyz/-nearEye.z);
    depthCoefficients=vec2(camera.projection[2][2],camera.projection[3][2]);
    gl_Position=vec4(ndc,1,1);
}
