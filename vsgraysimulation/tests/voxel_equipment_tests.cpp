#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include "../src/earth/VoxelSensorMaterials.h"
#include "../src/earth/VoxelTerrain.h"
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/broker/SensorProductCodec.h>
#include <vsgraysim/layer3b_signal/SensorSignalProcessor.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <stdexcept>
using namespace vsgraysim;using namespace vsgraysim::earth;using json=nlohmann::json;
void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
int main()try{
    auto base=VoxelEquipmentConfig::open("config/equipment/taipei-multispectral.vequipment.json");
    json object=json::parse(R"({
        "id":"target",
        "source":{"type":"box","dimensions":[4,1,1],"origin_m":[-2,0,-0.5],"cell_size_m":1},
        "physics":{"motion":"prescribed","fracture":true,"support":"none"},
        "material":{"temperature_k":300,"color_rgba":[1,1,1,1],"radar":{"dielectric_real":6,"sigma0_db":-10,"rcs_scale":1}},
        "sensor_materials":{
            "palette":[
                {"name":"cool","temperature_k":300,"rho_tv":0.1,"eps_mwir":0.6,"eps_lwir":0.8},
                {"name":"hot transparent radar","temperature_k":500,"rho_tv":0.9,"eps_mwir":0.95,"eps_lwir":0.7,"radar":{"dielectric_real":6,"sigma0_db":-10,"rcs_scale":0}}
            ],
            "regions":[{"min_m":[0,-1,-1],"max_m":[3,2,1],"material":1}]
        }
    })");
    json doc={{"format","voxel-scene-v1"},{"fixed_step_seconds",.02},{"objects",{object}}};doc["geo_anchor"]={{"lat",25.078},{"lon",121.772},{"alt",0},{"heading_deg",27}};doc["utc_origin"]={{"year",2026},{"month",9},{"day",26},{"hours_utc",4}};auto scene=VoxelScene::load(doc);
    auto field=scene->snapshot()->objects.at("target").sensorMaterials;require(field&&field->at({-.5,.5,0}).at("temperature_k")==300&&field->at({.5,.5,0}).at("temperature_k")==500,"per-cell material region assignment");
    auto restored=VoxelScene::load(scene->checkpoint());require(restored->checkpoint()==scene->checkpoint(),"spectral material checkpoint changed values");
    auto invalid=doc;invalid["objects"][0]["sensor_materials"]["cell_runs"]={{0,5,0}};bool rejected=false;try{VoxelScene::load(invalid);}catch(...){rejected=true;}require(rejected,"out-of-bounds spectral run accepted");
    json invalidEquipment;std::ifstream("config/equipment/taipei-multispectral.vequipment.json")>>invalidEquipment;
    invalidEquipment["equipment"][3]["radar"]["pulse_width_s"]=1e-6;rejected=false;try{VoxelEquipmentConfig::parse(invalidEquipment);}catch(...){rejected=true;}require(rejected,"unsupported long chirp was silently accepted as physical RD");
    invalidEquipment["equipment"][3]["radar"].erase("pulse_width_s");invalidEquipment["equipment"][3]["period_seconds"]=.1;rejected=false;try{VoxelEquipmentConfig::parse(invalidEquipment);}catch(...){rejected=true;}require(rejected,"radar configured cadence disagrees with contiguous CPI");
    base.platforms={{72,"target","真实体素位姿","aircraft",0}};base.equipment.clear();auto parsed=VoxelEquipmentConfig::open("config/equipment/taipei-multispectral.vequipment.json");
    for(size_t i=0;i<6;++i){auto s=parsed.equipment[i<2?1:i<4?0:3];s.equipment.equipment_id=s.sensor.id=7201+i;s.equipment.name="material probe "+std::to_string(i);s.sensor.period_seconds=.1;s.equipment.frame_rate=10;s.sensor.profile.width=9;s.sensor.profile.height=7;s.sensor.profile.fov_deg=4;s.sensor.profile.detector.integration_ms=5;s.aperture.position={i%2?.5:-.5,8,0};s.aperture.forward={0,-1,0};s.aperture.up={0,0,-1};if(s.sensor.radar){auto& r=*s.sensor.radar;r.width=9;r.height=7;r.n_fast=64;s.aperture.forward={.01,-1,0};r.pulse_width_s=1/r.sample_rate_hz;r.cpi_pulses=2;r.prf=100;s.sensor.period_seconds=.02;s.equipment.frame_rate=50;r.range_max_m=20;r.fov_h_deg=4;r.fov_v_deg=3;r.beam_width_rad=.3;}base.equipment.push_back(s);}
    const auto suite=VoxelEquipmentConfig::open("config/equipment/taipei-receiver-suite.vequipment.json");
    require(suite.equipment[3].sensor.echo_only&&suite.equipment[3].equipment.type==SensorType::RADAR&&
            suite.equipment[6].sensor.echo_only&&suite.equipment[6].equipment.type==SensorType::SAR,"receiver suite lost its raw echo configuration");
    auto raw=base.equipment.at(4);
    raw.sensor.output=rockyenv::sensor::ProductStage::Scene;raw.sensor.echo_only=true;
    raw.equipment.equipment_id=raw.sensor.id=7207;base.equipment.push_back(raw);
    raw.equipment.equipment_id=raw.sensor.id=7208;raw.equipment.type=SensorType::SAR;base.equipment.push_back(raw);
    // Settling transports material with cells rather than leaving heat at old coordinates.
    auto column=VoxelTerrain::fromOccupancy({0,0,0},1,1,3,1,{true,false,true});
    auto columnField=VoxelSensorMaterials::load({{"palette",{{{"temperature_k",300}},{{"temperature_k",600}}}},{"regions",{{{"min_m",{0,2,0}},{"max_m",{1,3,1}},{"material",1}}}}},*column,normalizeVoxelMaterial(json::object()));
    auto settled=columnField->settled(*column);require(settled->at({.5,1.5,.5}).at("temperature_k")==600&&columnField->at({.5,1.5,.5}).at("temperature_k")==300,"settling lost material or mutated old snapshot");
    auto& broker=SimDataBroker::instance();VoxelEquipmentRuntime runtime(*scene,base);auto poseTelemetry=runtime.platformTelemetry();require(poseTelemetry.size()==1&&poseTelemetry[0]["position"][1]==0&&poseTelemetry[0]["speed"]==0&&!poseTelemetry[0].contains("engine")&&!poseTelemetry[0]["flight_control"]["available"].get<bool>(),"voxel cockpit telemetry fabricated dynamics or instrument values");for(const auto& s:base.equipment)require(broker.subscribe(s.sensor.id),"equipment subscription failed");scene->setPaused(false);
    for(int i=1;i<=10;++i){scene->advanceTo(i*.02,1);runtime.advanceMeasurements();}
    IrFrame cool,hot;VisFrame dark,bright;RadarFrame scatter,transparent;
    require(broker.latestIr(7201,&cool)&&broker.latestIr(7202,&hot),"independent IR equipment routing");require(cool.sensor_id==7201&&hot.sensor_id==7202&&hot.wavelength_min>=3&&hot.wavelength_max<6&&hot.stage==0,"IR metadata/physical stage");
    const auto centre=size_t(3*9+4);std::cout<<"MWIR material probes "<<cool.radiance[centre]<<" / "<<hot.radiance[centre]<<std::endl;require(hot.radiance[centre]>cool.radiance[centre]*3,"hot cell did not change actual GPU MWIR radiance");
    require(broker.latestVis(7203,&dark)&&broker.latestVis(7204,&bright),"VIS products missing");std::cout<<"optical probes "<<dark.intensity[centre]<<" / "<<bright.intensity[centre]<<" thermal "<<cool.radiance[centre]<<" / "<<hot.radiance[centre]<<std::endl;require(bright.intensity[centre]>dark.intensity[centre]*3,"per-cell optical reflectance ignored by actual GPU data");
    require(broker.latestRadar(7205,&scatter)&&broker.latestRadar(7206,&transparent),"independent radar equipment routing");double on=0,off=0;for(auto p:scatter.rd_map)on+=std::norm(p);for(auto p:transparent.rd_map)off+=std::norm(p);std::cout<<"RCS probes "<<on<<" / "<<off<<std::endl;require(on>0&&off<on*1e-8,"zero relative RCS failed to suppress actual GPU radar return");
    size_t peak=0;double powerPeak=-1;for(uint32_t col=0;col<scatter.range_bins;++col){double power=0;for(uint32_t row=0;row<scatter.doppler_bins;++row)power+=std::norm(scatter.rd_map[row*scatter.range_bins+col]);if(power>powerPeak){powerPeak=power;peak=col;}}
    require(std::abs(scatter.rangeAtBin(uint32_t(peak))-7)<2.6,"radar peak disagrees with canonical 7m surface range; compression waveform mismatch");require(scatter.sample_rate_hz==30e6&&scatter.range_max==20&&scatter.coherent_pulses==2&&scatter.stage==2,"radar sample clock/CPI metadata lost");
    RadarFrame echo;SarFrame sar;require(broker.latestRadar(7207,&echo)&&broker.latestSar(7208,&sar)&&echo.stage==0&&sar.stage==0,
                                        "separate raw radar and SAR channels did not publish");
    require(std::abs(echo.beam_az_deg-117)<.01,"beam azimuth ignored the scene geodetic heading");
    require(sar.echo_data.size()==echo.rd_map.size()&&sar.sample_rate_hz==echo.sample_rate_hz&&sar.range_samples==echo.range_bins,
            "independent acquisitions of the static test block disagree or lost sampling metadata");
    double echoError=0;
    for(size_t n=0;n<sar.echo_data.size();++n)echoError+=std::norm(sar.echo_data[n]-echo.rd_map[n]);
    require(echoError/on<1e-8,"radar and SAR raw echo acquisition differs for identical configured apertures");
    const auto spectrum=slowTimeSpectrum(echo.rd_map,echo.range_bins,echo.doppler_bins);
    double error=0;for(size_t n=0;n<spectrum.size();++n)error+=std::norm(spectrum[n]-scatter.rd_map[n]);
    require(error/on<1e-8,"receiver slow-time spectrum differs from legacy GPU RD for the impulse model");
    for(const wire::SensorProduct& source:std::vector<wire::SensorProduct>{echo,sar}) {
        const auto bytes=wire::encodeSensorProduct(source);wire::SensorProduct decoded;
        require(wire::decodeSensorProduct(bytes.data(),bytes.size(),decoded)&&wire::encodeSensorProduct(decoded)==bytes,
                "raw echo metadata or I/Q changed in the worker transport");
    }
    scene->setPaused(true);auto scaled=object.at("sensor_materials");scaled["palette"][0]["radar"]={{"rcs_scale",.25}};
    scene->edit({{{"kind","sensor_materials"},{"object","target"},{"sensor_materials",scaled}}},scene->snapshot()->revision);field=scene->snapshot()->objects.at("target").sensorMaterials;
    require(field->palette()[0]["radar"]["sigma0_db"]==-10,"partial radar palette override lost parent parameters");scene->setPaused(false);
    const auto before=broker.channelStats(7201).published_frames;ControlCommand power;power.equipment_id=7201;power.type=ControlCommand::Type::PowerOff;broker.sendControl(power);for(int i=11;i<=20;++i){scene->advanceTo(i*.02,1);runtime.advanceMeasurements();}require(broker.channelStats(7201).published_frames==before,"power-off channel continued acquisition");require(broker.channelStats(7202).published_frames>before,"power-off blocked unrelated equipment");power.type=ControlCommand::Type::PowerOn;broker.sendControl(power);
    require(broker.latestRadar(7205,&scatter),"scaled radar product missing");double scaledPower=0;for(auto p:scatter.rd_map)scaledPower+=std::norm(p);require(std::abs(scaledPower/on-.25)<.002,"relative RCS power scale did not affect actual GPU RD energy");
    scene->schedule({{"kind","damage"},{"object","target"},{"tick",21},{"space","object"},{"point_m",{-.5,.5,0}},{"radius_m",.6}});
    for(int i=21;i<=30;++i){scene->advanceTo(i*.02,1);runtime.advanceMeasurements();}
    require(broker.latestIr(7201,&cool)&&cool.surface_revision==scene->snapshot()->contentRevision,"damage measurement revision mismatch");require(cool.radiance[centre]<hot.radiance[centre],"damage did not remove cold surface");
    for(const auto& [id,piece]:scene->snapshot()->objects)require(piece.sensorMaterials&&piece.sensorMaterials->encode()==field->encode(),"fragment lost original material chart");
    restored=VoxelScene::load(scene->checkpoint());require(restored->checkpoint()==scene->checkpoint(),"damaged material chart checkpoint mismatch");
    std::cout<<"PASS: actual Vulkan per-cell optical/thermal/radar materials, checkpoints, damage inheritance, independent broker channels, power and cadence; MWIR "<<hot.radiance[centre]<<", radar powers "<<on<<" / "<<off<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
