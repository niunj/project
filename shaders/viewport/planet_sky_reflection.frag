#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet_shell.glsl"
#include "../earth_atmosphere_optics.glsl"
#include "../env/sky_weather.glsl"
#include "weather_reflection.glsl"
#include "marine_fog.glsl"
layout(set=0,binding=9) uniform sampler2D airTransmission;
layout(location=2) in vec2 probeUV;
void main(){
    vec3 eye=w.eyeCover.xyz*6378137.,ray=reflectionDirection(probeUV,eye),up=earthAirUp(eye);
    float h=earthAirHeight(eye),r=length(eye),radius=r+EARTH_AIR_TOP-h;
    float b=dot(eye,ray),range=max(1.,-b+sqrt(max(0.,b*b+radius*radius-r*r)));
    vec3 radiance=vec3(0);
    if(w.air.x>.5){
        radiance=earthAirPath(airTransmission,eye,ray,range,w.sunWind.xyz,w.moon.xyz,w.moon.w,w.lighting.y).radiance;
        float rayUp=dot(ray,up),sunUp=dot(up,w.sunWind.xyz),sunCosine=dot(ray,w.sunWind.xyz);
        radiance=skyAirDisplay(radiance,rayUp,sunUp,sunCosine,h);
        float overcast=w.air.y*(1.-smoothstep(w.optics.z,w.optics.z+1450.,h));
        if(overcast>0.){
            float rain=clamp(w.detail.w,0.,1.);
            vec2 transmission=skyWeatherTransmission(rayUp,vec3(0.,overcast,rain));
            vec2 bodyUp=clamp(vec2(sunUp,dot(up,w.moon.xyz)),-1.,1.);
            radiance=skyWeatherRadiance(radiance,rayUp,vec2(sunCosine,dot(ray,w.moon.xyz)),
                vec3(degrees(asin(bodyUp)),w.moon.w),rain,transmission);
        }
    }
    if(w.fog.x>0.)radiance=marineFog(radiance,range,max(0.,h-w.fog.z),max(0.,earthAirHeight(eye+ray*range)-w.fog.z),w.fog.xy,w.fogLight.rgb);
    // Alpha stores cloud opacity after the subsequent premultiplied pass.
    color=vec4(radiance,0);
}
