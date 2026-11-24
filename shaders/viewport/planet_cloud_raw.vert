#version 450
#extension GL_GOOGLE_include_directive : require
#include "cloud_temporal.glsl"
layout(location=0) out vec3 rayECEF;
layout(location=1) flat out vec2 depthCoefficients;
void main(){
    vec2 ndc=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2)*2.-1.;
    vec2 jitter=2.*temporal.control.zw/temporal.resolution.xy;
    rayECEF=(temporal.rayMatrix*vec4(ndc+jitter,1.,1.)).xyz;
    depthCoefficients=temporal.resolution.zw;
    gl_Position=vec4(ndc,0.,1.);
}
