#include "PlatformMotionFeature.h"
#include "PlatformAnimationEffects.h"
#include "AircraftExterior.h"
#include "EngagementVisualFeature.h"
#include "FeatureRegistry.h"
#include "ViewportContext.h"
#include "ViewportKit.h"
#include "VoxelSceneFeature.h"
#include <simcore/GeoFrame.h>
#include <spdlog/spdlog.h>

#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace vsgraysim::viewport3d {
namespace {
using D=vsg::dvec3;
using C=vsg::vec4;
using Transform=vsg::ref_ptr<vsg::MatrixTransform>;
constexpr D axisX(1,0,0);
std::atomic_uint64_t nextPreviewId{0};
constexpr C rubber(.071f,.077f,.067f,1.f),deck(.31f,.35f,.36f,1.f);

Transform child(vsg::Group& parent,vsg::ref_ptr<vsg::Node> model={}) {
    auto local=vsg::MatrixTransform::create();
    if(model)local->addChild(model);
    parent.addChild(local);
    return local;
}
struct Shapes {
    vsg::ref_ptr<vsg::Builder> builder=vsg::Builder::create();
    vsg::ref_ptr<vsg::Node> box(D p,D width,C color) const {
        vsg::GeometryInfo info;
        info.position=vsg::vec3(p);info.dx={float(width.x),0,0};info.dy={0,float(width.y),0};info.dz={0,0,float(width.z)};
        info.color=color;
        return builder->createBox(info);
    }
    vsg::ref_ptr<vsg::Node> cylinder(D p,double radius,double length,C color) const {
        vsg::GeometryInfo info;
        info.position=vsg::vec3(p);info.dx={float(radius*2),0,0};info.dy={0,float(radius*2),0};info.dz={0,0,float(length)};
        info.color=color;
        return builder->createCylinder(info);
    }
};

struct TrackOutline {
    std::vector<D> points;
    std::vector<double> distances{0.};
    std::pair<D,double> sample(double distance) const {
        const double total=distances.back();
        double s=std::fmod(distance,total);
        if(s<0.)s+=total;
        const auto i=size_t(std::upper_bound(distances.begin(),distances.end(),s)-distances.begin()-1);
        const auto& from=points[i];
        const auto offset=points[i+1]-from;
        const auto along=vsg::normalize(offset);
        // Just outside the textured belt; the source belt still defines its silhouette.
        const D outward(0.,along.z,-along.y);
        return {from+along*(s-distances[i])+outward*.008,std::atan2(along.y,-along.z)};
    }
};

D vector3(const platform_animation::Vec3& values) {
    return {values[0],values[1],values[2]};
}

D vector3(const nlohmann::json& values) {
    return {values.at(0).get<double>(),values.at(1).get<double>(),values.at(2).get<double>()};
}

vsg::ref_ptr<vsg::Node> displayProp(const Shapes& shapes) {
    auto model=vsg::Group::create();
    model->addChild(shapes.cylinder({0,0,.16},.135,1.65,{.74,.77,.77,1}));
    model->addChild(shapes.cylinder({0,0,.71},.138,.11,{.62,.36,.09,1}));
    vsg::GeometryInfo nose;
    nose.position={0,0,-.83};nose.dx={.27,0,0};nose.dy={0,-.27,0};nose.dz={0,0,-.33};
    nose.color=C(.49,.51,.53,1);
    model->addChild(shapes.builder->createCone(nose));
    return model;
}
}

struct PlatformMotionFeature::Rig {
    std::string subject="tracked",shipDirectory,tankDirectory,modelId,partManifest;
    std::string effectOwner="platform-preview:"+std::to_string(++nextPreviewId);
    double previewCue=1.20;
    ViewportContext* context=nullptr;
    vsg::ref_ptr<vsg::Group> content;
    Transform root;
    vsg::ref_ptr<vsg::Switch> visibility;
    std::optional<std::array<double,2>> displayAngles;
    std::unique_ptr<platform_animation::PlatformAnimation> animation;
    platform_animation::AnimationFrame frame;

    struct Joint {
        Transform node;
        D pivot{};
        vsg::dmat4 rest;
        vsg::ref_ptr<vsg::Switch> visibility;
    };
    struct Attachment {
        std::vector<Transform> path;
        D position;
    };
    std::map<std::string,Joint,std::less<>> joints;
    std::map<std::string,Attachment,std::less<>> attachments;
    struct Part {Transform node;D position;double radius;};
    std::vector<Part> wheels;
    std::array<std::vector<Transform>,2> pads;
    std::array<TrackOutline,2> outlines;
    vsg::dmat4 pose;
    D antennaPivot{},hatchOrigin{0,7.,-63.};
    double treadPhaseRadius=1.;
    double travel=0.,lastTime=-1.,throttle=0.;
    PoseState published;

    Transform joint(const char* name, vsg::Group& parent, D pivot={}, const vsg::dmat4& rest={}) {
        auto node=child(parent);
        node->matrix=rest;
        joints.emplace(name,Joint{node,pivot,rest,{}});
        return node;
    }

    std::optional<vsg::dmat4> attachment(std::string_view name) const {
        const auto it=attachments.find(name);
        if(it==attachments.end()) {
            const auto offset=animation->modelAttachment(name,15.);
            if(!offset)return {};
            return pose*vsg::translate(vector3(*offset));
        }
        auto matrix=pose;
        for(const auto& node:it->second.path)matrix=matrix*node->matrix;
        return matrix*vsg::translate(it->second.position);
    }

    void apply(const platform_animation::JointPose& sample) {
        const auto it=joints.find(sample.joint);
        if(it==joints.end())return;
        const auto& part=it->second;
        part.node->matrix=vsg::translate(vector3(sample.offset))*vsg::translate(part.pivot)*
            vsg::rotate(sample.angle,vector3(sample.axis))*vsg::translate(-part.pivot)*part.rest;
        if(part.visibility)part.visibility->children.front().mask=sample.visible?vsg::MASK_ALL:vsg::MASK_OFF;
        if(sample.joint=="turret")published.turretYaw=sample.angle;
        else if(sample.joint=="elevation")published.barrelElevation=sample.angle;
        else if(sample.joint=="recoil")published.recoil=-sample.offset[2];
        else if(sample.joint=="antenna")published.antennaAngle=sample.angle;
        else if(sample.joint=="left_cover")published.hatch=sample.angle/1.36;
        else if(sample.joint=="prop"||sample.joint=="voxel_prop") {
            published.propVisible=sample.visible;
            published.propPosition=part.node->matrix*D{};
        }
    }

    void loadParts() {
        const auto file=std::filesystem::path(partManifest);
        nlohmann::json manifest;
        std::ifstream input(file);
        if(!input)throw std::runtime_error("visual parts manifest is missing: "+partManifest);
        input>>manifest;
        if(manifest.at("version")!=1)throw std::runtime_error("unsupported visual parts manifest");
        auto options=vsg::Options::create(*context->options());
        options->sharedObjects=vsg::SharedObjects::create();
        auto mesh=[&](const std::string& asset) {
            const auto path=file.parent_path()/asset;
            auto node=gui::ViewportKit::loadModelNode(path.string(),options);
            if(!node)throw std::runtime_error("visual part is missing: "+path.string());
            return node;
        };
        root->addChild(mesh(manifest.at("body").get<std::string>()));
        std::map<std::string,std::vector<Transform>,std::less<>> paths{{"",{}}};
        for(const auto& part:manifest.at("joints")) {
            const auto name=part.at("name").get<std::string>();
            const auto parent=part.value("parent",std::string());
            if(name.empty()||paths.count(name)||!paths.count(parent))
                throw std::runtime_error("invalid joint name or parent: "+name);
            auto path=paths.at(parent);
            auto& parentNode=path.empty()?*root:*path.back();
            auto node=joint(name.c_str(),parentNode,vector3(part.at("pivot_m")));
            if(part.contains("mesh")) {
                auto visibility=vsg::Switch::create();
                visibility->addChild(true,mesh(part.at("mesh").get<std::string>()));
                node->addChild(visibility);
                joints.at(name).visibility=visibility;
            }
            path.push_back(node);
            paths.emplace(name,std::move(path));
        }
        for(const auto& socket:manifest.at("attachments")) {
            const auto name=socket.at("name").get<std::string>();
            const auto parent=socket.value("joint",std::string());
            if(name.empty()||attachments.count(name)||!paths.count(parent))
                throw std::runtime_error("invalid attachment name or joint: "+name);
            attachments.emplace(name,Attachment{paths.at(parent),vector3(socket.at("position_m"))});
        }
    }

    void buildM1a2(const Shapes& shapes) {
        const auto directory=std::filesystem::path(tankDirectory);
        nlohmann::json manifest;
        std::ifstream input(directory/"m1a2-rig.json");
        if(!input)throw std::runtime_error("M1A2 visual rig manifest is missing");
        input>>manifest;
        const auto options=vsg::Options::create(*context->options());
        options->sharedObjects=vsg::SharedObjects::create();
        const auto load=[&](const nlohmann::json& file) {
            auto mesh=gui::ViewportKit::loadModelNode((directory/file.get<std::string>()).string(),options);
            if(!mesh)throw std::runtime_error("M1A2 visual part is missing: "+file.get<std::string>());
            return mesh;
        };
        root->addChild(load(manifest.at("hull")));
        auto turret=joint("turret",*root,vector3(manifest.at("turret_pivot_m")));
        turret->addChild(load(manifest.at("turret")));
        auto elevation=joint("elevation",*turret,vector3(manifest.at("barrel_pivot_m")));
        auto barrel=joint("recoil",*elevation);
        barrel->addChild(load(manifest.at("barrel")));
        attachments["muzzle"]={{turret,elevation,barrel},vector3(manifest.at("muzzle_m"))};
        const auto& wheelsJson=manifest.at("wheels");
        const auto& tracks=manifest.at("tracks");
        if(wheelsJson.empty()||tracks.size()!=2)throw std::runtime_error("unexpected M1A2 wheel or track count");
        for(const auto& wheel:wheelsJson) {
            const double radius=wheel.at("radius_m").get<double>();
            if(!std::isfinite(radius)||radius<=0.)throw std::runtime_error("invalid M1A2 wheel radius");
            wheels.push_back({child(*root,load(wheel.at("file"))),vector3(wheel.at("pivot_m")),radius});
        }
        treadPhaseRadius=wheels[std::min<size_t>(1,wheels.size()-1)].radius;
        for(size_t side=0;side<tracks.size();++side) {
            const auto& belt=tracks[side];
            auto& outline=outlines[side];
            const double x=belt.at("x").get<double>();
            for(const auto& coord:belt.at("outline_yz"))outline.points.emplace_back(x,coord.at(0).get<double>(),coord.at(1).get<double>());
            if(outline.points.size()<4)throw std::runtime_error("invalid M1A2 tread outline");
            outline.points.push_back(outline.points.front());
            for(size_t i=1;i<outline.points.size();++i) {
                const double segment=vsg::length(outline.points[i]-outline.points[i-1]);
                if(!std::isfinite(segment)||segment<1e-6)throw std::runtime_error("invalid M1A2 tread segment");
                outline.distances.push_back(outline.distances.back()+segment);
            }
            const double length=outline.distances.back();
            const size_t count=size_t(std::clamp(std::round(length/.16),16.,168.));
            const auto lug=shapes.box({}, {belt.at("width_m").get<double>()*.90,.026,length/count*.53},rubber);
            for(size_t i=0;i<count;++i)pads[side].push_back(child(*root,lug));
        }
    }

    void buildShip(const Shapes& shapes) {
        const auto dir=std::filesystem::path(shipDirectory);
        auto hull=gui::ViewportKit::loadModelNode((dir/"hull-split.obj").string(),context->options());
        auto mesh=gui::ViewportKit::loadModelNode((dir/"antenna-split.obj").string(),context->options());
        if(!hull||!mesh)throw std::runtime_error("CG-57 preview requires the split hull and antenna visual assets");
        vsg::ComputeBounds raw;hull->accept(raw);
        const auto span=raw.bounds.max-raw.bounds.min;
        auto aligned=child(*root);
        const bool zUp=span.z>span.y;
        vsg::dmat4 rotation(0,0,-1,0,zUp?-1.:0.,zUp?0.:1.,0,0,zUp?0.:1.,zUp?1.:0.,0,0,0,0,0,1);
        aligned->matrix=rotation;
        aligned->addChild(hull);
        vsg::ComputeBounds bounds;aligned->accept(bounds);
        auto centre=(bounds.bounds.min+bounds.bounds.max)*.5;
        aligned->matrix=vsg::translate(-centre.x,0.,-centre.z)*rotation;
        auto antenna=joint("antenna",*root);
        auto antennaAsset=child(*antenna,mesh);
        antennaAsset->matrix=aligned->matrix;
        vsg::ComputeBounds aerial;antennaAsset->accept(aerial);
        if(!aerial.bounds.valid())throw std::runtime_error("CG-57 antenna bounds missing");
        antennaPivot=(aerial.bounds.min+aerial.bounds.max)*.5;
        joints.at("antenna").pivot=antennaPivot;
        auto deckRay=vsg::LineSegmentIntersector::create(hatchOrigin+D(0,50.,0),hatchOrigin-D(0,25.,0));
        aligned->accept(*deckRay);
        if(!deckRay->intersections.empty()) {
            const auto nearest=std::min_element(deckRay->intersections.begin(),deckRay->intersections.end(),
                [](const auto& a,const auto& b){return a->ratio<b->ratio;});
            hatchOrigin.y=(*nearest)->worldIntersection.y+.38;
        }
        root->addChild(shapes.box(hatchOrigin+D(0,-.18,0),{4.9,.36,5.40},deck));
        root->addChild(shapes.box(hatchOrigin+D(0,.015,0),{4.30,.045,4.84},rubber));
        for(const double side : {-1.,1.}) {
            const auto hinge=hatchOrigin+D(side*2.09,0,0);
            auto cover=joint(side<0.?"left_cover":"right_cover",*root,hinge,vsg::translate(hinge));
            cover->addChild(shapes.box({-side*1.035,.075,0},{2.06,.13,4.62},{.45,.49,.49,1}));
        }
        attachProp(shapes,hatchOrigin+D(0.,1.2,0.));
    }
    void buildAircraft(const Shapes& shapes) {
        bool substitute=false;
        auto model=loadAircraftExterior(gui::ViewportKit::resolveAssetPath("models/F-16/F-16.obj"),"y","aircraft",15,context->options(),substitute,180);
        if(!model||substitute)throw std::runtime_error("F-16 preview model unavailable");
        root->addChild(model);
        root->addChild(shapes.box({-2.0,-.89,.62},{.18,.27,.76},{.27,.29,.31,1}));
        attachProp(shapes,{-2.0,-1.2,.30});
    }
    void buildDisplayVehicle(const Shapes& shapes) {
        auto path=gui::ViewportKit::resolveAssetPath("build/platform-motion-assets/display-vehicle/Himars.obj");
        if(!std::filesystem::is_regular_file(path))
            throw std::runtime_error("prepare the display vehicle with tools/prepare_display_vehicle.py");
        auto vehicle=gui::ViewportKit::loadModelNode(path,context->options());
        if(!vehicle)throw std::runtime_error("display vehicle model unavailable");
        root->addChild(vehicle);
        const D origin(0.,2.6,-1.25);
        auto prop=joint("voxel_prop",*root,origin,vsg::translate(origin));
        auto cells=vsg::Group::create();
        constexpr double voxel=.10;
        for(int y=0;y<10;++y)for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x) {
            if(y>=8&&(x||z))continue;
            cells->addChild(shapes.box({x*voxel,(y+.5)*voxel,z*voxel},{voxel,voxel,voxel},
                y>=8?C(.72,.72,.66,1):C(.46,.50,.51,1)));
        }
        auto visibility=vsg::Switch::create();
        visibility->addChild(false,cells);
        prop->addChild(visibility);
        joints.at("voxel_prop").visibility=visibility;
        attachments["stage"]={{},origin};
    }
    void attachProp(const Shapes& shapes, D origin) {
        auto prop=joint("prop",*root,origin,vsg::translate(origin));
        auto visibility=vsg::Switch::create();
        visibility->addChild(false,displayProp(shapes));
        prop->addChild(visibility);
        joints.at("prop").visibility=visibility;
        attachments["prop"]={{prop},{}};
        attachments["prop_origin"]={{},origin};
    }
};

PlatformMotionFeature::PlatformMotionFeature():rig_(std::make_unique<Rig>()){}
PlatformMotionFeature::~PlatformMotionFeature()=default;
void PlatformMotionFeature::configure(const nlohmann::json& config) {
    if(rig_->context)return;
    rig_->subject=config.value("subject",rig_->subject);
    rig_->shipDirectory=config.value("ship_assets",rig_->shipDirectory);
    rig_->tankDirectory=config.value("tank_assets",rig_->tankDirectory);
    rig_->modelId=config.value("model_id",rig_->modelId);
    rig_->partManifest=config.value("parts_manifest",rig_->partManifest);
    rig_->previewCue=config.value("preview_cue_seconds",rig_->previewCue);
}
void PlatformMotionFeature::onAttach(ViewportContext& ctx) {
    rig_->context=&ctx;
    rig_->content=ctx.contentRoot();
    rig_->root=vsg::MatrixTransform::create();
    const Shapes shapes;
    if(!rig_->partManifest.empty())rig_->loadParts();
    else if(rig_->subject=="tracked") {
        if(rig_->tankDirectory.empty())
            throw std::invalid_argument("tank_assets must point to an M1A2 rig prepared by tools/prepare_m1a2_motion.py");
        rig_->buildM1a2(shapes);
    }
    else if(rig_->subject=="ship")rig_->buildShip(shapes);
    else if(rig_->subject=="aircraft")rig_->buildAircraft(shapes);
    else if(rig_->subject=="display_vehicle")rig_->buildDisplayVehicle(shapes);
    else throw std::invalid_argument("unknown platform animation preview subject");
    const auto& subject=rig_->subject;
    if(rig_->modelId.empty())rig_->modelId=subject=="tracked"?"m1a2":subject=="ship"?"cg57":subject=="aircraft"?"f16c":subject;
    rig_->animation=platform_animation::makePlatformAnimation(rig_->modelId);
    if(!rig_->animation)throw std::invalid_argument("unknown platform animation model: "+rig_->modelId);
    rig_->visibility=vsg::Switch::create();
    rig_->visibility->addChild(enabled_,rig_->root);
    rig_->content->addChild(rig_->visibility);
    ctx.requestCompile();
}
void PlatformMotionFeature::onDetach(ViewportContext& ctx) {
    if(auto* fx=ctx.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId))
        fx->setPlatformPlumes(rig_->effectOwner,{});
    auto& nodes=rig_->content->children;
    nodes.erase(std::remove(nodes.begin(),nodes.end(),rig_->visibility),nodes.end());
    rig_=std::make_unique<Rig>();
}
void PlatformMotionFeature::setEnabled(bool on) {
    if(on==enabled_)return;
    enabled_=on;
    if(!rig_->context)return;
    rig_->visibility->setAllChildren(on);
    if(!on)if(auto* fx=rig_->context->featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId))
        fx->setPlatformPlumes(rig_->effectOwner,{});
}
void PlatformMotionFeature::setMotion(const vsg::dmat4& pose,double travelledMetres,double throttle) {
    if(!std::isfinite(travelledMetres))return;
    rig_->pose=pose;rig_->travel=travelledMetres;
    rig_->throttle=std::isfinite(throttle)?std::clamp(throttle,0.,1.):0.;
}
void PlatformMotionFeature::setDisplayAngles(double yawRadians,double pitchRadians) {
    if(std::isfinite(yawRadians)&&std::isfinite(pitchRadians))
        rig_->displayAngles=std::array<double,2>{yawRadians,pitchRadians};
}
const PlatformMotionFeature::PoseState& PlatformMotionFeature::state() const {return rig_->published;}
vsg::dvec3 PlatformMotionFeature::antennaCentre() const {return rig_->pose*rig_->antennaPivot;}

void PlatformMotionFeature::onFrame(ViewportContext& ctx,const FrameTick& tick) {
    auto& r=*rig_;
    const double t=tick.elapsed_sec;
    auto& state=r.published;
    state={};
    r.root->matrix=r.pose;
    r.animation->sample({t,r.lastTime,r.previewCue,r.throttle,r.displayAngles},r.frame);
    r.lastTime=t;
    for(const auto& joint:r.frame.joints)r.apply(joint);
    state.detached=state.propVisible && t>r.previewCue;
    if(!r.wheels.empty()) {
        state.trackDistance=r.travel;state.wheelAngle=r.travel/r.treadPhaseRadius;
        state.animatedWheels=unsigned(r.wheels.size());
        for(auto& wheel:r.wheels)
            wheel.node->matrix=vsg::translate(wheel.position)*vsg::rotate(r.travel/wheel.radius,axisX);
        for(size_t side=0;side<r.pads.size();++side)for(size_t i=0;i<r.pads[side].size();++i) {
            const auto& outline=r.outlines[side];
            const auto [p,angle]=outline.sample(r.travel+outline.distances.back()*i/r.pads[side].size());
            r.pads[side][i]->matrix=vsg::translate(p)*vsg::rotate(angle,axisX);
        }
    }
    if(auto* fx=ctx.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId))
        publishAnimationEffects(*fx,r.effectOwner,r.frame,[&r](std::string_view name){return r.attachment(name);});
}

struct ScenePlatformMotion::Instances {
    struct Instance {
        std::string rig;
        std::unique_ptr<PlatformMotionFeature> actor;
        double lastSceneSeconds = -1.;
    };
    std::map<std::string,Instance> actors;
};

ScenePlatformMotion::ScenePlatformMotion():instances_(std::make_unique<Instances>()){}
ScenePlatformMotion::~ScenePlatformMotion()=default;

const PlatformMotionFeature::PoseState* ScenePlatformMotion::state(const std::string& object) const {
    const auto entry=instances_->actors.find(object);
    if(entry==instances_->actors.end()||!entry->second.actor||!entry->second.actor->enabled())return nullptr;
    return &entry->second.actor->state();
}

void ScenePlatformMotion::onFrame(ViewportContext& context,const FrameTick& time) {
    auto* voxel=context.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    const auto snapshot=voxel&&voxel->enabled()?voxel->snapshot():nullptr;
    std::set<std::string> visible;
    if(snapshot&&!voxel->display().clusters&&voxel->presentation().is_array()) {
        const auto fromScene=vsg::inverse(context.contentModel())*simcore::geo::makeEnuToWorldMatrix(snapshot->anchor);
        for(const auto& sample:voxel->presentation())try {
            const auto id=sample.at("scene_object").get<std::string>();
            const auto& data=sample.at("presentation");
            if(data.value("model",std::string())!="m1a2"||data.at("tick")!=snapshot->tick||
               data.at("content_revision")!=snapshot->contentRevision||visible.count(id))continue;
            const auto body=snapshot->objects.find(id);
            if(body==snapshot->objects.end()||body->second.role!="target")continue;
            const auto rig=data.at("rig").get<std::string>();
            const double yaw=data.at("yaw_deg").get<double>(),pitch=data.at("pitch_deg").get<double>(),
                travel=data.at("travel_m").get<double>();
            if(!std::isfinite(yaw)||!std::isfinite(pitch)||!std::isfinite(travel)||
               yaw<-90.||yaw>90.||pitch<-12.||pitch>18.)continue;
            auto& instance=instances_->actors[id];
            if(instance.rig!=rig) {
                if(instance.actor)instance.actor->onDetach(context);
                instance.actor.reset();
                instance.rig=rig;
                instance.lastSceneSeconds=-1.;
                auto actor=std::make_unique<PlatformMotionFeature>();
                actor->configure({{"subject","tracked"},{"tank_assets",rig}});
                actor->onAttach(context);
                instance.actor=std::move(actor);
            }
            if(!instance.actor)continue;
            const double scale=body->second.provenance.value("units_to_metres",1.);
            instance.actor->setMotion(fromScene*body->second.pose(snapshot->tick,snapshot->fixedStep)*vsg::scale(scale),travel/scale);
            instance.actor->setDisplayAngles(vsg::radians(yaw),vsg::radians(pitch));
            instance.actor->setEnabled(true);
            auto sceneFrame=time;
            sceneFrame.elapsed_sec=snapshot->timeSeconds();
            sceneFrame.simulation_clock=true;
            sceneFrame.dt_sec=instance.lastSceneSeconds<0.?0.:
                std::max(0.,sceneFrame.elapsed_sec-instance.lastSceneSeconds);
            instance.lastSceneSeconds=sceneFrame.elapsed_sec;
            instance.actor->onFrame(context,sceneFrame);
            visible.insert(id);
        }catch(const std::exception& error) {
            spdlog::warn("Scene platform presentation: {}",error.what());
        }catch(const vsg::Exception& error) {
            spdlog::warn("Scene platform presentation: {}",error.message);
        }
    }
    for(auto& [id,instance]:instances_->actors)
        if(instance.actor&&!visible.count(id))instance.actor->setEnabled(false);
    if(voxel)voxel->setPresentationHiddenObjects(std::move(visible));
}
void ScenePlatformMotion::onDetach(ViewportContext& context) {
    if(context.viewer())context.viewer()->deviceWaitIdle();
    for(auto& [id,instance]:instances_->actors)if(instance.actor)instance.actor->onDetach(context);
    instances_->actors.clear();
    if(auto* voxel=context.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId))voxel->setPresentationHiddenObjects({});
}

VSGVP_REGISTER_FEATURE(PlatformMotionFeature);
VSGVP_REGISTER_FEATURE(ScenePlatformMotion);
} // namespace vsgraysim::viewport3d
