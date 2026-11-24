#include "rocky_env/SensorScheduler.h"

#include <cmath>

namespace rockyenv::sensor {
bool SensorScheduler::configure(const SensorDesc& desc) {
    period_=0; backlogged_=false; runtime_=simcore::SensorRuntime{};
    sensor_id_=uint32_t(desc.sensor_id);
    coherent_=desc.frame_kind=="RADAR" || desc.frame_kind=="SAR";
    if (!std::isfinite(desc.rate_hz) || !std::isfinite(desc.pulse_rate)) return false;
    try {
        simcore::SensorRuntime::Cadence cadence;
        cadence.sensor_id=sensor_id_;
        if (coherent_) {
            const double prf=desc.pulse_rate>0 ? desc.pulse_rate : desc.rate_hz;
            cpiPulses_=desc.cpi_pulses;
            cadence.timeline=simcore::PeriodicTimeline::fromHz(prf).dividedBy(cpiPulses_);
            cadence.contiguous_windows=true; cadence.samples=cpiPulses_;
        } else {
            const double exposure=desc.integration_ms*1e-3;
            if (!std::isfinite(exposure) || exposure<=0) return false;
            cadence.exposure=simcore::fromSeconds(exposure);
            cadence.timeline=simcore::PeriodicTimeline::fromHz(desc.rate_hz>0 ? desc.rate_hz : 1/exposure);
            cadence.first_index=std::max<uint64_t>(1,uint64_t(std::ceil(exposure/cadence.timeline.periodSeconds())));
            while(cadence.timeline.at(cadence.first_index)<cadence.exposure) ++cadence.first_index;
        }
        period_=cadence.timeline.periodSeconds();
        runtime_.add(cadence);
        return true;
    } catch(const std::exception&) { period_=0; return false; }
}
std::vector<SchedulerEvent> SensorScheduler::update(double simTime) {
    std::vector<SchedulerEvent> out;
    if (!valid()) return out;
    for (const auto& event:runtime_.advanceTo(simcore::fromSeconds(simTime))) {
        out.push_back({coherent_ ? SchedulerEvent::Kind::CpiBoundary : SchedulerEvent::Kind::Image,
            simcore::toSeconds(event.end),simcore::toSeconds(event.start),event.samples,event.frame_id});
    }
    backlogged_=runtime_.backlogged();
    return out;
}
}
