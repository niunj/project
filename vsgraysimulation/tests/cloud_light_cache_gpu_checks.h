#pragma once
void cloudLightCacheChecks(sensor::GpuContext& context,VoxelWeatherFrame frame,
                           vsg::ref_ptr<vsg::vec4Array> packed,const std::string& production) {
    auto* device=context.device();
    // A dormant cloud must acquire its profile before its mass envelope grows.
    (*packed)[16].w=0;packed->dirty();
    CloudLightCache cache(device,packed);cache.update(frame);
    const int count=int((*packed)[12].w);
    check(int((*cache.state)[1].x)==count&&(*cache.state)[38].x==0,
          "unrecorded lighting jobs were published or omitted");
    auto bake=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device),
        device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    bake->addChild(cache.commands);context.compile(bake);context.execute(bake);cache.update(frame);
    check((*cache.state)[1].x==0&&(*cache.state)[38].x==1,"recorded lighting atlas did not become resident");
    (*packed)[16].w=.08f;(*packed)[16].x+=1200;packed->dirty();cache.update(frame);
    check((*cache.state)[1].x==0,"cloud translation or mass envelope rebuilt normalized light density");
    const auto block=production.find("layout(set=CLOUD_DESCRIPTOR_SET,binding=6)");
    const auto end=production.find("} sparseCloud;",block),funcs=production.find("const int OBJECT_BIN_SIDE");
    const auto functionsEnd=production.find("float cloudRadius",funcs);
    std::ifstream file(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/cloud_light_cache.glsl");
    std::ostringstream cacheSource;cacheSource<<file.rdbuf();check(bool(file),"lighting cache shader missing");
    const auto source=std::string("#version 460\n#define CLOUD_DESCRIPTOR_SET 0\nlayout(local_size_x=64)in;\n")+
        production.substr(block,end+14-block)+production.substr(funcs,functionsEnd-funcs)+cacheSource.str()+R"(
layout(set=0,binding=0)buffer Output{vec4 result[];};
void main(){int i=int(gl_GlobalInvocationID.x),index=i%int(sparseCloud.objectOriginCount.w);
vec3 p=sparseCloud.objects[index*3].xyz+vec3((i%9-4)*225,(i%7-3)*170,(i%11-5)*190);
result[i]=vec4(cachedObjectCondensate(p,index),objectCondensate(p,index,0.),cloudLightResident(index)?1.:0.,0);})";
    auto shader=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",source);
    vsg::ShaderStages stages{shader};check(vsg::ShaderCompiler::create()->compile(stages),"lighting cache reader compile");
    HostBuffer output(*device,512*sizeof(vsg::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto descriptors=vsg::DescriptorSet::create(dsl,vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(output.buffer,0,512*sizeof(vsg::vec4))},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(packed,6),vsg::DescriptorImage::create(cache.image,11),
        vsg::DescriptorBuffer::create(cache.state,12),vsg::DescriptorBuffer::create(vsg::BufferInfoList{cache.bakedKeyBuffer},13,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto commands=vsg::Commands::create();
    commands->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,shader)));
    commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,descriptors));
    commands->addChild(vsg::Dispatch::create(8,1,1));
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    auto reader=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device),
        device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));reader->addChild(commands);
    context.compile(reader);context.execute(reader);
    auto values=output.values<vsg::vec4>(512);double maxError=0,meanError=0;
    for(const auto& v:values){check(std::isfinite(v.x)&&v.x>=0,"invalid lighting atlas density");
        maxError=std::max(maxError,double(std::abs(v.x-v.y)));meanError+=std::abs(v.x-v.y)/values.size();}
    std::cout<<"GPU cloud light cache diagnostic max/mean="<<maxError<<'/'<<meanError<<'\n';
    check(maxError<.02&&meanError<.002,"far-lighting density LOD exceeded its display error budget");
    (*packed)[18].z+=.02f;packed->dirty();cache.update(frame);
    check((*cache.state)[1].x==1&&(*cache.state)[38].x==0,"changed cloud shape retained stale lighting");
    context.execute(reader);values=output.values<vsg::vec4>(512);
    for(size_t i=0;i<values.size();i+=count)
        check(values[i].z==0&&std::abs(values[i].x-values[i].y)<1e-6,"invalid lighting tile did not fall back to exact density");
    // Bake a changed shape and consume it before any CPU update/commit.
    context.execute(bake);context.execute(reader);values=output.values<vsg::vec4>(512);
    check((*cache.state)[38].x==0,"test accidentally committed same-frame cache on CPU");
    for(size_t i=0;i<values.size();i+=count)check(values[i].z==1,"recorded GPU key did not publish same-frame lighting");
    cache.update(frame);cloudLightFieldChecks(context,frame,packed,cache,production);
    frame.clouds=false;cache.update(frame);
    check((*cache.state)[1].x==0,"hidden clouds kept lighting bake jobs active");
    std::cout<<"PASS GPU cloud light cache: far-density max/mean error="<<maxError<<'/'<<meanError
             <<"; dormant growth, pose/mass reuse, recorded publication and stale fallback\n";
    context.clearGraphs();
}
