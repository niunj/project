#include <rocky_env/GpuContext.h>
#include <rocky_env/SharedSceneResources.h>
#include <rocky_env/GBufferPool.h>
#include <rocky_env/SensorPipeline.h>
#include <rocky_env/GpuSensorRuntime.h>
#include <vsgraysim/Simulation.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/broker/EquipmentDataSource.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <filesystem>
#include <fstream>
#include <vsg/all.h>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <array>
#include <cstdlib>
#include <limits>
using namespace rockyenv::sensor;
namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
vsg::ref_ptr<vsg::VertexIndexDraw> quad() {
    auto draw=vsg::VertexIndexDraw::create();
    auto position=vsg::vec3Array::create({{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}});
    auto properties=vsg::vec2Array::create(4); for(auto& value:*properties) value={300,.9f};
    auto material=vsg::uintArray::create(4); for(auto& value:*material) value=0;
    auto uv=vsg::vec2Array::create(4); auto slot=vsg::uintArray::create(4);
    for(auto& value:*uv) value={0,0}; for(auto& value:*slot) value=0;
    auto index=vsg::uintArray::create({0,1,2,0,2,3});
    draw->assignArrays({position,properties,material,uv,slot}); draw->assignIndices(index);
    draw->indexCount=6; draw->instanceCount=1; return draw;
}
vsg::ref_ptr<vsg::VertexIndexDraw> overlappingTvQuads(bool nearFirst) {
    auto draw=vsg::VertexIndexDraw::create();
    auto positions=vsg::vec3Array::create({{-1,-1,2},{1,-1,2},{1,1,2},{-1,1,2},
                                         {-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}});
    auto properties=vsg::vec2Array::create(8);for(auto& value:*properties)value={300,.9f};
    auto materials=vsg::uintArray::create(8);for(auto& value:*materials)value=0;
    auto uv=vsg::vec2Array::create(8);for(auto& value:*uv)value={0,0};
    auto slots=vsg::uintArray::create(8);for(size_t i=0;i<8;++i)(*slots)[i]=i<4?1:2;
    auto indices=nearFirst?vsg::uintArray::create({0,1,2,0,2,3,4,5,6,4,6,7})
                          :vsg::uintArray::create({4,5,6,4,6,7,0,1,2,0,2,3});
    draw->assignArrays({positions,properties,materials,uv,slots});draw->assignIndices(indices);
    draw->indexCount=12;draw->instanceCount=1;return draw;
}
void voxelSceneMeasurement() {
    using namespace vsgraysim::earth;using json=nlohmann::json;
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",json::array({
        {{"id","test-block"},{"source",{{"type","box"},{"dimensions",{4,4,4}},{"origin_m",{-2,-2,-2}},{"cell_size_m",1}}},
         {"material",{{"temperature_k",320},{"emissivity",.85},{"rho_tv",.5},{"color_rgba",{.8,.2,.1,1}}}}}})},
        {"events",json::array({{{"kind","damage"},{"object","test-block"},{"tick",2},{"point_m",{0,0,0}},{"radius_m",2.3}}})}});
    GpuContext owner;owner.initialize();GpuSensorRuntime runtime;
    GpuSensorSpec thermal;thermal.id=71;thermal.period_seconds=.02;thermal.profile.width=9;thermal.profile.height=7;
    thermal.profile.fov_deg=35;thermal.profile.band=BandSpec::make(BandType::MWIR);thermal.output=ProductStage::Scene;
    auto visible=thermal;visible.id=72;visible.profile.band=BandSpec::make(BandType::TV);
    auto data=makeVoxelSceneSensorData(scene->snapshot());
    runtime.initialize(data.instances,data.materials,{thermal,visible},false,owner.device(),&data.textures);
    auto truth=[&](uint32_t,double,GpuAperture& aperture,auto& transforms,auto& masks){
        aperture.position={0,0,10};aperture.forward={0,0,-1};
        for(const auto& instance:data.instances){transforms.push_back(instance.transform);masks.push_back(instance.visible);}
    };
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer env;env.sigma_per_km=0;env.sky_radiance=0;env.sun_irradiance=100;env.sun_az_deg=180;env.sun_el_deg=0;return env;};
    std::map<uint32_t,std::vector<GpuMeasurementProduct>> products;
    auto sink=[&](GpuMeasurementProduct&& product){products[product.sensor_id].push_back(std::move(product));};
    scene->setPaused(false);scene->advanceTo(.02);runtime.advanceTo(.02,truth,environment,sink);
    const size_t center=(3*9+4)*4;const auto& ir=products.at(71).at(0).rgba;const auto& tv=products.at(72).at(0).rgba;
    require(ir[center]>0&&std::abs(ir[center+1]-320)<1,"voxel scene sensor lost material temperature");
    require(tv[center+1]>0&&std::abs(tv[center]/tv[center+1]-4)<.01&&std::abs(tv[center+1]/tv[center+2]-2)<.01,"dynamic voxel scene lost its per-object visible color");
    require(bool(scene->raycast({0,0,10},{0,0,-1},20)),"voxel collision missed intact target");
    scene->advanceTo(.04);data=makeVoxelSceneSensorData(scene->snapshot());
    require(!scene->raycast({0,0,10},{0,0,-1},20),"damage did not open the collision ray");
    runtime.replaceScene(data.instances,data.materials,&data.textures);runtime.advanceTo(.04,truth,environment,sink);
    require(products.at(71).size()==2&&products.at(71).back().frame_id==1,"voxel geometry replacement reset sensor cadence");
    require(products.at(71).back().rgba[center+1]<1,"sensor measured the removed voxel surface after geometry replacement");
    scene->schedule({{"kind","damage"},{"object","test-block"},{"tick",3},{"point_m",{0,0,0}},{"radius_m",100}});
    scene->advanceTo(.06);data=makeVoxelSceneSensorData(scene->snapshot());
    require(data.instances.empty(),"fully destroyed volume retained a sensor instance");
    runtime.replaceScene(data.instances,data.materials,&data.textures);runtime.advanceTo(.06,truth,environment,sink);
    require(products.at(71).size()==3&&products.at(71).back().rgba[center+1]<1,"fully destroyed scene did not produce a background measurement");
    auto trulyEmpty=makeVoxelSceneSensorData(VoxelScene::load({{"format","voxel-scene-v1"},{"objects",json::array()}})->snapshot());
    runtime.replaceScene(trulyEmpty.instances,trulyEmpty.materials,&trulyEmpty.textures);data=std::move(trulyEmpty);
    runtime.advanceTo(.08,truth,environment,sink);
    require(products.at(71).size()==4&&products.at(71).back().rgba[center+1]<1,"empty authoring scene cannot be measured");
    std::cout<<"PASS: published voxel occupancy drives collision, MWIR temperature, TV color, GPU topology replacement and empty-scene measurements\n";
}
void batchEquivalence(){
    struct Result {std::vector<GpuMeasurementProduct> products;GpuSensorRuntime::Statistics stats;};
    auto run=[&](bool serial,int motion,bool backlog,bool p0=false){
        if(serial)::setenv("VSGRAYSIM_GPU_SERIAL_REFERENCE","1",1);else ::unsetenv("VSGRAYSIM_GPU_SERIAL_REFERENCE");
        GpuSensorSpec image;image.id=1;image.profile.width=9;image.profile.height=7;image.profile.fov_deg=60;image.period_seconds=.08;image.profile.detector.integration_ms=5;image.profile.band=BandSpec::make(BandType::MWIR);image.output=p0?ProductStage::Scene:ProductStage::Image;
        auto thermal=image;thermal.id=2;thermal.profile.band=BandSpec::make(BandType::LWIR);
        auto visible=image;visible.id=3;visible.profile.band=BandSpec::make(BandType::TV);
        auto radar=image;radar.id=4;radar.output=ProductStage::Focal;RadarChainParams p;p.width=9;p.height=7;p.cpi_pulses=8;p.prf=100;p.n_fast=128;p.range_min_m=0;p.range_max_m=100;p.sample_rate_hz=30e6;p.bandwidth_hz=30e6;p.pulse_width_s=1/p.sample_rate_hz;p.beam_width_rad=1.;radar.radar=p;
        GpuSensorRuntime runtime;std::vector<RadarMaterial> materials(1);materials[0].emissivity=.9f;
        runtime.initialize({{"quad",quad(),vsg::dmat4(),true}},materials,{image,thermal,visible,radar});
        auto truth=[&](uint32_t id,double t,GpuAperture& a,auto& transforms,auto& masks){a.position={.1*id,0,10};a.forward={0,0,-1};transforms.push_back(vsg::translate(motion==1?t*.01:motion==2?.3*id:0.,0.,0.));masks.push_back(true);};
        auto env=[](uint32_t,double,const BandSpec& band){auto e=clearSkyEnv(band,false);e.sigma_per_km=0;return e;};Result r;
        auto sink=[&](GpuMeasurementProduct&& product){r.products.push_back(std::move(product));};
        if(backlog)runtime.advanceTo(.32,truth,env,sink);else for(int tick=1;tick<=16;++tick)runtime.advanceTo(tick*.02,truth,env,sink);
        r.stats=runtime.statistics();return r;
    };
    for(int motion=0;motion<3;++motion)for(bool backlog:{false,true}){
        const auto serial=run(true,motion,backlog),batch=run(false,motion,backlog);require(serial.products.size()==16&&batch.products.size()==16,"batch changed event count");
        for(size_t n=0;n<serial.products.size();++n){const auto& a=serial.products[n];const auto& b=batch.products[n];require(a.sensor_id==b.sensor_id&&a.frame_id==b.frame_id&&a.start_time==b.start_time&&a.end_time==b.end_time&&a.width==b.width&&a.height==b.height,"batch changed acquisition identity/order/time");if(a.rgba!=b.rgba){double error=0;size_t index=0;for(size_t k=0;k<a.rgba.size();++k)if(std::abs(double(a.rgba[k])-b.rgba[k])>error){error=std::abs(double(a.rgba[k])-b.rgba[k]);index=k;}std::cerr<<"Image batch mismatch motion="<<motion<<" backlog="<<backlog<<" sensor="<<a.sensor_id<<" frame="<<a.frame_id<<" element="<<index<<" reference="<<a.rgba[index]<<" batch="<<b.rgba[index]<<'\n';}require(a.rgba==b.rgba,"batch changed detector image, noise seed, view or band");
            if(!a.iq.empty()){double peak=0,error=0;for(size_t k=0;k<a.iq.size();++k){peak=std::max(peak,double(std::abs(a.iq[k])));error=std::max(error,double(std::abs(a.iq[k]-b.iq[k])));}require(error<=std::max(1e-12,128*std::numeric_limits<float>::epsilon()*peak),"batched CPI exceeds float32 reduction bound");}
        }
        if(motion==0){require(batch.stats.stationaryCpis==4&&batch.stats.multiEquipmentBatches==4&&batch.stats.maxBatchSize==3,"compatible jobs were not batched");require(batch.stats.graphSubmissions==24&&serial.stats.graphSubmissions==52,"unexpected actual graph submission counts");}
        if(motion==1)require(batch.stats.stationaryCpis==0&&batch.stats.dynamicCpis==4,"moving truth incorrectly used stationary CPI");
        if(motion==2)require(batch.stats.maxBatchSize==1,"different committed transforms shared a batch");
    }
    const auto full=run(true,0,false,true),prefix=run(false,0,false,true);
    require(full.products.size()==prefix.products.size(),"P0 stage pruning changed acquisition count");
    for(size_t n=0;n<full.products.size();++n)require(full.products[n].rgba==prefix.products[n].rgba,"P0 execution prefix changed physical radiance");
    ::unsetenv("VSGRAYSIM_GPU_SERIAL_REFERENCE");
    std::cout<<"PASS: four-equipment batching, exact P4/noise/time/order, bounded RD, dynamic fallback, incompatible truth and backlog\n";
}
void fracturedVoxelMeasurements() {
    using namespace vsgraysim::earth;using json=nlohmann::json;
    auto box=[](const char* id,json dimensions,json position){return json{{"id",id},{"position_m",position},
        {"source",{{"type","box"},{"dimensions",dimensions},{"cell_size_m",1}}},
        {"physics",{{"motion","static"},{"linear_damping",0},{"angular_damping",0},{"restitution",0}}},
        {"material",{{"temperature_k",325},{"rho_tv",.6},{"color_rgba",{.7,.3,.1,1}}}}};};
    auto column=box("column",{2,6,2},{0,0,0});column["physics"]["fracture"]=true;column["physics"]["support"]="base";
    auto scene=VoxelScene::load({{"format","voxel-scene-v1"},{"objects",{box("floor",{12,1,12},{-5,-1,-5}),column}},
        {"events",json::array({{{"tick",1},{"kind","damage"},{"object","column"},{"space","object"},{"point_m",{1,3,1}},{"radius_m",1.1}}})}});
    GpuContext owner;owner.initialize();GpuSensorSpec ir;ir.id=81;ir.period_seconds=.02;ir.profile.width=9;ir.profile.height=7;
    ir.profile.fov_deg=20;ir.profile.detector.integration_ms=5;ir.profile.band=BandSpec::make(BandType::MWIR);ir.output=ProductStage::Scene;
    auto tv=ir;tv.id=82;tv.profile.band=BandSpec::make(BandType::TV);
    auto crossing=ir;crossing.id=83;crossing.period_seconds=.03;crossing.profile.detector.integration_ms=20;
    VoxelSensorSession sensors(*scene,{ir,tv,crossing},owner.device());
    auto display=makeVoxelSceneDisplay(*scene->snapshot());std::optional<VoxelSceneDisplay> stagedDisplay;
    scene->setPreparePublication([&](auto snapshot){if(snapshot->contentRevision!=display.contentRevision)stagedDisplay=makeVoxelSceneDisplay(*snapshot);});
    std::map<uint32_t,std::vector<GpuMeasurementProduct>> products;
    auto sink=[&](GpuMeasurementProduct&& p){products[p.sensor_id].push_back(std::move(p));};
    auto aperture=[](uint32_t,double){GpuAperture a;a.position={1,3.5,-10};a.forward={0,0,1};return a;};
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_az_deg=0;e.sun_el_deg=0;return e;};
    scene->setPaused(false);const auto before=scene->checkpoint();scene->addPublicationParticipant("zz-failure",[](auto){throw std::runtime_error("injected multi-consumer failure");});
    bool rejected=false;try{scene->advanceTo(.02);}catch(const std::exception&){rejected=true;}
    require(rejected&&scene->checkpoint()==before&&sensors.contentRevision()==1&&products.empty(),"failed fracture published sensor resources or consumed measurement time");
    scene->removePublicationParticipant("zz-failure");
    auto step=[&](int tick){const double time=tick*.02;scene->advanceTo(time,1);auto snapshot=scene->snapshot();
        if(stagedDisplay&&stagedDisplay->contentRevision==snapshot->contentRevision){display=std::move(*stagedDisplay);stagedDisplay.reset();}display.sync(*snapshot);
        sensors.advanceTo(time,aperture,environment,sink);
        for(const auto& [id,o]:snapshot->objects)require(display.placements.at(id)->matrix==o.pose(snapshot->tick,snapshot->fixedStep),"display fragment transform differs from published truth");
    };
    step(1);const auto fragment=scene->snapshot()->journal[0]["fragments"][0].get<std::string>();
    for(int tick=2;tick<=25;++tick)step(tick);
    const size_t center=(3*9+4)*4;
    require(products[81].size()==25&&products[81][0].rgba[center+1]>324,"pre-fracture exposure was rendered with geometry born at its end");
    require(products[81][1].rgba[center+1]<1,"detached support left a ghost sensor surface");
    require(products[81].back().rgba[center+1]>324,"falling fragment did not enter the thermal measurement ray");
    for(const auto& p:products[81]) {
        const double t=.5*(p.start_time+p.end_time);const auto state=scene->sample(t);const auto hit=raycastVoxelScene(*state,{1,3.5,-10},{0,0,1},100);
        require(bool(hit)==(p.rgba[center+1]>1),"GPU sensor and occupied-volume collision disagree at acquisition time");
        if(hit)require(std::abs(p.rgba[center+1]-state->objects.at(hit->object).material.at("temperature_k").get<double>())<1,"fragment temperature was lost during sensor topology replacement");
    }
    const auto& color=products[82].back().rgba;
    require(color[center+2]>0&&std::abs(color[center]/color[center+1]-7./3)<.02&&std::abs(color[center+1]/color[center+2]-3)<.02,"fragment TV color was lost after topology replacement");
    require(sensors.rejectedSceneWindows()==1&&products[83].front().frame_id==1,"cross-topology exposure was silently filled with latest geometry");
    require(sensors.contentRevision()==scene->snapshot()->contentRevision,"sensor consumer did not commit the published topology version");
    scene->schedule({{"kind","damage"},{"object",fragment},{"tick",26},{"space","object"},{"point_m",{1,5,1}},{"radius_m",100}});step(26);step(27);
    require(products[81].back().rgba[center+1]<1,"destroyed moving fragment remained in sensor acceleration structures");
    std::cout<<"PASS: atomic display/Bullet/GPU fracture publication, historical exposure geometry, falling-fragment MWIR/TV and topology-boundary gating\n";
}
void sharedVoxelCloudMeasurement(){
    GpuContext context;context.initialize();
    auto cloud=rockyenv::createCloudGpuResources();cloud->ownerDevice=context.device();
    auto water=vsg::vec4Array3D::create(8,8,8,vsg::vec4(.01f,.0006f,0,0));
    water->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
    auto sampler=vsg::Sampler::create();sampler->maxLod=0;
    sampler->addressModeU=sampler->addressModeV=sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cloud->image=vsg::ImageInfo::create(sampler,water);
    auto& chart=*cloud->layout;
    chart[0]=vsg::vec4(6378137,0,0,1);chart[1]=vsg::vec4(0,1,0,24);
    chart[2]=vsg::vec4(1,0,0,8);chart[3]=vsg::vec4(0,0,1,8);chart[4]=vsg::vec4(-96,900,-96,8);
    vsg::dmat4 toECEF;toECEF[0]=vsg::dvec4(0,1,0,0);toECEF[1]=vsg::dvec4(1,0,0,0);toECEF[2]=vsg::dvec4(0,0,-1,0);toECEF[3]=vsg::dvec4(6379137,0,0,1);
    SharedSceneResources scene;scene.voxelCloud=rockyenv::createCloudSceneBinding(cloud,toECEF);
    std::vector<RadarMaterial> materials(1);materials[0].emissivity=.9f;
    auto geometry=quad();auto built=scene.buildInstances(*context.device(),{{"quad",geometry,vsg::dmat4(),true}},materials);require(built.ok,built.error.c_str());
    SensorProfile profile;profile.width=9;profile.height=7;profile.fov_deg=45;profile.band=BandSpec::make(BandType::MWIR);
    BandRadianceLut lut;require(lut.build(profile.band),"voxel measurement LUT");
    GBufferPool gbuffer;auto gb=gbuffer.build(*context.device(),scene.sceneGeometry(),&materials,&scene,9,7);require(gb.ok,gb.error.c_str());
    EnvAnswer env;env.sigma_per_km=0;env.sky_radiance=0;env.sun_irradiance=0;
    SensorPipeline pipeline;auto result=pipeline.buildOffscreen(*context.device(),profile,lut,env,scene.sceneGeometry(),&scene,&gbuffer);require(result.ok,result.error.c_str());
    auto proj=vsg::Perspective::create(45,9./7,.01,100)->transform();vsg::dvec3 eye(0,0,10);
    auto view=vsg::LookAt::create(eye,vsg::dvec3(0,0,0),vsg::dvec3(0,1,0))->transform();gbuffer.setSensorView(proj,view);pipeline.setSensorView(proj,view,eye,.01,100,45,9./7);
    context.compile(pipeline.commandGraph);context.execute(pipeline.commandGraph);
    std::vector<float> clear,cloudy;require(pipeline.readbackStage(*context.device(),ProductStage::Scene,clear),"clear voxel measurement");
    (*cloud->weather)[1]=vsg::vec4(1,10,900,1092);cloud->weather->dirty();context.execute(pipeline.commandGraph);
    require(pipeline.readbackStage(*context.device(),ProductStage::Scene,cloudy),"cloud voxel measurement");
    const size_t centre=(3*9+4)*4;const float ratio=cloudy[centre]/clear[centre];
    require(ratio>.3f&&ratio<.7f,"sensor did not use shared liquid-water extinction");
    std::cout<<"PASS: shared voxel liquid water attenuates the measured target, ratio="<<ratio<<'\n';context.clearGraphs();
}
void tvRasterRadiance() {
    GpuContext context;context.initialize();
    struct Fixture {
        float reflectance=.2f,temperature=300,irradiance=120;
        double sunAzimuth=180; // +Z, facing the quad and camera.
        uint32_t slot=0;bool visible=true;
        vsg::vec3 sky{0,0,0}; // Physical RGB; updateSkyMap receives spectral B,G,R.
        std::vector<vsg::vec4> colors{{1,1,1,0}};
        vsg::ref_ptr<vsg::VertexIndexDraw> geometry;
    };
    auto render=[&](const Fixture& fixture) {
        auto geometry=fixture.geometry?fixture.geometry:quad();
        auto* properties=geometry->arrays[1]->data->cast<vsg::vec2Array>();for(auto& value:*properties)value.x=fixture.temperature;
        if(!fixture.geometry){auto* slots=geometry->arrays[4]->data->cast<vsg::uintArray>();for(auto& value:*slots)value=fixture.slot;}
        if(!fixture.visible){auto* positions=geometry->arrays[0]->data->cast<vsg::vec3Array>();for(auto& value:*positions)value.x+=100;}
        std::vector<RadarMaterial> materials(1);materials[0].emissivity=.9f;materials[0].rho_tv=fixture.reflectance;materials[0].spec_strength=0;
        SharedSceneResources::TexArraySpec textures;textures.slotData=fixture.colors;
        SharedSceneResources scene;const auto resources=scene.build(*context.device(),geometry,&materials,&textures);require(resources.ok,resources.error.c_str());
        SensorProfile profile;profile.band=BandSpec::make(BandType::TV);profile.width=9;profile.height=7;profile.fov_deg=45;
        BandRadianceLut lut;require(lut.build(profile.band),"TV raster LUT build failed");
        EnvAnswer environment;environment.sigma_per_km=0;environment.sky_radiance=0;environment.sun_irradiance=fixture.irradiance;environment.sun_az_deg=fixture.sunAzimuth;environment.sun_el_deg=0;
        SensorPipeline pipeline;const auto built=pipeline.buildOffscreen(*context.device(),profile,lut,environment,scene.sceneGeometry(),&scene,nullptr,false);require(built.ok,built.error.c_str());
        require(!pipeline.rtModule()&&!pipeline.commandGraph->window,"TV raster regression did not force the window-free fallback");
        const vsg::dvec3 eye(0,0,10);const auto projection=vsg::Perspective::create(45,double(profile.width)/profile.height,.01,100)->transform();
        const auto view=vsg::LookAt::create(eye,vsg::dvec3(0,0,0),vsg::dvec3(0,1,0))->transform();pipeline.setSensorView(projection,view,eye,.01,100,45,double(profile.width)/profile.height);
        std::vector<float> sky(size_t(256)*128*4);for(size_t i=0;i<sky.size();i+=4){sky[i]=fixture.sky.z;sky[i+1]=fixture.sky.y;sky[i+2]=fixture.sky.x;sky[i+3]=1;}
        pipeline.updateSkyMap(sky.data(),256,128);
        context.compile(pipeline.commandGraph);context.execute(pipeline.commandGraph);std::vector<float> image;
        require(pipeline.readbackStage(*context.device(),ProductStage::Scene,image),"TV raster physical RGB readback failed");require(image.size()==size_t(profile.width)*profile.height*4,"TV raster P0 dimensions");
        const size_t center=(size_t(profile.height/2)*profile.width+profile.width/2)*4;
        const std::array<float,3> rgb{image[center],image[center+1],image[center+2]};
        for(float value:rgb)require(std::isfinite(value)&&value>=-1e-6f,"TV raster produced invalid physical radiance");
        context.clearGraphs();return rgb;
    };
    constexpr float pi=3.14159265358979323846f;
    Fixture parameters;const auto low=render(parameters);parameters.reflectance=.6f;const auto high=render(parameters);
    for(size_t channel=0;channel<3;++channel){
        require(std::abs(low[channel]-.2f*120/pi)<.002f,"TV raster RGB does not contain physical Lambert radiance");
        require(std::abs(high[channel]/low[channel]-3)<.002f,"TV raster ignored spectral reflectance");
        require(high[channel]<50,"TV raster leaked temperature or emissivity into a color channel");
    }
    parameters.temperature=600;const auto hot=render(parameters);for(size_t channel=0;channel<3;++channel)require(std::abs(hot[channel]-high[channel])<.002f,"TV raster physical P0 contains a temperature-driven display glow");
    parameters=Fixture{};parameters.irradiance=0;const auto night=render(parameters);parameters.irradiance=120;parameters.sunAzimuth=0;const auto back=render(parameters);parameters.sunAzimuth=180;parameters.reflectance=0;const auto black=render(parameters);
    for(size_t channel=0;channel<3;++channel)require(night[channel]<1e-5f&&back[channel]<1e-5f&&black[channel]<1e-5f,"TV raster failed night, back-light or physical black-material response");
    parameters=Fixture{};parameters.slot=1;parameters.colors={{1,1,1,0},{.9f,.3f,.1f,0}};const auto color=render(parameters);
    require(std::abs(color[0]/color[1]-3)<.002f&&std::abs(color[1]/color[2]-3)<.002f,"TV raster Kd color channel order is incorrect");
    parameters=Fixture{};parameters.visible=false;parameters.irradiance=0;parameters.sky={5,3,2};const auto sky=render(parameters);
    for(size_t channel=0;channel<3;++channel)require(std::abs(sky[channel]-parameters.sky[channel])<.002f,"TV raster sky spectral BGR was not converted to physical RGB");
    parameters=Fixture{};parameters.colors={{1,1,1,0},{.8f,.1f,.05f,0},{.05f,.1f,.8f,0}};parameters.geometry=overlappingTvQuads(true);const auto nearFirst=render(parameters);parameters.geometry=overlappingTvQuads(false);const auto nearLast=render(parameters);
    for(size_t channel=0;channel<3;++channel){require(std::abs(nearFirst[channel]-nearLast[channel])<.002f,"TV raster nearest surface depends on draw order");require(std::abs(nearFirst[channel]-parameters.colors[1][channel]*120/pi)<.002f,"TV raster depth test selected the farther surface");}
    std::cout<<"PASS: forced TV raster physical RGB, reflectance, sun, sky color and order-independent depth\n";
}
void measurementRuntime() {
    GpuContext owner; owner.initialize();
    GpuSensorRuntime runtime;
    SensorProfile profile; profile.width=9; profile.height=7; profile.fov_deg=60;
    profile.band=BandSpec::make(BandType::MWIR); profile.detector.integration_ms=5;
    GpuSensorSpec image; image.id=1; image.profile=profile; image.period_seconds=.02; image.output=ProductStage::Scene;
    auto other=image; other.id=2; other.period_seconds=.025; other.profile.width=7; other.profile.height=5;
    GpuSensorSpec radar; radar.id=3; radar.profile=profile; radar.echo_only=true;
    RadarChainParams parameters; parameters.width=9; parameters.height=7; parameters.n_fast=128;
    parameters.cpi_pulses=2; parameters.prf=40; parameters.range_max_m=1000; parameters.beam_width_rad=1;
    radar.radar=parameters;
    std::vector<RadarMaterial> materials(1); materials[0].emissivity=.9f;
    SceneInstance instance; instance.id="target"; instance.geometry=quad();
    runtime.initialize({instance},materials,{image,other,radar},false,owner.device());
    std::map<uint32_t,std::vector<GpuMeasurementProduct>> products;
    auto truth=[](uint32_t,double time,GpuAperture& aperture,std::vector<vsg::dmat4>& transforms,std::vector<bool>& masks) {
        aperture.position={0,0,10}; aperture.forward={0,0,-1};
        transforms.push_back(vsg::translate(time<.02 ? 0. : 20.,0.,0.)); masks.push_back(true);
    };
    auto sink=[&](GpuMeasurementProduct&& product){products[product.sensor_id].push_back(std::move(product));};
    runtime.advanceTo(.1,truth,{},sink);
    require(products[1].size()==5 && products[2].size()==4 && products[3].size()==2,"independent GPU cadence collapsed measurements");
    require(products[1][0].width==9 && products[2][0].width==7,"sensor dimensions share resources");
    require(std::abs(products[1][4].end_time-.1)<1e-12 && products[1][4].frame_id==4,"GPU frame timestamp/sequence");
    const auto& echo=products[3][0].iq;
    double first=0,second=0;
    for(std::size_t i=0;i<128;++i) { first+=std::norm(echo[i]); second+=std::norm(echo[128+i]); }
    require(first>0 && second==0,"radar reused CPI geometry or overwrote selected pulse rows");
    runtime.replaceScene({instance},materials);
    runtime.advanceTo(.15,truth,{},sink,[](uint32_t id){return id==1;});
    require(products[1].size()==7 && products[1].back().frame_id==6,"scene rebuild reset measurement clocks");
    require(products[2].size()==4 && products[3].size()==2,"sensor gate leaked GPU products");
    std::cout<<"PASS: independent image clocks, per-pulse moving geometry, CPI preservation, scene rebuild and gating\n";
}
void simulationPublishing() {
    using namespace vsgraysim;
    struct Scratch {
        std::filesystem::path root;
        Scratch() { std::string name="/tmp/vsgraysim_gpu_XXXXXX"; auto* path=mkdtemp(name.data()); require(path,"fixture directory"); root=path; }
        ~Scratch() { std::error_code error; std::filesystem::remove_all(root,error); }
    } scratch;
    const auto mesh=scratch.root/"target.obj",path=scratch.root/"scene.json";
    { std::ofstream out(mesh); out<<"v -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nf 1 2 3\nf 1 3 4\n"; }
    nlohmann::json scene;
    scene["simulation"]={{"sensor_backend","gpu"},{"frame_dt",.1},{"physics_step",.01},{"history_seconds",2}};
    scene["objects"]={{{"object_id","target"},{"mesh_path",mesh.string()},{"face_count",2}}};
    scene["objects"][0]["radar_x_params"]={{"dielectric_real",6.0},{"surface_rough",.3}};
    for(uint32_t id:{51,52}) scene["sensors"]["sensor_list"].push_back({{"sensor_id",id},{"type","ir"},{"band","MWIR"},
        {"tx_params",{{"power",0},{"wavelength",4e-6}}},
        {"signal_params",{{"image_width",9},{"image_height",7},{"frame_interval_s",id==51 ? .02 : .025},{"integration_time",.005},{"netd",.03}}},
        {"aperture",{{"position_offset",{0,0,10}},{"direction",{0,0,-1}},{"fov_horizontal",60}}}});
    scene["sensors"]["sensor_list"][1]["aperture"]["position_offset"]={0,10,0};
    scene["sensors"]["sensor_list"][1]["aperture"]["direction"]={0,-1,0};
    for(uint32_t id:{53,54}) scene["sensors"]["sensor_list"].push_back({{"sensor_id",id},{"type",id==53 ? "radar" : "sar"},
        {"tx_params",{{"power",id==53 ? 1000 : 0},{"wavelength",.03},{"pulse_rate",40},{"beam_width",1}}},
        {"frame",{{"kind","RADAR"},{"rate_hz",40},{"cpi_pulses",2},{"resolution",{{"width",9},{"height",7}}}}},
        {"signal_params",{{"range_gates",128},{"fs",30e6}}},
        {"aperture",{{"position_offset",{0,0,10}},{"direction",{0,0,-1}},{"fov_horizontal",60},{"range_m",1000}}}});
    auto visible=scene["sensors"]["sensor_list"][0]; visible["sensor_id"]=55; visible["type"]="vis"; visible["band"]="VIS";
    visible["signal_params"]["frame_interval_s"]=.05; visible["tx_params"]["wavelength"]=550e-9;
    scene["sensors"]["sensor_list"].push_back(visible);
    { std::ofstream out(path); out<<scene; }
    const auto descriptions=parseSensorDescriptions(scene.dump());
    require(descriptions.size()==5 && descriptions[0].integration_ms==5 && descriptions[0].netd_mk==30,"legacy detector units were not forwarded to GPU profile");
    GpuContext owner; owner.initialize();
    Simulation simulation; simulation.setSensorDevice(owner.device());
    require(simulation.initFromScenario(path.string()),"strict GPU scenario initialization failed");
    require(simulation.gpuSensorBackendActive(),"main simulation silently selected CPU");
    auto& broker=SimDataBroker::instance();
    struct Reset { SimDataBroker& broker; ~Reset(){broker.setFrameTap({},{},{},{}); broker.serverOnline(false);} } reset{broker};
    broker.serverOnline(true); std::vector<IrFrame> received;
    std::vector<RadarFrame> radarFrames; std::vector<SarFrame> sarFrames; std::vector<VisFrame> visFrames;
    broker.setFrameTap([&](const RadarFrame& frame){radarFrames.push_back(frame);},
        [&](const SarFrame& frame){sarFrames.push_back(frame);},[&](const IrFrame& frame){received.push_back(frame);},
        [&](const VisFrame& frame){visFrames.push_back(frame);});
    EquipmentDataSource source(&simulation); require(source.initialize({}),"GPU data source initialization failed");
    TimeTick tick; tick.dt=.1; tick.sim_time=.1; source.onTick(tick);
    require(source.lastTickOk() && received.size()==9,"GPU products lost between main simulation and Broker");
    std::map<uint32_t,uint64_t> count;
    for(const auto& frame:received) {
        require(frame.frame_id==count[frame.sensor_id]++,"published GPU frame sequence");
        require(frame.width==9 && frame.height==7 && frame.radiance.size()==63,"GPU publication payload dimensions");
        require(frame.stage==0,"physical radiance mislabeled as electronic image");
        require(frame.radiance[31]>0 && std::isfinite(frame.radiance[31]),"published GPU radiance is empty");
    }
    require(count[51]==5 && count[52]==4,"published GPU cadence depends on main step");
    require(radarFrames.size()==2 && sarFrames.size()==2 && visFrames.size()==2,"coherent/VIS GPU products were not published at independent cadence");
    require(radarFrames[0].stage==2 && radarFrames[0].sample_rate_hz==30e6f && radarFrames[0].rd_map.size()==256,"GPU RD stage or sampling metadata");
    double energy=0; for(auto value:radarFrames[0].rd_map) energy+=std::norm(value);
    require(energy>0,"GPU main program did not execute radar FFT output");
    require(sarFrames[0].stage==0 && sarFrames[0].sample_rate_hz==30e6f && sarFrames[0].echo_data.size()==256,"SAR raw echo mislabeled or lost sampling metadata");
    for(auto value:sarFrames[0].echo_data) require(std::norm(value)==0,"zero-power SAR fabricated nonzero returns");
    require(visFrames[0].stage==4 && visFrames[0].intensity.size()==63,"VIS electronic image publication");
    for(auto value:visFrames[0].intensity) require(std::isfinite(value),"nonfinite VIS electronic image");
    simulation.setSensorGate(true,{51}); tick.sim_time=.2; source.onTick(tick);
    require(source.lastTickOk() && received.size()==14,"main GPU sensor gate or frame continuity");
    require(radarFrames.size()==2 && sarFrames.size()==2 && visFrames.size()==2,"GPU gate leaked coherent/VIS products");
    simulation.stop();
    std::cout<<"PASS: strict GPU main program -> independent measurements -> EquipmentDataSource -> Broker\n";
}
}
int main() {
    try {
        {
        GpuContext context; context.initialize();
        std::cout<<"Offscreen Vulkan: "<<context.deviceName()<<" RT="<<context.rayTracing()<<'\n';
        SharedSceneResources scene;
        auto geometry=quad();
        std::vector<RadarMaterial> materials(1); materials[0].emissivity=.9f;
        auto built=scene.buildInstances(*context.device(),{{"quad",geometry,vsg::dmat4(),true}},materials);
        require(built.ok,built.error.c_str());
        SensorProfile profile; profile.width=9; profile.height=7; profile.fov_deg=60;
        profile.band=BandSpec::make(BandType::MWIR);
        BandRadianceLut lut; require(lut.build(profile.band),"radiance LUT failed");
        EnvAnswer environment; environment.sigma_per_km=0; environment.sky_radiance=0; environment.sun_irradiance=0;
        GBufferPool gbuffer;
        auto bufferBuilt=gbuffer.build(*context.device(),scene.sceneGeometry(),&materials,&scene,profile.width,profile.height);
        require(bufferBuilt.ok,bufferBuilt.error.c_str());
        SensorPipeline pipeline;
        auto pipelineBuilt=pipeline.buildOffscreen(*context.device(),profile,lut,environment,scene.sceneGeometry(),&scene,&gbuffer);
        require(pipelineBuilt.ok,pipelineBuilt.error.c_str());
        require(!pipeline.commandGraph->window,"offscreen pipeline unexpectedly owns a window");
        const auto projection=vsg::Perspective::create(45,double(profile.width)/profile.height,.01,100)->transform();
        const vsg::dvec3 eye(0,0,10);
        const auto view=vsg::LookAt::create(eye,vsg::dvec3(0,0,0),vsg::dvec3(0,1,0))->transform();
        gbuffer.setSensorView(projection,view);
        pipeline.setSensorView(projection,view,eye,.01,100,45,double(profile.width)/profile.height);
        context.compile(pipeline.commandGraph); context.execute(pipeline.commandGraph);
        std::vector<float> image;
        require(pipeline.readbackStage(*context.device(),ProductStage::Scene,image),"offscreen P0 readback failed");
        const auto center=(std::size_t(profile.height/2)*profile.width+profile.width/2)*4;
        std::cout<<"P0 center L="<<image[center]<<" T="<<image[center+1]<<" eps="<<image[center+2]<<'\n';
        require(image.size()==std::size_t(profile.width)*profile.height*4,"offscreen dimensions");
        require(std::isfinite(image[center]) && image[center]>0 && std::abs(image[center+1]-300)<1,"shader did not hit thermal geometry");
        scene.setTransforms({vsg::translate(20.,0.,0.)},{true});
        context.execute(pipeline.commandGraph);
        require(pipeline.readbackStage(*context.device(),ProductStage::Scene,image),"second measurement readback failed");
        require(image[center+1]<1,"GPU instance transform was not updated between measurements");
        context.clearGraphs();
        std::cout<<"PASS: window-free production GPU chain, host readback and dynamic instance occlusion\n";
        }
        batchEquivalence();
        fracturedVoxelMeasurements();
        voxelSceneMeasurement();
        sharedVoxelCloudMeasurement();
        tvRasterRadiance();
        measurementRuntime();
        simulationPublishing();
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
