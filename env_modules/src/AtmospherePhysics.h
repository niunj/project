#pragma once
/// @file AtmospherePhysics.h
/// @brief rocky_env 内部: 大气物理光照 —— 由天体状态计算每帧 AtmosphereState。
///
/// 物理链路 (平面/地球两后端共用, 保证光照与天空真值一致):
///   太阳高度角 → Kasten-Young 大气质量 → Rayleigh/Mie 光谱消光
///   → 直射辐照 RGB (暮光自动变红变暗); 天光梯度色 (天顶/地平线);
///   夜间月亮: 高度角 + 月相 → 月光平行光。
///
/// P1-1: 支持 EnvWeather 联动 (能见度/云量/风速 → 大气参数)
/// P1-2: 支持能见度/雾联动 (visibility → 消光系数)
#ifndef ROCKY_ENV_ATMOSPHERE_PHYSICS_H
#define ROCKY_ENV_ATMOSPHERE_PHYSICS_H

#include "SkyTypes.h"

namespace rockyenv {

/// 天气参数 (从 EnvFrame 提取, 用于大气计算)
struct WeatherParams {
    float visibility = 10000.0f;  ///< 能见度 (米)
    float cloud_cover = 0.5f;     ///< 云量 0..1
    float wind_speed = 6.0f;      ///< 风速 (m/s)
    float precipitation = 0.0f;   ///< 降水强度 0..1
};

/// 计算大气/光照状态。
/// @param b          本帧天体 (日月方向/高度/月相)。
/// @param turbidity  Mie 浊度倍数 (1=标准晴空, 越大越雾)。
/// @param exposure   穹顶曝光倍率 (回填到状态供查询)。
/// @param weather    天气参数 (能见度/云量/风速, 可选)。
AtmosphereState computeAtmosphere(const SkyBodies& b, float turbidity, float exposure,
                                  const WeatherParams& weather = {});

class AtmosphereLUT;
/// Shared band query: display and headless clients consume the same state.
bool sampleBandSky(const AtmosphereState& state, double lambdaMinM, double lambdaMaxM,
                   double viewElevationDeg, double& skyRadiance, double& sigmaPerKm,
                   double& sunIrradiance);
/// ECEF endpoints and ECEF sun direction. Heights use the WGS84 ellipsoid.
bool sampleAtmospherePath(const AtmosphereState& state, const vsg::dvec3& sunECEF,
                          const vsg::dvec3& fromECEF, const vsg::dvec3& toECEF,
                          const AtmosphereLUT* lut, vsg::vec3& transmittance,
                          vsg::vec3& pathRadiance);

}  // namespace rockyenv

#endif  // ROCKY_ENV_ATMOSPHERE_PHYSICS_H
