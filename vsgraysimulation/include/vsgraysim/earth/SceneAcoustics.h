#pragma once

#include <vsgraysim/earth/VoxelScene.h>
#include <array>
#include <deque>
#include <map>

namespace vsgraysim::earth {

inline constexpr std::array<double,8> acousticBands{63,125,250,500,1000,2000,4000,8000};
using AcousticBands=std::array<double,acousticBands.size()>;

struct AcousticPath {
    std::string key, cue;
    vsg::dvec3 arrivalDirection, sourcePosition;
    AcousticBands bandEnergy{}; // (Pa / 20 µPa)²
    double emissionTime=0., delay=0., doppler=1., pitch=1., playbackOffset=0.;
    double highBandLoss=0.;
    double distance=0.;
    bool oneShot=false, reflected=false, screened=false, estimated=true, underwater=false;
};

struct AcousticReading {
    AcousticBands bandEnergy{};
    std::vector<AcousticPath> paths;
    bool underwater=false;
    double submergedMix=0.;
    double dbSpl() const;
    double dbA() const;
    double referencePressure() const {return underwater?1e-6:20e-6;}
    nlohmann::json encode() const;
};

class AcousticMeter {
public:
    void observe(double seconds,const AcousticReading&);
    nlohmann::json encode() const;
private:
    double last_=-1.,elapsed_=0.,exposure_=0.,previous_=0.,fast_=0.,maximum_=0.;
    bool underwater_=false;
};

// Clocked, read-only acoustics for a listener in the local Earth chart. Sound
// Air paths use the scene's temperature and humidity. Nearby ocean ambience
// uses a separate estimate for listeners below the surface.
class SceneAcoustics {
public:
    void ingest(const VoxelSceneSnapshot&, const nlohmann::json& presentation, std::string_view epoch={});
    AcousticReading listen(const VoxelSceneSnapshot&, const vsg::dvec3& receiver,
        const vsg::dvec3& receiverVelocity={}, bool reflections=true) const;
    static constexpr double maxPathSeconds=20.;
private:
    struct Emission {
        std::string cue;
        vsg::dvec3 position{}, velocity{}, forward{};
        AcousticBands bands{};
        double time=0., reference=1., minimumDistance=1., pitch=1., duration=0., directivity=0.;
        bool oneShot=false, enabled=true, estimated=true, underwater=false;
    };
    struct Track {std::deque<Emission> samples; bool seen=false;};
    std::map<std::string,Track> tracks_;
    std::string epoch_;
    double lastTime_=-1.;
};

// The topic holds authored static/attached sources and named receivers. JSON
// is also the compact presentation wire format used by existing display clients.
void validateAcousticTopic(const nlohmann::json&);
void appendAcousticSources(const nlohmann::json&,const VoxelSceneSnapshot&,nlohmann::json& presentation);

}
