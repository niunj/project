#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include <rocky_env/EnvApi.h>

namespace vsgraysim::earth {
// Queries are in the pinned snapshot's tangent frame. Bind at publication or
// before onFrame on the host's update thread.
class EnvironmentTerrain final : public rockyenv::ITerrainBridge {
public:
    void bind(EarthSurfaceSnapshotPtr surface) { surface_=std::move(surface); }
    uint64_t revision() const override;
    float heightENU(double x,double z) const override;
    float waterBedENU(double x,double z,float seaLevel) const override;
    vsg::vec3 normalENU(double x,double z) const override;
    bool isLand(double x,double z,float seaLevel) const override;
    float waterDepth(double x,double z,float seaLevel) const override;
private:
    EarthSurfaceSnapshotPtr surface_;
};
}
