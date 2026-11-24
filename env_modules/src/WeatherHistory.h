#pragma once
#include <rocky_env/VoxelWeather.h>
#include <algorithm>
#include <cmath>
namespace rockyenv {
// History is a display reconstruction, never weather truth. Smooth changes
// retain a bounded weight; per-pixel HDR/opacity/depth rejection still applies.
inline float weatherHistoryWeight(const VoxelWeatherFrame& a,const VoxelWeatherFrame& b) {
    if(a.clouds!=b.clouds||a.cloudObjects!=b.cloudObjects||
       (a.fogVisibility>0)!=(b.fogVisibility>0)||(a.overcastAmount>=0)!=(b.overcastAmount>=0))return 0;
    if(a.overcastAmount<0&&a.overcast!=b.overcast)return 0;
    const auto relative=[](float x,float y,float minimum){return std::abs(x-y)/std::max(minimum,std::min(std::abs(x),std::abs(y)));};
    const float change=std::max({std::abs(a.cover-b.cover)/.025f,
        std::abs(a.cloudBase-b.cloudBase)/20.f,std::abs(a.cloudHeight-b.cloudHeight)/30.f,
        std::abs(a.opticalDepth-b.opticalDepth)/.75f,std::abs(a.asymmetry-b.asymmetry)/.025f,
        std::abs(a.turbidity-b.turbidity)/.08f,std::abs(a.overcastAmount-b.overcastAmount)/.04f,
        relative(a.fogVisibility,b.fogVisibility,50.f)/.08f,relative(a.fogHeight,b.fogHeight,1.f)/.08f,
        std::abs(a.windSpeed-b.windSpeed)/1.f,float(vsg::length(a.windAxis-b.windAxis))/.08f});
    return std::isfinite(change)&&change<1.f?.88f*(1.f-.65f*change):0.f;
}
}
