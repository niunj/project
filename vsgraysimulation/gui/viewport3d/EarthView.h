#pragma once
#include "IViewportFeature.h"
#include "GeoFrame.h"
#include <vsgraysim/earth/EarthSurface.h>

namespace vsgraysim::viewport3d {
// The persisted feature key predates backend selection. Existing viewport
// configurations and plugins continue to identify the same Earth service.
class EarthView : public IViewportFeature {
public:
    static constexpr const char* kId = "rocky_map";
    const char* id() const override { return kId; }
    virtual bool loadEarthScenario(const nlohmann::json& scene, const std::string& path,
                                   std::string& error) = 0;
    virtual earth::PrepareSurfaceView captureViewPreparation() = 0;
    virtual bool streamSurface(const std::string& key,std::function<earth::EarthSurfaceSnapshotPtr()> load) = 0;
    virtual void bindSurface(earth::EarthSurfaceSnapshotPtr surface, bool frame) = 0;
    virtual earth::EarthSurfaceSnapshotPtr surfaceSnapshot() const = 0;
    virtual nlohmann::json earthConfiguration() const = 0;
    virtual const GeoAnchor& anchor() const = 0;
    virtual void setGeoAnchor(const GeoAnchor&) = 0;
};
}
