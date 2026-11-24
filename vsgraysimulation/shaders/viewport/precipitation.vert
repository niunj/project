#version 450
layout(push_constant) uniform Camera {mat4 projection;mat4 modelView;} camera;
layout(set=0,binding=0) uniform Weather {vec4 p[10];} weather;
layout(location=0) out vec2 uv;
layout(location=1) flat out vec4 particle; // alpha, snow flag, distance, shape variation
layout(location=2) flat out float backlight;

const int layerCount=24*24*8;
float random01(uint x) {
    x=(x^(x>>16))*0x7feb352du;
    x=(x^(x>>15))*0x846ca68bu;
    return float((x^(x>>16))>>8)/16777216.;
}
vec4 cellRandom(ivec3 cell,bool snow,bool middle) {
    uvec3 index=uvec3(ivec3(mod(vec3(cell),24.)));
    uint key=index.x+24u*index.z+576u*index.y+uint(snow)*49157u+uint(middle)*78031u;
    return vec4(random01(key),random01(key+173u),random01(key+5399u),random01(key+11311u));
}
void hideParticle() {
    gl_Position=vec4(2,2,0,1);
    particle=vec4(0);
    backlight=0.;
}
void main() {
    const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    uv=corners[gl_VertexIndex];
    int layer=gl_InstanceIndex/layerCount,index=gl_InstanceIndex%layerCount;
    bool snow=layer>=2,middle=(layer%2)==1;
    float flux=snow?weather.p[3].z:weather.p[3].y;
    if(flux<=0.){hideParticle();return;}

    vec2 eye=vec2(weather.p[0].w,weather.p[2].w);
    float spacing=middle?6.:(snow?2.5:2.),halfWidth=12.*spacing;
    float spacingY=middle?10.:5.,halfHeight=4.*spacingY;
    vec2 travel=weather.p[snow?7:6].xy;
    vec2 centre=floor((eye-travel)/spacing);
    ivec3 cell=ivec3(index%24-12,index/(24*24),index/24%24-12);
    cell.xz+=ivec2(centre);
    vec4 seed=cellRandom(cell,snow,middle);
    float t=weather.p[3].x;
    // Co-moving clusters at a scale larger than individual drops. The repeat
    // spans the whole layer, so replacement cells do not pop at its boundary.
    vec2 band=vec2(cell.xz)*(.2617993878);
    float drift=sin(band.x+2.*band.y+.026179939*t)*sin(2.*band.x-band.y-.052359878*t);
    float density=1.+(snow?.35:.25)*drift;
    float population=snow?flux:mix(flux,sqrt(flux),.55);
    float presence=smoothstep(seed.z,seed.z+.085,clamp(population*density,0.,1.));
    if(presence<=0.){hideParticle();return;}

    // The vertical repeat and chosen velocities divide the same 240 s clock.
    float fall=snow?(1.+floor(seed.x*4.))/3.:(13.+floor(seed.x*15.))/3.;
    vec2 horizontal=(vec2(cell.xz)+seed.xw)*spacing-eye+travel;
    horizontal=mod(horizontal+halfWidth,2.*halfWidth)-halfWidth;
    float y=mod((float(cell.y)+seed.y)*spacingY-fall*t-weather.p[3].w+halfHeight,2.*halfHeight)-halfHeight;
    vec3 east=weather.p[0].xyz,up=weather.p[1].xyz,north=weather.p[2].xyz;
    vec3 wind=weather.p[4].xyz*(snow?.55:.3);
    if(snow) {
        vec2 phase=t*.02617993878*vec2(21.+floor(seed.y*13.),14.+floor(seed.x*17.))+40.*seed.xy;
        float flutter=(.11+.33*seed.w)*weather.p[9].w;
        horizontal+=vec2(sin(phase.x),cos(phase.y))*flutter;
    }

    vec3 relative=east*horizontal.x+north*horizontal.y+up*y;
    vec3 view=mat3(camera.modelView)*relative;
    if(view.z>-.65){hideParticle();return;}
    float distance=length(relative);
    float horizontalDistance=length(horizontal);
    float fade=(1.-smoothstep(.72*halfWidth,.98*halfWidth,horizontalDistance))*
        (1.-smoothstep(.72*halfHeight,.98*halfHeight,abs(y)));
    float blendStart=snow?16.:12.,blendEnd=snow?31.:25.;
    fade*=middle?smoothstep(blendStart,blendEnd,distance):1.-smoothstep(blendStart+4.,blendEnd+5.,distance);
    float altitude=weather.p[3].w+y;
    fade*=1.-smoothstep(weather.p[4].w-25.,weather.p[4].w+9.,altitude);
    fade*=smoothstep(weather.p[1].w-.06,weather.p[1].w+.20,altitude);
    fade*=smoothstep(1.25,4.,distance);
    if(fade<=.001){hideParticle();return;}

    vec3 velocity=mat3(camera.modelView)*(wind-up*fall);
    vec2 axis=length(velocity.xy)>.001?normalize(velocity.xy):vec2(0,1);
    float size,trail;
    if(snow) {
        // Slow tumbling changes the flake's projected profile as it falls.
        float angle=6.28318531*seed.w+t*.02617993878*(17.+floor(seed.y*18.));
        axis=vec2(sin(angle),cos(angle));
        size=mix(.013,.070,seed.x*seed.x)*mix(.86,1.19,flux);
        trail=size*mix(.54,1.16,.5+.5*sin(2.*angle+13.*seed.y));
    } else {
        size=mix(.006,.017,seed.x*seed.x)*mix(.85,1.19,flux);
        trail=clamp(length(velocity.xy)*mix(.006,.026,seed.w),.026,.24);
    }
    vec2 side=vec2(axis.y,-axis.x);
    float pixel=-view.z*.0010;
    float width=max(size,pixel);
    float lengthOnScreen=max(trail,pixel);
    view.xy+=side*uv.x*width+axis*uv.y*lengthOnScreen;
    gl_Position=camera.projection*vec4(view,1);
    float coverage=(size/width)*(trail/lengthOnScreen);
    // A distant drop is a wider filtered streak, with proportionally lower opacity.
    particle=vec4(fade*coverage*presence*(middle?.80:1.),snow?1.:0.,distance,seed.w);
    backlight=pow(clamp(dot(relative/max(distance,.001),weather.p[8].xyz),0.,1.),8.)*weather.p[8].w;
}
