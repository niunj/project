#include "F16CAnimation.h"
#include <algorithm>

namespace vsgraysim::platform_animation {
namespace {

class F16CAnimation final : public PlatformAnimation {
public:
    std::optional<Vec3> modelAttachment(std::string_view name, double modelLength) const override {
        if(name!="engine_nozzle")return {};
        const double scale=modelLength/15.;
        return Vec3{f16NozzleAt15m[0]*scale,f16NozzleAt15m[1]*scale,f16NozzleAt15m[2]*scale};
    }

    void sample(const AnimationInput& input, AnimationFrame& frame) const override {
        frame.joints.clear();
        frame.effects.clear();
        if (input.throttle>.73)
            frame.effects.push_back({"engine","engine_nozzle",{}, {0.,0.,-1.}, EffectStyle::Jet,
                                     float(smoothStep((input.throttle-.73)/.27)),.54f,{.12f,.42f,.53f}});
        if (!input.previewCue) return;
        const PreviewCue cue(input);
        const double path = smoothStep(cue.age/1.25);
        const bool visible = cue.age<1.25;
        frame.joints.push_back({"prop",{-path,-2.3*path,-17.*path}, {1.,0.,0.},0.,visible});
        if (cue.crossed)
            frame.effects.push_back({"flash","prop_origin",{}, {0.,0.,-1.}, EffectStyle::Flash,1.f,.43f});
        if (visible && cue.age>0.)
            frame.effects.push_back({"prop","prop",{0.,0.,1.01}, {0.,0.,-1.}, EffectStyle::WarmExhaust,
                                     float(1.-smoothStep((cue.age-.76)/.39)),.24f});
    }
};

class F16CCockpitModel final : public CockpitModel {
public:
    CabinCommand command(CabinAction action) const override {
        switch (action) {
        case CabinAction::None: return {};
        case CabinAction::PanelLight: return {CabinSignal::PanelLight};
        case CabinAction::Hud: return {CabinSignal::Hud};
        case CabinAction::FlightHold: return {CabinSignal::FlightHold};
        case CabinAction::LandingGear: return {CabinSignal::AircraftSystem,AircraftSystem::LandingGear};
        case CabinAction::NavLights: return {CabinSignal::AircraftSystem,AircraftSystem::NavigationLights};
        case CabinAction::LandingLight: return {CabinSignal::AircraftSystem,AircraftSystem::LandingLight};
        default: return {CabinSignal::SelectPage};
        }
    }

    CabinPose pose(const CabinState& state) const override {
        return {std::clamp(state.aileron,-1.,1.)*.16, std::clamp(state.elevator,-1.,1.)*.18,
                -std::clamp(state.throttle,0.,1.)*.21,
                (int(state.lamp)-1)*1.14, state.hud?-.42:.36, state.hold?-.46:.36,
                state.gear?.30:-.36, state.navLight?-.42:.36, state.landingLight?-.42:.36};
    }
};

} // namespace

std::unique_ptr<PlatformAnimation> makeF16CAnimation() { return std::make_unique<F16CAnimation>(); }

const CockpitModel& f16cCockpitModel() {
    static const F16CCockpitModel model;
    return model;
}

} // namespace vsgraysim::platform_animation
