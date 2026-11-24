#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet_light.glsl"
layout(location=0) in vec3 rayECEF;
layout(location=1) flat in vec2 depthCoefficients;
layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform sampler2D cloudColor;
layout(set=0,binding=1) uniform sampler2D cloudDepth;
#include "cloud_temporal.glsl"
void main(){
    vec2 uv=gl_FragCoord.xy/temporal.screen.xy;
    vec4 cloud=texture(cloudColor,uv);float depth=texture(cloudDepth,uv).y;
    if(cloud.a<.002||depth<=0.)discard;
    gl_FragDepth=clamp(-depthCoefficients.x+depthCoefficients.y*length(rayECEF)/depth,0.,1.);
    color=vec4(displayRadiance(cloud.rgb/max(cloud.a,.002),temporal.control.y),clamp(cloud.a,0.,1.));
}
