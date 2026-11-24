#version 450
layout(set=0,binding=0) uniform Ocean {vec4 p[796];} ocean;
layout(location=0) out vec2 q;
layout(location=1) flat out vec4 deposit;
void main(){
    const vec2 corners[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    q=corners[gl_VertexIndex];int i=gl_InstanceIndex;
    if(i>=int(ocean.p[218].x)){gl_Position=vec4(2,2,0,1);deposit=vec4(0);return;}
    vec4 a=ocean.p[220+3*i],b=ocean.p[221+3*i],c=ocean.p[222+3*i];float age=a.z;
    if(age<0.||age>=a.w){gl_Position=vec4(2,2,0,1);deposit=vec4(0);return;}
    // Diffusion broadens the deposit, conserving approximate integrated area.
    vec2 span=sqrt(b.xy*b.xy+vec2(.11,.20)*age);
    vec2 drift=ocean.p[50].xy+ocean.p[167].xy*.012;
    vec2 centre=a.xy+drift*age,axis=b.zw,side=vec2(-axis.y,axis.x);
    vec2 world=centre+(axis*q.x*span.x+side*q.y*span.y)*2.2;
    vec2 uv=(world-ocean.p[217].xy)*ocean.p[217].z;
    gl_Position=vec4(uv*2.-1.,0,1);
    float fade=exp(-age/10.)*pow(max(0.,1.-age/a.w),.7);
    deposit=vec4(c.x*fade*b.x*b.y/(span.x*span.y),c.y,age,span.x/max(span.y,.1));
}
