#pragma once

#include <string_view>

namespace vsgraysim::platform_animation {

enum class CabinAction {
    None, LeftFlight, LeftControl, LeftEngine, RightFlight, RightControl, RightEngine,
    PanelLight, Hud, FlightHold, LandingGear, NavLights, LandingLight
};

enum class CabinSignal { None, SelectPage, PanelLight, Hud, FlightHold, AircraftSystem };

enum class AircraftSystem { None, LandingGear, NavigationLights, LandingLight };

struct CabinCommand {
    CabinSignal signal = CabinSignal::None;
    AircraftSystem system = AircraftSystem::None;
};

struct CabinState {
    double throttle = .5, elevator = 0., aileron = 0.;
    unsigned lamp = 1;
    bool hud = true, hold = false, gear = false, navLight = false, landingLight = false;
};

struct CabinPose {
    double stickRoll, stickPitch, throttleOffset;
    double lamp, hud, hold, gear, navLight, landingLight;
};

class CockpitModel {
public:
    virtual ~CockpitModel() = default;
    virtual CabinCommand command(CabinAction) const = 0;
    virtual CabinPose pose(const CabinState&) const = 0;
};

const CockpitModel* findCockpitModel(std::string_view modelId);

} // namespace vsgraysim::platform_animation
