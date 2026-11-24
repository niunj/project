#pragma once
void cloudLightFieldChecks(sensor::GpuContext& context,VoxelWeatherFrame frame,
    vsg::ref_ptr<vsg::vec4Array> packed,CloudLightCache& bulk,const std::string& production) {
    frame.moonPhase=0;auto parameters=vsg::vec4Array::create(15,vsg::vec4{});
    parameters->properties.dataVariance=vsg::DYNAMIC_DATA;(*parameters)[2]=vsg::vec4(frame.sun,0);(*parameters)[8].x=frame.opticalDepth;
    CloudLightField field(context.device(),packed,parameters,bulk);
    const auto original=frame;frame.clouds=false;field.update(frame);
    auto bake=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    bake->addChild(field.commands);context.compile(bake);context.execute(bake);frame=original;field.update(frame);
    const auto block=production.find("layout(set=CLOUD_DESCRIPTOR_SET,binding=6)"),end=production.find("} sparseCloud;",block);
    const auto funcs=production.find("const int OBJECT_BIN_SIDE"),functionsEnd=production.find("float cloudRadius",funcs);
    auto read=[](const char* path){std::ifstream f(std::string(SIM_SOURCE_ROOT)+path);std::ostringstream s;s<<f.rdbuf();check(bool(f),"cloud lighting include missing");return s.str();};
    const auto source=std::string("#version 460\n#define CLOUD_DESCRIPTOR_SET 0\nlayout(local_size_x=64)in;\n")+
        production.substr(block,end+14-block)+production.substr(funcs,functionsEnd-funcs)+function(production,"cloudExtinctionPerMetre")+
        read("/shaders/viewport/cloud_light_cache.glsl")+read("/shaders/viewport/cloud_light_field.glsl")+R"(
layout(set=0,binding=0,std430)buffer Output{vec4 result[];};
layout(set=0,binding=1)uniform Frame{vec4 weather[15];};
void main(){int i=int(gl_GlobalInvocationID.x),index=i%int(sparseCloud.objectOriginCount.w);
if(!cloudLightingReady()){result[i]=vec4(-1,-1,-1,0);return;}
int side=int(cloudLightField.data[1].x);
vec3 q=OBJECT_SHAPE_LO+(vec3(i%side,(i/side)%side,(i*7)%side)+.5)/float(side)*(OBJECT_SHAPE_HI-OBJECT_SHAPE_LO);
vec4 c=sparseCloud.objects[index*3],r=sparseCloud.objects[index*3+1],s=sparseCloud.objects[index*3+2];
vec2 horizontal=s.xy*((q.x-.20*q.y)*r.x)+vec2(-s.y,s.x)*(q.z*r.z);
vec3 p=c.xyz+vec3(horizontal.x,q.y*r.y,horizontal.y),key=objectAxes(weather[2].xyz);
vec3 reference=bulkLightDepths(p,key,normalize(vec3(key.x*2.,1.,key.z*2.)),weather[8].x);
result[i]=vec4(abs(objectCachedLight(p,index)-reference),1);})";
    auto shader=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",source);vsg::ShaderStages stages{shader};
    check(vsg::ShaderCompiler::create()->compile(stages),"cloud lighting field reader compile");
    HostBuffer output(*context.device(),512*sizeof(vsg::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {13,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {14,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {15,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto descriptors=vsg::DescriptorSet::create(dsl,vsg::Descriptors{
        vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(output.buffer,0,512*sizeof(vsg::vec4))},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorBuffer::create(parameters,1),vsg::DescriptorBuffer::create(packed,6),vsg::DescriptorImage::create(bulk.image,11),
        vsg::DescriptorBuffer::create(bulk.state,12),vsg::DescriptorBuffer::create(vsg::BufferInfoList{bulk.bakedKeyBuffer},13,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        vsg::DescriptorImage::create(field.image,14),vsg::DescriptorBuffer::create(field.state,15)});
    auto reader=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    reader->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,shader)));
    reader->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,descriptors));reader->addChild(vsg::Dispatch::create(8,1,1));
    reader->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    context.compile(reader);context.execute(reader);
    for(const auto& v:output.values<vsg::vec4>(512))check(v.w==0,"unrecorded lighting field was published");
    context.execute(bake);context.execute(reader);float error=0;
    for(const auto& v:output.values<vsg::vec4>(512)){check(v.w==1&&std::isfinite(v.x),"same-frame lighting field not resident");error=std::max({error,v.x,v.y,v.z});}
    check(error<.035f,"lighting field grid-centre depth exceeds half-float error budget");
    field.update(frame);check(!field.commands->children.at(1).node.cast<vsg::Switch>()->children[0].mask,"frozen lighting kept rebaking");
    frame.sun=vsg::normalize(vsg::vec3(.7f,.2f,.4f));(*parameters)[2]=vsg::vec4(frame.sun,0);parameters->dirty();field.update(frame);
    context.execute(reader);check(output.values<vsg::vec4>(512)[0].w==0,"sun change retained stale lighting field");
    context.execute(bake);context.execute(reader);check(output.values<vsg::vec4>(512)[0].w==1,"changed-sun field failed publication");
    context.clearGraphs();std::cout<<"PASS GPU cloud light field: same-frame publication, stale rejection, frozen reuse; grid-centre tau error="<<error<<'\n';
}
