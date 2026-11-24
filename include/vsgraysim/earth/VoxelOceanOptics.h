#pragma once
#include "VoxelScene.h"
namespace vsgraysim::earth {
struct OceanRayResult {
    std::optional<VoxelSceneHit> hit;
    double pathLength=0,waterLength=0,opticalLength=0,marineFogOpticalDepth=0;
    unsigned interfaces=0;
    bool totalInternalReflection=false;
    vsg::dvec3 transmission{1,1,1};
};
// Medium-aware geometric measurement. Refraction uses the same instantaneous
// wave height/normal as display and buoyancy; range includes each ray segment.
OceanRayResult traceVoxelOceanScene(const VoxelSceneSnapshot&,const vsg::dvec3& origin,
                                    const vsg::dvec3& direction,double maximumDistance);
}
