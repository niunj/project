#pragma once
#include <QString>
enum class EarthDetailRoute {
    None, Ascent, CloudPass, CloudWind, CloudWindEdge, CloudRegion, CloudSeaFlight,
    ObjectCloudBenchmark, VoxelCloudBenchmark, ObjectCloudWindFlight, SkyDayCycle, SkyLightTimelapse, SkyDiscDetail,
    AtmosphereLight, Overcast
};
enum class CloudRegionWeather { White, Storm, Sunset, Many };
void runEarthDetailCapture(const QString& output,bool interactive,
                           EarthDetailRoute route=EarthDetailRoute::None,
                           CloudRegionWeather weather=CloudRegionWeather::White,
                           const QString& routeFile={});
