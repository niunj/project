#pragma once

#include <vsgraysim/model_sdk/SceneModel.h>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace vsgraysim::earth {struct AcousticReading;}

namespace vsgraysim::audio {

struct VoiceRequest {
    std::string key, cue;
    float left=0.f, right=0.f, pitch=1.f;
    bool oneShot=false;
    double startSeconds=0.;
    float filter=1.f;
};
struct MixFrame {
    std::string epoch;
    uint64_t tick=0;
    bool playing=false;
    std::vector<VoiceRequest> voices;
};

MixFrame spatialFrame(const nlohmann::json&, std::string epoch, bool playing,
    const model_sdk::Vec3& listener, const model_sdk::Vec3& right);
MixFrame acousticFrame(const earth::AcousticReading&,MixFrame base,const model_sdk::Vec3& right);

class SceneAudioMixer {
public:
    static constexpr int sampleRate=48000;
    SceneAudioMixer();
    void apply(const MixFrame&);
    void render(int16_t* interleaved,std::size_t sampleCount);
    void render(std::vector<int16_t>& output) {render(output.data(),output.size());}
private:
    struct Voice {
        const std::vector<float>* clip=nullptr;
        double cursor=0.;
        float left=0.f, right=0.f, pitch=1.f, fade=0.f;
        float targetLeft=0.f,targetRight=0.f,targetPitch=1.f,filter=1.f,targetFilter=1.f,filtered=0.f;
        bool oneShot=false, present=true;
    };
    std::map<std::string,std::vector<float>> sounds_;
    std::map<std::string,Voice> voices_;
    std::deque<std::string> heard_;
    std::string epoch_;
    uint64_t tick_=0;
    bool playing_=false;
};

}
