#pragma once
#include <vsg/core/Data.h>
#include <vsg/maths/vec3.h>
#include <algorithm>

namespace rockyenv {
// Standard clear-air transmission data used by the sky LUT and Earth shaders.
vsg::ref_ptr<vsg::Data> referenceAtmosphereTransmittance();

// A display radiance adjustment for the existing fog path. The authored
// visibility and spectral atmosphere remain the source of extinction.
inline vsg::vec3 precipitationFogLight(vsg::vec3 horizon,float rain,float snow,float overcast) {
    const float deck=std::clamp(overcast,0.f,1.f);
    const float storm=deck*std::clamp(rain,0.f,1.f);
    const float snowHaze=std::clamp(snow,0.f,1.f);
    const float luma=vsg::dot(horizon,vsg::vec3(.2126f,.7152f,.0722f));
    const auto neutral=vsg::vec3(luma*.94f,luma*.98f,luma*1.02f);
    const float blend=std::clamp(.23f*deck+.62f*storm+.34f*snowHaze,0.f,1.f);
    // The near-surface veil receives light filtered by the same deck as the
    // scene. Keep dense rain darker than snow under diffuse light.
    const float available=1.f-.18f*deck-.47f*storm-.16f*snowHaze;
    return (horizon*(1.f-blend)+neutral*blend)*available;
}
}
