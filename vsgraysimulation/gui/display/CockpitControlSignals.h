#pragma once

#include "platform_animation/CockpitModel.h"

namespace vsgraysim::gui::display {

class FlightPanel;

enum class CabinCommandResult { Submitted, HoldSubmitted, SystemRejected, HoldRejected, Unhandled };

// Flight requests use the same lease and acknowledgement path as the Qt instruments.
CabinCommandResult dispatchCabinCommand(const platform_animation::CabinCommand&, FlightPanel&);

} // namespace vsgraysim::gui::display
