#pragma once
#include <memory>
#include <array>
#include <functional>
#include <nlohmann/json.hpp>
#include <vsgraysim/layer0_phys/PhysParamManager.h>
#include <vsgraysim/layer3b_signal/SignalFrame.h>
namespace vsgraysim {
class Simulation;
namespace earth {class EarthSurfaceSnapshot;}
/// Host bridge: physics owns truth, rocky_env owns Vulkan execution.
class SimulationGpuAdapter {
public:
    SimulationGpuAdapter(Simulation&,PhysParamManagerPtr,const nlohmann::json& scenario);
    ~SimulationGpuAdapter();
    std::array<double,3> footprint(uint32_t id) const;
    struct PreparedTerrain {
        std::function<bool()> stillCurrent;
        std::function<void()> commit;
    };
    using TerrainBuilder = std::function<PreparedTerrain(const earth::EarthSurfaceSnapshot&)>;
    TerrainBuilder captureTerrainBuilder();
    void advanceTo(double,const std::function<void(SignalFrame)>&,
                   const std::function<bool(uint32_t)>&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
