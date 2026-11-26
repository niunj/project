#pragma once

#include <vsgraysim/model_sdk/SoundMount.h>

namespace vsgraysim::model_packages {

inline model_sdk::AirframeSoundMounts helicopterSoundMounts() {
    using model_sdk::SoundMount;
    using model_sdk::SoundMountKind;
    return {SoundMount{"main_rotor",{0.,2.4,0.},SoundMountKind::MainRotor,15.,1.,1.},
        SoundMount{"tail_rotor",{.35,1.25,6.},SoundMountKind::TailRotor,15.,.38,1.4}};
}

}
