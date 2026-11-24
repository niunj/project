#ifndef EARTH_ATMOSPHERE_OPTICS
#define EARTH_ATMOSPHERE_OPTICS

const float EARTH_AIR_TOP=100000.;
const float EARTH_AIR_SOLAR_SCALE=20.;
const vec3 EARTH_AIR_RAYLEIGH=vec3(5.802e-6,13.558e-6,33.1e-6);
const vec3 EARTH_AIR_OZONE=vec3(.650e-6,1.881e-6,.085e-6);
const float EARTH_AIR_MIE_SCATTER=3.996e-6;
const float EARTH_AIR_MIE_EXTINCT=4.444e-6;

vec3 earthAirTransmission(sampler2D table,float height,float zenithCosine) {
    vec2 size=vec2(textureSize(table,0));
    vec2 uv=vec2(.5+.5*zenithCosine,sqrt(clamp(height/EARTH_AIR_TOP,0.,1.)));
    return textureLod(table,(clamp(uv,0.,1.)*(size-1.)+.5)/size,0.).rgb;
}

float earthRayleighPhase(float nu) { return (3./50.26548246)*(1.+nu*nu); }
float earthMiePhase(float nu,float g) {
    float g2=g*g;
    return 3.*(1.-g2)*(1.+nu*nu)/(25.13274123*(2.+g2)*pow(max(1.e-6,1.+g2-2.*g*nu),1.5));
}

// Appearance exposure for a moonlit scene. The sensor irradiance ratio is
// maintained in the physical lighting service; this gain is display only.
float earthDisplayMoon(float phase,float solarMu) {
    phase=clamp(phase,0.,1.);
    return .039*phase*phase*(1.-smoothstep(-.155,-.033,solarMu));
}

vec3 earthAirUp(vec3 p) {
    return normalize(p*vec3(1.,1.,1.0067394967));
}
float earthAirHeight(vec3 p) {
    vec3 n=normalize(p);
    return length(p)-6378137.*inversesqrt(dot(n.xy,n.xy)+1.0067394967*n.z*n.z);
}

// A ground receiver and a high cloud see different portions of the source
// at the terminator. Keep the partial solar or lunar disk continuous there.
vec3 earthDiskTransmission(sampler2D table,float height,float mu,bool air) {
    float r=6371000.+max(height,0.);
    float dip=sqrt(max(0.,1.-6371000.*6371000./(r*r)));
    float coverage=smoothstep(-.00465,.00465,mu+dip);
    if(coverage<=0.)return vec3(0);
    return coverage*(air ? earthAirTransmission(table,height,max(mu,-dip+.003)) : vec3(1));
}

struct EarthAirPath { vec3 transmission; vec3 radiance; };

EarthAirPath earthAirPath(sampler2D table,vec3 eye,vec3 ray,float distance,
                          vec3 sun,vec3 moon,float phase,float turbidity) {
    EarthAirPath result=EarthAirPath(vec3(1),vec3(0));
    if(distance<1.)return result;
    float r=length(eye);
    float atmosphereRadius=r+EARTH_AIR_TOP-earthAirHeight(eye);
    if(r>atmosphereRadius) {
        float b=dot(eye,ray), c=(r-atmosphereRadius)*(r+atmosphereRadius);
        float disc=b*b-c;
        if(disc<=0.||b>=0.)return result;
        float entry=c/(-b+sqrt(disc));
        if(entry>=distance)return result;
        eye+=ray*entry;
        distance-=entry;
    }
    const float stops[7]=float[7](0.,.065,.25,.5,.75,.935,1.);
    float sunNu=clamp(dot(ray,sun),-1.,1.);
    float moonNu=clamp(dot(ray,moon),-1.,1.);
    vec2 sunPhase=vec2(earthRayleighPhase(sunNu),earthMiePhase(sunNu,.76));
    vec2 moonPhase=vec2(earthRayleighPhase(moonNu),earthMiePhase(moonNu,.76));
    for(int i=0;i<6;++i) {
        float stepSize=distance*(stops[i+1]-stops[i]);
        vec3 p=eye+ray*(distance*.5*(stops[i+1]+stops[i]));
        float height=max(0.,earthAirHeight(p));
        if(height>EARTH_AIR_TOP)continue;
        vec3 up=earthAirUp(p);
        float solarMu=dot(up,sun);
        vec3 molecular=EARTH_AIR_RAYLEIGH*exp(-height/8500.);
        float dust=clamp(turbidity,.35,6.)*exp(-height/1200.);
        float ozone=(height-25000.)/9000.;
        vec3 extinction=molecular+EARTH_AIR_MIE_EXTINCT*dust+
                        EARTH_AIR_OZONE*exp(-ozone*ozone);
        float aerosol=EARTH_AIR_MIE_SCATTER*dust;
        vec3 scatter=vec3(0);
        if(dot(sun,sun)>.5) {
            vec3 beam=earthDiskTransmission(table,height,solarMu,true);
            scatter+=(molecular*sunPhase.x+aerosol*sunPhase.y)*beam;
            // Low-order diffuse fill from the rest of the illuminated sky.
            // Its blue-hour tail decays as the Earth's shadow rises.
            float diffuse=smoothstep(-.245,.17,solarMu)*.018;
            scatter+=(molecular+aerosol)*diffuse*vec3(.76,.86,1.);
        }
        float lunar=earthDisplayMoon(phase,solarMu);
        if(lunar>1.e-5)
            scatter+=(molecular*moonPhase.x+aerosol*moonPhase.y)*lunar*
                      earthDiskTransmission(table,height,dot(up,moon),true);
        vec3 segment=exp(-extinction*stepSize);
        result.radiance+=result.transmission*EARTH_AIR_SOLAR_SCALE*scatter*(1.-segment)/max(extinction,vec3(1.e-8));
        result.transmission*=segment;
    }
    return result;
}

#endif
