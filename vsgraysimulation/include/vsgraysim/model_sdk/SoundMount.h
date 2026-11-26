#pragma once

#include <vsgraysim/model_sdk/SceneModel.h>

namespace vsgraysim::model_sdk {

enum class SoundMountKind { Local, MainRotor, TailRotor };

struct SoundMount {
    std::string_view name;
    Vec3 position{};
    SoundMountKind kind=SoundMountKind::Local;
    double referenceLength=0.; // Scale the default offset with the model's longitudinal bounds.
    double gain=1.;
    double pitch=1.;
};

using AirframeSoundMounts=std::array<SoundMount,2>;

}
