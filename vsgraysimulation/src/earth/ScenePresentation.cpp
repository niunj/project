#include <vsgraysim/earth/ScenePresentation.h>
#include <vsgraysim/earth/VoxelOcean.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include <vsgraysim/model_packages/SceneModelRegistry.h>
#include <vsgraysim/model_packages/SceneSoundProfiles.h>
#include <vsgraysim/model_packages/platform/fixed_wing/Acoustics.h>
#include <vsgraysim/model_packages/platform/rotorcraft/Acoustics.h>
#include <vsgraysim/model_packages/equipment/Acoustics.h>
#include "VoxelTerrain.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace vsgraysim::earth {
namespace {
using Json=nlohmann::json;
using model_packages::SoundProfile;

auto position(const VoxelSceneSnapshot& scene,const VoxelSceneObject& object) {
    return object.pose(scene.tick,scene.fixedStep)*vsg::dvec3{};
}

bool isGround(const VoxelSceneSnapshot& scene,const std::string& id) {
    if(id=="__earth_surface")return true;
    const auto it=scene.objects.find(id);
    return it!=scene.objects.end()&&(it->second.role=="terrain"||it->second.role=="structure");
}

const VoxelContact* groundContact(const VoxelSceneSnapshot& scene,const std::string& id) {
    const VoxelContact* strongest=nullptr;
    for(const auto& contact:scene.contacts){
        if(contact.a!=id&&contact.b!=id)continue;
        if(std::abs(contact.normalOnB.y)<.45)continue;
        const auto& peer=contact.a==id?contact.b:contact.a;
        if(isGround(scene,peer)&&(!strongest||contact.impulseNs>strongest->impulseNs))strongest=&contact;
    }
    return strongest;
}

using ContactPair=std::pair<std::string_view,std::string_view>;
ContactPair contactPair(const VoxelContact& contact) {return std::minmax(std::string_view(contact.a),std::string_view(contact.b));}

vsg::dvec3 offset(const model_sdk::Vec3& local) {return {local[0],local[1],local[2]};}

void attach(Json& emitter,std::string_view name,const std::string& id,const vsg::dvec3& velocity) {
    emitter["mount_name"]=name;emitter["mount_object"]=id;
    emitter["velocity_mps"]={velocity.x,velocity.y,velocity.z};
}

const VoxelSceneObject* rotorAt(const VoxelSceneSnapshot& scene,const VoxelSceneObject& carrier,model_sdk::SoundMountKind kind) {
    const bool main=kind==model_sdk::SoundMountKind::MainRotor;
    const VoxelSceneObject* fallback=nullptr;
    for(const auto& [id,part]:scene.objects){
        if(!part.rotor||part.rotor->carrier!=carrier.id)continue;
        if(id==(main?"main-rotor":"tail-rotor")||id==(main?"main_rotor":"tail_rotor"))return &part;
        if(!fallback&&(std::abs(part.rotor->axis.y)>.7)==main)fallback=&part;
    }
    return fallback;
}

struct AuthoredPhase {
    std::string status;
    double since=-1.;
};

AuthoredPhase authoredPhase(const VoxelSceneObject& object,double seconds) {
    AuthoredPhase phase;
    const auto it=object.provenance.find("presentation_phases");
    if(!object.poseTrack||it==object.provenance.end()||!it->is_array()||it->size()>32)return phase;
    for(const auto& entry:*it){
        if(!entry.is_object()||!entry.contains("seconds")||!entry["seconds"].is_number()||
            !entry.contains("state")||!entry["state"].is_string())continue;
        const auto since=entry["seconds"].get<double>();
        if(since<0.||since>seconds||since<phase.since)continue;
        const auto value=entry["state"].get<std::string>();
        if(value.size()<=48)phase={value,since};
    }
    return phase;
}

Json sound(std::string key,SoundProfile profile,const vsg::dvec3& point) {
    Json row={{"key",std::move(key)},{"cue",profile.cue},{"position_m",{point.x,point.y,point.z}},
        {"reference_m",profile.referenceDistance},{"gain",profile.gain},{"pitch",profile.pitch}};
    // Illustrative reference spectra. Model packages can override them with a
    // documented free-field band spectrum through a named acoustic mount.
    static const std::map<std::string_view,double> levels{{"jet",99.},{"propeller",79.},{"small_rotor",69.},
        {"helicopter",91.},{"helicopter_tail",91.},{"land_engine",78.},{"ship_engine",82.},{"equipment_fan",49.},{"optical",42.},
        {"sea_ship_water",79.},{"sea_propeller",82.},{"submersible",65.},
        {"rocket",106.},{"fracture",84.},{"impact",76.},{"touchdown",76.},{"separation",63.},{"mechanical",57.}};
    const auto level=levels.find(profile.cue);
    if(level!=levels.end()){
        constexpr double shape[]{-13.,-9.,-6.,-5.,-7.,-11.,-17.,-24.};
        row["spl_bands_db"]=Json::array();
        const double stateLevel=20.*std::log10(std::max(.01,profile.gain)/.5);
        for(const double band:shape)row["spl_bands_db"].push_back(level->second+stateLevel+band);
        row["acoustic_reference_m"]=10.;
        row["estimated_source"]=true;
    }
    if(profile.cue=="submersible"||profile.cue=="sea_ship_water"||profile.cue=="sea_propeller")
        row["medium"]="water";
    return row;
}

void platformEmitters(Json& loops,const VoxelSceneSnapshot& scene,const VoxelEquipmentPlatform& platform,
    const model_sdk::PlatformDescriptor& model,const VoxelSceneObject& body,SoundProfile profile,const std::string& state) {
    using F=model_sdk::PlatformFamily;
    using K=model_sdk::SoundMountKind;
    const auto key="platform:"+std::to_string(platform.id);
    const auto pose=body.pose(scene.tick,scene.fixedStep);
    const auto root=pose*vsg::dvec3{};
    if(scene.ocean&&scene.ocean->contains(root.x,root.z)&&
        (model.family==F::Surface||model.family==F::Submersible)){
        const auto& water=*scene.ocean;
        const double time=scene.timeSeconds();
        const auto relative=body.velocity-water.current;
        const double speed=std::hypot(relative.x,relative.z);
        const double seaLoad=std::clamp(.10*water.waveHeight+.05*water.swellHeight,0.,.28);
        const double load=std::clamp(speed/6.,0.,1.);
        const double level=water.surface(root.x,root.z,time).height;
        const auto emit=[&](const std::string& name,SoundProfile cue,const vsg::dvec3& point,
                            const vsg::dvec3& velocity,const std::string& object){
            auto row=sound(key+"/"+name,cue,point);
            row["velocity_mps"]={velocity.x,velocity.y,velocity.z};
            row["mount_object"]=object;row["water_speed_mps"]=speed;
            loops.push_back(std::move(row));
        };
        if(model.family==F::Surface){
            const double throttle=.24+.76*load;
            profile.gain*=throttle*(1.+seaLoad);
            profile.pitch*=.68+.61*load+.14*seaLoad;
            const auto bounds=body.localBounds();
            auto upper=pose*vsg::dvec3{0.,std::max(.9,(bounds.max.y-bounds.min.y)*.14),0.};
            upper.y=std::max(upper.y,water.surface(upper.x,upper.z,time).height+.35);
            emit("engine",profile,upper,body.velocity,body.id);
            const vsg::dvec3 immersed{root.x,level-.85,root.z};
            emit("hull",{"sea_ship_water",30.,profile.gain*.63,profile.pitch},immersed,body.velocity,body.id);
            return;
        }
        bool fittedShaft=false;
        for(const auto& [id,part]:scene.objects){
            if(!part.rotor||!part.rotor->waterJet||part.rotor->carrier!=body.id)continue;
            fittedShaft=true;
            const double rpm=part.rotor->rpmAt(time);
            if(std::abs(rpm)<1.||!part.volume->occupiedCount())continue;
            const auto hub=position(scene,part);
            if(water.densityAt(hub,time)<=0.)continue;
            const double shaft=std::abs(rpm)/90.;
            const double wash=std::min(1.8,shaft)*(.78+.22*load);
            emit("propeller/"+id,{"sea_propeller",28.,.49*wash,std::min(3.,shaft)},hub,part.velocity,id);
            loops.back()["rpm"]=rpm;
        }
        if(fittedShaft)return;
        profile.gain*=.50+.50*load;
        profile.pitch*=.72+.50*load;
    }
    const bool helicopter=model.family==F::Rotorcraft&&profile.cue=="helicopter";
    if(model.family!=F::FixedWing&&!helicopter){
        auto emitter=sound(key,profile,root);
        emitter["velocity_mps"]={body.velocity.x,body.velocity.y,body.velocity.z};
        if(state=="recorded_flight")emitter.erase("spl_bands_db");
        loops.push_back(std::move(emitter));
        return;
    }
    const auto mounts=helicopter?model_packages::helicopterSoundMounts():model_packages::fixedWingSoundMounts(model.id);
    for(const auto& mount:mounts){
        if(mount.name.empty())continue;
        const auto named=platform.mounts.find(std::string(mount.name));
        auto local=offset(mount.position);
        if(mount.referenceLength>0.&&named==platform.mounts.end()){
            const auto bounds=body.localBounds();
            local*=std::max(.01,bounds.max.z-bounds.min.z)/mount.referenceLength;
        }
        if(named!=platform.mounts.end())local=offset(named->second.position);
        auto partProfile=profile;
        partProfile.gain*=mount.gain;partProfile.pitch*=mount.pitch;
        if(mount.kind==K::TailRotor)partProfile.cue="helicopter_tail";
        const auto* rotor=helicopter?rotorAt(scene,body,mount.kind):nullptr;
        const double rpm=rotor?rotor->rotor->rpmAt(scene.timeSeconds()):0.;
        if(rotor&&(!rotor->volume||!rotor->volume->occupiedCount()||std::abs(rpm)<.01))continue;
        if(rotor&&named==platform.mounts.end())local=rotor->rotor->pivot;
        if(rotor){
            const auto nominal=mount.kind==K::MainRotor?300.:1200.;
            const auto speed=std::clamp(std::abs(rpm)/nominal,.15,2.);
            partProfile.gain*=std::min(1.,speed);
            partProfile.pitch=std::max(.25,partProfile.pitch*speed);
        }
        const bool followRotor=rotor&&named==platform.mounts.end();
        const auto point=followRotor?position(scene,*rotor):pose*local;
        auto emitter=sound(key+"/"+std::string(mount.name),partProfile,point);
        attach(emitter,mount.name,followRotor?rotor->id:body.id,
            body.velocity+vsg::cross(body.angularVelocity,point-root));
        if(rotor)emitter["rpm"]=rpm;
        loops.push_back(std::move(emitter));
    }
}

double closingSpeed(const VoxelSceneSnapshot& before,const VoxelContact& contact) {
    const auto velocityAt=[&](const std::string& id,const vsg::dvec3& p){
        const auto it=before.objects.find(id);
        if(it==before.objects.end())return vsg::dvec3{};
        const auto& body=it->second;
        return body.velocity+vsg::cross(body.angularVelocity,p-position(before,body));
    };
    const auto relative=velocityAt(contact.a,contact.pointA)-velocityAt(contact.b,contact.pointB);
    return std::max(0.,-vsg::dot(relative,contact.normalOnB));
}

Json contactSound(const VoxelSceneSnapshot& scene,const VoxelContact& contact,bool touchdown,
    const vsg::dvec3& point,double approachMps=0.) {
    const auto [a,b]=contactPair(contact);
    const auto kind=touchdown?"touchdown":"impact";
    const auto soundStrength=std::max(contact.impulseNs,1.5*approachMps);
    const double gain=touchdown?.54:std::clamp(.18+.12*std::log1p(soundStrength),.18,.8);
    auto event=sound(std::string(kind)+":"+std::to_string(scene.tick)+":"+std::to_string(a.size())+":"+
        std::string(a)+std::string(b),{kind,35.,gain,1.},point);
    event["duration_s"]=touchdown?.40:.55;
    event["contact"]={{"a",contact.a},{"b",contact.b},{"impulse_ns",contact.impulseNs},{"approach_mps",approachMps}};
    return event;
}
}

nlohmann::json scenePresentation(const VoxelEquipmentConfig& config,const VoxelSceneSnapshot& scene,
    const nlohmann::json& equipmentStates,const VoxelSceneSnapshot* previous) {
    using F=model_sdk::PlatformFamily;
    Json out={{"version",1},{"tick",scene.tick},{"revision",scene.revision}};
    auto& platforms=out["platforms"]=Json::array();
    auto& devices=out["equipment"]=Json::array();
    auto& loops=out["loops"]=Json::array();
    auto& events=out["events"]=Json::array();
    const double time=scene.timeSeconds();
    const bool consecutive=previous&&previous->tick<scene.tick&&scene.tick-previous->tick==1;
    std::set<ContactPair> touchdowns;
    std::map<std::string,bool> recentDamage;
    for(auto i=scene.journal.rbegin();i!=scene.journal.rend();++i){
        const auto tick=i->at("tick").get<uint64_t>();
        const double age=time-double(tick)*scene.fixedStep;
        if(age>1.5)break;
        const auto& edit=i->at("event");
        const auto kind=edit.value("kind",std::string());
        if(kind!="damage"&&kind!="fracture")continue;
        const bool fragmented=i->contains("fragments")&&!i->at("fragments").empty();
        if(!fragmented&&!i->value("removed_voxels",0)&&!i->value("broken_bonds",0))continue;
        const auto id=edit.at("object").get<std::string>();
        const auto body=scene.objects.find(id);
        if(body==scene.objects.end())continue;
        auto& fragments=recentDamage[id];
        fragments=fragments||fragmented;
        if(events.size()<16){
            auto cue=sound("damage:"+std::to_string(i->at("sequence").get<uint64_t>()),
                {fragmented?"fracture":"impact",48.,.67,fragmented?.93:1.},position(scene,body->second));
            cue["tick"]=tick;
            events.push_back(std::move(cue));
        }
    }
    for(const auto& item:config.platforms){
        const auto* model=model_packages::platformModel(item.modelId.empty()?
            model_packages::platformModelForKind(item.kind):std::string_view(item.modelId));
        const auto found=scene.objects.find(item.object);
        if(!model||found==scene.objects.end())continue;
        const auto& body=found->second;
        const auto point=position(scene,body);
        const auto speed=vsg::length(body.velocity);
        const auto phase=authoredPhase(body,time);
        std::string state="idle";
        auto profile=model_packages::platformSound(*model);
        const bool aircraft=model->family==F::FixedWing||model->family==F::Rotorcraft;
        const bool occupied=body.volume&&body.volume->occupiedCount()>0;
        const bool damaged=recentDamage.count(item.object)>0;
        if(!occupied||damaged){
            state=!occupied?"destroyed":recentDamage.at(item.object)?"breaking_up":"damaged";
            profile={};
        }else if(aircraft){
            const auto* ground=groundContact(scene,item.object);
            if(model->family==F::Rotorcraft&&!phase.status.empty()){
                state=phase.status;
                if(state=="landed"||state=="spooling_up")profile.gain*=.65;
            }else if(ground){
                state=speed<.8?"landed":"taxiing";
                profile.gain*=.32;
                if(consecutive&&!groundContact(*previous,item.object)&&touchdowns.insert(contactPair(*ground)).second)
                    events.push_back(contactSound(scene,*ground,true,ground->a==item.object?ground->pointB:ground->pointA));
            }else if(body.velocity.y>.35)state="climbing";
            else if(body.velocity.y<-.35)state="descending";
            else if(model->family==F::Rotorcraft&&speed<.35)state="hovering";
            else if(speed>.35)state="flying";
            else{state="ready";profile={};}
        }else if(model->family==F::Playback){
            state=phase.status.empty()?(item.kind=="missile"?"recorded_flight":"replay"):phase.status;
            if(state=="recorded_flight"&&speed>.35)profile={"jet",36.,.28,1.23};
            const auto fire=body.provenance.find("visual_exhaust_window_s");
            if(body.poseTrack&&fire!=body.provenance.end()&&fire->is_array()&&fire->size()==2&&
               (*fire)[0].is_number()&&(*fire)[1].is_number()&&time>=(*fire)[0].get<double>()&&time<(*fire)[1].get<double>()){
                profile={"rocket",110.,.85,model->id=="falcon9-block5-stage2"?1.18:1.};
                if(phase.status.empty())state="launching";
            }
            if(phase.since>=0.&&time-phase.since<.65&&
               (state=="stage_separated"||state=="fairing_separated"||state=="payload_released"||state=="deploying"))
                events.push_back(sound("phase:"+item.object+":"+state,
                    {state=="deploying"?"mechanical":"separation",28.,.33,1.},point));
        }else if(model->family==F::Submersible){
            if(scene.ocean&&scene.ocean->contains(point.x,point.z)){
                const auto depth=std::max(0.,scene.ocean->surface(point.x,point.z,time).height-point.y);
                state=model_packages::Alvin::state(depth,body.velocity.y,std::hypot(body.velocity.x,body.velocity.z));
                if(state=="hovering"||state=="surfaced")profile.gain*=.45;
            }else{
                state="unavailable";
                profile={};
            }
        }else if(model->family==F::Surface){
            state=speed>.35?"underway":"moored";
            if(speed<=.35&&!scene.ocean)profile.gain*=.22;
        }else if(model->family==F::Ground){
            state=speed>.35?"driving":"idle";
            if(speed<=.35)profile.gain*=.34;
        }
        platforms.push_back({{"id",item.id},{"model_id",model->id},{"state",state}});
        if(!profile.cue.empty())platformEmitters(loops,scene,item,*model,body,profile,state);
    }
    if(consecutive){
        std::set<ContactPair> sustained;
        for(const auto& contact:previous->contacts)sustained.insert(contactPair(contact));
        const auto dynamicBody=[&](const std::string& id){
            const auto body=previous->objects.find(id);
            return body!=previous->objects.end()&&body->second.body.motion=="dynamic";
        };
        struct Impact {const VoxelContact* contact=nullptr;double approach=0.;};
        std::map<ContactPair,Impact> fresh;
        for(const auto& contact:scene.contacts){
            const auto pair=contactPair(contact);
            if(sustained.count(pair)||touchdowns.count(pair))continue;
            const auto approach=closingSpeed(*previous,contact);
            const double minimumApproach=dynamicBody(contact.a)||dynamicBody(contact.b)?.02:.3;
            if(contact.impulseNs<.25&&approach<minimumApproach)continue;
            auto& strongest=fresh[pair];
            if(!strongest.contact||std::tie(contact.impulseNs,approach)>
                std::tie(strongest.contact->impulseNs,strongest.approach))strongest={&contact,approach};
        }
        for(const auto& [pair,impact]:fresh){
            if(events.size()>=24)break;
            const auto* contact=impact.contact;
            const auto p=isGround(scene,contact->b)?contact->pointB:isGround(scene,contact->a)?
                contact->pointA:(contact->pointA+contact->pointB)*.5;
            events.push_back(contactSound(scene,*contact,false,p,impact.approach));
        }
    }
    for(const auto& item:equipmentStates.at("items")){
        const auto id=item.at("id").get<uint32_t>();
        const auto name=item.at("model_id").get<std::string>();
        const auto state=item.at("state").get<std::string>();
        devices.push_back({{"id",id},{"platform_id",item.at("platform_id")},{"model_id",name},{"state",state}});
        const auto* model=model_packages::equipmentModel(name);
        if(state!="working"||!model||!item.contains("aperture"))continue;
        const auto mount=model_packages::equipmentFanMount(*model);
        if(mount.name.empty())continue;
        const auto& aperture=item.at("aperture");
        const auto forward=vsg::normalize(offset(aperture.at("forward").get<model_sdk::Vec3>()));
        const auto right=vsg::normalize(vsg::cross(offset(aperture.at("up").get<model_sdk::Vec3>()),forward));
        const auto up=vsg::cross(forward,right);
        const auto point=offset(aperture.at("origin").get<model_sdk::Vec3>())+
            right*mount.position[0]+up*mount.position[1]+forward*mount.position[2];
        vsg::dvec3 velocity{};std::string owner;
        const auto spec=std::find_if(config.equipment.begin(),config.equipment.end(),[&](const auto& row){return row.sensor.id==id;});
        if(spec!=config.equipment.end())if(const auto body=scene.objects.find(spec->mountObject);body!=scene.objects.end()){
            owner=body->first;
            velocity=body->second.velocity+vsg::cross(body->second.angularVelocity,point-position(scene,body->second));
        }
        auto emitter=sound("equipment:"+std::to_string(id)+"/"+std::string(mount.name),model_packages::equipmentSound(*model),point);
        attach(emitter,mount.name,owner,velocity);
        loops.push_back(std::move(emitter));
    }
    for(const auto& item:config.navigation){
        const auto platform=std::find_if(config.platforms.begin(),config.platforms.end(),
            [&](const auto& row){return row.id==item.platformId;});
        if(platform==config.platforms.end())continue;
        const auto body=scene.objects.find(platform->object);
        const bool working=body!=scene.objects.end()&&body->second.volume&&body->second.volume->occupiedCount()>0;
        devices.push_back({{"id",item.id},{"platform_id",item.platformId},{"model_id","navigation-truth"},
            {"state",working?"working":"unavailable"}});
        if(working)loops.push_back(sound("equipment:"+std::to_string(item.id),
            model_packages::equipmentSound(*model_packages::equipmentModel("navigation-truth")),position(scene,body->second)));
    }
    for(const auto& sounder:config.sounders){
        auto& broker=SimDataBroker::instance();
        const auto id=sounder.equipment.equipment_id;
        const auto info=EquipmentRegistry::instance().findById(id);
        const auto p=sounder.origin(scene);
        const auto frame=broker.sonarSnapshot(id);
        const bool current=frame&&frame->tick<=scene.tick&&frame->surface_revision==scene.contentRevision;
        const char* status=!p?"unavailable":config.visualOnly?"preview":!info||!info->powered?"off":
            !broker.isSubscribed(id)?"standby":!info->active||!current?"waiting":
            frame->bottom==BottomReturn::DryTransducer?"dry_transducer":
            frame->bottom==BottomReturn::NoBottom&&frame->echo_range_m==0.?"no_bottom":"working";
        devices.push_back({{"id",id},{"platform_id",sounder.equipment.platform_id},
            {"model_id",sounder.equipment.model_id},{"state",status}});
        if(std::string_view(status)=="working"&&time-frame->sim_time<.18){
            auto cue=sound("depth:"+std::to_string(id)+":"+std::to_string(frame->tick),
                model_packages::equipmentSound(*model_packages::equipmentModel(sounder.equipment.model_id)),*p);
            cue["tick"]=frame->tick;
            events.push_back(std::move(cue));
        }
    }
    for(auto& cue:events)
        if(!cue.contains("tick"))cue["tick"]=scene.tick;
    return out;
}

void retainSceneSoundEvents(Json& presentation,const Json& previous,const VoxelSceneSnapshot& scene) {
    if(!previous.is_object()||previous.value("tick",uint64_t(0))>scene.tick)return;
    auto& cues=presentation.at("events");
    if(cues.size()>=32)return;
    const auto& older=previous.at("events");
    if(older.empty())return;
    std::set<std::string> keys;
    for(const auto& cue:cues)keys.insert(cue.at("key").get<std::string>());
    for(const auto& cue:older){
        if(cues.size()>=32)break;
        const auto tick=cue.at("tick").get<uint64_t>();
        if(tick<=scene.tick&&double(scene.tick-tick)*scene.fixedStep<=1.5&&
            keys.insert(cue.at("key").get<std::string>()).second)cues.push_back(cue);
    }
}

}
