#ifdef LUNAR_LUT
layout(set=1,binding=4) uniform sampler2D starMap;
#else
layout(set=1,binding=1) uniform sampler2D starMap;
#endif

vec3 enuToCelestial(vec3 ray,float lst,float latitude) {
    float north=-ray.z;
    float cosHour=ray.y*cos(latitude)-north*sin(latitude);
    float sinHour=-ray.x;
    return vec3(cosHour*cos(lst)+sinHour*sin(lst),cosHour*sin(lst)-sinHour*cos(lst),
                ray.y*sin(latitude)+north*cos(latitude));
}

float galacticHash(vec3 p) {
    p=fract(p*.1031);
    p+=dot(p,p.yzx+31.32);
    return fract((p.x+p.y)*p.z);
}
float galacticNoise(vec3 p) {
    vec3 i=floor(p),t=smoothstep(vec3(0),vec3(1),fract(p));
    vec4 lo=vec4(galacticHash(i),galacticHash(i+vec3(1,0,0)),
                 galacticHash(i+vec3(0,1,0)),galacticHash(i+vec3(1,1,0)));
    i.z+=1.;
    vec4 hi=vec4(galacticHash(i),galacticHash(i+vec3(1,0,0)),
                 galacticHash(i+vec3(0,1,0)),galacticHash(i+vec3(1,1,0)));
    vec4 n=mix(lo,hi,t.z);
    return mix(mix(n.x,n.y,t.x),mix(n.z,n.w,t.x),t.y);
}

vec3 celestialSky(vec3 ray,vec3 viewTrans,vec3 scattered,bool atmosphere,float lunarCoverage) {
    // In vacuum there is no twilight. The solar disc and the display still
    // mask nearby stars through the local radiance term below.
    float solarElevation=atmosphere ? env.sun.w : -18.;
    if(solarElevation>-3.5)return vec3(0);
    vec3 c=enuToCelestial(ray,env.astro.x,env.astro.y);
    vec2 uv=vec2(atan(c.y,c.x)*.159154943,.5-asin(clamp(c.z,-1.,1.))*.318309886);
    vec2 dx=dFdx(uv),dy=dFdy(uv);
    dx.x-=round(dx.x);
    dy.x-=round(dy.x);
    vec3 stars=textureGrad(starMap,uv,dx,dy).rgb;

    float dark=1.-smoothstep(-17.5,-6.,solarElevation);
    float brightStars=1.-smoothstep(-10.,-3.5,solarElevation);
    float bright=smoothstep(.13,.55,max(stars.r,max(stars.g,stars.b)));
    float moonPhase=clamp(env.moon.w,0.,1.);
    float lunar=moonPhase*moonPhase*smoothstep(-1.,14.,env.astro.z);
    float moonRadius=max(env.diskOptics.y,.0001);
    vec3 apparentMoon=apparentDisc(env.moon.xyz,moonRadius,env.misc.z,atmosphere).direction;
    float moonAngle=acos(clamp(dot(ray,apparentMoon),-1.,1.));
    float halo=1./pow(1.+pow(moonAngle/.108,2.),1.32);
    float glare=1./pow(1.+pow(max(0.,moonAngle-.64*moonRadius)/.012,2.),1.52);
    // Resolve the fading blue-hour background between the low-solar LUT
    // slices and the steady night glow.
    float twilight=atmosphere ? smoothstep(-16.,-8.,solarElevation)*
                                (1.-smoothstep(-6.,-3.5,solarElevation)) : 0.;
    float background=dot(scattered,vec3(.2126,.7152,.0722))+.023*twilight;
    float wash=1./(1.+lunar*(1.05+3.2*halo)+10.*background);
    stars*=2.13*mix(dark,brightStars,bright)*wash;

    // Unresolved galactic light, with the dust lane and the brighter bulge
    // near the galactic centre. Resolved bright stars come from the catalog.
    const vec3 GALACTIC_POLE=vec3(-.8677,-.1981,.4560);
    const vec3 GALACTIC_CENTRE=vec3(-.0549,-.8734,-.4838);
    float galLat=dot(c,GALACTIC_POLE);
    float bulge=pow(max(dot(c,GALACTIC_CENTRE),0.),8.);
    float structure=galacticNoise(c*15.+4.);
    float dust=exp(-pow((galLat+.022*(structure-.5))/.024,2.));
    float band=exp(-pow(galLat/(.067+.038*bulge),2.));
    vec3 milkyWay=vec3(.76,.79,.85)*band*(.017+.079*bulge)*(.35+.65*structure)*(1.-.60*dust);
    float meanTrans=dot(viewTrans,vec3(.2126,.7152,.0722));
    vec3 starTrans=mix(vec3(meanTrans),viewTrans,.65);
    vec3 result=(stars+milkyWay*dark*wash)*starTrans*(1.-lunarCoverage);
    if(atmosphere) {
        float night=1.-smoothstep(-14.,-4.,solarElevation);
        float horizon=exp(-5.*abs(ray.y));
        vec3 airglow=vec3(.0035,.005,.007)*(1.+.45*horizon);
        float aerosol=smoothstep(.045,1.55,-log(max(meanTrans,.002)));
        vec3 moonColor=mix(vec3(.94,.97,1.),vec3(1.,.91,.78),.52*aerosol);
        // Atmospheric moonlight comes from the same transport used by day
        // sky. Keep only the unresolved near-disc scattering here.
        vec3 moonGlow=moonColor*lunar*.018*glare*(.24+.76*aerosol);
        vec3 residualTwilight=vec3(.010,.018,.034)*twilight*(1.+.26*horizon);
        result+=((airglow+moonGlow)*night+residualTwilight)*sqrt(viewTrans);
    }
    return result;
}
