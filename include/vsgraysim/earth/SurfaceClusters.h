#pragma once

#include <memory>
#include <vector>
#include <vsg/app/Camera.h>
#include <vsg/nodes/Group.h>
#include <vsg/state/Buffer.h>

namespace vsgraysim::earth {
struct VoxelSceneDisplay;

/// Visual-only cache of the current, already published surface meshes.
class SurfaceClusterDisplay {
public:
    struct IndirectDraws {
        vsg::ref_ptr<vsg::Buffer> buffer;
        uint32_t count=0;
        uint64_t sourceTriangles=0, lowerLodTriangles=0;
    };
    static std::shared_ptr<SurfaceClusterDisplay> prepare(VoxelSceneDisplay&,const SurfaceClusterDisplay*,vsg::Device&,uint32_t viewId);
    ~SurfaceClusterDisplay();
    vsg::ref_ptr<vsg::Group> commands() const;
    bool update(const VoxelSceneDisplay&,const vsg::dmat4& chart,const vsg::Camera&,float errorPixels,uint64_t renderedFrames);
    std::vector<IndirectDraws> indirectDraws() const;

private:
    SurfaceClusterDisplay();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
