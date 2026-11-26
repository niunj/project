#pragma once

#include <vsg/maths/vec3.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace vsgraysim::viewport3d {

// Interpolates the visible path between two observations. Tangents are
// limited per axis so an intervening point stays inside the observed bounds.
class FlightTrailCurve {
public:
    FlightTrailCurve(const vsg::dvec3& from, const vsg::dvec3& to,
                     const std::optional<vsg::dvec3>& fromVelocity,
                     const std::optional<vsg::dvec3>& toVelocity, double duration)
        : from_(from), to_(to), chord_(to-from)
    {
        for (int axis=0; axis<3; ++axis) {
            const double span=chord_[axis];
            if (std::abs(span)<1e-8) continue;
            double a=fromVelocity ? (*fromVelocity)[axis]*duration/span : 1.;
            double b=toVelocity ? (*toVelocity)[axis]*duration/span : 1.;
            a=std::clamp(a,0.,3.);
            b=std::clamp(b,0.,3.);
            const double magnitude=std::hypot(a,b);
            const double scale=magnitude>3. ? 3./magnitude : 1.;
            first_[axis]=span*a*scale;
            last_[axis]=span*b*scale;
        }
    }

    vsg::dvec3 at(double fraction) const
    {
        const double u=std::clamp(fraction,0.,1.);
        if (u==0.) return from_;
        if (u==1.) return to_;
        const double u2=u*u;
        return from_+chord_*(u2*(3.-2.*u))+first_*(u*(1.-u)*(1.-u))+last_*(u2*(u-1.));
    }

    vsg::dvec3 tangent(double fraction) const
    {
        const double u=std::clamp(fraction,0.,1.);
        return chord_*(6.*u*(1.-u))+first_*((1.-u)*(1.-3.*u))+last_*(u*(3.*u-2.));
    }

private:
    vsg::dvec3 from_,to_,chord_,first_{},last_{};
};

} // namespace vsgraysim::viewport3d
