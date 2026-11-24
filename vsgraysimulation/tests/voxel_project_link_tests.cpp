#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/kernel/TimingBus.h>
#include <vsgraysim/workspace/Workspace.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <thread>
using namespace vsgraysim;
using json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Directory {
    std::string path="/tmp/vsg_voxel_project_XXXXXX";
    Directory(){require(::mkdtemp(path.data()),"cannot create test directory");}
    ~Directory(){std::error_code error;fs::remove_all(path,error);}
};
void write(const fs::path& p,const json& j){std::ofstream(p)<<j.dump();}
int main()try{
    Directory directory;auto config=json::parse(std::ifstream("config/equipment/taipei-multispectral.vequipment.json"));
    config["equipment"]=json::array({config["equipment"][0]});auto& sensor=config["equipment"][0];sensor["width"]=9;sensor["height"]=7;
    sensor["aperture"]={{"position_m",{0,8,0}},{"look_at_m",{0,0,0}},{"up",{0,0,-1}}};
    config["platforms"][0]["object"]="target";
    const json source=json::parse(R"({
        "format":"voxel-scene-v1", "fixed_step_seconds":0.02,
        "geo_anchor":{"lat":25.078,"lon":121.772,"alt":0},
        "objects":[{
            "id":"target",
            "source":{"type":"box","dimensions":[4,1,4],"origin_m":[-2,0,-2],"cell_size_m":1},
            "physics":{"motion":"prescribed"},
            "material":{"temperature_k":310,"color_rgba":[1,1,1,1]}
        }]
    })");
    const auto original=fs::path(directory.path)/"source.json";write(original,source);
    const auto topic=fs::path(directory.path)/"topic.json";
    write(topic,{{"format","earth-scene-topic-v1"},{"scene","source.json"},{"earth_map",fs::absolute("config/earth/default.map.json").string()},{"clouds",{{"cover",0}}},{"equipment",config}});
    auto project=VoxelSceneProject::open(topic);auto authored=project.createScene();authored->setPaused(false);authored->advanceTo(.12,8);authored->setPaused(true);
    const auto expected=authored->checkpoint();fs::create_directory(fs::path(directory.path)/"moved");const auto moved=fs::path(directory.path)/"moved"/"saved.json";write(moved,project.saved(expected));
    auto reopened=VoxelSceneProject::open(moved);require(reopened.createScene()->checkpoint()==expected,"Save As lost canonical voxel/checkpoint state");
    Workspace workspace(directory.path);workspace.ensureDirs();write(fs::path(workspace.sceneDir())/"saved.vtopic.json",reopened.document);
    const auto listed=workspace.listScenes();require(listed.size()==1&&listed[0].meta["objects"].size()==1&&listed[0].meta["sensors"]["sensor_list"].size()==1&&!listed[0].meta.contains("checkpoint"),"startup browser lost the saved topic or retained its heavy checkpoint");
    require(reopened.scenePath==original&&reopened.equipmentConfig().topicPath==moved,"Save As did not resolve relative resources and override the old equipment topic");
    auto invalid=reopened.document;invalid["checkpoint"]["format"]="voxel-scene-v1";write(fs::path(directory.path)/"invalid.json",invalid);bool rejected=false;
    try{VoxelSceneProject::open((fs::path(directory.path)/"invalid.json").string());}catch(...){rejected=true;}require(rejected,"source geometry was silently accepted as a checkpoint");
    BrokerNet::Config brokerConfig;brokerConfig.enabled=false;BrokerNet::instance().startSimRole(brokerConfig);
    {
        auto source=std::make_shared<VoxelEquipmentSource>(reopened);TimingBus bus;bus.configureFixedStep(.02);bus.registerSubsystem(source);require(bus.initialize(),"official voxel source initialization failed");bus.start();bus.setPaused(false);
        for(int i=0;i<5;++i)require(bus.tick(),"unlinked simulation tick failed");
        require(source->isHealthy(),"voxel owner-thread invariant was violated");
        require(SimDataBroker::instance().channelStats(7101).published_frames==0,"source generated measurements without a subscriber");
        require(SimDataBroker::instance().subscribe(7101),"cannot subscribe official source");
        for(int i=0;i<10;++i)require(bus.tick(),"linked simulation tick failed");
        require(source->isHealthy(),"official source tick failed");require(std::abs(source->snapshot()->timeSeconds()-.42)<1e-9,"saved scene time was reset or advanced twice");
        VisFrame frame;require(SimDataBroker::instance().latestVis(7101,&frame)&&frame.intensity.size()==63,"actual GPU product did not reach the existing typed broker");
        const auto context=json::parse(BrokerNet::instance().lastSimMeta().earth_json);VoxelSceneReadView view;
        const auto snapshot=view.update(context.at("voxel_scene"));require(snapshot->tick==source->snapshot()->tick&&snapshot->contentRevision==frame.surface_revision,"read-only scene and sensor geometry version differ");
        const auto actual=snapshot->objects.at("target").pose(snapshot->tick,snapshot->fixedStep);const auto wanted=source->snapshot()->objects.at("target").pose(snapshot->tick,snapshot->fixedStep);
        require(actual==wanted,"published source transform changed in the read-only view");
        const auto recovery=fs::path(directory.path)/"recovery.json";source->saveRecovery(recovery.string());
        const auto durable=json::parse(std::ifstream(recovery));auto restored=earth::VoxelScene::load(durable,recovery.string());
        require(restored->checkpoint()==durable&&restored->snapshot()->tick==source->snapshot()->tick,"worker recovery changed the complete canonical checkpoint");
        require(source->snapshot()->tick==snapshot->tick,"checkpoint advanced the live world");
        {
            AsyncVoxelSceneReadView async;async.submit(context.at("voxel_scene"));earth::VoxelSceneSnapshotPtr decoded;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(!decoded&&std::chrono::steady_clock::now()<deadline){decoded=async.take();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            require(decoded&&decoded->tick==snapshot->tick&&async.error().empty(),"asynchronous immutable view did not decode the source state");
            require(decoded->objects.at("target").pose(decoded->tick,decoded->fixedStep)==actual,"async decode changed the canonical transform");
        }
        auto bad=context.at("voxel_scene");bad["content_revision"]=999;rejected=false;try{view.update(bad);}catch(...){rejected=true;}require(rejected,"read-only view accepted the wrong geometry version");
        bus.stop();require(!SimDataBroker::instance().isServerOnline(),"stopping official source left data service online");
    }
    BrokerNet::instance().stop();std::cout<<"PASS: project Save As/reopen, nonzero checkpoint clock, owner-thread source, subscriber gate, actual GPU typed product and read-only versioned geometry\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';BrokerNet::instance().stop();return 1;}
