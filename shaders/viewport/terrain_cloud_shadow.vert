#version 450
layout(push_constant) uniform Camera {mat4 projection;mat4 modelView;} camera;
layout(set=0,binding=0) uniform Terrain {
    vec4 advection,sun,moon,properties;
    mat4 frame;
    vec4 eyeAtmosphere,horizonTurbidity,display,zenithContext,workingRegion,marine;
} terrain;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normalENU;
layout(location=2) in vec2 texcoord;
layout(location=0) out vec3 normalECEF;
layout(location=1) out vec3 positionECEF;
layout(location=2) out vec2 uv;
layout(location=3) out vec3 viewPosition;
void main() {
    positionECEF=(terrain.frame*vec4(position,1)).xyz;
    normalECEF=mat3(terrain.frame)*normalENU;
    uv=texcoord;
    vec4 view=camera.modelView*vec4(position,1);
    viewPosition=view.xyz;
    gl_Position=camera.projection*view;
}
