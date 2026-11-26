#pragma once

#include <vsgraysim/model_sdk/SoundMount.h>

namespace vsgraysim::model_packages {

inline constexpr model_sdk::Vec3 f16NozzleAt15m{-.034,-1.018,6.67};

inline model_sdk::AirframeSoundMounts fixedWingSoundMounts(std::string_view model) {
    using model_sdk::SoundMount;
    using model_sdk::SoundMountKind;
    if(model=="f16c")return {SoundMount{"engine_nozzle",f16NozzleAt15m,SoundMountKind::Local,15.},{}};
    if(model=="a320")return {SoundMount{"engine_left",{-5.3,-1.5,1.2},SoundMountKind::Local,37.57,.707},
        SoundMount{"engine_right",{5.3,-1.5,1.2},SoundMountKind::Local,37.57,.707}};
    if(model=="skywalker-x8"||model=="small-fixed-wing-uav")return {SoundMount{"engine",{0.,0.,.6}}, {}};
    if(model=="mq9")return {SoundMount{"engine",{0.,.25,4.7},SoundMountKind::Local,11.}, {}};
    return {SoundMount{"engine",{}}, {}};
}

}
