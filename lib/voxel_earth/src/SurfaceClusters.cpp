#include <vsgraysim/earth/SurfaceClusters.h>
#include <vsgraysim/earth/VoxelScene.h>
#include "EarthShader.h"

#include <meshoptimizer.h>
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <unordered_map>

namespace vsgraysim::earth {
namespace {
constexpr uint32_t terrainClusterTriangles=4096;
constexpr uint32_t modelClusterTriangles=1024;
constexpr uint32_t minimumTriangles=modelClusterTriangles;

struct ClusterParameters {
    vsg::vec4 sphere;
    vsg::uivec4 ranges;
    vsg::vec4 errors;
};
struct ClusterView {
    vsg::vec4 planes[6]{};
    vsg::vec4 eyeAndScale{};
    vsg::vec4 forwardAndError{};
};
static_assert(sizeof(ClusterParameters)==48&&sizeof(ClusterView)==128);

struct Geometry {
    vsg::ref_ptr<vsg::BufferInfo> indices,clusters;
    uint32_t clusterCount=0;
    uint64_t triangles=0,lowerTriangles=0;
};

std::shared_ptr<Geometry> partition(const vsg::VertexIndexDraw& source,bool terrain) {
    if(source.firstIndex||source.vertexOffset||source.firstInstance||source.instanceCount!=1||source.arrays.empty()||!source.indices)return {};
    const auto vertices=source.arrays.front()->data.cast<vsg::vec3Array>();
    const auto indices=source.indices->data.cast<vsg::uintArray>();
    if(!vertices||!indices||vertices->empty()||source.indexCount!=indices->size()||indices->size()%3||indices->size()/3<minimumTriangles)return {};
    const auto verticesCount=vertices->size();
    if(std::any_of(indices->begin(),indices->end(),[&](uint32_t i){return i>=verticesCount;}))return {};
    const auto* positions=vertices->data()->data();
    const size_t clusterIndices=(terrain?terrainClusterTriangles:modelClusterTriangles)*3;
    std::vector<uint32_t> sorted(indices->size());
    meshopt_spatialSortTriangles(sorted.data(),indices->data(),indices->size(),positions,verticesCount,sizeof(vsg::vec3));
    std::vector<uint32_t> packed=sorted;
    packed.reserve(terrain?sorted.size()*3/2:sorted.size());
    std::vector<ClusterParameters> metadata;
    auto output=std::make_shared<Geometry>();
    std::vector<uint32_t> simplified(terrain?clusterIndices:0);
    for(size_t start=0;start<sorted.size();start+=clusterIndices) {
        const auto count=std::min(sorted.size()-start,clusterIndices);
        const auto* begin=sorted.data()+start;
        auto lo=(*vertices)[*begin],hi=lo;
        for(size_t i=0;i<count;++i)for(int axis=0;axis<3;++axis) {
            const auto& p=(*vertices)[begin[i]];
            lo[axis]=std::min(lo[axis],p[axis]);hi[axis]=std::max(hi[axis],p[axis]);
        }
        const auto center=(lo+hi)*.5f;
        const float radius=vsg::length(hi-lo)*.5f+0.002f;
        ClusterParameters cluster{vsg::vec4(center,radius),vsg::uivec4(uint32_t(start),uint32_t(count),0,0),vsg::vec4{}};
        // Untouched mesh-asset triangles retain their per-patch material atlas.
        // Continuous terrain UVs allow collapses with locked cluster borders.
        if(terrain&&count>=192) {
            float error=0;
            const float errorLimit=std::max(.025f,radius*.025f);
            const auto remaining=meshopt_simplify(simplified.data(),begin,count,positions,verticesCount,sizeof(vsg::vec3),
                (count/9)*3,errorLimit,meshopt_SimplifyLockBorder|meshopt_SimplifySparse|meshopt_SimplifyErrorAbsolute,&error);
            if(remaining>0&&remaining*6<count*5&&std::isfinite(error)) {
                cluster.ranges.z=uint32_t(packed.size());cluster.ranges.w=uint32_t(remaining);
                cluster.errors.x=std::max(error,.002f);
                packed.insert(packed.end(),simplified.begin(),simplified.begin()+remaining);
            }
        }
        output->lowerTriangles+=(cluster.ranges.w?cluster.ranges.w:cluster.ranges.y)/3;
        metadata.push_back(cluster);
    }
    auto packedIndices=vsg::uintArray::create(packed.size());
    std::copy(packed.begin(),packed.end(),packedIndices->begin());
    auto bounds=vsg::ubyteArray::create(metadata.size()*sizeof(ClusterParameters));
    std::memcpy(bounds->dataPointer(),metadata.data(),bounds->dataSize());
    output->indices=vsg::BufferInfo::create(packedIndices);
    output->clusters=vsg::BufferInfo::create(bounds);
    output->clusterCount=uint32_t(metadata.size());
    output->triangles=indices->size()/3;
    return output;
}

class IndirectSurfaceDraw : public vsg::Inherit<vsg::VertexIndexDraw,IndirectSurfaceDraw> {
public:
    IndirectSurfaceDraw(vsg::ref_ptr<vsg::VertexIndexDraw> source,vsg::ref_ptr<vsg::Buffer> indices,
                        vsg::ref_ptr<vsg::Buffer> commands,uint32_t viewId)
        :Inherit(*source),source_(std::move(source)),selectedIndices_(std::move(indices)),indirect_(std::move(commands)),viewId_(viewId) {}
    void compile(vsg::Context& context) override {
        // Retain the source geometry for other render views and share its vertex buffers.
        source_->compile(context);
        vsg::assignVulkanArrayData(context.deviceID,arrays,_vulkanData[context.deviceID]);
    }
    void record(vsg::CommandBuffer& commandBuffer) const override {
        if(commandBuffer.viewID!=viewId_) {
            source_->record(commandBuffer);
            return;
        }
        const auto& vkd=_vulkanData[commandBuffer.deviceID];
        VkCommandBuffer cb=commandBuffer;
        vkCmdBindVertexBuffers(cb,firstBinding,uint32_t(vkd.vkBuffers.size()),vkd.vkBuffers.data(),vkd.offsets.data());
        vkCmdBindIndexBuffer(cb,selectedIndices_->vk(commandBuffer.deviceID),0,VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(cb,indirect_->vk(commandBuffer.deviceID),0,1,sizeof(VkDrawIndexedIndirectCommand));
    }

private:
    vsg::ref_ptr<vsg::VertexIndexDraw> source_;
    vsg::ref_ptr<vsg::Buffer> selectedIndices_,indirect_;
    uint32_t viewId_;
};

class ResetIndexedDraw : public vsg::Inherit<vsg::Command,ResetIndexedDraw> {
public:
    explicit ResetIndexedDraw(vsg::ref_ptr<vsg::Buffer> commands):indirect_(std::move(commands)) {}
    void record(vsg::CommandBuffer& commandBuffer) const override {
        const VkDrawIndexedIndirectCommand draw{0,1,0,0,0};
        vkCmdUpdateBuffer(commandBuffer,indirect_->vk(commandBuffer.deviceID),0,sizeof(draw),&draw);
    }
private:
    vsg::ref_ptr<vsg::Buffer> indirect_;
};

struct Batch {
    vsg::ref_ptr<vsg::VertexIndexDraw> source,draw;
    vsg::ref_ptr<vsg::Value<ClusterView>> view;
    vsg::ref_ptr<vsg::Commands> compute;
    vsg::ref_ptr<vsg::Buffer> indirect;
    std::shared_ptr<Geometry> geometry;
};

Batch makeBatch(vsg::Device& device,vsg::ComputePipeline& pipeline,
                vsg::ref_ptr<vsg::VertexIndexDraw> source,std::shared_ptr<Geometry> geometry,uint32_t viewId) {
    Batch batch;
    batch.source=std::move(source);batch.geometry=std::move(geometry);
    const auto count=batch.geometry->clusterCount;
    const auto bytes=(VkDeviceSize(count)+1)*sizeof(VkDrawIndexedIndirectCommand);
    batch.indirect=vsg::createBufferAndMemory(&device,bytes,VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const auto selectedBytes=batch.geometry->triangles*3*sizeof(uint32_t);
    auto selectedIndices=vsg::createBufferAndMemory(&device,selectedBytes,VK_BUFFER_USAGE_INDEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_SHARING_MODE_EXCLUSIVE,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    batch.draw=IndirectSurfaceDraw::create(batch.source,selectedIndices,batch.indirect,viewId);
    batch.view=vsg::Value<ClusterView>::create(ClusterView{});
    const auto& layout=pipeline.layout;
    auto set=vsg::DescriptorSet::create(layout->setLayouts.front(),vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{batch.geometry->clusters},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(batch.indirect,0,bytes)},1,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{batch.geometry->indices},2,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(selectedIndices,0,selectedBytes)},3,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    batch.compute=vsg::Commands::create();
    batch.compute->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,set));
    batch.compute->addChild(vsg::PushConstants::create(VK_SHADER_STAGE_COMPUTE_BIT,0,batch.view));
    batch.compute->addChild(vsg::Dispatch::create(count,1,1));
    return batch;
}
}

struct SurfaceClusterDisplay::Impl {
    vsg::ref_ptr<vsg::ComputePipeline> pipeline;
    vsg::ref_ptr<vsg::Group> commands=vsg::Group::create();
    std::map<std::string,Batch> batches;
    std::optional<uint64_t> lastRequestedFrame;
};

SurfaceClusterDisplay::SurfaceClusterDisplay():impl_(std::make_unique<Impl>()){}
SurfaceClusterDisplay::~SurfaceClusterDisplay()=default;

std::shared_ptr<SurfaceClusterDisplay> SurfaceClusterDisplay::prepare(
    VoxelSceneDisplay& display,const SurfaceClusterDisplay* reuse,vsg::Device& device,uint32_t viewId) {
    auto output=std::shared_ptr<SurfaceClusterDisplay>(new SurfaceClusterDisplay());
    auto& result=*output->impl_;
    if(reuse)result.pipeline=reuse->impl_->pipeline;
    else {
        auto bindings=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
            {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
        auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{bindings},
            vsg::PushConstantRanges{{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(ClusterView)}});
        result.pipeline=vsg::ComputePipeline::create(layout,earthShader("surface_cluster_select.comp"));
    }
    const auto& limits=device.getPhysicalDevice()->getProperties().limits;
    auto& commands=*result.commands;
    // Reuse one set of GPU buffers per batch only after the previous draws have read them.
    commands.addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_INDEX_READ_BIT,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT)));
    auto dispatches=vsg::Group::create();
    std::unordered_map<const void*,std::shared_ptr<Geometry>> geometryCache;
    std::vector<std::pair<vsg::ref_ptr<vsg::Node>*,vsg::ref_ptr<vsg::StateGroup>>> replacements;
    auto mount=[&](const std::string& id,vsg::ref_ptr<vsg::Node>& node,const void* identity,bool terrain) {
        const auto group=node.cast<vsg::StateGroup>();
        const auto draw=group&&group->children.size()==1?group->children.front().cast<vsg::VertexIndexDraw>():nullptr;
        if(!draw||draw->indexCount/3<minimumTriangles)return;
        Batch batch;
        if(reuse) {
            auto previous=reuse->impl_->batches.find(id);
            if(previous!=reuse->impl_->batches.end()&&previous->second.source==draw)batch=previous->second;
        }
        if(!batch.geometry) {
            auto [entry,created]=geometryCache.try_emplace(identity?identity:draw.get());
            if(created)entry->second=partition(*draw,terrain);
            const auto& geometry=entry->second;
            if(!geometry||geometry->clusters->data->dataSize()>limits.maxStorageBufferRange||
               geometry->indices->data->dataSize()>limits.maxStorageBufferRange||
               geometry->clusterCount>limits.maxComputeWorkGroupCount[0])return;
            batch=makeBatch(device,*result.pipeline,draw,geometry,viewId);
        }
        auto replacement=vsg::StateGroup::create(*group);
        replacement->children.front()=batch.draw;
        commands.addChild(ResetIndexedDraw::create(batch.indirect));
        dispatches->addChild(batch.compute);
        result.batches.emplace(id,std::move(batch));
        replacements.emplace_back(&node,std::move(replacement));
    };
    for(const auto& [id,drawing]:display.drawings) {
        auto& children=display.placements.at(id)->children;
        if(!drawing.externalLighting&&children.size()==1)
            mount(id,children.front(),drawing.source.get(),drawing.terrain);
    }
    if(display.contextDrawable)mount({},display.node->children[*display.contextDrawable],nullptr,true);
    commands.addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT)));
    commands.addChild(vsg::BindComputePipeline::create(result.pipeline));
    commands.addChild(dispatches);
    commands.addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT|VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_INDIRECT_COMMAND_READ_BIT|VK_ACCESS_INDEX_READ_BIT)));
    for(auto& [target,replacement]:replacements)*target=std::move(replacement);
    return result.batches.empty()?nullptr:output;
}

vsg::ref_ptr<vsg::Group> SurfaceClusterDisplay::commands() const {return impl_->commands;}

bool SurfaceClusterDisplay::update(const VoxelSceneDisplay& display,const vsg::dmat4& chart,const vsg::Camera& camera,float errorPixels,uint64_t renderedFrames) {
    const auto projection=camera.projectionMatrix->transform();
    const auto worldToCamera=camera.viewMatrix->transform();
    const auto height=camera.getRenderArea().extent.height;
    bool changed=!impl_->lastRequestedFrame.has_value();
    for(auto& [id,batch]:impl_->batches) {
        const auto view=worldToCamera*(id.empty()?chart:chart*display.placements.at(id)->matrix);
        const auto eye=vsg::inverse(view)*vsg::dvec3{};
        const auto clip=vsg::transpose(projection*view);
        ClusterView data;
        const vsg::dvec4 planes[]={clip[3]+clip[0],clip[3]-clip[0],clip[3]+clip[1],clip[3]-clip[1],clip[2],clip[3]-clip[2]};
        for(int i=0;i<6;++i) {
            const auto& p=planes[i];const auto length=vsg::length(vsg::dvec3(p.x,p.y,p.z));
            data.planes[i]=length>1e-15?vsg::vec4(p/length):vsg::vec4(0,0,0,p.w>=0?1.f:-1.f);
        }
        const auto forward=vsg::normalize(vsg::dvec3(-view[0][2],-view[1][2],-view[2][2]));
        // Orthographic cameras retain exact surfaces. The depth-dependent
        // screen error calculation is only defined for perspective projection.
        data.eyeAndScale=vsg::vec4(vsg::vec3(eye),float(height*.5*std::abs(projection[1][1])));
        data.forwardAndError=vsg::vec4(vsg::vec3(forward),projection[3][3]==0.?errorPixels:0.f);
        auto& previous=batch.view->value();
        if(std::memcmp(&previous,&data,sizeof(data))!=0) {
            previous=data;
            changed=true;
        }
    }
    if(changed)impl_->lastRequestedFrame=renderedFrames;
    // Qt can throttle a render tick. Keep the dispatch pending until a frame is submitted.
    return impl_->lastRequestedFrame==renderedFrames;
}

std::vector<SurfaceClusterDisplay::IndirectDraws> SurfaceClusterDisplay::indirectDraws() const {
    std::vector<IndirectDraws> result;
    for(const auto& [id,batch]:impl_->batches)
        result.push_back({batch.indirect,batch.geometry->clusterCount,batch.geometry->triangles,batch.geometry->lowerTriangles});
    return result;
}
}
