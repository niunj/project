#include <vsgraysim/earth/EnvironmentTerrain.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim::earth {
namespace {
float height(const SurfaceSample& sample) {
    return sample.status==SurfaceStatus::NoData ? std::numeric_limits<float>::quiet_NaN() : float(sample.height);
}
}
uint64_t EnvironmentTerrain::revision() const { return surface_ ? surface_->revision : 0; }

float EnvironmentTerrain::heightENU(double x,double z) const {
    return height(surface_ ? surface_->sampleLocal(x,z) : SurfaceSample{});
}

float EnvironmentTerrain::waterBedENU(double x,double z,float level) const {
    return height(surface_ ? surface_->waterBedLocal(x,z,level) : SurfaceSample{});
}

vsg::vec3 EnvironmentTerrain::normalENU(double x,double z) const {
    return surface_ ? vsg::vec3(surface_->sampleLocal(x,z).normal) : vsg::vec3(0,1,0);
}

bool EnvironmentTerrain::isLand(double x,double z,float level) const {
    return waterBedENU(x,z,level)>level;
}

float EnvironmentTerrain::waterDepth(double x,double z,float level) const {
    const float bed=waterBedENU(x,z,level);
    return std::isfinite(bed) ? std::max(0.f,level-bed) : std::numeric_limits<float>::quiet_NaN();
}
}
