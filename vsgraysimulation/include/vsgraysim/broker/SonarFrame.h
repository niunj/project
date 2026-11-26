#pragma once

#include <cmath>
#include <cstdint>

namespace vsgraysim {

enum class BottomReturn : uint8_t { Valid, DryTransducer, NoBottom };

// Civilian vertical depth survey. One result per sounding, metres from the
// transducer and the instantaneous water surface respectively.
struct SonarFrame {
    uint32_t sensor_id=0, platform_id=0;
    uint64_t frame_id=0, tick=0, surface_revision=0;
    double sim_time=0., range_limit_m=0., transducer_draft_m=0.;
    double clearance_m=0., water_depth_m=0., two_way_ms=0.;
    BottomReturn bottom=BottomReturn::NoBottom;
    uint8_t stage=0; // Common EquipmentSession product stage.
    bool water_column_enabled=false;
    double echo_range_m=0.; // Zero when there is no near water-column return.

    double echoDepthM() const {return transducer_draft_m+echo_range_m;}
    double echoTwoWayMs() const {return echo_range_m*(2./1.5);}

    bool valid()const {
        if(!sensor_id||!platform_id||!frame_id||!surface_revision||!std::isfinite(sim_time)||sim_time<0.||
            !std::isfinite(range_limit_m)||range_limit_m<1.||range_limit_m>300.||
            !std::isfinite(transducer_draft_m)||transducer_draft_m<0.||transducer_draft_m>1000.||
            !std::isfinite(echo_range_m)||echo_range_m<0.||echo_range_m>range_limit_m)return false;
        if(echo_range_m>0.&&(!water_column_enabled||bottom!=BottomReturn::NoBottom||transducer_draft_m<.02))return false;
        if(bottom==BottomReturn::DryTransducer||bottom==BottomReturn::NoBottom)
            return clearance_m==0.&&water_depth_m==0.&&two_way_ms==0.;
        return bottom==BottomReturn::Valid&&std::isfinite(clearance_m)&&clearance_m>0.&&clearance_m<=range_limit_m&&
            std::abs(water_depth_m-clearance_m-transducer_draft_m)<1e-6&&
            std::abs(two_way_ms-clearance_m*(2./1.5))<1e-6;
    }
};

}
