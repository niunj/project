// Display-only normalized bulk condensate; poses and mass envelopes stay live.
layout(set=0,binding=12) uniform CloudLightCache {vec4 data[218];} cloudLightCache;
#if defined(CLOUD_LIGHT_CACHE_WRITE) || defined(CLOUD_LIGHT_FIELD_WRITE)
layout(set=0,binding=13,std430) buffer CloudLightKeys {vec4 keys[145];} cloudLightKeys;
#else
layout(set=0,binding=13,std430) readonly buffer CloudLightKeys {vec4 keys[145];} cloudLightKeys;
#endif
#ifdef CLOUD_LIGHT_CACHE_WRITE
layout(set=0,binding=11,r32f) uniform writeonly image3D cloudLightAtlas;
#else
layout(set=0,binding=11) uniform sampler3D cloudLightAtlas;
bool cloudLightResident(int index) {
    // The compute->fragment barrier publishes the image and its exact key in
    // this submission. A skipped/unrecorded bake keeps the old key and safely
    // falls back. No CPU prediction of GPU completion is needed.
    if(cloudLightCache.data[0].x<=0.)return false;
    if(cloudLightCache.data[38+index/4][index%4]>.5)return true;
    return all(equal(cloudLightKeys.keys[index],cloudLightCache.data[74+index]));
}
float cachedObjectCondensate(vec3 p,int index) {
    if(!cloudLightResident(index))
        return objectCondensate(p,index,0.);
    int row=index*3;
    vec4 c=sparseCloud.objects[row],r=sparseCloud.objects[row+1],shape=sparseCloud.objects[row+2];
    vec3 d=p-c.xyz;
    if(c.w<=0.||abs(d.x)>r.w||abs(d.z)>r.w||abs(d.y)>1.95*r.y)return 0.;
    vec3 q=objectShapeSpace(d,r.xyz,shape.xy);
    if(any(lessThan(q,OBJECT_SHAPE_LO))||any(greaterThan(q,OBJECT_SHAPE_HI))||dot(q,q)>5.50)return 0.;
    int side=int(cloudLightCache.data[0].x),tile=int(cloudLightCache.data[0].y),axis=int(cloudLightCache.data[0].z);
    ivec3 cell=ivec3(index%axis,(index/axis)%axis,index/(axis*axis));
    vec3 uv=(vec3(cell*tile)+1.+float(side)*(q-OBJECT_SHAPE_LO)/(OBJECT_SHAPE_HI-OBJECT_SHAPE_LO))/vec3(textureSize(cloudLightAtlas,0));
    return max(0.,textureLod(cloudLightAtlas,uv,0.).r)*c.w;
}
float cachedObjectCloudDensity(vec3 p) {
    ivec2 cell=cloudObjectBin(p);
    if(!validObjectBin(cell)||p.y<sparseCloud.objectUpBase.w||p.y>sparseCloud.objectNorthTop.w)return 0.;
    int bin=cloudObjectList(cell),count=objectCandidateCount(bin);float rho=0.;
    for(int i=0;i<count;++i){int index=objectCandidate(bin,i);if(index<0)break;
        rho=max(rho,cachedObjectCondensate(p,index));}
    return rho*smoothstep(0.,5000.,sparseCloud.objectEastExtent.w-max(abs(p.x),abs(p.z)));
}
#endif
