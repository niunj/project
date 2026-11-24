#ifndef CLOUD_DESCRIPTOR_SET
#define CLOUD_DESCRIPTOR_SET 0
#endif
// Coarse discrete cloud objects and the local sparse-cluster atlas are shared
// by cloud, surface, sea and terrain shadow passes. No 2-D cloud impostor or
// procedural density shell is reconstructed here.
layout(set=CLOUD_DESCRIPTOR_SET,binding=2) uniform sampler3D cloudMap;
layout(set=CLOUD_DESCRIPTOR_SET,binding=3) uniform sampler3D cloudDetail;
layout(set=CLOUD_DESCRIPTOR_SET,binding=4) uniform usampler3D sparseCloudPages;
layout(set=CLOUD_DESCRIPTOR_SET,binding=5) uniform sampler3D sparseCloudAtlas;
layout(set=CLOUD_DESCRIPTOR_SET,binding=6) uniform SparseCloudLayout {
    vec4 originEnabled,eastVoxel,northBrick,upPageX,page,atlas,microphysics;
    vec4 midOriginAuthority,midEastCell,midNorthSide,midUpBase,midTopExtent;
    vec4 objectOriginCount,objectEastExtent,objectUpBase,objectNorthTop;
    vec4 objects[432];
    vec4 objectBins[432];
    vec4 objectFlow;       // horizontal direction and wind speed in the chart
    vec4 objectAges[36];   // seconds since birth, four cloud objects per vector
} sparseCloud;
const float CLOUD_A=6378137., CLOUD_B=6356752.314245;
const float CLOUD_PI=3.141592653589793;
const int OBJECT_BIN_SIDE=12,OBJECT_BIN_SLOTS=12;
bool useCloudObjects(){return sparseCloud.objectEastExtent.w>0.;}
vec3 objectAxes(vec3 p) {
    return vec3(dot(p,sparseCloud.objectEastExtent.xyz),dot(p,sparseCloud.objectUpBase.xyz),
                dot(p,sparseCloud.objectNorthTop.xyz));
}
float objectBinWidth() {return 2.*sparseCloud.objectEastExtent.w/float(OBJECT_BIN_SIDE);}
ivec2 cloudObjectBin(vec3 p) {
    return ivec2(floor((p.xz+sparseCloud.objectEastExtent.w)/objectBinWidth()));
}
bool validObjectBin(ivec2 cell) {
    return all(greaterThanEqual(cell,ivec2(0)))&&all(lessThan(cell,ivec2(OBJECT_BIN_SIDE)));
}
int cloudObjectList(ivec2 cell) {return (cell.x+cell.y*OBJECT_BIN_SIDE)*(OBJECT_BIN_SLOTS/4);}
int objectCandidateCount(int bin) {
    return sparseCloud.objectBins[bin].x<0. ? int(sparseCloud.objectOriginCount.w) : OBJECT_BIN_SLOTS;
}
int objectCandidate(int bin,int i) {
    return sparseCloud.objectBins[bin].x<0. ? i : int(sparseCloud.objectBins[bin+i/4][i%4])-1;
}
float cloudLobe(vec3 q) {
    float p=max(0.,1.-dot(q,q));
    return p*p;
}
float objectNoise(vec3 p) {
    ivec3 base=ivec3(floor(p));
    vec3 t=smoothstep(vec3(0.),vec3(1.),fract(p));
    float result=0.;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        uvec3 cell=uvec3(base+ivec3(x,y,z));
        uint h=cell.x*1597334677u^cell.y*3812015801u^cell.z*2798796415u;
        h=(h^(h>>16))*2246822519u;
        result+=float(h^(h>>13))*(1./4294967296.)*
                (x==0?1.-t.x:t.x)*(y==0?1.-t.y:t.y)*(z==0?1.-t.z:t.z);
    }
    return result;
}
// Bounds of the compact density lobes after the strongest possible warp.
const vec3 OBJECT_SHAPE_LO=vec3(-1.58,-.82,-1.39),OBJECT_SHAPE_HI=vec3(1.90,1.90,1.39);
vec3 objectShapeSpace(vec3 d,vec3 radii,vec2 heading) {
    return vec3(dot(heading,d.xz)/radii.x+.20*d.y/radii.y,d.y/radii.y,
                dot(vec2(-heading.y,heading.x),d.xz)/radii.z);
}
vec4 objectBulkProfile(vec3 q,vec3 r,vec4 shape) {
    float condensation=smoothstep(-.82,-.64,q.y);
    if(condensation<=0.||any(lessThan(q,OBJECT_SHAPE_LO))||any(greaterThan(q,OBJECT_SHAPE_HI))||dot(q,q)>5.50)return vec4(0);
    vec2 ratios=clamp(r.xz/r.y,vec2(1.),vec2(3.2));
    vec3 detail=vec3(q.x*ratios.x,q.y,q.z*ratios.y);
    float billow=objectNoise(3.7*detail+vec3(15.*shape.z,18.*shape.w,9.))-.5;
    float edge=objectNoise(8.1*detail+vec3(3.,24.*shape.z,19.*shape.w))-.5;
    q*=1.+.35*billow+.10*edge;
    q.y+=.13*billow;
    vec2 seed=fract(vec2(7.13*shape.z+3.71*shape.w,11.71*shape.z+5.19*shape.w));
    // Match the CPU profile. Taller clouds put more condensate into
    // separated upper billows without changing the shared condensation base.
    // The bounded aspect inputs are also the normalized density cache key.
    float developed=smoothstep(.3125,.48,1./sqrt(ratios.x*ratios.y));
    float bodyWidth=.94-.14*developed;
    float body=cloudLobe(q/vec3(bodyWidth,.86,bodyWidth));
    float crown=cloudLobe((q-vec3(-.43+.49*shape.z-.12*developed,.37+.22*shape.w+.10*developed,-.22+.44*seed.x))/
                          vec3(.46+.20*seed.y+.05*developed,.53+.13*shape.z+.08*developed,.43+.23*shape.w));
    float crest=cloudLobe((q-vec3(.32+.23*seed.y,.58+.13*shape.z+.10*developed,-.36+.40*shape.w))/
                          vec3(.35+.14*seed.x+.05*developed,.46+.15*shape.w,.36+.17*seed.y));
    float flank=cloudLobe((q-vec3(-.56+.99*seed.x,-.03+.26*seed.y,.44-.85*shape.w))/
                          vec3(.49+.16*shape.z,.42+.13*seed.y,.48+.15*seed.x));
    float turret=cloudLobe((q-vec3(-.44+.82*seed.y,.82+.09*shape.w+.08*developed,.40-.82*shape.z))/
                           vec3(.28+.12*shape.w+.05*developed,.28+.10*shape.z,.30+.11*seed.x));
    float lip=cloudLobe((q-vec3(.70-.20*shape.w,.10+.30*shape.z,.25-.60*seed.y))/
                        vec3(.34,.32+.10*shape.w,.36));
    float tail=cloudLobe((q-vec3(.77,-.15,-.26))/vec3(.69,.31,.34));
    float lobe=max(max(max(body,(.89+.21*developed)*crown),max((.82+.20*developed)*crest,.80*flank)),
                   max((.70+.25*developed)*turret,.67*lip));
    float eroded=max(0.,lobe-(.05+.09*(.5-edge)));
    float bulk=max(eroded*smoothstep(0.,.09,eroded)*(1.20+.45*edge),.13*tail);
    float fringe=max(smoothstep(.006,.052,lobe),.56*tail)*(1.-smoothstep(.20,.67,lobe));
    return vec4(bulk,fringe,lobe,condensation);
}
float objectCondensate(vec3 p,int index,float detailWeight) {
    int row=index*3;
    vec4 centre=sparseCloud.objects[row],r=sparseCloud.objects[row+1],shape=sparseCloud.objects[row+2];
    vec3 d=p-centre.xyz;
    if(centre.w<=0.||abs(d.x)>r.w||abs(d.z)>r.w||abs(d.y)>1.95*r.y)return 0.;
    vec3 q=objectShapeSpace(d,r.xyz,shape.xy);
    vec4 profile=objectBulkProfile(q,r.xyz,shape);
    float bulk=profile.x,fringe=profile.y,lobe=profile.z,condensation=profile.w;
    if(detailWeight<=0.||fringe<=.002)return centre.w*bulk*condensation;
    // Advection relative to the parent cloud resolves faster-moving wisps.
    vec3 flow=sparseCloud.objectFlow.xyz;
    float along=dot(d.xz,flow.xy),across=dot(d.xz,vec2(-flow.y,flow.x));
    float drift=flow.z*sparseCloud.objectAges[index/4][index%4];
    float tuft=objectNoise(vec3((along-.26*drift)/48.+17.*shape.z,(d.y+.03*across)/34.,
                               across/41.+21.*shape.w));
    // The thin anisotropic threads need a smaller footprint than the tufts.
    // Keep their mean condensate when unresolved, rather than striped aliases.
    float threadWeight=smoothstep(.98,1.,detailWeight),strand=.12;
    if(threadWeight>0.) {
        float thread=objectNoise(vec3((along-.58*drift)/175.+13.*shape.w,
                                     (d.y-.026*along+.012*across)/24.+7.,(across-.045*d.y)/30.+23.*shape.z));
        strand=mix(.12,smoothstep(.56,.72,thread),threadWeight);
    }
    float loose=bulk*(.10+1.46*smoothstep(.37,.65,tuft)+1.10*strand)+
                .035*strand*(1.-smoothstep(.09,.33,lobe));
    return centre.w*mix(bulk,loose,detailWeight*fringe)*condensation;
}
float objectDensityAndId(vec3 p,int bin,float detailWeight,out int dominant) {
    float rho=0.;dominant=-1;
    int n=objectCandidateCount(bin);
    for(int i=0;i<n;++i) {
        int index=objectCandidate(bin,i);
        if(index<0)break;
        float value=objectCondensate(p,index,detailWeight);
        if(value>rho)dominant=index;
        rho=max(rho,value);
    }
    return rho*smoothstep(0.,5000.,sparseCloud.objectEastExtent.w-max(abs(p.x),abs(p.z)));
}
float objectDensityInBin(vec3 p,int bin,float detailWeight) {
    int unused;return objectDensityAndId(p,bin,detailWeight,unused);
}
float localObjectCloudDensity(vec3 p,float detailWeight) {
    ivec2 cell=cloudObjectBin(p);
    if(!validObjectBin(cell)||p.y<sparseCloud.objectUpBase.w||p.y>sparseCloud.objectNorthTop.w)return 0.;
    return objectDensityInBin(p,cloudObjectList(cell),detailWeight);
}

float cloudRadius(vec3 n) {
    return inversesqrt(dot(n.xy,n.xy)/(CLOUD_A*CLOUD_A)+n.z*n.z/(CLOUD_B*CLOUD_B));
}
float cloudExtinctionPerMetre(float normalizedLiquidWater,float opticalScale) {
    // Qext≈2 in the visible spectrum. For spherical droplets the mass
    // extinction coefficient is 3 Qext / (4 rhoWater rEffective).
    float liquidWaterKgM3=max(normalizedLiquidWater,0.)*sparseCloud.microphysics.x;
    float dropletRadius=max(sparseCloud.microphysics.y,2e-6);
    float waterDensity=max(sparseCloud.microphysics.z,100.);
    float extinctionEfficiency=max(sparseCloud.microphysics.w,.1);
    float massExtinction=3.*extinctionEfficiency/(4.*waterDensity*dropletRadius);
    return liquidWaterKgM3*massExtinction*max(opticalScale,0.)/10.;
}
// Filter fine vertical layers in wide visual ray segments. The renderer
// samples the same condensate volume as the point and spectral queries.
float sampleCloudAtlas(sampler3D atlas,vec3 uvw,vec3 verticalAxis,float lod,
                       float layerMetres,float footprintMetres,float voxels) {
    float value=textureLod(atlas,uvw,lod).r;
#ifdef CLOUD_VIEW_FILTER
    float weight=smoothstep(120.,950.,footprintMetres);
    if(weight>0.) {
        float width=min(2.5/voxels,footprintMetres*.10/max(layerMetres,1.));
        vec3 offset=verticalAxis*width;
        float adjacent=textureLod(atlas,uvw-offset,lod).r+textureLod(atlas,uvw+offset,lod).r;
        value=mix(value,.5*value+.25*adjacent,weight);
    }
#endif
    return value;
}
void cloudSplineWeights(vec3 fraction,out vec3 left,out vec3 middle,out vec3 right);
vec4 samplePaddedCloud(sampler3D atlas,vec3 centre,vec3 dimensions,
                       vec3 left,vec3 middle,vec3 right);
float intermediateCloudDensityAt(vec3 point,vec3 windDisplacement,float altitude,float broad,float footprintMetres) {
    float authority=clamp(sparseCloud.midOriginAuthority.w,0.,1.);
    if(authority<=0. || sparseCloud.midEastCell.w<=0.)return broad;
    vec3 relative=point-sparseCloud.midOriginAuthority.xyz;
    float halfExtent=sparseCloud.midTopExtent.y;
    float vertical=max(sparseCloud.midTopExtent.x-sparseCloud.midUpBase.w,1.);
    float h=(altitude-sparseCloud.midUpBase.w)/vertical;
    bool dense=sparseCloud.midTopExtent.w>.5;
    if(dense)relative+=windDisplacement*(.68+.76*smoothstep(0.,1.,h));
    vec2 local=vec2(dot(relative,sparseCloud.midEastCell.xyz),dot(relative,sparseCloud.midNorthSide.xyz));
    // The cloud shell follows Earth curvature. Tangent-plane height differs
    // by hundreds of metres at the side of the 160 km chart.
    vec3 uvw=vec3(local.x/(2.*halfExtent)+.5,
                  h,
                  local.y/(2.*halfExtent)+.5);
    if(any(lessThanEqual(uvw,vec3(0.)))||any(greaterThanEqual(uvw,vec3(1.))))return broad;
    float lod=clamp(log2(max(footprintMetres,1.)/sparseCloud.midEastCell.w),0.,7.);
    vec3 size=vec3(textureSize(cloudDetail,0));
    float condensate;
    if(dense && lod<1.) {
        vec3 voxel=uvw*size,centre=floor(voxel),left,middle,right;
        cloudSplineWeights(voxel-vec3(.5)-centre,left,middle,right);
        condensate=samplePaddedCloud(cloudDetail,centre,size,left,middle,right).r;
        if(lod>0.)condensate=mix(condensate,sampleCloudAtlas(cloudDetail,uvw,vec3(0,1,0),lod,
                                                            vertical,footprintMetres,size.y),smoothstep(0.,1.,lod));
    } else {
        condensate=sampleCloudAtlas(cloudDetail,uvw,vec3(0,1,0),lod,vertical,footprintMetres,size.y);
    }
    float edgeDistance=min(halfExtent-abs(local.x),halfExtent-abs(local.y));
    // Preserve clear air between resident cloud objects. Feather only the
    // domain boundary into the coarser stored field.
    float domain=smoothstep(0.,max(halfExtent*.5,sparseCloud.midTopExtent.z),edgeDistance);
    float layerEdge=smoothstep(0.,.035,min(uvw.y,1.-uvw.y));
    return mix(broad,condensate,authority*domain*layerEdge);
}
// Hash-based 3D value noise for orbital cloud detail (non-periodic).
float cloudDetailNoise(vec3 p) {
    vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
    float n=0.;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        vec3 c=i+vec3(x,y,z);
        uint h=uint(c.x)*73856093u^uint(c.y)*19349663u^uint(c.z)*83492791u;
        h=(h^(h>>16))*2246822519u;
        n+=float((h^(h>>13))>>8)/16777216.*(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y)*(z==0?1.-f.z:f.z);
    }
    return n;
}
float regionalCloudDensityAt(vec3 point,vec4 advection,vec4 properties,float footprintMetres,
                             float altitude) {
    float layerMetres=properties.w-properties.z;
    float h=(altitude-properties.z)/layerMetres;
    vec3 q=advection.xyz;
    vec3 displacement=2.*cross(q,cross(q,point)+advection.w*point);
    vec3 p=point+displacement;
    vec3 m=normalize(p);
    vec3 uvw=vec3(atan(m.y,m.x)/(2.*CLOUD_PI)+.5,
                  .5-asin(clamp(m.z,-1.,1.))/CLOUD_PI,h);
    // Use the macro texture's own resolution from 50 km footprints and
    // smaller. Forcing mip 1 at 100 km footprints erased the 39 km synoptic
    // structure that makes orbital cloud fields read as weather, not blobs.
    float macroLod=clamp(log2(max(footprintMetres,1.)/100000.),0.,3.);
    float macro=sampleCloudAtlas(cloudMap,uvw,vec3(0,0,1),macroLod,layerMetres,footprintMetres,float(textureSize(cloudMap,0).z));
    // cloudMap already contains the condensate voxels of discrete cloud
    // objects at the orbital LOD. Height, top and underside are data, not a
    // procedural shell reconstructed in the fragment shader.
    // Density must follow the stored condensate continuously. Saturating a
    // narrow smoothstep flattened every bright texel to the same density, so
    // orbital cloud systems rendered as featureless plates. Keep a soft
    // threshold for coverage but let the top end carry the texture's own
    // structure.
    float threshold=.34-.22*properties.x;
    // Fully continuous: density follows the stored condensate all the way
    // up, so synoptic systems keep their internal cloudlet structure.
    float broad=.0035*smoothstep(threshold,threshold+.08,macro)*
                mix(.45,1.6,clamp((macro-threshold)/max(1e-4,.55-threshold*.4),0.,1.));
    // The macro atlas is clipped to the shell. Taper its outermost vertical
    // samples so a grazing ray cannot reveal a hard spherical sheet.
    broad*=smoothstep(0.,.05,min(h,1.-h));
    return intermediateCloudDensityAt(point,displacement,altitude,broad,footprintMetres);
}
vec3 sparseCloudAxes(vec3 v) {
    return vec3(dot(v,sparseCloud.eastVoxel.xyz),dot(v,sparseCloud.upPageX.xyz),
                dot(v,sparseCloud.northBrick.xyz));
}
ivec3 sparsePageOrigin() {
    return ivec3(int(sparseCloud.upPageX.w),int(sparseCloud.page.x),int(sparseCloud.page.y));
}
ivec3 sparsePageSize() {
    return ivec3(int(sparseCloud.page.z),int(sparseCloud.page.w),int(sparseCloud.atlas.x));
}
void sparseChartBounds(out vec3 minimum,out vec3 maximum) {
    float voxel=sparseCloud.eastVoxel.w,brick=sparseCloud.northBrick.w;
    minimum=(vec3(sparsePageOrigin())*brick+.5)*voxel;
    maximum=minimum+vec3(sparsePageSize())*brick*voxel;
}
// Centred quadratic B-spline. The weights are positive and sum to one.
// Combine two taps on each axis into one hardware linear sample, so the
// tensor-product reconstruction uses eight reads from the padded brick.
void cloudSplineWeights(vec3 fraction,out vec3 left,out vec3 middle,out vec3 right) {
    vec3 a=.5-fraction,b=.5+fraction;
    left=.5*a*a;
    middle=.75-fraction*fraction;
    right=.5*b*b;
}
vec4 samplePaddedCloud(sampler3D atlas,vec3 centre,vec3 dimensions,
                       vec3 left,vec3 middle,vec3 right) {
    vec3 pair=left+middle;
    vec3 lo=(centre+vec3(.5)-left/pair)/dimensions;
    vec3 hi=(centre+vec3(1.5))/dimensions;
    vec4 result=vec4(0.);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec3 uv=vec3(x==0 ? lo.x : hi.x,y==0 ? lo.y : hi.y,z==0 ? lo.z : hi.z);
        float weight=(x==0 ? pair.x : right.x)*(y==0 ? pair.y : right.y)*(z==0 ? pair.z : right.z);
        result+=weight*textureLod(atlas,uv,0.);
    }
    return result;
}
uint sparsePageSlot(ivec3 pageCoord,ivec3 size) {
    if(any(lessThan(pageCoord,ivec3(0)))||any(greaterThanEqual(pageCoord,size)))return 0u;
    return texelFetch(sparseCloudPages,pageCoord,0).r;
}
ivec3 sparseAtlasCell(uint encoded,ivec3 localCell) {
    uint slot=encoded-1u,axis=uint(sparseCloud.atlas.y);
    uvec3 xyz=uvec3(slot%axis,(slot/axis)%axis,slot/(axis*axis));
    return ivec3(xyz)*int(sparseCloud.atlas.z)+ivec3(1)+localCell;
}
#ifdef CLOUD_VIEW_FILTER
vec2 sparseChartInterval(vec3 origin,vec3 direction) {
    if(sparseCloud.originEnabled.w<=1.)return vec2(0.);
    vec3 p=sparseCloudAxes(origin-sparseCloud.originEnabled.xyz);
    vec3 d=sparseCloudAxes(direction);
    vec3 lo,hi;
    sparseChartBounds(lo,hi);
    lo.xz=max(lo.xz,vec2(-5000.));
    hi.xz=min(hi.xz,vec2(5000.));
    vec2 range=vec2(0.,1e8);
    for(int axis=0;axis<3;++axis) {
        if(abs(d[axis])<1e-6) {
            if(p[axis]<lo[axis]||p[axis]>hi[axis])return vec2(0.);
        } else {
            vec2 t=(vec2(lo[axis],hi[axis])-p[axis])/d[axis];
            range=vec2(max(range.x,min(t.x,t.y)),min(range.y,max(t.x,t.y)));
        }
    }
    return range.y>range.x ? range : vec2(0.);
}
#endif
float sparseCloudDensityAt(vec3 point,out float domainBlend) {
    domainBlend=0.;
    float localAuthority=clamp(sparseCloud.originEnabled.w-1.,0.,1.);
    if(localAuthority<=0.)return 0.;
    vec3 local=sparseCloudAxes(point-sparseCloud.originEnabled.xyz);
    float voxelSize=sparseCloud.eastVoxel.w,brickSide=sparseCloud.northBrick.w;
    // The page lookup is cell centred. Fade the actual page bounds and the
    // radial workspace together, under the same altitude authority.
    vec3 grid=local/voxelSize-vec3(.5);
    ivec3 cell=ivec3(floor(grid+.5));
    ivec3 brick=ivec3(floor(vec3(cell)/brickSide));
    ivec3 pageCoord=brick-sparsePageOrigin();
    ivec3 pageSize=sparsePageSize();
    if(any(lessThan(pageCoord,ivec3(0)))||any(greaterThanEqual(pageCoord,pageSize)))
        return 0.;
    vec3 minimum,maximum;
    sparseChartBounds(minimum,maximum);
    vec3 edge=min(local-minimum,maximum-local);
    float horizontal=1.-smoothstep(3500.,5000.,length(local.xz));
    float feather=smoothstep(0.,2.*brickSide*voxelSize,min(edge.x,min(edge.y,edge.z)));
    domainBlend=localAuthority*horizontal*feather;
    if(domainBlend<=0.)return 0.;
    uint encoded=sparsePageSlot(pageCoord,pageSize);
    ivec3 localCell=cell-brick*int(brickSide);
    vec3 left,middle,right;
    cloudSplineWeights(grid-vec3(cell),left,middle,right);
    if(encoded!=0u) {
        return samplePaddedCloud(sparseCloudAtlas,vec3(sparseAtlasCell(encoded,localCell)),
                                 vec3(sparseCloud.atlas.w),left,middle,right).r;
    }
    // A vacant centre page still sees the support of adjoining occupied
    // pages. Each of the adjacent slots contributes only its own cells.
    ivec3 reach=ivec3(equal(localCell,ivec3(int(brickSide)-1)))-
                 ivec3(equal(localCell,ivec3(0)));
    int mask=(reach.x!=0 ? 1 : 0)+(reach.y!=0 ? 2 : 0)+(reach.z!=0 ? 4 : 0);
    float density=0.;
    for(int subset=mask;subset!=0;subset=(subset-1)&mask) {
        ivec3 delta=reach*ivec3(subset&1,(subset>>1)&1,(subset>>2)&1);
        uint neighbour=sparsePageSlot(pageCoord+delta,pageSize);
        if(neighbour==0u)continue;
        ivec3 base=sparseAtlasCell(neighbour,localCell-delta*int(brickSide));
        for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            ivec3 d=ivec3(x,y,z),voxel=localCell+d-delta*int(brickSide);
            if(any(lessThan(voxel,ivec3(0)))||any(greaterThanEqual(voxel,ivec3(int(brickSide)))))continue;
            float weight=(x<0 ? left.x : x>0 ? right.x : middle.x)*
                         (y<0 ? left.y : y>0 ? right.y : middle.y)*
                         (z<0 ? left.z : z>0 ? right.z : middle.z);
            density+=weight*texelFetch(sparseCloudAtlas,base+d,0).r;
        }
    }
    return density;
}
float fallbackCloudDensityAt(vec3 point,vec4 advection,vec4 properties,float footprintMetres) {
    if(properties.x<=0. || properties.w<=properties.z)return 0.;
    if(useCloudObjects())return localObjectCloudDensity(objectAxes(point-sparseCloud.objectOriginCount.xyz),
                                                       1.-smoothstep(16.,82.,footprintMetres));
    float altitude=length(point)-cloudRadius(normalize(point));
    if(altitude<=properties.z || altitude>=properties.w)return 0.;
    float domainBlend;
    float fine=sparseCloudDensityAt(point,domainBlend);
    bool dense=sparseCloud.midTopExtent.w>.5;
    if(domainBlend>=1. && !dense)return fine;
    float regional=regionalCloudDensityAt(point,advection,properties,footprintMetres,altitude);
    // A fine object fills only its own footprint. Air in the sparse atlas
    // cannot erase the entire bank around it at the regional hand-off.
    if(dense)fine=max(fine,regional*(1.-.42*smoothstep(.006,.10,fine)));
    return mix(regional,fine,domainBlend);
}
layout(set=CLOUD_DESCRIPTOR_SET,binding=7) uniform sampler3D gpuCloudState;
layout(set=CLOUD_DESCRIPTOR_SET,binding=8) uniform GpuCloudLayout {
    vec4 anchorEnabled; // xyz ECEF anchor, w=enabled
    vec4 eastCell;      // xyz east axis, w=voxel metres
    vec4 upWidth;       // xyz up axis, w=grid width
    vec4 northHeight;   // xyz north axis, w=grid height
    vec4 originDepth;   // xyz local ENU origin, w=grid depth
} gpuCloud;

float cloudDensityAt(vec3 point,vec4 advection,vec4 properties,float footprintMetres) {
    float fallback=fallbackCloudDensityAt(point,advection,properties,footprintMetres);
    if(gpuCloud.anchorEnabled.w<=0. || gpuCloud.eastCell.w<=0.)return fallback;
    float gpuAuthority=clamp(gpuCloud.anchorEnabled.w,0.,1.);
    vec3 relative=point-gpuCloud.anchorEnabled.xyz;
    vec3 local=vec3(dot(relative,gpuCloud.eastCell.xyz),dot(relative,gpuCloud.upWidth.xyz),
                    dot(relative,gpuCloud.northHeight.xyz));
    vec3 dimensions=vec3(gpuCloud.upWidth.w,gpuCloud.northHeight.w,gpuCloud.originDepth.w);
    vec3 voxel=(local-gpuCloud.originDepth.xyz)/gpuCloud.eastCell.w;
    if(any(lessThan(voxel,vec3(0.)))||any(greaterThanEqual(voxel,dimensions)))return fallback;
    // Cell-centred texture coordinates: physical position/cell already puts
    // a cell centre at i+.5. Do not introduce another half-cell displacement.
    vec3 edge=min(voxel,dimensions-voxel);
    float authority=gpuAuthority*smoothstep(0.,4.,min(edge.x,min(edge.y,edge.z)));
    // All normalized density consumers use the same reference LWC.
    vec3 centre=floor(voxel),left,middle,right;
    cloudSplineWeights(voxel-vec3(.5)-centre,left,middle,right);
    float physical=max(samplePaddedCloud(gpuCloudState,centre,dimensions,left,middle,right).g,0.)/
                   sparseCloud.microphysics.x;
    return mix(fallback,physical,authority);
}


float cloudVisibilityFromPoint(vec3 point,vec3 light,vec4 advection,vec4 properties,float footprintMetres) {
    if(properties.x<=0. || properties.w<=properties.z || dot(point,light)<=0.)return 1.;
    vec3 n=normalize(point);
    float altitude=length(point)-cloudRadius(n);
    if(altitude>=properties.w)return 1.;
    float mu=max(dot(n,light),.04);
    float start=max(properties.z-altitude,0.)/mu;
    float distance=min((properties.w-max(properties.z,altitude))/mu,80000.);
    if(distance<=0.)return 1.;
#ifdef CLOUD_VIEW_FILTER
    // Resolve the first few metres of condensate as sunlight enters a
    // billow. The remaining samples cover the much longer regional path.
    const int nearSteps=8,farSteps=8;
    float nearDistance=min(distance,850.);
    float opticalDepth=0.;
    for(int i=0;i<nearSteps+farSteps;++i) {
        bool near=i<nearSteps;
        float u=float(near ? i : i-nearSteps)/float(near ? nearSteps : farSteps);
        float v=u+1./float(near ? nearSteps : farSteps);
        float segment=near ? nearDistance : distance-nearDistance;
        if(segment<=0.)break;
        float offset=near ? start : start+nearDistance;
        float lo=offset+u*u*segment,hi=offset+v*v*segment;
        float dt=hi-lo;
        opticalDepth+=cloudExtinctionPerMetre(
            cloudDensityAt(point+light*(.5*(lo+hi)),advection,properties,
                           max(footprintMetres,.25*dt)),properties.y)*dt;
        if(opticalDepth>=16.)break;
    }
#else
    int lightSteps=footprintMetres<2000. ? 16 : 6;
    float dt=distance/float(lightSteps);
    float opticalDepth=0.;
    for(int i=0;i<16;++i) {
        if(i>=lightSteps)break;
        opticalDepth+=cloudExtinctionPerMetre(
            cloudDensityAt(point+light*(start+(float(i)+.5)*dt),advection,properties,
                           max(footprintMetres,dt*.25)),properties.y)*dt;
    }
#endif
    return exp(-min(opticalDepth,16.));
}
#ifndef CLOUD_COMPUTE
float cloudVisibilityFromPoint(vec3 point,vec3 light,vec4 advection,vec4 properties) {
    return cloudVisibilityFromPoint(point,light,advection,properties,length(fwidth(point)));
}
float cloudVisibility(vec3 n,vec3 light,vec4 advection,vec4 properties) {
    vec3 point=n*cloudRadius(n);
    return cloudVisibilityFromPoint(point,light,advection,properties,length(fwidth(point)));
}

#endif
