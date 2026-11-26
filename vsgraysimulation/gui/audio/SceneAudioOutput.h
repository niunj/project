#pragma once
#include "SceneAudioMixer.h"
#include <memory>

namespace vsgraysim::audio {

class SceneAudioOutput {
public:
    SceneAudioOutput();
    ~SceneAudioOutput();
    void submit(MixFrame);
    bool active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
