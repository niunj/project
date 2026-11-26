#include "SceneEffectsAdapter.h"
#include "ViewportContext.h"
#include "VoxelSceneFeature.h"
#include "../../src/earth/VoxelTerrain.h"
#include <simcore/GeoFrame.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <vsg/maths/transform.h>

namespace vsgraysim::viewport3d {

void SceneEffectsAdapter::reset() {
    lastTick_=lastEvent_=0;
    readEvents_=0;
    seenScene_=false;
    burnStart_.clear();
    debrisSockets_.clear();
}

SceneEffectFrame SceneEffectsAdapter::collect(ViewportContext& ctx,std::vector<AviationParticles::Source>& sources,double time) {
    constexpr double burnLifeSeconds = 18.;
    SceneEffectFrame frame{time};
    const auto* voxel=ctx.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    const auto state=voxel&&voxel->enabled()?voxel->snapshot():nullptr;
    if(!state) {
        frame.resetHistory=seenScene_;
        reset();
        return frame;
    }
    const auto sceneTime=state->timeSeconds();
    frame.time=sceneTime;
    if(state->weather.enabled) {
        const auto weather=state->weather.sample(time);
        const double az=vsg::radians(weather.windAzimuth);
        frame.wind=vsg::vec3(float(std::sin(az)*weather.windSpeed),0,float(-std::cos(az)*weather.windSpeed));
    }
    const auto toContent=vsg::inverse(ctx.contentModel())*simcore::geo::makeEnuToWorldMatrix(state->anchor);
    for(const auto& [id,object]:state->objects) {
        if(!object.poseTrack)continue;
        const auto window=object.provenance.find("visual_exhaust_window_s");
        if(window==object.provenance.end()||!window->is_array()||window->size()!=2||
            !(*window)[0].is_number()||!(*window)[1].is_number())continue;
        const double start=(*window)[0].get<double>(),end=(*window)[1].get<double>();
        if(!(end>start&&sceneTime>=start&&sceneTime<end))continue;
        const auto root=toContent*object.pose(state->tick,state->fixedStep);
        const auto axis=root*vsg::dvec4(0.,1.,0.,0.);
        const auto velocity=toContent*vsg::dvec4(object.velocity,0.);
        const auto bounds=object.localBounds();
        const float radius=float(std::clamp((bounds.max.x-bounds.min.x)*.38,.12,2.8));
        const float strength=float(std::clamp(std::min(sceneTime-start,end-sceneTime)/.17,0.,1.));
        sources.push_back({"track:"+id,vsg::vec3(root*vsg::dvec3{}),
            {float(axis.x),float(axis.y),float(axis.z)},AviationParticles::Style::LaunchExhaust,
            strength,radius,{float(velocity.x),float(velocity.y),float(velocity.z)}});
    }
    const uint64_t latest=state->journal.empty()?0:state->journal.back().value("sequence",uint64_t(0));
    if(seenScene_&&(state->tick<lastTick_||readEvents_>state->journal.size()||
       (readEvents_&&state->journal.at(readEvents_-1).value("sequence",uint64_t(0))!=lastEvent_))) {
        frame.resetHistory=true;
        reset();
    }
    for(size_t i=readEvents_;i<state->journal.size();++i) {
        const auto& record=state->journal.at(i);
        const auto tick=record.value("tick",uint64_t(0));
        const double occurred=double(tick)*state->fixedStep;
        const auto& event=record.at("event");
        const auto id=event.value("object",std::string{});
        const auto kind=event.value("kind",std::string{});
        const auto fragments=record.find("fragments");
        if(sceneTime-occurred>burnLifeSeconds||(kind!="fracture"&&kind!="damage")||
           fragments==record.end()||!fragments->is_array()||fragments->empty())continue;
        auto obj=state->objects.find(id);
        if(obj==state->objects.end()||obj->second.role!="target")continue;
        std::string root=id;
        auto parent=obj;
        for(int i=0;i<32&&!parent->second.parentId.empty();++i) {
            auto next=state->objects.find(parent->second.parentId);
            if(next==state->objects.end())break;
            parent=next;root=parent->first;
        }
        const auto& provenance=parent->second.provenance;
        const auto sourcePath=provenance.find("path");
        std::string path=sourcePath!=provenance.end()&&sourcePath->is_string()?sourcePath->get<std::string>():std::string{};
        std::replace(path.begin(),path.end(),'\\','/');
        for(auto& c:path)c=char(std::tolower(static_cast<unsigned char>(c)));
        const auto visual=provenance.find("visual_burning_debris");
        const bool aircraft=visual!=provenance.end()&&visual->is_boolean()?visual->get<bool>():
            path.find("/f-16/")!=std::string::npos||path.find("/mq-9/")!=std::string::npos||path.find("/ah-64/")!=std::string::npos;
        if(!aircraft)continue;
        // One initial flash even if multiple cuts were published in one tick.
        const auto [_,fresh]=burnStart_.emplace(root,occurred);
        if(fresh&&seenScene_&&sceneTime-occurred<=.20) {
            const auto coordinates=event.find("point_m");
            if(coordinates==event.end()||!coordinates->is_array()||coordinates->size()!=3||
               !std::all_of(coordinates->begin(),coordinates->end(),[](const auto& value){return value.is_number();}))continue;
            const auto& p=*coordinates;
            vsg::dvec3 point{p.at(0).get<double>(),p.at(1).get<double>(),p.at(2).get<double>()};
            if(event.value("space",kind=="fracture"?std::string("object"):std::string("scene"))=="object")
                point=obj->second.pose(state->tick,state->fixedStep)*point;
            frame.fractureFlashes.emplace_back(toContent*point);
        }
    }
    seenScene_=true;lastTick_=state->tick;lastEvent_=latest;readEvents_=state->journal.size();
    for(auto it=burnStart_.begin();it!=burnStart_.end();) {
        if(sceneTime-it->second>burnLifeSeconds)it=burnStart_.erase(it);else ++it;
    }
    if(burnStart_.empty()) {debrisSockets_.clear();return frame;}
    struct Piece {const earth::VoxelSceneObject* body;double age;size_t cells;};
    std::vector<Piece> pieces;
    for(const auto& [id,o]:state->objects) {
        if(o.role!="target"||o.body.motion!="dynamic"||!o.volume)continue;
        auto root=id;
        auto next=&o;
        for(int i=0;i<32&&!next->parentId.empty();++i) {
            auto parent=state->objects.find(next->parentId);
            if(parent==state->objects.end())break;
            root=parent->first;next=&parent->second;
        }
        auto fire=burnStart_.find(root);
        if(fire==burnStart_.end())continue;
        const auto cells=o.volume->occupiedCount();
        if(cells>=12)pieces.push_back({&o,std::max(0.,sceneTime-fire->second),cells});
    }
    std::stable_sort(pieces.begin(),pieces.end(),[](const Piece& a,const Piece& b){return a.cells>b.cells;});
    for(const auto& p:pieces) {
        if(frame.burningDebrisCount>=8)break;
        const auto& o=*p.body;
        auto& socket=debrisSockets_[o.id];
        if(socket.shape.lock().get()!=o.volume.get()) {
            const auto n=o.volume->dimensions();
            const auto origin=o.volume->gridOrigin();
            const double cell=o.volume->resolution_m;
            const auto com=o.localCentreOfMass();
            // Choose the occupied cell nearest the projected upper surface.
            // It remains the same local material point as the fragment rolls.
            const auto aim=com+vsg::dvec3(0,(o.localBounds().max.y-com.y)*.87,0);
            double best=std::numeric_limits<double>::infinity();
            socket.local=com;
            for(size_t k=0;k<o.volume->cellCount();++k)if(o.volume->occupancy()[k]) {
                const auto q=origin+(vsg::dvec3(k%n[0],k/n[0]%n[1],k/(size_t(n[0])*n[1]))+vsg::dvec3(.5,.5,.5))*cell;
                const auto d=q-aim;
                const double s=vsg::dot(d,d);
                if(s<best){best=s;socket.local=q+vsg::dvec3(0,cell*.54,0);}
            }
            socket.shape=o.volume;
        }
        const auto pose=o.pose(state->tick,state->fixedStep);
        const auto point=pose*socket.local, origin=pose*vsg::dvec3{};
        const auto velocity=o.velocity+vsg::cross(o.angularVelocity,point-origin);
        const auto vector=toContent*vsg::dvec4(velocity.x,velocity.y,velocity.z,0);
        const float radius=float(std::clamp(vsg::length(o.localBounds().max-o.localBounds().min)*.14,.26,1.3));
        const float strength=float(std::clamp((burnLifeSeconds-p.age)/10.,0.,1.))*(o.parentId.empty()?.91f:.69f);
        if(strength<=.01f)continue;
        sources.push_back({"fragment:"+o.id,vsg::vec3(toContent*point),{0,1,0},
            AviationParticles::Style::BurningDebris,strength,radius,{float(vector.x),float(vector.y),float(vector.z)}});
        ++frame.burningDebrisCount;
    }
    return frame;
}

} // namespace vsgraysim::viewport3d
