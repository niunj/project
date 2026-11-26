#pragma once

#include "visual_effects/ExhaustAppearance.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace vsg { class Node; class Draw; template<class T> class Array; }
namespace vsgraysim::viewport3d {
class AviationParticles {
public:
    enum class Style { Jet, WarmExhaust, Soot, BurningDebris, LaunchExhaust, Condensation };
    enum class Burst { Launch, Impact, Fracture, StageFlash };
    struct Source {
        std::string id;
        vsg::vec3 nozzle, forward;
        Style style=Style::Jet;
        float strength=1, radius=.45f;
        vsg::vec3 velocity{};
        visual_effects::ExhaustAppearance exhaust{};
    };
    AviationParticles();
    bool initialize();
    vsg::Node* node() const;
    void clear();
    void burst(vsg::vec3 centre,Burst kind,vsg::vec3 direction={},float scale=1.f);
    void update(double now,const std::vector<Source>&,vsg::vec3 wind,vsg::vec3 eye,
                vsg::vec3 sunDirection,vsg::vec3 sky,vsg::vec3 sun,vsg::vec3 haze,float visibility);
    size_t historyCount() const {return smoke_.size();}
    unsigned visibleCount() const;
    unsigned visibleJetFlames() const { return visibleJetFlames_; }
    ~AviationParticles();
private:
    struct Puff {
        vsg::vec3 origin, velocity, drift;
        double born;
        float life,radius,kind,seed,opacity,spread;
    };
    struct Track {
        vsg::vec3 nozzle;
        double time;
        float remainder=0;
        vsg::vec3 forward{};
    };
    std::map<std::string,Track> tracks_;
    std::deque<Puff> smoke_;
    struct Event {vsg::vec3 origin; Burst kind;vsg::vec3 direction;float scale;};
    std::vector<Event> pending_;
    double last_=-1;
    unsigned seed_=1,visibleJetFlames_=0;
    vsg::ref_ptr<vsg::Node> root_;
    vsg::ref_ptr<vsg::Draw> draw_;
    vsg::ref_ptr<vsg::Array<vsg::vec4>> particles_, lighting_;
};
}
