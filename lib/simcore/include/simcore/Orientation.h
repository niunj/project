#pragma once
#include <simcore/WorldState.h>

namespace simcore {
/// Hamilton product: rotate by b, then by a. VSG's quaternion operator*
/// uses the opposite composition order; keep that convention out of physics.
inline vsg::dquat composeRotations(const vsg::dquat& a,const vsg::dquat& b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
/// Local +X east, +Y up, -Z north. Heading increases clockwise from north;
/// pitch is nose-up and roll is right-wing-down, all in degrees.
inline vsg::dquat headingRotation(double heading, double pitch=0, double roll=0) {
    if (!std::isfinite(heading) || !std::isfinite(pitch) || !std::isfinite(roll))
        throw std::invalid_argument("nonfinite heading/pitch/roll");
    constexpr double r=3.14159265358979323846/180;
    return normalized(composeRotations(vsg::dquat(-heading*r,{0,1,0}),
        composeRotations(vsg::dquat(pitch*r,{1,0,0}),vsg::dquat(-roll*r,{0,0,1}))));
}
inline vsg::dvec3 headingAngles(const vsg::dquat& orientation) {
    const auto m=vsg::rotate(normalized(orientation));
    const double pitch=std::asin(std::clamp(-m[2][1],-1.0,1.0));
    const bool locked=std::abs(std::cos(pitch))<1e-9;
    const double yaw=locked ? -std::atan2(-m[0][2],m[0][0]) : -std::atan2(m[2][0],m[2][2]);
    const double roll=locked ? 0 : -std::atan2(m[0][1],m[1][1]);
    constexpr double d=180/3.14159265358979323846;
    return {std::fmod(yaw*d+360,360),pitch*d,roll*d};
}
inline vsg::dvec3 angularVelocityBetween(const vsg::dquat& a,const vsg::dquat& b,double dt) {
    if (!std::isfinite(dt) || dt<=0) throw std::invalid_argument("invalid rotation interval");
    const auto qa=normalized(a);
    auto delta=normalized(composeRotations(normalized(b),vsg::dquat(-qa.x,-qa.y,-qa.z,qa.w)));
    if (delta.w<0) delta={-delta.x,-delta.y,-delta.z,-delta.w};
    const double n=std::hypot(std::hypot(delta.x,delta.y),delta.z);
    if (n<1e-12) return {};
    return vsg::dvec3(delta.x,delta.y,delta.z)*(2*std::atan2(n,delta.w)/(n*dt));
}
} // namespace simcore
