#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include "VoxelTerrain.h"
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/model_packages/SceneModelRegistry.h>
#include <vsgraysim/model_packages/equipment/VisualRegistry.h>
#include <rocky_env/EnvPhysicsService.h>
#include <rocky_env/EnvApi.h>
#include <rocky_env/RadarSampling.h>
#include <simcore/GeoFrame.h>
#include <vsg/maths/transform.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <tuple>
namespace vsgraysim::earth {
using namespace rockyenv::sensor;using json=nlohmann::json;
namespace {
void fields(const json& j,std::initializer_list<const char*> allowed){if(!j.is_object())throw std::invalid_argument("equipment configuration must be an object");for(const auto& f:j.items()){bool found=false;for(auto a:allowed)found|=f.key()==a;if(!found)throw std::invalid_argument("unknown equipment field: "+f.key());}}
double number(const json& j,const char* k,double fallback,double low,double high){double n=j.value(k,fallback);if(!std::isfinite(n)||n<low||n>high)throw std::invalid_argument(std::string("invalid equipment ")+k);return n;}
uint32_t integer(const json& j,const char* k,uint32_t fallback,uint32_t low,uint32_t high){const auto v=j.value(k,json(fallback));if(!v.is_number_integer()||v.get<double>()<low||v.get<double>()>high)throw std::invalid_argument(std::string("invalid equipment integer ")+k);return v.get<uint32_t>();}
vsg::dvec3 vector(const json& v){if(!v.is_array()||v.size()!=3)throw std::invalid_argument("equipment vector requires three coordinates");vsg::dvec3 p(v[0].get<double>(),v[1].get<double>(),v[2].get<double>());if(!std::isfinite(vsg::length(p)))throw std::invalid_argument("nonfinite equipment vector");return p;}
}
namespace {
model_sdk::Vec3 sdkVector(const vsg::dvec3& v){return {v.x,v.y,v.z};}
vsg::dvec3 vsgVector(const model_sdk::Vec3& v){return {v[0],v[1],v[2]};}
model_sdk::SceneTime sceneTime(const VoxelSceneSnapshot& snapshot){
    return {snapshot.tick,snapshot.revision,snapshot.contentRevision,snapshot.timeSeconds(),snapshot.fixedStep};
}
json timeJson(const VoxelSceneSnapshot& snapshot){
    const auto time=sceneTime(snapshot);
    return {{"tick",time.tick},{"state_revision",time.stateRevision},{"content_revision",time.contentRevision},
        {"sim_time",time.seconds},{"fixed_step_seconds",time.stepSeconds}};
}
model_sdk::RigidMount mountFrame(const vsg::dvec3& position,const vsg::dvec3& forward,const vsg::dvec3& up){
    const auto z=vsg::normalize(forward);
    const auto x=vsg::normalize(vsg::cross(up,z));
    const auto y=vsg::cross(z,x);
    return {sdkVector(position),{{{x.x,y.x,z.x},{x.y,y.y,z.y},{x.z,y.z,z.z}}}};
}
void applyMount(GpuAperture& aperture,const model_sdk::RigidMount& mount){
    const auto pose=model_sdk::IEquipmentRuntime::resolve({
        sdkVector(aperture.position),sdkVector(aperture.forward),sdkVector(aperture.up)},mount);
    aperture.position=vsgVector(pose.position);
    aperture.forward=vsgVector(pose.forward);
    aperture.up=vsgVector(pose.up);
}
GpuAperture mountedAperture(const VoxelEquipmentSpec& spec,const VoxelSceneSnapshot& scene) {
    auto aperture=spec.aperture;
    if(spec.mountObject.empty())return aperture;
    const auto& body=scene.objects.at(spec.mountObject);
    const auto matrix=body.pose(scene.tick,scene.fixedStep);
    model_sdk::RigidMount mount{};
    for(size_t row=0;row<3;++row){
        mount.position[row]=matrix[3][row];
        for(size_t col=0;col<3;++col)mount.rotation[row][col]=matrix[col][row];
    }
    applyMount(aperture,mount);
    return aperture;
}

std::string_view defaultEquipmentModel(std::string_view band) {
    if(band=="RADAR")return "radar";
    if(band=="SAR")return "sar";
    if(band=="TV")return "optical-camera";
    if(band=="SWIR")return "infrared-swir";
    if(band=="LWIR")return "infrared-lwir";
    return "infrared-mwir";
}

bool mountAvailable(const VoxelEquipmentSpec& spec,const VoxelSceneSnapshot& state) {
    if(spec.mountObject.empty())return true;
    const auto body=state.objects.find(spec.mountObject);
    return body!=state.objects.end()&&body->second.volume&&body->second.volume->occupiedCount()>0;
}
}

VoxelEquipmentConfig VoxelEquipmentConfig::open(const std::string& path){if(std::filesystem::file_size(path)>1024*1024)throw std::invalid_argument("equipment config exceeds 1 MiB");std::ifstream f(path);if(!f)throw std::invalid_argument("cannot open equipment config: "+path);return parse(json::parse(f),std::filesystem::absolute(path).string());}
VoxelEquipmentConfig VoxelEquipmentConfig::parse(const json& j,const std::string& path){
    fields(j,{"format","topic","visibility_m","equipment","platforms","state_connections","visual_only","navigation","depth_sounders"});if(j.at("format")!="voxel-equipment-v1")throw std::invalid_argument("unsupported equipment format");
    VoxelEquipmentConfig c;auto topic=std::filesystem::path(j.at("topic").get<std::string>());if(topic.is_relative())topic=std::filesystem::path(path).parent_path()/topic;c.topicPath=std::filesystem::absolute(topic).lexically_normal().string();c.visibilityM=number(j,"visibility_m",30000,100,1e6);
    c.visualOnly=j.value("visual_only",false);
    const auto& list=j.at("equipment");if(!list.is_array()||list.size()>16)throw std::invalid_argument("invalid equipment list size");std::set<uint32_t> ids;uint64_t pixels=0;
    for(const auto& item:list){fields(item,{"id","name","platform_id","platform_name","platform_kind","model_id","band","period_seconds","width","height","fov_deg","output","aperture","optics","detector","electronics","radar","visual"});VoxelEquipmentSpec s;
        s.equipment.equipment_id=s.sensor.id=integer(item,"id",0,1,UINT32_MAX);if(!ids.insert(s.sensor.id).second)throw std::invalid_argument("duplicate equipment id");
        s.equipment.name=item.at("name").get<std::string>();if(s.equipment.name.empty()||s.equipment.name.size()>128)throw std::invalid_argument("invalid equipment name");s.equipment.platform_id=integer(item,"platform_id",1,1,UINT32_MAX);s.equipment.platform_name=item.value("platform_name",std::string("多谱段观测站"));s.equipment.platform_kind=item.value("platform_kind",std::string("sensor_site"));
        const auto band=item.at("band").get<std::string>();
        const bool sar=band=="SAR",coherent=band=="RADAR"||sar;
        if(band!="TV"&&band!="SWIR"&&band!="MWIR"&&band!="LWIR"&&!coherent)throw std::invalid_argument("unsupported equipment band");
        s.equipment.type=sar?SensorType::SAR:band=="RADAR"?SensorType::RADAR:band=="TV"?SensorType::VIS:SensorType::IR;
        s.sensor.profile.band=BandSpec::make(band=="TV"?BandType::TV:band=="SWIR"?BandType::SWIR:band=="LWIR"?BandType::LWIR:BandType::MWIR);s.sensor.profile.name=s.equipment.name;
        s.sensor.period_seconds=number(item,"period_seconds",.1,.01,60);s.equipment.frame_rate=1/s.sensor.period_seconds;
        auto& p=s.sensor.profile;p.width=integer(item,"width",320,3,2048);p.height=integer(item,"height",240,3,2048);pixels+=uint64_t(p.width)*p.height;if(pixels>4'000'000)throw std::invalid_argument("equipment exceeds 4M total sensor pixels");p.fov_deg=number(item,"fov_deg",30,.1,150);
        const auto stage=item.value("output",std::string(band=="RADAR"?"P2":"P0"));
        if(stage!="P0"&&(sar||(coherent?stage!="P2":stage!="P4")))
            throw std::invalid_argument("imaging output must be P0/P4; pulse radar P0/P2; SAR acquisition P0");
        s.sensor.output=stage=="P0"?ProductStage::Scene:stage=="P2"?ProductStage::Focal:ProductStage::Image;
        s.sensor.echo_only=coherent&&stage=="P0";
        const auto& a=item.at("aperture");fields(a,{"position_m","look_at_m","up","mount_object","mount_id"});s.aperture.position=vector(a.at("position_m"));s.aperture.forward=vector(a.at("look_at_m"))-s.aperture.position;s.aperture.up=vector(a.value("up",json::array({0,1,0})));if(vsg::length(s.aperture.forward)<1e-8||vsg::length(vsg::cross(s.aperture.forward,s.aperture.up))<1e-8)throw std::invalid_argument("degenerate equipment aperture");s.aperture.forward=vsg::normalize(s.aperture.forward);s.aperture.up=vsg::normalize(s.aperture.up);s.mountObject=a.value("mount_object",std::string());
        s.mountId=a.value("mount_id",std::string());
        if(a.contains("mount_id")&&(s.mountId.empty()||s.mountId.size()>64||!s.mountObject.empty()))
            throw std::invalid_argument("mount_id requires a single named platform mount");
        if(item.contains("optics")){const auto& o=item.at("optics");fields(o,{"f_number","transmission","psf_sigma_px","vignetting"});p.optics.f_number=number(o,"f_number",4,.5,64);p.optics.transmission=number(o,"transmission",.85,0,1);p.optics.psf_sigma_px=number(o,"psf_sigma_px",.9,0,8);p.optics.vignetting=number(o,"vignetting",.15,0,1);}
        if(item.contains("detector")){const auto& d=item.at("detector");fields(d,{"integration_ms","pixel_pitch_um","qe","netd_mk","well_ke","read_noise_e","bits"});p.detector.integration_ms=number(d,"integration_ms",8,.001,s.sensor.period_seconds*1000);p.detector.pixel_pitch_um=number(d,"pixel_pitch_um",15,.1,100);p.detector.qe=number(d,"qe",.7,0,1);p.detector.netd_mk=number(d,"netd_mk",50,0,1e5);p.detector.well_ke=number(d,"well_ke",20000,1,1e9);p.detector.read_noise_e=number(d,"read_noise_e",80,0,1e6);p.detector.bits=integer(d,"bits",14,1,24);}
        if(item.contains("electronics")){const auto& e=item.at("electronics");fields(e,{"agc_low_pct","agc_high_pct","gamma"});p.electronics.agc_low_pct=number(e,"agc_low_pct",.002,0,.999);p.electronics.agc_high_pct=number(e,"agc_high_pct",.998,.001,1);if(p.electronics.agc_low_pct>=p.electronics.agc_high_pct)throw std::invalid_argument("AGC percentile order");p.electronics.gamma=number(e,"gamma",1,.1,8);}
        if(coherent){const auto& r=item.at("radar");fields(r,{"lambda_m","prf_hz","cpi_pulses","pulse_width_s","bandwidth_hz","range_min_m","range_max_m","n_fast","beam_width_rad","sample_rate_hz","transmit_power_w","receiver_noise_iq_rms","terrain_layer"});RadarChainParams q;q.width=p.width;q.height=p.height;q.lambda=number(r,"lambda_m",.03,.001,10);q.prf=number(r,"prf_hz",100,1,1e6);q.cpi_pulses=integer(r,"cpi_pulses",8,2,256);q.n_fast=integer(r,"n_fast",256,16,4096);if((q.cpi_pulses&(q.cpi_pulses-1))||(q.n_fast&(q.n_fast-1)))throw std::invalid_argument("radar FFT sizes must be powers of two");if(std::abs(q.cpi_pulses/q.prf-s.sensor.period_seconds)>1e-9)throw std::invalid_argument("this radar backend uses contiguous CPI; period_seconds must equal cpi_pulses/prf_hz");q.bandwidth_hz=number(r,"bandwidth_hz",30e6,1,1e10);q.sample_rate_hz=RadarSampling::make(q.bandwidth_hz,number(r,"sample_rate_hz",q.bandwidth_hz,1,1e10)).rate_hz;q.pulse_width_s=number(r,"pulse_width_s",1/q.sample_rate_hz,1e-10,1/q.prf);if(std::abs(q.pulse_width_s*q.sample_rate_hz-1)>1e-6)throw std::invalid_argument("this backend synthesizes impulse-equivalent echoes; pulse_width_s must equal 1/sample_rate_hz (long chirps are not generated)");q.range_min_m=number(r,"range_min_m",0,0,1e6);q.range_max_m=number(r,"range_max_m",1000,q.range_min_m+.001,1e6);if(q.range_max_m>q.range_min_m+(q.n_fast-1)*299792458./(2*q.sample_rate_hz))throw std::invalid_argument("radar range gate exceeds physical fast-time samples");q.beam_width_rad=number(r,"beam_width_rad",.1,.001,3.14);q.transmit_power_w=number(r,"transmit_power_w",1,0,1e12);q.receiver_noise_iq_rms=number(r,"receiver_noise_iq_rms",band=="RADAR"&&s.sensor.echo_only?.015:0.,0,1e6);if(2*q.range_min_m/RadarSampling::lightSpeed+q.n_fast/q.sample_rate_hz>1/q.prf)throw std::invalid_argument("radar receive window exceeds one PRI");q.terrain_footprint=r.value("terrain_layer",!sar);q.fov_h_deg=p.fov_deg;q.fov_v_deg=2*std::atan(std::tan(p.fov_deg*3.141592653589793/360)*p.height/p.width)*180/3.141592653589793;s.sensor.radar=q;s.equipment.range_max=q.range_max_m;}
        else if(item.contains("radar"))throw std::invalid_argument("radar parameters on imaging equipment");
        if(const auto visual=item.find("visual");visual!=item.end()) {
            fields(*visual,{"model_id","display_range_m","preview_period_seconds","phase_deg","display_horizontal_deg","display_vertical_deg"});
            VoxelEquipmentSpec::Visual options;
            options.model=visual->at("model_id").get<std::string>();
            if(options.model.empty()||options.model.size()>64)
                throw std::invalid_argument("invalid equipment visual model ID");
            options.rangeMetres=number(*visual,"display_range_m",100.,1.,50000.);
            options.previewPeriodSeconds=number(*visual,"preview_period_seconds",0.,0.,3600.);
            if(options.previewPeriodSeconds>0.&&options.previewPeriodSeconds<.1)
                throw std::invalid_argument("equipment preview period must be at least .1 s");
            options.phaseDegrees=number(*visual,"phase_deg",0.,-360.,360.);
            if(visual->contains("display_horizontal_deg"))options.horizontalDegrees=number(*visual,"display_horizontal_deg",30.,.1,150.);
            if(visual->contains("display_vertical_deg"))options.verticalDegrees=number(*visual,"display_vertical_deg",18.,.1,150.);
            s.visual=std::move(options);
        }
        const auto* standard=model_packages::equipmentModel(defaultEquipmentModel(band));
        const auto preferred=s.visual?model_packages::equipmentModel(s.visual->model):nullptr;
        const auto compatible=[&](const model_sdk::EquipmentDescriptor* model){
            return model&&model->family==standard->family&&(model->band.empty()||model->band==band);
        };
        s.equipment.model_id=item.value("model_id",std::string(compatible(preferred)?preferred->id:standard->id));
        const auto* descriptor=model_packages::equipmentModel(s.equipment.model_id);
        if(!compatible(descriptor))
            throw std::invalid_argument("equipment model is incompatible with its band: "+s.equipment.model_id);
        s.equipment.description=band+" · "+stage+(coherent?(s.sensor.echo_only?" · 原始复数回波":" · 复数距离-多普勒"):stage=="P0"?" · 物理辐亮度":" · 探测器图像");c.equipment.push_back(std::move(s));
    }
    const auto platforms=j.value("platforms",json::array());if(!platforms.is_array()||platforms.size()>64)throw std::invalid_argument("invalid equipment platform list");std::set<uint32_t> platformIds;std::set<std::string> objects;
    for(const auto& p:platforms){fields(p,{"id","object","name","kind","model_id","model_heading_deg","m1a2_display_rig","mounts"});VoxelEquipmentPlatform platform;platform.id=integer(p,"id",0,1,UINT32_MAX);platform.object=p.at("object").get<std::string>();platform.name=p.at("name").get<std::string>();platform.kind=p.at("kind").get<std::string>();platform.modelHeadingDeg=number(p,"model_heading_deg",0,-360,360);
        platform.modelId=p.value("model_id",std::string(p.contains("m1a2_display_rig")
            ? "m1a2":model_packages::platformModelForKind(platform.kind)));
        if(!model_packages::platformModel(platform.modelId))
            throw std::invalid_argument("unknown platform model: "+platform.modelId);
        if(p.contains("m1a2_display_rig")){
            if(platform.modelId!="m1a2")throw std::invalid_argument("M1A2 rig requires the M1A2 model");
            const auto value=p.at("m1a2_display_rig").get<std::string>();
            if(value.empty()||value.size()>1024)throw std::invalid_argument("invalid M1A2 display rig");
            auto rig=std::filesystem::path(value);
            if(rig.is_relative())rig=std::filesystem::path(path).parent_path()/rig;
            platform.displayRig=std::filesystem::absolute(rig).lexically_normal().string();
        }
        if(const auto mounts=p.find("mounts");mounts!=p.end()) {
            if(!mounts->is_object()||mounts->size()>32)throw std::invalid_argument("invalid platform mounts");
            for(const auto& [id,mount]:mounts->items()) {
                if(id.empty()||id.size()>64)throw std::invalid_argument("invalid platform mount ID");
                fields(mount,{"position_m","forward","up"});
                const auto p=vector(mount.at("position_m"));
                const auto forward=vector(mount.value("forward",json::array({0,0,1})));
                const auto up=vector(mount.value("up",json::array({0,1,0})));
                if(vsg::length(forward)<1e-8||vsg::length(vsg::cross(up,forward))<1e-8)
                    throw std::invalid_argument("degenerate platform mount");
                platform.mounts.emplace(id,mountFrame(p,forward,up));
            }
        }
        if(!platformIds.insert(platform.id).second||!objects.insert(platform.object).second||platform.object.empty()||platform.object.size()>128||platform.name.empty()||platform.name.size()>128||platform.kind.empty()||platform.kind.size()>64)throw std::invalid_argument("duplicate or invalid equipment platform");c.platforms.push_back(std::move(platform));}
    for(auto& equipment:c.equipment) {
        const auto platform=std::find_if(c.platforms.begin(),c.platforms.end(),[&](const auto& entry){
            return entry.id==equipment.equipment.platform_id;
        });
        if(platform!=c.platforms.end()&&
            model_packages::platformModel(platform->modelId)->family==model_sdk::PlatformFamily::Playback)
            throw std::invalid_argument("recorded playback does not host active sensors");
        if(!equipment.mountId.empty()){
            if(platform==c.platforms.end()||!platform->mounts.count(equipment.mountId))
                throw std::invalid_argument("equipment mount not declared on its platform: "+equipment.mountId);
            applyMount(equipment.aperture,platform->mounts.at(equipment.mountId));
            equipment.mountObject=platform->object;
        }
    }
    if(const auto navigation=j.find("navigation");navigation!=j.end()){
        if(!navigation->is_array()||navigation->size()>16)
            throw std::invalid_argument("invalid navigation equipment list");
        std::set<uint32_t> navigationIds,hostIds;
        for(const auto& entry:*navigation){
            fields(entry,{"id","name","model_id","platform_id"});
            VoxelNavigationSpec spec{integer(entry,"id",0,1,UINT32_MAX),integer(entry,"platform_id",0,1,UINT32_MAX),
                entry.at("name").get<std::string>()};
            const auto host=std::find_if(c.platforms.begin(),c.platforms.end(),[&](const auto& p){return p.id==spec.platformId;});
            if(entry.at("model_id")!="navigation-truth"||spec.name.empty()||spec.name.size()>128||
                ids.count(spec.id)||!navigationIds.insert(spec.id).second||!hostIds.insert(spec.platformId).second||
                host==c.platforms.end()||model_packages::platformModel(host->modelId)->family==model_sdk::PlatformFamily::Playback)
                throw std::invalid_argument("invalid navigation-truth model, ID or host");
            c.navigation.push_back(std::move(spec));
        }
    }
    if(const auto sounders=j.find("depth_sounders");sounders!=j.end()){
        if(!sounders->is_array()||sounders->size()>8)throw std::invalid_argument("invalid depth sounder list");
        for(const auto& entry:*sounders){
            fields(entry,{"id","name","model_id","platform_id","seabed_object","mount_position_m","period_seconds","range_m",
                "water_column_returns"});
            model_packages::DepthSounder sounder;
            auto& info=sounder.equipment;
            info.type=SensorType::SONAR;
            info.equipment_id=integer(entry,"id",0,1,UINT32_MAX);
            info.platform_id=integer(entry,"platform_id",0,1,UINT32_MAX);
            info.name=entry.at("name").get<std::string>();
            info.model_id=entry.at("model_id").get<std::string>();
            sounder.seabedObject=entry.at("seabed_object").get<std::string>();
            sounder.localPosition=vector(entry.at("mount_position_m"));
            sounder.periodSeconds=number(entry,"period_seconds",.4,.25,10.);
            sounder.rangeMetres=number(entry,"range_m",150.,1.,300.);
            sounder.waterColumnReturns=entry.value("water_column_returns",false);
            const auto host=std::find_if(c.platforms.begin(),c.platforms.end(),[&](const auto& p){return p.id==info.platform_id;});
            if(info.model_id!=model_packages::DepthSounder::id||info.name.empty()||info.name.size()>128||
                sounder.seabedObject.empty()||sounder.seabedObject.size()>128||host==c.platforms.end()||
                model_packages::platformModel(host->modelId)->family!=model_sdk::PlatformFamily::Surface||
                !ids.insert(info.equipment_id).second||std::any_of(c.navigation.begin(),c.navigation.end(),[&](const auto& nav){return nav.id==info.equipment_id;}))
                throw std::invalid_argument("invalid depth sounder model, ID or surface host");
            sounder.hostObject=host->object;
            info.platform_kind=host->kind;info.platform_name=host->name;
            info.frame_rate=float(1./sounder.periodSeconds);info.range_max=float(sounder.rangeMetres);
            info.description=sounder.waterColumnReturns?"单波束垂直测深 · 近距离水体回波":"单波束垂直测深 · 海床剖面";
            c.sounders.push_back(std::move(sounder));
        }
    }
    if(!c.visualOnly&&c.equipment.empty()&&c.sounders.empty())throw std::invalid_argument("active topic has no equipment");
    if(const auto connections=j.find("state_connections");connections!=j.end()) {
        using namespace model_sdk;
        if(!connections->is_array()||connections->size()>64)
            throw std::invalid_argument("invalid state connections");
        const auto endpoint=[&](const json& input){
            fields(input,{"kind","id"});
            const auto kind=input.at("kind").get<std::string>();
            const auto id=integer(input,"id",0,1,UINT32_MAX);
            if(kind=="platform"&&platformIds.count(id))return ModelEndpoint{InstanceKind::Platform,id};
            if(kind=="equipment"&&std::any_of(c.equipment.begin(),c.equipment.end(),[&](const auto& spec){return spec.sensor.id==id;}))return ModelEndpoint{InstanceKind::Equipment,id};
            throw std::invalid_argument("unknown model state endpoint");
        };
        std::set<std::tuple<InstanceKind,uint32_t,InstanceKind,uint32_t,StatePort>> unique;
        for(const auto& item:*connections){
            fields(item,{"source","destination","port"});
            const auto from=endpoint(item.at("source"));
            const auto to=endpoint(item.at("destination"));
            const auto port=item.at("port").get<std::string>();
            StatePort kind;
            if(port=="platform_motion"&&from.kind==InstanceKind::Platform)kind=StatePort::PlatformMotion;
            else if(port=="equipment_mount"&&from.kind==InstanceKind::Equipment)kind=StatePort::EquipmentMount;
            else throw std::invalid_argument("model state port does not belong to its source");
            if((from.kind==to.kind&&from.id==to.id)||!unique.emplace(from.kind,from.id,to.kind,to.id,kind).second)
                throw std::invalid_argument("duplicate or self-referencing state connection");
            c.connections.push_back({from,to,kind});
        }
    }
    return c;
}
VoxelEquipmentRuntime::VoxelEquipmentRuntime(VoxelScene& scene,VoxelEquipmentConfig config,vsg::Device* device,bool startOnline):VoxelEquipmentRuntime(readVoxelScene(scene),std::move(config),device,startOnline){}
VoxelEquipmentRuntime::VoxelEquipmentRuntime(VoxelSceneReader scene,VoxelEquipmentConfig config,vsg::Device* device,bool startOnline):scene_(std::move(scene)),config_(std::move(config)){
    if(config_.visualOnly)throw std::invalid_argument("visual-only topic has no measurement runtime");
    if(SimDataBroker::instance().isServerOnline())throw std::runtime_error("a local broker data source is already online");
    std::vector<GpuSensorSpec> specs;for(const auto& s:config_.equipment){if(EquipmentRegistry::instance().findById(s.sensor.id))throw std::invalid_argument("equipment id already registered");if(!s.mountObject.empty()&&!scene_.snapshot()->objects.count(s.mountObject))throw std::invalid_argument("equipment mount object is missing");specs.push_back(s.sensor);}
    for(const auto& p:config_.platforms)if(!scene_.snapshot()->objects.count(p.object))throw std::invalid_argument("equipment platform scene object is missing: "+p.object);
    sensors_=std::make_unique<VoxelSensorSession>(scene_,specs,device,"equipment-runtime");
    for(const auto& s:config_.equipment)SimDataBroker::instance().resetEquipmentChannel(s.sensor.id);SimDataBroker::instance().serverOnline(startOnline);for(const auto& s:config_.equipment)EquipmentRegistry::instance().registerEquipment(s.equipment);
}
VoxelEquipmentRuntime::~VoxelEquipmentRuntime(){sensors_.reset();for(const auto& s:config_.equipment){EquipmentRegistry::instance().unregisterEquipment(s.sensor.id);SimDataBroker::instance().unsubscribe(s.sensor.id);}SimDataBroker::instance().serverOnline(false);}
void VoxelEquipmentRuntime::advanceMeasurements(){
    auto begin=std::chrono::steady_clock::now();auto& broker=SimDataBroker::instance();auto& registry=EquipmentRegistry::instance();
    ControlCommand command;while(broker.pollControl(&command)){} // power state is acknowledged atomically by broker; no unimplemented gain/mode claims
    auto find=[&](uint32_t id)->const VoxelEquipmentSpec&{for(const auto& s:config_.equipment)if(s.sensor.id==id)return s;throw std::logic_error("unknown product equipment id");};
    auto aperture=[&](uint32_t id,double t){
        const auto& spec=find(id);
        if(spec.mountObject.empty())return spec.aperture;
        return mountedAperture(spec,*scene_.sample(t));
    };
    auto environment=[&](uint32_t id,double t,const BandSpec& band){auto state=scene_.sample(t);const auto a=aperture(id,t);rockyenv::EnvFrame frame;frame.model=simcore::geo::makeEnuToWorldMatrix(state->anchor);frame.cam_pos_enu=a.position;frame.cam_pos_world=frame.model*a.position;frame.time_sec=t;const auto utc=sceneUtc(state->utcOrigin,t);frame.year=utc.year;frame.month=utc.month;frame.day=utc.day;frame.hours_utc=utc.hours;frame.cloud_cover=0;frame.wind_speed=0;frame.visibility=config_.visibilityM;
        if(state->weather.enabled){const auto w=state->weather.sample(t);frame.cloud_cover=w.cover;frame.wind_speed=w.windSpeed;frame.visibility=w.visibility;frame.precipitation=std::min(1.,w.rain/50.);frame.cloud_base_m=w.cloudBase;frame.overcast=w.overcast;}
        EnvPhysicsService service;service.update(frame);EnvRequest request;request.band=band;request.sim_time=t;request.hours_utc=frame.hours_utc;const auto llh=simcore::geo::ecefToGeodetic(frame.cam_pos_world.x,frame.cam_pos_world.y,frame.cam_pos_world.z);request.lat=llh.x;request.lon=llh.y;request.alt=llh.z;return service.answerAtmosphere(request);};
    auto enabled=[&](uint32_t id){
        const auto info=registry.findById(id);
        return info&&info->powered&&broker.isSubscribed(id)&&mountAvailable(find(id),*scene_.snapshot());
    };
    sensors_->advanceTo(scene_.snapshot()->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product){const auto& s=find(product.sensor_id);if(!enabled(product.sensor_id))return;const uint64_t revision=sensors_->contentRevision();const auto stage=uint8_t(product.stage);const auto a=aperture(product.sensor_id,.5*(product.start_time+product.end_time));const auto localToWorld=simcore::geo::makeEnuToWorldMatrix(scene_.snapshot()->anchor);const auto world=localToWorld*a.position;const auto llh=simcore::geo::ecefToGeodetic(world.x,world.y,world.z);bool accepted=false;
        auto common=[&](auto& f){f.sensor_id=product.sensor_id;f.platform_id=s.equipment.platform_id;f.frame_id=product.frame_id;f.sim_time=product.end_time;f.timestamp_ns=uint64_t(std::llround(product.end_time*1e9));f.surface_revision=revision;f.stage=stage;f.latitude=llh.x;f.longitude=llh.y;f.altitude=llh.z;};
        if(s.equipment.type==SensorType::SAR){
            if(!product.echo_only||product.iq.size()!=size_t(product.width)*product.height)
                throw std::runtime_error("SAR echo product shape or stage mismatch");
            SarFrame f;common(f);const auto& r=*s.sensor.radar;
            f.range_samples=product.width;f.azimuth_samples=product.height;f.echo_data=std::move(product.iq);
            f.carrier_hz=product.carrier_hz;f.prf=product.prf;f.sample_rate_hz=product.sample_rate_hz;
            f.range_min=product.range_min_m;f.range_resolution=299792458./(2*r.bandwidth_hz);
            f.altitude_m=llh.z;accepted=broker.publishSar(f);
        }else if(s.sensor.radar){RadarFrame f;common(f);const auto& r=*s.sensor.radar;f.resize(product.width,product.height);if(product.iq.size()!=f.rd_map.size())throw std::runtime_error("radar product shape mismatch");f.rd_map=std::move(product.iq);f.carrier_hz=product.carrier_hz;f.prf=product.prf;f.coherent_pulses=r.cpi_pulses;f.sample_rate_hz=product.sample_rate_hz;f.range_min=product.range_min_m;f.range_max=r.range_max_m;f.range_resolution=299792458./(2*r.bandwidth_hz);f.velocity_resolution=r.lambda*r.prf/(2*r.cpi_pulses);f.pulse_width=r.pulse_width_s;f.bandwidth=r.bandwidth_hz;
            const auto ecefDirection=localToWorld*vsg::dvec4(a.forward,0.);
            const vsg::dvec3 dir{ecefDirection.x,ecefDirection.y,ecefDirection.z};
            const auto east=simcore::geo::geoEast(llh.x,llh.y);
            const auto north=vsg::cross(simcore::geo::geoUp(llh.x,llh.y),east);
            f.receiver=RadarFrame::Receiver{product.start_time,r.receiver_noise_iq_rms};
            const auto bearing=[&](const vsg::dvec3& d){
                return std::fmod(float(std::atan2(vsg::dot(d,east),vsg::dot(d,north))*180/3.141592653589793+360.),360.f);
            };
            f.beam_az_deg=bearing(dir);
            if(r.terrain_footprint){
                f.ground.emplace();
                f.ground->reserve(product.terrain_footprint.size());
                for(const auto& hit:product.terrain_footprint){
                    if(hit.w<=0.f)continue;
                    const auto delta=localToWorld*vsg::dvec4(hit.x,hit.y,hit.z,0.);
                    RadarFrame::GroundSample point{bearing({delta.x,delta.y,delta.z}),
                        float(vsg::length(vsg::dvec3{hit.x,hit.y,hit.z})),hit.w};
                    if(point.valid())f.ground->push_back(point);
                }
            }
            accepted=broker.publishRadar(f);
        }else{if(product.rgba.size()!=size_t(product.width)*product.height*4)throw std::runtime_error("image product shape mismatch");const auto& p=s.sensor.profile;
            if(s.equipment.type==SensorType::VIS){VisFrame f;common(f);f.resize(product.width,product.height);f.wavelength=.5*(p.band.lambda_min+p.band.lambda_max)*1e9;f.fov=p.fov_deg*3.141592653589793/180;f.exposure_time=p.detector.integration_ms*.001;for(size_t i=0;i<f.intensity.size();++i)f.intensity[i]=.2126f*product.rgba[4*i]+.7152f*product.rgba[4*i+1]+.0722f*product.rgba[4*i+2];accepted=broker.publishVis(f);
            }else{IrFrame f;common(f);f.resize(product.width,product.height);f.wavelength_min=p.band.lambda_min*1e6;f.wavelength_max=p.band.lambda_max*1e6;f.fov_horizontal=p.fov_deg*3.141592653589793/180;f.fov_vertical=2*std::atan(std::tan(f.fov_horizontal/2)*p.height/p.width);f.integration_time=p.detector.integration_ms*.001;f.netd=p.detector.netd_mk*.001;for(size_t i=0;i<f.radiance.size();++i)f.radiance[i]=product.rgba[4*i];accepted=broker.publishIr(f);}}
        if(!accepted)throw std::runtime_error("broker rejected voxel equipment product");++stats_.published;registry.setActive(product.sensor_id,true);
    },enabled);
    for(const auto& s:config_.equipment)if(!enabled(s.sensor.id))registry.setActive(s.sensor.id,false);
    stats_.rejectedWindows=sensors_->rejectedSceneWindows();stats_.sensorMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    stats_.gpu=sensors_->statistics();
}
nlohmann::json VoxelEquipmentRuntime::platformTelemetry()const{return voxelEquipmentPlatformTelemetry(config_,scene_.snapshot());}
nlohmann::json voxelEquipmentVisualTelemetry(const VoxelEquipmentConfig& config,const VoxelSceneSnapshot& state) {
    json items=json::array();
    constexpr double radians=3.14159265358979323846/180.;
    for(const auto& spec:config.equipment) {
        if(!spec.visual||!mountAvailable(spec,state))continue;
        const auto aperture=mountedAperture(spec,state);
        const auto& profile=spec.sensor.profile;
        const auto& visual=*spec.visual;
        const double vertical=2.*std::atan(std::tan(profile.fov_deg*radians*.5)*profile.height/profile.width)/radians;
        const double horizontal=visual.horizontalDegrees.value_or(profile.fov_deg);
        const double verticalAngle=visual.verticalDegrees.value_or(vertical);
        auto item=json{{"id",spec.sensor.id},{"model_id",visual.model},{"mount",spec.mountObject},{"mount_id",spec.mountId},
            {"origin",sdkVector(aperture.position)},{"forward",sdkVector(aperture.forward)},{"up",sdkVector(aperture.up)},
            {"horizontal_deg",horizontal},{"vertical_deg",verticalAngle},{"range_m",visual.rangeMetres},
            {"preview_period_seconds",visual.previewPeriodSeconds},{"phase_deg",visual.phaseDegrees}};
        if(const auto* model=model_packages::equipmentVisual(visual.model))
            item["preview_angle_deg"]=model->azimuthDegrees({horizontal,verticalAngle,visual.rangeMetres,
                visual.previewPeriodSeconds,visual.phaseDegrees},state.timeSeconds());
        items.push_back(std::move(item));
    }
    auto out=timeJson(state);
    out["version"]=1;
    out["items"]=std::move(items);
    return out;
}

nlohmann::json voxelEquipmentStateTelemetry(const VoxelEquipmentConfig& config,const VoxelSceneSnapshot& state) {
    auto result=timeJson(state);
    result["version"]=1;
    result["visual_only"]=config.visualOnly;
    auto& items=result["items"]=json::array();
    for(const auto& spec:config.equipment) {
        const auto* model=model_packages::equipmentModel(spec.equipment.model_id);
        if(!model)continue;
        const auto timing=model_packages::equipmentRuntime(model->family).timing(spec.sensor.period_seconds,
            spec.sensor.profile.detector.integration_ms*.001);
        const bool mounted=mountAvailable(spec,state);
        const auto info=EquipmentRegistry::instance().findById(spec.sensor.id);
        auto item=json{{"id",spec.sensor.id},{"platform_id",spec.equipment.platform_id},{"model_id",model->id},
            {"mount_id",spec.mountId},{"mount_available",mounted},{"period_seconds",timing.periodSeconds},
            {"integration_seconds",timing.integrationSeconds},{"powered",info&&info->powered}};
        item["state"]=!mounted?"unavailable":config.visualOnly?"preview":
            !info||!info->powered?"off":!SimDataBroker::instance().isSubscribed(spec.sensor.id)?"standby":
            info->active?"working":"waiting";
        if(mounted){
            const auto aperture=mountedAperture(spec,state);
            item["aperture"]={{"origin",sdkVector(aperture.position)},{"forward",sdkVector(aperture.forward)},
                {"up",sdkVector(aperture.up)}};
        }
        items.push_back(std::move(item));
    }
    return result;
}

nlohmann::json voxelModelStateMessages(const VoxelEquipmentConfig& config,const VoxelSceneSnapshot& source,
    const VoxelSceneSnapshot& current) {
    using namespace model_sdk;
    auto result=json::array();
    if(current.tick<=source.tick||current.tick-source.tick!=1)return result;
    const auto endpoint=[](const ModelEndpoint& item){
        return json{{"kind",item.kind==InstanceKind::Platform?"platform":"equipment"},{"id",item.id}};
    };
    for(const auto& link:config.connections) {
        const bool motion=link.port==StatePort::PlatformMotion;
        auto payload=json{{"available",false}};
        if(motion){
            const auto entry=std::find_if(config.platforms.begin(),config.platforms.end(),[&](const auto& p){
                return p.id==link.source.id;
            });
            if(entry==config.platforms.end())continue;
            const auto body=source.objects.find(entry->object);
            if(body!=source.objects.end()&&body->second.volume&&body->second.volume->occupiedCount()>0) {
                const auto& object=body->second;
                payload={{"available",true},{"position_m",sdkVector(object.pose(source.tick,source.fixedStep)*vsg::dvec3{})},
                    {"velocity_mps",sdkVector(object.velocity)}};
            }
        }else{
            const auto entry=std::find_if(config.equipment.begin(),config.equipment.end(),[&](const auto& e){
                return e.sensor.id==link.source.id;
            });
            if(entry==config.equipment.end())continue;
            payload["available"]=mountAvailable(*entry,source);
            payload["mount_id"]=entry->mountId;
        }
        result.push_back({{"source",endpoint(link.source)},{"destination",endpoint(link.destination)},
            {"port",motion?"platform_motion":"equipment_mount"},{"origin",timeJson(source)},
            {"delivery_tick",current.tick},{"valid_through_tick",current.tick},{"payload",std::move(payload)}});
    }
    return result;
}

nlohmann::json voxelEquipmentPlatformTelemetry(const VoxelEquipmentConfig& config,VoxelSceneSnapshotPtr state){
    json out=json::array();constexpr double radians=3.14159265358979323846/180;
    for(const auto& platform:config.platforms){const auto found=state->objects.find(platform.object);if(found==state->objects.end()||!found->second.volume->occupiedCount())continue;const auto& object=found->second;
        const auto pose=object.pose(state->tick,state->fixedStep);const auto m=pose*vsg::rotate(platform.modelHeadingDeg*radians,0.,1.,0.);const auto position=pose*vsg::dvec3{};const auto velocity=object.velocity;
        const double pitch=std::asin(std::clamp(-m[2][1],-1.,1.));const bool locked=std::abs(std::cos(pitch))<1e-9;
        const double yaw=locked?-std::atan2(-m[0][2],m[0][0]):-std::atan2(m[2][0],m[2][2]);const double roll=locked?0:-std::atan2(m[0][1],m[1][1]);
        const auto world=simcore::geo::makeEnuToWorldMatrix(state->anchor)*position;const auto llh=simcore::geo::ecefToGeodetic(world.x,world.y,world.z);
        const auto* model=model_packages::platformModel(platform.modelId.empty()?model_packages::platformModelForKind(platform.kind):platform.modelId);
        if(!model)continue;
        out.push_back({{"platform_id",platform.id},{"scene_object",platform.object},{"name",platform.name},{"kind",platform.kind},
            {"model_id",model->id},{"console",model->console},{"time",timeJson(*state)},
            {"position",{position.x,position.y,position.z}},{"velocity",{velocity.x,velocity.y,velocity.z}},{"speed",std::hypot(velocity.x,velocity.z)},{"speed_3d",vsg::length(velocity)},
            {"yaw",std::fmod(yaw/radians+360,360)},{"pitch",pitch/radians},{"roll",roll/radians},{"sim_time",state->timeSeconds()},{"latitude",llh.x},{"longitude",llh.y},{"altitude_m",llh.z},
            {"flight_control",{{"available",false},{"mode",object.body.motion},{"reason","voxel rigid-body pose telemetry only; no flight dynamics controller attached"}}}});
        auto& telemetry=out.back();
        const auto sample=model_packages::platformRuntime(model->family).sample({
            sceneTime(*state),sdkVector(position),sdkVector(velocity),pitch/radians,roll/radians});
        if(sample.surface) {
            const auto& water=*sample.surface;
            telemetry["surface_motion"]={{"speed_mps",water.speedMetresPerSecond},{"vertical_position_m",water.heightMetres},
                {"roll_deg",water.rollDegrees},{"pitch_deg",water.pitchDegrees}};
        }
        if(sample.air){
            const auto& air=*sample.air;
            telemetry["air_motion"]={{"horizontal_speed_mps",air.horizontalSpeedMetresPerSecond},
                {"vertical_speed_mps",air.verticalSpeedMetresPerSecond},{"near_stationary",air.nearStationary}};
        }
        if(model->family==model_sdk::PlatformFamily::Submersible&&state->ocean&&state->ocean->contains(position.x,position.z)){
            const double level=state->ocean->surface(position.x,position.z,state->timeSeconds()).height;
            telemetry["submersible_motion"]={{"depth_m",std::max(0.,level-position.y)},
                {"horizontal_speed_mps",std::hypot(velocity.x,velocity.z)},{"vertical_speed_mps",velocity.y}};
        }
        const auto navigation=std::find_if(config.navigation.begin(),config.navigation.end(),
            [&](const auto& entry){return entry.platformId==platform.id;});
        if(navigation!=config.navigation.end()){
            const auto& instrument=model_packages::navigationTruthEquipment();
            const auto fix=instrument.sample(sceneTime(*state),sdkVector(llh),sdkVector(velocity));
            telemetry["navigation"]={{"id",navigation->id},{"name",navigation->name},{"model_id","navigation-truth"},
                {"tick",fix.time.tick},{"period_seconds",instrument.timing(fix.time.stepSeconds,0.).periodSeconds},
                {"latitude_deg",fix.latitudeDegrees},{"longitude_deg",fix.longitudeDegrees},
                {"ellipsoid_height_m",fix.ellipsoidHeightMetres},{"velocity_scene_mps",fix.velocitySceneMetresPerSecond}};
        }
        for(const auto& [partId,part]:state->objects)if(part.rotor&&part.rotor->carrier==platform.object) {
            const auto& drive=*part.rotor;
            const double t=state->timeSeconds();
            telemetry["rotors"][partId]={{"rpm",drive.rpmAt(t)},
                {"angle_deg",std::remainder(drive.angleAt(t)*180./vsg::PI,360.)},
                {"tick",state->tick}};
        }
    }return out;
}
VoxelEquipmentRuntime::Statistics VoxelEquipmentRuntime::statistics()const{return stats_;}
}
