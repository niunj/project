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

struct EarthNavigation {
    static constexpr double orbitDegreesPerPixel = .008 * 180. / vsg::PI;
    static constexpr double panPerPixel = .0016;
    static constexpr double zoomPerNotch = .82;
    static constexpr double dollyPerPixel = .0035;
    double yaw = 0., pitch = 0., zoom = 1., panX = 0., panY = 0.;

    void orbit(double dx, double dy) {
        yaw = std::remainder(yaw + dx * orbitDegreesPerPixel, 360.);
        pitch = std::clamp(pitch + dy * orbitDegreesPerPixel, -89., 89.);
    }
    void pan(double dx, double dy) {
        panX = std::clamp(panX - dx * panPerPixel, -4., 4.);
        panY = std::clamp(panY + dy * panPerPixel, -4., 4.);
    }
    void dolly(double scale) { zoom = std::clamp(zoom * scale, .25, 4.); }
};

inline CameraPose workspaceEarthCamera(GeoAnchor anchor, bool globe, const EarthNavigation& input,
                                       vsg::dvec3 aim = {0.,500.,0.}, vsg::dvec3 offset = {8400.,7900.,-10300.}) {
    if (globe) {
        anchor.lat_deg = std::clamp(anchor.lat_deg + input.pitch + input.panY * 55., -86., 86.);
        anchor.lon_deg += input.yaw + input.panX * 55.;
        const auto up = geoUp(anchor.lat_deg, anchor.lon_deg);
        const auto east = geoEast(anchor.lat_deg, anchor.lon_deg);
        return {up * std::clamp(17.8e6 * input.zoom, 8.1e6, 80e6), {0.,0.,0.}, -vsg::cross(east,up)};
    }
    const double radius = vsg::length(offset) * input.zoom;
    const double azimuth = std::atan2(offset.x, offset.z) + vsg::radians(input.yaw);
    const double altitude = std::clamp(std::atan2(offset.y, std::hypot(offset.x, offset.z)) + vsg::radians(input.pitch),
                                       vsg::radians(3.), vsg::radians(86.));
    const auto model = makeEnuToWorldMatrix(anchor);
    offset = {radius * std::cos(altitude) * std::sin(azimuth), radius * std::sin(altitude),
              radius * std::cos(altitude) * std::cos(azimuth)};
    const auto up = model * vsg::dvec4(0.,1.,0.,0.);
    CameraPose pose{model * (aim + offset), model * aim, {up.x,up.y,up.z}};
    return input.panX == 0. && input.panY == 0. ? pose : panCamera(pose, input.panX * radius, input.panY * radius);
}
} // namespace simcore::geo
