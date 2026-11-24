#ifndef MARINE_SPECULAR_GLSL
#define MARINE_SPECULAR_GLSL
vec3 waterHalfVector(vec3 light,vec3 view,vec3 fallback){
    vec3 h=light+view;
    return dot(h,h)>1e-10 ? h*inversesqrt(dot(h,h)) : fallback;
}
float waterGgx(float cosine,float roughness){
    float a=roughness*roughness,a2=a*a,c=clamp(cosine,0.,1.);
    // Avoid cancellation of +1 and -cosine^2 near a sharp normal highlight.
    float d=max((1.-c*c)+a2*c*c,1e-8);
    return a2/(3.14159265359*d*d);
}
float waterNormalVariance(vec3 dx,vec3 dy){
    return min(.72,.5*(dot(dx,dx)+dot(dy,dy)));
}
float waterSpecularRoughness(float base,float variance){
    float a=base*base;
    return pow(min(1.,a*a+max(variance,0.)),.25);
}
#endif
