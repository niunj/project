#pragma once
#include <vsgraysim/earth/OrbitalSurface.h>
#include <vsgraysim/earth/PlanetWeather.h>
#include <array>
#include <memory>
#include <optional>
#include <vsg/core/Array.h>
#include <vsg/nodes/StateGroup.h>

namespace vsgraysim::earth {
class OrbitalGlobe {
public:
    explicit OrbitalGlobe(std::shared_ptr<const OrbitalSurface> source,
                         std::shared_ptr<rockyenv::CloudGpuResources> cloud={});
    void setWeather(const PlanetWeatherFrame&);
    void setLighting(const vsg::vec3& moon,float phase,float exposure,float turbidity);
    void setSun(const vsg::vec3& direction);
    bool intersect(const vsg::dvec3& origin,const vsg::dvec3& direction,vsg::dvec3& hit) const;
    using TileKey = std::array<int,3>; // global-geodetic z, x, north-first y
    void setWindows(const TileKey& active, std::optional<TileKey> observer = {});
    vsg::ref_ptr<vsg::StateGroup> node;
    std::shared_ptr<const OrbitalSurface> source;
private:
    vsg::ref_ptr<vsg::vec4Array> windows_;
};
}
