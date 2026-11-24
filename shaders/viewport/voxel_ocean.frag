#version 450
#extension GL_GOOGLE_include_directive : require
#include "voxel_ocean_field.glsl"
#include "../earth_atmosphere_optics.glsl"
#include "marine_fog.glsl"
#include "marine_specular.glsl"
#include "rain_water.glsl"
layout(set=0,binding=5)uniform sampler2D airTransmission;
layout(set=0,binding=1)uniform sampler2D albedoMap;
layout(set=0,binding=2)uniform sampler2D normalMap;
layout(set=0,binding=3)uniform sampler2D mrMap;
layout(set=0,binding=6)uniform sampler2D emissiveMap;
layout(set=0,binding=7)uniform sampler2D weatherReflection;
layout(set=0,binding=8)uniform sampler2D foamMap;
layout(set=0,binding=9)uniform ReflectionState {vec4 state;vec4 eye;} probe;
vec2 weatherUV(vec3 direction){
    mat3 chart=mat3(ocean.p[161].xyz,ocean.p[162].xyz,ocean.p[163].xyz);
    vec3 ray=normalize(chart*direction),up=earthAirUp(probe.eye.xyz);
    vec3 east=normalize(vec3(-probe.eye.y,probe.eye.x,0)),north=cross(up,east);
    return vec2(atan(dot(ray,north),dot(ray,east))/6.28318530718,asin(clamp(dot(ray,up),0.,1.))/1.57079632679);
}
float weatherBeam(vec3 direction){return probe.state.x>.5?clamp(1.-textureLod(weatherReflection,weatherUV(direction),0.).a,.02,1.):1.;}
vec3 reflectedSkyTap(vec3 ray,vec3 water){
    vec3 skyRadiance=textureLod(weatherReflection,weatherUV(ray),0.).rgb;
    return mix(water,skyRadiance,smoothstep(-.075,.025,ray.y));
}
vec3 cachedWeather(vec3 ray,float normalVariance,vec3 water){
    // Each ray in the roughness lobe crosses the horizon separately. Applying
    // visibility only at the centre made the entire lobe change at once.
    vec3 axis=abs(ray.y)<.95?vec3(0,1,0):vec3(1,0,0);
    vec3 tangent=normalize(cross(ray,axis));
    vec3 bitangent=cross(ray,tangent);
    float spread=clamp(sqrt(pow(.025+.0055*clamp(ocean.p[7].w,0.,27.),2.)+4.*normalVariance),.025,.58);
    return reflectedSkyTap(ray,water)*.4+.15*(
        reflectedSkyTap(normalize(ray+tangent*spread),water)+
        reflectedSkyTap(normalize(ray-tangent*spread),water)+
        reflectedSkyTap(normalize(ray+bitangent*spread),water)+
        reflectedSkyTap(normalize(ray-bitangent*spread),water));
}
layout(location=0)in vec3 scenePosition;
layout(location=1)in vec3 sceneNormal;
layout(location=2)in vec2 texcoord;
layout(location=0)out vec4 color;
const float PI=3.14159265359;
float fresnel(float cosine,float n1,float n2){float sin2=(n1/n2)*(n1/n2)*(1.-cosine*cosine);if(sin2>=1.)return 1.;float ct=sqrt(1.-sin2);float a=(n1*cosine-n2*ct)/(n1*cosine+n2*ct),b=(n2*cosine-n1*ct)/(n2*cosine+n1*ct);return .5*(a*a+b*b);}
vec3 sky(vec3 direction,bool disks,float variance){
    float up=clamp(direction.y,0.,1.);
    if(ocean.p[166].w<.5)up=clamp(sqrt(up*up+(.003+.00512*ocean.p[7].w)*.5),0.,1.);
    float elevation=ocean.p[166].w>.5?pow(up,.45):(pow(up+.025,.45)-pow(.025,.45))/(pow(1.025,.45)-pow(.025,.45));
    vec3 water=waterAmbient()*.52;
    vec3 c=probe.state.x>.5?cachedWeather(direction,variance,water):
        mix(water,mix(ocean.p[4].rgb,ocean.p[5].rgb,elevation),smoothstep(-.075,.025,direction.y));
    // Above water the finite rough sun/moon reflection is the GGX term below.
    // Keep actual disks for transmitted Snell-window views, never add them twice.
    if(disks){
        float cosine=clamp(dot(direction,normalize(ocean.p[2].xyz)),-1.,1.);
        float disk=smoothstep(cos(.0049),cos(.0043),cosine),glow=pow(max(0.,cosine),400.);
        c+=ocean.p[52].rgb*weatherBeam(normalize(ocean.p[2].xyz))*(disk*3.+glow*.025);
        if(ocean.p[166].w<.5&&dot(ocean.p[54].rgb,vec3(1))>1e-7){
            float moonCosine=clamp(dot(direction,normalize(ocean.p[3].xyz)),-1.,1.);
            float moonDisk=smoothstep(cos(.0048),cos(.0041),moonCosine);
            c+=ocean.p[54].rgb*weatherBeam(normalize(ocean.p[3].xyz))*(moonDisk*15000.+pow(max(0.,moonCosine),350.)*.08);
        }
    }
    return c;
}
float ggx(float ndh,float rough){return waterGgx(ndh,rough);}
vec2 rippleGradient(vec2 p){vec2 i=floor(p),u=fract(p),f=u*u*(3.-2.*u),df=6.*u*(1.-u);float a=fract(sin(dot(i,vec2(127.1,311.7)))*43758.5453),b=fract(sin(dot(i+vec2(1,0),vec2(127.1,311.7)))*43758.5453),c=fract(sin(dot(i+vec2(0,1),vec2(127.1,311.7)))*43758.5453),d=fract(sin(dot(i+vec2(1,1),vec2(127.1,311.7)))*43758.5453);return vec2(mix(b-a,d-c,f.y)*df.x,mix(c-a,d-b,f.x)*df.y);}
// A continuous simplex field and its analytic gradient share the same hash.
vec3 windSimplex(vec2 p){
    const float F=.36602540378,G=.21132486540;
    vec2 cell=floor(p+(p.x+p.y)*F),x0=p-cell+(cell.x+cell.y)*G;
    vec2 corner=x0.x>x0.y?vec2(1,0):vec2(0,1),result=vec2(0);
    float value=0.;
    const vec2 gradients[8]=vec2[8](vec2(1,0),vec2(-1,0),vec2(0,1),vec2(0,-1),vec2(.7071,.7071),vec2(-.7071,.7071),vec2(.7071,-.7071),vec2(-.7071,-.7071));
    for(int n=0;n<3;++n){vec2 offset=n==0?vec2(0):n==1?corner:vec2(1);vec2 x=x0-offset+float(n)*G;
        uvec2 ij=uvec2(ivec2(cell+offset));uint h=ij.x*1664525u^ij.y*1013904223u;h^=h>>16;h*=2246822519u;h^=h>>13;
        vec2 gradient=gradients[h&7u];float t=max(0.,.5-dot(x,x)),t2=t*t,t3=t2*t,t4=t2*t2;
        float ramp=dot(gradient,x);
        result+=t4*gradient-8.*t3*x*ramp;
        value+=t4*ramp;
    }
    return vec3(value*36.,result*8.4);
}
float foamNoise(vec2 p){vec2 i=floor(p),u=fract(p);u=u*u*(3.-2.*u);vec4 h=fract(sin(vec4(dot(i,vec2(127.1,311.7)),dot(i+vec2(1,0),vec2(127.1,311.7)),dot(i+vec2(0,1),vec2(127.1,311.7)),dot(i+vec2(1,1),vec2(127.1,311.7))))*43758.5453);return mix(mix(h.x,h.y,u.x),mix(h.z,h.w,u.x),u.y);}
vec3 oceanAir(vec3 rgb,vec3 p){vec3 eye=ocean.p[1].xyz;float distance=length(p-eye);if(eye.y<ocean.p[51].y)return rgb;if(ocean.p[165].y>.5&&distance>50.){mat4 chart=mat4(ocean.p[161],ocean.p[162],ocean.p[163],ocean.p[164]);vec3 delta=normalize(p-eye),direction=normalize(mat3(chart)*delta),eyeWorld=(chart*vec4(eye,1)).xyz;EarthAirPath path=earthAirPath(airTransmission,eyeWorld,direction,distance,normalize(mat3(chart)*ocean.p[2].xyz),normalize(mat3(chart)*ocean.p[3].xyz),ocean.p[3].w,ocean.p[165].x);float clear=smoothstep(.055,.42,max(0.,-delta.y));rgb=mix(rgb,rgb*path.transmission+path.radiance*mix(1.,.36,clear),smoothstep(50.,200.,distance));}
if(ocean.p[166].w<.5)rgb=marineFog(rgb,distance,eye.y-oceanMean(eye.xz).x,p.y-oceanMean(p.xz).x,ocean.p[166].xy,ocean.p[4].rgb);return rgb;}
float hullAeration(vec2 xz){float foam=0.;for(int i=0;i<int(ocean.p[216].x);++i){vec4 a=ocean.p[200+i*2],b=ocean.p[201+i*2];vec2 d=xz-a.xy,q=vec2(dot(d,b.xy),dot(d,vec2(-b.y,b.x)));if(any(greaterThan(abs(q),a.zw+vec2(2.))))continue;
// Rounded superellipse bounds avoid the former rectangular white outline.
vec2 relative=abs(q)/max(a.zw,vec2(.1));float edge=(pow(pow(relative.x,2.6)+pow(relative.y,2.6),1./2.6)-1.)*min(a.z,a.w);
float width=.12+min(b.z,5.)*.065;float breakup=foamNoise(xz*2.3-ocean.p[50].xy*ocean.p[0].x*2.3);float ring=exp(-pow((edge-.08)/(width*(.65+breakup)),2.));foam=max(foam,ring*b.w*(.3+.7*breakup));}return foam;}
float knownWaterGeometry(vec2 xz){float known=0.;for(int i=0;i<8;++i){vec4 r=ocean.p[153+i];if(r.z<=r.x)continue;float edge=min(min(xz.x-r.x,xz.y-r.y),min(r.z-xz.x,r.w-xz.y));known=max(known,smoothstep(0.,8.,edge));}return known;}
void main(){if(ocean.p[9].x>2.5){float r=dot(texcoord,texcoord);if(r>1.)discard;float alpha=pow(1.-r,2.)*sceneNormal.z*.65;vec3 light=(ocean.p[53].rgb*1.4+ocean.p[52].rgb*.35+ocean.p[54].rgb*.8)*vec3(.92,.97,1.);color=vec4(displayWater(oceanAir(light,scenePosition)),alpha);return;}
if(ocean.p[9].x>.5&&ocean.p[9].x<1.5&&max(abs(scenePosition.x),abs(scenePosition.z))>ocean.p[51].x)discard;if(ocean.p[9].x>.5&&ocean.p[9].x<1.5&&ocean.p[9].y<-.5){color=vec4(0);return;}vec3 eye=ocean.p[1].xyz,p=scenePosition,V=normalize(eye-p),N=normalize(sceneNormal),L=normalize(ocean.p[2].xyz);bool wetEye=eye.y<ocean.p[51].y;float maximumSea=ocean.p[0].y+ocean.p[128].w+ocean.p[50].w;vec2 optical=vec2(maximumSea,0);if(ocean.p[9].x<.5&&(ocean.p[128].y>.5||p.y<=maximumSea))optical=ocean.p[128].y>.5?vec2(oceanHeight(p.xz),0):oceanOpticalSurface(p.xz);float level=optical.x;
if(ocean.p[9].x>1.5){vec3 direction=normalize(p-eye);if(wetEye){vec3 rgb=waterInscatter(eye,direction);color=vec4(displayWater(rgb),1);return;}if(ocean.p[166].w>.5||direction.y>=-.00001)discard;
// A deep-water backing behind the transparent interface replaces the old
// rectangular known-geometry opacity mask. Actual underwater objects retain
// their depth, absorption and collision geometry in front of this backing.
float range=(oceanMean(eye.xz).x-eye.y)/direction.y;for(int i=0;i<4;++i){vec3 q=eye+direction*range;vec3 mean=oceanMean(q.xz);float derivative=direction.y-dot(mean.yz,direction.xz);if(abs(derivative)<.00001)break;range-=clamp((q.y-mean.x)/derivative,-20000.,20000.);}vec3 q=eye+direction*range;if(range<=0.||max(abs(q.x),abs(q.z))>ocean.p[51].x||abs(q.y-oceanMean(q.xz).x)>1.)discard;vec3 backing=waterAmbient()*.78;if(ocean.p[166].x>0.||range>1500.)backing=oceanAir(backing,q);float backingWeight=1.-smoothstep(ocean.p[51].x*.92,ocean.p[51].x,max(abs(q.x),abs(q.z)));color=vec4(displayWater(backing),backingWeight);return;}
if(ocean.p[9].x>.5){
float distance=length(eye-p);
// The screen ray remains continuous across mesh triangles and wave crests.
// A bounded tangent-plane projection supplies a stable optical footprint.
float footprint=distance*length(fwidth(V))/max(.085,abs(V.y));
float unresolvedSlopes;
vec4 resolved=oceanFilteredField(p.xz,footprint,unresolvedSlopes);
N=normalize(vec3(-resolved.y,1,-resolved.z));
float speed=ocean.p[7].w;
vec2 wind=speed>.001?ocean.p[167].xy/speed:vec2(1,0),across=vec2(-wind.y,wind.x);
vec2 advected=p.xz-ocean.p[219].yz;
vec2 windPosition=vec2(dot(advected,wind),dot(advected,across));
// Capillary detail is optical roughness only, below the resolved wave mesh.
vec2 detail=vec2(0);
if(ocean.p[166].w>.5){float frequency=1.8,weight=mix(.12,.055,smoothstep(15.,90.,distance));for(int octave=0;octave<3;++octave){float bandWeight=1.-smoothstep(.18,.55,footprint*frequency);detail+=rippleGradient(p.xz*frequency+ocean.p[0].x*vec2(.28,-.17))*weight*bandWeight;unresolvedSlopes+=.12*weight*weight*(1.-bandWeight*bandWeight);frequency*=2.17;weight*=.58;}}
else{
// Wind-driven capillary groups are aperiodic in two dimensions. Resolved
// slopes perturb reflection; unresolved energy becomes BRDF roughness below.
float frequency=.42,weight=.008+.18*smoothstep(.35,10.8,speed)+.08*smoothstep(8.,22.,speed);
for(int band=0;band<3;++band){float resolvedBand=1.-smoothstep(.25,1.1,footprint*frequency);
    vec2 uv=windPosition*frequency*vec2(1.,1.35)+float(band)*vec2(17.3,4.9);
    if(resolvedBand>0.){
        vec2 gradient=windSimplex(uv).yz;
        detail+=(wind*gradient.x+across*gradient.y*1.35)*weight*resolvedBand;
    }
    unresolvedSlopes+=.12*weight*weight*(1.-resolvedBand*resolvedBand);
    frequency*=3.11;weight*=.52;
}}

if(!wetEye&&ocean.p[166].w<.5&&distance<80.) {
    detail+=rainImpactSlope(p.xz,ocean.p[219].x,ocean.p[166].z,footprint)*(1.-smoothstep(42.,80.,distance));
}
N=normalize(N+vec3(detail.x,0,detail.y));
if(wetEye){vec3 facing=-N;float cosine=max(0.,dot(facing,V)),F=fresnel(cosine,ocean.p[6].w,1.);vec3 through=refract(-V,facing,ocean.p[6].w);vec3 reflection=waterAmbient()*.45;vec3 rgb=length(through)<.01?reflection:mix(sky(through,true,0.),reflection,F);vec3 transmission=waterTransmission(distance);rgb=rgb*transmission+waterInscatter(eye,normalize(p-eye))*(1.-transmission);color=vec4(displayWater(rgb),1);return;}
// The filtered normal already depends on the pixel footprint. Estimating its
// variance with screen derivatives would implicitly use undefined higher derivatives.
float variance=min(.30,unresolvedSlopes);
if(ocean.p[166].w<.5){
    float grazing=(1.-smoothstep(.025,.14,max(0.,V.y)))*smoothstep(1.5,14.,footprint);
    variance+=.28*grazing*dot(resolved.yz,resolved.yz);
    N=normalize(mix(N,vec3(0,1,0),.52*grazing));
}
float cosine=max(0.,dot(N,V)),F=fresnel(cosine,1.,ocean.p[6].w);vec3 reflected=sky(reflect(-V,N),false,variance);vec3 H=waterHalfVector(L,V,N);float baseRoughness=ocean.p[166].w>.5?.10+ocean.p[7].w*.007:ocean.p[165].z;float rough=waterSpecularRoughness(clamp(baseRoughness+footprint*.09+ocean.p[166].z*.12,.065,.48),variance);float ndl=max(.001,dot(N,L)),ndv=max(.001,dot(N,V));float a=rough*rough;float gv=2.*ndv/(ndv+sqrt(a*a+(1.-a*a)*ndv*ndv));float gl=2.*ndl/(ndl+sqrt(a*a+(1.-a*a)*ndl*ndl));float fh=fresnel(max(0.,dot(H,V)),1.,ocean.p[6].w);float spec=ggx(max(0.,dot(N,H)),rough)*gv*gl*fh/(4.*ndl*ndv+.0001);
vec3 glint=ocean.p[52].rgb*weatherBeam(L)*min(spec,60.)*max(0.,dot(N,L));if(ocean.p[166].w>.5){reflected+=ocean.p[52].rgb*min(spec,14.)*ndl;glint=vec3(0);}else if(dot(ocean.p[54].rgb,vec3(1))>1e-7){vec3 M=normalize(ocean.p[3].xyz),HM=waterHalfVector(M,V,N);float ml=max(.001,dot(N,M)),gm=2.*ml/(ml+sqrt(a*a+(1.-a*a)*ml*ml));float mf=fresnel(max(0.,dot(HM,V)),1.,ocean.p[6].w);float ms=ggx(max(0.,dot(N,HM)),rough)*gv*gm*mf/(4.*ml*ndv+.0001);glint+=ocean.p[54].rgb*weatherBeam(M)*min(ms,60.)*max(0.,dot(N,M));}
vec3 scatter=waterAmbient()*.78;
// Crest foam is intermittent and advected, not a sine painted along every
// long-wave ridge. Wake foam comes from checkpointed body-emitted packets.
float whitecapCoverage=ocean.p[165].w;
float breaking=smoothstep(.0003,.056,whitecapCoverage);
float downwindFace=max(0.,-dot(resolved.yz,wind));
float steepness=mix(length(resolved.yz),downwindFace,.36*breaking);
float crest=smoothstep(mix(.19,.09,breaking),mix(.34,.24,breaking),steepness+.20*length(detail));
float patchDetail=1.-smoothstep(.45,1.8,footprint*.58);
float foamPattern=.5;
if(patchDetail>0.&&(crest*breaking>0.||resolved.w>0.)){
    // Wind-elongated fronts with an irregular crosswind envelope and torn edges.
    vec2 foamUV=windPosition*vec2(mix(.46,.16,breaking),.58);
    float envelope=windSimplex(foamUV).x;
    float erosion=1.-smoothstep(.21,.74,footprint*1.5);
    float torn=erosion>0.?windSimplex(foamUV*2.37+vec2(9.6,17.1)).x*erosion:0.;
    foamPattern=clamp(.5+.67*envelope+.33*torn,0.,1.);
}
float whitecap=mix(whitecapCoverage,breaking*crest*smoothstep(.43,.74,foamPattern),patchDetail);
float aerated=smoothstep(.035,.24,resolved.w)*smoothstep(.22,.65,foamPattern);
float poreWeight=1.-smoothstep(.1,.52,footprint*3.8);
float pores=poreWeight>0.&&(whitecap>0.||aerated>0.)?windSimplex(advected*3.8+vec2(-.18,.23)*ocean.p[0].x).x+.5:.5;
float persistent=0.;if(ocean.p[217].w>.5){vec2 uv=(p.xz-ocean.p[217].xy)*ocean.p[217].z;if(all(greaterThanEqual(uv,vec2(0)))&&all(lessThanEqual(uv,vec2(1))))persistent=(1.-exp(-texture(foamMap,uv).r))*smoothstep(0.,.04,min(min(uv.x,uv.y),min(1.-uv.x,1.-uv.y)));}
float foam=clamp(max(max(max(whitecap*.64,aerated),persistent),ocean.p[166].w<.5?hullAeration(p.xz):0.)*(1.-.28*clamp(pores,0.,1.)*poreWeight),0.,.82);
float scatterWeight=.32;
if(ocean.p[166].w<.5){
    // The background channel has no seafloor depth. Allow close underwater
    // geometry through and converge to optically deep water at long range.
    vec3 extinction=ocean.p[6].rgb+ocean.p[7].rgb;
    float clearExtinction=min(extinction.x,min(extinction.y,extinction.z));
    float effectivePath=.10*max(0.,distance-45.)*(1.-.50*abs(V.y));
    scatterWeight=1.-(1.-scatterWeight)*exp(-clearExtinction*effectivePath);
}
float opacity=F+(1.-F)*scatterWeight;
vec3 rgb=(reflected*F+glint+scatter*(1.-F)*scatterWeight)/max(opacity,.001);
if(ocean.p[128].y<.5&&ocean.p[166].w>.5){float known=knownWaterGeometry(p.xz);rgb=mix(reflected*F+glint+scatter*(1.-F),rgb,known);opacity=mix(1.,opacity,known);}
vec3 foamLight=(ocean.p[53].rgb*1.6+ocean.p[52].rgb*weatherBeam(L)*max(.12,L.y)*.42+ocean.p[54].rgb*.8)*vec3(.9,.96,1.);
rgb=mix(rgb,foamLight,foam);opacity=mix(opacity,.97,foam);
float fog=0.;if(ocean.p[128].y>.5){fog=1.-exp(-distance*max(.0001,ocean.p[4].w*.001));rgb=mix(rgb,ocean.p[4].rgb,fog);}else rgb=oceanAir(rgb,p);float edge=1.-smoothstep(ocean.p[51].x*.92,ocean.p[51].x,max(abs(p.x),abs(p.z)));color=vec4(displayWater(rgb),max(fog*.9,opacity)*edge);return;}
vec3 albedo=texture(albedoMap,texcoord).rgb*ocean.p[8].rgb;float rough=.55,metal=0.;if(ocean.p[9].w>.5){vec4 mr=texture(mrMap,texcoord);rough=clamp(mr.g,.05,1.);metal=mr.b;}
if(ocean.p[9].z>.5){vec3 map=texture(normalMap,texcoord).xyz*2.-1.;vec3 q0=dFdx(p),q1=dFdy(p);vec2 st0=dFdx(texcoord),st1=dFdy(texcoord);vec3 T=q0*st1.y-q1*st0.y;if(length(T)>1e-5){T=normalize(T-N*dot(T,N));N=normalize(T*map.x+normalize(cross(N,T))*map.y+N*map.z);}}
bool wet=p.y<level;vec3 sunlight=ocean.p[52].rgb;vec3 ambient=ocean.p[53].rgb;float caustic=1.;if(wet){L=-refract(-L,vec3(0,1,0),1./ocean.p[6].w);float depth=level-p.y;sunlight*=waterTransmission(depth/max(.12,L.y))*.98;ambient*=waterTransmission(depth*.7);caustic=ocean.p[128].y>.5?waterCaustic(p):clamp(1.+optical.y*min(depth,8.)*.7,.45,2.3);rough=max(.08,rough*.7);}
vec3 H=waterHalfVector(L,V,N);float ndl=max(0.,dot(N,L)),ndv=max(.03,dot(N,V)),ndh=max(0.,dot(N,H));vec3 f0=mix(vec3(.04),albedo,metal);vec3 f=f0+(1.-f0)*pow(1.-max(0.,dot(H,V)),5.);vec3 spec=ggx(ndh,rough)*f/(4.*max(.02,ndl)*ndv+.05);vec3 rgb=albedo*(ambient*.8+sunlight*ndl*caustic*(1.-metal))+sunlight*spec*ndl*.3;rgb+=albedo*ocean.p[54].rgb*max(0.,dot(N,normalize(ocean.p[3].xyz)));rgb+=texture(emissiveMap,texcoord).rgb*mix(.08,2.,1.-smoothstep(-.02,.12,ocean.p[2].y));rgb=waterTransport(rgb,p,level);if(ocean.p[128].y<.5)rgb=oceanAir(rgb,p);color=vec4(displayWater(rgb),1);}
