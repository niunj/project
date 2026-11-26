#version 450
layout(push_constant) uniform Camera { mat4 projection; mat4 modelView; } camera;
layout(std430,set=0,binding=0) readonly buffer Segments { vec4 values[]; } segments;
layout(std140,set=0,binding=1) uniform Parameters { vec4 viewportTime; } params;
layout(location=0) out vec3 trailColor;
layout(location=1) out vec2 trailSample;
const vec2 quad[6]=vec2[6](vec2(0,-1),vec2(1,-1),vec2(1,1),vec2(0,-1),vec2(1,1),vec2(0,1));

void main() {
    int index=gl_InstanceIndex*5;
    vec2 face=quad[gl_VertexIndex];
    int endpoint=int(face.x);
    vec4 point=segments.values[index+endpoint];
    vec4 tangent=segments.values[index+2+endpoint];
    mat4 viewProjection=camera.projection*camera.modelView;
    vec4 clip=viewProjection*vec4(point.xyz,1.);
    vec4 along=viewProjection*tangent;
    vec2 viewport=params.viewportTime.xy;
    vec2 screen=(along.xy*clip.w-clip.xy*along.w)*viewport;
    vec2 across=vec2(-screen.y,screen.x)/max(length(screen),.0001);
    clip.xy+=across*face.y*2.3*clip.w/viewport;
    gl_Position=clip;
    trailColor=segments.values[index+4].rgb;
    trailSample=vec2(face.y,(params.viewportTime.z-point.w)/params.viewportTime.w);
}
