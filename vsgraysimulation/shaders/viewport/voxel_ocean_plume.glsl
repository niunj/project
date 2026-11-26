// Uniform records store the shaft hub, advected trail tip, and the current shaft phase.
struct PropellerPlume {
    vec3 hub;
    float radius;
    vec3 axis;
    float lengthM;
    float age;
    float spin;
    float phase;
    float strength;
};

PropellerPlume propellerPlume(int index) {
    vec4 hub=ocean.p[796+3*index];
    vec4 trail=ocean.p[797+3*index];
    vec4 shaft=ocean.p[798+3*index];
    float extent=length(trail.xyz);
    return PropellerPlume(hub.xyz,hub.w,trail.xyz/max(extent,.001),extent,trail.w,
                          shaft.x,shaft.y,shaft.z);
}

// The bounds enclose the widening plume and limit the fragments to integrate.
float plumeRadialBound(PropellerPlume plume) {
    return plume.radius*(1.36+.28*min(plume.age,3.2));
}
