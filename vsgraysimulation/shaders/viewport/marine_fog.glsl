#ifndef VSGRAYSIM_MARINE_FOG
#define VSGRAYSIM_MARINE_FOG
// Exponential marine boundary layer, visibility specified at sea level.
// Exact integral along the linear-height segment, with a stable horizontal limit.
float marineFogDepth(float distance,float h0,float h1,float visibility,float scaleHeight){
    if(visibility<=0.)return 0.;
    h0=max(h0,0.);h1=max(h1,0.);float a=h0/scaleHeight,b=h1/scaleHeight,d=b-a;
    float density=abs(d)<.001?exp(-.5*(a+b)):(exp(-a)-exp(-b))/d;
    return min(60.,3.912*distance/visibility*max(0.,density));
}
vec3 marineFog(vec3 radiance,float distance,float h0,float h1,vec2 settings,vec3 illumination){
    float transmission=exp(-marineFogDepth(distance,h0,h1,settings.x,settings.y));
    return radiance*transmission+illumination*(1.-transmission);
}
#endif
