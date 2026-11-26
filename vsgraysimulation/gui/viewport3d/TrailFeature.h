#pragma once

#include "IViewportFeature.h"

#include <vsg/commands/Draw.h>
#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <deque>
#include <map>
#include <optional>
#include <string>

namespace vsgraysim::viewport3d {

// Displays recent position observations in the viewport's content chart.
class TrailFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "trail";
    const char* id() const override { return kId; }
    void configure(const nlohmann::json&) override;
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    void setEnabled(bool on) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

    void addPoint(const std::string& id, const vsg::dvec3& pos, int faction,
                  double time, std::optional<vsg::dvec3> velocity = {});
    void removeTrack(const std::string& id);
    void clear();
    unsigned segmentCount() const;

private:
    struct Sample {
        vsg::dvec3 position;
        double time;
        std::optional<vsg::dvec3> velocity;
        bool startsRun;
    };
    struct Track {
        int faction = 0;
        std::deque<Sample> samples;
    };
    void sampleVoxelTracks(ViewportContext& ctx);
    void refreshSegments();
    void mount(bool visible);

    ViewportContext* ctx_ = nullptr;
    vsg::ref_ptr<vsg::Group> root_, attachedRoot_;
    vsg::ref_ptr<vsg::Draw> draw_;
    vsg::ref_ptr<vsg::vec4Array> segments_, parameters_;
    std::map<std::string, Track> tracks_;
    bool dirty_ = true, smooth_ = true;
    double clockOrigin_ = 0., lastSampleTime_ = -1., visibleTime_ = 0.;
    double lastVoxelTime_ = -1.;
};

} // namespace vsgraysim::viewport3d
