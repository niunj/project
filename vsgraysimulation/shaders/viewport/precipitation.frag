#version 450
layout(set=0,binding=0) uniform Weather {vec4 p[10];} weather;
layout(location=0) in vec2 uv;
layout(location=1) flat in vec4 particle;
layout(location=2) flat in float backlight;
layout(location=0) out vec4 color;

vec3 toneMap(vec3 light) {
    vec3 x=max(light*weather.p[5].w,vec3(0.));
    return clamp(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14),0.,1.);
}
void main() {
    bool snow=particle.y>.5;
    float shape;
    if(snow) {
        // Close flakes have a soft irregular outline, resolving into a
        // compact filtered spot at a distance.
        float handedness=2.*step(.5,particle.w)-1.;
        vec2 offset=vec2(.24*handedness,.13);
        vec2 chip=vec2(-.22*handedness,.05+.32*particle.w);
        float flakes=max(exp(-5.8*dot(uv-offset,uv-offset)),
                         .78*exp(-5.3*dot(uv+offset,uv+offset)));
        flakes=max(flakes,.57*exp(-10.*dot(uv-chip,uv-chip)));
        shape=mix(flakes,exp(-4.4*dot(uv,uv)),smoothstep(8.,24.,particle.z))*
            (1.-smoothstep(.63,1.,length(uv)));
    } else {
        // Short bright core with a tapered tail, without hard line ends.
        float head=smoothstep(-.9,.68,uv.y);
        float width=mix(.40,.94,head);
        float taper=smoothstep(-1.,-.36,uv.y)*(1.-smoothstep(.57,1.,uv.y));
        shape=exp(-3.5*uv.x*uv.x/(width*width))*taper*mix(.40,1.,head);
    }
    float alpha=shape*particle.x*(snow?.90:.76);
    if(alpha<.001)discard;
    vec3 light=weather.p[5].rgb*(snow?mix(2.55,3.8,particle.w):mix(.86,1.29,particle.w));
    light+=backlight*weather.p[9].rgb*(snow?.24:.65);
    if(weather.p[7].w>0.) {
        float haze=1.-exp(-3.912*particle.z/weather.p[7].w);
        light=mix(light,vec3(weather.p[6].zw,weather.p[7].z),haze);
        alpha*=1.-.80*haze;
    }
    color=vec4(toneMap(light)*alpha,alpha);
}
