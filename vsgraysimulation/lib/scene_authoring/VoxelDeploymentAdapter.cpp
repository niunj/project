#include "VoxelDeploymentAdapter.h"
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsg/maths/transform.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace vsgraysim::earth::deployment {
namespace {
Json xyz(const vsg::dvec3& p) { return Json::array({p.x,p.y,p.z}); }

vsg::dvec3 vec(const Json& p) { return {p[0].get<double>(),p[1].get<double>(),p[2].get<double>()}; }

Json equipmentOf(const Json& topic) { return topic.value("equipment",Json()); }

void putEquipment(Json& topic,const Json& equipment) {
    if(equipment.is_null()) topic.erase("equipment");
    else topic["equipment"]=equipment;
}

Json lookup(const Json& group,const std::string& key,const char* kind) {
    if(!group.contains(key)) throw std::invalid_argument(std::string("unknown ")+kind+": "+key);
    return group.at(key);
}

void mergeMaterial(Json& base,const Json& patch) {
    for(const auto& [name,value]:patch.items()) {
        if(name=="radar"&&base.contains("radar")) base["radar"].update(value);
        else base[name]=value;
    }
}

vsg::dvec3 resolve(const Json& position,const VoxelSceneSnapshot& scene,std::optional<vsg::dvec3> mapPoint) {
    vsg::dvec3 result;
    if(position.contains("local_m")) result=vec(position.at("local_m"));
    else if(position.contains("relative_m")) {
        if(!mapPoint) throw std::invalid_argument("relative_m requires a selected map point");
        result=*mapPoint+vec(position.at("relative_m"));
    } else {
        const auto p=vec(position.at("wgs84"));
        result=simcore::geo::ecefToLocal(scene.anchor,simcore::geo::geodeticToECEF(p.x,p.y,p.z));
    }
    if(position.contains("agl_m")) {
        const auto hit=raycastVoxelScene(scene,{result.x,35000,result.z},{0,-1,0},70000,true);
        if(!hit) throw std::invalid_argument("no authored terrain at the requested AGL waypoint");
        result.y=hit->position.y+position.at("agl_m").get<double>();
    }
    if(std::max({std::abs(result.x),std::abs(result.y),std::abs(result.z)})>1e7)
        throw std::invalid_argument("location outside the scene coordinate range");
    return result;
}

Json routeKeys(const Json& action,const VoxelSceneSnapshot& scene,std::optional<vsg::dvec3> point) {
    std::vector<vsg::dvec3> points;
    for(const auto& input:action.at("points")) points.push_back(resolve(input,scene,point));
    double time=scene.timeSeconds(),speed=action.at("speed_mps").get<double>();
    auto keys=Json::array();
    for(size_t i=0;i<points.size();++i) {
        const auto delta=i+1<points.size()?points[i+1]-points[i]:points[i]-points[i-1];
        if(i) {
            const double interval=vsg::length(points[i]-points[i-1])/speed;
            if(interval<.0015) throw std::invalid_argument("route has coincident or too closely spaced points");
            time+=interval;
        }
        if(time>86400.) throw std::invalid_argument("route ends past the 24-hour pose track limit");
        // The catalog models use +Z forward. yaw followed by pitch aligns it to the route.
        const double yaw=std::atan2(delta.x,delta.z);
        const double pitch=-std::atan2(delta.y,std::hypot(delta.x,delta.z));
        const vsg::dquat rotation(std::sin(pitch*.5)*std::cos(yaw*.5),
            std::sin(yaw*.5)*std::cos(pitch*.5),-std::sin(yaw*.5)*std::sin(pitch*.5),
            std::cos(yaw*.5)*std::cos(pitch*.5));
        keys.push_back({{"seconds",time},{"position_m",xyz(points[i])},
            {"rotation_xyzw",{rotation.x,rotation.y,rotation.z,rotation.w}}});
    }
    return keys;
}

void checkSensorFootprint(const VoxelSceneSnapshot& scene,const VoxelEquipmentSpec& device,
                          const vsg::dmat4& pose) {
    const auto& aperture=device.aperture;
    const auto forward=pose*vsg::dvec4(aperture.forward,0.);
    const auto up=pose*vsg::dvec4(aperture.up,0.);
    const auto& profile=device.sensor.profile;
    try {
        validateVoxelEarthView(scene,pose*aperture.position,{forward.x,forward.y,forward.z},{up.x,up.y,up.z},
            profile.fov_deg,double(profile.width)/profile.height,
            device.sensor.radar?device.sensor.radar->range_max_m:1e5);
    } catch(const std::exception& e) {
        throw std::invalid_argument(device.equipment.name+": "+e.what());
    }
}

uint32_t newId(const Json& equipment,bool platform) {
    std::set<uint32_t> used;
    if(platform)
        for(const auto& item:equipment.at("platforms")) used.insert(item.at("id").get<uint32_t>());
    for(const auto* list:{"equipment","navigation","depth_sounders"})
        for(const auto& item:equipment.value(list,Json::array())) {
            if(!platform) used.insert(item.at("id").get<uint32_t>());
            else if(item.contains("platform_id")) used.insert(item.at("platform_id").get<uint32_t>());
        }
    uint32_t id=1;
    while(used.count(id)) ++id;
    return id;
}

Json concise(const Json& group) {
    auto out=Json::object();
    for(const auto& [id,entry]:group.items()) out[id]=entry.at("label");
    return out;
}
}

Adapter::Adapter(const std::filesystem::path& catalog) {
    std::ifstream input(catalog);
    if(!input) throw std::runtime_error("cannot open deployment catalog: "+catalog.string());
    input>>catalog_;
    if(catalog_.at("format")!="voxel-deployment-catalog-v1") throw std::invalid_argument("unsupported deployment catalog");
    root_=std::filesystem::absolute(catalog).parent_path().parent_path().parent_path();
}

Json Adapter::context(const VoxelSceneSnapshot& scene,const std::string& selected,
                      std::optional<vsg::dvec3> mapPoint) const {
    const auto& a=scene.anchor;
    Json result={{"anchor_wgs84",{a.lat_deg,a.lon_deg,a.alt_m}},{"heading_deg",a.heading_deg},
        {"models",concise(catalog_.at("models"))},{"sensor_profiles",concise(catalog_.at("sensors"))},
        {"material_presets",concise(catalog_.at("materials"))},{"weather_presets",catalog_.at("weather")},
        {"objects",Json::array()}};
    for(const auto& [id,object]:scene.objects) {
        if(!object.parentId.empty()&&id!=selected) continue;
        const auto p=object.pose(scene.tick,scene.fixedStep)*vsg::dvec3{};
        result["objects"].push_back({{"id",id},{"role",object.role},{"local_m",xyz(p)}});
    }
    if(scene.objects.count(selected)) result["selected_object"]=selected;
    if(mapPoint) result["selected_point_m"]=xyz(*mapPoint);
    return result;
}

Edit Adapter::stage(const Json& input,VoxelSceneSnapshotPtr scene,const Json& topic,
                    const std::string& topicPath,std::optional<vsg::dvec3> mapPoint) const {
    if(!scene||!topic.is_object()) throw std::invalid_argument("open a voxel topic before staging deployment");
    const auto plan=scene_deployment::validate(input);
    Edit edit;
    edit.before=scene;
    edit.equipmentBefore=equipmentOf(topic);
    edit.commands=Json::array();
    const auto& a=scene->anchor;
    edit.imports={{"format","voxel-scene-v1"},{"fixed_step_seconds",scene->fixedStep},
        {"geo_anchor",{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}}},
        {"objects",Json::array()}};
    auto equipment=edit.equipmentBefore.is_null()
        ?Json{{"format","voxel-equipment-v1"},{"visual_only",true},{"platforms",Json::array()},{"equipment",Json::array()}}
        :edit.equipmentBefore;
    if(!equipment.contains("platforms")) equipment["platforms"]=Json::array();
    std::set<std::string> planned,hasRoute;
    std::set<uint32_t> attached;
    std::map<std::string,std::vector<vsg::dmat4>> plannedPoses;
    bool equipmentChanged=false;
    const auto& actions=plan.at("actions");
    for(size_t index=0;index<actions.size();++index) {
        try {
            const auto& action=actions[index];
            const auto op=action.at("op").get<std::string>();
            if(op=="add_target") {
                const auto id=action.at("id").get<std::string>();
                if(scene->objects.count(id)||!planned.insert(id).second) throw std::invalid_argument("duplicate object: "+id);
                const auto modelId=action.at("model").get<std::string>();
                const auto model=lookup(catalog_.at("models"),modelId,"model");
                auto source=model.at("source");
                const auto path=root_/source.at("path").get<std::string>();
                if(!std::filesystem::is_regular_file(path)) throw std::invalid_argument("catalog model is unavailable: "+modelId);
                source["path"]=path.string();
                auto material=model.value("material",Json::object());
                const auto position=resolve(action.at("position"),*scene,mapPoint);
                plannedPoses[id]={vsg::translate(position)};
                edit.imports["objects"].push_back({{"id",id},{"role","target"},{"source",source},{"material",material},
                    {"position_m",xyz(position)},{"physics",{{"motion","prescribed"}}}});
                equipment["platforms"].push_back({{"id",newId(equipment,true)},{"object",id},
                    {"model_id",model.value("platform_model",modelId)},{"kind",model.at("kind")},
                    {"mounts",model.value("mounts",Json::object())},
                    {"name",action.value("name",model.at("label").get<std::string>())}});
                equipmentChanged=true;
                continue;
            }
            if(op=="weather") {
                const auto preset=action.at("preset").get<std::string>();
                SceneWeather::preset(preset);
                edit.commands.push_back({{"kind","environment"},{"weather",{{"initial",{{"preset",preset}}}}}});
                continue;
            }
            const auto id=action.at("object").get<std::string>();
            const auto object=scene->objects.find(id);
            if(object==scene->objects.end()&&!planned.count(id)) throw std::invalid_argument("unknown object: "+id);
            const auto* existing=object==scene->objects.end()?nullptr:&object->second;
            if(op=="place") {
                if(hasRoute.count(id)||(existing&&existing->poseTrack)) throw std::invalid_argument("use route to move an object with a trajectory");
                if(existing&&existing->earth) throw std::invalid_argument("cannot move a georeferenced terrain");
                auto pose=existing?existing->pose(scene->tick,scene->fixedStep):vsg::dmat4();
                const auto p=resolve(action.at("position"),*scene,mapPoint);
                pose[3]=vsg::dvec4(p,1.);
                plannedPoses[id]={pose};
                auto matrix=Json::array();
                for(size_t column=0;column<4;++column)
                    for(size_t row=0;row<4;++row) matrix.push_back(pose[column][row]);
                edit.commands.push_back({{"kind","place"},{"object",id},{"pose",matrix},
                    {"velocity_mps",existing?xyz(existing->velocity):xyz({})}});
            } else if(op=="route") {
                if(existing&&(existing->body.motion!="prescribed"||existing->earth||existing->rotor||existing->body.fracture))
                    throw std::invalid_argument("route requires a prescribed non-fracturing target: "+id);
                hasRoute.insert(id);
                auto keys=routeKeys(action,*scene,mapPoint);
                auto& poses=plannedPoses[id];
                poses.clear();
                for(const auto& key:keys) {
                    const auto& q=key.at("rotation_xyzw");
                    poses.push_back(vsg::translate(vec(key.at("position_m")))*vsg::rotate(vsg::dquat(
                        q[0].get<double>(),q[1].get<double>(),q[2].get<double>(),q[3].get<double>())));
                }
                edit.commands.push_back({{"kind","pose_track"},{"object",id},{"keys",std::move(keys)}});
            } else if(op=="attach_sensor") {
                auto& platforms=equipment.at("platforms");
                const auto platform=std::find_if(platforms.begin(),platforms.end(),[&id](const Json& entry){return entry.at("object")==id;});
                if(platform==platforms.end()) throw std::invalid_argument("object is not a registered platform: "+id);
                auto profile=lookup(catalog_.at("sensors"),action.at("profile").get<std::string>(),"sensor profile");
                auto sensor=profile.at("settings");
                const auto sensorId=newId(equipment,false);
                attached.insert(sensorId);
                sensor["id"]=sensorId;
                sensor["name"]=action.value("name",profile.at("label").get<std::string>());
                sensor["platform_id"]=platform->at("id");
                sensor["platform_name"]=platform->at("name");
                sensor["platform_kind"]=platform->at("kind");
                auto& aperture=sensor["aperture"];
                if(platform->value("mounts",Json::object()).contains("deployment-sensor")) {
                    aperture.erase("mount_object");
                    aperture["mount_id"]="deployment-sensor";
                } else if(!aperture.contains("mount_id")) {
                    aperture["mount_object"]=id;
                    if(!existing) throw std::invalid_argument("catalog platform has no sensor mount");
                    const auto bounds=existing->localBounds();
                    const vsg::dvec3 p{(bounds.min.x+bounds.max.x)*.5,bounds.min.y-.05,(bounds.min.z+bounds.max.z)*.5};
                    aperture["position_m"]=xyz(p);
                    aperture["look_at_m"]=xyz(p+vec(profile.at("settings").at("aperture").at("look_at_m"))-
                                               vec(profile.at("settings").at("aperture").at("position_m")));
                }
                equipment["equipment"].push_back(std::move(sensor));
                equipment["visual_only"]=false;
                equipmentChanged=true;
            } else if(op=="material") {
                auto patch=Json::object();
                if(action.contains("preset")) patch=lookup(catalog_.at("materials"),action.at("preset").get<std::string>(),"material preset").at("parameters");
                if(action.contains("parameters")) mergeMaterial(patch,action.at("parameters"));
                if(patch.contains("color_rgba")) {
                    if(action.contains("region")) throw std::invalid_argument("regional edits accept only optical, infrared and radar properties");
                    edit.commands.push_back({{"kind","material"},{"object",id},{"merge",true},
                        {"material",{{"color_rgba",patch.at("color_rgba")}}}});
                    patch.erase("color_rgba");
                }
                auto region=action.value("region",Json());
                if(region.contains("center")){
                    const auto pose=plannedPoses.count(id)?plannedPoses.at(id).front():existing->pose(scene->tick,scene->fixedStep);
                    const auto center=vsg::inverse(pose)*resolve(region.at("center"),*scene,mapPoint);
                    const auto halfSize=vec(region.at("size_m"))*.5;
                    region={{"min_m",xyz(center-halfSize)},{"max_m",xyz(center+halfSize)}};
                }
                if(!patch.empty()) edit.commands.push_back({{"kind","sensor_paint"},{"object",id},
                    {"region",std::move(region)},{"material",patch}});
            }
        } catch(const std::exception& error) {
            throw std::invalid_argument("action "+std::to_string(index+1)+" — "+error.what());
        }
    }
    if(equipmentChanged||(!plannedPoses.empty()&&!edit.equipmentBefore.is_null())) {
        auto validated=equipment;
        validated["topic"]=topicPath.empty()?"deployment.vtopic.json":topicPath;
        const auto config=VoxelEquipmentConfig::parse(validated,topicPath);
        for(const auto& device:config.equipment) {
            if(device.mountObject.empty())continue;
            const auto plannedPose=plannedPoses.find(device.mountObject);
            if(plannedPose!=plannedPoses.end())
                for(const auto& pose:plannedPose->second) checkSensorFootprint(*scene,device,pose);
            else if(attached.count(device.sensor.id)) {
                const auto& target=scene->objects.at(device.mountObject);
                checkSensorFootprint(*scene,device,target.pose(scene->tick,scene->fixedStep));
            }
        }
        equipment.erase("topic");
    }
    edit.equipmentAfter=equipmentChanged?std::move(equipment):edit.equipmentBefore;
    return edit;
}

void Adapter::applyInWorker(VoxelScene& runtime, Json& topic, const Json& plan, const std::string& topicPath) const {
    auto edit = stage(plan,runtime.snapshot(),topic,topicPath);
    VoxelSceneSnapshotPtr prepared;
    if (!edit.imports.at("objects").empty()) {
        auto preparation = VoxelScene::prepareImportAsync(edit.imports);
        prepared = preparation.snapshot.get();
    }
    commit(runtime,topic,edit,std::move(prepared));
}

Undo Adapter::commit(VoxelScene& runtime,Json& topic,const Edit& edit,VoxelSceneSnapshotPtr prepared) {
    if(runtime.snapshot()!=edit.before||equipmentOf(topic)!=edit.equipmentBefore||!runtime.paused())
        throw std::logic_error("scene or equipment changed after the deployment preview");
    const auto& imports=edit.imports.at("objects");
    if(bool(prepared)!=!imports.empty()||(prepared&&prepared->objects.size()!=imports.size()))
        throw std::invalid_argument("prepared model list differs from the staged deployment");
    for(const auto& expected:imports)
        if(!prepared->objects.count(expected.at("id").get<std::string>()))
            throw std::invalid_argument("prepared model does not match the staged object");
    auto candidate=topic;
    putEquipment(candidate,edit.equipmentAfter);
    Undo undo{edit.before,{},edit.equipmentBefore,edit.equipmentAfter};
    if(prepared||!edit.commands.empty()) runtime.edit(edit.commands,edit.before->revision,std::move(prepared));
    undo.after=runtime.snapshot();
    topic.swap(candidate);
    return undo;
}

void Adapter::undo(VoxelScene& runtime,Json& topic,const Undo& undo) {
    if(runtime.snapshot()!=undo.after||equipmentOf(topic)!=undo.equipmentAfter||!runtime.paused())
        throw std::logic_error("scene or equipment changed after deployment; cannot undo");
    auto candidate=topic;
    putEquipment(candidate,undo.equipmentBefore);
    if(undo.before!=undo.after) runtime.restoreAuthored(undo.before,undo.after->revision);
    topic.swap(candidate);
}
}
