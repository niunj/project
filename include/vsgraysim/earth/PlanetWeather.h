#pragma once
// Earth callers keep source compatibility; rocky_env owns the renderer and
// its weather field. The native product uses this one implementation.
#include <rocky_env/VoxelWeather.h>
namespace vsgraysim::earth {
using PlanetWeatherFrame=rockyenv::VoxelWeatherFrame;
using PlanetWeather=rockyenv::VoxelWeather;
using rockyenv::planetCloudTexture;
using rockyenv::planetCloudDetailTexture;
using rockyenv::planetCloudVoxelLayout;
using rockyenv::planetSparseCloudPageTable;
using rockyenv::planetSparseCloudAtlas;
using rockyenv::planetSparseCloudParameters;
using rockyenv::planetAdvection;
using rockyenv::localWeatherDetail;
using rockyenv::cloudFraction;
using rockyenv::cloudDensity;
using rockyenv::cloudTransmittance;
}
