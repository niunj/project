#pragma once
#include "IViewportFeature.h"

#include <vsg/maths/mat4.h>
#include <memory>
#include <optional>
#include <array>

namespace vsgraysim::viewport3d {

// A camera-facing animation rig driven by the render timeline and the host's
// observed travel. The demonstration cues are consumed only in this viewport.
class PlatformMotionFeature final : public IViewportFeature {
public:
    static constexpr const char* kId="platform_motion_preview";
    struct PoseState {
        double trackDistance=0.;
        double wheelAngle=0.;
        double antennaAngle=0.;
        double hatch=0.;
        double recoil=0.;
        double turretYaw=0.;
        double barrelElevation=0.;
        unsigned animatedWheels=0;
        bool propVisible=false;
        bool detached=false;
        vsg::dvec3 propPosition{};
    };
    PlatformMotionFeature();
    ~PlatformMotionFeature() override;
    const char* id() const override {return kId;}
    void configure(const nlohmann::json&) override;
    void onAttach(ViewportContext&) override;
    void onDetach(ViewportContext&) override;
    void onFrame(ViewportContext&,const FrameTick&) override;
    void setEnabled(bool on) override;
    void setMotion(const vsg::dmat4& pose,double travelledMetres,double throttle=0.);
    void setDisplayAngles(double yawRadians,double pitchRadians);
    const PoseState& state() const;
    vsg::dvec3 antennaCentre() const;
private:
    struct Rig;
    std::unique_ptr<Rig> rig_;
};

// Binds published animation metadata to the corresponding voxel snapshot in both hosts.
class ScenePlatformMotion final : public IViewportFeature {
public:
    static constexpr const char* kId="scene_platform_motion";
    ScenePlatformMotion();
    ~ScenePlatformMotion() override;
    const char* id() const override {return kId;}
    void onAttach(ViewportContext&) override {}
    void onFrame(ViewportContext&,const FrameTick&) override;
    void onDetach(ViewportContext&) override;
    const PlatformMotionFeature::PoseState* state(const std::string& object) const;
private:
    struct Instances;
    std::unique_ptr<Instances> instances_;
};

} // namespace vsgraysim::viewport3d
