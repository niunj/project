#include <rocky_env/GpuSensorRuntime.h>
#include <rocky_env/GpuContext.h>
#include <rocky_env/SharedSceneResources.h>
#include <rocky_env/GBufferPool.h>
#include <rocky_env/SensorPipeline.h>
#include <simcore/WorldState.h>
#include <stdexcept>
#include <map>
#include <cstdlib>
#include <algorithm>
#include <chrono>

namespace rockyenv::sensor {
namespace {
void require(bool ok,const std::string& error) { if(!ok) throw std::runtime_error(error); }
uint32_t receiverSeed(uint32_t id,uint64_t frame) {
    const uint64_t seed=(frame+1)*0x9e3779b97f4a7c15ull ^ (uint64_t(id)*0xbf58476d1ce4e5b9ull);
    return uint32_t(seed >> 24);
}
void radarReadback(GpuContext& context,RadarChain& radar,GpuMeasurementProduct& product){
    product.iq.resize(size_t(product.width)*product.height);
    std::vector<GpuContext::Readback> reads{{nullptr,product.echo_only?radar.gpuEcho().get():radar.gpuRdMap().get(),
        VK_IMAGE_LAYOUT_UNDEFINED,product.iq.size()*sizeof(std::complex<float>),product.iq.data()}};
    if(const auto count=radar.terrainFootprintSize()){
        product.terrain_footprint.resize(count);
        reads.push_back({nullptr,radar.gpuTerrainFootprint().get(),VK_IMAGE_LAYOUT_UNDEFINED,
            product.terrain_footprint.size()*sizeof(vsg::vec4),product.terrain_footprint.data()});
    }
    context.readbackBatch(reads);
}
void validateAperture(const GpuAperture& aperture){
    if(!simcore::finite(aperture.position) || !simcore::finite(aperture.forward) || !simcore::finite(aperture.up) ||
       vsg::length(aperture.forward)<1e-12 || vsg::length(vsg::cross(aperture.forward,aperture.up))<1e-12)
        throw std::invalid_argument("invalid GPU aperture truth");
}
void view(GBufferPool& gbuffer,SensorPipeline* pipeline,const GpuSensorSpec& spec,const GpuAperture& aperture) {
    validateAperture(aperture);
    const double aspect=double(spec.profile.width)/spec.profile.height;
    const double vertical=2*std::atan(std::tan(vsg::radians(spec.profile.fov_deg)*.5)/aspect)*180/3.14159265358979323846;
    const double far=spec.radar ? spec.radar->range_max_m : 1e5;
    auto projection=vsg::Perspective::create(vertical,aspect,.001,far)->transform();
    auto matrix=vsg::LookAt::create(aperture.position,aperture.position+aperture.forward,aperture.up)->transform();
    gbuffer.setSensorView(projection,matrix);
    if(pipeline) pipeline->setSensorView(projection,matrix,aperture.position,.001,far,vertical,aspect);
}
}
struct GpuSensorRuntime::Impl {
    GpuContext context;
    std::shared_ptr<SharedSceneResources> scene;
    std::shared_ptr<CloudSceneBinding> cloud;
    std::vector<RadarMaterial> materials;
    std::optional<SharedSceneResources::TexArraySpec> textures;
    std::vector<GpuSensorSpec> specifications;
    struct Instance {
        GpuSensorSpec spec;
        GBufferPool gbuffer;
        std::unique_ptr<SensorPipeline> image;
        std::unique_ptr<RadarChain> radar;
        vsg::ref_ptr<vsg::CommandGraph> clear,pulse,process,imageCommands;
    };
    std::map<uint32_t,std::unique_ptr<Instance>> instances;
    bool initialized=false,faulted=false;
    GpuSensorRuntime::SceneBuildStatistics sceneBuild;
    bool batching=!(std::getenv("VSGRAYSIM_GPU_SERIAL_REFERENCE")&&std::string(std::getenv("VSGRAYSIM_GPU_SERIAL_REFERENCE"))=="1");
    ~Impl() { try { context.clearGraphs(); } catch(...) {} instances.clear(); scene.reset(); }
    void build(const std::vector<SceneInstance>& geometry,const std::vector<RadarMaterial>& material,
               const SharedSceneResources::TexArraySpec* textureSpec=nullptr,const SharedSceneResources* previous=nullptr) {
        const auto begin=std::chrono::steady_clock::now();
        const auto elapsed=[](const auto at){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-at).count();};
        sceneBuild={};sceneBuild.hardware=context.rayTracing();
        context.clearGraphs(); instances.clear(); scene.reset(); materials=material;
        textures=textureSpec?std::make_optional(*textureSpec):std::nullopt;
        scene=std::make_shared<SharedSceneResources>();
        scene->voxelCloud=cloud;
        auto sceneResult=scene->buildInstances(*context.device(),geometry,materials,textures?&*textures:nullptr,previous);
        require(sceneResult.ok,sceneResult.error);
        sceneBuild.resources=scene->stats();sceneBuild.resourcesMs=elapsed(begin);
        if(scene->tlas) {
            const auto start=std::chrono::steady_clock::now();
            const auto family=context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
            // The compile submission builds the new TLAS and only BLASes
            // without a device handle. Sensor graph compilation can reuse it.
            auto acceleration=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
            acceleration->addChild(scene->sceneUpdates);
            context.compile(acceleration);
            sceneBuild.accelerationSubmitMs=elapsed(start);
        }
        const auto graphBegin=std::chrono::steady_clock::now();
        for(const auto& spec:specifications) {
            auto instance=std::make_unique<Instance>(); instance->spec=spec;
            auto bufferResult=instance->gbuffer.build(*context.device(),scene->sceneGeometry(),&materials,scene.get(),spec.profile.width,spec.profile.height);
            require(bufferResult.ok,bufferResult.error);
            if(spec.radar) {
                require(context.radarSupported(),"radar shader requires shared float atomics");
                instance->radar=std::make_unique<RadarChain>();
                auto result=instance->radar->build(*context.device(),&instance->gbuffer,*spec.radar,&materials,scene.get()); require(result.ok,result.error);
                const auto family=context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
                instance->clear=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
                instance->pulse=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
                instance->clear->addChild(instance->radar->clearCommands);
                instance->pulse->addChild(instance->gbuffer.commands); instance->pulse->addChild(instance->radar->pulseCommands);
                context.compile(instance->clear); context.compile(instance->pulse);
                if(!spec.echo_only) {
                    instance->process=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
                    instance->process->addChild(instance->radar->processCommands);
                    context.compile(instance->process);
                }

            } else {
                BandRadianceLut lut; require(lut.build(spec.profile.band),"GPU band LUT failed");
                instance->image=std::make_unique<SensorPipeline>();
                auto result=instance->image->buildOffscreen(*context.device(),spec.profile,lut,clearSkyEnv(spec.profile.band,false),scene->sceneGeometry(),scene.get(),&instance->gbuffer);
                require(result.ok,result.error);
                instance->imageCommands=batching?instance->image->offscreenStageGraph(spec.output):instance->image->commandGraph;
                if(batching)instance->imageCommands->children.insert(instance->imageCommands->children.begin(),vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT)));
                context.compile(instance->imageCommands);
            }
            instances.emplace(spec.id,std::move(instance));
        }
        sceneBuild.graphsMs=elapsed(graphBegin);
    }
};
GpuSensorRuntime::GpuSensorRuntime():impl_(std::make_unique<Impl>()) {}
GpuSensorRuntime::~GpuSensorRuntime()=default;
void GpuSensorRuntime::setVoxelCloud(std::shared_ptr<CloudSceneBinding> cloud) {
    if(impl_->initialized)throw std::logic_error("bind cloud before initializing sensors");
    impl_->cloud=std::move(cloud);
}
void GpuSensorRuntime::initialize(const std::vector<SceneInstance>& scene,const std::vector<RadarMaterial>& materials,
        const std::vector<GpuSensorSpec>& sensors,bool validation,vsg::Device* shared,const SharedSceneResources::TexArraySpec* textures) try {
    if(impl_->initialized) throw std::logic_error("GPU sensor runtime already initialized");
    if(sensors.empty()) throw std::invalid_argument("GPU sensor runtime requires sensors");
    impl_->context.initialize(validation,shared); impl_->specifications=sensors;
    for(const auto& sensor:sensors) {
        if(!sensor.profile.width || !sensor.profile.height || uint64_t(sensor.profile.width)*sensor.profile.height>(1u<<24) ||
           !std::isfinite(sensor.profile.fov_deg) || sensor.profile.fov_deg<=0 || sensor.profile.fov_deg>=179)
            throw std::invalid_argument("invalid GPU sensor dimensions/FOV");
        simcore::SensorRuntime::Cadence cadence; cadence.sensor_id=sensor.id;
        if(sensor.radar) {
            cadence.timeline=simcore::PeriodicTimeline::fromHz(sensor.radar->prf).dividedBy(sensor.radar->cpi_pulses);
            cadence.samples=sensor.radar->cpi_pulses; cadence.contiguous_windows=true;
        } else {
            cadence.timeline=simcore::PeriodicTimeline::fromHz(1/sensor.period_seconds);
            cadence.exposure=simcore::fromSeconds(sensor.profile.detector.integration_ms*.001);
            cadence.first_index=std::max<uint64_t>(1,uint64_t(std::ceil(simcore::toSeconds(cadence.exposure)/cadence.timeline.periodSeconds())));
            while(cadence.timeline.at(cadence.first_index)<cadence.exposure) ++cadence.first_index;
        }
        clock_.add(cadence);
    }
    impl_->build(scene,materials,textures); impl_->initialized=true;
} catch(const vsg::Exception& error) { impl_->faulted=true; throw std::runtime_error(error.message); }
GpuSensorRuntime::SceneBuilder GpuSensorRuntime::captureSceneBuilder() const {
    if(!impl_->initialized || impl_->faulted) throw std::logic_error("invalid GPU terrain staging state");
    return [device=vsg::ref_ptr<vsg::Device>(impl_->context.device()), specs=impl_->specifications,cloud=impl_->cloud,textures=impl_->textures]
           (GpuSensorRuntime& target,const auto& scene,const auto& materials) {
        try {
            auto& staged=*target.impl_;
            staged.context.initialize(false,device);
            staged.specifications=specs;
            staged.cloud=cloud;
            staged.build(scene,materials,textures?&*textures:nullptr);
            staged.initialized=true;
        } catch(const vsg::Exception& error) { throw std::runtime_error(error.message); }
    };
}
GpuSensorRuntime::TexturedSceneBuilder GpuSensorRuntime::captureTexturedSceneBuilder(bool reuse)const {
    if(!impl_->initialized||impl_->faulted)throw std::logic_error("invalid GPU voxel staging state");
    return [device=vsg::ref_ptr<vsg::Device>(impl_->context.device()),specs=impl_->specifications,cloud=impl_->cloud,
            previous=reuse?std::shared_ptr<const SharedSceneResources>(impl_->scene):nullptr]
        (GpuSensorRuntime& target,const auto& scene,const auto& materials,const SharedSceneResources::TexArraySpec* textures){
            try{auto& staged=*target.impl_;staged.context.initialize(false,device);staged.specifications=specs;staged.cloud=cloud;
                staged.build(scene,materials,textures,previous.get());staged.initialized=true;
            }catch(const vsg::Exception& error){throw std::runtime_error(error.message);}
        };
}
void GpuSensorRuntime::replaceScene(const std::vector<SceneInstance>& scene,const std::vector<RadarMaterial>& materials,const SharedSceneResources::TexArraySpec* textures) {
    if(!impl_->initialized || impl_->faulted) throw std::logic_error("GPU runtime cannot rebuild in its current state");
    const auto previous=impl_->scene;
    try { impl_->build(scene,materials,textures,previous.get()); }
    catch(const vsg::Exception& error) { impl_->faulted=true; throw std::runtime_error(error.message); }
    catch(...) { impl_->faulted=true; throw; }
}
void GpuSensorRuntime::advanceTo(double time,const Truth& truth,const Environment& environment,const Sink& sink,
        const std::function<bool(uint32_t)>& enabled,double sceneValidFrom) {
    if(!impl_->initialized || impl_->faulted || !truth || !sink) throw std::logic_error("GPU runtime is uninitialized/faulted or lacks truth/product sink");
    try {
        statistics_.lastProducts=0;statistics_.batching=impl_->batching;
        if(impl_->batching){
            struct Sample {GpuAperture aperture;std::vector<vsg::dmat4> transforms;std::vector<bool> visible;};
            auto sample=[&](uint32_t id,double t){Sample s;truth(id,t,s.aperture,s.transforms,s.visible);validateAperture(s.aperture);return s;};
            auto sameScene=[](const Sample& a,const Sample& b){return a.transforms==b.transforms&&a.visible==b.visible;};
            auto sameView=[](const Sample& a,const Sample& b){return a.aperture.position==b.aperture.position&&a.aperture.forward==b.aperture.forward&&a.aperture.up==b.aperture.up;};
            auto apply=[&](Impl::Instance& instance,const Sample& s){impl_->scene->setTransforms(s.transforms,s.visible);view(instance.gbuffer,instance.image.get(),instance.spec,s.aperture);};
            struct Pending {Impl::Instance* instance;GpuMeasurementProduct product;vsg::ref_ptr<vsg::CommandGraph> graph;};
            std::vector<Pending> batch;batch.reserve(16);Sample batchTruth;
            auto flush=[&]{
                if(batch.empty())return;
                std::vector<vsg::ref_ptr<vsg::CommandGraph>> graphs;for(const auto& job:batch)graphs.push_back(job.graph);
                const auto before=impl_->context.executionSubmissions();impl_->context.executeBatch(graphs);statistics_.graphSubmissions+=impl_->context.executionSubmissions()-before;
                if(batch.size()>1)++statistics_.multiEquipmentBatches;
                statistics_.maxBatchSize=std::max(statistics_.maxBatchSize,uint32_t(batch.size()));
                std::vector<GpuContext::Readback> reads;
                for(auto& job:batch){auto& p=job.product;auto& instance=*job.instance;
                    if(instance.image){VkImageLayout layout;auto image=instance.image->stageImageView(instance.spec.output,layout);require(bool(image),"GPU image batch stage unavailable");p.rgba.resize(size_t(p.width)*p.height*4);reads.push_back({image.get(),nullptr,layout,p.rgba.size()*sizeof(float),p.rgba.data()});}
                    else {p.iq.resize(size_t(p.width)*p.height);reads.push_back({nullptr,instance.spec.echo_only?instance.radar->gpuEcho().get():instance.radar->gpuRdMap().get(),VK_IMAGE_LAYOUT_UNDEFINED,p.iq.size()*sizeof(std::complex<float>),p.iq.data()});}
                }
                impl_->context.readbackBatch(reads);++statistics_.readbackBatches;
                for(auto& job:batch){sink(std::move(job.product));++statistics_.measurements;++statistics_.lastProducts;}
                batch.clear();
            };
            auto enqueue=[&](Impl::Instance& instance,GpuMeasurementProduct product,const Sample& s,vsg::ref_ptr<vsg::CommandGraph> graph){
                // No tolerance, time quantization or pose approximation. Reuse
                // only identical committed geometry; never overwrite an in-flight
                // sensor's UBO/seed/output with another exposure of that sensor.
                const bool duplicate=std::any_of(batch.begin(),batch.end(),[&](const auto& job){return job.instance==&instance;});
                if(!batch.empty()&&(impl_->cloud||!sameScene(batchTruth,s)||duplicate||batch.size()>=16))flush();
                if(batch.empty()){batchTruth=s;impl_->scene->setTransforms(s.transforms,s.visible);}
                view(instance.gbuffer,instance.image.get(),instance.spec,s.aperture);
                batch.push_back({&instance,std::move(product),graph});
            };
            do {
                for(const auto& measurement:clock_.advanceTo(simcore::fromSeconds(time))){
                    if(measurement.start<simcore::fromSeconds(sceneValidFrom)){++rejected_scene_windows_;continue;}
                    if(enabled&&!enabled(measurement.sensor_id))continue;
                    auto& instance=*impl_->instances.at(measurement.sensor_id);const auto& spec=instance.spec;
                    GpuMeasurementProduct product;product.sensor_id=spec.id;product.frame_id=measurement.frame_id;product.start_time=simcore::toSeconds(measurement.start);product.end_time=simcore::toSeconds(measurement.end);product.stage=spec.output;
                    const auto start=sample(spec.id,product.start_time),end=sample(spec.id,product.end_time);
                    if(instance.image){
                        const double midpoint=.5*(product.start_time+product.end_time);const auto center=sample(spec.id,midpoint);
                        product.width=spec.profile.width;product.height=spec.profile.height;
                        // Flush before touching this sensor's mutable parameters.
                        if(std::any_of(batch.begin(),batch.end(),[&](const auto& job){return job.instance==&instance;}))flush();
                        instance.image->setEnv(environment?environment(spec.id,midpoint,spec.profile.band):clearSkyEnv(spec.profile.band,false));
                        uint64_t seed=measurement.frame_id+0x9e3779b97f4a7c15ull*(uint64_t(spec.id)+1);seed=(seed^(seed>>30))*0xbf58476d1ce4e5b9ull;seed=(seed^(seed>>27))*0x94d049bb133111ebull;
                        instance.image->setFrameSeed(float((seed^(seed>>31))&0xffffffu));
                        enqueue(instance,std::move(product),center,instance.imageCommands);
                    }else{
                        auto& radar=*instance.radar;const auto& parameters=radar.params();std::vector<Sample> pulses;std::vector<vsg::dvec3> antennas,beams;
                        bool stationary=!impl_->cloud&&sameScene(start,end)&&sameView(start,end);
                        for(uint32_t pulse=0;pulse<parameters.cpi_pulses;++pulse){auto s=sample(spec.id,product.start_time+double(pulse)/parameters.prf);stationary=stationary&&sameScene(start,s)&&sameView(start,s);antennas.push_back(s.aperture.position);beams.push_back(s.aperture.forward);pulses.push_back(std::move(s));}
                        if(std::any_of(batch.begin(),batch.end(),[&](const auto& job){return job.instance==&instance;}))flush();
                        radar.setPulses(antennas,true,beams,float(parameters.beam_width_rad),receiverSeed(spec.id,product.frame_id));
                        product.coherent=true;product.echo_only=spec.echo_only;product.width=parameters.n_fast;product.height=parameters.cpi_pulses;product.sample_rate_hz=parameters.sample_rate_hz;product.prf=parameters.prf;product.carrier_hz=299792458./parameters.lambda;product.range_min_m=parameters.range_min_m;
                        if(stationary){
                            // Preserve the original per-stage compiled state and
                            // dynamic transfer domains. Only pulse work is merged:
                            // identical geometry/view permits one full-CPI kernel.
                            flush();++statistics_.stationaryCpis;apply(instance,start);radar.selectPulse(-1);
                            const auto before=impl_->context.executionSubmissions();
                            impl_->context.execute(instance.clear);impl_->context.execute(instance.pulse);
                            if(!spec.echo_only)impl_->context.execute(instance.process);
                            statistics_.graphSubmissions+=impl_->context.executionSubmissions()-before;
                            radarReadback(impl_->context,radar,product);
                            ++statistics_.readbackBatches;sink(std::move(product));++statistics_.measurements;++statistics_.lastProducts;
                        }
                        else{
                            flush();++statistics_.dynamicCpis;const auto before=impl_->context.executionSubmissions();impl_->context.execute(instance.clear);
                            for(uint32_t pulse=0;pulse<parameters.cpi_pulses;++pulse){apply(instance,pulses[pulse]);radar.selectPulse(int(pulse));impl_->context.execute(instance.pulse);}
                            radar.selectPulse(-1);
                            if(!spec.echo_only)impl_->context.execute(instance.process);
                            statistics_.graphSubmissions+=impl_->context.executionSubmissions()-before;
                            radarReadback(impl_->context,radar,product);
                            ++statistics_.readbackBatches;sink(std::move(product));++statistics_.measurements;++statistics_.lastProducts;
                        }
                    }
                    if(impl_->cloud)flush();
                }
                flush();
            }while(clock_.backlogged());
            return;
        }
        do {
            for(const auto& measurement:clock_.advanceTo(simcore::fromSeconds(time))) {
                if(measurement.start < simcore::fromSeconds(sceneValidFrom)){++rejected_scene_windows_;continue;}
                if(enabled && !enabled(measurement.sensor_id)) continue;
                auto& instance=*impl_->instances.at(measurement.sensor_id); const auto& spec=instance.spec;
                const auto beforeSubmissions=impl_->context.executionSubmissions();
                GpuMeasurementProduct product; product.sensor_id=spec.id; product.frame_id=measurement.frame_id;
                product.start_time=simcore::toSeconds(measurement.start); product.end_time=simcore::toSeconds(measurement.end); product.stage=spec.output;
                auto setTruth=[&](double sampleTime) {
                    GpuAperture aperture; std::vector<vsg::dmat4> transforms; std::vector<bool> visibility;
                    truth(spec.id,sampleTime,aperture,transforms,visibility);
                    impl_->scene->setTransforms(transforms,visibility); view(instance.gbuffer,instance.image.get(),spec,aperture);
                    return aperture;
                };
                // Verify both ends; never replace missing exposure/CPI truth with latest state.
                setTruth(product.start_time); setTruth(product.end_time);
                if(instance.image) {
                    const double sampleTime=.5*(product.start_time+product.end_time);
                    setTruth(sampleTime);
                    instance.image->setEnv(environment ? environment(spec.id,sampleTime,spec.profile.band) : clearSkyEnv(spec.profile.band,false));
                    uint64_t seed=measurement.frame_id+0x9e3779b97f4a7c15ull*(uint64_t(spec.id)+1);
                    seed=(seed^(seed>>30))*0xbf58476d1ce4e5b9ull;
                    seed=(seed^(seed>>27))*0x94d049bb133111ebull;
                    instance.image->setFrameSeed(float((seed^(seed>>31))&0xffffffu));
                    impl_->context.execute(instance.image->commandGraph);
                    product.width=spec.profile.width; product.height=spec.profile.height;
                    require(instance.image->readbackStage(*impl_->context.device(),spec.output,product.rgba),"GPU image product readback failed");
                } else {
                    auto& radar=*instance.radar; const auto& parameters=radar.params();
                    std::vector<vsg::dvec3> antennas,beams;
                    for(uint32_t pulse=0;pulse<parameters.cpi_pulses;++pulse) {
                        const auto aperture=setTruth(product.start_time+double(pulse)/parameters.prf);
                        antennas.push_back(aperture.position); beams.push_back(aperture.forward);
                    }
                    radar.setPulses(antennas,true,beams,float(parameters.beam_width_rad),receiverSeed(spec.id,product.frame_id));
                    impl_->context.execute(instance.clear);
                    for(uint32_t pulse=0;pulse<parameters.cpi_pulses;++pulse) {
                        setTruth(product.start_time+double(pulse)/parameters.prf);
                        radar.selectPulse(int(pulse)); impl_->context.execute(instance.pulse);
                    }
                    radar.selectPulse(-1);
                    if(!spec.echo_only)impl_->context.execute(instance.process);
                    product.coherent=true; product.echo_only=spec.echo_only;
                    product.width=parameters.n_fast; product.height=parameters.cpi_pulses;
                    product.sample_rate_hz=parameters.sample_rate_hz; product.prf=parameters.prf;
                    product.carrier_hz=299792458./parameters.lambda; product.range_min_m=parameters.range_min_m;
                    radarReadback(impl_->context,radar,product);
                }
                sink(std::move(product));
                statistics_.graphSubmissions+=impl_->context.executionSubmissions()-beforeSubmissions;
                ++statistics_.measurements;++statistics_.lastProducts;++statistics_.readbackBatches;
                if(instance.radar)++statistics_.dynamicCpis;
            }
        } while(clock_.backlogged());
    } catch(const vsg::Exception& error) { impl_->faulted=true; throw std::runtime_error(error.message); }
    catch(...) { impl_->faulted=true; throw; }
}
const std::string& GpuSensorRuntime::deviceName() const { return impl_->context.deviceName(); }
bool GpuSensorRuntime::hardwareRayTracing() const { return impl_->context.rayTracing(); }
GpuSensorRuntime::SceneBuildStatistics GpuSensorRuntime::sceneBuildStatistics() const {return impl_->sceneBuild;}
}
