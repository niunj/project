#pragma once

#include <vsgraysim/model_sdk/PlatformVisual.h>

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace vsgraysim::platform_animation {

using model_sdk::Vec3;
using model_sdk::AnimationInput;
using model_sdk::JointPose;
using model_sdk::EffectStyle;
using model_sdk::Effect;
using model_sdk::AnimationFrame;
using PlatformAnimation = model_sdk::IPlatformVisual;

std::unique_ptr<PlatformAnimation> makePlatformAnimation(std::string_view modelId);

class PreviewCue {
public:
    explicit PreviewCue(const AnimationInput& in) :
        age(in.time-in.previewCue.value_or(in.time)),
        crossed(in.previewCue && in.previousTime>=0. && in.previousTime<*in.previewCue && in.time>=*in.previewCue) {}
    double age;
    bool crossed;
};

double smoothStep(double fraction);

} // namespace vsgraysim::platform_animation
