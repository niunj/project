#include <vsgraysim/earth/TerrainCloudShadow.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <cstdlib>
#include <vsgraysim/earth/EarthSurface.h>
#include <rocky_env/AtmosphereRender.h>
#include "EarthShader.h"
#include <vsg/utils/GraphicsPipelineConfigurator.h>
#include <cstring>
namespace vsgraysim::earth {
TerrainCloudShadow::TerrainCloudShadow(const EarthSurfaceSnapshot& surface,vsg::ref_ptr<vsg::VertexIndexDraw> draw,
                                     std::shared_ptr<rockyenv::CloudGpuResources> cloud,const EarthSurfaceSnapshot* pinnedTexture) {
    if(!cloud)cloud=rockyenv::createCloudGpuResources();
    auto shaders=vsg::ShaderSet::create(vsg::ShaderStages{
        earthShader("terrain_cloud_shadow.vert"),earthShader("terrain_cloud_shadow.frag")});
    shaders->addAttributeBinding("position","",0,VK_FORMAT_R32G32B32_SFLOAT,{});
    shaders->addAttributeBinding("normal","",1,VK_FORMAT_R32G32B32_SFLOAT,{});
    shaders->addAttributeBinding("texcoord","",2,VK_FORMAT_R32G32_SFLOAT,{});
    shaders->addPushConstantRange("camera","",VK_SHADER_STAGE_VERTEX_BIT,0,128);
    shaders->addDescriptorBinding("shadow","",0,0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,
                                  VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("albedo","",0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("cloud","",0,2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("cloudDetail","",0,3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloudPages","",0,4,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloudAtlas","",0,5,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("sparseCloud","",0,6,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("gpuCloudState","",0,7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("gpuCloud","",0,8,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("pinnedAlbedo","",0,9,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    shaders->addDescriptorBinding("airTransmission","",0,10,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,{});
    auto cfg=vsg::GraphicsPipelineConfigurator::create(shaders);
    cfg->enableArray("position",VK_VERTEX_INPUT_RATE_VERTEX,sizeof(vsg::vec3));
    cfg->enableArray("normal",VK_VERTEX_INPUT_RATE_VERTEX,sizeof(vsg::vec3));
    cfg->enableArray("texcoord",VK_VERTEX_INPUT_RATE_VERTEX,sizeof(vsg::vec2));
    parameters_=vsg::vec4Array::create(14,vsg::vec4{});
    (*parameters_)[11].w=pinnedTexture?1.f:0.f;
    if(pinnedTexture) {
        const double spanX=surface.max_x-surface.min_x,spanZ=surface.max_z-surface.min_z;
        (*parameters_)[12]=vsg::vec4(
            float((pinnedTexture->min_x-surface.min_x)/spanX),
            float((pinnedTexture->min_z-surface.min_z)/spanZ),
            float((pinnedTexture->max_x-pinnedTexture->min_x)/spanX),
            float((pinnedTexture->max_z-pinnedTexture->min_z)/spanZ));
    }
    auto imageFor=[](const EarthSurfaceSnapshot& source) {
        const bool textured=!source.imagery_rgba.empty();
        auto image=vsg::ubvec4Array2D::create(textured?source.imagery_width:1,textured?source.imagery_height:1);
        image->properties.format=VK_FORMAT_R8G8B8A8_SRGB;
        if(textured) {
            if(source.imagery_rgba.size()!=image->size()*4)
                throw std::invalid_argument("invalid terrain imagery dimensions");
            std::memcpy(image->dataPointer(),source.imagery_rgba.data(),source.imagery_rgba.size());
        } else {
            (*image)[0]=vsg::ubvec4(194,185,163,255);
        }
        return image;
    };
    auto albedo=imageFor(surface);
    auto imageSampler=vsg::Sampler::create();
    imageSampler->addressModeU=imageSampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    imageSampler->maxLod=VK_LOD_CLAMP_NONE;
    cfg->assignTexture("albedo",albedo,imageSampler);
    cfg->assignTexture("pinnedAlbedo",pinnedTexture?imageFor(*pinnedTexture):albedo,imageSampler);
    auto frame=simcore::geo::makeEnuToWorldMatrix(surface.anchor);
    for(int i=0;i<4;++i) (*parameters_)[i+4]=vsg::vec4(frame[i]);
    parameters_->properties.dataVariance=vsg::DYNAMIC_DATA;
    cfg->assignDescriptor("shadow",parameters_);
    auto sampler=vsg::Sampler::create();
    sampler->addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod=VK_LOD_CLAMP_NONE;
    cfg->assignTexture("cloud",planetCloudTexture(),sampler);
    auto detailSampler=vsg::Sampler::create();
    detailSampler->addressModeU=detailSampler->addressModeV=detailSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    detailSampler->maxLod=VK_LOD_CLAMP_NONE;
    cfg->assignTexture("cloudDetail",planetCloudDetailTexture(),detailSampler);
    auto sparseSampler=vsg::Sampler::create();
    sparseSampler->addressModeU=sparseSampler->addressModeV=sparseSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sparseSampler->maxLod=0;
    auto pageSampler=vsg::Sampler::create();
    pageSampler->addressModeU=pageSampler->addressModeV=pageSampler->addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pageSampler->magFilter=pageSampler->minFilter=VK_FILTER_NEAREST;pageSampler->maxLod=0;
    cfg->assignTexture("sparseCloudPages",planetSparseCloudPageTable(),pageSampler);
    cfg->assignTexture("sparseCloudAtlas",planetSparseCloudAtlas(),sparseSampler);
    cfg->assignDescriptor("sparseCloud",planetSparseCloudParameters());
    cfg->assignTexture("gpuCloudState",vsg::ImageInfoList{cloud->image});
    cfg->assignDescriptor("gpuCloud",cloud->layout);
    auto airSampler=vsg::Sampler::create();
    airSampler->addressModeU=airSampler->addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    airSampler->maxLod=0.f;
    cfg->assignTexture("airTransmission",rockyenv::referenceAtmosphereTransmittance(),airSampler);
    shaders->variants.emplace(cfg->shaderHints,shaders->stages);
    cfg->init();
    node=vsg::StateGroup::create();cfg->copyTo(node);node->addChild(draw);
}
TerrainCloudShadow::TerrainCloudShadow(const TerrainCloudShadow& previous,vsg::ref_ptr<vsg::VertexIndexDraw> geometry)
    :node(vsg::StateGroup::create(*previous.node)),parameters_(previous.parameters_) {
    node->children.clear();
    node->addChild(geometry);
}
void TerrainCloudShadow::update(const PlanetWeatherFrame& weather,const vsg::vec3& horizon,const vsg::vec3& zenith,bool atmosphere,const VoxelOcean* ocean) {
    (*parameters_)[0]=planetAdvection(weather);
    (*parameters_)[1]=vsg::vec4(weather.sun,weather.overcastAmount>=0?weather.overcastAmount:(weather.overcast?1.f:0.f));
    (*parameters_)[2]=vsg::vec4(weather.moon,weather.moonPhase);
    (*parameters_)[3]=vsg::vec4(weather.clouds?weather.cover:0,weather.opticalDepth,weather.cloudBase,weather.cloudHeight);
    (*parameters_)[8]=vsg::vec4(vsg::vec3(weather.eye),atmosphere?1.f:0.f);
    (*parameters_)[9]=vsg::vec4(weather.overcastAmount>=0?weather.fogLight:horizon,weather.turbidity);
    (*parameters_)[10]=vsg::vec4(weather.exposure,weather.wetness,weather.snowCover,weather.overcastAmount>=0?1.f:0.f);
    auto& context=(*parameters_)[11];
    context.x=zenith.x;context.y=zenith.y;context.z=zenith.z;
    (*parameters_)[13]=ocean&&!std::getenv("VSGRAYSIM_OCEAN_LEGACY_EFFECTS")?vsg::vec4(float(ocean->fogVisibility),float(ocean->fogHeight),float(weather.seaLevel),1.f):(weather.fogVisibility>0?vsg::vec4(weather.fogVisibility,weather.fogHeight,weather.seaLevel,2.f):vsg::vec4{});
    if(weather.overcastAmount>=0&&weather.fogVisibility>0)(*parameters_)[13]=vsg::vec4(weather.fogVisibility,weather.fogHeight,weather.seaLevel,2.f);
    parameters_->dirty();
}
}
