#include "Precipitation.h"
#include "EarthShader.h"
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace rockyenv {
namespace {
constexpr uint32_t particlesPerType=2*24*24*8;
float smoothUnit(float low,float high,float value) {
    const float t=std::clamp((value-low)/(high-low),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
}
Precipitation::Precipitation() {
    parameters_=vsg::vec4Array::create(10,vsg::vec4{});
    parameters_->properties.dataVariance=vsg::DYNAMIC_DATA;
    node=vsg::Switch::create();
    auto shader=vsg::ShaderSet::create(vsg::ShaderStages{
        vsgraysim::earth::earthShader("precipitation.vert"),vsgraysim::earth::earthShader("precipitation.frag")});
    shader->addPushConstantRange("pc","",VK_SHADER_STAGE_VERTEX_BIT,0,128);
    shader->addDescriptorBinding("weather","",0,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,{});
    auto cfg=vsg::GraphicsPipelineConfigurator::create(shader);cfg->assignDescriptor("weather",parameters_);
    for(auto& state:cfg->pipelineStates) {
        if(auto depth=state.cast<vsg::DepthStencilState>()){depth->depthWriteEnable=VK_FALSE;depth->depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL;}
        if(auto raster=state.cast<vsg::RasterizationState>())raster->cullMode=VK_CULL_MODE_NONE;
        if(auto blend=state.cast<vsg::ColorBlendState>())for(auto& a:blend->attachments){
            a.blendEnable=VK_TRUE;a.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;a.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;a.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    shader->variants.emplace(cfg->shaderHints,shader->stages);cfg->init();
    auto draw=vsg::StateGroup::create();cfg->copyTo(draw);
    draw_=vsg::Draw::create(6,2*particlesPerType,0,0);draw->addChild(draw_);node->addChild(false,draw);
}
void Precipitation::update(const VoxelWeatherFrame& f) {
    const char* toggle=std::getenv("VSGRAYSIM_PRECIPITATION");
    const bool on=(f.rain>0||f.snow>0)&&f.precipitationAltitude<f.cloudBase+50.f&&
        (!f.ocean||f.precipitationAltitude>=f.seaLevel-.1f)&&
        !(toggle&&std::string_view(toggle)=="0");node->setAllChildren(on);if(!on)return;
    draw_->firstInstance=f.rain>0?0:particlesPerType;
    draw_->instanceCount=f.rain>0&&f.snow>0?2*particlesPerType:particlesPerType;
    const auto observerUp=vsg::normalize(f.eye);
    const auto anchor=vsg::length(f.cloudAnchor)>1?f.cloudAnchor:observerUp*6378137.;
    // Keep the precipitation cells in the scene's chart when moving the
    // camera. View-dependent up is still used for the illumination.
    const auto up=vsg::normalize(anchor);
    auto east=vsg::cross(vsg::dvec3(0,0,1),up);if(vsg::length(east)<1e-6)east={1,0,0};else east=vsg::normalize(east);
    const auto north=vsg::cross(up,east),wind=vsg::cross(f.windAxis,up)*double(f.windSpeed);
    const auto offset=f.eye-anchor;
    auto& p=*parameters_;
    p[0]=vsg::vec4(vsg::vec3(east),float(vsg::dot(offset,east)));
    p[1]=vsg::vec4(vsg::vec3(up),f.ocean?f.seaLevel:-1e20f);
    p[2]=vsg::vec4(vsg::vec3(north),float(vsg::dot(offset,north)));
    // Small periodic clock prevents long-running float time loss. The field
    // uses a 240 s period for both layers, preserving positions at the wrap.
    const float rainDensity=std::pow(std::clamp(f.rain,0.f,1.f),.56f);
    const float snowDensity=std::pow(std::clamp(f.snow,0.f,1.f),.56f);
    p[3]=vsg::vec4{float(std::fmod(f.seconds,240.)),rainDensity,snowDensity,f.precipitationAltitude};
    p[4]=vsg::vec4(vsg::vec3(wind),f.cloudBase);
    const float mu=float(vsg::dot(observerUp,vsg::dvec3(f.sun)));
    const float day=smoothUnit(-.105f,.22f,mu);
    const float moon=std::max(0.f,float(vsg::dot(observerUp,vsg::dvec3(f.moon))))*std::max(0.f,f.moonPhase);
    const float grey=f.overcastAmount>=0.f?std::clamp(f.overcastAmount,0.f,1.f):float(f.overcast);
    const auto night=vsg::vec3(.0015f,.002f,.003f)+vsg::vec3(.010f,.012f,.016f)*moon*(1.f-.80f*grey);
    const float lowSun=(1.f-smoothUnit(-.055f,.32f,mu))*(1.f-.87f*grey);
    const vsg::vec3 evening(.49f,.36f,.28f),daylight(.46f,.51f,.55f),overcast(.30f,.35f,.40f);
    const auto diffuse=daylight*(1.f-grey)+overcast*grey;
    p[5]=vsg::vec4(night*(1.f-day)+(diffuse*(1.f-lowSun)+evening*lowSun)*day,f.exposure);
    const auto travel=f.hasWindTravel?f.windTravel:wind*f.seconds;
    // Common repeat of the 60 and 144 m horizontal particle fields.
    p[6]=vsg::vec4{float(std::fmod(vsg::dot(travel,east)*.3,720.)),float(std::fmod(vsg::dot(travel,north)*.3,720.)),0,0};
    p[7]=vsg::vec4{float(std::fmod(vsg::dot(travel,east)*.55,720.)),float(std::fmod(vsg::dot(travel,north)*.55,720.)),0,0};
    p[6].z=f.fogLight.x;p[6].w=f.fogLight.y;p[7].z=f.fogLight.z;p[7].w=f.fogVisibility;
    p[8]=vsg::vec4(f.sun,smoothUnit(-.014f,.085f,mu)*(1.f-.95f*grey));
    const float flutter=.80f+.55f*std::min(1.f,float(vsg::length(wind))*(.55f/8.f));
    p[9]=vsg::vec4(vsg::vec3(.88f,.93f,1.0f)*(1.f-lowSun)+vsg::vec3(1.f,.58f,.30f)*lowSun,flutter);
    p.dirty();
}
}
