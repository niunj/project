#include "PlatformAnimation.h"

#include <cmath>


namespace vsgraysim::platform_animation {
namespace {
constexpr double pi = 3.14159265358979323846;

class CG57Animation final : public PlatformAnimation {
public:
    void sample(const AnimationInput& input, AnimationFrame& frame) const override {
        frame.joints.clear();
        frame.effects.clear();
        frame.joints.push_back({"antenna",{}, {0.,1.,0.},std::fmod(input.time*1.55,2.*pi)});
        if (!input.previewCue) return;
        const PreviewCue cue(input);
        const double hatch = smoothStep((cue.age+.70)/.49)*(1.-smoothStep((cue.age-1.26)/.57));
        frame.joints.push_back({"left_cover",{}, {0.,0.,1.},hatch*1.36});
        frame.joints.push_back({"right_cover",{}, {0.,0.,1.},-hatch*1.36});
        const double path = smoothStep(cue.age/1.25);
        const bool visible = cue.age>=0. && cue.age<1.15;
        frame.joints.push_back({"prop",{.8*path,28.*path,.60*path}, {1.,0.,0.},pi*.5,visible});
        if (cue.crossed)
            frame.effects.push_back({"flash","prop_origin",{}, {0.,1.,0.},EffectStyle::Flash,1.f,1.33f});
        if (visible && cue.age>0.)
            frame.effects.push_back({"prop","prop",{0.,0.,1.01}, {0.,0.,-1.},EffectStyle::LaunchExhaust,
                                     float(1.-smoothStep((cue.age-.76)/.39)),.48f,{std::nullopt,1.12f,1.14f}});
    }
};

} // namespace

std::unique_ptr<PlatformAnimation> makeCG57Animation() { return std::make_unique<CG57Animation>(); }

} // namespace vsgraysim::platform_animation
