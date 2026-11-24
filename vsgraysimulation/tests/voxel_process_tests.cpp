#include <vsgraysim/broker/VoxelEquipmentSource.h>
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/kernel/TimingBus.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>
#include <unistd.h>
using namespace vsgraysim;using json=nlohmann::json;namespace fs=std::filesystem;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Result {json checkpoint;VisFrame image;RadarFrame radar;};
int main()try{
    std::string root="/tmp/vsg_gpu_pool_test_XXXXXX";require(::mkdtemp(root.data()),"cannot create test directory");
    auto devices=json::parse(std::ifstream("config/equipment/taipei-multispectral.vequipment.json"));devices["equipment"]=json::array({devices["equipment"][0],devices["equipment"][3]});
    for(auto& device:devices["equipment"]){device["width"]=9;device["height"]=7;device["aperture"]={{"position_m",{0,8,0}},{"look_at_m",{0,0,0}},{"up",{0,0,-1}}};}
    devices["equipment"][1]["radar"]["range_min_m"]=0;devices["equipment"][1]["radar"]["range_max_m"]=100;devices["platforms"][0]["object"]="target";
    const auto source=json::parse(R"({"format":"voxel-scene-v1","fixed_step_seconds":0.02,"geo_anchor":{"lat":25.078,"lon":121.772,"alt":0},"objects":[{"id":"target","source":{"type":"box","dimensions":[4,1,4],"origin_m":[-2,0,-2],"cell_size_m":1},"physics":{"motion":"prescribed"},"material":{"temperature_k":310,"color_rgba":[1,1,1,1]}}]})");
    const auto sourceFile=fs::path(root)/"scene.json",topic=fs::path(root)/"test.vtopic.json";std::ofstream(sourceFile)<<source;
    auto authored=earth::VoxelScene::load(source);authored->schedule({{"kind","damage"},{"tick",6},{"object","target"},{"point_m",{0,.5,0}},{"radius_m",.8},{"energy_j",1e9}});
    std::ofstream(topic)<<json{{"format","earth-scene-topic-v1"},{"scene",sourceFile.string()},{"earth_map",fs::absolute("config/earth/default.map.json").string()},{"clouds",{{"cover",0}}},{"equipment",devices},{"checkpoint",authored->checkpoint()}};
    BrokerNet::Config transport;transport.enabled=false;BrokerNet::instance().startSimRole(transport);
    auto run=[&](bool process){if(process)::unsetenv("VSGRAYSIM_GPU_SERIAL_REFERENCE");else ::setenv("VSGRAYSIM_GPU_SERIAL_REFERENCE","1",1);auto src=std::make_shared<VoxelEquipmentSource>(VoxelSceneProject::open(topic));json options=json::object();if(process)options["measurement_worker"]=VSGRAYSIM_MEASUREMENT_WORKER_PATH;
        require(src->initialize(options),"source init failed");TimingBus bus;bus.configureFixedStep(.02);bus.registerSubsystem(src);require(bus.initialize(),"bus init failed");bus.start();
        if(process){src->idleMeasurements();require(src->diagnostics().at("gpu_pool").at("pid")==0,"GPU process started without a data lease");}
        require(SimDataBroker::instance().subscribe(7101)&&SimDataBroker::instance().subscribe(7104),"cannot enable test devices");
        for(int n=0;n<16;++n){require(bus.tick(),"fixed-step failure");if(!src->isHealthy())throw std::runtime_error(src->diagnostics().dump());}
        if(process){const auto gpu=src->diagnostics().at("gpu_pool");require(gpu.at("pid").get<int>()>0&&gpu.at("pid")!=::getpid(),"measurement did not execute in a separate process");}
        Result out;require(SimDataBroker::instance().latestVis(7101,&out.image)&&SimDataBroker::instance().latestRadar(7104,&out.radar),"missing actual GPU products");const auto checkpoint=fs::path(root)/(process?"remote.json":"reference.json");src->saveRecovery(checkpoint.string());std::ifstream(checkpoint)>>out.checkpoint;
        require(out.checkpoint.at("content_revision").get<uint64_t>()>1,"damage did not change topology");require(src->diagnostics().at("rejected_windows").get<uint64_t>()>0,"CPI crossing topology was not rejected");bus.stop();return out;};
    const auto reference=run(false),repeat=run(false),remote=run(true);require(reference.checkpoint==remote.checkpoint,"process boundary changed authoritative physics or pending events");
    require(reference.image.frame_id==remote.image.frame_id&&reference.image.surface_revision==remote.image.surface_revision&&reference.image.intensity==remote.image.intensity,"process boundary changed the VIS payload/version");
    require(reference.checkpoint==repeat.checkpoint,"reference physics is not deterministic");
    require(reference.radar.frame_id==remote.radar.frame_id&&reference.radar.surface_revision==remote.radar.surface_revision&&reference.radar.rd_map.size()==remote.radar.rd_map.size()&&reference.radar.sample_rate_hz==remote.radar.sample_rate_hz&&reference.radar.prf==remote.radar.prf,"process boundary changed RD identity, geometry or axes");
    double peak=0,error=0,self=0,norm=0,squared=0;for(size_t n=0;n<reference.radar.rd_map.size();++n){const auto a=std::complex<double>(reference.radar.rd_map[n]),b=std::complex<double>(remote.radar.rd_map[n]),c=std::complex<double>(repeat.radar.rd_map[n]);peak=std::max(peak,std::abs(a));error=std::max(error,std::abs(a-b));self=std::max(self,std::abs(a-c));norm+=std::norm(a);squared+=std::norm(a-b);}
    // Existing radar_csr/echo shaders reorder a bucket with atomics and sum in
    // float32. Independent GPU executions can differ in their final bits. This
    // bound covers the fixture's 63 scatterers and FFT reduction; physics and
    // the actual transported product remain exact (separate SHA tests).
    const double relativeLimit=128*std::numeric_limits<float>::epsilon();
    const double relativeL2=std::sqrt(squared/std::max(norm,1e-30));
    const json numeric={{"physics_checkpoint_exact",true},{"vis_payload_exact",true},{"radar_max_absolute_error",error},{"radar_reference_repeat_max_error",self},{"radar_peak",peak},{"radar_relative_l2_error",relativeL2},{"radar_relative_bound",relativeLimit}};
    std::cout<<numeric.dump()<<'\n';std::ofstream(fs::path(VSGRAYSIM_MEASUREMENT_WORKER_PATH).parent_path().parent_path()/"task-pool-gpu-equivalence.json")<<numeric.dump(2);
    require(error<=std::max(1e-12,relativeLimit*peak)&&relativeL2<=relativeLimit,"RD discrepancy exceeds existing float32 reduction precision");
    BrokerNet::instance().stop();fs::remove_all(root);std::cout<<"PASS: lazy GPU process, identical canonical damaged state, exact VIS payload, float32-bounded RD and topology/CPI rejection\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';BrokerNet::instance().stop();return 1;}
