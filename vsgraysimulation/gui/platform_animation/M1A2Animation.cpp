#include "PlatformAnimation.h"

namespace vsgraysim::platform_animation {
namespace {

class M1A2Animation final : public PlatformAnimation {
public:
    void sample(const AnimationInput& input, AnimationFrame& frame) const override {
        frame.joints.clear();
        frame.effects.clear();
        if(input.displayAngles) {
            frame.joints.push_back({"turret",{}, {0.,1.,0.},(*input.displayAngles)[0]});
            frame.joints.push_back({"elevation",{}, {-1.,0.,0.},(*input.displayAngles)[1]});
            frame.joints.push_back({"recoil",{}});
            return;
        }
        if (!input.previewCue) return;
        const PreviewCue cue(input);
        const double t = input.time;
        const double sweep = .54*smoothStep((t-.18)/.80)
                           -.87*smoothStep((t-1.60)/.88)
                           +.33*smoothStep((t-2.65)/.65);
        const double elevation = .105*smoothStep((t-.48)/.52)
                               -.105*smoothStep((t-1.64)/.75);
        const double recoil = .32*smoothStep(cue.age/.09)*(1.-smoothStep((cue.age-.09)/.53));
        frame.joints.push_back({"turret",{}, {0.,1.,0.}, sweep});
        frame.joints.push_back({"elevation",{}, {-1.,0.,0.}, elevation});
        frame.joints.push_back({"recoil",{0.,0.,-recoil}});
        if (cue.crossed)
            frame.effects.push_back({"flash","muzzle",{}, {0.,0.,1.}, EffectStyle::Flash,1.f,.86f});
    }
};

} // namespace

std::unique_ptr<PlatformAnimation> makeM1A2Animation() { return std::make_unique<M1A2Animation>(); }

} // namespace vsgraysim::platform_animation
