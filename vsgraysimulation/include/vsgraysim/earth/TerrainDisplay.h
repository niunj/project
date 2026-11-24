#pragma once
#include <vsg/nodes/StateGroup.h>
#include <vsg/maths/box.h>
#include <simcore/GeoFrame.h>
#include <vector>
#include <functional>

namespace vsgraysim::earth {
class EarthSurfaceSnapshot;
// Draw the pinned surface and its co-versioned imagery in its local tangent frame.
vsg::ref_ptr<vsg::StateGroup> makeTerrainDisplay(const EarthSurfaceSnapshot& surface);
/// Preview context only: reproject the pinned surrounding Earth mesh into the
/// physics scene chart and cut out the complete authoritative working windows.
/// No undeformed preview surface may remain beneath an excavated window.
vsg::ref_ptr<vsg::StateGroup> makeTerrainContextDisplay(const EarthSurfaceSnapshot&,
    const simcore::geo::GeoAnchor&,const std::vector<vsg::dbox>& windows,std::function<double(double,double)> seaLevel={});
}
