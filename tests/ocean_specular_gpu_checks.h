#pragma once
// Execute production BRDF helpers, including an antiparallel light/view pair.
inline void checkMarineSpecular(rockyenv::sensor::GpuContext& context) {
    std::ifstream input(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/marine_specular.glsl");
    std::ostringstream functions; functions<<input.rdbuf();
    require(bool(input),"production marine specular helpers missing");
    std::ifstream rain(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/rain_water.glsl");
    functions<<'\n'<<rain.rdbuf();
    require(bool(rain),"production rain impact helper missing");
    const auto source=std::string("#version 450\nlayout(local_size_x=64)in;\n")+functions.str()+R"(
layout(set=0,binding=0)buffer Output{vec4 result[];};
void main(){
    uint i=gl_GlobalInvocationID.x;
    float base=.08+.01*float(i),amount=float(i)/63.;
    float variance=waterNormalVariance(vec3(amount*.4,0,0),vec3(0,0,amount*.3));
    float rough=waterSpecularRoughness(base,variance);
    vec3 L=vec3(0,1,0),V=i%2==0?vec3(0,-1,0):normalize(vec3(.3,.1,.5));
    vec3 H=waterHalfVector(L,V,vec3(0,1,0));
    result[i]=vec4(rough,waterGgx(1.,rough),waterGgx(1.,base),length(H));
    vec2 point=vec2(10.43+.09*float(i%8),-14.31+.09*float(i/8));
    vec2 impact=rainImpactSlope(point,1.32,.95,.012);
    vec2 later=rainImpactSlope(point,1.49,.95,.012);
    result[64u+i]=vec4(length(impact),length(impact-later),
                       length(rainImpactSlope(point,1.32,0.,.012)),
                       length(rainImpactSlope(point,1.32,.95,.3)));
})";
    auto shader=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",source);
    vsg::ShaderStages stages{shader};
    require(vsg::ShaderCompiler::create()->compile(stages),"marine specular fixture compile");
    auto* device=context.device();
    constexpr size_t bytes=128*sizeof(vsg::vec4);
    auto buffer=vsg::Buffer::create(bytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_SHARING_MODE_EXCLUSIVE);
    buffer->compile(device);
    auto memory=vsg::DeviceMemory::create(device,buffer->getMemoryRequirements(device->deviceID),
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buffer->bind(memory,0);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto descriptor=vsg::DescriptorBuffer::create(
        vsg::BufferInfoList{vsg::BufferInfo::create(buffer,0,bytes)},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    auto set=vsg::DescriptorSet::create(dsl,vsg::Descriptors{descriptor});
    auto commands=vsg::Commands::create();
    commands->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,shader)));
    commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,set));
    commands->addChild(vsg::Dispatch::create(1,1,1));
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(device),
        device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    graph->addChild(commands);context.compile(graph);context.execute(graph);
    void* mapped=nullptr;
    require(memory->map(0,bytes,0,&mapped)==VK_SUCCESS,"marine specular GPU readback failed");
    std::array<vsg::vec4,128> output;
    std::memcpy(output.data(),mapped,bytes);memory->unmap();
    float maximumImpact=0,maximumMotion=0;
    for(size_t i=0;i<64;++i){
        const auto& p=output[i];const float base=.08f+.01f*i;
        require(std::isfinite(vsg::length(p)),"nonfinite marine reflection at degenerate half vector");
        require(p.x>=base-1e-6f&&p.x<=1.f&&p.y<=p.z*1.00001f,
            "normal variance sharpened a subpixel water highlight");
        require(std::abs(p.w-1.f)<1e-6f,"marine half vector lost its unit fallback");
        const auto& ripple=output[64+i];
        require(std::isfinite(vsg::length(ripple))&&ripple.x<=.25f&&ripple.z==0&&ripple.w==0,
                "rain impacts affected dry or unresolved water or destabilized normals");
        maximumImpact=std::max(maximumImpact,ripple.x);
        maximumMotion=std::max(maximumMotion,ripple.y);
    }
    require(std::abs(output[0].x-.08f)<1e-6f,"flat normal changed material roughness");
    require(maximumImpact>.002f&&maximumMotion>.002f,"rain ripples did not appear and evolve");
    std::cout<<"PASS GPU marine specular: finite half vectors, stable GGX and variance filtering\n";
}
