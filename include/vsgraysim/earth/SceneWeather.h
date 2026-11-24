#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace vsgraysim::earth {
struct SceneUtc {int year,month,day;double hours;};
SceneUtc sceneUtc(const nlohmann::json& origin,double sceneSeconds);

struct SeaWindPreset {
    double windSpeed, waveHeight, waveLength, swellHeight, swellLength;
};
/// Authored wind and wave examples, indexed by Beaufort force 0–8.
SeaWindPreset seaWindPreset(int force);
void applySeaWindPreset(nlohmann::json& ocean,int force,double azimuth);
// Authored regional weather, sampled in authoritative scene seconds. No wall
// clock, particles or renderer state enter checkpoint/replay calculations.
struct SceneWeatherState {
    double cover=.25, visibility=40000, windSpeed=4, windAzimuth=70;
    double rain=0, snow=0, temperature=293.15, humidity=.55;
    double cloudBase=1400, cloudTop=3200, opticalDepth=10, overcast=0;
    double wetness=0, snowCover=0, fogHeight=450;
    double precipitation() const { return std::min(1.,rain/40.+snow/8.); }
};
class SceneWeather {
public:
    struct Key {double seconds=0,duration=60;SceneWeatherState state;};
    bool enabled=false;
    SceneWeatherState initial;
    std::vector<Key> keys;
    static SceneWeather load(const nlohmann::json&);
    static SceneWeatherState preset(const std::string&);
    static nlohmann::json encodeState(const SceneWeatherState&);
    nlohmann::json encode() const;
    SceneWeatherState sample(double sceneSeconds) const;
    std::array<double,2> windTravel(double sceneSeconds) const; // east/north metres
};
}
