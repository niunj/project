#include "../../../src/earth/VoxelTerrain.h"
#include "EarthShader.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <rocky_env/AtmosphereRender.h>
#include <rocky_env/VoxelWeather.h>
#include <vsg/all.h>
#include <vsgraysim/earth/SurfaceMesh.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/earth/VoxelOceanDisplay.h>
namespace vsgraysim::earth {
vsg::ref_ptr<vsg::floatArray2D> makeOceanReferenceImage(const VoxelOcean &o) {
    auto image = vsg::floatArray2D::create(o.referenceSize ? o.referenceSize : 1,
                                           o.referenceSize ? o.referenceSize : 1);
    image->properties.format = VK_FORMAT_R32_SFLOAT;
    if (o.referenceSize)
        std::copy(o.referenceHeights.begin(), o.referenceHeights.end(), image->begin());
    else
        (*image)[0] = 0;
    return image;
}
struct VoxelOceanDisplay::Effects {
    vsg::ref_ptr<vsg::ImageInfo> foam;
    vsg::ref_ptr<vsg::vec4Array> params;
    vsg::ref_ptr<vsg::Switch> commands = vsg::Switch::create();
    std::shared_ptr<std::atomic<uint64_t>> recorded = std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t lastRecord = 0;
    double seconds = -1, pendingSeconds = -1;
    vsg::vec2 origin{1e30f, 1e30f}, pendingOrigin{};
    std::shared_ptr<const VoxelOcean> ocean, pendingOcean;
};
namespace {
struct FoamRecorded : vsg::Inherit<vsg::Command, FoamRecorded> {
    std::shared_ptr<std::atomic<uint64_t>> count;
    void record(vsg::CommandBuffer &) const override { count->fetch_add(1); }
};
} // namespace
VoxelOceanDisplay::VoxelOceanDisplay(const VoxelSceneSnapshot &scene, const VoxelOceanDisplay *reuse,
                                     vsg::Device *device, std::shared_ptr<rockyenv::CloudGpuResources> cloud)
    : cloud_(std::move(cloud)) {
    sourceOcean_ = scene.ocean;
    if (reuse && reuse->sourceOcean_) {
        const auto &a = *sourceOcean_;
        const auto &b = *reuse->sourceOcean_;
        if (a.halfExtent == b.halfExtent && a.referenceSize == b.referenceSize &&
            a.referenceX == b.referenceX && a.referenceZ == b.referenceZ &&
            a.referenceStepX == b.referenceStepX && a.referenceStepZ == b.referenceStepZ &&
            a.referenceHeights == b.referenceHeights) {
            effects_ = reuse->effects_;
            commands = reuse->commands;
            referenceImage_ = reuse->referenceImage_;
            surface = reuse->surface;
            depthSurface = reuse->depthSurface;
            background = reuse->background;
            spray = reuse->spray;
            for (const auto &binding : reuse->bindings_)
                if (binding.id.empty())
                    bindings_.push_back(binding);
            reuseObjects_ = reuse->objects_;
            return;
        }
    }
    effects_ = std::make_shared<Effects>();
    commands = effects_->commands;
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod = 0;
    auto black = vsg::floatArray2D::create(1, 1, 0.f);
    black->properties.format = VK_FORMAT_R32_SFLOAT;
    effects_->foam = vsg::ImageInfo::create(sampler, black);
    effects_->params = vsg::vec4Array::create(voxelOceanUniformCount, vsg::vec4{});
    effects_->params->properties.dataVariance = vsg::DYNAMIC_DATA;
    if (!cloud_)
        cloud_ = rockyenv::createCloudGpuResources();
    if (device && !std::getenv("VSGRAYSIM_OCEAN_NO_PERSISTENT_FOAM")) {
        auto image = vsg::Image::create();
        image->imageType = VK_IMAGE_TYPE_2D;
        image->format = VK_FORMAT_R16_SFLOAT;
        image->extent = {512, 512, 1};
        image->mipLevels = 1;
        image->arrayLayers = 1;
        image->samples = VK_SAMPLE_COUNT_1_BIT;
        image->tiling = VK_IMAGE_TILING_OPTIMAL;
        image->usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        auto view = vsg::createImageView(device, image, VK_IMAGE_ASPECT_COLOR_BIT);
        effects_->foam = vsg::ImageInfo::create(sampler, view, VK_IMAGE_LAYOUT_GENERAL);
        vsg::RenderPass::Attachments attachments(1);
        auto &a = attachments[0];
        a.format = image->format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        vsg::SubpassDescription sub;
        sub.colorAttachments.push_back({0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        vsg::SubpassDependency before{};
        before.srcSubpass = VK_SUBPASS_EXTERNAL;
        before.dstSubpass = 0;
        before.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        before.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        before.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vsg::SubpassDependency after{};
        after.srcSubpass = 0;
        after.dstSubpass = VK_SUBPASS_EXTERNAL;
        after.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        after.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        after.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        auto pass = vsg::RenderPass::create(device, attachments, vsg::RenderPass::Subpasses{sub},
                                            vsg::RenderPass::Dependencies{before, after});
        auto graph = vsg::RenderGraph::create();
        graph->framebuffer = vsg::Framebuffer::create(pass, vsg::ImageViews{view}, 512, 512, 1);
        graph->renderArea = {{0, 0}, {512, 512}};
        graph->viewportState = vsg::ViewportState::create(VkExtent2D{512, 512});
        graph->clearValues.resize(1);
        graph->clearValues[0].color = {{0, 0, 0, 0}};
        auto shader = vsg::ShaderSet::create(
            vsg::ShaderStages{earthShader("voxel_ocean_foam.vert"), earthShader("voxel_ocean_foam.frag")});
        shader->addDescriptorBinding("ocean", "", 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, {});
        auto cfg = vsg::GraphicsPipelineConfigurator::create(shader);
        cfg->assignDescriptor("ocean", effects_->params);
        auto depth = vsg::DepthStencilState::create();
        depth->depthTestEnable = VK_FALSE;
        depth->depthWriteEnable = VK_FALSE;
        auto blend = vsg::ColorBlendState::create();
        blend->attachments[0].blendEnable = VK_TRUE;
        blend->attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend->attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        auto raster = vsg::RasterizationState::create();
        raster->cullMode = VK_CULL_MODE_NONE;
        for (auto &state : cfg->pipelineStates) {
            if (state.cast<vsg::DepthStencilState>())
                state = depth;
            else if (state.cast<vsg::ColorBlendState>())
                state = blend;
            else if (state.cast<vsg::RasterizationState>())
                state = raster;
        }
        shader->variants.emplace(cfg->shaderHints, shader->stages);
        cfg->init();
        auto group = vsg::StateGroup::create();
        cfg->copyTo(group);
        group->addChild(vsg::Draw::create(6, VoxelOcean::maximumFoam, 0, 0));
        graph->addChild(group);
        auto commandsGroup = vsg::Group::create();
        commandsGroup->addChild(graph);
        auto marker = FoamRecorded::create();
        marker->count = effects_->recorded;
        commandsGroup->addChild(marker);
        commands->addChild(true, commandsGroup);
    }
    referenceImage_ = makeOceanReferenceImage(*scene.ocean);
    constexpr int rings = 192, edge = 128, perRing = 4 * edge;
    const double extent = scene.ocean->halfExtent;
    auto points = vsg::vec3Array::create(1 + rings * perRing);
    auto normals = vsg::vec3Array::create(points->size(), vsg::vec3(0, 1, 0));
    auto uv = vsg::vec2Array::create(points->size(), vsg::vec2{});
    (*points)[0] = {0, 0, 0};
    std::vector<uint32_t> indices;
    for (int r = 1; r <= rings; ++r) {
        const double q = std::max(0., double(r - 128) / 64), nearStep = std::min(.2, extent / (2 * rings)),
                     h = nearStep * r + (extent - nearStep * rings) * std::pow(q, 4);
        for (int i = 0; i < perRing; ++i) {
            const double t = double(i % edge) / edge;
            vsg::vec3 p;
            switch (i / edge) {
            case 0:
                p = {float(-h + 2 * h * t), 0, float(-h)};
                break;
            case 1:
                p = {float(h), 0, float(-h + 2 * h * t)};
                break;
            case 2:
                p = {float(h - 2 * h * t), 0, float(h)};
                break;
            default:
                p = {float(-h), 0, float(h - 2 * h * t)};
            }
            (*points)[1 + (r - 1) * perRing + i] = p;
            const double previousQ = std::max(0., double(r - 1 - 128) / 64),
                         previousH =
                             nearStep * (r - 1) + (extent - nearStep * rings) * std::pow(previousQ, 4);
            (*uv)[1 + (r - 1) * perRing + i].x = float(std::max(h - previousH, 2 * h / edge));
            const uint32_t a = 1 + (r - 1) * perRing + i, b = 1 + (r - 1) * perRing + (i + 1) % perRing;
            if (r == 1)
                indices.insert(indices.end(), {0, b, a});
            else {
                const uint32_t c = a - perRing, d = b - perRing;
                indices.insert(indices.end(), {c, b, a, c, d, b});
            }
        }
    }
    auto index = vsg::uintArray::create(indices.size());
    std::copy(indices.begin(), indices.end(), index->begin());
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays({points, normals, uv});
    draw->assignIndices(index);
    draw->indexCount = indices.size();
    draw->instanceCount = 1;
    depthSurface = pipeline(nullptr, draw, 1, true);
    surface = pipeline(nullptr, draw);
    auto p = vsg::vec3Array::create(3);
    (*p)[0] = {-1, -1, 0};
    (*p)[1] = {3, -1, 0};
    (*p)[2] = {-1, 3, 0};
    auto n = vsg::vec3Array::create(3, vsg::vec3(0, 1, 0));
    auto u = vsg::vec2Array::create(3, vsg::vec2{});
    auto ix = vsg::uintArray::create(3);
    (*ix)[0] = 0;
    (*ix)[1] = 1;
    (*ix)[2] = 2;
    auto bg = vsg::VertexIndexDraw::create();
    bg->assignArrays({p, n, u});
    bg->assignIndices(ix);
    bg->indexCount = 3;
    bg->instanceCount = 1;
    background = pipeline(nullptr, bg, 2);
    const auto makeCards=[](size_t emitterCount,int cardsPerEmitter) {
        auto positions=vsg::vec3Array::create(emitterCount*cardsPerEmitter*4);
        auto normals=vsg::vec3Array::create(positions->size(),vsg::vec3(0,1,0));
        auto texcoords=vsg::vec2Array::create(positions->size());
        auto indices=vsg::uintArray::create(emitterCount*cardsPerEmitter*6);
        for(size_t emitter=0;emitter<emitterCount;++emitter)
            for(int card=0;card<cardsPerEmitter;++card){
                const size_t index=emitter*cardsPerEmitter+card;
                const uint32_t base=uint32_t(index*4);
                for(int c=0;c<4;++c){
                    (*positions)[base+c]=vsg::vec3(float(emitter),float(card),0);
                    (*texcoords)[base+c]=vsg::vec2(c&1?1.f:-1.f,c&2?1.f:-1.f);
                }
                const uint32_t corners[]={base,base+1,base+2,base+2,base+1,base+3};
                for(size_t i=0;i<6;++i)(*indices)[index*6+i]=corners[i];
            }
        auto draw=vsg::VertexIndexDraw::create();
        draw->assignArrays({positions,normals,texcoords});
        draw->assignIndices(indices);
        draw->indexCount=indices->size();
        draw->instanceCount=1;
        return draw;
    };
    spray=vsg::Switch::create();
    spray->addChild(false,pipeline(nullptr,makeCards(VoxelOcean::maximumSplashes,48),3));
    spray->addChild(false,pipeline(nullptr,makeCards(maximumOceanPropellerJets,1),4));
}
vsg::ref_ptr<vsg::ImageInfo> VoxelOceanDisplay::foamImage() const { return effects_->foam; }
vsg::ref_ptr<vsg::StateGroup> VoxelOceanDisplay::pipeline(const VoxelSceneObject *object,
                                                          vsg::ref_ptr<vsg::VertexIndexDraw> draw, int mode,
                                                          bool depthOnly) {
    auto shader = vsg::ShaderSet::create(
        vsg::ShaderStages{earthShader("voxel_ocean.vert"),
            earthShader(mode==4?"voxel_ocean_plume.frag":"voxel_ocean.frag")});
    const VkShaderStageFlags cameraStages=mode==4?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT:
        VK_SHADER_STAGE_VERTEX_BIT;
    shader->addPushConstantRange("camera", "", cameraStages, 0, 128);
    shader->addAttributeBinding("position", "", 0, VK_FORMAT_R32G32B32_SFLOAT, {});
    shader->addAttributeBinding("normal", "", 1, VK_FORMAT_R32G32B32_SFLOAT, {});
    shader->addAttributeBinding("uv", "", 2, VK_FORMAT_R32G32_SFLOAT, {});
    shader->addDescriptorBinding("ocean", "", 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, {});
    for (const auto &[name, binding] : std::vector<std::pair<std::string, int>>{
             {"albedoMap", 1}, {"normalMap", 2}, {"mrMap", 3}, {"emissiveMap", 6}})
        shader->addDescriptorBinding(name, "", 0, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT, {});
    shader->addDescriptorBinding("meanSeaMap", "", 0, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, {});
    shader->addDescriptorBinding("airTransmission", "", 0, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, {});
    shader->addDescriptorBinding("weatherReflection", "", 0, 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, {});
    shader->addDescriptorBinding("foamMap", "", 0, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, {});
    shader->addDescriptorBinding("reflectionState", "", 0, 9, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_FRAGMENT_BIT, {});
    auto cfg = vsg::GraphicsPipelineConfigurator::create(shader);
    cfg->assignTexture("weatherReflection", vsg::ImageInfoList{cloud_->reflection});
    cfg->assignTexture("foamMap", vsg::ImageInfoList{effects_->foam});
    cfg->assignDescriptor("reflectionState", cloud_->reflectionState);
    cfg->enableArray("position", VK_VERTEX_INPUT_RATE_VERTEX, 12);
    cfg->enableArray("normal", VK_VERTEX_INPUT_RATE_VERTEX, 12);
    cfg->enableArray("uv", VK_VERTEX_INPUT_RATE_VERTEX, 8);
    auto params = vsg::vec4Array::create(voxelOceanUniformCount, vsg::vec4{});
    params->properties.dataVariance = vsg::DYNAMIC_DATA;
    (*params)[8] = vsg::vec4(1, 1, 1, 1);
    (*params)[9].x = object ? 0 : float(mode);
    if (depthOnly)
        (*params)[9].y = -1;
    auto image = [&](const std::vector<uint8_t> &bytes, uint32_t width, uint32_t height, VkFormat format,
                     vsg::ubvec4 fallback) {
        auto data =
            vsg::ubvec4Array2D::create(bytes.empty() ? 1 : width, bytes.empty() ? 1 : height, fallback);
        data->properties.format = format;
        if (!bytes.empty())
            std::memcpy(data->dataPointer(), bytes.data(), bytes.size());
        return data;
    };
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler->maxLod = 0;
    cfg->assignTexture("meanSeaMap", referenceImage_, sampler);
    cfg->assignTexture("airTransmission", rockyenv::referenceAtmosphereTransmittance(), sampler);
    const EarthSurfaceSnapshot *s = object ? &object->surface() : nullptr;
    const std::vector<uint8_t> empty;
    cfg->assignTexture("albedoMap",
                       image(s ? s->imagery_rgba : empty, s ? s->imagery_width : 1, s ? s->imagery_height : 1,
                             VK_FORMAT_R8G8B8A8_SRGB, {255, 255, 255, 255}),
                       sampler);
    cfg->assignTexture("normalMap",
                       image(s ? s->normal_rgba : empty, s ? s->imagery_width : 1, s ? s->imagery_height : 1,
                             VK_FORMAT_R8G8B8A8_UNORM, {128, 128, 255, 255}),
                       sampler);
    const auto appearance = object && object->volume ? object->volume->appearance()
                                                     : std::shared_ptr<const VoxelSurfaceAppearance>{};
    cfg->assignTexture("emissiveMap",
                       image(appearance ? appearance->emissive_rgba : empty, s ? s->imagery_width : 1,
                             s ? s->imagery_height : 1, VK_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 255}),
                       sampler);
    cfg->assignTexture("mrMap",
                       image(s ? s->mr_rgba : empty, s ? s->imagery_width : 1, s ? s->imagery_height : 1,
                             VK_FORMAT_R8G8B8A8_UNORM, {255, 180, 0, 255}),
                       sampler);
    if (object) {
        const auto &c = object->material.at("color_rgba");
        (*params)[8] = vsg::vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), c[3].get<float>());
        (*params)[9].y = !s->imagery_rgba.empty();
        (*params)[9].z = !s->normal_rgba.empty();
        (*params)[9].w = !s->mr_rgba.empty();
    }
    cfg->assignDescriptor("ocean", params);
    auto raster = vsg::RasterizationState::create();
    raster->cullMode = VK_CULL_MODE_NONE;
    cfg->pipelineStates.push_back(raster);
    if (!object) {
        auto depth = vsg::DepthStencilState::create();
        depth->depthWriteEnable = depthOnly ? VK_TRUE : VK_FALSE;
        depth->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        cfg->pipelineStates.push_back(depth);
        if (depthOnly) {
            auto colorMask = vsg::ColorBlendState::create();
            colorMask->attachments[0].colorWriteMask = 0;
            cfg->pipelineStates.push_back(colorMask);
        } else if (mode >= 1 && mode <= 4) {
            auto blend = vsg::ColorBlendState::create();
            blend->attachments[0].blendEnable = VK_TRUE;
            blend->attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend->attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend->attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend->attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            cfg->pipelineStates.push_back(blend);
        }
    }
    shader->variants.emplace(cfg->shaderHints, shader->stages);
    cfg->init();
    auto group = vsg::StateGroup::create();
    cfg->copyTo(group);
    group->addChild(draw);
    bindings_.push_back({object ? object->id : std::string{}, params});
    return group;
}
vsg::ref_ptr<vsg::StateGroup> VoxelOceanDisplay::object(const VoxelSceneObject &object,
                                                        vsg::ref_ptr<vsg::VertexIndexDraw> draw) {
    const auto found = reuseObjects_.find(object.id);
    if (found != reuseObjects_.end() && found->second.source == &object.surface() &&
        found->second.material == object.material) {
        objects_[object.id] = found->second;
        bindings_.push_back({object.id, found->second.parameters});
        return found->second.node;
    }
    auto node = pipeline(&object, draw);
    objects_[object.id] = {&object.surface(), object.material, node, bindings_.back().data};
    return node;
}
void writeVoxelOceanField(vsg::vec4Array &p, const VoxelOcean &water, double seconds) {
    const double base = std::floor(seconds / 256.) * 256.;
    p[0] = vsg::vec4(float(seconds - base), float(water.seaLevel), float(water.bottom),
                     float(water.harmonics.size()));
    p[6] = vsg::vec4(vsg::vec3(water.absorption), 1.333f);
    p[7] = vsg::vec4(vsg::vec3(water.scattering), float(water.windSpeed));
    for (size_t i = 0; i < water.harmonics.size(); ++i) {
        const auto &h = water.harmonics[i];
        p[18 + i] = vsg::vec4(float(h.amplitude), float(h.k), float(h.dx), float(h.dz));
        p[34 + i] = vsg::vec4(float(h.omega),
                              float(std::remainder(h.phase - h.omega * base, 6.283185307179586)), 0, 0);
    }
    p[51] = vsg::vec4(float(water.halfExtent), 0, 0, 0);
    p[127] = vsg::vec4(float(water.referenceX), float(water.referenceZ), float(water.referenceStepX),
                       float(water.referenceStepZ));
    p[128] = vsg::vec4(float(water.referenceSize), std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO") ? 1.f : 0.f,
                       float(water.referenceMin), float(water.referenceMax));
    p[166] = vsg::vec4(float(water.fogVisibility), float(water.fogHeight), 0,
                       std::getenv("VSGRAYSIM_OCEAN_LEGACY_EFFECTS") ? 1.f : 0.f);
    const double az = vsg::radians(water.windAzimuth);
    p[167] = vsg::vec4(float(std::sin(az) * water.windSpeed), float(-std::cos(az) * water.windSpeed), 0,
                       float(water.splashes.size()));
    for (size_t i = 0; i < water.splashes.size(); ++i) {
        const auto &e = water.splashes[i];
        p[168 + i * 2] = vsg::vec4(float(e.x), float(e.z), float(e.birth - base), float(e.height));
        p[169 + i * 2] = vsg::vec4(float(e.speed), float(e.radius), float(e.lifetime), float(e.seed % 65521));
        p[808 + i] = vsg::vec4(float(e.drift.x),float(e.drift.z),0,0);
    }
    p[218] = vsg::vec4(float(water.foam.size()), 0, 0, 0);
    for (size_t i = 0; i < water.foam.size(); ++i) {
        const auto &f = water.foam[i];
        p[220 + 3 * i] = vsg::vec4(float(f.x), float(f.z), float(seconds - f.birth), float(f.lifetime));
        p[221 + 3 * i] = vsg::vec4(float(f.length), float(f.width), float(f.dirX), float(f.dirZ));
        p[222 + 3 * i] = vsg::vec4(float(f.strength), float(f.seed % 65521), 0, 0);
    }
    p[50] = vsg::vec4(float(water.current.x), float(water.current.z), float(water.wakes.size()),
                      float(water.amplitudeBound));
    for (size_t i = 0; i < water.wakes.size(); ++i) {
        const auto &q = water.wakes[i];
        p[55 + 2 * i] = vsg::vec4(float(q.x), float(q.z), float(q.birth - base), float(q.amplitude));
        p[56 + 2 * i] = vsg::vec4(float(q.width), float(q.speed), float(q.lifetime), float(q.k));
        const double age = seconds - q.birth,
                     life = age > 0 && age < q.lifetime ? std::pow(1 - age / q.lifetime, 3) : 0;
        const double gain = life * (-std::expm1(-6 * std::max(0., age))) * std::exp(-.38 * std::max(0., age));
        p[103 + i] = vsg::vec4(float(q.dirX), float(q.dirZ), float(q.bodySpeed), float(q.amplitude * gain));
        p[129 + i] = vsg::vec4(
            float(age > 0 && age < q.lifetime
                      ? std::clamp(q.bodySpeed * .16 + q.amplitude * 5., 0., .8) * (-std::expm1(-5 * age)) *
                            std::exp(-.55 * age) * std::pow(1 - age / q.lifetime, 2)
                      : 0),
            float(std::min(.12, q.amplitude * 3.) * std::exp(-.7 * std::max(0., age))), 0, 0);
    }
}
void VoxelOceanDisplay::update(const VoxelSceneSnapshot &scene, const vsg::dmat4 &vp, const vsg::dvec3 &eye,
                               const rockyenv::AtmosphereState &light, bool atmosphere,double weatherSeconds) {
    reuseObjects_.clear();
    const vsg::vec2 foamOrigin(float(std::floor(eye.x / 64) * 64 - 512),
                               float(std::floor(eye.z / 64) * 64 - 512));
    const auto count = effects_->recorded->load();
    if (count != effects_->lastRecord) {
        effects_->lastRecord = count;
        effects_->seconds = effects_->pendingSeconds;
        effects_->origin = effects_->pendingOrigin;
        effects_->ocean = effects_->pendingOcean;
    }
    const bool foamEnabled =
        !std::getenv("VSGRAYSIM_OCEAN_NO_PERSISTENT_FOAM") && !commands->children.empty();
    const bool foamDirty = effects_->lastRecord == 0 || effects_->seconds != scene.timeSeconds() ||
                           effects_->origin != foamOrigin || effects_->ocean != scene.ocean;
    commands->setAllChildren(foamEnabled && foamDirty);
    if (foamEnabled && foamDirty) {
        writeVoxelOceanField(*effects_->params, *scene.ocean, scene.timeSeconds());
        (*effects_->params)[217] = vsg::vec4(foamOrigin.x, foamOrigin.y, 1.f / 1024, 1);
        effects_->params->dirty();
    }
    if (foamEnabled && foamDirty) {
        effects_->pendingSeconds = scene.timeSeconds();
        effects_->pendingOrigin = foamOrigin;
        effects_->pendingOcean = scene.ocean;
    }
    const auto &water = *scene.ocean;
    const auto jets=oceanPropellerJets(scene);
    const bool effectsEnabled=!std::getenv("VSGRAYSIM_OCEAN_LEGACY_EFFECTS");
    spray->children[0].mask=effectsEnabled&&!water.splashes.empty()?vsg::MASK_ALL:vsg::MASK_OFF;
    spray->children[1].mask=effectsEnabled&&!jets.empty()&&eye.y<water.surface(eye.x,eye.z,scene.timeSeconds()).height+.06
        ?vsg::MASK_ALL:vsg::MASK_OFF;
    const auto chart = simcore::geo::makeEnuToWorldMatrix(scene.anchor);
    std::vector<vsg::vec4> regions;
    for (const auto &[id, o] : scene.objects)
        if (!o.earth) {
            const auto box = o.localBounds();
            const auto pose = o.pose(scene.tick, scene.fixedStep);
            vsg::dvec3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
            for (int k = 0; k < 8; ++k) {
                const auto p = pose * vsg::dvec3(k & 1 ? box.max.x : box.min.x, k & 2 ? box.max.y : box.min.y,
                                                 k & 4 ? box.max.z : box.min.z);
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min(lo[a], p[a]);
                    hi[a] = std::max(hi[a], p[a]);
                }
            }
            if (lo.y > water.seaLevel + water.referenceMax + water.amplitudeBound)
                continue;
            const double margin = std::clamp((water.meanSurface(lo.x, lo.z).x - lo.y) * .8 + 2., 2., 20.);
            vsg::vec4 r(float(lo.x - margin), float(lo.z - margin), float(hi.x + margin),
                        float(hi.z + margin));
            size_t into = regions.size();
            for (size_t i = 0; i < regions.size(); ++i)
                if (r.x <= regions[i].z && r.z >= regions[i].x && r.y <= regions[i].w &&
                    r.w >= regions[i].y) {
                    into = i;
                    break;
                }
            if (into == regions.size() && regions.size() < 8)
                regions.push_back(r);
            else {
                if (into == regions.size()) {
                    into = 0;
                    double best = 1e30;
                    for (size_t i = 0; i < regions.size(); ++i) {
                        const double cost = std::pow((regions[i].x + regions[i].z - r.x - r.z), 2) +
                                            std::pow((regions[i].y + regions[i].w - r.y - r.w), 2);
                        if (cost < best) {
                            best = cost;
                            into = i;
                        }
                    }
                }
                auto &b = regions[into];
                b = vsg::vec4(std::min(b.x, r.x), std::min(b.y, r.y), std::max(b.z, r.z), std::max(b.w, r.w));
            }
        }
    std::vector<vsg::vec4> hulls;
    for (const auto &[id, o] : scene.objects)
        if (!o.earth && o.body.motion != "static" && hulls.size() < 16) {
            const auto box = o.localBounds();
            const auto size = box.max - box.min;
            const auto pose = o.pose(scene.tick, scene.fixedStep);
            const auto c = pose * ((box.min + box.max) * .5);
            double ry = 0;
            for (int a = 0; a < 3; ++a)
                ry += std::abs(pose[a][1]) * size[a] * .5;
            if (std::abs(c.y - water.surface(c.x, c.z, scene.timeSeconds()).height) > ry + .1)
                continue;
            const double yaw = std::atan2(pose[0][2], pose[0][0]);
            const auto velocity = o.velocity - water.current;
            const double speed = std::hypot(velocity.x, velocity.z);
            hulls.push_back({float(c.x), float(c.z), float(size.x * .5), float(size.z * .5)});
            hulls.push_back({float(std::cos(yaw)), float(std::sin(yaw)), float(speed),
                             float(std::clamp(speed * .3 + std::abs(velocity.y) * .12, 0., .8))});
        }
    const double appearanceTime=weatherSeconds>=0.?weatherSeconds:scene.timeSeconds();
    const auto weather=scene.weather.enabled?scene.weather.sample(appearanceTime):SceneWeatherState{};
    const double windSpeed=scene.weather.enabled?weather.windSpeed:water.windSpeed;
    const double windAzimuth=vsg::radians(scene.weather.enabled?weather.windAzimuth:water.windAzimuth);
    const double windEast=std::sin(windAzimuth)*windSpeed;
    const double windNorth=std::cos(windAzimuth)*windSpeed;
    const auto travel=scene.weather.enabled?scene.weather.windTravel(appearanceTime):
        std::array<double,2>{windEast*appearanceTime,windNorth*appearanceTime};
    // Sub-grid slope variation supplements the shared, resolved height field.
    // Whitecap coverage uses the Monahan/O Muircheartaigh wind dependence as
    // an appearance guide. Bound the extrapolation for extreme winds.
    const float windRoughness=float(.063+.010*std::min(windSpeed,26.));
    const float whitecapCoverage=float(std::min(.16,2.95e-6*std::pow(windSpeed,3.52)));
    for (auto &binding : bindings_) {
        auto &p = *binding.data;
        writeVoxelOceanField(p, water, scene.timeSeconds());
        p[218].y=float(jets.size());
        for(size_t i=0;i<jets.size();++i){
            const auto& jet=jets[i];
            p[796+3*i]=vsg::vec4(vsg::vec3(jet.hub),float(jet.radius));
            p[797+3*i]=vsg::vec4(vsg::vec3(jet.plume),float(jet.age));
            p[798+3*i]=vsg::vec4(float(jet.spin),float(jet.phase),
                float(std::clamp(std::abs(jet.spin)*jet.radius/12.,0.,1.)),0);
        }
        if(scene.weather.enabled) {
            p[166].x=weather.visibility;p[166].y=weather.fogHeight;
            p[166].z=std::min(1.,weather.rain/40.);
        }
        p[167].x=float(windEast);
        p[167].y=float(-windNorth);
        p[219]=vsg::vec4(float(std::fmod(appearanceTime,256.)),
                         float(water.current.x*scene.timeSeconds()+travel[0]*.028),
                         float(water.current.z*scene.timeSeconds()-travel[1]*.028),0.f);
        p[217] = vsg::vec4(foamOrigin.x, foamOrigin.y, 1.f / 1024, foamEnabled ? 1.f : 0.f);
        for (int i = 0; i < 8; ++i)
            p[153 + i] = i < int(regions.size()) ? regions[i] : vsg::vec4{};
        for (int i = 0; i < 4; ++i)
            p[161 + i] = vsg::vec4(chart[i]);
        for (int i = 0; i < 16; ++i)
            p[200 + i] = i < int(hulls.size()) ? hulls[i] : vsg::vec4{};
        p[216] = vsg::vec4(float(hulls.size() / 2), 0, 0, 0);
        p[165] = vsg::vec4(light.turbidity, atmosphere ? 1.f : 0.f, windRoughness, whitecapCoverage);
        p[51].z = float(std::floor(eye.x / 8) * 8);
        p[51].w = float(std::floor(eye.z / 8) * 8);
        p[51].y = float(water.surface(eye.x, eye.z, scene.timeSeconds()).height);
        p[1] = vsg::vec4(vsg::vec3(eye), light.exposure);
        p[2] = vsg::vec4(light.sun_dir_enu, light.is_day);
        p[3] = vsg::vec4(light.moon_dir_enu, light.moon_phase);
        const auto fogLight=scene.weather.enabled?rockyenv::precipitationFogLight(light.horizon_color,
            p[166].z,std::min(1.f,float(weather.snow/8.)),float(weather.overcast)):light.horizon_color;
        p[4] = vsg::vec4(fogLight, std::max(.1f, light.extinction_coeff));
        p[5] = vsg::vec4(light.zenith_color, light.is_day);
        p[6] = vsg::vec4(vsg::vec3(water.absorption), 1.333f);
        p[7] = vsg::vec4(vsg::vec3(water.scattering), float(windSpeed));
        for (int i = 0; i < 4; ++i)
            p[10 + i] = vsg::vec4(vp[i]);
        const auto object = binding.id.empty()
                                ? vsg::dmat4{}
                                : scene.objects.at(binding.id).pose(scene.tick, scene.fixedStep);
        for (int i = 0; i < 4; ++i)
            p[14 + i] = vsg::vec4(object[i]);
        p[52] = vsg::vec4(light.sun_light * 2.5f, 0);
        p[53] = vsg::vec4(light.ambient_light, 0);
        p[54] = vsg::vec4(light.moon_light, 0);
        binding.data->dirty();
    }
}
} // namespace vsgraysim::earth
