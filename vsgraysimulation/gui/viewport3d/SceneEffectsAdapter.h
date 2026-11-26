#pragma once

#include "AviationParticles.h"
#include <map>
#include <memory>
#include <optional>

namespace vsgraysim::viewport3d {

class ViewportContext;

struct SceneEffectFrame {
    double time;
    bool resetHistory = false;
    std::optional<vsg::vec3> wind;
    std::vector<vsg::vec3> fractureFlashes;
    size_t burningDebrisCount = 0;
};

// Converts scene snapshots into renderer-neutral inputs for the viewport effects layer.
class SceneEffectsAdapter {
public:
    SceneEffectFrame collect(ViewportContext&, std::vector<AviationParticles::Source>&, double time);
    void reset();

private:
    struct DebrisSocket {
        std::weak_ptr<const void> shape;
        vsg::dvec3 local;
    };
    std::map<std::string,double> burnStart_;
    std::map<std::string,DebrisSocket> debrisSockets_;
    uint64_t lastTick_ = 0, lastEvent_ = 0;
    size_t readEvents_ = 0;
    bool seenScene_ = false;
};

} // namespace vsgraysim::viewport3d
