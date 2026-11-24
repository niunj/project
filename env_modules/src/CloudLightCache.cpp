#include "CloudLightCache.h"
#include "EarthShader.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace rockyenv {
namespace {
struct LightCacheInit : vsg::Inherit<vsg::Command,LightCacheInit> {
    vsg::ref_ptr<vsg::PipelineBarrier> barrier;
    vsg::ref_ptr<vsg::Buffer> keys;
    mutable std::atomic<bool> done{false};
    void record(vsg::CommandBuffer& command)const override {
        if(!done.exchange(true)){
            if(keys)vkCmdFillBuffer(command,keys->vk(command.deviceID),0,VK_WHOLE_SIZE,0);
            barrier->record(command);
        }
    }
};
struct LightCacheRecorded : vsg::Inherit<vsg::Command,LightCacheRecorded> {
    std::shared_ptr<std::atomic<uint64_t>> count;
    void record(vsg::CommandBuffer&)const override {count->fetch_add(1);}
};
}
CloudLightCache::CloudLightCache(vsg::Device* device,vsg::ref_ptr<vsg::vec4Array> source):source_(source) {
    state=vsg::vec4Array::create(218,vsg::vec4{});
    bakedKeys=vsg::vec4Array::create(145,vsg::vec4{});
    bakedKeyBuffer=vsg::BufferInfo::create(bakedKeys);
    state->properties.dataVariance=vsg::DYNAMIC_DATA;
    commands=vsg::Switch::create();
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=sampler->addressModeV=sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod=0;
    const char* enabled=std::getenv("VSGRAYSIM_CLOUD_LIGHT_CACHE");
    if(!device||(enabled&&std::string(enabled)=="0")){
        auto zero=vsg::floatArray3D::create(1,1,1,0.f);zero->properties.format=VK_FORMAT_R32_SFLOAT;
        image=vsg::ImageInfo::create(sampler,zero);return;
    }
    auto keyStorage=vsg::Buffer::create(145*sizeof(vsg::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_SHARING_MODE_EXCLUSIVE);
    keyStorage->compile(device);
    auto keyMemory=vsg::DeviceMemory::create(device,keyStorage->getMemoryRequirements(device->deviceID),VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    keyStorage->bind(keyMemory,0);bakedKeyBuffer=vsg::BufferInfo::create(keyStorage,0,145*sizeof(vsg::vec4));
    constexpr int side=48,tile=side+2,axis=6;
    (*state)[0]=vsg::vec4(side,tile,axis,0);
    auto texture=vsg::Image::create();
    texture->imageType=VK_IMAGE_TYPE_3D;texture->format=VK_FORMAT_R32_SFLOAT;
    texture->extent={tile*axis,tile*axis,tile*4};texture->mipLevels=1;texture->arrayLayers=1;
    texture->samples=VK_SAMPLE_COUNT_1_BIT;texture->tiling=VK_IMAGE_TILING_OPTIMAL;
    texture->usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto view=vsg::createImageView(device,texture,VK_IMAGE_ASPECT_COLOR_BIT);
    image=vsg::ImageInfo::create(sampler,view,VK_IMAGE_LAYOUT_GENERAL);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {11,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto store=vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},view,VK_IMAGE_LAYOUT_GENERAL);
    auto descriptors=vsg::DescriptorSet::create(dsl,vsg::Descriptors{
        vsg::DescriptorBuffer::create(source_,6),
        vsg::DescriptorImage::create(store,11,0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorBuffer::create(state,12),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{bakedKeyBuffer},13,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto work=vsg::Commands::create();
    auto init=LightCacheInit::create();
    init->keys=keyStorage;
    init->barrier=vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT));
    init->barrier->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(0,VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,
        texture,VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}));
    // Establish GENERAL layout even when this viewport uses a different cloud path.
    commands->addChild(true,init);
    work->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_SHADER_WRITE_BIT)));
    work->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,
        vsgraysim::earth::earthShader("cloud_light_cache.comp"))));
    work->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,descriptors));
    dispatch_=vsg::Dispatch::create((tile+7)/8,(tile+7)/8,1);work->addChild(dispatch_);
    work->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT)));
    auto mark=LightCacheRecorded::create();mark->count=recorded_;work->addChild(mark);
    work_=vsg::Switch::create();work_->addChild(false,work);commands->addChild(true,work_);
}
void CloudLightCache::update(const VoxelWeatherFrame& frame) {
    const auto record=recorded_->load();
    if(record!=lastRecord_){
        for(const auto& [index,key]:pending_){keys_[index]=key;valid_[index]=true;}
        pending_.clear();lastRecord_=record;
    }
    for(int i=38;i<74;++i)(*state)[i]=vsg::vec4{};
    if(!dispatch_||!frame.cloudObjects||!frame.clouds||frame.cover<=0){
        if(work_)work_->setAllChildren(false);(*state)[1].x=0;state->dirty();return;
    }
    pending_.clear();
    const int count=std::clamp(int((*source_)[12].w),0,144);
    for(int i=0;i<count;++i){
        const auto& r=(*source_)[17+3*i];const auto& shape=(*source_)[18+3*i];
        const vsg::vec4 key(shape.z,shape.w,std::clamp(r.x/std::max(r.y,.001f),1.f,3.2f),
                            std::clamp(r.z/std::max(r.y,.001f),1.f,3.2f));
        const bool current=valid_[i]&&key.x==keys_[i].x&&key.y==keys_[i].y&&
            std::abs(key.z-keys_[i].z)<.01f&&std::abs(key.w-keys_[i].w)<.01f;
        (*state)[74+i]=current?keys_[i]:key;
        if(current)(*state)[38+i/4][i%4]=1;
        else{
            const int job=int(pending_.size());(*state)[2+job/4][job%4]=float(i);
            pending_.push_back({i,key});
        }
    }
    (*state)[1].x=float(pending_.size());
    dispatch_->groupCountZ=uint32_t((int((*state)[0].y)*pending_.size()+3)/4);
    work_->setAllChildren(!pending_.empty());state->dirty();
    if(std::getenv("VSGRAYSIM_CLOUD_LIGHT_CACHE_DEBUG"))
        std::cerr<<"CloudLightCache resident="<<count-int(pending_.size())<<'/'<<count
                 <<" jobs="<<pending_.size()<<" records="<<record<<'\n';
}
}
