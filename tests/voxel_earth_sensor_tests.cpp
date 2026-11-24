#include "voxel_earth_fixture.h"
#include <vsgraysim/earth/VoxelSensorSession.h>
#include <rocky_env/GpuContext.h>
#include <iostream>
#include <map>
#include <cmath>
#include <unistd.h>
namespace {
using namespace vsgraysim::earth;using namespace rockyenv::sensor;
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
}
int main()try {
    std::string pattern="/tmp/vsgraysim_earth_measurement_XXXXXX";auto* made=mkdtemp(pattern.data());require(made,"Earth GPU scratch");
    const std::filesystem::path root=made;
    try {
        auto document=earth_fixture::create(root);document["events"]={
            {{"kind","terrain_mode"},{"object","earth"},{"tick",1},{"mode","voxel"}},
            {{"kind","damage"},{"object","earth"},{"tick",2},{"space","object"},{"point_m",{0,1,0}},{"radius_m",1.6}},
            {{"kind","terrain_mode"},{"object","earth"},{"tick",3},{"mode","hd"}},
            {{"kind","damage"},{"object","earth"},{"tick",5},{"space","object"},{"point_m",{0,0,0}},{"radius_m",100}}};
        auto scene=VoxelScene::load(document,(root/"earth.vscene.json").string());
        GpuContext owner;owner.initialize();GpuSensorSpec ir;ir.id=81;ir.period_seconds=.02;ir.output=ProductStage::Scene;
        ir.profile.width=9;ir.profile.height=7;ir.profile.fov_deg=8;ir.profile.detector.integration_ms=5;ir.profile.band=BandSpec::make(BandType::MWIR);
        auto north=ir;north.id=82;north.profile.band=BandSpec::make(BandType::TV);auto south=north;south.id=83;
        auto crossing=ir;crossing.id=85;crossing.period_seconds=.03;crossing.profile.detector.integration_ms=20;
        auto radar=ir;radar.id=84;radar.echo_only=true;RadarChainParams parameters;
        parameters.width=9;parameters.height=7;parameters.n_fast=128;parameters.cpi_pulses=2;parameters.prf=80;parameters.range_max_m=30;parameters.beam_width_rad=.3;parameters.terrain_footprint=true;radar.radar=parameters;
        VoxelSensorSession measurements(*scene,{ir,north,south,radar,crossing},owner.device());
        std::map<uint32_t,std::vector<GpuMeasurementProduct>> products;
        auto aperture=[](uint32_t id,double){GpuAperture a;a.position={.25,8,id==82?-2.2:(id==83?2.2:.25)};a.forward={0,-1,0};a.up={0,0,-1};return a;};
        auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_el_deg=90;e.sun_az_deg=0;return e;};
        auto sink=[&](GpuMeasurementProduct&& p){products[p.sensor_id].push_back(std::move(p));};
        scene->setPaused(false);const auto before=scene->checkpoint();const auto history=scene->history();
        scene->addPublicationParticipant("zz-failure",[](auto){throw std::runtime_error("injected terrain texture consumer failure");});
        bool rejected=false;try{scene->advanceTo(.02);}catch(const std::exception&){rejected=true;}
        require(rejected&&scene->checkpoint()==before&&scene->history()==history&&measurements.contentRevision()==1&&products.empty(),"failed Earth mode/texture preparation consumed truth or sensor time");
        scene->removePublicationParticipant("zz-failure");
        for(int tick=1;tick<=7;++tick){scene->advanceTo(tick*.02,1);measurements.advanceTo(tick*.02,aperture,environment,sink);}
        const size_t center=(3*9+4)*4;require(products[81].size()==7,"terrain geometry switches changed IR cadence");
        for(const auto& image:products[81]) {
            const double time=.5*(image.start_time+image.end_time);const auto state=scene->sample(time);const auto a=aperture(81,time);
            const auto hit=raycastVoxelScene(*state,a.position,a.forward,30);
            require(bool(hit)==(image.rgba[center+1]>1),"Earth GPU thermal visibility disagrees with historical physical geometry");
            if(hit) {
                require(std::abs(image.rgba[center+1]-314)<.1,"Earth material temperature changed across terrain geometry replacement");
                require(std::abs(image.rgba[center+2]-.83)<.001,"Earth emissivity lost");
                require(std::abs(image.rgba[center+3]*1000-hit->distance)<.003,"Earth GPU/Bullet canonical geometry distance mismatch");
            }
        }
        require(products[81][1].rgba[center+3]<products[81][2].rgba[center+3]-.001,"excavation did not change actual measured range");
        auto linear=[](double n){n/=255;return n<=.04045?n/12.92:std::pow((n+.055)/1.055,2.4);};
        const auto& r=products[82][0].rgba;const auto& b=products[83][0].rgba;
        require(r[center+1]>0&&std::abs(r[center]/r[center+1]-linear(220)/linear(80))<.25,"TV north imagery is reversed or sRGB was used as physical albedo");
        require(b[center+2]>b[center+1]*5&&b[center+2]>b[center]*20,"TV south imagery georeferencing mismatch");
        require(products[82][5].rgba[center]+products[82][5].rgba[center+1]+products[82][5].rgba[center+2]<1e-5,"destroyed terrain survived in TV sensor textures/geometry");
        const auto& echoes=products[84];require(echoes.size()==2&&echoes[0].frame_id==3&&echoes[1].frame_id==4,"cross-boundary terrain CPI was fabricated or frame IDs reset");
        double energy=0,empty=0;for(const auto& value:echoes[0].iq)energy+=std::norm(value);for(const auto& value:echoes[1].iq)empty+=std::norm(value);
        require(energy>0&&empty==0,"terrain radar echo used stale geometry after complete destruction");
        const double centerTime=echoes[0].start_time+1/parameters.prf;
        const auto station=aperture(84,centerTime).position;
        const auto measuredScene=scene->sample(centerTime);
        size_t groundHits=0;
        for(const auto& sample:echoes[0].terrain_footprint)if(sample.w>0.f){
            const vsg::dvec3 delta{sample.x,sample.y,sample.z};const auto range=vsg::length(delta);
            const auto hit=raycastVoxelScene(*measuredScene,station,delta/range,parameters.range_max_m);
            require(hit&&std::abs(hit->distance-range)<.004,"terrain overlay point disagrees with canonical ray distance");
            ++groundHits;
        }
        require(groundHits>0&&!echoes[1].terrain_footprint.empty()&&std::none_of(echoes[1].terrain_footprint.begin(),echoes[1].terrain_footprint.end(),[](auto sample){return sample.w>0.f;}),
                "terrain footprint missing or keeps ghosts of removed ground");
        require(measurements.rejectedSceneWindows()>=3&&measurements.contentRevision()==scene->snapshot()->contentRevision,"terrain topology validity/gating counters lost");
        // HD and smooth-voxel triangles retain exact backing-cell ownership.
        // Per-cell spectral properties must survive mode switches and crater cuts.
        auto spectralDoc=earth_fixture::create(root);
        spectralDoc["objects"][0]["sensor_materials"]={
            {"palette",{{{"temperature_k",310},{"eps_mwir",.6}},{{"temperature_k",370},{"eps_mwir",.9}}}},
            {"regions",{{{"min_m",{0,-10,-10}},{"max_m",{10,10,10}},{"material",1}}}}};
        spectralDoc["events"]={{{"kind","terrain_mode"},{"object","earth"},{"tick",2},{"mode","voxel"}},
            {{"kind","damage"},{"object","earth"},{"tick",3},{"space","object"},{"point_m",{2.25,1,.25}},{"radius_m",.6}},
            {{"kind","terrain_mode"},{"object","earth"},{"tick",4},{"mode","hd"}}};
        auto spectral=VoxelScene::load(spectralDoc,(root/"spectral.vscene.json").string());auto left=ir;left.id=91;auto right=ir;right.id=92;
        VoxelSensorSession cellMeasurements(*spectral,{left,right},owner.device(),"earth-cell-materials");size_t checked=0;
        auto probe=[](uint32_t id,double){GpuAperture a;a.position={id==91?-2.25:2.25,8,.25};a.forward={0,-1,0};a.up={0,0,-1};return a;};spectral->setPaused(false);
        for(int tick=1;tick<=5;++tick){spectral->advanceTo(tick*.02,1);cellMeasurements.advanceTo(tick*.02,probe,environment,[&](GpuMeasurementProduct&& p){
            const auto t=.5*(p.start_time+p.end_time);auto state=spectral->sample(t);const auto a=probe(p.sensor_id,t);const auto hit=raycastVoxelScene(*state,a.position,a.forward,30);
            require(hit&&std::abs(p.rgba[center+3]*1000-hit->distance)<.003,"cell-material Earth GPU/collision geometry mismatch");
            require(std::abs(p.rgba[center+1]-(p.sensor_id==91?310:370))<.1,"Earth local cell temperature lost across mode/damage");
            require(std::abs(p.rgba[center+2]-(p.sensor_id==91?.6:.9))<.001,"Earth per-band cell emissivity ignored");++checked;});}
        require(checked==10,"Earth per-cell measurements changed cadence");
        std::cout<<"PASS: real Vulkan Earth HD/voxel/crater range truth, georeferenced linear RGB, thermal material, radar CPI gating, total destruction and atomic publication on "<<owner.device()->getPhysicalDevice()->getProperties().deviceName<<'\n';
    }catch(...){std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
