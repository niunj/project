#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace rockyenv {
// Rendering cadence never changes the amount of physical time simulated.
// The per-frame budget bounds catch-up work; unprocessed time stays pending.
class CloudStepClock {
public:
    static constexpr double stepSeconds=1.0/20.0;
    uint32_t advance(double seconds,bool active,bool reset=false) {
        if(!std::isfinite(seconds))throw std::invalid_argument("invalid cloud clock");
        if(!active){last_=seconds;active_=false;pending_=0.;return 0;}
        if(reset||!active_||seconds<last_) {
            last_=seconds;pending_=0.;active_=true;return 1;
        }
        pending_+=seconds-last_;last_=seconds;
        const auto count=uint32_t(std::min(8.,std::floor((pending_+1e-10)/stepSeconds)));
        pending_=std::max(0.,pending_-count*stepSeconds);
        return count;
    }
    double pendingSeconds() const {return pending_;}
private:
    double last_=0.,pending_=0.;
    bool active_=false;
};
}
