#pragma once

#include <vsgraysim/earth/VoxelScene.h>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

namespace vsgraysim::service {

/// Produce a browser presentation from already committed terrain on a separate thread.
class BrowserEarthSurface {
public:
    BrowserEarthSurface(std::filesystem::path directory, std::string generation);
    ~BrowserEarthSurface();
    void submit(earth::VoxelSceneSnapshotPtr snapshot);
    static std::optional<vsg::dvec3> intersect(const std::filesystem::path&,const nlohmann::json&,
                                               const vsg::dvec3& origin,const vsg::dvec3& direction,double limit);

private:
    void run();
    std::filesystem::path directory_;
    std::string generation_;
    earth::EarthSurfaceSnapshotPtr latest_;
    earth::VoxelSceneSnapshotPtr pending_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    std::thread thread_;
};

}
