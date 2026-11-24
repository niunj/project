// The two passes use the same WGS84 ray, reverse-Z depth and overlap footprint.
layout(location=0) in vec3 rayECEF;
layout(location=1) flat in vec2 depthCoefficients;
layout(set=0,binding=0) uniform Weather {
    vec4 eyeCover, advection, sunWind, detail, sea, cloud, wave, windAxis, optics, moon, lighting, cloudBase, air, fog, fogLight;
} w;
layout(set=0,binding=1) uniform sampler2D field;
layout(location=0) out vec4 color;
#include "planet_light.glsl"
#include "planet_cloud_shadow.glsl"
const float A=6378137.;
vec2 atlasUV(vec3 n) { return vec2(atan(n.y,n.x)*.159154943+.5,.5-asin(clamp(n.z,-1.,1.))*.318309886); }
vec3 shellHit(vec4 shell,out float footprintDistance) {
    if(shell.w==0.) discard;
    vec3 scale=vec3(1,1,shell.y)/shell.x;
    vec3 e=w.eyeCover.xyz*scale, d=rayECEF*scale;
    float b=dot(e,d), a=dot(d,d), disc=b*b-a*shell.z;
    if(disc<=0.) discard;
    // Stable near root for a camera just above the ellipsoid.
    float t=(shell.z>=0.) ? shell.z/(-b+sqrt(disc)) : (-b+sqrt(disc))/a;
    if(t<=0.) discard;
    gl_FragDepth=-depthCoefficients.x+depthCoefficients.y/(t*A);
    if(gl_FragDepth<0. || gl_FragDepth>1.) discard;
    vec3 n=normalize((e+d*t)*vec3(1,1,shell.y));
    footprintDistance=A*length(n-normalize(e*vec3(1,1,shell.y)));
    return n;
}
float nearWeight(float distanceMetres,float radius) {
    return w.detail.x*(1.-smoothstep(.65*radius,radius,distanceMetres));
}
