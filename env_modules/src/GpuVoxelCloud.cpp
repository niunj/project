#include <rocky_env/GpuVoxelCloud.h>
#include "EarthShader.h"

#include <vsg/all.h>
#include <vsg/commands/PipelineBarrier.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rockyenv {
namespace {

vsg::ref_ptr<vsg::ImageView> storageVolume(vsg::Device* device, VkExtent3D extent) {
    auto image=vsg::Image::create();
    image->imageType=VK_IMAGE_TYPE_3D;
    image->format=VK_FORMAT_R32G32B32A32_SFLOAT;
    image->extent=extent;
    image->mipLevels=1;
    image->arrayLayers=1;
    image->samples=VK_SAMPLE_COUNT_1_BIT;
    image->tiling=VK_IMAGE_TILING_OPTIMAL;
    image->usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image->initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
    return vsg::createImageView(device,image,VK_IMAGE_ASPECT_COLOR_BIT);
}

vsg::ref_ptr<vsg::PipelineBarrier> computeBarrier() {
    return vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT));
}

struct SeedPush { uint32_t reset=1; uint32_t seed=1; uint32_t pad[2]{}; };
struct PressurePush { uint32_t source=0; uint32_t pad[3]{}; };
// P0 pressure warm start: on a cold reset the divergence pass zeroes both
// pressure images; on steady steps it keeps the previous converged pressure
// (damped by warmScale) as the Jacobi initial guess instead of restarting
// from zero every frame.
struct DivergencePush { uint32_t warmStart=0; float warmScale=.8f; uint32_t pad[2]{}; };

} // namespace

struct GpuVoxelCloud::Impl {
    vsg::ref_ptr<vsg::Device> device;
    vsg::ref_ptr<vsg::ImageView> state[2],velocity[2],pressure[2],backward[2],divergence;
    vsg::ref_ptr<vsg::Sampler> sampler;
    vsg::ref_ptr<vsg::vec4Array> parameters;
    vsg::ref_ptr<vsg::DescriptorSetLayout> descriptorLayout;
    vsg::ref_ptr<vsg::PipelineLayout> pipelineLayout;
    vsg::ref_ptr<vsg::DescriptorSet> descriptorSet;
    vsg::ref_ptr<vsg::Commands> initialize,step;
    vsg::ref_ptr<vsg::Value<SeedPush>> seedPush;
    vsg::ref_ptr<vsg::Value<PressurePush>> pressurePush[2];
    vsg::ref_ptr<vsg::Value<DivergencePush>> divergencePush;
    std::vector<vsg::ref_ptr<vsg::ComputePipeline>> pipelines;
    vsg::Commands::Children singleStep,steadyStep;
};

GpuVoxelCloud::GpuVoxelCloud()=default;
GpuVoxelCloud::~GpuVoxelCloud(){delete impl_;}

GpuVoxelCloud::Result GpuVoxelCloud::build(vsg::Device& deviceArg,const GpuVoxelCloudConfig& requested) {
    Result result;
    if(requested.width<8||requested.height<8||requested.depth<8||
       !std::isfinite(requested.voxelSizeMetres)||requested.voxelSizeMetres<=0||
       requested.pressureIterations<2||requested.maxColliders==0||
       !std::isfinite(vsg::length(requested.originMetres))) {
        result.error="invalid GPU voxel cloud configuration";return result;
    }
    if(requested.maxColliders>32) {
        result.error="GPU voxel cloud supports at most 32 simultaneous colliders";return result;
    }
    const auto& limits=deviceArg.getPhysicalDevice()->getProperties().limits;
    if(requested.width>limits.maxImageDimension3D||requested.height>limits.maxImageDimension3D||
       requested.depth>limits.maxImageDimension3D||
       uint64_t(requested.width)*requested.height*requested.depth>4'000'000||
       requested.pressureIterations>256) {
        result.error="GPU voxel cloud exceeds solver/device limits";return result;
    }
    delete impl_;impl_=new Impl;
    config_=requested;
    // Two Jacobi dispatches form a stable pressure0 -> pressure1 -> pressure0 pair.
    if(config_.pressureIterations&1u)++config_.pressureIterations;
    auto& i=*impl_;i.device=vsg::ref_ptr<vsg::Device>(&deviceArg);
    const VkExtent3D extent{config_.width,config_.height,config_.depth};
    for(auto& image:i.state)image=storageVolume(&deviceArg,extent);
    for(auto& image:i.velocity)image=storageVolume(&deviceArg,extent);
    for(auto& image:i.pressure)image=storageVolume(&deviceArg,extent);
    for(auto& image:i.backward)image=storageVolume(&deviceArg,extent);
    i.divergence=storageVolume(&deviceArg,extent);

    i.sampler=vsg::Sampler::create();
    i.sampler->magFilter=i.sampler->minFilter=VK_FILTER_LINEAR;
    i.sampler->addressModeU=i.sampler->addressModeV=i.sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    i.sampler->borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    i.sampler->maxLod=0;

    // Seven header vectors plus two vec4 arrays of up to 32 colliders plus
    // one impulse-meta vector and 16 impulses (3 vec4 each) with energy.
    i.parameters=vsg::vec4Array::create(7+2*32+1+3*maxImpulses(),vsg::vec4{});
    i.parameters->properties.dataVariance=vsg::DYNAMIC_DATA;

    vsg::DescriptorSetLayoutBindings bindings;
    for(uint32_t binding=0;binding<4;++binding)
        bindings.push_back({binding,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    bindings.push_back({4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    for(uint32_t binding=5;binding<10;++binding)
        bindings.push_back({binding,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    i.descriptorLayout=vsg::DescriptorSetLayout::create(bindings);
    i.pipelineLayout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{i.descriptorLayout},
        vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT,0,16}});
    vsg::Descriptors descriptors;
    auto storageDescriptor=[&](vsg::ref_ptr<vsg::ImageView> image,uint32_t binding) {
        descriptors.push_back(vsg::DescriptorImage::create(
            vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},image,VK_IMAGE_LAYOUT_GENERAL),
            binding,0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
    };
    storageDescriptor(i.state[0],0);storageDescriptor(i.state[1],1);
    storageDescriptor(i.velocity[0],2);storageDescriptor(i.velocity[1],3);
    descriptors.push_back(vsg::DescriptorBuffer::create(i.parameters,4));
    storageDescriptor(i.pressure[0],5);storageDescriptor(i.pressure[1],6);
    storageDescriptor(i.divergence,7);
    storageDescriptor(i.backward[0],8);storageDescriptor(i.backward[1],9);
    i.descriptorSet=vsg::DescriptorSet::create(i.descriptorLayout,descriptors);

    auto pipeline=[&](const char* shader) {
        auto value=vsg::ComputePipeline::create(i.pipelineLayout,vsgraysim::earth::earthShader(shader));
        i.pipelines.push_back(value);return value;
    };
    auto seed=pipeline("cloud_voxel_seed.comp");
    auto advect=pipeline("cloud_voxel_advect.comp");
    auto reverseAdvect=pipeline("cloud_voxel_advect_reverse.comp");
    auto correctAdvect=pipeline("cloud_voxel_advect_correct.comp");
    auto micro=pipeline("cloud_voxel_microphysics.comp");
    auto rain=pipeline("cloud_voxel_rain.comp");
    auto divergence=pipeline("cloud_voxel_divergence.comp");
    auto pressure=pipeline("cloud_voxel_pressure.comp");
    auto project=pipeline("cloud_voxel_project.comp");
    auto copy=pipeline("cloud_voxel_copy.comp");
    i.seedPush=vsg::Value<SeedPush>::create(SeedPush{});
    i.pressurePush[0]=vsg::Value<PressurePush>::create(PressurePush{0,{0,0,0}});
    i.pressurePush[1]=vsg::Value<PressurePush>::create(PressurePush{1,{0,0,0}});
    i.divergencePush=vsg::Value<DivergencePush>::create(DivergencePush{});

    i.initialize=vsg::Commands::create();
    auto transition=vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0);
    const std::array<vsg::ref_ptr<vsg::ImageView>,9> images{
        i.state[0],i.state[1],i.velocity[0],i.velocity[1],i.pressure[0],i.pressure[1],i.divergence,
        i.backward[0],i.backward[1]};
    for(const auto& image:images)
        transition->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
            0,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,image->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}));
    i.initialize->addChild(transition);

    i.step=vsg::Commands::create();
    // A prior frame may still sample the persistent cloud while this frame's
    // pre-render solver starts. Order those reads before overwriting state0.
    i.step->addChild(vsg::PipelineBarrier::create(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
                                   VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT)));
    const uint32_t gx=(config_.width+3)/4,gy=(config_.height+3)/4,gz=(config_.depth+3)/4;
    auto bind=[&](vsg::ComputePipeline* p) {
        i.step->addChild(vsg::BindComputePipeline::create(p));
        i.step->addChild(vsg::BindDescriptorSets::create(VK_PIPELINE_BIND_POINT_COMPUTE,
            i.pipelineLayout,0,vsg::DescriptorSets{i.descriptorSet}));
    };
    auto dispatch=[&]() {i.step->addChild(vsg::Dispatch::create(gx,gy,gz));};
    bind(seed);i.step->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,i.seedPush));dispatch();
    i.step->addChild(computeBarrier());
    bind(advect);dispatch();i.step->addChild(computeBarrier());
    bind(reverseAdvect);dispatch();i.step->addChild(computeBarrier());
    bind(correctAdvect);dispatch();i.step->addChild(computeBarrier());
    bind(micro);dispatch();i.step->addChild(computeBarrier());
    bind(rain);dispatch();i.step->addChild(computeBarrier());
    bind(divergence);
    i.step->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,i.divergencePush));
    dispatch();i.step->addChild(computeBarrier());
    bind(pressure);
    for(uint32_t pass=0;pass<config_.pressureIterations;++pass) {
        i.step->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,i.pressurePush[pass&1u]));
        dispatch();i.step->addChild(computeBarrier());
    }
    bind(project);dispatch();i.step->addChild(computeBarrier());
    bind(copy);dispatch();
    i.step->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT)));

    i.singleStep=i.step->children;
    i.steadyStep=i.singleStep;
    for(auto& command:i.steadyStep) {
        auto push=command.cast<vsg::PushConstants>();
        if(!push)continue;
        if(push->data==i.seedPush)
            command=vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,
                vsg::Value<SeedPush>::create(SeedPush{0,config_.weatherSeed,{0,0}}));
        else if(push->data==i.divergencePush)
            command=vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,
                vsg::Value<DivergencePush>::create(DivergencePush{1,.8f,{0,0}}));
    }

    ready_=true;resetPending_=true;revision_=0;
    result.ok=true;return result;
}

void GpuVoxelCloud::update(const GpuVoxelCloudFrame& frame,uint32_t substeps) {
    if(!ready_||!impl_)throw std::logic_error("GPU voxel cloud is not built");
    if(substeps==0||substeps>64||!std::isfinite(frame.simulationSeconds)||!std::isfinite(frame.stepSeconds)||
       frame.stepSeconds<=0||frame.stepSeconds>.1f||frame.ambientVapourMixingRatio<0||
       frame.ambientTemperatureK<180||frame.ambientPressurePa<1000)
        throw std::invalid_argument("invalid GPU voxel cloud frame");
    const float scalars[]{frame.ambientVapourMixingRatio,frame.ambientTemperatureK,
        frame.ambientPressurePa,frame.gravity,frame.lapseRateKPerMetre,
        frame.condensationRate,frame.evaporationRate,frame.buoyancy,frame.waterLoading,
        frame.vorticityConfinement,frame.velocityDamping,frame.solarHeatingKPerSecond,
        frame.wakeSeconds,frame.displacement,frame.cloudCover};
    for(float scalar:scalars)if(!std::isfinite(scalar)||scalar<0)
        throw std::invalid_argument("invalid cloud thermodynamic/forcing parameter");
    if(!std::isfinite(vsg::length(frame.ambientWind)))throw std::invalid_argument("invalid cloud wind");
    for(const auto& body:frame.colliders)
        if(!std::isfinite(vsg::length(body.centreMetres))||!std::isfinite(vsg::length(body.velocityMetresPerSecond))||
           !std::isfinite(body.radiusMetres)||body.radiusMetres<=0||!std::isfinite(body.coupling)||body.coupling<0)
            throw std::invalid_argument("invalid cloud collider");
    for(const auto& event:frame.impulses)
        if(!std::isfinite(vsg::length(event.centreMetres))||!std::isfinite(event.radiusMetres)||event.radiusMetres<=0||
           !std::isfinite(event.temperatureKickK)||event.temperatureKickK<0||
           !std::isfinite(event.radialVelocityMetresPerSecond)||event.radialVelocityMetresPerSecond<0||
           !std::isfinite(event.dissipation)||event.dissipation<0||event.dissipation>1||
           !std::isfinite(event.durationSeconds)||event.durationSeconds<=0)
            throw std::invalid_argument("invalid cloud impulse");
    auto& u=*impl_->parameters;
    u[0]=vsg::vec4(config_.originMetres,config_.voxelSizeMetres);
    u[1]=vsg::vec4(float(config_.width),float(config_.height),float(config_.depth),frame.stepSeconds);
    u[2]=vsg::vec4(frame.ambientWind,frame.ambientVapourMixingRatio);
    u[3]=vsg::vec4(frame.ambientTemperatureK,frame.ambientPressurePa,frame.gravity,frame.lapseRateKPerMetre);
    u[4]=vsg::vec4(frame.condensationRate,frame.evaporationRate,frame.buoyancy,frame.waterLoading);
    u[5]=vsg::vec4(frame.vorticityConfinement,frame.velocityDamping,frame.solarHeatingKPerSecond,float(frame.simulationSeconds));
    const size_t count=std::min<size_t>({frame.colliders.size(),config_.maxColliders,32});
    // colliderMeta.w carries the coverage-dependent weather regime into the
    // seed pass (cumulus / stratiform deck / deep convection blend).
    u[6]=vsg::vec4(float(count),frame.wakeSeconds,std::clamp(frame.displacement,0.f,1.f),std::clamp(frame.cloudCover,0.f,1.f));
    for(size_t collider=0;collider<32;++collider) {
        if(collider<count) {
            const auto& body=frame.colliders[collider];
            u[7+collider]=vsg::vec4(body.centreMetres,std::max(body.radiusMetres,.1f));
            u[39+collider]=vsg::vec4(body.velocityMetresPerSecond,std::max(body.coupling,0.f));
        } else {u[7+collider]=vsg::vec4{};u[39+collider]=vsg::vec4{};}
    }
    // Impulse block: meta vector at 71, then 16 slots of 3 vec4 each.
    // Impulses carry energy (temperature kick + radial shock + liquid
    // dissipation) and act inside the microphysics pass, unlike colliders
    // which only displace. Each impulse tracks its remaining lifetime so a
    // multi-second blast keeps expanding until it decays.
    const size_t impulseCount=std::min<size_t>(frame.impulses.size(),maxImpulses());
    u[71]=vsg::vec4(float(impulseCount),frame.stepSeconds,0.f,0.f);
    for(size_t k=0;k<maxImpulses();++k) {
        const size_t base=72+3*k;
        if(k<impulseCount) {
            const auto& impulse=frame.impulses[k];
            u[base+0]=vsg::vec4(impulse.centreMetres,std::max(impulse.radiusMetres,.5f));
            u[base+1]=vsg::vec4(impulse.temperatureKickK,impulse.dissipation,
                                std::max(impulse.durationSeconds,.05f),
                                impulse.radialVelocityMetresPerSecond);
            u[base+2]=vsg::vec4{}; // reserved (e.g. per-impulse timing)
        } else {u[base+0]=vsg::vec4{};u[base+1]=vsg::vec4{};u[base+2]=vsg::vec4{};}
    }
    u.dirty();
    impl_->seedPush->value().reset=resetPending_?1u:0u;
    // P0: fixed weather seed from the configuration, never the simulation
    // clock, so every rebase draws the same deterministic weather pattern.
    impl_->seedPush->value().seed=config_.weatherSeed;
    // P0: warm-start the pressure solve from the previous converged field on
    // steady steps; only a genuine reset starts the Jacobi guess from zero.
    impl_->divergencePush->value().warmStart=resetPending_?0u:1u;
    impl_->step->children=impl_->singleStep;
    for(uint32_t step=1;step<substeps;++step)
        impl_->step->children.insert(impl_->step->children.end(),impl_->steadyStep.begin(),impl_->steadyStep.end());
    resetPending_=false;revision_+=substeps;
}

void GpuVoxelCloud::setOriginMetres(const vsg::vec3& origin) {
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))
        throw std::invalid_argument("invalid GPU voxel cloud origin");
    if(origin!=config_.originMetres) {config_.originMetres=origin;resetPending_=true;}
}
void GpuVoxelCloud::reset(){resetPending_=true;}
vsg::Commands* GpuVoxelCloud::initializationCommands()const{return impl_?impl_->initialize.get():nullptr;}
vsg::Commands* GpuVoxelCloud::stepCommands()const{return impl_?impl_->step.get():nullptr;}
vsg::ImageView* GpuVoxelCloud::thermodynamicMap()const{return impl_?impl_->state[0].get():nullptr;}
vsg::ImageView* GpuVoxelCloud::velocityMap()const{return impl_?impl_->velocity[0].get():nullptr;}
vsg::ImageView* GpuVoxelCloud::pressureMap()const{return impl_?impl_->pressure[0].get():nullptr;}
vsg::Sampler* GpuVoxelCloud::sampler()const{return impl_?impl_->sampler.get():nullptr;}

} // namespace rockyenv
