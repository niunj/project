#pragma once
#include "IViewportFeature.h"
#include <memory>

namespace vsgraysim::viewport3d {

class SceneAudioFeature final : public IViewportFeature {
public:
    static constexpr const char* kId="scene_audio";
    SceneAudioFeature();
    ~SceneAudioFeature() override;
    const char* id() const override {return kId;}
    void configure(const nlohmann::json&) override;
    void onAttach(ViewportContext&) override {}
    void onDetach(ViewportContext&) override;
    void onFrame(ViewportContext&,const FrameTick&) override;
    void publishLocalBrokerState(nlohmann::json);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
