#include <vsgraysim/layer2_rt/SimulationGpuAdapter.h>
#include <vsgraysim/Simulation.h>
#include <rocky_env/GpuSensorRuntime.h>
#include <rocky_env/SensorInstance.h>
#include <rocky_env/EnvPhysicsService.h>
#include <rocky_env/EnvApi.h>
#include <simcore/UtcEpoch.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <map>
#include <tuple>
#include <array>

namespace vsgraysim {
namespace gpu=rockyenv::sensor;
struct SimulationGpuAdapter::Impl {
    Simulation& simulation;
    PhysParamManagerPtr parameters;
    nlohmann::json scenario;
    gpu::GpuSensorRuntime runtime;
    gpu::EnvPhysicsService environment;
    std::vector<std::string> bodies;
    std::vector<double> grounds;
    std::vector<std::tuple<std::size_t,std::string,std::size_t>> topology;
    std::map<uint32_t,SensorType> types;
    std::map<uint32_t,std::array<double,3>> footprints;
    uint64_t revision=0, surface_revision=0, object_revision=0;
    Impl(Simulation& s,PhysParamManagerPtr p,const nlohmann::json& j):simulation(s),parameters(std::move(p)),scenario(j) {}
    auto currentTopology() const {
        std::vector<std::tuple<std::size_t,std::string,std::size_t>> result;
        const auto physics=simulation.physics();
        for(std::size_t i=0;i<physics->bodyCount();++i) if(physics->bodyActive(i))
            result.emplace_back(i,physics->bodyId(i),physics->bodyLocalTriangles(i).size());
        return result;
    }
    struct BodyGeometry {
        std::string id;
        std::vector<Vec3> triangles;
        vsg::dmat4 transform;
    };
    std::shared_ptr<const std::vector<BodyGeometry>> terrain_inputs;
    uint64_t input_geometry_revision=0;
    PhysParamManagerPtr terrain_parameters;
    std::vector<BodyGeometry> captureBodies(bool replaceEarth) const {
        auto physics=simulation.physics();
        std::vector<BodyGeometry> input;
        for(std::size_t i=0;i<physics->bodyCount();++i) if(physics->bodyActive(i)) {
            const auto& triangles=physics->bodyLocalTriangles(i);
            if(triangles.empty()) continue;
            auto id=physics->bodyId(i);
            input.push_back({id,{},physics->transform(id)});
            if(!replaceEarth || id!="__earth_surface") input.back().triangles=triangles;
        }
        return input;
    }
    std::vector<gpu::SceneInstance> geometry(std::vector<gpu::RadarMaterial>& materials) {
        grounds=simulation.physics()->groundPlanes();
        topology=currentTopology();
        object_revision=simulation.physics()->objectGeometryRevision();
        surface_revision=simulation.earthSurface()?simulation.earthSurface()->revision:0;
        revision=parameters->revision();
        return buildGeometry(captureBodies(false),materials,nullptr);
    }
    std::vector<gpu::SceneInstance> buildGeometry(const std::vector<BodyGeometry>& input,
            std::vector<gpu::RadarMaterial>& materials,const earth::EarthSurfaceSnapshot* candidate) {
        bodies.clear();
        materials.emplace_back(); materials[0].emissivity=.92;
        std::map<const GeoUnitPhysParams*,uint32_t> material_indices;
        std::vector<gpu::SceneInstance> result;
        auto makeMesh=[&](const std::vector<Vec3>& triangles,const std::string& id) {
            auto pos=vsg::vec3Array::create(triangles.size());
            auto props=vsg::vec2Array::create(triangles.size());
            auto mat=vsg::uintArray::create(triangles.size());
            auto uv=vsg::vec2Array::create(triangles.size());
            auto slots=vsg::uintArray::create(triangles.size());
            auto indices=vsg::uintArray::create(triangles.size());
            for(std::size_t t=0;t<triangles.size()/3;++t) {
                const auto* physical=parameters->findFace(id,uint32_t(t));
                uint32_t material=0; vsg::vec2 thermal(290,.92f);
                if(physical) {
                    gpu::RadarMaterial value;
                    value.emissivity=physical->ir_emissivity[0]; value.eps_lwir=physical->ir_emissivity[0];
                    value.eps_mwir=physical->ir_emissivity[0]; value.rho_tv=physical->vis_albedo;
                    value.rho_swir=1-physical->ir_emissivity[1];
                    value.dielectric_real=physical->radar_epsilon_real; value.dielectric_imag=physical->radar_epsilon_imag;
                    value.sigma0_db=physical->sar_sigma0_db+(physical->rcs_modify_factor>0 ? 10*std::log10(physical->rcs_modify_factor) : -1000);
                    value.surface_rough=physical->radar_surface_rough; value.rcs_lut_id=int(physical->radar_rcs_lut_id);
                    auto cached=material_indices.find(physical);
                    if(cached!=material_indices.end()) material=cached->second;
                    else {material=uint32_t(materials.size()); materials.push_back(value);material_indices[physical]=material;}
                    thermal={physical->ir_base_temp+physical->temp_modify_offset,physical->ir_emissivity[0]};
                }
                for(std::size_t k=0;k<3;++k) {
                    const auto i=3*t+k; const auto& v=triangles[i];
                    (*pos)[i]={v.x,v.y,v.z}; (*props)[i]=thermal; (*mat)[i]=material;
                    (*uv)[i]={0,0}; (*slots)[i]=0; (*indices)[i]=uint32_t(i);
                }
            }
            auto mesh=vsg::VertexIndexDraw::create(); mesh->assignArrays({pos,props,mat,uv,slots});
            mesh->assignIndices(indices); mesh->indexCount=uint32_t(triangles.size()); mesh->instanceCount=1;
            return mesh;
        };
        for(const auto& body:input) {
            gpu::SceneInstance instance; instance.id=body.id;
            if(candidate && instance.id=="__earth_surface") {
                std::vector<Vec3> expanded;
                expanded.reserve(candidate->indices().size());
                for(auto index:candidate->indices()) {
                    const auto& p=candidate->vertices()[index];
                    expanded.push_back({float(p.x),float(p.y),float(p.z)});
                }
                instance.geometry=makeMesh(expanded,instance.id);
            } else instance.geometry=makeMesh(body.triangles,instance.id);
            instance.transform=body.transform; bodies.push_back(instance.id); result.push_back(std::move(instance));
        }
        // The authoritative analytic ground is bounded only by the sensor's
        // 100 km query domain. Recenter its patch for every aperture sample.
        for(std::size_t i=0;i<grounds.size();++i) {
            constexpr float e=200000;
            const std::vector<Vec3> vertices{{-e,0,-e},{e,0,-e},{e,0,e},{-e,0,-e},{e,0,e},{-e,0,e}};
            gpu::SceneInstance instance; instance.id="__physics_ground_"+std::to_string(i);
            instance.geometry=makeMesh(vertices,instance.id); instance.transform=vsg::translate(0.,grounds[i],0.);
            result.push_back(std::move(instance));
        }
        if(result.empty()) throw std::runtime_error("GPU scene has no authoritative physics geometry");
        return result;
    }
    void truth(uint32_t id,double time,gpu::GpuAperture& aperture,std::vector<vsg::dmat4>& transforms,std::vector<bool>& visible) {
        Simulation::SensorPose pose;
        if(!simulation.sampleSensorPose(id,time,pose)) throw std::runtime_error("GPU aperture truth unavailable at "+std::to_string(time));
        const auto bounds=footprints.at(id);simulation.validateEarthMeasurement(pose,bounds[0],bounds[1],bounds[2]);
        aperture={pose.position,pose.direction,pose.up};
        for(const auto& body:bodies) {
            simcore::RigidState state;
            if(!simulation.physics()->sampleBody(body,time,state))
                throw std::runtime_error("GPU body truth unavailable for "+body+" at "+std::to_string(time));
            transforms.push_back(state.matrix()); visible.push_back(true);
        }
        for(double height:grounds) { transforms.push_back(vsg::translate(pose.position.x,height,pose.position.z)); visible.push_back(true); }
    }
    gpu::EnvAnswer answer(uint32_t id,double time,const gpu::BandSpec& band) {
        const auto snapshot=simulation.physics()->snapshot();
        const auto epoch=simcore::parseUtcEpoch(snapshot->epoch_utc);
        rockyenv::EnvFrame frame; frame.model=simcore::geo::makeEnuToWorldMatrix(snapshot->anchor);
        frame.year=epoch.year; frame.month=epoch.month; frame.day=epoch.day; frame.hours_utc=epoch.hours()+time/3600.; frame.time_sec=time;
        auto weather=scenario.value("atmosphere",nlohmann::json::object());
        weather.update(scenario.value("environment",nlohmann::json::object()));
        frame.cloud_cover=weather.value("cloud_cover",.5f);
        frame.visibility=weather.value("visibility_m",weather.value("visibility",10000.f));
        frame.wind_speed=weather.value("wind_speed",6.f);
        frame.precipitation=weather.value("precipitation",weather.value("rain",0.f));
        frame.cloud_base_m=weather.value("cloud_base_m",weather.value("cloud_base",1200.f)); environment.update(frame);
        Simulation::SensorPose pose; if(!simulation.sampleSensorPose(id,time,pose)) throw std::runtime_error("environment aperture truth unavailable");
        gpu::EnvRequest request; request.band=band; request.sim_time=time; request.hours_utc=frame.hours_utc;
        request.alt=pose.position.y; request.lat=snapshot->anchor.lat_deg; request.lon=snapshot->anchor.lon_deg;
        request.los_el_deg=std::asin(std::clamp(pose.direction.y,-1.,1.))*180/M_PI;
        return environment.answer(request);
    }
};

SimulationGpuAdapter::SimulationGpuAdapter(Simulation& simulation,PhysParamManagerPtr parameters,const nlohmann::json& scenario)
    :impl_(std::make_unique<Impl>(simulation,std::move(parameters),scenario)) {
    std::vector<gpu::GpuSensorSpec> specifications;
    for(const auto& description:gpu::parseSensorDescriptions(scenario.dump())) {
        auto js=nlohmann::json::object();
        for(const auto& item:scenario.at("sensors").at("sensor_list")) if(item.at("sensor_id")==description.sensor_id) { js=item; break; }
        const auto signal=js.value("signal_params",nlohmann::json::object());
        for(const auto& coupling:js.value("rx_cross_sensors",nlohmann::json::array()))
            if(coupling.value("is_process",true) && coupling.value("coupling_factor",0.)>0 && coupling.value("band_match",1.)>0)
                throw std::runtime_error("GPU inter-sensor coupling is not implemented; select the CPU reference backend");
        if(description.scan.mode!="fixed" || description.scan.az_center!=0 || description.scan.el_center!=0)
            throw std::runtime_error("GPU scanning requires versioned gimbal truth, which is not implemented");
        const auto frame=js.value("frame",nlohmann::json::object());
        gpu::GpuSensorSpec spec; spec.id=uint32_t(description.sensor_id); spec.profile=gpu::SensorRegistry::profileFromDesc(description);
        if(!frame.contains("resolution")) {
            spec.profile.width=signal.value("image_width",spec.profile.width); spec.profile.height=signal.value("image_height",spec.profile.height);
        }
        spec.period_seconds=frame.contains("rate_hz") ? 1/description.rate_hz : signal.value("frame_interval_s",1/std::max(description.rate_hz,1.));
        if(spec.period_seconds<=0) spec.period_seconds=simulation.config().frame_dt;
        const bool radar=description.type=="radar" || description.type=="sar";
        if(description.type=="laser") throw std::runtime_error("GPU laser time-of-flight pipeline is not implemented");
        if(radar) {
            gpu::RadarChainParams p; p.width=spec.profile.width; p.height=spec.profile.height;
            p.lambda=description.wavelength; p.prf=description.pulse_rate;
            if(frame.contains("rate_hz")) p.prf=description.rate_hz;
            p.cpi_pulses=signal.value("doppler_bins",description.cpi_pulses);
            if(frame.contains("cpi_pulses")) p.cpi_pulses=description.cpi_pulses;
            p.n_fast=signal.value("range_gates",512u); p.bandwidth_hz=signal.value("chirp_bandwidth",30e6);
            p.pulse_width_s=signal.value("chirp_duration",1e-6); p.sample_rate_hz=signal.value("fs",p.bandwidth_hz);
            p.range_max_m=description.range_m; p.fov_h_deg=spec.profile.fov_deg;
            p.fov_v_deg=2*std::atan(std::tan(vsg::radians(p.fov_h_deg)*.5)*p.height/p.width)*180/M_PI;
            p.beam_width_rad=description.beam_width; spec.radar=p; spec.echo_only=description.type=="sar";
            spec.radar->transmit_power_w=description.power;
            impl_->types[spec.id]=spec.echo_only ? SensorType::SAR : SensorType::RADAR;
        } else {
            const bool infrared=description.type=="ir";
            if(!infrared && description.type!="vis") throw std::runtime_error("unsupported GPU sensor type: "+description.type);
            impl_->types[spec.id]=infrared ? SensorType::IR : SensorType::VIS;
            // IR broker contract is band-integrated radiance, not AGC display DN.
            spec.output=infrared ? gpu::ProductStage::Scene : gpu::ProductStage::Image;
        }
        const double fov_x=spec.radar?spec.radar->fov_h_deg:spec.profile.fov_deg;
        const double fov_y=spec.radar?spec.radar->fov_v_deg:2*std::atan(std::tan(fov_x*3.14159265358979323846/360)*spec.profile.height/spec.profile.width)*180/3.14159265358979323846;
        impl_->footprints[spec.id]={fov_x,fov_y,spec.radar?spec.radar->range_max_m:100000.};
        specifications.push_back(std::move(spec));
    }
    std::vector<gpu::RadarMaterial> materials; const auto geometry=impl_->geometry(materials);
    impl_->runtime.initialize(geometry,materials,specifications,simulation.config().gpu_validation,simulation.sensorDevice());
    spdlog::info("GPU measurement backend: {} ({})",impl_->runtime.deviceName(),impl_->runtime.hardwareRayTracing() ? "dynamic hardware TLAS" : "compute BVH");
}
SimulationGpuAdapter::~SimulationGpuAdapter()=default;
std::array<double,3> SimulationGpuAdapter::footprint(uint32_t id) const {
    return impl_->footprints.at(id);
}
SimulationGpuAdapter::TerrainBuilder SimulationGpuAdapter::captureTerrainBuilder() {
    // Only this capture and the returned validation/commit run on the world owner.
    const auto objectRevision=impl_->simulation.physics()->objectGeometryRevision();
    if(!impl_->terrain_inputs || impl_->input_geometry_revision!=objectRevision) {
        impl_->terrain_inputs=std::make_shared<const std::vector<Impl::BodyGeometry>>(impl_->captureBodies(true));
        impl_->input_geometry_revision=objectRevision;
    }
    if(!impl_->terrain_parameters || impl_->terrain_parameters->revision()!=impl_->parameters->revision())
        impl_->terrain_parameters=std::make_shared<PhysParamManager>(*impl_->parameters);
    auto staged=std::make_shared<Impl>(impl_->simulation,impl_->terrain_parameters,impl_->scenario);
    staged->object_revision=objectRevision;
    staged->grounds=impl_->simulation.physics()->groundPlanes();
    staged->revision=staged->parameters->revision();
    auto baseline=impl_->currentTopology();
    return [this,staged,baseline,input=impl_->terrain_inputs,build=impl_->runtime.captureSceneBuilder()]
           (const earth::EarthSurfaceSnapshot& candidate) {
        std::vector<gpu::RadarMaterial> materials;
        auto geometry=staged->buildGeometry(*input,materials,&candidate);
        build(staged->runtime,geometry,materials);
        staged->surface_revision=candidate.revision;
        staged->topology=baseline;
        for(auto& entry:staged->topology) if(std::get<1>(entry)=="__earth_surface")
            std::get<2>(entry)=candidate.indices().size();
        return PreparedTerrain{
            [this,staged,baseline] { return impl_->currentTopology()==baseline &&
                impl_->simulation.physics()->objectGeometryRevision()==staged->object_revision &&
                impl_->parameters->revision()==staged->revision &&
                impl_->simulation.physics()->groundPlanes()==staged->grounds; },
            [this,staged] {
                impl_->runtime.swap(staged->runtime);
                impl_->topology.swap(staged->topology);
                impl_->grounds.swap(staged->grounds);
                impl_->bodies.swap(staged->bodies);
                impl_->revision=staged->revision;
                impl_->object_revision=staged->object_revision;
                impl_->surface_revision=staged->surface_revision;
            }};
    };
}
void SimulationGpuAdapter::advanceTo(double time,const std::function<void(SignalFrame)>& sink,const std::function<bool(uint32_t)>& enabled) {
    const auto surface=impl_->simulation.earthSurface();
    if(impl_->object_revision!=impl_->simulation.physics()->objectGeometryRevision() || impl_->surface_revision!=(surface?surface->revision:0) || impl_->revision!=impl_->parameters->revision() || impl_->topology!=impl_->currentTopology() || impl_->grounds!=impl_->simulation.physics()->groundPlanes()) {
        std::vector<gpu::RadarMaterial> materials; const auto geometry=impl_->geometry(materials);
        impl_->runtime.replaceScene(geometry,materials);
    }
    impl_->runtime.advanceTo(time,[&](auto id,auto t,auto& aperture,auto& matrices,auto& masks){impl_->truth(id,t,aperture,matrices,masks);},
        [&](auto id,auto t,const auto& band){return impl_->answer(id,t,band);},
        [&](gpu::GpuMeasurementProduct&& product) {
            SignalFrame frame; frame.sensor_id=product.sensor_id; frame.sensor_type=impl_->types.at(product.sensor_id);
            frame.sim_time=product.end_time; frame.frame_id=product.frame_id; frame.resize(product.width,product.height);
            frame.product_stage=product.coherent ? (product.echo_only ? 0 : 2) : uint8_t(product.stage);
            if(product.coherent) {
                frame.samples=std::move(product.iq); frame.fs=float(product.sample_rate_hz); frame.prf=float(product.prf);
                frame.carrier_hz=product.carrier_hz; frame.coherent_pulses=product.height;
                frame.range_min=float(product.range_min_m); frame.range_max=float(product.range_min_m+(product.width-1)*299792458./(2*product.sample_rate_hz));
                frame.range_axis.resize(product.width); for(uint32_t i=0;i<product.width;++i) frame.range_axis[i]=float(product.range_min_m+i*299792458./(2*product.sample_rate_hz));
                frame.doppler_axis.resize(product.height); for(uint32_t i=0;i<product.height;++i) frame.doppler_axis[i]=float((i<product.height/2 ? int(i) : int(i)-int(product.height))*product.prf/product.height);
            } else for(std::size_t i=0;i<frame.samples.size();++i) frame.samples[i]={product.rgba.at(4*i),0};
            sink(std::move(frame));
        },enabled,impl_->simulation.earthValidFrom());
}
}
