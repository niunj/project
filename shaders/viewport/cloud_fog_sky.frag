#version 450
#extension GL_GOOGLE_include_directive : require
#include "cloud_temporal.glsl"
#include "planet_light.glsl"
#include "../earth_atmosphere_optics.glsl"
layout(location=0) in vec3 rayECEF;
layout(location=1) flat in vec2 depthCoefficients;
layout(location=0) out vec4 color;
void main(){
    if(temporal.fog.x<=0.)discard;
    vec3 eye=temporal.eyeEarth.xyz,ray=normalize(rayECEF);
    // Only empty sky. The opaque depth test excludes mountains and objects;
    // the ellipsoid excludes sea, which already receives the same fog law.
    vec3 scale=1./vec3(6378137.+temporal.fog.z,6378137.+temporal.fog.z,6356752.314245+temporal.fog.z);
    vec3 e=eye*scale,d=ray*scale;float a=dot(d,d),b=dot(e,d),c=dot(e,e)-1.;
    float disc=b*b-a*c;
    if(disc>=0.&&(-b+sqrt(disc))/a>0.)discard;
    float mu=dot(ray,earthAirUp(eye));
    float span=min(180000.,max(2000.,8.*temporal.fog.y/max(mu,.01))),tau=0.;
    for(int n=0;n<12;++n){float lo=pow(float(n)/12.,2.)*span,hi=pow(float(n+1)/12.,2.)*span;
        float h=max(0.,earthAirHeight(eye+ray*(.5*(lo+hi)))-temporal.fog.z);
        tau+=exp(-h/temporal.fog.y)*(hi-lo)*3.912/temporal.fog.x;
    }
    float alpha=1.-exp(-min(tau,30.));if(alpha<.001)discard;
    gl_FragDepth=0.;
    color=vec4(displayRadiance(temporal.fogLight.rgb,temporal.control.y),alpha);
}
