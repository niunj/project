#pragma once

#include "ScrollableImageView.h"
#include <vsgraysim/broker/SensorFrames.h>
#include <vsgraysim/earth/EarthSurface.h>

namespace vsgraysim::gui::display {

class RadarTerrainMap : public ScrollableImageView {
public:
    explicit RadarTerrainMap(QWidget* parent);
    void setSource(const std::string& path);
    void clearStation();
    void updateStation(const RadarFrame& frame);

private:
    struct Load;
    std::string source_;
    std::shared_ptr<Load> inFlight_;
    earth::EarthSurfaceSnapshotPtr surface_;
    std::optional<simcore::geo::GeoAnchor> attempt_;
    vsg::dvec3 station_;
    double radius_=0;
    double bearing_=0;
};

} // namespace vsgraysim::gui::display
