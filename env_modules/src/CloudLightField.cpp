#include "CloudLightField.h"
#include "EarthShader.h"
#include <cstdlib>
namespace rockyenv {
namespace {
struct FieldInit : vsg::Inherit<vsg::Command,FieldInit> {
    vsg::ref_ptr<vsg::PipelineBarrier> barrier;mutable bool done=false;
    void record(vsg::CommandBuffer& c)const override{if(!done){done=true;barrier->record(c);}}
};
struct FieldRecorded : vsg::Inherit<vsg::Command,FieldRecorded> {
    std::shared_ptr<std::atomic<uint64_t>> count;
    void record(vsg::CommandBuffer&)const override{count->fetch_add(1);}
};
}
CloudLightField::CloudLightField(vsg::Device* device,vsg::ref_ptr<vsg::vec4Array> source,
    vsg::ref_ptr<vsg::vec4Array> weather,CloudLightCache& bulk):source_(source) {
    state=vsg::vec4Array::create(3,vsg::vec4{});state->properties.dataVariance=vsg::DYNAMIC_DATA;
    commands=vsg::Switch::create();
    auto sampler=vsg::Sampler::create();sampler->maxLod=0;
    sampler->addressModeU=sampler->addressModeV=sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const auto* option=std::getenv("VSGRAYSIM_CLOUD_LIGHT_FIELD");
    if(!device||(*bulk.state)[0].x<=0||(option&&std::string(option)=="0")) {
        auto zero=vsg::vec4Array3D::create(1,1,1,vsg::vec4{});zero->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
        image=vsg::ImageInfo::create(sampler,zero);return;
    }
    constexpr int side=24,tile=side+2,axis=6;
    (*state)[1]=vsg::vec4(side,tile,axis,0);
    auto texture=vsg::Image::create();texture->imageType=VK_IMAGE_TYPE_3D;texture->format=VK_FORMAT_R16G16B16A16_SFLOAT;
    texture->extent={tile*axis,tile*axis,tile*4};texture->mipLevels=texture->arrayLayers=1;
    texture->samples=VK_SAMPLE_COUNT_1_BIT;texture->tiling=VK_IMAGE_TILING_OPTIMAL;
    texture->usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto view=vsg::createImageView(device,texture,VK_IMAGE_ASPECT_COLOR_BIT);
    image=vsg::ImageInfo::create(sampler,view,VK_IMAGE_LAYOUT_GENERAL);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {14,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {15,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto store=vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},view,VK_IMAGE_LAYOUT_GENERAL);
    auto descriptors=vsg::DescriptorSet::create(dsl,vsg::Descriptors{
        vsg::DescriptorBuffer::create(weather,0),vsg::DescriptorBuffer::create(source_,6),
        vsg::DescriptorImage::create(bulk.image,11),vsg::DescriptorBuffer::create(bulk.state,12),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{bulk.bakedKeyBuffer},13,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorImage::create(store,14,0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),vsg::DescriptorBuffer::create(state,15)});
    auto init=FieldInit::create();init->barrier=vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0);
    init->barrier->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(0,VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
        VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,texture,VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}));
    commands->addChild(true,init);
    auto work=vsg::Commands::create();work->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT)));
    work->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,vsgraysim::earth::earthShader("cloud_light_field.comp"))));
    work->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,descriptors));
    work->addChild(vsg::Dispatch::create((tile*axis+7)/8,(tile*axis+7)/8,(tile*4+3)/4));
    work->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)));
    auto mark=FieldRecorded::create();mark->count=recorded_;work->addChild(mark);
    work_=vsg::Switch::create();work_->addChild(false,work);commands->addChild(true,work_);
}
void CloudLightField::update(const VoxelWeatherFrame& f) {
    const auto record=recorded_->load();if(record!=lastRecord_){bakedEpoch_=pendingEpoch_;lastRecord_=record;}
    const bool active=work_&&f.cloudObjects&&f.clouds&&f.cover>0;
    (*state)[1].w=active?1.f:0.f;
    if(!active){if(work_)work_->setAllChildren(false);state->dirty();return;}
    const size_t size=std::min(size_t(448),source_->size());
    const bool changed=!initialized_||previous_.size()!=size||!std::equal(previous_.begin(),previous_.end(),source_->begin())||
        f.sun!=frame_.sun||f.moon!=frame_.moon||f.moonPhase!=frame_.moonPhase||f.opticalDepth!=frame_.opticalDepth;
    if(changed){
        ++epoch_;(*state)[0]=vsg::vec4(float(epoch_&65535),float((epoch_>>16)&65535),float((epoch_>>32)&65535),float(epoch_>>48));
        previous_.assign(source_->data(),source_->data()+size);frame_=f;initialized_=true;
    }
    (*state)[2].x=std::clamp((*source_)[12].w,0.f,144.f);
    // Re-record an outstanding bake until it actually executes. GPU epoch
    // comparison in the reader prevents either stale use or CPU early commit.
    // On a frozen scene a cheap compute submission can be skipped after record.
    const bool bake=changed||bakedEpoch_!=epoch_;if(bake)pendingEpoch_=epoch_;
    work_->setAllChildren(bake);state->dirty();
}
}
