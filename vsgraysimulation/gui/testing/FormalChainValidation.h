#pragma once
namespace vsgraysim::gui {
class SceneDesignerWindow;
class SimulationControlWindow;
namespace display {class ControlWorkbench;}
// Explicit, BUILD_TESTING-only automation of the existing windows and slots.
void installFormalValidation(SceneDesignerWindow&);
void installFormalValidation(SimulationControlWindow&);
void installFormalValidation(display::ControlWorkbench&);
}
