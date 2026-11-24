#include "vsgraysim/layer4_sched/SensorScheduler.h"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace vsgraysim {
namespace {
BandMask defaultBand(SensorType type) {
    switch(type) {
        case SensorType::IR: return BAND_IR;
        case SensorType::RADAR: return BAND_RADAR;
        case SensorType::SAR: return BAND_SAR;
        default: return BAND_VIS;
    }
}
}
SensorScheduler::SensorScheduler(CrossInductionRulesPtr rules):rules_(std::move(rules)) {}
void SensorScheduler::setImageCadence(uint32_t id,double period,double exposure) {
    if (configured_) throw std::logic_error("configure image cadence before simulation starts");
    if (!std::isfinite(period) || period<0 || !std::isfinite(exposure) || exposure<0)
        throw std::invalid_argument("invalid image cadence");
    image_cadences_[id]={period,exposure};
}
void SensorScheduler::buildFrameSchedule(double dt,std::optional<double> frame_end) {
    schedule_.clear(); cursor_=0;
    if (!std::isfinite(dt) || dt<=0) throw std::invalid_argument("invalid transmit interval");
    const auto begin=time_;
    const auto end=frame_end ? simcore::fromSeconds(*frame_end) : begin+simcore::fromSeconds(dt);
    if (end<=begin || std::abs(simcore::toSeconds(end-begin)-dt)>2e-9)
        throw std::invalid_argument("transmit interval disagrees with simulation time");
    if (!rules_) { time_=end; return; }
    if (!configured_) {
        for (const auto& sensor:rules_->sensors()) {
            const bool image=sensor.type==SensorType::IR || sensor.type==SensorType::VIS;
            if (!std::isfinite(sensor.tx_params.power) || sensor.tx_params.power<0)
                throw std::invalid_argument("invalid transmit power");
            if (!image && sensor.tx_params.power==0) continue;
            simcore::SensorRuntime::Cadence cadence;
            cadence.sensor_id=sensor.sensor_id;
            if (image) {
                const auto spec=image_cadences_[sensor.sensor_id];
                const double period=spec.period>0 ? spec.period : dt;
                cadence.timeline=simcore::PeriodicTimeline::fromHz(1/period);
                cadence.exposure=simcore::fromSeconds(spec.exposure);
                cadence.first_index=std::max<uint64_t>(1,uint64_t(std::ceil(spec.exposure/period)));
                while(cadence.timeline.at(cadence.first_index)<cadence.exposure) ++cadence.first_index;
                images_.add(cadence);
            } else {
                const double rate=sensor.tx_params.pulse_rate;
                if (!std::isfinite(rate) || rate<0) throw std::invalid_argument("invalid PRF");
                const auto fallback=image_cadences_.find(sensor.sensor_id);
                const double period=fallback!=image_cadences_.end() && fallback->second.period>0 ? fallback->second.period : dt;
                cadence.timeline=simcore::PeriodicTimeline::fromHz(rate>0 ? rate : 1/period);
                cadence.first_index=0;
                pulses_.add(cadence);
            }
        }
        configured_=true;
    }
    std::map<uint32_t,uint32_t> sequences;
    auto drain=[&](simcore::SensorRuntime& runtime,simcore::TimePoint target,bool image) {
        do {
            for (const auto& event:runtime.advanceTo(target)) {
                if (schedule_.size()>=1000000) throw std::runtime_error("measurement work budget exceeded");
                const auto* sensor=rules_->findSensor(event.sensor_id);
                ScheduledTx tx{sensor->sensor_id,sensor->type,uint32_t(defaultBand(sensor->type)),
                    sensor->tx_params.power,sensor->tx_params.beam_width,sensor->tx_params.wavelength,
                    simcore::toSeconds(event.end-begin),0,sequences[event.sensor_id]++};
                tx.absolute_time=simcore::toSeconds(event.end);
                tx.pulse_index=image ? event.frame_id : event.index;
                tx.measurement_start=simcore::toSeconds(event.start);
                tx.image_measurement=image;
                schedule_.push_back(tx);
            }
        } while(runtime.backlogged());
    };
    // Emission lattice is [begin,end); completed exposures are (begin,end].
    drain(pulses_,end-1,false); drain(images_,end,true);
    std::sort(schedule_.begin(),schedule_.end(),[](const auto& a,const auto& b) {
        if(a.absolute_time!=b.absolute_time) return a.absolute_time<b.absolute_time;
        return a.sensor_id<b.sensor_id;
    });
    time_=end;
    spdlog::debug("Layer4: scheduled {} measurements over {} s",schedule_.size(),dt);
}
const ScheduledTx* SensorScheduler::nextDue(double t) const {
    if(cursor_<schedule_.size() && schedule_[cursor_].fire_time<=t) return &schedule_[cursor_++];
    return nullptr;
}
}
