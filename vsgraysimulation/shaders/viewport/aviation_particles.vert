#version 450
layout(push_constant) uniform Camera { mat4 projection; mat4 modelView; } camera;
layout(std430,set=0,binding=0) readonly buffer Instances { vec4 values[]; } instances;
layout(location=0) out vec3 eyeRay;
layout(location=1) flat out vec3 centreEye;
layout(location=2) flat out vec4 axisHalfLength;
layout(location=3) flat out vec4 centreRadius;
layout(location=4) flat out vec4 shape; // kind, extinction, age fraction, phase
layout(location=5) flat out mat3 toChart;
const vec2 quad[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
void main() {
    centreRadius=instances.values[gl_InstanceIndex*3];
    vec4 axis=instances.values[gl_InstanceIndex*3+1];
    shape=instances.values[gl_InstanceIndex*3+2];
    centreEye=(camera.modelView*vec4(centreRadius.xyz,1.)).xyz;
    axisHalfLength=vec4(normalize(mat3(camera.modelView)*axis.xyz),axis.w);
    toChart=transpose(mat3(camera.modelView));

    // Project a conservative box enclosing the oriented ellipsoid. This is
    // just a raster bound; the volume itself is intersected in the fragment.
    float r=centreRadius.w,h=axis.w;
    vec3 span=sqrt(max(vec3(.0001),vec3(r*r)+(h*h-r*r)*axisHalfLength.xyz*axisHalfLength.xyz));
    if(-centreEye.z<=span.z+.075) {
        vec2 ndc=quad[gl_VertexIndex];
        eyeRay=vec3(ndc.x/camera.projection[0][0],ndc.y/camera.projection[1][1],-1.);
        gl_Position=vec4(ndc,.5,1.);
    } else {
        float distanceToFront=max(.075,-centreEye.z-span.z);
        vec2 halfSpan=(span.xy+abs(centreEye.xy/centreEye.z)*span.z)*(-centreEye.z/distanceToFront);
        eyeRay=centreEye+vec3(quad[gl_VertexIndex]*halfSpan,0);
        gl_Position=camera.projection*vec4(eyeRay,1.);
    }
}
