#ifdef LUNAR_LUT
layout(set=1,binding=3) uniform sampler2D lunarMap;
#else
layout(set=1,binding=0) uniform sampler2D lunarMap;
#endif

struct ApparentDisc {
    vec3 direction;
    float verticalScale;
};

float refractionAngle(float geometricElevation,float altitude) {
    float degreesUp=degrees(geometricElevation);
    if(degreesUp>25.||degreesUp<-3.5||altitude>95000.)return 0.;
    // Standard pressure near-horizon approximation, scaled with observer height.
    float h=max(degreesUp,-1.);
    float bending=1.02/(60.*tan(radians(h+10.3/(h+5.11))));
    return radians(bending)*exp(-max(altitude,0.)/9500.)*
           smoothstep(-3.5,-1.,degreesUp)*(1.-smoothstep(17.,25.,degreesUp));
}

ApparentDisc apparentDisc(vec3 direction,float radius,float altitude,bool atmosphere) {
    ApparentDisc disc=ApparentDisc(direction,1.);
    if(atmosphere&&abs(direction.y)<.43) {
        float el=asin(direction.y);
        float observed=el+refractionAngle(el,altitude);
        float difference=refractionAngle(el+radius,altitude)-refractionAngle(el-radius,altitude);
        vec2 horizontal=normalize(direction.xz)*cos(observed);
        disc.direction=vec3(horizontal.x,sin(observed),horizontal.y);
        disc.verticalScale=clamp(1.+difference/(2.*radius),.74,1.08);
    }
    return disc;
}

vec3 discEast(vec3 direction) {
    vec3 vertical=abs(direction.y)<.9999 ? vec3(0,1,0) : vec3(0,0,1);
    return normalize(cross(vertical,direction));
}

vec2 discPosition(vec3 ray,ApparentDisc disc,float radius) {
    vec3 east=discEast(disc.direction);
    vec3 up=cross(disc.direction,east);
    return vec2(dot(ray,east),dot(ray,up)/disc.verticalScale)/sin(radius);
}

float discCoverage(float rho,float opticalBlur) {
    float raster=max(.001,.58*fwidth(rho));
    float aa=length(vec2(raster,opticalBlur));
    return 1.-smoothstep(1.-aa,1.+aa,rho);
}

vec3 solarDisplayLight(vec3 ray,vec3 direction,float radius,float altitude,bool atmosphere,vec3 viewTrans) {
    if(dot(ray,direction)<.97)return vec3(0);
    ApparentDisc disc=apparentDisc(direction,radius,altitude,atmosphere);
    float rho=length(discPosition(ray,disc,radius));
    float maximum=max(viewTrans.r,max(viewTrans.g,viewTrans.b));
    float lowSun=1.-smoothstep(-.022,.14,direction.y);
    float visibility=dot(viewTrans,vec3(.2126,.7152,.0722));
    float opticalDepth=-log(max(visibility,.002));
    float haze=atmosphere ? smoothstep(.08,2.1,opticalDepth) : 0.;
    float coverage=discCoverage(rho,(.019+.095*lowSun)*haze);
    float mu=sqrt(max(0.,1.-rho*rho));
    float limb=.31+.69*pow(mu,.72);
    // Compress the solar photosphere into the display highlight range. Keep
    // the chromatic extinction even when its uncompressed luminance clips.
    vec3 extinctionTint=pow(max(viewTrans/max(maximum,1.e-5),vec3(0)),vec3(.72));
    vec3 sunColor=mix(vec3(1.,.985,.952),extinctionTint,.80*lowSun+.12);
    float diskLevel=mix(5.4,2.0,lowSun)*smoothstep(.002,.23,maximum)*(1.-.19*haze);
    float angle=length(cross(ray,disc.direction));
    // A compact forward-scattering lobe wraps the apparent disk; the broad
    // aerosol component falls gradually into the existing atmospheric sky.
    float gap=max(rho-.59,0.);
    float narrow=1./pow(1.+gap*gap/(.91+.58*haze),1.75);
    float broad=1./pow(1.+pow(angle/.027,2.),1.38);
    float aureole=(.47*narrow+.20*broad)*haze*sqrt(maximum)*(.45+.55*lowSun);
    return sunColor*(diskLevel*coverage*limb)+mix(sunColor,vec3(1.,.942,.836),.22)*aureole;
}

vec3 meanLunarPole(float lst,float latitude) {
    // IAU mean lunar pole in the J2000 equatorial frame.
    const float poleRa=radians(269.9949),poleDec=radians(66.5392);
    float H=lst-poleRa;
    float d=sin(poleDec),c=cos(poleDec)*cos(H);
    return vec3(-cos(poleDec)*sin(H),d*sin(latitude)+c*cos(latitude),
                c*sin(latitude)-d*cos(latitude));
}

float linearLunarAlbedo(float encoded) {
    return encoded<=.04045 ? encoded/12.92 : pow((encoded+.055)/1.055,2.4);
}
float lunarElevation(vec2 uv) {
    return 21.*textureLod(lunarMap,uv,0.).g-10.;
}
const float LUNAR_RADIUS_KM=1737.4;
float lunarTerrainSun(vec2 uv,float height,float incidence,vec2 slopeDirection,vec2 angularScale,float span) {
    // Follow the sunlight along the local tangent and account for curvature
    // of the lunar surface. Only near the terminator can nearby relief block it.
    if(incidence>.23)return 1.;
    if(incidence<-.082)return 0.;
    float horizontal=max(length(slopeDirection),.001);
    vec2 texelsPerKm=slopeDirection/(angularScale*horizontal);
    float raySlope=incidence/horizontal;
    float blocking=-1.;
    const float distances[4]=float[4](14.,31.,58.,96.);
    for(int i=0;i<4;++i) {
        float d=distances[i];
        float obstacle=lunarElevation(uv+vec2(d,-d)*texelsPerKm);
        blocking=max(blocking,obstacle-height-raySlope*d-d*d/(2.*LUNAR_RADIUS_KM));
    }
    return 1.-smoothstep(.09,1.05+.55*span,blocking);
}

vec4 lunarDisplayDisc(vec3 ray,vec3 direction,vec3 solarDirection,vec3 pole,
                      float radius,float altitude,bool atmosphere) {
    if(dot(ray,direction)<.999)return vec4(0);
    ApparentDisc apparent=apparentDisc(direction,radius,altitude,atmosphere);
    vec2 p=discPosition(ray,apparent,radius);
    float rho=length(p);
    float pixelAngle=length(fwidth(ray));
    float opticalBlur=atmosphere ? .055*(1.-smoothstep(.025,.27,direction.y)) : 0.;
    float coverage=discCoverage(rho,opticalBlur);
    if(coverage<=0.)return vec4(0);
    vec3 east=discEast(direction),up=cross(direction,east);
    p/=max(1.,rho); // continue the limb colour across its filtered edge
    float mu=sqrt(max(0.,1.-dot(p,p)));
    vec3 normal=east*p.x+up*p.y-direction*mu;
    vec3 moonNorth=pole-direction*dot(pole,direction);
    moonNorth=dot(moonNorth,moonNorth)>1.e-6 ? normalize(moonNorth) : up;
    vec3 moonEast=normalize(cross(moonNorth,-direction));
    float sinLatitude=clamp(dot(normal,moonNorth),-1.,1.);
    vec2 uv=vec2(.5+atan(dot(normal,moonEast),max(mu,1.e-6))/6.283185307,
                 .5-asin(sinLatitude)/3.141592654);
    vec2 size=vec2(textureSize(lunarMap,0));
    float span=clamp(.62*pixelAngle*size.y/(radius*3.141592654),1.,6.);
    vec2 duv=span/size;
    float cosLatitude=max(.10,sqrt(max(0.,1.-sinLatitude*sinLatitude)));
    vec2 angularScale=3.141592654*LUNAR_RADIUS_KM*vec2(2.*cosLatitude,1.);
    vec2 slope=vec2(lunarElevation(uv+vec2(duv.x,0))-lunarElevation(uv-vec2(duv.x,0)),
                    lunarElevation(uv-vec2(0,duv.y))-lunarElevation(uv+vec2(0,duv.y)))/(2.*duv*angularScale);
    vec3 surfaceEast=cross(moonNorth,normal);
    surfaceEast=dot(surfaceEast,surfaceEast)>1.e-5 ? normalize(surfaceEast) : moonEast;
    vec3 surfaceNorth=cross(normal,surfaceEast);
    vec3 reliefNormal=normalize(normal-slope.x*surfaceEast-slope.y*surfaceNorth);
    vec2 terrain=texture(lunarMap,uv).rg;
    float albedo=linearLunarAlbedo(terrain.r);
    float incidence=dot(normal,solarDirection);
    float mu0=max(dot(reliefNormal,solarDirection),0.);
    float emission=max(dot(reliefNormal,-direction),.012);
    float shadow=lunarTerrainSun(uv,21.*terrain.g-10.,incidence,
                    vec2(dot(solarDirection,surfaceEast),dot(solarDirection,surfaceNorth)),angularScale,span);
    // The lunar regolith is backscattering, with gentle limb falloff at full
    // phase and stronger relief when the illumination is oblique.
    float reflectance=mix(2.*mu0/max(mu0+emission,.025),mu0,.24)*shadow;
    float litEarth=clamp(.5+.5*dot(direction,solarDirection),0.,1.);
    float earthshine=.011*litEarth*litEarth;
    vec3 radiance=vec3(1.,.978,.943)*(reflectance+earthshine)*albedo*2.65;
    return vec4(radiance*coverage,coverage);
}
