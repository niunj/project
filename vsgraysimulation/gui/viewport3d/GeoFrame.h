#pragma once
// Compatibility facade: earth-frame mathematics belongs to the shared kernel.
#include <simcore/GeoFrame.h>
namespace vsgraysim::viewport3d {
using simcore::geo::GeoAnchor;
using simcore::geo::deg2rad;
using simcore::geo::geodeticToECEF;
using simcore::geo::geoEast;
using simcore::geo::geoUp;
using simcore::geo::makeEnuToWorldMatrix;
using simcore::geo::ecefToGeodetic;
using simcore::geo::rayIntersectEllipsoid;
using simcore::geo::localToECEF;
using simcore::geo::ecefToLocal;
}
