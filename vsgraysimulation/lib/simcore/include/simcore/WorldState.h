#pragma once

#include <simcore/Time.h>
#include <simcore/GeoFrame.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/transform.h>
#include <deque>
#include <map>
#include <memory>
#include <string>

namespace simcore {

inline bool finite(const vsg::dvec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline vsg::dquat normalized(const vsg::dquat& q) {
    const double n = std::hypot(std::hypot(q.x, q.y), std::hypot(q.z, q.w));
    if (!std::isfinite(n) || n < 1e-12) throw std::invalid_argument("invalid body orientation");
    return {q.x/n, q.y/n, q.z/n, q.w/n};
}
inline vsg::dquat interpolateRotation(vsg::dquat a, vsg::dquat b, double u) {
    a = normalized(a); b = normalized(b);
    double d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    d = std::clamp(d, 0.0, 1.0);
    double wa = 1-u, wb = u;
    if (d < .9995) {
        const double angle = std::acos(d), s = std::sin(angle);
        wa = std::sin((1-u)*angle)/s; wb = std::sin(u*angle)/s;
    }
    return normalized({wa*a.x+wb*b.x, wa*a.y+wb*b.y, wa*a.z+wb*b.z, wa*a.w+wb*b.w});
}

/// All vectors in the snapshot's anchored, right-handed Y-up frame.
/// Position and linear velocity refer to the model origin, not the body COM.
struct RigidState {
    TimePoint time = 0;
    vsg::dvec3 position{0,0,0};
    vsg::dquat orientation{0,0,0,1};
    vsg::dvec3 velocity{0,0,0};
    vsg::dvec3 angular_velocity{0,0,0}; // world-frame rad/s

    vsg::dmat4 matrix() const { return vsg::translate(position) * vsg::rotate(orientation); }
    vsg::dvec3 point(const vsg::dvec3& local) const { return position + orientation * local; }
    vsg::dvec3 pointVelocity(const vsg::dvec3& local) const {
        return velocity + vsg::cross(angular_velocity, orientation * local);
    }
};

/// Bounded interpolation history. Never extrapolates measurement truth.
class RigidStateHistory {
public:
    explicit RigidStateHistory(TimePoint horizon = 10 * nanosecondsPerSecond) : horizon_(horizon) {
        if (horizon <= 0) throw std::invalid_argument("truth history horizon must be positive");
    }
    void clear() { states_.clear(); }
    bool push(RigidState state) {
        if (state.time < 0 || !finite(state.position) || !finite(state.velocity) ||
            !finite(state.angular_velocity) || (!states_.empty() && state.time < states_.back().time)) return false;
        try { state.orientation = normalized(state.orientation); } catch (const std::invalid_argument&) { return false; }
        if (!states_.empty() && state.time == states_.back().time) states_.back() = state;
        else states_.push_back(state);
        // Retain one bracketing sample before the horizon.
        while (states_.size() > 2 && states_[1].time < state.time - horizon_) states_.pop_front();
        return true;
    }
    bool sample(TimePoint time, RigidState& out) const {
        if (states_.empty() || time < states_.front().time || time > states_.back().time) return false;
        auto b = std::lower_bound(states_.begin(), states_.end(), time,
            [](const RigidState& s, TimePoint t) { return s.time < t; });
        if (b->time == time) { out = *b; return true; }
        const auto& a = *std::prev(b);
        const double h = toSeconds(b->time - a.time), u = double(time-a.time)/double(b->time-a.time);
        const double u2=u*u, u3=u2*u;
        RigidState result;
        result.time = time;
        result.position = a.position*(2*u3-3*u2+1) + a.velocity*(h*(u3-2*u2+u))
                        + b->position*(-2*u3+3*u2) + b->velocity*(h*(u3-u2));
        result.velocity = a.position*((6*u2-6*u)/h) + a.velocity*(3*u2-4*u+1)
                        + b->position*((-6*u2+6*u)/h) + b->velocity*(3*u2-2*u);
        result.orientation = interpolateRotation(a.orientation, b->orientation, u);
        result.angular_velocity = a.angular_velocity*(1-u) + b->angular_velocity*u;
        out = result;
        return true;
    }
    std::size_t size() const { return states_.size(); }
private:
    TimePoint horizon_;
    std::deque<RigidState> states_;
};

/// Published immutable value; readers never retain pointers into mutable physics.
struct WorldSnapshot {
    TimePoint time = 0;
    uint64_t revision = 0;
    uint64_t surface_revision=0;
    geo::GeoAnchor anchor;
    std::string epoch_utc="2026-09-10T04:00:00Z";
    std::map<std::string, RigidState> bodies;
};
using WorldSnapshotPtr = std::shared_ptr<const WorldSnapshot>;

} // namespace simcore
