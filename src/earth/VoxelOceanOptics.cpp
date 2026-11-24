#include <vsgraysim/earth/VoxelOceanOptics.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace vsgraysim::earth {
namespace {
double marineDepth(const VoxelOcean& water,const vsg::dvec3& a,const vsg::dvec3& b){
    if(water.fogVisibility<=0)return 0;const double h0=std::max(0.,a.y-water.meanSurface(a.x,a.z).x)/water.fogHeight,h1=std::max(0.,b.y-water.meanSurface(b.x,b.z).x)/water.fogHeight,d=h1-h0;const double density=std::abs(d)<.001?std::exp(-.5*(h0+h1)):(std::exp(-h0)-std::exp(-h1))/d;return std::min(60.,3.912*vsg::length(b-a)/water.fogVisibility*std::max(0.,density));
}
}

OceanRayResult traceVoxelOceanScene(const VoxelSceneSnapshot& scene,const vsg::dvec3& origin,const vsg::dvec3& direction,double maximum){
    // Use the existing validator even for an empty geometry set.
    raycastVoxelScene(scene,origin,direction,maximum);
    OceanRayResult out;auto p=origin,d=vsg::normalize(direction);bool wet=scene.ocean&&scene.ocean->densityAt(p,scene.timeSeconds())>0;
    for(unsigned crossing=0;crossing<8;++crossing){const double left=maximum-out.pathLength;auto hit=raycastVoxelScene(scene,p,d,left);double surface=left;const bool boundary=scene.ocean&&scene.ocean->intersectSurface(p,d,scene.timeSeconds(),left,surface);
        const bool solid=hit&&(!boundary||hit->distance<=surface);const double segment=solid?hit->distance:boundary?surface:left;
        if(wet){double edge=left;for(int a:{0,2})if(std::abs(d[a])>1e-12)edge=std::min(edge,((d[a]>0?scene.ocean->halfExtent:-scene.ocean->halfExtent)-p[a])/d[a]);if(d.y< -1e-12)edge=std::min(edge,(scene.ocean->bottom-p.y)/d.y);if(segment>edge+1e-5)throw std::out_of_range("underwater optical ray leaves the declared ocean working volume");}
        out.pathLength+=segment;if(wet)out.waterLength+=segment;else if(scene.ocean)out.marineFogOpticalDepth+=marineDepth(*scene.ocean,p,p+d*segment);
        if(solid){out.hit=hit;out.hit->distance=out.pathLength;break;}if(!boundary)break;
        const auto point=p+d*segment;auto n=scene.ocean->sample(point.x,point.z,scene.timeSeconds()).normal;if(wet)n=-n;const double eta=wet?1.333:1./1.333,cosine=-vsg::dot(n,d),discriminant=1-eta*eta*(1-cosine*cosine);
        if(discriminant<0){d=d+n*(2*cosine);out.totalInternalReflection=true;}else{d=d*eta+n*(eta*cosine-std::sqrt(discriminant));wet=!wet;}
        ++out.interfaces;const double offset=std::min(1e-5,maximum-out.pathLength);p=point+d*offset;out.pathLength+=offset;if(wet)out.waterLength+=offset;else out.marineFogOpticalDepth+=marineDepth(*scene.ocean,point,p);if(out.pathLength>=maximum)break;
        if(crossing==7)throw std::runtime_error("ocean optical ray exceeded eight interface crossings");
    }
    out.opticalLength=out.pathLength+.333*out.waterLength;if(scene.ocean)out.transmission=scene.ocean->transmission(out.waterLength)*std::exp(-out.marineFogOpticalDepth);return out;
}
}
