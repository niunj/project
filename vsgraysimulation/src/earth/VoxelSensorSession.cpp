#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
namespace vsgraysim::earth {
using namespace rockyenv::sensor;
struct VoxelSensorSession::Impl {
    VoxelSceneReader scene;std::string key;GpuSensorRuntime gpu;
    GpuSensorRuntime::TexturedSceneBuilder build;
    VoxelSceneSensorData active;double activeFrom=0,advancedThrough=0;
    struct Prepared {VoxelSceneSensorData data;std::unique_ptr<GpuSensorRuntime> gpu;};
    std::map<uint64_t,Prepared> pending;
    std::map<uint32_t,GpuSensorSpec> specifications;
    Impl(VoxelSceneReader owner,std::string participant):scene(std::move(owner)),key(std::move(participant)){}
    void prepare(VoxelSceneSnapshotPtr candidate) {
        if(candidate->ocean)throw std::invalid_argument("legacy GPU sensors do not model ocean refraction/underwater spectral transport; use the medium-aware ocean ray API");
        if(candidate->contentRevision==active.snapshot->contentRevision)return;
        auto previous=pending.find(candidate->contentRevision);
        const auto history=scene.history();
        if(previous!=pending.end()&&std::find(history.begin(),history.end(),previous->second.data.snapshot)!=history.end())return;
        if(previous==pending.end()&&pending.size()>=8)throw std::runtime_error("drain voxel measurements before publishing more than eight topology versions");
        Prepared staged;staged.data=makeVoxelSceneSensorData(candidate);staged.gpu=std::make_unique<GpuSensorRuntime>();
        build(*staged.gpu,staged.data.instances,staged.data.materials,&staged.data.textures);
        pending[candidate->contentRevision]=std::move(staged);
    }
    void measure(double time,double end,const VoxelSensorSession::Aperture& aperture,
                 const GpuSensorRuntime::Environment& environment,const GpuSensorRuntime::Sink& sink,const std::function<bool(uint32_t)>& enabled) {
        auto truth=[&](uint32_t id,double t,GpuAperture& output,std::vector<vsg::dmat4>& transforms,std::vector<bool>& masks){
            output=aperture(id,t);
            // At a topology cut the old segment is left-closed at acquisition
            // end. No geometry born at the cut may leak into an earlier exposure.
            const double sample=std::isfinite(end)&&t>=end-1e-10?std::max(activeFrom,end-std::max(1e-8,std::abs(end)*std::numeric_limits<double>::epsilon()*8)):t;
            const auto state=scene.sample(sample);
            if(state->contentRevision!=active.snapshot->contentRevision)throw std::runtime_error("voxel measurement geometry is outside its validity interval");
            const auto& spec=specifications.at(id);
            validateVoxelEarthView(*state,output.position,output.forward,output.up,spec.profile.fov_deg,
                double(spec.profile.width)/spec.profile.height,spec.radar?spec.radar->range_max_m:1e5);
            for(const auto& instance:active.instances) {
                const auto& o=state->objects.at(instance.id);transforms.push_back(o.pose(state->tick,state->fixedStep));masks.push_back(instance.visible);
            }
        };
        gpu.advanceTo(time,truth,environment,sink,enabled,activeFrom);
    }
};
VoxelSensorSession::VoxelSensorSession(VoxelScene& scene,const std::vector<GpuSensorSpec>& sensors,vsg::Device* device,
                                       const std::string& participant,std::shared_ptr<rockyenv::CloudSceneBinding> cloud)
    :VoxelSensorSession(readVoxelScene(scene),sensors,device,participant,std::move(cloud)) {}
VoxelSensorSession::VoxelSensorSession(VoxelSceneReader scene,const std::vector<GpuSensorSpec>& sensors,vsg::Device* device,
                                       const std::string& participant,std::shared_ptr<rockyenv::CloudSceneBinding> cloud)
    :impl_(std::make_unique<Impl>(scene,participant)) {
    if(scene.snapshot()->ocean)throw std::invalid_argument("legacy GPU sensors do not model ocean refraction/underwater spectral transport; use traceVoxelOceanScene for medium-aware geometry");
    auto& state=*impl_;state.active=makeVoxelSceneSensorData(scene.snapshot());state.activeFrom=state.advancedThrough=scene.snapshot()->timeSeconds();
    for(const auto& sensor:sensors)state.specifications.emplace(sensor.id,sensor);
    if(cloud)state.gpu.setVoxelCloud(std::move(cloud));
    state.gpu.initialize(state.active.instances,state.active.materials,sensors,false,device,&state.active.textures);
    state.build=state.gpu.captureTexturedSceneBuilder();scene.addParticipant(participant,[this](auto candidate){impl_->prepare(std::move(candidate));});
}
VoxelSensorSession::~VoxelSensorSession(){if(impl_)impl_->scene.removeParticipant(impl_->key);}
uint64_t VoxelSensorSession::rejectedSceneWindows()const{return impl_->gpu.rejectedSceneWindows();}
uint64_t VoxelSensorSession::contentRevision()const{return impl_->active.snapshot->contentRevision;}
GpuSensorRuntime::Statistics VoxelSensorSession::statistics()const{return impl_->gpu.statistics();}
void VoxelSensorSession::advanceTo(double time,const Aperture& aperture,const GpuSensorRuntime::Environment& environment,const GpuSensorRuntime::Sink& sink,const std::function<bool(uint32_t)>& enabled) {
    auto& state=*impl_;
    if(!aperture||!sink||!std::isfinite(time)||time<state.advancedThrough||time>state.scene.snapshot()->timeSeconds()+1e-10)throw std::invalid_argument("invalid voxel measurement target/providers");
    const auto history=state.scene.history();
    if(state.advancedThrough<history.front()->timeSeconds()-1e-10)throw std::out_of_range("voxel measurement backlog exceeds retained truth history");
    for(const auto& snapshot:history) {
        const double boundary=snapshot->timeSeconds();
        if(boundary<state.advancedThrough-1e-10||boundary>time+1e-10||snapshot->contentRevision==state.active.snapshot->contentRevision)continue;
        auto prepared=state.pending.find(snapshot->contentRevision);
        if(prepared==state.pending.end())throw std::logic_error("voxel sensor resources were not prepared before publication");
        state.measure(boundary,boundary,aperture,environment,sink,enabled);
        state.gpu.swap(*prepared->second.gpu);state.active=std::move(prepared->second.data);state.activeFrom=boundary;
        state.pending.erase(prepared);state.advancedThrough=boundary;
    }
    state.measure(time,std::numeric_limits<double>::infinity(),aperture,environment,sink,enabled);state.advancedThrough=time;
}
}
