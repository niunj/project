#define CLOUD_VIEW_FILTER
#include "planet_shell.glsl"
#include "cloud_light_cache.glsl"
#include "cloud_light_field.glsl"
#include "../earth_atmosphere_optics.glsl"
#include "marine_fog.glsl"
#ifdef CLOUD_TEMPORAL_RAW
layout(location=1) out vec4 cloudMoments;
#endif
layout(set=0,binding=9) uniform sampler2D airTransmission;
vec2 shellInterval(vec4 shell,vec3 direction) {
    vec3 scale=vec3(1.,1.,shell.y)/shell.x;
    vec3 e=w.eyeCover.xyz*scale,d=direction*scale;
    float a=dot(d,d),b=dot(e,d),c=shell.z;
    float discriminant=b*b-a*c;
    if(discriminant<=0.) return vec2(-1.);
    // Use the double-precision constant uploaded with the shell and avoid
    // cancellation in the small root when the eye is close to a boundary.
    float root=sqrt(discriminant);
    float q=-b-(b>=0. ? root : -root);
    if(abs(q)<1.e-12) return vec2(-1.);
    vec2 hit=vec2(q/a,c/q)*CLOUD_A;
    return vec2(min(hit.x,hit.y),max(hit.x,hit.y));
}
float nextSampleBoundary(float start,float end,vec2 fineRange,float angularFootprint) {
    float farStep=min(2400.,180.+.016*max(0.,start-7300.));
    float stepLength=mix(26.,farStep,smoothstep(3700.,7300.,start));
    if(sparseCloud.midTopExtent.w>.5) {
        float verticalCell=(sparseCloud.midTopExtent.x-sparseCloud.midUpBase.w)/sparseCloud.midNorthSide.w;
        float regionalStep=verticalCell*mix(1.4,2.65,smoothstep(24000.,56000.,start));
        stepLength=min(stepLength,regionalStep);
    }
    if(start<fineRange.x)return min(min(end,fineRange.x),start+stepLength);
    if(start<fineRange.y) {
        // Both views integrate the same sparse condensate, with at least two
        // evaluations per one to two stored voxels even on a distant ray.
        float cell=sparseCloud.eastVoxel.w;
        float closeStep=mix(.29,.44,smoothstep(260.,850.,start));
        float fineStep=mix(closeStep,1.35,smoothstep(cell,4.*cell,angularFootprint*start))*cell;
        return min(min(end,fineRange.y),start+min(stepLength,fineStep));
    }
    return min(end,start+stepLength);
}
float cloudSkyDepth(vec3 point,vec3 direction,float distance,vec4 properties,float footprint) {
    float skyTau=0.;
    for(int i=0;i<6;++i) {
        float a=float(i)/6.,b=float(i+1)/6.;
        float lo=a*a*(.45+.55*a)*distance,hi=b*b*(.45+.55*b)*distance;
        skyTau+=cloudExtinctionPerMetre(
            cloudDensityAt(point+direction*(.5*(lo+hi)),w.advection,properties,
                           max(footprint,.20*(hi-lo))),properties.y)*(hi-lo);
        if(skyTau>=16.)break;
    }
    return min(skyTau,16.);
}
vec3 cloudLightDepth(vec3 point,vec4 properties,float footprint,float sideSkyWeight) {
    vec3 up=normalize(point);
    float mu=dot(up,w.sunWind.xyz);
    float solar=mu>-.02 ? cloudVisibilityFromPoint(point,w.sunWind.xyz,w.advection,properties,footprint) : 1.;
    float lunar=mu<.05 ? cloudVisibilityFromPoint(point,w.moon.xyz,w.advection,properties,footprint) : 1.;
    float height=clamp(properties.w-(length(point)-cloudRadius(up)),0.,2400.);
    float skyTau=cloudSkyDepth(point,up,height,properties,footprint);
    vec3 sunward=w.sunWind.xyz-mu*up;
    float horizontal=length(sunward);
    if(sideSkyWeight>0. && horizontal>.02 && mu>-.04) {
        // A zenith-only probe shades a thin side streamer as if the dense
        // cloud over it covered the entire sky. Sample the brighter, slanted
        // part of the sky dome and average transmission through both paths.
        vec3 slant=normalize(up+.96*sunward/horizontal);
        float sideTau=cloudSkyDepth(point,slant,height/dot(up,slant),properties,footprint);
        float skyTransmission=mix(exp(-.4*skyTau),exp(-.4*sideTau),sideSkyWeight);
        skyTau=-log(skyTransmission)/.4;
    }
    return vec3(-log(max(solar,1e-7)),skyTau,-log(max(lunar,1e-7)));
}
vec2 objectRayBox(vec3 eye,vec3 inverseDirection,vec3 lo,vec3 hi) {
    vec3 a=(lo-eye)*inverseDirection,b=(hi-eye)*inverseDirection;
    vec3 t0=min(a,b),t1=max(a,b);
    return vec2(max(t0.x,max(t0.y,t0.z)),min(t1.x,min(t1.y,t1.z)));
}
float objectNextSupport(vec3 eye,vec3 direction,int bin,float distance,float end) {
    float next=end;int count=objectCandidateCount(bin);
    for(int i=0;i<count;++i){
        int index=objectCandidate(bin,i);if(index<0)break;
        vec4 c=sparseCloud.objects[index*3],r=sparseCloud.objects[index*3+1];
        if(c.w<=0.)continue;
        vec2 heading=sparseCloud.objects[index*3+2].xy;
        vec3 origin=objectShapeSpace(eye-c.xyz,r.xyz,heading);
        vec3 slope=objectShapeSpace(direction,r.xyz,heading);
        vec3 inv=mix(vec3(-1.),vec3(1.),greaterThanEqual(slope,vec3(0.)))/max(abs(slope),vec3(1e-8));
        vec2 hit=objectRayBox(origin,inv,OBJECT_SHAPE_LO,OBJECT_SHAPE_HI);
        if(hit.y<=distance)continue;
        // Inside any conservative support, retain fine stepping through holes.
        if(hit.x<=distance)return distance;
        next=min(next,hit.x);
    }
    return next;
}
vec3 cloudSunlight(float solarMu,float height) {
    if(dot(w.sunWind.xyz,w.sunWind.xyz)<.5)return vec3(0);
    return earthDiskTransmission(airTransmission,height,solarMu,w.air.x>.5);
}
float cloudMoonStrength(vec3 up,vec3 moon,float phase,float sunMu) {
    return (earthDisplayMoon(phase,sunMu)/.039)*smoothstep(-.035,.19,dot(up,moon));
}
float cloudPhase(float g,float angleCosine) {
    return (1.-g*g)/pow(max(.01,1.+g*g-2.*g*angleCosine),1.5);
}
float cloudSolarPhase(float g,float viewSun) {
    // Retain the narrow backlit edge and a weak backward lobe on sunlit
    // faces. A smooth shoulder avoids a constant bright band around the Sun.
    float forward=cloudPhase(g,viewSun);
    return .90*forward/(1.+.16*forward)+.13*cloudPhase(-.26,viewSun);
}
vec3 cloudSkylight(float solarMu,float openSky,float lunar,float solarFacing,float storm) {
    float daylight=smoothstep(-.106,.098,solarMu);
    float warm=smoothstep(-.088,-.012,solarMu)*(1.-smoothstep(.018,.133,solarMu));
    float blueHour=smoothstep(-.273,-.093,solarMu)*(1.-smoothstep(-.09,.022,solarMu));
    float night=1.-smoothstep(-.227,-.067,solarMu);
    vec3 dayFill=vec3(.036,.043,.057)+vec3(.15,.18,.23)*openSky;
    vec3 stormFill=vec3(.024,.027,.033)+vec3(.091,.100,.113)*openSky;
    // Warm sky fill is limited to a clear path in the illuminated azimuth.
    // Most of the twilight colour still comes from the transmitted Sun.
    vec3 sunset=mix(vec3(.019,.012,.011),vec3(.068,.028,.020),solarFacing);
    return daylight*mix(dayFill,stormFill,storm)+
           warm*sunset*openSky*(1.-.72*storm)+
           blueHour*vec3(.010,.016,.026)*mix(openSky,sqrt(openSky),.5)*(1.-.42*storm)+
           night*(vec3(.003,.004,.006)+lunar*vec3(.010,.013,.019))*sqrt(openSky);
}
vec3 cloudMoonlight(vec3 up,vec3 direction,float height,float lunar) {
    if(lunar<=0.)return vec3(0);
    return .069*lunar*earthDiskTransmission(airTransmission,height,dot(up,direction),w.air.x>.5);
}
vec3 cloudAirRadiance(vec3 source,vec3 eye,vec3 ray,float distance) {
    if(w.air.x<.5)return source;
    EarthAirPath air=earthAirPath(airTransmission,eye,ray,distance,w.sunWind.xyz,w.moon.xyz,w.moon.w,w.lighting.y);
    return air.transmission*source+air.radiance;
}
// Integrate the same density used by shadows and sensors along the light ray.
// Increasing intervals cover adjacent billows, rather than two fixed probes.
float objectLightDepth(vec3 p,vec3 direction,float span,int steps,float detailWeight,bool fineNear) {
    float tau=0.;
    for(int i=0;i<steps;++i){float a=float(i)/float(steps),b=float(i+1)/float(steps);
        // Resolve nearby billows before spanning the gaps to adjacent clouds.
        float lo=span*a*a*a,hi=span*b*b*b;
        float offset=.5*(lo+hi);
        vec3 samplePoint=p+direction*offset;
        float rho=fineNear&&i==0 ? localObjectCloudDensity(samplePoint,detailWeight) : cachedObjectCloudDensity(samplePoint);
        tau+=cloudExtinctionPerMetre(rho,w.optics.x)*(hi-lo);
        if(tau>18.)break;
    }
    return min(tau,24.);
}
float cloudScattering(float solarTau,float openSky,float phase,float storm) {
    // Direct light still distinguishes neighbouring billows sharing a view
    // ray. Broad diffusion needs nearby condensate and falls off with depth.
    float direct=.60*phase*exp(-solarTau);
    float diffuse=exp(-.37*solarTau);
    float nearbyCondensate=1.-mix(diffuse,openSky,.53);
    float multiple=nearbyCondensate*(.38*diffuse+.21*exp(-.12*solarTau)*mix(.56,1.,openSky));
    return direct*mix(1.,.68,storm)+multiple*mix(1.,.65,storm);
}
vec3 cloudGroundLight(vec3 sunlight,float solarMu,float openSky,float storm,
                      float depthAboveBase,float verticalRadius,float extinction) {
    // The ground adds a weak diffuse source to the bottom of the cloud.
    // Approximate the short path to the condensation base using the local
    // extinction, which is already available from the view integration.
    float lowerCloud=1.-smoothstep(.20,.86,depthAboveBase/verticalRadius);
    float lowerTransmission=exp(-.56*extinction*min(260.,depthAboveBase));
    float daylight=smoothstep(-.025,.23,solarMu);
    vec3 skyOnGround=vec3(.085,.102,.125)*daylight;
    float exposedGround=mix(.24,.64,sqrt(openSky));
    vec3 sunOnGround=.16*max(solarMu,0.)*exposedGround*sunlight;
    // A representative diffuse terrain reflectance under surrounding sky.
    return vec3(.19,.20,.185)*(skyOnGround+sunOnGround)*
           (lowerCloud*lowerTransmission*(1.-.68*storm));
}
vec3 cloudMist(vec3 source,vec3 eye,vec3 ray,float distance) {
    if(w.fog.x<=0.)return source;
    float h0=earthAirHeight(eye)-w.fog.z,h1=earthAirHeight(eye+ray*distance)-w.fog.z;
    return marineFog(source,distance,h0,h1,w.fog.xy,w.fogLight.rgb);
}
void finishCloud(vec3 source,float alpha,float average,float front,vec3 eye,vec3 ray) {
    source=cloudMist(cloudAirRadiance(source,eye,ray,average),eye,ray,average);
#if defined(CLOUD_TEMPORAL_RAW) || defined(CLOUD_REFLECTION)
    color=vec4(source*alpha,alpha);
    #ifdef CLOUD_TEMPORAL_RAW
    cloudMoments=vec4(average*alpha,front*alpha,0.,0.);
    #endif
#else
    color=vec4(displayRadiance(source,w.lighting.x),alpha);
#endif
}
void renderCloudObjects(vec3 ecef,vec3 ray,float angularFootprint) {
    vec3 eye=vec3(w.air.z,w.air.w,w.windAxis.w);
    vec3 direction=objectAxes(ray);
    vec3 inverseDirection=mix(vec3(-1.),vec3(1.),greaterThanEqual(direction,vec3(0.)))/
                          max(abs(direction),vec3(1.e-6));
    float extent=sparseCloud.objectEastExtent.w;
    vec2 range=objectRayBox(eye,inverseDirection,
        vec3(-extent,sparseCloud.objectUpBase.w,-extent),vec3(extent,sparseCloud.objectNorthTop.w,extent));
    float cursor=max(range.x,.5);
    vec2 ground=shellInterval(vec4(1.,CLOUD_A/CLOUD_B,w.lighting.z,1.),ray);
    if(ground.x>0.)range.y=min(range.y,ground.x);
    if(range.y<=cursor)discard;
    vec3 up=sparseCloud.objectUpBase.xyz;
    float sunMu=dot(up,w.sunWind.xyz);
    float meanCloudHeight=.5*(sparseCloud.objectUpBase.w+sparseCloud.objectNorthTop.w);
    vec3 clearSun=cloudSunlight(sunMu,meanCloudHeight);
    float lunar=cloudMoonStrength(up,w.moon.xyz,w.moon.w,sunMu);
    bool lunarKey=sunMu<-.057&&lunar>0.;
    vec3 solarDirection=objectAxes(w.sunWind.xyz);
    vec3 keyDirection=lunarKey ? objectAxes(w.moon.xyz) : solarDirection;
    vec3 sideSkyDirection=normalize(vec3(keyDirection.x*2.,1.,keyDirection.z*2.));
    float viewSun=clamp(dot(ray,w.sunWind.xyz),-1.,1.);
    float phase=cloudSolarPhase(w.optics.y,viewSun);
    float solarFacing=smoothstep(-.12,.92,viewSun);
    float storm=clamp(w.air.y,0.,1.);
    float undersideView=smoothstep(-.22,.28,direction.y);
    bool twilight=sunMu<.14&&sunMu>-.30;
    vec3 moonlight=cloudMoonlight(up,w.moon.xyz,meanCloudHeight,lunar);
    float transmittance=1.,visibleDepth=-1.,distanceSum=0.;
    int dominantCloud=-1;float largestContribution=0.;
    vec3 lightSum=vec3(0.);
#ifdef CLOUD_LIGHT_FIELD_RENDER
    bool lightingResident=cloudLightingReady();
#endif
    float width=objectBinWidth();
    // Each region has one bounded candidate list. Empty cells and holes
    // between cloud objects are skipped before evaluating density or light.
    for(int cellIndex=0;cellIndex<2*OBJECT_BIN_SIDE+2&&cursor<range.y&&transmittance>.003;++cellIndex) {
        ivec2 cell=cloudObjectBin(eye+direction*(cursor+.1));
        if(!validObjectBin(cell))break;
        vec2 edge=(vec2(cell)+mix(vec2(0.),vec2(1.),greaterThanEqual(direction.xz,vec2(0.))))*width-extent;
        vec2 exits=(edge-eye.xz)*inverseDirection.xz;
        float next=min(range.y,min(exits.x,exits.y));
        if(next<=cursor)break;
        int bin=cloudObjectList(cell),count=objectCandidateCount(bin);
        float first=next,last=cursor;
        for(int i=0;i<count;++i) {
            int index=objectCandidate(bin,i);
            if(index<0)break;
            vec4 c=sparseCloud.objects[index*3],r=sparseCloud.objects[index*3+1];
            if(c.w<=0.)continue;
            vec3 support=vec3(r.w,1.95*r.y,r.w);
            vec2 hit=objectRayBox(eye,inverseDirection,c.xyz-support,c.xyz+support);
            vec2 clipped=vec2(max(cursor,hit.x),min(next,hit.y));
            if(clipped.y<=clipped.x)continue;
            vec2 heading=sparseCloud.objects[index*3+2].xy;
            vec3 origin=objectShapeSpace(eye-c.xyz,r.xyz,heading);
            vec3 slope=objectShapeSpace(direction,r.xyz,heading);
            vec3 inv=mix(vec3(-1.),vec3(1.),greaterThanEqual(slope,vec3(0.)))/max(abs(slope),vec3(1.e-8));
            vec2 tight=objectRayBox(origin,inv,OBJECT_SHAPE_LO,OBJECT_SHAPE_HI);
            clipped=vec2(max(clipped.x,tight.x),min(clipped.y,tight.y));
            if(clipped.y>clipped.x){first=min(first,clipped.x);last=max(last,clipped.y);}
        }
        for(int sampleIndex=0;sampleIndex<384&&first<last&&transmittance>.003;++sampleIndex) {
            float baseStep=mix(34.,190.,smoothstep(1800.,20000.,first));
            float dt=min(last-first,mix(10.,baseStep,smoothstep(180.,900.,first)));
            float distance=first+.5*dt;
            vec3 p=eye+direction*distance;
            float footprint=max(distance*angularFootprint,.65*dt);
            // Keep resolved wisps in the middle distance. A fixed 1.5 km
            // cutoff erased them even when their projection spanned pixels.
            float detailWeight=1.-smoothstep(18.,82.,footprint);
            int sampleCloud;
            float rho=objectDensityAndId(p,bin,detailWeight,sampleCloud);
            first+=dt;
            if(rho<1.e-5){
                float nextSupport=objectNextSupport(eye,direction,bin,distance,last);
                if(nextSupport>distance+.01)first=nextSupport;
                continue;
            }
            float extinction=cloudExtinctionPerMetre(rho,w.optics.x);
            float absorb=1.-exp(-extinction*dt);
            float contribution=transmittance*absorb;
            if(contribution>largestContribution){largestContribution=contribution;dominantCloud=sampleCloud;}
            // Bulk density represents the averaged light paths. The local
            // optical depth still responds to fine condensate on the view ray.
            float distanceToTop=max(0.,sparseCloud.objectNorthTop.w-p.y);
            float lightSpan=min(16000.,max(2000.,distanceToTop/max(.12,abs(keyDirection.y))));
#ifdef CLOUD_LIGHT_FIELD_RENDER
            vec3 depths=lightingResident?objectCachedLight(p,sampleCloud):vec3(
                bulkLightDepth(p,keyDirection,lightSpan,2,w.optics.x),
                bulkLightDepth(p,vec3(0,1,0),max(30.,distanceToTop),2,w.optics.x),
                bulkLightDepth(p,sideSkyDirection,max(30.,distanceToTop/sideSkyDirection.y),2,w.optics.x));
            // The atlas carries smooth bulk self/neighbor shadowing. Restore
            // resolved near-wisp extinction with the already measured density,
            // keeping the high-frequency silhouette/opacity sampling intact.
            float nearWeight=1.-smoothstep(300.,1100.,distance);
            if(nearWeight>0.){
                float envelope=smoothstep(0.,5000.,extent-max(abs(p.x),abs(p.z)));
                float difference=rho-cachedObjectCondensate(p,sampleCloud)*envelope;
                float correction=sign(difference)*cloudExtinctionPerMetre(abs(difference),w.optics.x)*nearWeight;
                depths=max(vec3(0),depths+correction*vec3(min(12.,lightSpan/216.),min(18.,distanceToTop/64.),min(18.,distanceToTop/sideSkyDirection.y/64.)));
            }
#else
            vec3 depths=vec3(objectLightDepth(p,keyDirection,lightSpan,6,detailWeight,true),
                objectLightDepth(p,vec3(0,1,0),max(30.,distanceToTop),4,0.,false),
                objectLightDepth(p,sideSkyDirection,max(30.,distanceToTop/sideSkyDirection.y),4,0.,false));
#endif
            // Diffuse light reaches a billow through the slanted sky as well
            // as the zenith. Average transmission, never optical depth.
            float openSky=mix(exp(-.61*depths.y),exp(-.61*depths.z),.30);
            float scattering=cloudScattering(depths.x,openSky,phase,storm);
            // In chart coordinates the surface normal changes to first order
            // with horizontal displacement. This matters close to sunset.
            float localSunMu=twilight ? sunMu+dot(p.xz,solarDirection.xz)/CLOUD_A : sunMu;
            vec3 sunlight=twilight ? cloudSunlight(localSunMu,p.y+dot(p.xz,p.xz)/2./CLOUD_A) : clearSun;
            vec3 sky=cloudSkylight(localSunMu,openSky,lunar,solarFacing,storm);
            if(undersideView>0. && localSunMu>-.025) {
                float radius=sparseCloud.objects[sampleCloud*3+1].y;
                float base=sparseCloud.objects[sampleCloud*3].y-.82*radius;
                sky+=undersideView*cloudGroundLight(sunlight,localSunMu,openSky,storm,
                                                    max(0.,p.y-base),radius,extinction);
            }
            float moonTransmission=lunarKey ? exp(-.66*depths.x) : openSky;
            lightSum+=contribution*(sunlight*scattering+sky+moonTransmission*moonlight);
            if(visibleDepth<0.&&1.-transmittance*(1.-absorb)>.008)visibleDepth=distance;
            distanceSum+=contribution*distance;
            transmittance*=1.-absorb;
        }
        cursor=next;
    }
    float alpha=1.-transmittance;
    if(alpha<.002)discard;
    float average=distanceSum/alpha;
    float depth=visibleDepth>0. ? visibleDepth : average;
    gl_FragDepth=clamp(-depthCoefficients.x+depthCoefficients.y*length(rayECEF)/depth,0.,1.);
    vec3 source=lightSum/alpha;
    float below=1.-smoothstep(sparseCloud.objectUpBase.w,sparseCloud.objectNorthTop.w,eye.y);
    source=mix(source,vec3(dot(source,vec3(.2126,.7152,.0722))),.43*below*w.air.y);
    // Thick, rain-bearing cloud bases receive less diffuse light. The same
    // optical density and silhouette are used when viewed from the side.
    source*=1.-.24*w.detail.w*w.air.y*below;
    finishCloud(source,alpha,average,depth,ecef,ray);
#ifdef CLOUD_TEMPORAL_RAW
    cloudMoments.z=float(dominantCloud+1);
#endif
}
#ifdef CLOUD_REFLECTION
#include "weather_reflection.glsl"
layout(location=2) in vec2 probeUV;
#endif
void main() {
#ifdef CLOUD_REFLECTION
    vec3 direction=reflectionDirection(probeUV,w.eyeCover.xyz*CLOUD_A);
#else
    vec3 direction=normalize(rayECEF);
#endif
    // Evaluate derivatives before per-ray exits diverge across a pixel quad.
    float angularFootprint=length(fwidth(direction));
    if(w.cloud.w==0. || w.cloudBase.w==0. || w.cloud.x<=w.cloudBase.x)discard;
    if(useCloudObjects()) {
        renderCloudObjects(w.eyeCover.xyz*CLOUD_A,direction,angularFootprint);
        return;
    }
#ifdef CLOUD_TEMPORAL_RAW
    // CloudTemporal only enables this pass for single-cloud objects. Keep the
    // independent sparse-voxel marcher out of this pipeline's register budget.
    discard;
#else
    vec2 outer=shellInterval(w.cloud,direction);
    if(outer.y<=0.)discard;
    float entry=max(outer.x,.5),leave=outer.y;
    vec2 ground=shellInterval(vec4(1.,CLOUD_A/CLOUD_B,w.lighting.z,1.),direction);
    if(ground.x>0.) leave=min(leave,ground.x);
    if(leave<=entry) discard;

    // March both cloud intervals around the clear interior. Keeping only
    // the first made the far bank disappear at the inner shell tangent,
    // even on rays with no ground intersection.
    vec2 inner=shellInterval(w.cloudBase,direction);
    float firstEnd=leave,farEntry=leave;
    if(inner.y>entry && inner.x<leave) {
        if(inner.x<=entry) entry=max(entry,inner.y);
        else {
            firstEnd=inner.x;
            farEntry=min(inner.y,leave);
        }
    }
    if(firstEnd<=entry) discard;
    vec3 eye=w.eyeCover.xyz*CLOUD_A;
    vec2 fineRange=sparseChartInterval(eye,direction);
    float eyeAltitude=length(eye)-cloudRadius(normalize(eye));
    float maxTravel=mix(120000.,300000.,smoothstep(20000.,200000.,eyeAltitude));
    firstEnd=min(firstEnd,entry+maxTravel);
    float farEnd=min(leave,farEntry+max(0.,maxTravel-(firstEnd-entry)));
    vec4 properties=vec4(w.eyeCover.w,w.optics.x,w.optics.zw);
    float end=firstEnd,cursor=entry;
    float transmittance=1.;
    float visibleDepth=-1.;
    float absorbedDistance=0.;
    vec3 radiance=vec3(0.);
    vec3 lightDepth=vec3(0.),lightDepthNext=vec3(0.);
    float shadowStartT=0.,shadowEndT=0.;
    int shadowEndIndex=-1;
    float viewSun=clamp(dot(direction,w.sunWind.xyz),-1.,1.);
    float solarFacing=smoothstep(-.12,.92,viewSun);
    float solarPhase=cloudSolarPhase(w.optics.y,viewSun);
    float storm=clamp(w.air.y,0.,1.);
    // No temporal accumulation is present. Frame-counter jitter therefore
    // changes a paused scene and exposes a stipple pattern at cloud edges.
    // Near and far both sample two fixed points in each metric segment.
    int maxSteps=sparseCloud.midTopExtent.w>.5 ? 1024 : 720;
    bool dense=sparseCloud.midTopExtent.w>.5;
    for(int i=0;i<maxSteps && transmittance>=.001;++i) {
        if(cursor>=end) {
            if(end>=farEnd || farEntry>=farEnd) break;
            cursor=farEntry;
            end=farEnd;
            shadowEndIndex=-1;
        }
        float s0=cursor,s1=nextSampleBoundary(cursor,end,fineRange,angularFootprint);
        cursor=s1;
        float dt=s1-s0;
        float t=.5*(s0+s1);
        vec3 p=eye+direction*t;
        float footprint=max(angularFootprint*t,.45*dt);
        float density;
        float densityT=t;
        {
            // Two-point Gauss-Legendre integration resolves a cloud boundary
            // crossing inside a long segment without stochastic stipple. This
            // is also the stable input needed by a later temporal history pass.
            const float ga=.2113248654,gb=.7886751346;
            float ta=mix(s0,s1,ga);
            float tb=mix(s0,s1,gb);
            float da=cloudDensityAt(eye+direction*ta,w.advection,properties,footprint*.7);
            float db=cloudDensityAt(eye+direction*tb,w.advection,properties,footprint*.7);
            density=.5*(da+db);
            densityT=(ta*da+tb*db)/max(da+db,1e-12);
        }
        if(density<=0.)continue;
        float extinction=cloudExtinctionPerMetre(density,w.optics.x)*dt;
        if(extinction<2e-5)continue;
        vec3 n=normalize(p);
        // Clear-air samples are skipped above. A modulo test on the march
        // index can therefore miss the cache refresh entirely and carry the
        // shadow of a different cloud into this one. Cache physical sample
        // positions and reseed whenever a gap has passed the look-ahead.
        bool resolved=t>=fineRange.x && t<fineRange.y;
        vec3 tau;
        if(resolved) {
            // A lighting cache referenced to the eye forms moving rings when
            // a nearby billow's optical depth changes within one cache span.
            float sideSkyWeight=.48*(1.-smoothstep(400.,2200.,densityT));
            tau=cloudLightDepth(eye+direction*densityT,properties,footprint,sideSkyWeight);
            shadowEndIndex=-1;
        } else {
            if(shadowEndIndex<0 || i>=shadowEndIndex) {
                float sideSkyWeight=dense ? .29*(1.-smoothstep(2400.,17000.,t)) : 0.;
                lightDepth=i==shadowEndIndex ? lightDepthNext : cloudLightDepth(p,properties,footprint,sideSkyWeight);
                float fs0=s0,fs1=s1;
                int futureIndex=i;
                for(int j=0;j<4 && fs1<end;++j) {
                    fs0=fs1;
                    fs1=nextSampleBoundary(fs0,end,fineRange,angularFootprint);
                    ++futureIndex;
                }
                shadowStartT=t;
                shadowEndT=.5*(fs0+fs1);
                shadowEndIndex=futureIndex;
                vec3 futurePoint=eye+direction*shadowEndT;
                float futureFootprint=max(angularFootprint*shadowEndT,.45*(fs1-fs0));
                float nextSide=dense ? .29*(1.-smoothstep(2400.,17000.,shadowEndT)) : 0.;
                lightDepthNext=cloudLightDepth(futurePoint,properties,futureFootprint,nextSide);
            }
            // Interpolate between the cached light samples in metres.
            float shadowMix=clamp((t-shadowStartT)/max(shadowEndT-shadowStartT,.001),0.,1.);
            tau=mix(lightDepth,lightDepthNext,shadowMix);
        }
        float solarMu=dot(n,w.sunWind.xyz);
        float lightAltitude=length(p)-cloudRadius(n);
        vec3 solar=cloudSunlight(solarMu,lightAltitude);
        float moon=cloudMoonStrength(n,w.moon.xyz,w.moon.w,solarMu);
        vec3 lunar=cloudMoonlight(n,w.moon.xyz,lightAltitude,moon)*exp(-.66*tau.z);
        float openSky=exp(-.61*tau.y);
        vec3 diffuse=cloudSkylight(solarMu,openSky,moon,solarFacing,storm);
        vec3 source=solar*cloudScattering(tau.x,openSky,solarPhase,storm)+diffuse+lunar*.65;
        // The coarse orbital representation averages optically thick
        // cloudlets over clear space. Apply its radiance closure by spatial
        // footprint, never by an abrupt density threshold within a cloud.
        float coarse=smoothstep(4500.,19000.,footprint);
        vec3 coarseSource=solar*(.52+.10*min(solarPhase,4.))+diffuse+lunar*.65;
        source=mix(source,coarseSource,coarse);
        float absorbed=extinction<.001 ? extinction*(1.-.5*extinction) : 1.-exp(-extinction);
        if(visibleDepth<0. && transmittance*(1.-absorbed)<=.992) {
            float depthFraction=log(max(transmittance,.992)/.992)/extinction;
            visibleDepth=mix(s0,s1,clamp(depthFraction,0.,1.));
        }
        float contribution=transmittance*absorbed;
        radiance+=contribution*source;
        absorbedDistance+=contribution*t;
        transmittance*=1.-absorbed;
    }
    float alpha=1.-transmittance;
    if(alpha<.002)discard;
    // Test against the first visible condensate, rather than the often empty
    // outer shell. This prevents an empty foreground interval from giving a
    // cloud behind a mountain the depth of the air in front of it.
    float depth=visibleDepth>0. ? visibleDepth : absorbedDistance/alpha;
    gl_FragDepth=clamp(-depthCoefficients.x+depthCoefficients.y*length(rayECEF)/depth,0.,1.);
    // Integral radiance is premultiplied by absorption. The VSG blend state
    // expects straight alpha, so undo that factor before display mapping.
    float visibleDistance=absorbedDistance/max(alpha,.002);
    finishCloud(radiance/max(alpha,.002),alpha,visibleDistance,depth,eye,direction);
#endif
}
