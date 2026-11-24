#version 450
layout(set=0,binding=0) uniform Weather {vec4 p[15];} weather;
layout(location=0) out vec3 rayECEF;
layout(location=1) flat out vec2 depthCoefficients;
layout(location=2) out vec2 probeUV;
void main(){
    vec2 ndc=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.-1.;
    probeUV=ndc*.5+.5;rayECEF=vec3(0);depthCoefficients=vec2(0);
    gl_Position=vec4(ndc,0,1);
}
