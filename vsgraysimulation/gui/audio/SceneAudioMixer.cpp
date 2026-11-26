#include "SceneAudioMixer.h"
#include <vsgraysim/earth/SceneAcoustics.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vsgraysim::audio {
namespace {
using Json=nlohmann::json;
constexpr float pi=3.14159265358979323846f;

struct Sound {
    const char* id;
    float length, fundamental, rumble, hiss, pulse, gain;
    bool transient=false;
};

constexpr Sound bank[] = {
    {"jet",2,52,.52f,.46f,2,.68f},
    {"propeller",2,94,.39f,.31f,8,.68f},
    {"helicopter",2,43,.58f,.36f,14,.69f},
    {"helicopter_tail",2,83,.37f,.27f,21,.65f},
    {"small_rotor",2,192,.18f,.28f,26,.64f},
    {"land_engine",2,37,.65f,.19f,9,.67f},
    {"ship_engine",2,31,.73f,.13f,3,.62f},
    {"sea_ship_water",2,28,.86f,.12f,4,.67f},
    {"submersible",2,82,.31f,.14f,7,.53f},
    {"rocket",2,25,.96f,.59f,4,.90f},
    {"equipment_fan",2,113,.29f,.18f,5,.40f},
    {"optical",2,189,.17f,.11f,2,.33f},
    {"navigation",2,335,.06f,.038f,1,.24f},
    {"fracture",1.2f,48,.96f,.8f,3,.9f,true},
    {"impact",.55f,49,.93f,.36f,3,.78f,true},
    {"touchdown",.40f,68,.81f,.34f,2,.7f,true},
    {"separation",.30f,128,.68f,.45f,3,.62f,true},
    {"mechanical",.50f,153,.29f,.30f,6,.53f,true},
    {"depth_ping",.20f,580,.02f,.03f,1,.65f,true}
};

std::vector<float> synthesize(const Sound& s) {
    std::vector<float> clip(size_t(s.length*SceneAudioMixer::sampleRate));
    uint32_t rand=0xadb98345u;
    float fast=0.f,slow=0.f,low=0.f;
    for(size_t i=0;i<clip.size();++i){
        const float t=float(i)/SceneAudioMixer::sampleRate;
        rand^=rand<<13;rand^=rand>>17;rand^=rand<<5;
        const float white=float(rand>>8)*(2.f/16777215.f)-1.f;
        fast+=.15f*(white-fast);
        slow+=.027f*(fast-slow);
        low+=.007f*(slow-low);
        const float phase=2.f*pi*s.fundamental*t;
        const float tone=.19f*std::sin(phase)+.094f*std::sin(phase*2.01f)+.043f*std::sin(phase*3.02f);
        const float beat=.76f+.24f*std::sin(2.f*pi*s.pulse*t);
        const float texture=beat*(s.rumble*(1.6f*slow+3.f*low)+s.hiss*(fast-.6f*slow)+tone);
        const float envelope=s.transient?std::min(1.f,t*300.f)*std::exp(-5.2f*t/s.length):1.f;
        clip[i]=std::tanh(texture*s.gain*envelope);
    }
    if(!s.transient){
        constexpr size_t blend=400;
        // Roll the loop's head into its tail to avoid boundary clicks.
        for(size_t i=0;i<blend;++i){
            const float u=float(i+1)/blend;
            clip[clip.size()-blend+i]=clip[clip.size()-blend+i]*(1.f-u)+clip[i]*u;
        }
        clip.erase(clip.begin(),clip.begin()+blend);
    }
    return clip;
}

std::vector<float> synthesizeWater(bool submerged,bool transient) {
    const size_t count=transient?size_t(.44*SceneAudioMixer::sampleRate):11*SceneAudioMixer::sampleRate;
    std::vector<float> clip(count);
    uint32_t rng=0x9f31aab1u+(submerged?191u:0u)+(transient?57u:0u);
    float fine=0.f, wash=0.f, surge=0.f, movement=0.f;
    for(size_t i=0;i<count;++i){
        const float time=float(i)/SceneAudioMixer::sampleRate;
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        const float white=float(rng>>8)*(2.f/16777215.f)-1.f;
        fine+=.53f*(white-fine);
        wash+=.094f*(fine-wash);
        surge+=.012f*(wash-surge);
        movement+=.00038f*(std::abs(surge)-movement);
        if(transient){
            const float attack=1.f-std::exp(-time*180.f);
            const float decay=.75f*std::exp(-time*14.f)+.25f*std::exp(-time*5.f);
            clip[i]=std::tanh((1.2f*fine+.6f*wash+2.f*surge)*attack*decay);
        }else{
            const float swell=.68f+.17f*std::sin(time*1.68f)+.10f*std::sin(time*2.43f+1.4f)
                +.05f*std::sin(time*4.62f+.7f);
            const float fizz=std::clamp((movement-.024f)*16.f,.06f,.65f);
            const float water=submerged?(wash*.29f+surge*3.2f):(fine*fizz+wash*.63f+surge*1.65f);
            clip[i]=std::tanh(water*swell*1.8f);
        }
    }
    if(!transient){
        constexpr size_t blend=2400;
        for(size_t i=0;i<blend;++i){
            const float weight=.5f-.5f*std::cos(pi*float(i+1)/blend);
            clip[count-blend+i]=clip[count-blend+i]*(1.f-weight)+clip[i]*weight;
        }
        clip.erase(clip.begin(),clip.begin()+blend);
    }
    return clip;
}

std::vector<float> synthesizeShaft() {
    // Playback at unity represents the demonstration's 90 RPM shaft.
    std::vector<float> clip(4*SceneAudioMixer::sampleRate);
    uint32_t rng=0x8da61a31u;
    float wash=0.f,rolling=0.f;
    for(size_t i=0;i<clip.size();++i){
        const float t=float(i)/SceneAudioMixer::sampleRate;
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        const float white=float(rng>>8)*(2.f/16777215.f)-1.f;
        wash+=.055f*(white-wash);
        rolling+=.006f*(wash-rolling);
        const float rotation=2.f*pi*1.5f*t;
        const float passage=.50f+.50f*std::cos(7.f*rotation+.22f*std::sin(rotation));
        const float hum=.085f*std::sin(14.f*rotation)+.045f*std::sin(21.f*rotation);
        const float wake=.42f+.58f*passage*passage;
        const float seam=std::min(1.f,std::min(float(i),float(clip.size()-1-i))/105.f);
        clip[i]=std::tanh(seam*(wake*(wash*1.35f+rolling*3.0f)+hum));
    }
    return clip;
}
}

MixFrame spatialFrame(const nlohmann::json& data, std::string epoch,bool playing,
    const model_sdk::Vec3& listener,const model_sdk::Vec3& right) {
    MixFrame frame{std::move(epoch),0,playing};
    if(!data.is_object()||!data.contains("tick")||!data.at("tick").is_number_unsigned())return frame;
    frame.tick=data.at("tick");
    if(!playing)return frame;
    for(const auto* field:{"events","loops"}){
        const auto list=data.find(field);
        if(list==data.end()||!list->is_array())continue;
        const bool oneShot=std::string_view(field)=="events";
        for(const auto& row:*list)try{
            const auto point=row.at("position_m").get<model_sdk::Vec3>();
            const double dx=point[0]-listener[0],dy=point[1]-listener[1],dz=point[2]-listener[2];
            const double dist=std::hypot(dx,dy,dz),ref=row.at("reference_m").get<double>();
            if(!std::isfinite(dist)||!std::isfinite(ref)||ref<=0.)continue;
            const double pan=dist>.1?std::clamp((dx*right[0]+dy*right[1]+dz*right[2])/dist,-1.,1.):0.;
            const double ratio=dist/ref;
            const auto gain=row.at("gain").get<double>();
            const auto pitch=row.at("pitch").get<float>();
            if(!std::isfinite(gain)||!std::isfinite(pitch)||!std::isfinite(pan))continue;
            const auto level=std::clamp(gain,0.,1.)/(1.+ratio*ratio);
            if(level<.002)continue;
            const auto key=row.at("key").get<std::string>(), cue=row.at("cue").get<std::string>();
            if(key.size()>192||cue.size()>48)continue;
            frame.voices.push_back({key,cue,float(level*std::sqrt(.5*(1.-pan))),
                float(level*std::sqrt(.5*(1.+pan))),std::clamp(pitch,.5f,2.f),oneShot});
            if(frame.voices.size()>=144)break;
        }catch(const Json::exception&){continue;}
        if(frame.voices.size()>=144)break;
    }
    std::stable_sort(frame.voices.begin(),frame.voices.end(),[](const auto& a,const auto& b){
        if(a.oneShot!=b.oneShot)return a.oneShot;
        return a.left+a.right>b.left+b.right;
    });
    if(frame.voices.size()>24)frame.voices.resize(24);
    return frame;
}

SceneAudioMixer::SceneAudioMixer() {
    for(const auto& sound:bank)sounds_.emplace(sound.id,synthesize(sound));
    sounds_.emplace("sea_surface",synthesizeWater(false,false));
    sounds_.emplace("sea_underwater",synthesizeWater(true,false));
    sounds_.emplace("sea_slap",synthesizeWater(false,true));
    sounds_.emplace("sea_propeller",synthesizeShaft());
}
MixFrame acousticFrame(const earth::AcousticReading& reading,MixFrame frame,const model_sdk::Vec3& right) {
    if(!frame.playing)return frame;
    for(const auto& path:reading.paths){
        const double energy=std::accumulate(path.bandEnergy.begin(),path.bandEnergy.end(),0.);
        const double level=.44*std::sqrt(energy/1e9); // 90 dB SPL -> nominal digital gain .44
        if(level<.0001)continue;
        const auto& d=path.arrivalDirection;
        const double pan=std::clamp(d.x*right[0]+d.y*right[1]+d.z*right[2],-1.,1.);
        const double hf=std::pow(10.,-std::max(0.,path.highBandLoss)/20.);
        const double minimumPitch=path.cue=="sea_propeller"?.01:.25;
        frame.voices.push_back({path.key,path.cue,float(level*std::sqrt(.5*(1.-pan))),
            float(level*std::sqrt(.5*(1.+pan))),float(std::clamp(path.pitch*path.doppler,minimumPitch,4.)),
            path.oneShot,path.playbackOffset,float(std::clamp(hf,.025,1.))});
    }
    std::stable_sort(frame.voices.begin(),frame.voices.end(),[](const auto& a,const auto& b){return a.left+a.right>b.left+b.right;});
    if(frame.voices.size()>24)frame.voices.resize(24);
    return frame;
}
void SceneAudioMixer::apply(const MixFrame& frame) {
    if(frame.epoch!=epoch_||frame.tick<tick_){voices_.clear();heard_.clear();}
    tick_=frame.tick;epoch_=frame.epoch;playing_=frame.playing;
    if(!playing_)return;
    for(auto& [key,voice]:voices_)if(!voice.oneShot)voice.present=false;
    for(const auto& request:frame.voices){
        const auto source=sounds_.find(request.cue);
        if(source==sounds_.end())continue;
        auto it=voices_.find(request.key);
        if(request.oneShot&&it==voices_.end()){
            if(std::find(heard_.begin(),heard_.end(),request.key)!=heard_.end())continue;
            heard_.push_back(request.key);
            if(heard_.size()>256)heard_.pop_front();
        }
        if(it==voices_.end()){
            if(voices_.size()>=48)continue;
            it=voices_.try_emplace(request.key).first;
        }
        auto& voice=it->second;
        if(voice.clip!=&source->second){
            voice={};
            voice.clip=&source->second;
            voice.oneShot=request.oneShot;
            const double offset=request.startSeconds*sampleRate*request.pitch;
            voice.cursor=request.oneShot?offset:std::fmod(offset,voice.clip->size());
            voice.left=request.left;voice.right=request.right;voice.pitch=request.pitch;
            voice.filter=request.filter;
        }
        voice.targetLeft=request.left;voice.targetRight=request.right;voice.targetPitch=request.pitch;
        voice.targetFilter=request.filter;voice.present=true;
    }
}
void SceneAudioMixer::render(int16_t* interleaved,std::size_t sampleCount) {
    if(!sampleCount)return;
    std::fill_n(interleaved,sampleCount,0);
    if(!playing_)return;
    for(size_t i=0;i+1<sampleCount;i+=2){
        float left=0.f,right=0.f;
        for(auto& [key,v]:voices_){
            if(!v.clip||v.cursor>=v.clip->size())continue;
            v.fade+=((v.present?1.f:0.f)-v.fade)*.003f;
            const auto index=size_t(v.cursor);
            const auto next=std::min(index+1,v.clip->size()-1);
            const float a=(*v.clip)[index],b=(*v.clip)[next];
            const auto sample=a+(b-a)*float(v.cursor-index);
            v.filter+=(v.targetFilter-v.filter)*.001f;
            v.filtered+=(sample-v.filtered)*v.filter;
            v.left+=(v.targetLeft-v.left)*.002f;v.right+=(v.targetRight-v.right)*.002f;
            v.pitch+=(v.targetPitch-v.pitch)*.002f;
            left+=v.filtered*v.fade*v.left;
            right+=v.filtered*v.fade*v.right;
            v.cursor+=v.pitch;
            if(!v.oneShot&&v.cursor>=v.clip->size())v.cursor-=v.clip->size();
        }
        interleaved[i]=int16_t(std::tanh(left)*31000.f);
        interleaved[i+1]=int16_t(std::tanh(right)*31000.f);
    }
    for(auto it=voices_.begin();it!=voices_.end();){
        const auto& v=it->second;
        if((v.oneShot&&v.cursor>=v.clip->size())||(!v.present&&v.fade<.001f))it=voices_.erase(it);
        else ++it;
    }
}
}
