// Small rain impacts perturb only the optical surface normal. Each pair of
// grids has independent drop timing and resolves into existing rain roughness
// once the ripple becomes too small for the pixel footprint.
vec3 rainImpactHash(ivec2 cell,uint layer) {
    uvec2 q=uvec2(cell);
    uint bits=q.x*1597334677u^q.y*3812015801u^layer*2798796415u;
    bits=(bits^(bits>>16))*2246822519u;
    bits^=bits>>13;
    return vec3(uvec3(bits,bits>>10,bits>>20)&1023u)/1024.;
}
vec2 rainImpactSlope(vec2 point,float seconds,float strength,float footprint) {
    float resolved=1.-smoothstep(.035,.14,footprint);
    if(strength<=0.||resolved<=0.)return vec2(0);
    vec2 slope=vec2(0);
    for(uint layer=0u;layer<2u;++layer) {
        float span=layer==0u?.85:1.18;
        vec2 tile=point/span+float(layer)*vec2(.43,.67);
        vec3 random=rainImpactHash(ivec2(floor(tile)),layer);
        float cycle=mod(seconds+random.z*4.,256.);
        float event=floor(cycle);
        random=rainImpactHash(ivec2(floor(tile)),layer+uint(event)*3u);
        float age=fract(cycle);
        float probability=smoothstep(random.z,random.z+.22,sqrt(strength));
        if(age>.76||probability<=0.)continue;
        vec2 displacement=(fract(tile)-mix(vec2(.36),vec2(.64),random.xy))*span;
        float radius=length(displacement);
        float outward=(.17+.10*random.y)*age;
        float wavelength=.044+.031*random.x;
        float phase=(radius-outward)/wavelength;
        float lifetime=smoothstep(0.,.10,age)*(1.-smoothstep(.37,.76,age));
        float band=exp(-.72*phase*phase)*sin(6.2831853*phase)*(1.-smoothstep(.26*span,.35*span,radius));
        slope+=probability*lifetime*band*displacement/max(radius,.008)*(.06+.06*random.y);
    }
    return slope*resolved;
}
