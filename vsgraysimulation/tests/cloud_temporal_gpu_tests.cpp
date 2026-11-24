#include "../lib/voxel_earth/src/EarthShader.h"
#include "../env_modules/src/WeatherHistory.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <rocky_env/GpuContext.h>
#include <vsg/all.h>
using namespace rockyenv::sensor;
void require(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
int main() try {
    rockyenv::VoxelWeatherFrame a,b;a.overcastAmount=b.overcastAmount=0;
    a.fogVisibility=b.fogVisibility=10000;b.cover+=.001f;b.cloudBase+=1;b.fogVisibility-=10;
    require(rockyenv::weatherHistoryWeight(a,b)>.7f,"smooth weather unnecessarily discarded all history");
    b.cover+=.15f;require(rockyenv::weatherHistoryWeight(a,b)==0,"abrupt weather retained history");
    b=a;b.cloudObjects=!a.cloudObjects;require(rockyenv::weatherHistoryWeight(a,b)==0,"cloud architecture change retained history");
    b=a;b.fogVisibility=0;require(rockyenv::weatherHistoryWeight(a,b)==0,"fog removal retained stale history");
    GpuContext context;
    context.initialize(false);
    auto *device = context.device();
    constexpr uint32_t side = 16;
    auto current = vsg::vec4Array2D::create(side, side), moments = vsg::vec4Array2D::create(side, side),
         old = vsg::vec4Array2D::create(side, side), oldDepth = vsg::vec4Array2D::create(side, side);
    for (auto data : {current, moments, old, oldDepth}) {
        data->properties.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        data->properties.dataVariance = vsg::DYNAMIC_DATA;
    }
    auto output = [&](VkFormat format) {
        auto image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = format;
        image->extent = {side, side, 1};
        image->mipLevels = 1;
        image->arrayLayers = 1;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        return vsg::createImageView(device, image, VK_IMAGE_ASPECT_COLOR_BIT);
    };
    auto color = output(VK_FORMAT_R16G16B16A16_SFLOAT), depth = output(VK_FORMAT_R32G32B32A32_SFLOAT);
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod = 0;
    auto params = vsg::vec4Array::create(16, vsg::vec4{});
    params->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto motions=vsg::vec4Array::create(433,vsg::vec4{});
    motions->properties.dataVariance=vsg::DYNAMIC_DATA;
    vsg::DescriptorSetLayoutBindings bindings;
    for (uint32_t b = 0; b < 6; ++b)
        bindings.push_back(
            {b, b < 4 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
             VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    bindings.push_back({10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    bindings.push_back({11,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    auto dsl = vsg::DescriptorSetLayout::create(bindings);
    auto layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl}, vsg::PushConstantRanges{});
    vsg::Descriptors descriptors;
    uint32_t binding = 0;
    for (auto data : {current, moments, old, oldDepth})
        descriptors.push_back(vsg::DescriptorImage::create(sampler, data, binding++));
    descriptors.push_back(vsg::DescriptorImage::create(
        vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, color, VK_IMAGE_LAYOUT_GENERAL), 4, 0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
    descriptors.push_back(vsg::DescriptorImage::create(
        vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{}, depth, VK_IMAGE_LAYOUT_GENERAL), 5, 0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
    descriptors.push_back(vsg::DescriptorBuffer::create(params, 10));
    descriptors.push_back(vsg::DescriptorBuffer::create(motions,11));
    auto set = vsg::DescriptorSet::create(dsl, descriptors);
    auto pipeline =
        vsg::ComputePipeline::create(layout, vsgraysim::earth::earthShader("cloud_temporal_resolve.comp"));
    auto commands = vsg::Commands::create();
    auto init = vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0);
    for (auto view : {color, depth})
        init->imageMemoryBarriers.push_back(vsg::ImageMemoryBarrier::create(
            0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, view->image,
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}));
    commands->addChild(init);
    commands->addChild(vsg::BindComputePipeline::create(pipeline));
    commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, set));
    commands->addChild(vsg::Dispatch::create(2, 2, 1));
    auto graph =
        vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device),
                                  device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    graph->addChild(commands);
    std::vector<vsg::vec4> result(side * side);
    auto projection = vsg::Perspective::create(60., 1., 1., 10000.)->transform();
    auto matrix = [&](int at, const vsg::dmat4 &value) {
        for (int c = 0; c < 4; ++c)
            (*params)[at + c] = vsg::vec4(value[c]);
    };
    auto execute = [&](bool eligible, float oldRange, float shift, bool clear, bool alphaMismatch) {
        for (uint32_t y = 0; y < side; ++y)
            for (uint32_t x = 0; x < side; ++x) {
                (*current)(x, y) = clear ? vsg::vec4{} : vsg::vec4(.2f + .01f * x, .2f, .2f, .5f);
                (*moments)(x, y) = clear ? vsg::vec4{} : vsg::vec4(500, 400, 0, 0);
                (*old)(x, y) = vsg::vec4(.2f + .01f * x, .2f, .2f, alphaMismatch ? .9f : .5f);
                (*oldDepth)(x, y) = vsg::vec4(oldRange, 800, 2.f + .2f * x, 0);
            }
        auto previous = projection;
        previous[2][0] -= shift;
        matrix(0, vsg::inverse(projection));
        matrix(4, previous);
        (*params)[8] = vsg::vec4(side, side, eligible ? 1.f : 0.f, 0);
        (*params)[11] = vsg::vec4(.88f, 1, 0, 0);
        (*params)[12] = vsg::vec4(side, side, 0, 0);
        for (auto data : {current, moments, old, oldDepth})
            data->dirty();
        params->dirty();
        context.execute(graph);
        context.readbackBatch({{depth.get(), nullptr, VK_IMAGE_LAYOUT_GENERAL,
                                result.size() * sizeof(vsg::vec4), result.data()}});
        for (const auto &p : result)
            require(std::isfinite(vsg::length(p)), "nonfinite temporal output");
        return result[8 * side + 8];
    };
    context.compile(graph);
    auto p = execute(true, 1000, 0, false, false);
    require(std::abs(p.z - 4.6f) < .002 && p.w > .5f, "static history was not reused");
    p = execute(true, 1000, .25f, false, false);
    require(std::abs(p.z - 5.f) < .002 && p.w > .5f, "reprojection did not sample the prior projected pixel");
    p = execute(true, 4000, 0, false, false);
    require(p.z == 1 && p.w == 0, "depth-disoccluded history leaked");
    // Half a pixel between two unrelated layers reproduces the current mean
    // range when interpolated, even though neither history ray matches it.
    execute(true,1000,.0625f,false,false);
    for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x)
        (*oldDepth)(x,y).x=x<=8?650.f:1350.f;
    oldDepth->dirty();context.execute(graph);
    context.readbackBatch({{depth.get(),nullptr,VK_IMAGE_LAYOUT_GENERAL,
        result.size()*sizeof(vsg::vec4),result.data()}});
    p=result[8*side+8];
    require(p.z==1.f&&p.w==0.f,"unrelated history layers blended to a false matching range");
    p = execute(true, 1000, 0, false, true);
    require(p.z == 1 && p.w == 0, "opacity-disoccluded history leaked");
    p = execute(false, 1000, 0, false, false);
    require(p.z == 1 && p.w == 0, "invalidated history leaked");
    p = execute(true, 1000, 4, false, false);
    require(p.z == 1 && p.w == 0, "out-of-frustum history leaked");
    p = execute(true, 1000, 0, true, false);
    require(p == vsg::vec4{}, "newly clear pixels retained old cloud");
    // Opaque wind-driven cloudlets can change radiance without changing alpha.
    execute(true,1000,0,false,false);
    for(auto& pixel:*old)pixel=vsg::vec4(1.5f,1.5f,1.5f,.5f);
    old->dirty();context.execute(graph);
    context.readbackBatch({{depth.get(),nullptr,VK_IMAGE_LAYOUT_GENERAL,
        result.size()*sizeof(vsg::vec4),result.data()}});
    require(result[8*side+8].w==0.f,"changed cloud illumination retained stale opaque history");
    // Exercise subpixel cubic reconstruction across a premultiplied HDR edge.
    (*params)[8].z=0;(*params)[11].z=.25f;params->dirty();
    for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x){
        const float alpha=x<8?.1f:.9f;
        (*current)(x,y)=vsg::vec4(2.f*alpha,.5f*alpha,.25f*alpha,alpha);
        (*moments)(x,y)=vsg::vec4(1000.f*alpha,800.f*alpha,0,0);
    }
    current->dirty();moments->dirty();context.execute(graph);
    std::vector<uint16_t> rgba(side*side*4);
    context.readbackBatch({{color.get(),nullptr,VK_IMAGE_LAYOUT_GENERAL,
        rgba.size()*sizeof(uint16_t),rgba.data()}});
    auto half=[](uint16_t h){
        const int e=(h>>10)&31,m=h&1023;
        return (h&32768?-1.:1.)*(e?std::ldexp(1.+m/1024.,e-15):std::ldexp(double(m),-24));
    };
    for(size_t i=0;i<side*side;++i){
        const float r=half(rgba[4*i]),alpha=half(rgba[4*i+3]);
        require(alpha>=.099f&&alpha<=.901f&&std::abs(r-2*alpha)<.003f,
            "cubic cloud reconstruction introduced an HDR/opacity bright rim");
    }
    // A resolved cloud silhouette still has one real occlusion distance
    // when the raw bilinear footprint overlaps another cloud layer.
    for(uint32_t y=0;y<side;++y)for(uint32_t x=0;x<side;++x){
        const bool farLayer=x>=8;
        (*current)(x,y)=vsg::vec4(.2f,.2f,.2f,.5f);
        (*moments)(x,y)=farLayer?vsg::vec4(500.f,400.f,2.f,0.f):vsg::vec4(120.f,100.f,1.f,0.f);
    }
    current->dirty();moments->dirty();context.execute(graph);
    context.readbackBatch({{depth.get(),nullptr,VK_IMAGE_LAYOUT_GENERAL,
        result.size()*sizeof(vsg::vec4),result.data()}});
    p=result[8*side+8];
    require(std::abs(p.x-1000.f)<.002f&&std::abs(p.y-800.f)<.002f,
        "silhouette used an interpolated depth between two cloud layers");
    execute(true,1000,0,false,false);
    (*motions)[0]=vsg::vec4(1,2,0,0);
    (*motions)[1]=vsg::vec4(1,0,0,125);(*motions)[2]=vsg::vec4(0,1,0,0);(*motions)[3]=vsg::vec4(0,0,1,0);
    (*motions)[4]=vsg::vec4(1,0,0,-125);(*motions)[5]=vsg::vec4(0,1,0,0);(*motions)[6]=vsg::vec4(0,0,1,0);
    motions->dirty();
    auto motionResult=[&](float id){
        for(auto& m:*moments)m.z=id;
        moments->dirty();context.execute(graph);
        context.readbackBatch({{depth.get(),nullptr,VK_IMAGE_LAYOUT_GENERAL,result.size()*sizeof(vsg::vec4),result.data()}});
        return result[8*side+8];
    };
    auto right=motionResult(1),left=motionResult(2);
    require(right.w>.5f&&left.w>.5f&&right.z>4.8f&&left.z<4.5f,
        "independent cloud transforms used the global mean wind");
    (*motions)[1]=vsg::vec4{};motions->dirty();p=motionResult(1);
    require(p.z==1&&p.w==0,"reborn or remapped cloud retained stale transform history");
    std::cout << "PASS production temporal GPU resolve: projection, reuse, depth/opacity/clear/frustum "
                 "rejection and finite output\n";
} catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
