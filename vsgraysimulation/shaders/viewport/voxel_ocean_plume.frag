#version 450
#extension GL_GOOGLE_include_directive : require
#include "voxel_ocean_field.glsl"
#include "voxel_ocean_plume.glsl"

layout(push_constant)uniform Camera{mat4 projection;mat4 modelView;} camera;
layout(location=0)in vec3 rayDirection;
layout(location=1)in vec3 jetIndex;
layout(location=0)out vec4 color;

float plumeDensity(PropellerPlume plume,vec3 q,vec3 tangent,vec3 binormal) {
    float distanceAft=dot(q,plume.axis);
    float u=distanceAft/plume.lengthM;
    if(u<=0.||u>=1.)return 0.;
    float age=plume.age*u;
    vec2 radial=vec2(dot(q,tangent),dot(q,binormal));
    float phase=plume.phase-.43*plume.spin*age-.72*log(1.+age);
    radial-=plume.radius*.11*u*vec2(sin(phase+2.8*u),cos(phase+2.8*u));
    // Core and rotating shear layer broaden as the plume mixes into the water.
    float width=plume.radius*(.54+.24*age+.11*smoothstep(0.,1.2,age));
    float r=length(radial);
    float core=exp(-1.65*r*r/(width*width));
    float shear=exp(-pow((r-width*.80)/(width*.44),2.));
    vec2 twist=vec2(cos(phase),sin(phase));
    vec2 plane=vec2(dot(radial,twist),dot(radial,vec2(-twist.y,twist.x)))/max(r,.025);
    float rolls=.68+.32*(plane.x*plane.x-plane.y*plane.y)*exp(-.42*age);
    // Band-limited modulation breaks up the mixing layer without discrete white cards.
    vec3 turbulent=vec3(radial/width,distanceAft/max(plume.radius,.1));
    float textureGain=.89+.12*sin(dot(turbulent,vec3(6.8,2.3,-2.1))+phase)
                     +.09*cos(dot(turbulent,vec3(-3.1,5.7,3.4))+phase-1.2*age);
    float start=smoothstep(0.,min(.15,plume.radius*.21/plume.lengthM),u);
    float end=1.-smoothstep(.57,1.,u);
    return start*end*exp(-.32*age)*textureGain*(.62*core+.54*shear*rolls);
}

void main() {
    PropellerPlume plume=propellerPlume(int(jetIndex.x));
    vec3 eye=ocean.p[1].xyz;
    vec3 ray=normalize(rayDirection);
    if(eye.y>ocean.p[51].y+.06)discard;
    vec3 tangent=normalize(cross(plume.axis,abs(plume.axis.y)<.9?vec3(0,1,0):vec3(1,0,0)));
    vec3 binormal=cross(plume.axis,tangent);
    // Intersect a conservative oriented box so that small/far wakes stay cheap.
    vec3 origin=eye-plume.hub;
    vec3 ro=vec3(dot(origin,tangent),dot(origin,binormal),dot(origin,plume.axis));
    vec3 rd=vec3(dot(ray,tangent),dot(ray,binormal),dot(ray,plume.axis));
    vec3 low=vec3(-plumeRadialBound(plume),-plumeRadialBound(plume),0.);
    vec3 high=vec3(-low.xy,plume.lengthM);
    vec3 inv=vec3(rd.x<0.?-1.:1.,rd.y<0.?-1.:1.,rd.z<0.?-1.:1.)/max(abs(rd),vec3(1.e-5));
    vec3 a=(low-ro)*inv,b=(high-ro)*inv;
    vec3 enters=min(a,b),leaves=max(a,b);
    float first=max(.025,max(enters.x,max(enters.y,enters.z)));
    float last=min(leaves.x,min(leaves.y,leaves.z));
    if(last<=first)discard;
    float stepM=(last-first)/16.;
    float total=0.,front=last,weightedRange=0.;
    const float maxClipDepth=.48;
    vec3 firstPoint=eye+ray*first,lastPoint=eye+ray*last;
    float firstDepth=oceanMean(firstPoint.xz).x-firstPoint.y;
    float lastDepth=oceanMean(lastPoint.xz).x-lastPoint.y;
    for(int i=0;i<16;++i) {
        float f=(float(i)+.5)/16.;
        float t=mix(first,last,f);
        vec3 point=eye+ray*t;
        float density=plumeDensity(plume,point-plume.hub,tangent,binormal);
        density*=smoothstep(.03,maxClipDepth,mix(firstDepth,lastDepth,f));
        if(density>.055)front=min(front,t);
        weightedRange+=density*t;
        total+=density;
    }
    if(total<.035||front==last)discard;
    float alpha=(1.-exp(-.38*total*stepM/max(.6,plume.radius)))*plume.strength;
    if(alpha<.008)discard;
    vec3 point=eye+ray*(weightedRange/total);
    float sea=oceanMean(point.xz).x;
    vec3 light=(ocean.p[53].rgb*.72+ocean.p[52].rgb*.30)*vec3(.56,.73,.76);
    light*=waterTransmission(max(0.,sea-point.y)*.48);
    color=vec4(displayWater(waterTransport(light,point,sea)),alpha);
    vec4 clip=camera.projection*camera.modelView*vec4(eye+ray*front,1.);
    gl_FragDepth=clamp(clip.z/clip.w,0.,1.);
}
