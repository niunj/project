// Relative irradiance referenced to clear daylight. Elevation-dependent RGB
// extinction approximates the clear atmospheric column at each surface point.
vec3 surfaceSun(vec3 normal,vec3 source,float turbidity) {
    if(dot(source,source)<.5) return vec3(0);
    float mu=dot(normal,source);
    float visible=smoothstep(-.008,.008,mu);
    float mass=1./max(.025,max(mu,0.)+.025*exp(-11.*max(mu,0.)));
    return exp(-(vec3(.055,.11,.23)+.015*max(turbidity-1.,0.))*mass)*visible;
}
vec3 surfaceMoon(vec3 normal,vec3 source,float phase) {
    return vec3(.78,.83,1.)*(2.5e-6*phase*phase)*surfaceSun(normal,source,1.);
}
vec3 displayRadiance(vec3 linear,float exposure) {
    vec3 x=max(linear*exposure,vec3(0));
    return clamp(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14),0.,1.);
}
