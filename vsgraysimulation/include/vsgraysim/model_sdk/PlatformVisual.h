#pragma once

#include <vsgraysim/model_sdk/ExhaustAppearance.h>
#include <vsgraysim/model_sdk/SceneModel.h>

#include <vector>

namespace vsgraysim::model_sdk {

struct AnimationInput {
    double time = 0.;
    double previousTime = 0.;
    std::optional<double> previewCue;
    double throttle = 0.;
    std::optional<std::array<double,2>> displayAngles;
};

struct JointPose {
    std::string_view joint;
    Vec3 offset{};
    Vec3 axis{0., 1., 0.};
    double angle = 0.;
    bool visible = true;
};

enum class EffectStyle { Jet, WarmExhaust, LaunchExhaust, Flash };

struct Effect {
    std::string_view id;
    std::string_view attachment;
    Vec3 offset{};
    Vec3 direction{0., 0., 1.};
    EffectStyle style;
    float strength;
    float radius;
    visual_effects::ExhaustAppearance exhaust{};
};

struct AnimationFrame {
    std::vector<JointPose> joints;
    std::vector<Effect> effects;
};

class IPlatformVisual {
public:
    virtual ~IPlatformVisual() = default;
    // Evaluated from the supplied clock and state. The caller owns the output buffers.
    virtual void sample(const AnimationInput&, AnimationFrame&) const = 0;
    virtual std::optional<Vec3> modelAttachment(std::string_view, double) const { return {}; }
};

} // namespace vsgraysim::model_sdk
