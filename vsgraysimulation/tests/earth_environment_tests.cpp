#include <rocky_env/EnvApi.h>
#include <rocky_env/CloudStepClock.h>
#include <rocky_env/CloudField.h>
#include <rocky_env/GpuContext.h>
#include <rocky_env/SceneSource.h>
#include <rocky_env/GpuVoxelCloud.h>
#include <rocky_env/SparseCloudVolume.h>
#include <rocky_env/VoxelWeather.h>
#include "OceanFFTField.h"
#include "WaveField.h"
#include "CloudLightCache.h"
#include "CloudLightField.h"
#include <vsg/all.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <filesystem>
#include <chrono>
using namespace rockyenv;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string function(const std::string& source,const char* name){
    const auto begin=source.find(std::string("float ")+name+"(");check(begin!=std::string::npos,"production cloud function missing");
    auto end=source.find('{',begin)+1;int depth=1;
    while(depth&&end<source.size()){depth+=(source[end]=='{')-(source[end]=='}');++end;}
    check(!depth,"production cloud function unbalanced");return source.substr(begin,end-begin)+"\n";
}
struct HostBuffer {
    vsg::ref_ptr<vsg::Buffer> buffer;
    vsg::ref_ptr<vsg::DeviceMemory> memory;
    HostBuffer(vsg::Device& device,size_t bytes,VkBufferUsageFlags usage){
        buffer=vsg::Buffer::create(bytes,usage,VK_SHARING_MODE_EXCLUSIVE);buffer->compile(&device);
        memory=vsg::DeviceMemory::create(&device,buffer->getMemoryRequirements(device.deviceID),VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buffer->bind(memory,0);
    }
    template<class T>std::vector<T> values(size_t count){
        void* mapped=nullptr;check(memory->map(0,count*sizeof(T),0,&mapped)==VK_SUCCESS,"map host data");
        std::vector<T> result(count);std::memcpy(result.data(),mapped,count*sizeof(T));memory->unmap();return result;
    }
};
#include "cloud_light_field_gpu_checks.h"
#include "cloud_light_cache_gpu_checks.h"
#include "ground_weather_gpu_checks.h"
void cloudClockContracts(){
    CloudStepClock fine,coarse;
    uint64_t a=fine.advance(0,true),b=coarse.advance(0,true);
    for(int i=1;i<=240;++i)a+=fine.advance(i/240.,true);
    for(int i=1;i<=10;++i)b+=coarse.advance(i/10.,true);
    check(a==21 && a==b,"cloud physics changes with render cadence");
    check(fine.advance(1.,true)==0,"paused clock advanced cloud physics");
    check(fine.advance(1.,false)==0 && fine.advance(1.,true)==1,"cloud resume did not bootstrap");
    CloudStepClock jump;jump.advance(0,true);
    auto count=jump.advance(5,true);check(count==8 && jump.pendingSeconds()>3.,"cloud jump discarded time debt");
    while(jump.pendingSeconds()>1e-8)count+=jump.advance(5,true);
    check(count==100,"cloud catch-up lost substeps");
    check(jump.advance(2,true)==1 && jump.pendingSeconds()==0.,"cloud rewind retained old time debt");
    std::cout<<"PASS: fixed cloud cadence, pause, catch-up and rewind\n";
}
void sparseWindTransport() {
    SparseCloudConfig config;
    config.voxelSizeMetres=32.f;
    config.maxBricks=8;
    SparseCloudVolume volume(config);
    CloudVoxelState filament;
    filament.liquidWaterKgM3=.001f;
    filament.velocity={8.f,0.f,0.f};
    volume.setVoxel(7,3,3,filament);
    CloudDynamics wind;
    wind.ambientWind=filament.velocity;
    wind.condensationRate=0.f;
    wind.evaporationRate=0.f;
    wind.vorticity=0.f;
    volume.step(.25f,wind);
    const auto crest=volume.voxel(7,3,3).liquidWaterKgM3;
    const auto downwind=volume.voxel(8,3,3).liquidWaterKgM3;
    const auto upwind=volume.voxel(6,3,3).liquidWaterKgM3;
    check(crest>.965f*filament.liquidWaterKgM3&&crest<=filament.liquidWaterKgM3,
          "sparse wind blurred a filament or created a new water peak");
    check(downwind>0.f&&downwind<crest&&upwind==0.f,
          "sparse wind did not carry the filament across the downwind page face");
}
void graphicsShaderContracts(){
    auto compiler=vsg::ShaderCompiler::create();
    const auto options=vsg::Options::create();
    const auto stage=[&](VkShaderStageFlagBits type,const char* name){
        auto result=vsg::ShaderStage::read(type,"main",std::string(SIM_SOURCE_ROOT)+"/shaders/env/"+name,options);
        check(bool(result),"environment shader source missing");
        return result;
    };
    for(const char* vertex:{"ocean.vert","ocean_fft.vert","cloud_volume.vert","sensor_rad.vert"}){
        const char* fragment=std::string(vertex)=="cloud_volume.vert"?"cloud_volume.frag":std::string(vertex)=="sensor_rad.vert"?"sensor_vis.frag":"ocean.frag";
        vsg::ShaderStages stages{stage(VK_SHADER_STAGE_VERTEX_BIT,vertex),stage(VK_SHADER_STAGE_FRAGMENT_BIT,fragment)};
        if(!compiler->compile(stages))throw std::runtime_error(std::string("environment graphics shader interface mismatch: ")+vertex+"/"+fragment);
    }
    vsg::ShaderStages rasterIR{stage(VK_SHADER_STAGE_VERTEX_BIT,"sensor_rad.vert"),
        stage(VK_SHADER_STAGE_FRAGMENT_BIT,"sensor_rad.frag")};
    check(compiler->compile(rasterIR),"production raster infrared shader compile");
    vsg::ShaderStages compute{stage(VK_SHADER_STAGE_COMPUTE_BIT,"band_shade.comp")};
    check(compiler->compile(compute),"production band shader compile");
    std::cout<<"PASS: production cloud/ocean/sensor shader stage interfaces\n";
}
void cloudGpuParity(sensor::GpuContext& context){
    constexpr size_t count=4096;
    std::ifstream file(std::string(SIM_SOURCE_ROOT)+"/shaders/env/cloud_volume.frag");std::ostringstream input;input<<file.rdbuf();const auto production=input.str();
    std::string shader="#version 460\nlayout(local_size_x=64)in;layout(set=0,binding=0)buffer Output{float result[];};struct Env{vec4 wind;vec4 camPosTime;};Env env;float vBase,vThick;\n";
    for(auto name:{"hash3","noise3","fbm3","coverageThreshold","cloudDensityField"})shader+=function(production,name);
    shader+=R"(
void main(){uint i=gl_GlobalInvocationID.x;if(i>=4096)return;env.wind=vec4(.6,.8,12,0);env.camPosTime=vec4(0,0,0,27.25);vBase=1200;vThick=600;
vec3 p=vec3((float(i%17)-8)*180,1100+float((i/17)%17)*50,(float((i/289)%17)-8)*170);
result[i]=cloudDensityField(p,.85,.8,3);})";
    auto stage=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",shader);
    auto compiler=vsg::ShaderCompiler::create();vsg::ShaderStages stages{stage};check(compiler&&compiler->compile(stages),"cloud parity shader compile");
    auto& device=*context.device();HostBuffer output(device,count*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto layout=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto pipelineLayout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{layout},vsg::PushConstantRanges{});
    auto descriptor=vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(output.buffer,0,count*sizeof(float))},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    auto set=vsg::DescriptorSet::create(layout,vsg::Descriptors{descriptor});
    auto commands=vsg::Commands::create();commands->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(pipelineLayout,stage)));
    commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,set));commands->addChild(vsg::Dispatch::create(count/64,1,1));
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(&device),uint32_t(device.getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)));graph->addChild(commands);
    context.compile(graph);context.execute(graph);const auto actual=output.values<float>(count);float maximum=0;size_t positive=0;
    for(size_t i=0;i<count;++i){const auto expected=cloudfield::density((float(i%17)-8)*180,1100+float((i/17)%17)*50,(float((i/289)%17)-8)*170,1200,600,.85,.8,27.25,.6,.8,12);maximum=std::max(maximum,std::abs(actual[i]-expected));if(actual[i]>.01)++positive;}
    std::cout<<"Cloud CPU/GPU maximum density error="<<maximum<<", positive="<<positive<<'\n';check(maximum<2e-4f&&positive>10,"CPU cloud density differs from actual production shader");context.clearGraphs();
}
std::vector<float> sensorDensityDispatch(sensor::GpuContext& context,const std::string& shader,vsg::ref_ptr<vsg::floatArray> parameters,uint32_t binding,size_t count){
    auto stage=vsg::ShaderStage::create(VK_SHADER_STAGE_COMPUTE_BIT,"main",shader);vsg::ShaderStages stages{stage};check(vsg::ShaderCompiler::create()->compile(stages),"production sensor density shader compile");
    auto& device=*context.device();HostBuffer output(device,count*sizeof(float),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto layout=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto pipelineLayout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{layout},vsg::PushConstantRanges{});
    auto result=vsg::DescriptorBuffer::create(vsg::BufferInfoList{vsg::BufferInfo::create(output.buffer,0,count*sizeof(float))},0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    auto set=vsg::DescriptorSet::create(layout,vsg::Descriptors{result,vsg::DescriptorBuffer::create(parameters,binding)});
    auto commands=vsg::Commands::create();commands->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(pipelineLayout,stage)));commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,set));commands->addChild(vsg::Dispatch::create(uint32_t(count/2/64),1,1));
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT)));
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(&device),uint32_t(device.getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)));graph->addChild(commands);context.compile(graph);context.execute(graph);const auto values=output.values<float>(count);context.clearGraphs();return values;
}
void objectCloudParity(sensor::GpuContext& context) {
    VoxelWeather weather;
    VoxelWeatherFrame frame;
    frame.cloudObjects=true;
    frame.eye=frame.cloudAnchor={6378137.,0.,0.};
    frame.cloudBase=1500.f;
    frame.cloudHeight=7000.f;
    frame.cover=.88f;
    frame.windSpeed=8.f;
    frame.seconds=27.25;
    weather.update(frame);
    const auto packed=planetSparseCloudParameters().cast<vsg::vec4Array>();
    int developed=0,active=0;
    float minAspect=100.f,maxAspect=0.f;
    for(int i=0;i<int((*packed)[12].w);++i) {
        const auto& centre=(*packed)[16+3*i];
        const auto& radii=(*packed)[17+3*i];
        if(centre.w<=.005f)continue;
        const float aspect=radii.y/std::sqrt(radii.x*radii.z);
        minAspect=std::min(minAspect,aspect);maxAspect=std::max(maxAspect,aspect);
        if(aspect>.33f)++developed;
        ++active;
    }
    check(active>20&&developed>=active/5&&maxAspect-minAspect>.15f,
          "fair-weather cloud population collapsed into uniformly flat ellipsoids");
    std::cout<<"PASS: cloud development diversity aspect=["<<minAspect<<','<<maxAspect
             <<"] developed="<<developed<<'/'<<active<<'\n';
    auto words=vsg::floatArray::create(packed->size()*4);
    for(size_t i=0;i<packed->size();++i)
        for(int component=0;component<4;++component)(*words)[4*i+component]=(*packed)[i][component];
    std::ifstream file(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/planet_cloud_shadow.glsl");
    std::ostringstream source;source<<file.rdbuf();
    const auto production=source.str();
    const auto block=production.find("layout(set=CLOUD_DESCRIPTOR_SET,binding=6)");
    const auto end=production.find("} sparseCloud;",block);
    const auto funcs=production.find("const int OBJECT_BIN_SIDE");
    const auto functionsEnd=production.find("float cloudRadius",funcs);
    check(block!=std::string::npos&&end!=std::string::npos&&funcs!=std::string::npos&&
          functionsEnd!=std::string::npos,"shared object cloud shader source unavailable");
    std::ifstream commonFile(std::string(SIM_SOURCE_ROOT)+"/shaders/viewport/planet_cloud_common.glsl");
    std::ostringstream commonText;commonText<<commonFile.rdbuf();const auto common=commonText.str();
    const auto rayBegin=common.find("vec2 objectRayBox"),rayEnd=common.find("vec3 cloudSunlight",rayBegin);
    check(rayBegin!=std::string::npos&&rayEnd!=std::string::npos,"production cloud support traversal missing");
    const auto shader=std::string(
        "#version 460\n#define CLOUD_DESCRIPTOR_SET 0\nlayout(local_size_x=64)in;"
        "layout(set=0,binding=0)buffer Output{float result[];};\n")+
        production.substr(block,end+14-block)+"\n"+production.substr(funcs,functionsEnd-funcs)+
        common.substr(rayBegin,rayEnd-rayBegin)+R"(
void main() {
    int i=int(gl_GlobalInvocationID.x);
    if(i>=512)return;
    int row=3*(i%int(sparseCloud.objectOriginCount.w));
    vec3 offset=vec3((i%9-4)*225,(i%7-3)*170,(i%11-5)*190);
    vec3 p=sparseCloud.objects[row].xyz+offset;
    result[i]=localObjectCloudDensity(p,1.);
    result[512+i]=i%2==0 ? localObjectCloudDensity(vec3(60000.,2500.,0.),1.) : localObjectCloudDensity(p,0.);
    float all=0.;
    if(validObjectBin(cloudObjectBin(p))&&p.y>=sparseCloud.objectUpBase.w&&p.y<=sparseCloud.objectNorthTop.w){
        for(int j=0;j<int(sparseCloud.objectOriginCount.w);++j)all=max(all,objectCondensate(p,j,1.));
        all*=smoothstep(0.,5000.,sparseCloud.objectEastExtent.w-max(abs(p.x),abs(p.z)));
    }
    result[1024+i]=all;
    float skipped=0.;ivec2 cell=cloudObjectBin(p);
    if(validObjectBin(cell)){
        vec3 direction=normalize(vec3(.37,.22,.90)),eye=p-direction*2000.;
        vec2 edge=(vec2(cell)+1.)*objectBinWidth()-sparseCloud.objectEastExtent.w;
        vec2 exits=(edge-eye.xz)/direction.xz;
        float end=max(2000.,min(2600.,min(exits.x,exits.y)));
        float next=objectNextSupport(eye,direction,cloudObjectList(cell),2000.,end);
        if(result[i]>1e-5&&next>2000.01)skipped=1.;
        if(next>2000.01)for(int k=0;k<8;++k)
            skipped=max(skipped,localObjectCloudDensity(eye+direction*mix(2000.,next,(float(k)+.5)/8.),1.));
    }
    result[1536+i]=skipped;
})";
    const auto actual=sensorDensityDispatch(context,shader,words,6,2048);
    const auto vector=[](const vsg::vec4& v){return vsg::dvec3(v.x,v.y,v.z);};
    const auto origin=vector((*packed)[12]),east=vector((*packed)[13]),
               up=vector((*packed)[14]),north=vector((*packed)[15]);
    const int count=int((*packed)[12].w);
    float maxError=0.f;
    int wet=0,detail=0;
    for(int i=0;i<512;++i) {
        const auto p=vector((*packed)[16+3*(i%count)])+
            vsg::dvec3((i%9-4)*225,(i%7-3)*170,(i%11-5)*190);
        const float expected=cloudDensity(frame,origin+east*p.x+up*p.y+north*p.z);
        maxError=std::max(maxError,std::abs(actual[i]-expected));
        check(std::abs(actual[i]-actual[1024+i])<1e-6f,"spatial bins omitted a cloud from the full GPU scan");
        check(actual[1536+i]<1e-6f,"ray support traversal skipped occupied condensate");
        if(actual[i]>.001f)++wet;
        if(i%2==0)check(actual[512+i]==0.f,"object cloud escaped the regional domain");
        else if(std::abs(actual[512+i]-actual[i])>.0001f)++detail;
    }
    check(wet>50&&detail>15&&maxError<4.e-4f,"object cloud detail or CPU/GPU density differs");
    int most=0,crowded=0;
    for(int bin=0;bin<144;++bin){int used=0;
        if((*packed)[448+bin*3].x<0){++crowded;continue;}
        for(int i=0;i<12;++i){
            if((*packed)[448+bin*3+i/4][i%4]==0)break;
            ++used;
        }most=std::max(most,used);
    }
    std::cout<<"PASS: cloud bins max occupancy="<<most<<" full-scan bins="<<crowded
             <<"; accelerated density equals all-object scan\n";
    std::cout<<"PASS: object cloud CPU/GPU density error="<<maxError<<" objects="<<count
             <<" detailed samples="<<detail<<'\n';
    cloudLightCacheChecks(context,frame,packed,production);
    groundWeatherGpuChecks(context);
    weather.update(frame);
}
void sensorCloudGpuParity(sensor::GpuContext& context){
    constexpr size_t count=4096;
    struct Parameters{float base,thickness,coverage,density,time,windX,windZ,speed;};
    const Parameters cases[]={{1200,600,.85f,.8f,27.25f,.6f,.8f,12},{2200,900,.45f,.35f,93.5f,-.8f,.6f,25},{700,350,0,.8f,1,0,0,5},{1200,600,.85f,0,27.25f,.6f,.8f,12},{1400,800,.85f,.8f,61.75f,0,0,8}};
    for(const char* name:{"sensor_vis.frag","band_shade.comp"}){
        const bool visible=std::string(name)=="sensor_vis.frag";std::ifstream file(std::string(SIM_SOURCE_ROOT)+"/shaders/env/"+name);std::ostringstream source;source<<file.rdbuf();const auto production=source.str();
        const auto begin=production.find(visible?"layout(set = 0, binding = 4) uniform VolUbo":"layout(set = 0, binding = 3) uniform BandUbo");check(begin!=std::string::npos,"production cloud UBO block missing");const auto end=production.find(visible?"} vol;":"} bb;",begin);check(end!=std::string::npos,"production cloud UBO block incomplete");
        std::string declarations="#version 460\nlayout(local_size_x=64)in;layout(set=0,binding=0)buffer Output{float result[];};\n"+production.substr(begin,end+6-begin)+"\n";
        for(const char* functionName:{"cHash3","cNoise3","cFbm3","coverageThreshold","continuousCloudDensity","cloudDensityCore","cloudDensity"})declarations+=function(production,functionName);
        float maximum=0;size_t positive=0;
        for(const auto& p:cases){
            auto values=vsg::floatArray::create(visible?12:96);for(auto& value:*values)value=0;
            if(visible){(*values)[0]=p.base;(*values)[1]=.7f;(*values)[2]=p.base+p.thickness;(*values)[3]=p.coverage;(*values)[4]=p.density;(*values)[5]=p.speed;(*values)[6]=p.windX;(*values)[7]=p.windZ;(*values)[8]=p.time;(*values)[9]=17.2f;(*values)[10]=3;}
            else {(*values)[6]=p.base;(*values)[7]=.7f;(*values)[8]=p.base+p.thickness;(*values)[9]=p.coverage;(*values)[10]=p.density;(*values)[11]=p.speed;(*values)[12]=p.windX;(*values)[13]=p.windZ;(*values)[14]=p.time;(*values)[15]=17.2f;(*values)[88]=3;}
            const auto main="\nvoid main(){uint i=gl_GlobalInvocationID.x;if(i>=4096)return;vec3 p=vec3((float(i%17)-8)*180,"+std::to_string(p.base)+"+(float((i/17)%17)/16.0)*("+std::to_string(p.thickness)+"+200.0)-100.0,(float((i/289)%17)-8)*170);float distance=float(i%2)*35000.0;result[i]=cloudDensity(p,distance);result[4096+i]=cloudDensityCore(p,distance);}\n";
            const auto actual=sensorDensityDispatch(context,declarations+main,values,visible?4:3,count*2);
            for(size_t i=0;i<count;++i){const float expected=cloudfield::density((float(i%17)-8)*180,p.base+(float((i/17)%17)/16.f)*(p.thickness+200)-100,(float((i/289)%17)-8)*170,p.base,p.thickness,p.coverage,p.density,p.time,p.windX,p.windZ,p.speed);maximum=std::max({maximum,std::abs(actual[i]-expected),std::abs(actual[count+i]-expected)});if(actual[i]>.01f)++positive;}
        }
        std::cout<<name<<" production density CPU/GPU maximum error="<<maximum<<", positive="<<positive<<'\n';check(maximum<2e-4f&&positive>10,"production sensor cloud field or UBO mapping differs from CPU truth");
    }
}
std::vector<vsg::vec4> readImage(vsg::Device& device,vsg::ImageView& view){
    const auto extent=view.image->extent;const size_t count=size_t(extent.width)*extent.height*extent.depth;
    HostBuffer host(device,count*sizeof(vsg::vec4),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    const auto family=uint32_t(device.getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));auto pool=vsg::CommandPool::create(&device,family);auto command=pool->allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command->vk(),&begin)==VK_SUCCESS,"begin readback");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_GENERAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=view.image->vk(device.deviceID);barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command->vk(),VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=extent;vkCmdCopyImageToBuffer(command->vk(),barrier.image,barrier.newLayout,host.buffer->vk(device.deviceID),1,&copy);
    std::swap(barrier.oldLayout,barrier.newLayout);barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command->vk(),VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);check(vkEndCommandBuffer(command->vk())==VK_SUCCESS,"end readback");
    auto fence=vsg::Fence::create(&device);VkCommandBuffer raw=command->vk();VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&raw;check(device.getQueue(family)->submit(submit,fence)==VK_SUCCESS&&fence->wait(30000000000ull)==VK_SUCCESS,"ocean readback deadline");return host.values<vsg::vec4>(count);
}
void oceanGpuParity(sensor::GpuContext& context){
    constexpr uint32_t N=32;constexpr float patch=256;
    WaveField waves;waves.generate(WaveField::GerstnerSpectral,10,.6,.8,250000,10800,8,2,80);waves.setChoppy(.65f);
    OceanFFTField ocean;const auto built=ocean.buildOffscreen(*context.device(),N,patch);check(built.ok,built.error);ocean.updateSpectrum(waves,30,.6,.8);
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),uint32_t(context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)));graph->addChild(vsg::ref_ptr<vsg::Commands>(ocean.commands()));context.compile(graph);
    float rms=0;
    for(float time:{12.5f,12.75f}){
        ocean.onFrame(time,30,.65f,1);context.execute(graph);
        const auto displacement=readImage(*context.device(),*ocean.displacementMap()),normal=readImage(*context.device(),*ocean.slopeFoamMap());float maximum=0;
        for(uint32_t y=0;y<N;++y)for(uint32_t x=0;x<N;++x){
            const auto index=size_t(y)*N+x;const double ex=(double(x)+.5-N/2.)*patch/N,ez=(double(y)+.5-N/2.)*patch/N;
            const auto sample=ocean.sample(ex,ez,time);const auto& actual=displacement[index];maximum=std::max(maximum,vsg::length(vsg::vec3(actual.x,actual.y,actual.z)-sample.displacement));
            maximum=std::max(maximum,vsg::length(vsg::vec3(normal[index].x,normal[index].y,normal[index].z)-sample.normal));maximum=std::max(maximum,std::abs(normal[index].w-sample.jacobian));rms+=actual.y*actual.y;
        }
        std::cout<<"Ocean CPU/GPU maximum displacement/normal/J error="<<maximum<<" at t="<<time<<'\n';check(maximum<.003f,"FFT CPU sampler differs from actual production compute");
        const auto periodic=ocean.sample(17,-23,time),again=ocean.sample(17+patch,-23-patch,time);check(vsg::length(periodic.displacement-again.displacement)<1e-5,"FFT periodic coordinates mismatch");
    }
    rms=std::sqrt(rms/(2*N*N));check(rms>.1f&&rms<2.f,"FFT physical amplitude lost N-squared normalization");context.clearGraphs();
}
void gpuVoxelCloudDynamics(sensor::GpuContext& context){
    GpuVoxelCloudConfig config;config.width=32;config.height=24;config.depth=32;
    config.voxelSizeMetres=30;config.originMetres={-480,700,-480};config.pressureIterations=8;
    GpuVoxelCloud cloud;const auto built=cloud.build(*context.device(),config);check(built.ok,built.error);
    const auto family=uint32_t(context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    auto initialize=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    initialize->addChild(vsg::ref_ptr<vsg::Commands>(cloud.initializationCommands()));
    auto step=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    step->addChild(vsg::ref_ptr<vsg::Commands>(cloud.stepCommands()));
    context.compile(initialize);context.compile(step);context.execute(initialize);
    GpuVoxelCloudFrame frame;frame.ambientWind={4,0,1};frame.ambientVapourMixingRatio=.010f;
    for(int i=0;i<6;++i){frame.simulationSeconds=i*frame.stepSeconds;cloud.update(frame);context.execute(step);}
    const auto beforeState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto beforeVelocity=readImage(*context.device(),*cloud.velocityMap());
    double liquid=0;float maxVelocity=0;
    for(size_t i=0;i<beforeState.size();++i){
        check(std::isfinite(beforeState[i].y)&&beforeState[i].y>=0,"GPU cloud produced invalid liquid water");
        liquid+=beforeState[i].y;maxVelocity=std::max(maxVelocity,vsg::length(vsg::vec3(beforeVelocity[i].x,beforeVelocity[i].y,beforeVelocity[i].z)));
    }
    check(liquid>.001,"GPU cloud wet thermal failed to condense");check(maxVelocity<150,"GPU pressure projection failed to bound velocity");
    GpuCloudCollider aircraft;aircraft.id=42;aircraft.radiusMetres=55;aircraft.velocityMetresPerSecond={85,0,0};aircraft.coupling=1;
    for(int i=0;i<8;++i){
        aircraft.centreMetres={-180.f+i*45.f,900.f,0.f};frame.colliders={aircraft};
        frame.simulationSeconds=(6+i)*frame.stepSeconds;cloud.update(frame);context.execute(step);
    }
    const auto afterState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto afterVelocity=readImage(*context.device(),*cloud.velocityMap());
    double disturbance=0;float wakeVelocity=0;
    for(size_t i=0;i<afterState.size();++i){
        disturbance+=std::abs(afterState[i].y-beforeState[i].y);
        wakeVelocity=std::max(wakeVelocity,vsg::length(vsg::vec3(afterVelocity[i].x-beforeVelocity[i].x,afterVelocity[i].y-beforeVelocity[i].y,afterVelocity[i].z-beforeVelocity[i].z)));
    }
    std::cout<<"GPU voxel cloud liquid="<<liquid<<", collision density delta="<<disturbance<<", wake delta="<<wakeVelocity<<" m/s\n";
    check(disturbance>1e-5,"aircraft failed to displace GPU cloud water");
    check(wakeVelocity>1.f,"aircraft failed to inject a GPU cloud wake");
    check(cloud.revision()==14,"GPU cloud revision does not follow simulation steps");
    // Raised collider budget: 32 simultaneous bodies must all perturb the
    // field without violating the velocity / non-negativity invariants.
    std::vector<GpuCloudCollider> swarm;
    for(int i=0;i<32;++i) {
        GpuCloudCollider body;body.id=uint64_t(100+i);
        body.centreMetres={-200.f+float(i%8)*55.f,860.f+float(i/8)*40.f,-120.f+float(i%4)*70.f};
        body.radiusMetres=30.f;body.velocityMetresPerSecond={60.f,2.f,0.f};body.coupling=1.f;
        swarm.push_back(body);
    }
    frame.colliders=swarm;
    for(int i=0;i<4;++i){frame.simulationSeconds=(14+i)*frame.stepSeconds;cloud.update(frame);context.execute(step);}
    const auto swarmState=readImage(*context.device(),*cloud.thermodynamicMap());
    for(const auto& v:swarmState)
        check(std::isfinite(v.y)&&v.y>=0&&std::isfinite(v.x)&&v.x>=0,"32-collider swarm produced invalid cloud state");
    double swarmLiquid=0;for(const auto& v:swarmState)swarmLiquid+=v.y;
    check(swarmLiquid>0.,"32-collider swarm erased the cloud field");
    context.clearGraphs();
}
void gpuRainConservation(sensor::GpuContext& context){
    constexpr uint32_t side=8;
    auto input=vsg::vec4Array3D::create(side,side,side,vsg::vec4(.2f,0,0,0));
    input->at(3,6,3).a=.002f;input->properties.format=VK_FORMAT_R32G32B32A32_SFLOAT;
    auto inputImage=vsg::Image::create(input);inputImage->usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    auto inputView=vsg::ImageView::create(inputImage);
    auto outputImage=vsg::Image::create();outputImage->imageType=VK_IMAGE_TYPE_3D;
    outputImage->format=VK_FORMAT_R32G32B32A32_SFLOAT;outputImage->extent={side,side,side};
    outputImage->mipLevels=outputImage->arrayLayers=1;outputImage->samples=VK_SAMPLE_COUNT_1_BIT;
    outputImage->usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    auto output=vsg::createImageView(context.device(),outputImage,VK_IMAGE_ASPECT_COLOR_BIT);
    auto parameters=vsg::vec4Array::create(71,vsg::vec4{});
    (*parameters)[0]=vsg::vec4(0,700,0,24);(*parameters)[1]=vsg::vec4(8,8,8,.05f);
    (*parameters)[3]=vsg::vec4(285.15f,90000,9.80665f,.0065f);(*parameters)[4]=vsg::vec4(2.2f,.45f,1,1);
    auto descriptorLayout=vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}});
    auto layout=vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorLayout},vsg::PushConstantRanges{});
    auto set=vsg::DescriptorSet::create(descriptorLayout,vsg::Descriptors{
        vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},inputView,VK_IMAGE_LAYOUT_GENERAL),0,0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorImage::create(vsg::ImageInfo::create(vsg::ref_ptr<vsg::Sampler>{},output,VK_IMAGE_LAYOUT_GENERAL),1,0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        vsg::DescriptorBuffer::create(parameters,4)});
    auto stage=vsg::ShaderStage::read(VK_SHADER_STAGE_COMPUTE_BIT,"main",std::string(SIM_SOURCE_ROOT)+"/shaders/env/cloud_voxel_rain.comp");
    auto compiler=vsg::ShaderCompiler::create();vsg::ShaderStages stages{stage};check(compiler->compile(stages),"rain shader compilation");
    auto commands=vsg::Commands::create();
    commands->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
        vsg::ImageMemoryBarrier::create(0,VK_ACCESS_SHADER_WRITE_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,
            VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,outputImage,VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1})));
    commands->addChild(vsg::BindComputePipeline::create(vsg::ComputePipeline::create(layout,stage)));
    commands->addChild(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,set));commands->addChild(vsg::Dispatch::create(2,2,2));
    auto graph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),uint32_t(context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT)));graph->addChild(commands);
    context.compile(graph);context.execute(graph);const auto result=readImage(*context.device(),*output);
    double total=0,moment=0;
    for(size_t i=0;i<result.size();++i){check(result[i].a>=0&&std::isfinite(result[i].a),"rain sedimentation became negative");total+=result[i].a;moment+=result[i].a*((i/side)%side);}
    check(std::abs(total-.002)<1e-8 && moment/total<6.,"rain failed conservative downward transport");
    check(result[(3*side+5)*side+3].a>0,"rain did not enter the cell below");
    std::cout<<"PASS: independent rain sedimentation preserves water and descends\n";context.clearGraphs();
}
void gpuVoxelCloudBatches(sensor::GpuContext& context){
    const auto family=uint32_t(context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    for(uint32_t capacity:{1u,8u,16u}) {
        GpuVoxelCloudConfig config;config.width=config.height=config.depth=8;
        config.maxColliders=capacity;config.pressureIterations=4;
        GpuVoxelCloud batch,serial;
        check(batch.build(*context.device(),config).ok&&serial.build(*context.device(),config).ok,"small collider cloud build");
        auto initialization=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
        initialization->addChild(vsg::ref_ptr<vsg::Commands>(batch.initializationCommands()));
        initialization->addChild(vsg::ref_ptr<vsg::Commands>(serial.initializationCommands()));
        auto a=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
        auto b=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
        a->addChild(vsg::ref_ptr<vsg::Commands>(batch.stepCommands()));b->addChild(vsg::ref_ptr<vsg::Commands>(serial.stepCommands()));
        context.compile(initialization);context.compile(a);context.compile(b);context.execute(initialization);
        GpuVoxelCloudFrame frame;frame.stepSeconds=float(CloudStepClock::stepSeconds);
        batch.update(frame,4);context.execute(a);
        for(int k=0;k<4;++k){serial.update(frame);context.execute(b);}
        const auto actual=readImage(*context.device(),*batch.thermodynamicMap());
        const auto expected=readImage(*context.device(),*serial.thermodynamicMap());
        float maximum=0;
        for(size_t k=0;k<actual.size();++k)for(int c=0;c<4;++c){
            check(std::isfinite(actual[k][c]),"small collider capacity corrupted cloud state");
            maximum=std::max(maximum,std::abs(actual[k][c]-expected[k][c]));
        }
        check(maximum<1e-6&&batch.revision()==serial.revision(),"batched substeps reseeded or lost physical evolution");
        context.clearGraphs();
    }
    std::cout<<"PASS: GPU catch-up matches serial substeps with collider capacities 1/8/16\n";
}
void gpuVoxelCloudSeedLifecycle(sensor::GpuContext& context){
    // P0 regressions: the seed pass must only paint on reset, the weather
    // seed must stay fixed across rebases, and steady steps must warm-start
    // the pressure solve from the previous converged field.
    GpuVoxelCloudConfig config;config.width=16;config.height=12;config.depth=16;
    config.voxelSizeMetres=30;config.originMetres={-240,700,-240};config.pressureIterations=8;
    GpuVoxelCloud cloud;const auto built=cloud.build(*context.device(),config);check(built.ok,built.error);
    const auto family=uint32_t(context.device()->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    auto initialize=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    initialize->addChild(vsg::ref_ptr<vsg::Commands>(cloud.initializationCommands()));
    auto step=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    step->addChild(vsg::ref_ptr<vsg::Commands>(cloud.stepCommands()));
    context.compile(initialize);context.compile(step);context.execute(initialize);
    GpuVoxelCloudFrame frame;frame.ambientWind={4,0,1};frame.ambientVapourMixingRatio=.010f;frame.cloudCover=.5f;
    for(int i=0;i<8;++i){frame.simulationSeconds=i*frame.stepSeconds;cloud.update(frame);context.execute(step);}
    const auto evolvedState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto evolvedVelocity=readImage(*context.device(),*cloud.velocityMap());
    double liquid=0;for(const auto& v:evolvedState)liquid+=v.y;
    check(liquid>.0005,"seed lifecycle: initial thermal failed to condense");
    // 1) Seed early-out: a reset=0 step must continue evolving the field.
    //    One physics step only drifts the state slightly, while an explicit
    //    reseed jumps it back to the initial pattern. Pre-fix the seed pass
    //    repainted state0/velocity0 every step, so both runs were identical.
    cloud.update(frame);context.execute(step);
    const auto continueState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto continueVelocity=readImage(*context.device(),*cloud.velocityMap());
    auto stateDelta=[&](const std::vector<vsg::vec4>& a,const std::vector<vsg::vec4>& b){
        double delta=0;for(size_t k=0;k<a.size();++k)
            delta+=std::abs(a[k].x-b[k].x)+std::abs(a[k].y-b[k].y)+std::abs(a[k].z-b[k].z);
        return delta;};
    const double drift=stateDelta(continueState,evolvedState);
    // 2) Pressure warm start: after a steady full step pressure0 holds the
    //    converged field (the host keeps the Jacobi iteration count even).
    const auto convergedPressure=readImage(*context.device(),*cloud.pressureMap());
    double pressureEnergy=0;for(const auto& v:convergedPressure)pressureEnergy+=double(v.x)*v.x;
    check(pressureEnergy>1e-8,"pressure solve produced no field to warm-start from");
    //    Re-run only seed..divergence (the 7th dispatch) and read pressure0:
    //    on a steady step it must equal warmScale * the converged field.
    const auto divergencePrefix=vsg::Commands::create();
    {
        const auto& children=cloud.stepCommands()->children;
        uint32_t dispatches=0;size_t cut=0;
        for(size_t k=0;k+1<children.size();++k)
            if(dynamic_cast<vsg::Dispatch*>(children[k].get())&&++dispatches==7){cut=k+2;break;}
        check(cut>0,"unexpected cloud step command layout");
        for(size_t k=0;k<cut;++k)divergencePrefix->addChild(children[k]);
    }
    divergencePrefix->addChild(vsg::PipelineBarrier::create(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,vsg::MemoryBarrier::create(VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT)));
    auto pressureGraph=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    pressureGraph->addChild(divergencePrefix);context.compile(pressureGraph);
    cloud.update(frame);context.execute(pressureGraph);
    const auto warmPressure=readImage(*context.device(),*cloud.pressureMap());
    for(size_t k=0;k<warmPressure.size();++k){
        const float expected=.8f*convergedPressure[k].x;
        check(std::abs(warmPressure[k].x-expected)<=1e-5f*std::max(1.f,std::abs(expected)),
              "pressure warm start discarded the previous converged field");}
    // 3) Explicit reset clears the pressure guess: the divergence pass must
    //    zero both buffers when warmStart=0.
    cloud.reset();cloud.update(frame);context.execute(pressureGraph);
    const auto coldPressure=readImage(*context.device(),*cloud.pressureMap());
    for(const auto& v:coldPressure)check(v.x==0.f,"reset failed to clear the pressure warm-start buffer");
    //    Finish the seed early-out check: an explicit reset + full step must
    //    jump the field back to the seed pattern, far more than one step of
    //    drift, and must differ from the continued (reset=0) state.
    cloud.reset();cloud.update(frame);context.execute(step);
    const auto reseedState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto reseedVelocity=readImage(*context.device(),*cloud.velocityMap());
    const double jump=stateDelta(reseedState,evolvedState);
    std::cout<<"seed lifecycle drift="<<drift<<", reseed jump="<<jump<<'\n';
    check(std::memcmp(continueState.data(),reseedState.data(),reseedState.size()*sizeof(vsg::vec4))!=0&&
          std::memcmp(continueVelocity.data(),reseedVelocity.data(),reseedVelocity.size()*sizeof(vsg::vec4))!=0,
          "seed pass rewrote the thermodynamic state on a non-reset step");
    check(jump>4.*drift+1e-9,"non-reset step redrew the seed pattern instead of continuing the evolution");
    // 4) Fixed rebase seed: after evolving at a later clock time, resetting
    //    and rebasing the origin must reproduce the same reset run at the
    //    same origin with a different clock (pre-fix the seed was float(time),
    //    so the weather pattern changed on every rebase).
    context.execute(step);
    for(int i=0;i<8;++i){frame.simulationSeconds=1000.+i*frame.stepSeconds;cloud.update(frame);context.execute(step);}
    cloud.setOriginMetres({360,700,360});
    frame.simulationSeconds=2000.;cloud.update(frame);context.execute(step);
    const auto rebaseState=readImage(*context.device(),*cloud.thermodynamicMap());
    const auto rebaseVelocity=readImage(*context.device(),*cloud.velocityMap());
    // The seed pass is world-coordinate invariant, but microphysics and the
    // collider wake use absolute world positions, so compare two runs that
    // evolve from the same reset at the SAME origin: only the clock differs.
    GpuVoxelCloud fresh;const auto freshBuilt=fresh.build(*context.device(),config);check(freshBuilt.ok,freshBuilt.error);
    fresh.setOriginMetres({360,700,360});
    auto freshInitialize=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    freshInitialize->addChild(vsg::ref_ptr<vsg::Commands>(fresh.initializationCommands()));
    auto freshStep=vsg::CommandGraph::create(vsg::ref_ptr<vsg::Device>(context.device()),family);
    freshStep->addChild(vsg::ref_ptr<vsg::Commands>(fresh.stepCommands()));
    context.compile(freshInitialize);context.compile(freshStep);context.execute(freshInitialize);
    GpuVoxelCloudFrame freshFrame;freshFrame.ambientWind={4,0,1};freshFrame.ambientVapourMixingRatio=.010f;freshFrame.cloudCover=.5f;
    freshFrame.simulationSeconds=0.;fresh.update(freshFrame);context.execute(freshStep);
    const auto freshState=readImage(*context.device(),*fresh.thermodynamicMap());
    const auto freshVelocity=readImage(*context.device(),*fresh.velocityMap());
    check(std::memcmp(rebaseState.data(),freshState.data(),freshState.size()*sizeof(vsg::vec4))==0,
          "rebase seed depends on the simulation clock instead of the fixed weather seed");
    check(std::memcmp(rebaseVelocity.data(),freshVelocity.data(),freshVelocity.size()*sizeof(vsg::vec4))==0,
          "rebase velocity seed depends on the simulation clock");
    std::cout<<"GPU voxel cloud seed lifecycle: liquid="<<liquid<<", warm pressure energy="<<pressureEnergy<<'\n';
    context.clearGraphs();
}
struct Terrain:ITerrainBridge {
    mutable int calls=0;bool valid=false;uint64_t version=0;
    uint64_t revision()const override{return version;}
    float heightENU(double,double)const override{++calls;return valid?-5.f:std::numeric_limits<float>::quiet_NaN();}
    vsg::vec3 normalENU(double,double)const override{return{0,1,0};}bool isLand(double,double,float)const override{return false;}float waterDepth(double,double,float)const override{return valid?5.f:std::numeric_limits<float>::quiet_NaN();}
};
void moduleQueries(){
    CloudParams params;params.cover=1;params.volume=true;params.layers={{CloudLayerType::Cumulus,1200,600,.85f,.8f}};
    auto cloud=createCloudModule(params);auto* query=dynamic_cast<ICloudQuery*>(cloud.get());EnvFrame frame;frame.time_sec=27.25;frame.wind_dir_enu={.6,0,.8};frame.wind_speed=12;frame.sun_dir_enu={0,1,0};cloud->onFrame(frame);
    bool covered=false;for(int x=-3000;x<=3000;x+=300)for(int z=-3000;z<=3000;z+=300){const float coverage=query->coverageAt(x,z);check(std::abs(query->shadowFactor(x,z,1.57079632679)-(1-coverage))<1e-4,"volume cloud shadow uses a different density field");covered|=coverage>.01;}check(covered,"continuous cloud query still depends on discrete cloud blocks");
    Terrain terrain;OceanParams settings;settings.grid=4;settings.terrain=&terrain;auto ocean=createOceanModule(settings);ocean->onFrame(frame);const auto first=terrain.calls;check(first>0,"terrain bridge never queried");ocean->onFrame(frame);check(terrain.calls==first,"missing terrain retried without bounded backoff");terrain.valid=true;++terrain.version;ocean->onFrame(frame);check(terrain.calls>first,"static camera failed to invalidate coast on terrain version");const auto complete=terrain.calls;ocean->onFrame(frame);check(terrain.calls==complete,"valid coast cache not reused");dynamic_cast<IOceanControl*>(ocean.get())->setSeaLevel(2);ocean->onFrame(frame);check(terrain.calls>complete,"sea-level change failed to invalidate coast cache");
    std::cout<<"PASS: continuous volume queries/shadows and versioned NoData coastline cache\n";
}
void independentFftPatch(){
    OceanParams a;a.wave_mode=2;a.grid=4;a.size=3200;a.fft_patch_size=2048;a.auto_sea_state=false;a.wave_height=2;a.wave_length=80;
    auto b=a;b.size=60000;
    auto small=createOceanModule(a),large=createOceanModule(b);
    EnvFrame frame;frame.time_sec=12.5;frame.wind_dir_enu={.6,0,.8};frame.wind_speed=12;
    small->onFrame(frame);large->onFrame(frame);
    check(!small->fftCommands()&&!large->fftCommands(),"headless FFT unexpectedly requires GPU/window commands");
    auto* first=dynamic_cast<IOceanSampler*>(small.get());auto* second=dynamic_cast<IOceanSampler*>(large.get());check(first&&second,"headless ocean sampler missing");
    double energy=0;
    for(int i=0;i<128;++i){const double x=i*15.7,z=i*9.3;const auto h=first->waveHeight(x,z,12.5);energy+=h*h;
        check(std::abs(h-second->waveHeight(x,z,12.5))<1e-5,"display extent changed the physical FFT field");
        check(std::abs(h-first->waveHeight(x+a.fft_patch_size,z-a.fft_patch_size,12.5))<2e-4,"configured world FFT patch is not periodic");
    }
    check(std::sqrt(energy/128)>.1,"large ocean extent removed near-field wave energy");
    a.fft_patch_size=0;bool rejected=false;try{createOceanModule(a);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid FFT patch was accepted");
    const auto path=std::filesystem::temp_directory_path()/("earth-ocean-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    {std::ofstream out(path);out<<R"({"scenario_name":"FFT patch fixture","ocean":{"enabled":true,"fft_patch_size":2048},"objects":[]})";}
    sensor::JsonSceneDataSource source(path.string());const auto loaded=source.load();std::filesystem::remove(path);check(loaded.ok,"scenario FFT patch configuration failed to load");check(source.data().ocean.fft_patch_size==2048,"scenario FFT patch configuration was discarded");
    {std::ofstream out(path);out<<R"({"scenario_name":"Invalid FFT patch fixture","ocean":{"fft_patch_size":0},"objects":[]})";}
    sensor::JsonSceneDataSource invalid(path.string());const auto invalidResult=invalid.load();std::filesystem::remove(path);check(!invalidResult.ok,"scenario accepted a nonpositive FFT patch");
    std::cout<<"PASS: headless FFT patch is independent of display extent and preserved by scenario configuration\n";
}
}
int main(){try{cloudClockContracts();sparseWindTransport();graphicsShaderContracts();moduleQueries();independentFftPatch();sensor::GpuContext gpu;gpu.initialize();cloudGpuParity(gpu);sensorCloudGpuParity(gpu);objectCloudParity(gpu);oceanGpuParity(gpu);gpuVoxelCloudDynamics(gpu);gpuVoxelCloudSeedLifecycle(gpu);gpuVoxelCloudBatches(gpu);gpuRainConservation(gpu);std::cout<<"PASS: coherent Earth cloud/ocean production GPU fields\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
