#version 450
#extension GL_GOOGLE_include_directive : require
#include "planet_shell.glsl"
#include "marine_fog.glsl"
#include "../earth_atmosphere_optics.glsl"
void main() {
    float dist;
    vec3 n=shellHit(w.sea,dist);
    float regionalWeight=w.detail.z<0.?smoothstep(.8*abs(w.detail.z),abs(w.detail.z),dist):1.;if(regionalWeight<.001)discard;
    float ocean=texture(field,atlasUV(n)).r;
    // The FFT patch overlays this far-field base, including along its feathered edge.
    if(ocean<.01) discard;
    vec3 V=-normalize(rayECEF), L=w.sunWind.xyz;
    float day=max(dot(n,L),0.), grazing=pow(1.-max(dot(n,V),0.),5.);
    // Filter unresolved peak waves into the roughness lobe at orbital pixel sizes.
    // Stable geographic wave direction even at simulation time zero.
    vec3 wind=normalize(cross(w.windAxis.xyz,n)+vec3(1e-8));
    vec3 pole=normalize(w.windAxis.xyz);
    vec3 basis=normalize(cross(pole,abs(pole.z)<.8?vec3(0,0,1):vec3(1,0,0)));
    float phase=atan(dot(n,cross(pole,basis)),dot(n,basis))*round(A*w.wave.w);
    float resolved=1.-smoothstep(.6,3.,length(fwidth(n))*A*w.wave.w);
    float swell=cos(phase)*w.wave.y+sin(phase)*w.wave.x;
    vec3 slope=normalize(n-wind*clamp(.25*w.wave.z*w.wave.w,0.,.25)*swell*resolved);
    // Cox-Munk isotropic mean-square slope, wind speed in m/s.
    float mss=.003+.00512*w.sunWind.w;
    vec3 H=normalize(V+L);
    float nh=max(dot(slope,H),.001);
    float vh=max(dot(V,H),.001), nv=max(dot(slope,V),.001), nl=max(dot(slope,L),0.);
    float tan2=max(0.,1.-nh*nh)/(nh*nh);
    float D=exp(-tan2/mss)/(3.14159265*mss*pow(nh,4.));
    // Unpolarized dielectric Fresnel, air to water (n = 1.333).
    float transmitted=sqrt(max(0.,1.-(1.-vh*vh)/(1.333*1.333)));
    float rs=(vh-1.333*transmitted)/(vh+1.333*transmitted);
    float rp=(1.333*vh-transmitted)/(1.333*vh+transmitted);
    float F=.5*(rs*rs+rp*rp);
    float G=min(1.,min(2.*nh*nv/vh,2.*nh*nl/vh));
    float glint=D*F*G/(4.*nv);
    vec3 water=mix(vec3(.012,.055,.095),vec3(.14,.26,.37),grazing)*(.07+.93*sqrt(day));
    vec4 cloudProperties=vec4(w.eyeCover.w,w.optics.x,
                              (w.cloudBase.x-1.)*CLOUD_A,(w.cloud.x-1.)*CLOUD_A);
    float solarVisibility=cloudVisibility(n,L,w.advection,cloudProperties);
    vec3 solar=surfaceSun(n,L,w.lighting.y)*(.18+.82*solarVisibility);
    vec3 M=w.moon.xyz;
    float moonNH=max(dot(slope,normalize(V+M)),.001);
    float moonD=exp(-(1.-moonNH*moonNH)/(moonNH*moonNH*mss))/(3.14159265*mss*pow(moonNH,4.));
    vec3 lunar=surfaceMoon(n,M,w.moon.w)*cloudVisibility(n,M,w.advection,cloudProperties);
    vec3 radiance=(water+glint*day)*solar+
        lunar*(.02+moonD*.02)*max(dot(n,M),0.)+vec3(.000002);
    if(w.fog.x>0.){float distance=abs(depthCoefficients.y/(gl_FragDepth+depthCoefficients.x))*length(rayECEF);radiance=marineFog(radiance,distance,earthAirHeight(w.eyeCover.xyz*CLOUD_A)-w.fog.z,0.,w.fog.xy,w.fogLight.rgb);}
    color=vec4(displayRadiance(radiance,w.lighting.x),.83*ocean*regionalWeight);
}
