#include "CockpitControlSignals.h"
#include "FlightPanel.h"

namespace vsgraysim::gui::display {
namespace {

const char* flightSystemChannel(platform_animation::AircraftSystem system) {
    using platform_animation::AircraftSystem;
    switch(system) {
    case AircraftSystem::LandingGear: return "gear_down";
    case AircraftSystem::NavigationLights: return "nav_lights";
    case AircraftSystem::LandingLight: return "landing_light";
    default: return nullptr;
    }
}

} // namespace

CabinCommandResult dispatchCabinCommand(const platform_animation::CabinCommand& request, FlightPanel& controls) {
    using platform_animation::CabinSignal;
    switch(request.signal) {
    case CabinSignal::AircraftSystem: {
        const auto* channel=flightSystemChannel(request.system);
        return channel && controls.toggleAircraftSystem(channel)
            ? CabinCommandResult::Submitted : CabinCommandResult::SystemRejected;
    }
    case CabinSignal::FlightHold:
        return controls.toggleCockpitHold() ? CabinCommandResult::HoldSubmitted : CabinCommandResult::HoldRejected;
    default:
        return CabinCommandResult::Unhandled;
    }
}

} // namespace vsgraysim::gui::display
