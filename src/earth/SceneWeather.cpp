#include <vsgraysim/earth/SceneWeather.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vsgraysim::earth {
using json=nlohmann::json;
SceneUtc sceneUtc(const json& origin,double seconds) {
    if(!std::isfinite(seconds))throw std::invalid_argument("non-finite UTC time");
    int y=origin.at("year"),m=origin.at("month"),d=origin.at("day");
    const double hours=origin.at("hours_utc").get<double>()+seconds*origin.value("time_scale",1.)/3600.;
    const auto shift=static_cast<long long>(std::floor(hours/24.));
    y-=m<=2;const int era=(y>=0?y:y-399)/400;const unsigned yoe=y-era*400;
    const unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    long long z=static_cast<long long>(era)*146097+yoe*365+yoe/4-yoe/100+doy+shift;
    const auto e=(z>=0?z:z-146096)/146097;const unsigned doe=z-e*146097;
    const unsigned yy=(doe-doe/1460+doe/36524-doe/146096)/365;
    const unsigned dayOfYear=doe-(365*yy+yy/4-yy/100),mp=(5*dayOfYear+2)/153;
    m=mp+(mp<10?3:-9);d=dayOfYear-(153*mp+2)/5+1;y=int(yy+e*400)+(m<=2);
    return {y,m,d,hours-shift*24.};
}
namespace {
struct Field {const char* name;double SceneWeatherState::*member;double lo,hi;};
constexpr Field fields[]={
    {"cloud_cover",&SceneWeatherState::cover,0,1},{"visibility_m",&SceneWeatherState::visibility,50,200000},
    {"wind_speed_mps",&SceneWeatherState::windSpeed,0,75},{"wind_azimuth_deg",&SceneWeatherState::windAzimuth,0,360},
    {"rain_mm_h",&SceneWeatherState::rain,0,200},{"snow_water_mm_h",&SceneWeatherState::snow,0,30},
    {"temperature_k",&SceneWeatherState::temperature,193.15,333.15},{"relative_humidity",&SceneWeatherState::humidity,0,1},
    {"cloud_base_m",&SceneWeatherState::cloudBase,50,18000},{"cloud_top_m",&SceneWeatherState::cloudTop,51,20000},
    {"optical_depth",&SceneWeatherState::opticalDepth,1,100},{"overcast",&SceneWeatherState::overcast,0,1},
    {"surface_wetness",&SceneWeatherState::wetness,0,1},{"snow_cover",&SceneWeatherState::snowCover,0,1},
    {"fog_height_m",&SceneWeatherState::fogHeight,10,5000}};
double number(const json& j,double lo,double hi,const char* name) {
    if(!j.is_number())throw std::invalid_argument(std::string("weather requires number: ")+name);
    const double x=j.get<double>();
    if(!std::isfinite(x)||x<lo||x>hi)throw std::invalid_argument(std::string("weather out of range: ")+name);
    return x;
}
SceneWeatherState state(const json& j) {
    if(!j.is_object())throw std::invalid_argument("weather state must be an object");
    auto s=SceneWeather::preset(j.value("preset",std::string("clear")));
    for(const auto& entry:j.items()) {
        if(entry.key()=="preset")continue;
        bool found=false;
        for(const auto& f:fields)if(entry.key()==f.name){s.*(f.member)=number(entry.value(),f.lo,f.hi,f.name);found=true;break;}
        if(!found)throw std::invalid_argument("unknown weather property: "+entry.key());
    }
    if(s.cloudTop<=s.cloudBase)throw std::invalid_argument("weather cloud top must exceed base");
    return s;
}
SceneWeatherState interpolate(const SceneWeatherState& a,const SceneWeatherState& b,double t) {
    t=std::clamp(t,0.,1.);t=t*t*(3-2*t);auto s=a;
    for(const auto& f:fields)s.*(f.member)=(1-t)*(a.*(f.member))+t*(b.*(f.member));
    // Optical extinction interpolates continuously, as does wind across north.
    s.visibility=1./((1-t)/a.visibility+t/b.visibility);
    s.windAzimuth=std::fmod(a.windAzimuth+t*std::remainder(b.windAzimuth-a.windAzimuth,360.)+360.,360.);
    return s;
}
}
SceneWeatherState SceneWeather::preset(const std::string& name) {
    SceneWeatherState s;
    if(name=="clear")return s;
    if(name=="fair_clouds"){
        s.cover=.42;s.cloudBase=1350;s.cloudTop=3600;s.opticalDepth=12;s.visibility=46000;return s;
    }
    if(name=="cloudy"){s.cover=.65;s.cloudTop=4200;s.visibility=30000;return s;}
    if(name=="twilight_clouds"){
        s.cover=.53;s.cloudBase=2200;s.cloudTop=4300;s.opticalDepth=9;s.overcast=.12;s.visibility=38000;return s;
    }
    s.cover=.98;s.overcast=1;s.cloudBase=800;s.cloudTop=2800;s.opticalDepth=24;s.humidity=.85;s.visibility=16000;
    if(name=="overcast")return s;
    if(name=="storm_clouds"){
        s.cover=.94;s.overcast=.94;s.cloudBase=680;s.cloudTop=5400;s.opticalDepth=40;s.windSpeed=8;s.visibility=13000;return s;
    }
    if(name=="light_rain"){
        s.cover=.86;s.overcast=.80;s.rain=1.4;s.wetness=.46;s.windSpeed=4.5;s.visibility=13000;s.cloudBase=900;s.opticalDepth=18;return s;
    }
    if(name=="rain"||name=="moderate_rain"){s.rain=6;s.wetness=.8;s.windSpeed=7;s.visibility=6500;s.cloudBase=650;return s;}
    if(name=="heavy_rain"){s.rain=35;s.wetness=1;s.windSpeed=13;s.visibility=1800;s.cloudBase=400;s.cloudTop=5400;s.opticalDepth=40;return s;}
    if(name=="light_snow"){
        s.cover=.87;s.overcast=.8;s.snow=.55;s.snowCover=.28;s.temperature=270.15;s.windSpeed=2.5;s.visibility=6500;s.cloudBase=850;s.opticalDepth=17;return s;
    }
    if(name=="snow"||name=="moderate_snow"){s.snow=2;s.snowCover=.6;s.temperature=269.15;s.windSpeed=4;s.visibility=2500;s.cloudBase=500;return s;}
    if(name=="heavy_snow"){
        s.snow=6.5;s.snowCover=.93;s.temperature=266.15;s.windSpeed=8.5;s.visibility=700;s.cloudBase=330;s.cloudTop=3200;s.opticalDepth=30;return s;
    }
    if(name=="fog"){s.cover=.4;s.overcast=.6;s.visibility=220;s.fogHeight=120;s.windSpeed=1;s.cloudBase=1000;s.humidity=.99;return s;}
    throw std::invalid_argument("unknown weather preset: "+name);
}

SeaWindPreset seaWindPreset(int force) {
    constexpr SeaWindPreset levels[]={{.1,0.,12.,0.,42.},{.9,.06,9.,.03,36.},
        {2.5,.23,12.,.08,48.},{4.5,.52,18.,.16,60.},{6.8,.95,28.,.29,83.},
        {9.4,1.6,40.,.5,102.},{12.3,2.5,53.,.74,123.},{15.5,3.65,68.,1.05,151.},
        {19.,5.0,86.,1.45,181.}};
    if(force<0||force>=int(std::size(levels)))throw std::invalid_argument("sea wind force must be 0..8");
    return levels[force];
}

void applySeaWindPreset(json& ocean,int force,double azimuth) {
    const auto preset=seaWindPreset(force);
    if(!ocean.is_object()||!std::isfinite(azimuth)||azimuth<0.||azimuth>360.)
        throw std::invalid_argument("invalid sea wind preset input");
    ocean.update({{"wind_speed_mps",preset.windSpeed},{"wind_azimuth_deg",azimuth},
        {"wave_height_m",preset.waveHeight},{"wave_length_m",preset.waveLength},
        {"swell_height_m",preset.swellHeight},{"swell_length_m",preset.swellLength}});
    ocean.erase("harmonics"); // Regenerate the shared physical wave spectrum using the existing ocean loader.
    ocean["provenance"]["sea_wind_preset"]={{"beaufort_force",force},{"values","authored wind, wave and swell example"}};
}
SceneWeather SceneWeather::load(const json& j) {
    SceneWeather w;if(j.is_null())return w;
    if(!j.is_object())throw std::invalid_argument("weather must be an object");
    for(const auto& e:j.items())if(e.key()!="enabled"&&e.key()!="initial"&&e.key()!="transitions")throw std::invalid_argument("unknown weather field: "+e.key());
    w.enabled=j.value("enabled",true);w.initial=state(j.value("initial",json::object()));
    const auto values=j.value("transitions",json::array());
    if(!values.is_array()||values.size()>256)throw std::invalid_argument("weather supports at most 256 transitions");
    double end=-1;
    for(const auto& k:values){
        if(!k.is_object())throw std::invalid_argument("weather transition must be an object");
        for(const auto& e:k.items())if(e.key()!="at_seconds"&&e.key()!="duration_seconds"&&e.key()!="state")throw std::invalid_argument("unknown weather transition field: "+e.key());
        Key key{number(k.at("at_seconds"),0,31536000,"at_seconds"),number(k.value("duration_seconds",60.),0,86400,"duration_seconds"),state(k.at("state"))};
        if(key.seconds<end||(!w.keys.empty()&&key.seconds==w.keys.back().seconds))throw std::invalid_argument("weather transitions must be ordered and non-overlapping");
        end=key.seconds+key.duration;w.keys.push_back(key);
    }
    return w;
}
SceneWeatherState SceneWeather::sample(double seconds) const {
    if(!std::isfinite(seconds))throw std::invalid_argument("non-finite weather time");
    auto s=initial;
    for(const auto& k:keys){if(seconds<k.seconds)break;if(k.duration>0&&seconds<k.seconds+k.duration)return interpolate(s,k.state,(seconds-k.seconds)/k.duration);s=k.state;}
    return s;
}
json SceneWeather::encodeState(const SceneWeatherState& s) {json j=json::object();for(const auto& f:fields)j[f.name]=s.*(f.member);return j;}
std::array<double,2> SceneWeather::windTravel(double seconds) const {
    if(!std::isfinite(seconds))throw std::invalid_argument("non-finite wind time");
    std::array<double,2> distance{};
    const auto add=[&](const SceneWeatherState& s,double dt){const double a=s.windAzimuth*3.141592653589793/180.;distance[0]+=std::sin(a)*s.windSpeed*dt;distance[1]+=std::cos(a)*s.windSpeed*dt;};
    if(seconds<=0){add(initial,seconds);return distance;}
    constexpr double nodes[]={.1834346424956498,.5255324099163290,.7966664774136267,.9602898564975363};
    constexpr double weights[]={.3626837833783620,.3137066458778873,.2223810344533745,.1012285362903763};
    double cursor=0;auto previous=initial;
    for(const auto& k:keys) {
        add(previous,std::max(0.,std::min(seconds,k.seconds)-cursor));
        if(seconds<=k.seconds)return distance;
        const double span=std::min(k.duration,seconds-k.seconds);
        if(k.duration>0)for(int i=0;i<4;++i)for(double sign:{-1.,1.})
            add(interpolate(previous,k.state,span*(1.+sign*nodes[i])/(2.*k.duration)),weights[i]*span*.5);
        if(seconds<k.seconds+k.duration)return distance;
        cursor=k.seconds+k.duration;previous=k.state;
    }
    add(previous,seconds-cursor);return distance;
}
json SceneWeather::encode() const {
    json j={{"enabled",enabled},{"initial",encodeState(initial)},{"transitions",json::array()}};
    for(const auto& k:keys)j["transitions"].push_back({{"at_seconds",k.seconds},{"duration_seconds",k.duration},{"state",encodeState(k.state)}});
    return j;
}
}
