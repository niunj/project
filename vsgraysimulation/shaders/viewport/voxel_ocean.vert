#version 450
#extension GL_GOOGLE_include_directive : require
#include "voxel_ocean_field.glsl"
#include "voxel_ocean_plume.glsl"
layout(push_constant)uniform Camera{mat4 projection;mat4 modelView;} camera;
layout(location=0)in vec3 position;
layout(location=1)in vec3 normal;
layout(location=2)in vec2 uv;
layout(location=0)out vec3 scenePosition;
layout(location=1)out vec3 sceneNormal;
layout(location=2)out vec2 texcoord;
float sprayRandom(float n){return fract(sin(n*12.9898+78.233)*43758.5453);}
void projectPropellerPlume(int index) {
    gl_Position=vec4(2,2,2,1);
    if(index>=int(ocean.p[218].y))return;
    PropellerPlume plume=propellerPlume(index);
    float radius=plumeRadialBound(plume);
    vec3 side=normalize(cross(plume.axis,abs(plume.axis.y)<.9?vec3(0,1,0):vec3(1,0,0)));
    vec3 up=cross(plume.axis,side);
    mat4 vp=camera.projection*camera.modelView;
    vec2 low=vec2(1e10),high=vec2(-1e10);
    bool behind=false,visible=false;
    for(int i=0;i<8;++i) {
        vec3 corner=plume.hub+plume.axis*((i&1)!=0?plume.lengthM:0.)
            +side*((i&2)!=0?radius:-radius)+up*((i&4)!=0?radius:-radius);
        vec4 clip=vp*vec4(corner,1.);
        if(clip.w<=.05){behind=true;continue;}
        vec2 ndc=clip.xy/clip.w;
        low=min(low,ndc); high=max(high,ndc); visible=true;
    }
    if(!visible)return;
    if(behind){low=vec2(-1.);high=vec2(1.);}
    vec2 ndc=mix(clamp(low,-1.,1.),clamp(high,-1.,1.),uv*.5+.5);
    vec4 ray=inverse(camera.projection)*vec4(ndc,.5,1.);
    scenePosition=transpose(mat3(camera.modelView))*ray.xyz;
    sceneNormal=vec3(index,0,0);
    gl_Position=vec4(ndc,.5,1.);
}
void main(){if(ocean.p[9].x>3.5){projectPropellerPlume(int(position.x));return;}
if(ocean.p[9].x>2.5){int event=int(position.x);if(event>=int(ocean.p[167].w)||ocean.p[166].w>.5){gl_Position=vec4(2,2,2,1);return;}vec4 a=ocean.p[168+event*2],b=ocean.p[169+event*2];float seed=b.w+position.y*7.17;float t=ocean.p[0].x-a.z-sprayRandom(seed)*.10;float az=sprayRandom(seed+1.)*6.2831853,radial=b.x*(.22+.35*sprayRandom(seed+2.)),vy=b.x*(.5+.5*sprayRandom(seed+3.));vec3 centre=vec3(a.x,a.w,a.y)+vec3(cos(az)*radial*t+ocean.p[167].x*t*t*.025,vy*t-4.903325*t*t,sin(az)*radial*t+ocean.p[167].y*t*t*.025);vec2 drift=ocean.p[808+event].xy;
centre.xz+=drift*(.49*(1.-exp(-max(0.,t)/.49)));
if(t<0.||t>b.z||centre.y<a.w-.04){gl_Position=vec4(2,2,2,1);return;}float size=b.y*(.035+.055*sprayRandom(seed+4.))*(1.+t*.35);mat3 mv=mat3(camera.modelView);vec3 right=vec3(mv[0][0],mv[1][0],mv[2][0]),up=vec3(mv[0][1],mv[1][1],mv[2][1]);scenePosition=centre+size*(right*uv.x+up*uv.y);sceneNormal=vec3(0,1,clamp(1.-t/b.z,0.,1.));texcoord=uv;gl_Position=camera.projection*camera.modelView*vec4(scenePosition,1);return;}
if(ocean.p[9].x>1.5){mat4 vp=camera.projection*camera.modelView;vec4 farPoint=inverse(vp)*vec4(position.xy,.9,1.);scenePosition=farPoint.xyz/farPoint.w;sceneNormal=vec3(0,1,0);texcoord=position.xy;gl_Position=vec4(position.xy,0,1);return;}mat4 object=mat4(ocean.p[14],ocean.p[15],ocean.p[16],ocean.p[17]);vec3 p=position;bool surface=ocean.p[9].x>.5;if(surface){p.xz+=ocean.p[51].zw;p.y+=oceanMeshHeight(p.xz,uv.x);}scenePosition=(object*vec4(p,1)).xyz;sceneNormal=surface?vec3(0,1,0):normalize(mat3(object)*normal);texcoord=uv;vec3 apparent=scenePosition;
// First-order planar-interface apparent-depth projection. It changes optical
// projection only: canonical mesh positions, collision and mass stay intact.
vec3 eye=ocean.p[1].xyz;float maximumSea=ocean.p[0].y+ocean.p[128].w+ocean.p[50].w;bool needsRefraction=!surface&&(ocean.p[128].y>.5||(eye.y>ocean.p[51].y+.03&&scenePosition.y<=maximumSea));float level=needsRefraction?oceanHeight(scenePosition.xz):maximumSea;if(needsRefraction&&eye.y>level+.03&&scenePosition.y<level){vec2 delta=eye.xz-scenePosition.xz;float range=length(delta),air=eye.y-level,water=level-scenePosition.y;if(range>1e-4){float lo=0,hi=range;for(int i=0;i<12;++i){float x=(lo+hi)*.5;float a=(range-x)/sqrt((range-x)*(range-x)+air*air),b=ocean.p[6].w*x/sqrt(x*x+water*water);if(a>b)lo=x;else hi=x;}vec3 s=vec3(scenePosition.xz+delta*((lo+hi)*.5/range),level).xzy;apparent=s+(s-eye)*(water/max(.1,air*ocean.p[6].w));}}
// Use the same current camera push constants as terrain. The native
// viewport may update near/far clips immediately before submission.
vec3 localApparent=transpose(mat3(object))*(apparent-object[3].xyz);gl_Position=camera.projection*camera.modelView*vec4(localApparent,1);}
