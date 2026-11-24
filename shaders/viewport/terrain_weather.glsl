// Authored surface appearance only. No height, occupancy or sensor spectrum
// is changed; RGB canopy inference is not a measured vegetation classification.
float groundWeatherHash(ivec3 cell) {
    uvec3 p=uvec3(cell);
    uint h=p.x*1597334677u^p.y*3812015801u^p.z*2798796415u;
    h=(h^(h>>16))*2246822519u;
    return float((h^(h>>13))>>8)*(1./16777216.);
}
float groundWeatherNoise(vec3 p) {
    ivec3 i=ivec3(floor(p));
    vec3 f=fract(p);f=f*f*(3.-2.*f);
    return mix(mix(mix(groundWeatherHash(i),groundWeatherHash(i+ivec3(1,0,0)),f.x),
        mix(groundWeatherHash(i+ivec3(0,1,0)),groundWeatherHash(i+ivec3(1,1,0)),f.x),f.y),
        mix(mix(groundWeatherHash(i+ivec3(0,0,1)),groundWeatherHash(i+ivec3(1,0,1)),f.x),
        mix(groundWeatherHash(i+ivec3(0,1,1)),groundWeatherHash(i+ivec3(1,1,1)),f.x),f.y),f.z);
}
float groundWeatherBand(vec3 p,float footprint,float frequency) {
    float weight=1.-smoothstep(.25,.9,footprint*frequency);
    return weight>0.?mix(.5,groundWeatherNoise(p),weight):.5;
}
vec4 groundWeather(vec3 base,vec3 world,vec3 normal,vec3 up,float footprint,float wetness,float snow) {
    if(wetness<=0.&&snow<=0.)return vec4(base,0);
    float variation=groundWeatherBand(world*.014,footprint,.014);
    float incline=clamp(dot(normal,up),0.,1.);
    // Whole rock faces can shed snow. Ordinary slopes change its retention
    // gradually, so the outline is driven by drifts rather than triangle normals.
    float retention=smoothstep(.12,.73,incline);
    float canopy=clamp(5.*(base.g-.5*(base.r+base.b)),0.,.65);
    float drift=groundWeatherBand(world*.055+vec3(19.3,6.2,-11.7),footprint,.055);
    float deposit=mix(variation,drift,.36);
    float coverage=0.;
    if(snow>0.) {
        float edges=groundWeatherBand(world*.137+vec3(-9.6,19.2,4.2),footprint,.137);
        float threshold=.09+.67*deposit+.085*(edges-.5)+.14*canopy+.08*(1.-retention);
        float depth=smoothstep(threshold-.13,threshold+.19,snow);
        // A dusting leaves soil and vegetation readable, even in regions
        // where the precipitation history allows a nearly continuous cover.
        float dust=mix(.60,1.,smoothstep(.24,.76,snow));
        coverage=depth*dust*retention*(1.-mix(.79,.41,snow)*canopy);
    }
    // Wet slopes darken before a continuous reflective film forms. Water
    // collects mainly on flatter exposed ground as wetness increases.
    float bare=1.-coverage;
    float damp=wetness*(.68+.32*variation);
    float pooled=smoothstep(.33,.97,wetness)*smoothstep(.68,.99,incline);
    float film=bare*(.33*damp+.67*pooled*(.36+.64*deposit))*(1.-.36*canopy);
    // The terrain lighting pass scales imagery by 0.40. Keep effective
    // snow reflectance below one, with broad contrast even under grey light.
    vec3 snowColor=vec3(1.25,1.30,1.38)*(.76+.24*deposit)+.25*base;
    return vec4(mix(base*(1.-.29*damp),snowColor,coverage),film);
}
