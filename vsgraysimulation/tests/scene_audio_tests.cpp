#include "audio/SceneAudioMixer.h"
#include "audio/SceneAudioOutput.h"
#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/SceneAcoustics.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace vsgraysim;
using json=nlohmann::json;
using audio::SceneAudioMixer;

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
double channelEnergy(const std::vector<int16_t>& data,size_t side) {
    double value=0.;
    for(size_t i=side;i<data.size();i+=2)value+=double(data[i])*data[i];
    return value;
}
void wave(const std::filesystem::path& path,const std::vector<int16_t>& pcm){
    std::ofstream out(path,std::ios::binary);
    const auto word=[&](uint32_t n,int width){
        for(int i=0;i<width;++i)out.put(char((n>>(8*i))&255u));
    };
    const auto bytes=uint32_t(pcm.size()*sizeof(int16_t));
    out.write("RIFF",4);word(36+bytes,4);out.write("WAVEfmt ",8);word(16,4);
    word(1,2);word(2,2);word(SceneAudioMixer::sampleRate,4);word(SceneAudioMixer::sampleRate*4,4);
    word(4,2);word(16,2);out.write("data",4);word(bytes,4);
    for(auto s:pcm)word(uint16_t(s),2);
    check(bool(out),"could not write stereo WAV");
}
model_sdk::Vec3 localPosition(const earth::VoxelSceneSnapshot& state,const char* id){
    const auto p=state.objects.at(id).pose(state.tick,state.fixedStep)*vsg::dvec3{};
    return {p.x,p.y,p.z};
}
json build(const earth::VoxelEquipmentConfig& config,const earth::VoxelSceneSnapshot& snapshot,
    const earth::VoxelSceneSnapshot* prior=nullptr){
    return earth::scenePresentation(config,snapshot,earth::voxelEquipmentStateTelemetry(config,snapshot),prior);
}

void verifyAcoustics(){
    auto world=earth::VoxelScene::load({{"format","voxel-scene-v1"},{"geo_anchor",{{"lat",25.063},{"lon",121.56}}},
        {"objects",json::array({
            {{"id","ground"},{"role","terrain"},{"source",{{"type","box"},{"origin_m",{-100,-1,-100}},
                {"cell_size_m",1},{"dimensions",{200,1,200}}}}},
            {{"id","screen"},{"role","structure"},{"source",{{"type","box"},{"origin_m",{15,0,-4}},
                {"cell_size_m",1},{"dimensions",{1,8,8}}}}}
        })}});
    auto state=*world->snapshot();
    auto makeSource=[](const char* id,vsg::dvec3 p){return json{{"key",id},{"cue","propeller"},
        {"acoustic_reference_m",10.},{"position_m",{p.x,p.y,p.z}},
        {"spl_bands_db",{90.,-30.,-30.,-30.,-30.,-30.,-30.,-30.}}};};
    auto snapshot=[&](double time){state.tick=uint64_t(std::llround(time/state.fixedStep));};
    auto input=json{{"loops",json::array({makeSource("reference",{0,2,0})})}};
    earth::SceneAcoustics sounds;
    sounds.ingest(state,input);
    check(sounds.listen(state,{-10,2,0},{},false).paths.empty(),"sound arrived before its emission time");
    snapshot(1.);sounds.ingest(state,input);
    const auto at10=sounds.listen(state,{-10,2,0},{},false),at20=sounds.listen(state,{-20,2,0},{},false);
    check(at10.paths.size()==1&&std::abs(at10.dbSpl()-90.)<.01&&std::abs(at10.dbSpl()-at20.dbSpl()-6.0206)<.03,
        "the acoustic pressure does not follow inverse-distance spreading");
    check(std::abs(at20.paths[0].emissionTime+at20.paths[0].delay-state.timeSeconds())<.0001,
        "receiving and emitting clocks differ from path delay");
    const auto screened=sounds.listen(state,{20,2,0},{},false);
    check(screened.paths.size()==1&&screened.paths[0].screened&&screened.dbSpl()<at20.dbSpl()-2.9,
        "voxel screen did not attenuate the received level");
    const auto reflected=sounds.listen(state,{-20,2,0});
    check(reflected.paths.size()==2&&reflected.paths[1].reflected&&reflected.paths[1].delay>at20.paths[0].delay,
        "ground reflection lost its separate arrival time");
    input["loops"].push_back(makeSource("second",{0,2,0}));
    snapshot(2.);sounds.ingest(state,input);
    snapshot(2.2);sounds.ingest(state,input);
    const auto pair=sounds.listen(state,{-20,2,0},{},false);
    check(std::abs(pair.dbSpl()-at20.dbSpl()-3.0103)<.01,"sound levels were not summed in the energy domain");
    earth::AcousticMeter meter;meter.observe(0.,pair);meter.observe(2.,pair);
    check(std::abs(meter.encode().at("laeq_db").get<double>()-pair.dbA())<.001&&
        std::abs(meter.encode().at("sel_db").get<double>()-pair.dbA()-3.0103)<.01,"exposure and LAeq do not integrate energy");

    earth::SceneAcoustics flyover;
    for(int i=0;i<=5;++i){
        auto source=makeSource("plane",{-100.+40.*i,20,0});source["velocity_mps"]={40,0,0};
        snapshot(i);flyover.ingest(state,{{"loops",json::array({source})}},"flyover");
        if(i==1||i==5){
            const auto heard=flyover.listen(state,{0,2,0},{},false);
            check(heard.paths.size()==1&&(i==1?heard.paths[0].doppler>1.08:heard.paths[0].doppler<.93),
                "subsonic approach and departure have the wrong Doppler direction");
            const auto rendered=audio::acousticFrame(heard,{"flyover",state.tick,true,{}},{1,0,0});
            check(rendered.voices.size()==1&&std::abs(rendered.voices[0].pitch-heard.paths[0].doppler)<1e-6,
                "PCM voice does not use the same propagation result");
        }
    }
    snapshot(0.);flyover.ingest(state,{{"loops",json::array()}},"restarted");
    check(flyover.listen(state,{0,2,0}).paths.empty(),"previous scene acoustic history leaked across replay");
}

void verifyOceanSound() {
    earth::VoxelSceneSnapshot state;
    state.tick=200;
    const auto open=earth::VoxelOcean::load({{"wave_height_m",.1},{"swell_height_m",0.},{"wind_speed_mps",1.}});
    state.ocean=open;
    earth::SceneAcoustics solver;
    const auto calm=solver.listen(state,{0,2,0});
    state.ocean=earth::VoxelOcean::load({{"wave_height_m",1.8},{"swell_height_m",.4},{"wind_speed_mps",17.}});
    const auto rough=solver.listen(state,{0,2,0});
    const auto deep=solver.listen(state,{0,-4,0});
    const auto high=solver.listen(state,{0,150,0});
    check(calm.paths.size()==4&&rough.paths.size()==4&&rough.dbSpl()>calm.dbSpl()+9.&&high.dbSpl()<rough.dbSpl()-14.,
        "sea noise did not respond to wind, waves and distance");
    const auto pressure=deep.encode().at("pressure_rms_pa").get<double>();
    check(deep.underwater&&deep.paths.size()==4&&deep.encode().at("dba").is_null()&&
        std::abs(deep.referencePressure()*std::pow(10.,deep.dbSpl()/20.)-pressure)<1e-10,
        "water pressure was not reported with the underwater reference");
    auto water=std::make_shared<earth::VoxelOcean>(*open);
    state.ocean=water;
    earth::OceanSplash splash;
    splash.x=10.;splash.birth=state.timeSeconds();splash.speed=2.5;splash.seed=111;
    water->splashes.push_back(splash);
    const auto slap=[](const earth::AcousticPath& path){return path.cue=="sea_slap";};
    const auto early=solver.listen(state,{0,2,0});
    check(std::none_of(early.paths.begin(),early.paths.end(),slap),"water impact sounded before the wave reached the listener");
    state.tick+=3;
    const auto arrived=solver.listen(state,{0,2,0});
    const auto pulse=std::find_if(arrived.paths.begin(),arrived.paths.end(),slap);
    check(pulse!=arrived.paths.end()&&pulse->emissionTime==splash.birth&&std::abs(pulse->sourcePosition.x-10.)<1e-9,
        "wave impact audio did not use the simulated splash position and clock");
    SceneAudioMixer mixer;
    mixer.apply(audio::acousticFrame(arrived,{"ocean",state.tick,true,{}},{1,0,0}));
    std::vector<int16_t> pcm(9600);
    mixer.render(pcm);
    check(channelEnergy(pcm,0)>0.&&channelEnergy(pcm,1)>0.,"ocean ambience generated silent PCM");

    state.ocean=earth::VoxelOcean::load({{"wave_height_m",0.},{"swell_height_m",0.}});
    const json machinery={{"loops",json::array({
        {{"key","engine"},{"cue","ship_engine"},{"position_m",{0.,2.,0.}},
            {"spl_bands_db",{74.,77.,70.,66.,60.,57.,50.,42.}}},
        {{"key","shaft"},{"cue","sea_propeller"},{"medium","water"},{"pitch",1.2},
            {"position_m",{0.,-2.,0.}},{"spl_bands_db",{67.,73.,70.,65.,61.,54.,47.,40.}}}
    })}};
    solver.ingest(state,machinery,"machinery");
    state.tick+=25;
    solver.ingest(state,machinery,"machinery");
    const auto at10=solver.listen(state,{10.,-2.,0.});
    const auto at20=solver.listen(state,{20.,-2.,0.});
    const auto onSurface=solver.listen(state,{10.,0.,0.});
    const auto atKey=[](const earth::AcousticReading& reading,const char* key){
        return std::find_if(reading.paths.begin(),reading.paths.end(),[&](const auto& p){return p.key==key;});
    };
    const auto a=atKey(at10,"shaft"),b=atKey(at20,"shaft");
    check(a!=at10.paths.end()&&b!=at20.paths.end()&&atKey(at10,"engine")==at10.paths.end()&&
        std::abs(a->delay-10./1480.)<1e-7&&a->pitch==1.2,"water path did not preserve pitch and travel time");
    check(a->bandEnergy[1]/b->bandEnergy[1]>3.99&&a->bandEnergy[1]/b->bandEnergy[1]<4.03,
        "underwater shaft did not attenuate over distance");
    check(onSurface.submergedMix>.45&&onSurface.submergedMix<.55&&
        atKey(onSurface,"engine")!=onSurface.paths.end()&&atKey(onSurface,"shaft")!=onSurface.paths.end(),
        "waterline did not blend the two sound fields");
}

void recordOceanSound(const char* topic,const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    auto scene=VoxelSceneProject::open(topic).createScene();
    scene->setPaused(false);
    earth::SceneAcoustics solver;
    const auto calmWater=earth::VoxelOcean::load({{"wave_height_m",.1},{"swell_height_m",0.},{"wind_speed_mps",1.}});
    SceneAudioMixer calm,air,submerged;
    std::array<std::vector<int16_t>,3> clips;
    json report=json::array();
    unsigned audibleSplashes=0;
    for(int i=0;i<150;++i){
        const double seconds=i*.04;
        scene->advanceTo(seconds,4);
        const auto state=scene->snapshot();
        auto quiet=*state;quiet.ocean=calmWater;quiet.weather.enabled=false;
        const double z=2.-3.4*seconds-8.;
        const vsg::dvec3 airEar{9.,1.5,z};
        const auto sea=state->ocean->surface(airEar.x,airEar.z,seconds).height;
        std::array readings{solver.listen(quiet,airEar),solver.listen(*state,airEar),solver.listen(*state,{9.,sea-4.5,z})};
        if(std::any_of(readings[1].paths.begin(),readings[1].paths.end(),[](const auto& p){return p.cue=="sea_slap";}))++audibleSplashes;
        SceneAudioMixer* mixers[]{&calm,&air,&submerged};
        std::vector<int16_t> block(3840);
        for(size_t k=0;k<readings.size();++k){
            mixers[k]->apply(audio::acousticFrame(readings[k],{"ocean-record",state->tick,true,{}},{1,0,0}));
            mixers[k]->render(block);
            clips[k].insert(clips[k].end(),block.begin(),block.end());
        }
        if(i%25==15)report.push_back({{"seconds",seconds},{"calm",readings[0].encode()},
            {"sailing",readings[1].encode()},{"submerged",readings[2].encode()}});
    }
    check(audibleSplashes>0,"Taipei playback did not include the ship's splash sound");
    const char* names[]{"ocean-calm.wav","ocean-sailing.wav","ocean-underwater.wav"};
    for(size_t k=0;k<clips.size();++k){
        check(channelEnergy(clips[k],0)>0.,"empty ocean preview wave");
        wave(output/names[k],clips[k]);
    }
    std::ofstream(output/"ocean-audio.json")<<json{{"splash_audible_samples",audibleSplashes},{"samples",report}}.dump(2)<<'\n';
}
}

int main(int argc,char** argv)try {
    QCoreApplication app(argc,argv);
    verifyAcoustics();
    verifyOceanSound();
    if(argc==4&&std::string_view(argv[1])=="--ocean-audio"){
        recordOceanSound(argv[2],argv[3]);
        std::cout<<"PASS: sea state, water pressure datum, splash positions and stereo ocean WAV\n";
        return 0;
    }
    const auto drone=VoxelSceneProject::open("config/topics/taipei-civil-aviation.vtopic.json");
    const auto config=drone.equipmentConfig();
    const auto scene=drone.createScene();
    const auto initial=scene->snapshot();
    const auto drones=build(config,*initial);
    check(drones.at("platforms").at(0).at("state")=="flying"&&
        drones.at("platforms").at(1).at("state")=="hovering","fixed wing and rotorcraft model states differ");
    check(drones.at("equipment").at(0).at("state")=="working","navigation device is not marked working");
    const auto& engine=drones.at("loops").at(0);
    const auto expectedEngine=initial->objects.at("survey-wing").pose(initial->tick,initial->fixedStep)*vsg::dvec3{0.,0.,.6};
    check(engine.at("key")=="platform:801/engine"&&engine.at("mount_name")=="engine"&&
        engine.at("position_m")==json::array({expectedEngine.x,expectedEngine.y,expectedEngine.z}),
        "fixed-wing emitter was not attached to its model mount");
    auto interrupted=*initial;
    interrupted.tick=1;interrupted.revision++;
    interrupted.contacts.push_back({"survey-wing","__earth_surface",{4.,.03,8.},{4.,0.,8.},{0,1,0},1.,0.});
    const auto touchdown=build(config,interrupted,initial.get());
    check(touchdown.at("platforms").at(0).at("state")=="taxiing"&&
        touchdown.at("events").at(0).at("cue")=="touchdown"&&
        touchdown.at("events").at(0).at("position_m")==json::array({4.,0.,8.}),"touchdown audio did not use the actual contact point");
    auto sustained=interrupted;++sustained.tick;
    check(build(config,sustained,&interrupted).at("events").empty(),"persistent contact repeated the impact cue");

    const auto rotorBox=[](const char* id){return json{{"id",id},{"source",{{"type","box"},{"dimensions",{2,2,2}},
        {"cell_size_m",.25}}},{"physics",{{"motion","prescribed"}}}};};
    auto carrier=rotorBox("helicopter");carrier["position_m"]={5.,10.,7.};carrier["rotation_deg"]={0,43,0};
    auto main=rotorBox("main-rotor");main["rotor_drive"]={{"carrier","helicopter"},{"pivot_m",{0,2,0}},{"axis",{0,1,0}},{"rpm",300}};
    auto tail=rotorBox("tail-rotor");tail["rotor_drive"]={{"carrier","helicopter"},{"pivot_m",{.5,1,4}},{"axis",{1,0,0}},{"rpm",1200}};
    auto heli=earth::VoxelScene::load({{"format","voxel-scene-v1"},{"objects",json::array({carrier,main,tail})}});
    earth::VoxelEquipmentConfig heliConfig;
    heliConfig.platforms.push_back({880,"helicopter","Heli","helicopter",0.,{},"ah64d",{}});
    const auto attached=build(heliConfig,*heli->snapshot()).at("loops");
    check(attached.size()==2,"main and tail rotors were not emitted independently");
    for(const auto& unit:attached){
        const auto key=unit.at("mount_object").get<std::string>();
        const auto p=localPosition(*heli->snapshot(),key.c_str());
        check(unit.at("position_m")==p,"rotor acoustic origin differs from rendered hub");
    }
    auto stopped=*heli->snapshot();stopped.objects.at("tail-rotor").rotor->rpm=0.;
    const auto live=build(heliConfig,stopped).at("loops");
    check(live.size()==1&&live[0].at("mount_name")=="main_rotor","stopping tail rotor should leave only main-rotor sound");
    interrupted.journal.push_back({{"tick",1},{"sequence",4},{"fragments",json::array({"fragment"})},
        {"event",{{"object","survey-wing"},{"kind","fracture"}}}});
    const auto fracture=build(config,interrupted);
    check(fracture.at("platforms").at(0).at("state")=="breaking_up"&&
        fracture.at("events").at(0).at("cue")=="fracture","fracture journal state was lost");

    const auto rocket=VoxelSceneProject::open("config/topics/taipei-falcon9-swot.vtopic.json");
    const auto mission=rocket.createScene();
    const auto missionConfig=rocket.equipmentConfig();
    mission->setPaused(false);
    check(!mission->advanceTo(3.25,100).pendingSteps,"could not advance launch fixture");
    const auto launch=mission->snapshot();
    const auto launchFrame=build(missionConfig,*launch);
    check(launchFrame.at("platforms").at(0).at("state")=="launching"&&
        launchFrame.at("loops").at(0).at("cue")=="rocket","launch phases and audio are not synchronized");

    const model_sdk::Vec3 right={1,0,0};
    const auto listener=localPosition(*launch,"falcon-booster");
    const auto near=audio::spatialFrame(launchFrame,"launch",true,listener,right);
    SceneAudioMixer mixer;
    mixer.apply(near);
    std::vector<int16_t> pcm(size_t(SceneAudioMixer::sampleRate)*2);
    mixer.render(pcm);
    const double nearEnergy=channelEnergy(pcm,0);
    check(nearEnergy>10000.,"launch has silent PCM");
    auto farListener=listener;
    farListener[0]-=220.;
    auto far=audio::spatialFrame(launchFrame,"far",true,farListener,right);
    mixer.apply(far);mixer.render(pcm);
    check(channelEnergy(pcm,1)<.25*nearEnergy&&channelEnergy(pcm,1)>20.*channelEnergy(pcm,0),
        "stereo panning or distance attenuation failed");
    far.playing=false;mixer.apply(far);mixer.render(pcm);
    check(channelEnergy(pcm,0)==0.&&channelEnergy(pcm,1)==0.,"paused scene produced PCM");
    const auto shot=audio::spatialFrame(fracture,"fracture",true,localPosition(*initial,"survey-wing"),right);
    auto onlyShot=shot;
    onlyShot.voices.erase(std::remove_if(onlyShot.voices.begin(),onlyShot.voices.end(),
        [](const auto& v){return !v.oneShot;}),onlyShot.voices.end());
    mixer.apply(onlyShot);mixer.render(pcm);
    check(channelEnergy(pcm,0)>0.,"fracture was silent");
    mixer.render(pcm);
    mixer.apply(onlyShot);mixer.render(pcm);
    check(channelEnergy(pcm,0)==0.,"same fracture played twice");

    if(argc>1&&std::string_view(argv[1])=="--device"){
        audio::SceneAudioOutput output;
        QElapsedTimer time;
        time.start();
        while(time.elapsed()<3500&&!output.active()){output.submit(near);QThread::msleep(20);}
        check(output.active(),"Qt audio sink never became active");
        for(int i=0;i<30;++i){output.submit(near);QThread::msleep(20);}
        auto pause=near;pause.playing=false;output.submit(std::move(pause));
        time.restart();
        while(time.elapsed()<1500&&output.active())QThread::msleep(10);
        check(!output.active(),"Qt sink did not stop on scene pause");
    }else if(argc>1){
        std::vector<int16_t> demo;
        const auto record=[&](const audio::MixFrame& frame,int seconds){
            mixer.apply(frame);
            for(int i=0;i<seconds;++i){mixer.render(pcm);demo.insert(demo.end(),pcm.begin(),pcm.end());}
        };
        record(audio::spatialFrame(drones,"drones",true,localPosition(*initial,"survey-wing"),right),2);
        record(audio::spatialFrame(drones,"iris",true,localPosition(*initial,"inspection-quad"),right),2);
        record(audio::spatialFrame(fracture,"impact",true,localPosition(*initial,"survey-wing"),right),2);
        record(near,3);
        wave(argv[1],demo);
    }
    std::cout<<"PASS: model states, launch clock, stereo spatial audio, no duplicate event, silence on pause\n";
    return 0;
} catch(const std::exception& e){
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
