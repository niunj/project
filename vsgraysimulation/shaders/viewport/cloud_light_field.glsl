layout(set=0,binding=15) uniform CloudLightField {vec4 data[3];} cloudLightField;
float bulkLightDepth(vec3 p,vec3 direction,float span,int steps,float optics) {
    float tau=0.;
    for(int i=0;i<steps;++i){float a=float(i)/float(steps),b=float(i+1)/float(steps);
        float lo=span*a*a*a,hi=span*b*b*b;
        tau+=cloudExtinctionPerMetre(cachedObjectCloudDensity(p+direction*(.5*(lo+hi))),optics)*(hi-lo);
        if(tau>18.)break;
    }
    return min(tau,24.);
}
vec3 bulkLightDepths(vec3 p,vec3 key,vec3 side,float optics) {
    float top=max(0.,sparseCloud.objectNorthTop.w-p.y);
    return vec3(bulkLightDepth(p,key,min(16000.,max(2000.,top/max(.12,abs(key.y)))),6,optics),
        bulkLightDepth(p,vec3(0,1,0),max(30.,top),4,optics),
        bulkLightDepth(p,side,max(30.,top/side.y),4,optics));
}
#ifdef CLOUD_LIGHT_FIELD_WRITE
layout(set=0,binding=14,rgba16f) uniform writeonly image3D cloudLighting;
#else
layout(set=0,binding=14) uniform sampler3D cloudLighting;
bool cloudLightingReady() {
    return cloudLightField.data[1].w>.5 && all(equal(cloudLightKeys.keys[144],cloudLightField.data[0]));
}
vec3 objectCachedLight(vec3 p,int index) {
    vec4 centre=sparseCloud.objects[index*3],r=sparseCloud.objects[index*3+1],shape=sparseCloud.objects[index*3+2];
    vec3 q=objectShapeSpace(p-centre.xyz,r.xyz,shape.xy);
    int tile=int(cloudLightField.data[1].y),axis=int(cloudLightField.data[1].z);
    ivec3 cell=ivec3(index%axis,(index/axis)%axis,index/(axis*axis));
    vec3 uv=(vec3(cell*tile)+1.+cloudLightField.data[1].x*(q-OBJECT_SHAPE_LO)/(OBJECT_SHAPE_HI-OBJECT_SHAPE_LO))/vec3(textureSize(cloudLighting,0));
    return max(vec3(0),textureLod(cloudLighting,uv,0).rgb);
}
#endif
