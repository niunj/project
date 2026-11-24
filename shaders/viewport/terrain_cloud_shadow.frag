#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet_cloud_shadow.glsl"
#include "planet_light.glsl"
#include "../earth_atmosphere_optics.glsl"
#include "marine_fog.glsl"
#include "terrain_weather.glsl"
layout(set=0,binding=0) uniform Terrain {
    vec4 advection,sun,moon,properties;
    mat4 frame;
    vec4 eyeAtmosphere,horizonTurbidity,display,zenithContext,workingRegion,marine;
} terrain;
layout(set=0,binding=1) uniform sampler2D albedo;
layout(set=0,binding=9) uniform sampler2D pinnedAlbedo;
layout(set=0,binding=10) uniform sampler2D airTransmission;
layout(location=0) in vec3 normalECEF;
layout(location=1) in vec3 positionECEF;
layout(location=2) in vec2 uv;
layout(location=3) in vec3 viewPosition;
layout(location=0) out vec4 color;
void main() {
    vec3 n=normalize(normalECEF);
    vec3 up=earthAirUp(positionECEF);
    float height=max(0.,earthAirHeight(positionECEF));
    float footprint=length(fwidth(positionECEF));
    float turbidity=terrain.horizonTurbidity.w;
    float sunMu=dot(up,terrain.sun.xyz);
    bool air=terrain.eyeAtmosphere.w>.5;
    float daylight=smoothstep(-.106,.14,sunMu);
    float lunar=earthDisplayMoon(terrain.moon.w,sunMu);
    vec3 lunarBeam=vec3(0.);
    if(lunar>1.e-5)lunarBeam=lunar*earthDiskTransmission(airTransmission,height,dot(up,terrain.moon.xyz),air);
    vec3 light=vec3(.20,.26,.338)*daylight+vec3(.00013)+lunarBeam*.23;
    float skyView=mix(1.,.5+.5*max(0.,dot(n,up)),terrain.display.w);
    light=mix(light,vec3(.135)*daylight*skyView+vec3(.00013)+lunarBeam*.23,terrain.sun.w);
    if(dot(n,terrain.sun.xyz)>0.) {
        float visibility=cloudVisibilityFromPoint(positionECEF,terrain.sun.xyz,terrain.advection,terrain.properties,footprint);
        light+=earthDiskTransmission(airTransmission,height,sunMu,air)*dot(n,terrain.sun.xyz)*visibility*
               mix(1.,mix(.35,.06,terrain.display.w),terrain.sun.w);
    }
    if(lunar>1.e-5 && dot(n,terrain.moon.xyz)>0.) {
        float visibility=cloudVisibilityFromPoint(positionECEF,terrain.moon.xyz,terrain.advection,terrain.properties,footprint);
        light+=lunarBeam*dot(n,terrain.moon.xyz)*visibility;
    }
    vec3 reflectance=texture(albedo,uv).rgb;
    if(terrain.zenithContext.w>0.) {
        // Retain the original atlas throughout the authoritative region.
        // Fade its clamped edge into the continuation only outside the ROI.
        vec2 localUV=(uv-terrain.workingRegion.xy)/terrain.workingRegion.zw;
        vec2 outside=max(abs(localUV-.5)-.5,vec2(0.));
        float weight=1.-smoothstep(0.,.045,max(outside.x,outside.y));
        reflectance=mix(reflectance,texture(pinnedAlbedo,localUV).rgb,weight);
    }
    if(terrain.zenithContext.w>0. && air) {
        float edge=max(abs(uv.x-.5),abs(uv.y-.5));
        reflectance=mix(reflectance,vec3(.028,.035,.040),smoothstep(.38,.50,edge));
    }
    // The orbital imagery is a display product with its own colour grading.
    // Map it into the diffuse reflectance range before applying irradiance.
    const float imageryReflectanceScale=.40;
    vec4 appearance=vec4(reflectance,0.);
    if(terrain.display.y>0. || terrain.display.z>0.) {
        // Near the ground, absolute ECEF varying precision is coarser than
        // one pixel on the surface. Estimate angular derivatives in view space.
        float range=max(length(viewPosition),1.);
        vec3 viewRay=viewPosition/range;
        float incidence=abs(dot(positionECEF-terrain.eyeAtmosphere.xyz,up))/range;
        float weatherFootprint=range*length(fwidth(viewRay))/max(.09,incidence);
        appearance=groundWeather(reflectance,positionECEF,n,up,weatherFootprint,terrain.display.y,terrain.display.z);
    }
    reflectance=appearance.rgb;
    vec3 rgb=reflectance*light*(imageryReflectanceScale*EARTH_AIR_SOLAR_SCALE/3.14159265359);
    float grazing=pow(1.-max(0.,dot(n,normalize(terrain.eyeAtmosphere.xyz-positionECEF))),5.);
    rgb+=appearance.w*(.008+.08*grazing)*terrain.horizonTurbidity.rgb;
    if(air) {
        vec3 offset=positionECEF-terrain.eyeAtmosphere.xyz;
        float distance=length(offset);
        EarthAirPath path=earthAirPath(airTransmission,terrain.eyeAtmosphere.xyz,offset/max(distance,1.),distance,
                         terrain.sun.xyz,terrain.moon.xyz,terrain.moon.w,turbidity);
        // The low-range imagery needs a restrained veiling-luminance mapping
        // on downward views. Keep the atmospheric extinction and the shallow
        // horizon rays intact, while recovering ground colour through the
        // shorter, steep air column. All weights vary continuously in angle.
        float down=max(0.,-dot(offset/max(distance,1.),earthAirUp(terrain.eyeAtmosphere.xyz)));
        float clearGround=smoothstep(.055,.42,down);
        float skyLuminance=dot(path.radiance,vec3(.2126,.7152,.0722));
        vec3 veil=mix(path.radiance,vec3(skyLuminance),.28*clearGround);
        veil=mix(veil,mix(veil,vec3(dot(veil,vec3(.2126,.7152,.0722))),.86)*.35,terrain.sun.w);
        veil*=mix(1.,.36,clearGround);
        rgb=rgb*path.transmission+veil;
    }
    if(terrain.marine.w>.5){float wet=1.-smoothstep(.15,1.5,max(0.,height-terrain.marine.z));if(terrain.marine.w<1.5)rgb*=1.-.18*wet;rgb=marineFog(rgb,length(positionECEF-terrain.eyeAtmosphere.xyz),earthAirHeight(terrain.eyeAtmosphere.xyz)-terrain.marine.z,height-terrain.marine.z,terrain.marine.xy,terrain.horizonTurbidity.rgb);}
    color=vec4(displayRadiance(rgb,terrain.display.x),1.);
}
