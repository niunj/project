#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include "VoxelTerrain.h"
#include <vsgraysim/broker/SimDataBroker.h>
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
namespace vsgraysim::earth {
using namespace rockyenv::sensor;using json=nlohmann::json;
namespace {
void fields(const json& j,std::initializer_list<const char*> allowed){if(!j.is_object())throw std::invalid_argument("equipment configuration must be an object");for(const auto& f:j.items()){bool found=false;for(auto a:allowed)found|=f.key()==a;if(!found)throw std::invalid_argument("unknown equipment field: "+f.key());}}
double number(const json& j,const char* k,double fallback,double low,double high){double n=j.value(k,fallback);if(!std::isfinite(n)||n<low||n>high)throw std::invalid_argument(std::string("invalid equipment ")+k);return n;}
uint32_t integer(const json& j,const char* k,uint32_t fallback,uint32_t low,uint32_t high){const auto v=j.value(k,json(fallback));if(!v.is_number_integer()||v.get<double>()<low||v.get<double>()>high)throw std::invalid_argument(std::string("invalid equipment integer ")+k);return v.get<uint32_t>();}
vsg::dvec3 vector(const json& v){if(!v.is_array()||v.size()!=3)throw std::invalid_argument("equipment vector requires three coordinates");vsg::dvec3 p(v[0].get<double>(),v[1].get<double>(),v[2].get<double>());if(!std::isfinite(vsg::length(p)))throw std::invalid_argument("nonfinite equipment vector");return p;}
}
VoxelEquipmentConfig VoxelEquipmentConfig::open(const std::string& path){if(std::filesystem::file_size(path)>1024*1024)throw std::invalid_argument("equipment config exceeds 1 MiB");std::ifstream f(path);if(!f)throw std::invalid_argument("cannot open equipment config: "+path);return parse(json::parse(f),std::filesystem::absolute(path).string());}
VoxelEquipmentConfig VoxelEquipmentConfig::parse(const json& j,const std::string& path){
    fields(j,{"format","topic","visibility_m","equipment","platforms"});if(j.at("format")!="voxel-equipment-v1")throw std::invalid_argument("unsupported equipment format");
    VoxelEquipmentConfig c;auto topic=std::filesystem::path(j.at("topic").get<std::string>());if(topic.is_relative())topic=std::filesystem::path(path).parent_path()/topic;c.topicPath=std::filesystem::absolute(topic).lexically_normal().string();c.visibilityM=number(j,"visibility_m",30000,100,1e6);
    const auto& list=j.at("equipment");if(!list.is_array()||list.empty()||list.size()>16)throw std::invalid_argument("equipment list must contain 1..16 sensors");std::set<uint32_t> ids;uint64_t pixels=0;
    for(const auto& item:list){fields(item,{"id","name","platform_id","platform_name","platform_kind","band","period_seconds","width","height","fov_deg","output","aperture","optics","detector","electronics","radar"});VoxelEquipmentSpec s;
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
        const auto& a=item.at("aperture");fields(a,{"position_m","look_at_m","up","mount_object"});s.aperture.position=vector(a.at("position_m"));s.aperture.forward=vector(a.at("look_at_m"))-s.aperture.position;s.aperture.up=vector(a.value("up",json::array({0,1,0})));if(vsg::length(s.aperture.forward)<1e-8||vsg::length(vsg::cross(s.aperture.forward,s.aperture.up))<1e-8)throw std::invalid_argument("degenerate equipment aperture");s.aperture.forward=vsg::normalize(s.aperture.forward);s.aperture.up=vsg::normalize(s.aperture.up);s.mountObject=a.value("mount_object",std::string());
        if(item.contains("optics")){const auto& o=item.at("optics");fields(o,{"f_number","transmission","psf_sigma_px","vignetting"});p.optics.f_number=number(o,"f_number",4,.5,64);p.optics.transmission=number(o,"transmission",.85,0,1);p.optics.psf_sigma_px=number(o,"psf_sigma_px",.9,0,8);p.optics.vignetting=number(o,"vignetting",.15,0,1);}
        if(item.contains("detector")){const auto& d=item.at("detector");fields(d,{"integration_ms","pixel_pitch_um","qe","netd_mk","well_ke","read_noise_e","bits"});p.detector.integration_ms=number(d,"integration_ms",8,.001,s.sensor.period_seconds*1000);p.detector.pixel_pitch_um=number(d,"pixel_pitch_um",15,.1,100);p.detector.qe=number(d,"qe",.7,0,1);p.detector.netd_mk=number(d,"netd_mk",50,0,1e5);p.detector.well_ke=number(d,"well_ke",20000,1,1e9);p.detector.read_noise_e=number(d,"read_noise_e",80,0,1e6);p.detector.bits=integer(d,"bits",14,1,24);}
        if(item.contains("electronics")){const auto& e=item.at("electronics");fields(e,{"agc_low_pct","agc_high_pct","gamma"});p.electronics.agc_low_pct=number(e,"agc_low_pct",.002,0,.999);p.electronics.agc_high_pct=number(e,"agc_high_pct",.998,.001,1);if(p.electronics.agc_low_pct>=p.electronics.agc_high_pct)throw std::invalid_argument("AGC percentile order");p.electronics.gamma=number(e,"gamma",1,.1,8);}
        if(coherent){const auto& r=item.at("radar");fields(r,{"lambda_m","prf_hz","cpi_pulses","pulse_width_s","bandwidth_hz","range_min_m","range_max_m","n_fast","beam_width_rad","sample_rate_hz","transmit_power_w","receiver_noise_iq_rms","terrain_layer"});RadarChainParams q;q.width=p.width;q.height=p.height;q.lambda=number(r,"lambda_m",.03,.001,10);q.prf=number(r,"prf_hz",100,1,1e6);q.cpi_pulses=integer(r,"cpi_pulses",8,2,256);q.n_fast=integer(r,"n_fast",256,16,4096);if((q.cpi_pulses&(q.cpi_pulses-1))||(q.n_fast&(q.n_fast-1)))throw std::invalid_argument("radar FFT sizes must be powers of two");if(std::abs(q.cpi_pulses/q.prf-s.sensor.period_seconds)>1e-9)throw std::invalid_argument("this radar backend uses contiguous CPI; period_seconds must equal cpi_pulses/prf_hz");q.bandwidth_hz=number(r,"bandwidth_hz",30e6,1,1e10);q.sample_rate_hz=RadarSampling::make(q.bandwidth_hz,number(r,"sample_rate_hz",q.bandwidth_hz,1,1e10)).rate_hz;q.pulse_width_s=number(r,"pulse_width_s",1/q.sample_rate_hz,1e-10,1/q.prf);if(std::abs(q.pulse_width_s*q.sample_rate_hz-1)>1e-6)throw std::invalid_argument("this backend synthesizes impulse-equivalent echoes; pulse_width_s must equal 1/sample_rate_hz (long chirps are not generated)");q.range_min_m=number(r,"range_min_m",0,0,1e6);q.range_max_m=number(r,"range_max_m",1000,q.range_min_m+.001,1e6);if(q.range_max_m>q.range_min_m+(q.n_fast-1)*299792458./(2*q.sample_rate_hz))throw std::invalid_argument("radar range gate exceeds physical fast-time samples");q.beam_width_rad=number(r,"beam_width_rad",.1,.001,3.14);q.transmit_power_w=number(r,"transmit_power_w",1,0,1e12);q.receiver_noise_iq_rms=number(r,"receiver_noise_iq_rms",band=="RADAR"&&s.sensor.echo_only?.015:0.,0,1e6);if(2*q.range_min_m/RadarSampling::lightSpeed+q.n_fast/q.sample_rate_hz>1/q.prf)throw std::invalid_argument("radar receive window exceeds one PRI");q.terrain_footprint=r.value("terrain_layer",!sar);q.fov_h_deg=p.fov_deg;q.fov_v_deg=2*std::atan(std::tan(p.fov_deg*3.141592653589793/360)*p.height/p.width)*180/3.141592653589793;s.sensor.radar=q;s.equipment.range_max=q.range_max_m;}
        else if(item.contains("radar"))throw std::invalid_argument("radar parameters on imaging equipment");
        s.equipment.description=band+" · "+stage+(coherent?(s.sensor.echo_only?" · 原始复数回波":" · 复数距离-多普勒"):stage=="P0"?" · 物理辐亮度":" · 探测器图像");c.equipment.push_back(std::move(s));
    }
    const auto platforms=j.value("platforms",json::array());if(!platforms.is_array()||platforms.size()>64)throw std::invalid_argument("invalid equipment platform list");std::set<uint32_t> platformIds;std::set<std::string> objects;
    for(const auto& p:platforms){fields(p,{"id","object","name","kind","model_heading_deg"});VoxelEquipmentPlatform platform;platform.id=integer(p,"id",0,1,UINT32_MAX);platform.object=p.at("object").get<std::string>();platform.name=p.at("name").get<std::string>();platform.kind=p.at("kind").get<std::string>();platform.modelHeadingDeg=number(p,"model_heading_deg",0,-360,360);
        if(!platformIds.insert(platform.id).second||!objects.insert(platform.object).second||platform.object.empty()||platform.object.size()>128||platform.name.empty()||platform.name.size()>128||platform.kind.empty()||platform.kind.size()>64)throw std::invalid_argument("duplicate or invalid equipment platform");c.platforms.push_back(std::move(platform));}
    return c;
}
VoxelEquipmentRuntime::VoxelEquipmentRuntime(VoxelScene& scene,VoxelEquipmentConfig config,vsg::Device* device,bool startOnline):VoxelEquipmentRuntime(readVoxelScene(scene),std::move(config),device,startOnline){}
VoxelEquipmentRuntime::VoxelEquipmentRuntime(VoxelSceneReader scene,VoxelEquipmentConfig config,vsg::Device* device,bool startOnline):scene_(std::move(scene)),config_(std::move(config)){
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
    auto aperture=[&](uint32_t id,double t){const auto& s=find(id);auto a=s.aperture;if(!s.mountObject.empty()){auto state=scene_.sample(t);const auto& o=state->objects.at(s.mountObject);auto m=o.pose(state->tick,state->fixedStep);a.position=m*a.position;auto d=m*vsg::dvec4(a.forward,0),u=m*vsg::dvec4(a.up,0);a.forward={d.x,d.y,d.z};a.up={u.x,u.y,u.z};}return a;};
    auto environment=[&](uint32_t id,double t,const BandSpec& band){auto state=scene_.sample(t);const auto a=aperture(id,t);rockyenv::EnvFrame frame;frame.model=simcore::geo::makeEnuToWorldMatrix(state->anchor);frame.cam_pos_enu=a.position;frame.cam_pos_world=frame.model*a.position;frame.time_sec=t;const auto utc=sceneUtc(state->utcOrigin,t);frame.year=utc.year;frame.month=utc.month;frame.day=utc.day;frame.hours_utc=utc.hours;frame.cloud_cover=0;frame.wind_speed=0;frame.visibility=config_.visibilityM;
        if(state->weather.enabled){const auto w=state->weather.sample(t);frame.cloud_cover=w.cover;frame.wind_speed=w.windSpeed;frame.visibility=w.visibility;frame.precipitation=std::min(1.,w.rain/50.);frame.cloud_base_m=w.cloudBase;frame.overcast=w.overcast;}
        EnvPhysicsService service;service.update(frame);EnvRequest request;request.band=band;request.sim_time=t;request.hours_utc=frame.hours_utc;const auto llh=simcore::geo::ecefToGeodetic(frame.cam_pos_world.x,frame.cam_pos_world.y,frame.cam_pos_world.z);request.lat=llh.x;request.lon=llh.y;request.alt=llh.z;return service.answerAtmosphere(request);};
    auto enabled=[&](uint32_t id){const auto info=registry.findById(id);const auto& mount=find(id).mountObject;if(!mount.empty()){const auto state=scene_.snapshot();const auto o=state->objects.find(mount);if(o==state->objects.end()||!o->second.volume->occupiedCount())return false;}return info&&info->powered&&broker.isSubscribed(id);};
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
nlohmann::json voxelEquipmentPlatformTelemetry(const VoxelEquipmentConfig& config,VoxelSceneSnapshotPtr state){
    json out=json::array();constexpr double radians=3.14159265358979323846/180;
    for(const auto& platform:config.platforms){const auto found=state->objects.find(platform.object);if(found==state->objects.end()||!found->second.volume->occupiedCount())continue;const auto& object=found->second;
        const auto pose=object.pose(state->tick,state->fixedStep);const auto m=pose*vsg::rotate(platform.modelHeadingDeg*radians,0.,1.,0.);const auto position=pose*vsg::dvec3{};const auto velocity=object.velocity;
        const double pitch=std::asin(std::clamp(-m[2][1],-1.,1.));const bool locked=std::abs(std::cos(pitch))<1e-9;
        const double yaw=locked?-std::atan2(-m[0][2],m[0][0]):-std::atan2(m[2][0],m[2][2]);const double roll=locked?0:-std::atan2(m[0][1],m[1][1]);
        const auto world=simcore::geo::makeEnuToWorldMatrix(state->anchor)*position;const auto llh=simcore::geo::ecefToGeodetic(world.x,world.y,world.z);
        out.push_back({{"platform_id",platform.id},{"name",platform.name},{"kind",platform.kind},{"position",{position.x,position.y,position.z}},{"velocity",{velocity.x,velocity.y,velocity.z}},{"speed",std::hypot(velocity.x,velocity.z)},{"speed_3d",vsg::length(velocity)},
            {"yaw",std::fmod(yaw/radians+360,360)},{"pitch",pitch/radians},{"roll",roll/radians},{"sim_time",state->timeSeconds()},{"latitude",llh.x},{"longitude",llh.y},{"altitude_m",llh.z},
            {"flight_control",{{"available",false},{"mode",object.body.motion},{"reason","voxel rigid-body pose telemetry only; no flight dynamics controller attached"}}}});
    }return out;
}
VoxelEquipmentRuntime::Statistics VoxelEquipmentRuntime::statistics()const{return stats_;}
}
