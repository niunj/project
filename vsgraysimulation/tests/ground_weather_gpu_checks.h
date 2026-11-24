#pragma once
void groundWeatherGpuChecks(sensor::GpuContext& context) {
    std::ifstream file(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/terrain_weather.glsl");
    std::ostringstream text;text<<file.rdbuf();check(bool(file),"ground weather shader missing");
    const auto source=std::string("#version 460\nlayout(local_size_x=64)in;\n")+text.str()+R"(
layout(set=0,binding=0,std430)buffer Output{vec4 result[];};
void main(){int i=int(gl_GlobalInvocationID.x);vec3 base=vec3(.12,.29,.14),up=vec3(0,1,0);
vec3 p=vec3(6378000.+float(i%16)*27.,float(i/16)*35.,2000.);
result[4*i]=groundWeather(base,p,up,up,1.,0.,0.);
result[4*i+1]=groundWeather(base,p,up,up,1.,1.,0.);
result[4*i+2]=groundWeather(base,p,up,up,1.,0.,.5);
vec4 lo=groundWeather(base,p,up,up,1.,0.,.4999),hi=groundWeather(base,p,up,up,1.,0.,.5001);
result[4*i+3]=vec4(length(hi.rgb-lo.rgb),groundWeather(base,p,vec3(1,0,0),up,1.,0.,1.).g,
groundWeather(base,p,up,up,1.,0.,1.).g,groundWeather(base,p,vec3(.6,.8,0),up,1.,0.,.5).g);})";
    auto shader=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",source);
    vsg::ShaderStages stages{shader};check(vsg::ShaderCompiler::create()->compile(stages),"ground weather compute compile");
    HostBuffer output(*context.device(),1024*sizeof(vsg::vec4),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto dsl=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{dsl},vsg::PushConstantRanges{});
    auto descriptors=vsg::DescriptorSet::create(dsl,vsg::Descriptors{vsg::DescriptorBuffer::create(
        vsg::BufferInfoList{vsg::BufferInfo::create(output.buffer,0,1024*sizeof(vsg::vec4))},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)});
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    graph->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,shader)));
    graph->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,descriptors));
    graph->addChild(vsg::Dispatch::create(4,1,1));
    graph->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,
        vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    context.compile(graph);context.execute(graph);const auto values=output.values<vsg::vec4>(1024);
    float lo=10,hi=-10;
    for(int i=0;i<256;++i){const auto a=values[4*i],b=values[4*i+1],s=values[4*i+2],c=values[4*i+3];
        check(std::abs(a.x-.12f)<1e-6&&std::abs(a.y-.29f)<1e-6&&a.w==0,"dry ground changed imagery");
        check(b.x<=a.x&&b.y<=a.y&&b.w>0,"wet ground did not retain diffuse darkening");
        check(std::isfinite(s.x)&&s.x>=a.x&&s.y>=a.y,"non-finite or dark snow reflectance");
        check(c.x<.004&&std::abs(c.y-.29f)<1e-6&&c.z>c.y,"snow transition popped or steep slopes accumulated full snow");
        check(std::abs(c.w-s.y)<.01,"moderate slope introduced a sharp snow boundary");
        lo=std::min(lo,s.y);hi=std::max(hi,s.y);
    }
    check(hi-lo>.05,"partial snow erased all surface variation");context.clearGraphs();
    std::cout<<"PASS GPU ground weather: dry identity, wet film, continuous patch/slope snow and finite surface variation\n";
}
