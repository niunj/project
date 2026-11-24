#pragma once
#include <simcore/GeoFrame.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace simcore::geo {
struct CameraPose { vsg::dvec3 eye, center, up; };
inline CameraPose initialCamera(const GeoAnchor& anchor, double height) {
    if (!std::isfinite(height) || height <= 0) throw std::invalid_argument("invalid camera height");
    const auto center = geodeticToECEF(anchor.lat_deg, anchor.lon_deg, anchor.alt_m);
    const auto up = geoUp(anchor.lat_deg, anchor.lon_deg);
    const auto east = geoEast(anchor.lat_deg, anchor.lon_deg);
    // North is screen-up in the nadir view; the viewing direction is radial.
    return {center + up * height, center, -vsg::cross(east, up)};
}
inline CameraPose orbitCamera(const CameraPose& pose, double azimuth, double elevation) {
    const auto geo = ecefToGeodetic(pose.center.x, pose.center.y, pose.center.z);
    const auto up = geoUp(geo.x, geo.y), east = geoEast(geo.x, geo.y);
    const auto south = vsg::cross(east, up);
    const auto offset = pose.eye - pose.center;
    const double distance = vsg::length(offset);
    if (!(distance > 1e-6)) return pose;
    const double az = std::atan2(vsg::dot(offset,east),vsg::dot(offset,south)) + azimuth;
    const double el = std::clamp(std::asin(std::clamp(vsg::dot(offset,up)/distance,-1.,1.)) + elevation,-1.54,1.54);
    const auto direction = east*(std::cos(el)*std::sin(az)) + up*std::sin(el) + south*(std::cos(el)*std::cos(az));
    return {pose.center + direction*distance, pose.center, up};
}
inline CameraPose panCamera(const CameraPose& pose, double dx, double dy) {
    const auto direction = vsg::normalize(pose.center-pose.eye);
    auto right = vsg::cross(direction,pose.up);
    if (vsg::length(right)<1e-10) return pose;
    right = vsg::normalize(right);
    const auto screenUp = vsg::normalize(vsg::cross(right,direction));
    const auto oldGeo = ecefToGeodetic(pose.center.x,pose.center.y,pose.center.z);
    const auto candidate = pose.center + right*dx + screenUp*dy;
    const auto newGeo = ecefToGeodetic(candidate.x,candidate.y,candidate.z);
    const auto center = geodeticToECEF(newGeo.x,newGeo.y,oldGeo.z);
    const auto oldEast=geoEast(oldGeo.x,oldGeo.y),oldUp=geoUp(oldGeo.x,oldGeo.y),oldSouth=vsg::cross(oldEast,oldUp);
    const auto newEast=geoEast(newGeo.x,newGeo.y),newUp=geoUp(newGeo.x,newGeo.y),newSouth=vsg::cross(newEast,newUp);
    auto transport=[&](const vsg::dvec3& v) {
        return newEast*vsg::dot(v,oldEast)+newUp*vsg::dot(v,oldUp)+newSouth*vsg::dot(v,oldSouth);
    };
    return {center+transport(pose.eye-pose.center),center,transport(pose.up)};
}
inline std::pair<double,double> cameraClip(const CameraPose& pose) {
    const auto geo=ecefToGeodetic(pose.eye.x,pose.eye.y,pose.eye.z);
    const double height=std::max(0.,geo.z),distance=vsg::length(pose.eye-pose.center);
    const double horizon=std::sqrt(height*(2*GeoAnchor::kEarthA+height));
    const double near=std::clamp(std::max(height,distance)*1e-4,.1,10000.);
    return {near,std::clamp(std::max(300000.,distance+2*horizon+10000.),300000.,1e8)};
}
} // namespace simcore::geo
