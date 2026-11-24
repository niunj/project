#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet_light.glsl"
#include "planet_cloud_shadow.glsl"
layout(set=0,binding=0) uniform sampler2D imagery;
layout(set=0,binding=1) uniform Windows { vec4 region[2]; vec4 sun; vec4 moon; vec4 lighting; vec4 advection; vec4 cloudProperties; } windows;
layout(location=0) in vec2 uv;
layout(location=1) in vec3 normalECEF;
layout(location=0) out vec4 color;
void main() {
    for(int i=0;i<2;++i) {
        vec4 r=windows.region[i]; // west, width, north, south in wrapped atlas coordinates
        if(fract(uv.x-r.x)<r.y && uv.y>=r.z && uv.y<r.w) discard;
    }
    vec3 n=normalize(normalECEF);
    vec3 irradiance=vec3(1.);
    if(windows.sun.w>0.) {
        float mu=max(dot(n,windows.sun.xyz),0.);
        irradiance=surfaceSun(n,windows.sun.xyz,windows.lighting.y)*cloudVisibility(n,windows.sun.xyz,windows.advection,windows.cloudProperties)*mu+
            surfaceMoon(n,windows.moon.xyz,windows.moon.w)*cloudVisibility(n,windows.moon.xyz,windows.advection,windows.cloudProperties)*max(dot(n,windows.moon.xyz),0.)+vec3(.00001);
        irradiance+=vec3(.025,.04,.07)*smoothstep(-.08,.25,dot(n,windows.sun.xyz));
    }
    color=vec4(displayRadiance(texture(imagery,uv).rgb*irradiance,windows.lighting.x),1.);
}
