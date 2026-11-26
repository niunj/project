#pragma once
#include "PlatformAnimation.h"

namespace vsgraysim::platform_animation {

class DisplayVehicleAnimation final : public PlatformAnimation {
public:
    void sample(const AnimationInput& input, AnimationFrame& frame) const override {
        frame.joints.clear();
        frame.effects.clear();
        if(!input.previewCue)return;
        const PreviewCue cue(input);
        const double rise=smoothStep(cue.age/.8);
        const bool onStage=cue.age>=0.&&cue.age<.9;
        frame.joints.push_back({"voxel_prop",{0.,3.2*rise,0.},{0.,1.,0.},0.,onStage});
        if(cue.crossed)
            frame.effects.push_back({"display_cue","stage",{}, {0.,1.,0.},EffectStyle::Flash,1.f,.48f});
        if(onStage)
            frame.effects.push_back({"stage_mist","stage",{}, {0.,1.,0.},EffectStyle::WarmExhaust,
                float(.6*(1.-rise)),.3f,{.55f,.45f,.75f}});
    }
};

} // namespace vsgraysim::platform_animation
