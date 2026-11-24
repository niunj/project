#version 450
layout(location=0) in vec2 q;
layout(location=1) flat in vec4 deposit;
layout(location=0) out float coverage;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1)),f.x),f.y);}
void main(){
    float r=dot(q,q);if(r>1.)discard;
    vec2 p=q*vec2(clamp(deposit.w,1.,35.)*6.,6.)+deposit.y*.013;
    float cells=noise(p)+.35*noise(p*2.17+3.1);
    float lace=smoothstep(.35,.95,cells);
    float edge=exp(-r*5.)*smoothstep(1.,.5,r);
    coverage=deposit.x*edge*mix(.3,1.5,lace);
}
