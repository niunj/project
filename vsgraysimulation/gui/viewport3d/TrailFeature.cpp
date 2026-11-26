#include "TrailFeature.h"

#include "FeatureRegistry.h"
#include "FlightTrailCurve.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "VoxelSceneFeature.h"
#include "VsgQtWidget.h"

#include <simcore/GeoFrame.h>
#include <vsg/all.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim::viewport3d {
namespace {
constexpr double life = 18.;
constexpr double maxSampleGap = 2.;
constexpr size_t maxTracks = 128, maxSamples = 400, maxSegments = 16384;

bool finite(const vsg::dvec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

vsg::vec4 trailColor(int faction)
{
    if (faction==1) return {.33f,.65f,1.f,1.f};
    if (faction==2) return {1.f,.36f,.30f,1.f};
    return {.81f,.86f,.93f,1.f};
}

vsg::vec4 direction(const vsg::dvec3& tangent, const vsg::dvec3& fallback)
{
    const auto vector=vsg::length(tangent)>1e-8 ? tangent : fallback;
    return {vsg::vec3(vsg::normalize(vector)),0.f};
}
}

void TrailFeature::configure(const nlohmann::json& params)
{
    smooth_=params.value("smooth_sampling",smooth_);
    dirty_=true;
}

void TrailFeature::onAttach(ViewportContext& ctx)
{
    ctx_=&ctx;
    const auto path=gui::ViewportKit::resolveAssetPath("shaders/viewport");
    vsg::ShaderStages stages{
        vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT,"main",path+"/observed_trail.vert"),
        vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT,"main",path+"/observed_trail.frag")};
    auto compiler=vsg::ShaderCompiler::create();
    if (!stages[0] || !stages[1] || !compiler || !compiler->supported() || !compiler->compile(stages)) {
        spdlog::warn("TrailFeature: could not compile the observation trail shaders");
        return;
    }
    auto shader=vsg::ShaderSet::create(stages);
    shader->addPushConstantRange("camera","",VK_SHADER_STAGE_VERTEX_BIT,0,128);
    shader->addDescriptorBinding("segments","",0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,{});
    shader->addDescriptorBinding("parameters","",0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,{});
    segments_=vsg::vec4Array::create(5*maxSegments,vsg::vec4{});
    parameters_=vsg::vec4Array::create(1,vsg::vec4{});
    segments_->properties.dataVariance=parameters_->properties.dataVariance=vsg::DYNAMIC_DATA;
    auto pipeline=vsg::GraphicsPipelineConfigurator::create(shader);
    pipeline->assignDescriptor("segments",segments_);
    pipeline->assignDescriptor("parameters",parameters_);
    for (const auto& state:pipeline->pipelineStates) {
        if (auto depth=state.cast<vsg::DepthStencilState>()) {
            depth->depthWriteEnable=VK_FALSE;
            depth->depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL;
        }
        if (auto raster=state.cast<vsg::RasterizationState>()) raster->cullMode=VK_CULL_MODE_NONE;
        if (auto blend=state.cast<vsg::ColorBlendState>()) for (auto& a:blend->attachments) {
            a.blendEnable=VK_TRUE;
            a.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;
            a.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
            a.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    shader->variants.emplace(pipeline->shaderHints,shader->stages);
    pipeline->init();
    auto group=vsg::StateGroup::create();
    pipeline->copyTo(group);
    draw_=vsg::Draw::create(6,0,0,0);
    group->addChild(draw_);
    auto layer=vsg::Layer::create();
    layer->binNumber=1;
    layer->child=group;
    root_=vsg::Group::create();
    root_->addChild(layer);
    if (enabled_) mount(true);
}

void TrailFeature::onDetach(ViewportContext&)
{
    mount(false);
    clear();
    root_={};
    draw_={};
    segments_={};
    parameters_={};
    ctx_=nullptr;
}

void TrailFeature::setEnabled(bool on)
{
    enabled_=on;
    mount(on);
}

void TrailFeature::mount(bool visible)
{
    if (!root_ || !ctx_) return;
    const auto wanted=visible ? ctx_->contentRoot() : vsg::ref_ptr<vsg::Group>{};
    if (wanted==attachedRoot_) return;
    if (attachedRoot_ && wanted) clear();
    if (attachedRoot_) {
        auto& children=attachedRoot_->children;
        children.erase(std::remove(children.begin(),children.end(),root_),children.end());
    }
    attachedRoot_=wanted;
    if (attachedRoot_) attachedRoot_->addChild(root_);
    ctx_->requestCompile();
}

void TrailFeature::addPoint(const std::string& id, const vsg::dvec3& pos, int faction,
                            double time, std::optional<vsg::dvec3> velocity)
{
    if (id.empty() || !finite(pos) || !std::isfinite(time) || time<0.) return;
    if (velocity && !finite(*velocity)) velocity.reset();
    if (time<lastSampleTime_) clear();
    if (lastSampleTime_<0.) clockOrigin_=time;
    lastSampleTime_=time;
    if (tracks_.size()>=maxTracks && tracks_.find(id)==tracks_.end()) return;
    auto& track=tracks_[id];
    track.faction=faction;
    auto& history=track.samples;
    bool startsRun=history.empty();
    if (!history.empty()) {
        const auto& previous=history.back();
        const double dt=time-previous.time;
        const double distance=vsg::length(pos-previous.position);
        if (dt==0. && distance<.02) return;
        if (dt==0.) {
            history.pop_back();
            startsRun=true;
        } else {
            startsRun=dt>maxSampleGap;
            if (velocity && previous.velocity) {
                const double speed=std::max(vsg::length(*velocity),vsg::length(*previous.velocity));
                startsRun=startsRun || distance>std::max(25.,speed*dt*2.5+10.);
            }
        }
    }
    history.push_back({pos,time,velocity,startsRun});
    while (history.size()>maxSamples) history.pop_front();
    dirty_=true;
}

void TrailFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    if (!draw_) return;
    mount(true);
    sampleVoxelTracks(ctx);
    visibleTime_=tick.simulation_clock ? tick.elapsed_sec : std::max(tick.elapsed_sec,lastSampleTime_);
    for (auto it=tracks_.begin(); it!=tracks_.end();) {
        auto& history=it->second.samples;
        while (!history.empty() && history.front().time<visibleTime_-life-1.) {
            history.pop_front();
            dirty_=true;
        }
        if (history.empty()) it=tracks_.erase(it);
        else ++it;
    }
    if (dirty_) refreshSegments();
    const auto* widget=ctx.widget();
    const float pixelScale=float(widget->devicePixelRatioF());
    const vsg::vec4 values(std::max(1.f,float(widget->width())*pixelScale),
                           std::max(1.f,float(widget->height())*pixelScale),
                           float(visibleTime_-clockOrigin_),float(life));
    if ((*parameters_)[0]!=values) {
        (*parameters_)[0]=values;
        parameters_->dirty();
    }
}

void TrailFeature::sampleVoxelTracks(ViewportContext& ctx)
{
    const auto* feature=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    const auto snapshot=feature && feature->enabled() ? feature->snapshot() : nullptr;
    if (!snapshot) return;
    const double time=snapshot->timeSeconds();
    if (time<lastVoxelTime_) clear();
    if (lastVoxelTime_>=0. && time-lastVoxelTime_<.099) return;
    lastVoxelTime_=time;
    const auto chart=vsg::inverse(ctx.contentModel())*simcore::geo::makeEnuToWorldMatrix(snapshot->anchor);
    for (const auto& [id,object]:snapshot->objects) {
        if (object.role!="target" || !object.parentId.empty() || object.body.motion=="static") continue;
        const auto flag=object.provenance.find("visual_flight_trail");
        const auto file=object.provenance.find("path");
        const std::string path=file!=object.provenance.end() && file->is_string() ? file->get<std::string>() : std::string{};
        const bool show=flag!=object.provenance.end() && flag->is_boolean() ? flag->get<bool>() :
            path.find("F-16.")!=std::string::npos || path.find("MQ-9.")!=std::string::npos || path.find("AH-64.")!=std::string::npos;
        if (!show) continue;
        const auto pose=object.pose(snapshot->tick,snapshot->fixedStep);
        const auto p=chart*(pose*vsg::dvec3{});
        const auto v=chart*vsg::dvec4(object.velocity,0.);
        addPoint(id,p,0,time,vsg::dvec3(v.x,v.y,v.z));
    }
}

void TrailFeature::removeTrack(const std::string& id)
{
    dirty_=tracks_.erase(id)>0 || dirty_;
}

void TrailFeature::clear()
{
    tracks_.clear();
    lastSampleTime_=-1.;
    lastVoxelTime_=-1.;
    dirty_=true;
    if (draw_) draw_->instanceCount=0;
}

unsigned TrailFeature::segmentCount() const
{
    return draw_ ? draw_->instanceCount : 0;
}

void TrailFeature::refreshSegments()
{
    size_t count=0;
    for (const auto& [id, track]:tracks_) {
        const auto& history=track.samples;
        const auto color=trailColor(track.faction);
        for (size_t i=1; i<history.size() && count<maxSegments; ++i) {
            const auto& a=history[i-1];
            const auto& b=history[i];
            const auto chord=b.position-a.position;
            if (b.startsRun || vsg::length(chord)<.02) continue;
            const double dt=b.time-a.time;
            const FlightTrailCurve curve(a.position,b.position,a.velocity,b.velocity,dt);
            const bool smooth=smooth_ && (a.velocity || b.velocity);
            const int steps=smooth ? std::clamp(int(std::ceil(dt*16.)),2,10) : 1;
            const auto tangent=[&](const Sample& sample, double t) {
                if (smooth && sample.velocity) {
                    const auto d=curve.tangent(t);
                    if (vsg::length(d)>1e-8) return direction(d,chord);
                }
                return direction(chord,chord);
            };
            auto left=vsg::vec4(vsg::vec3(a.position),float(a.time-clockOrigin_));
            auto leftDirection=tangent(a,0.);
            for (int step=1; step<=steps && count<maxSegments; ++step) {
                const double t=double(step)/steps;
                const auto point=smooth ? curve.at(t) : b.position;
                const auto right=vsg::vec4(vsg::vec3(point),float(a.time+dt*t-clockOrigin_));
                const auto rightDirection=step==steps ? tangent(b,1.) : direction(curve.tangent(t),chord);
                const size_t at=count++*5;
                (*segments_)[at]=left;
                (*segments_)[at+1]=right;
                (*segments_)[at+2]=leftDirection;
                (*segments_)[at+3]=rightDirection;
                (*segments_)[at+4]=color;
                left=right;
                leftDirection=rightDirection;
            }
        }
    }
    draw_->instanceCount=uint32_t(count);
    if (count) segments_->dirty();
    dirty_=false;
}

VSGVP_REGISTER_FEATURE(TrailFeature);

} // namespace vsgraysim::viewport3d
