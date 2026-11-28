#include <vsgraysim/earth/VoxelSceneSensors.h>
#include "earth_aviation_capture.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include "../src/earth/VoxelTerrain.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <vsg/all.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace vsgraysim::viewport3d;
using namespace vsgraysim::earth;
using json=nlohmann::json;
using V=vsg::dvec3;
void require(bool ok,const char* e){if(!ok)throw std::runtime_error(e);}
void pump(int ms) {
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<ms){QApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
}
double p95(std::vector<double> input) {
    if(input.empty())return 0.;
    std::sort(input.begin(),input.end());
    return input[size_t(std::ceil(input.size()*.95))-1];
}
}

void runEarthAircraftDebrisCapture(const QString& output,bool impactCrush) {
    require(QApplication::platformName()=="xcb","debris capture requires native Vulkan");
    const auto directory=std::filesystem::absolute(output.toStdString());std::filesystem::create_directories(directory);
    const auto asset=std::filesystem::absolute("models/F-16/F-16.obj");
    const auto map=std::filesystem::absolute("config/earth/taipei-region.map.json");
    json definition={
        {"format","voxel-scene-v1"},{"fixed_step_seconds",.02},
        {"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",364.}}},
        {"utc_origin",{{"year",2026},{"month",11},{"day",18},{"hours_utc",4.}}},
        {"objects",json::array({
            {{"id","terrain"},{"role","terrain"},{"source",{{"type","earth"},{"map",map.string()},
                {"earth",{{"coverage",{{"min_x",-128},{"max_x",128},{"min_z",-128},{"max_z",128}}},
                {"grid_size",65},{"imagery_size",128},{"strict",true}}},
                {"cell_size_m",4},{"bottom_m",-55},{"mode","hd"}}}},
            {{"id","aircraft"},{"role","target"},{"velocity_mps",{1,0,-7}},
                {"rotation_deg",{0,180,0}},
                {"source",{{"type","mesh"},{"path",asset.string()},{"max_dimension",88},{"up_axis","y"},
                    {"surface_detail","source_clipped"},{"fill_interior",true}}},
                {"physics",{{"motion","prescribed"},{"fracture",true},{"support","none"},{"linear_damping",.11},
                    {"angular_damping",.25},{"friction",.74},{"restitution",.10}}},
                {"material",{{"density_kg_m3",325},{"color_rgba",{1.,1.,1.,1.}}}}}
        })}};
    if(impactCrush){
        definition["geo_anchor"]={{"lat",25.0692},{"lon",121.5487},{"alt",35.}};
        auto& aircraft=definition["objects"][1];
        aircraft["source"]["max_dimension"]=112;
        aircraft["physics"]["motion"]="dynamic";
        aircraft["physics"]["crush"]={{"radius_m",8.5},{"yield_speed_mps",3.},{"max_strain",.78}};
    }
    auto scene=VoxelScene::load(definition);
    const auto start=scene->snapshot();
    const auto& intact=start->objects.at("aircraft");
    const auto base=raycastVoxelScene(*start,{0,5000,0},{0,-1,0},10000,true);
    require(bool(base),"no Taipei DEM ground in debris fixture");
    const double ground=base->position.y;
    const double altitude=ground+(impactCrush?4.1:35.);
    scene->edit(json::array({{{"kind","place"},{"object","aircraft"},{"position_m",{0,altitude,0}},
        {"rotation_deg",{0,180,0}},{"velocity_mps",impactCrush?json::array({.45,-6.8,-2.0}):json::array({1,0,-7})}}}),start->revision);
    const auto chart=simcore::geo::makeEnuToWorldMatrix(start->anchor);
    Viewport3DView view;view.resize(1120,630);
    bool ready=false;QString failure;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;view.setAutoFrame(false);});
    QObject::connect(view.widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&view,[&](const QString& msg){failure=msg;});
    auto contextMap=map;
    if(impactCrush){
        json context;
        std::ifstream(map)>>context;
        auto& rect=context["earth"];
        rect["coverage"]={{"min_x",-5200},{"max_x",5200},{"min_z",-5200},{"max_z",5200}};
        rect["grid_size"]=261;
        rect["imagery_size"]=1024;
        contextMap=directory/"taipei-impact-context.map.json";
        std::ofstream(contextMap)<<context.dump(2);
    }
    auto cfg=ViewportConfig::fromPreset("earth_display");cfg.remove(OceanFeature::kId);
    cfg.add(EarthView::kId,{{"map_json",contextMap.string()},{"lat",start->anchor.lat_deg},{"lon",start->anchor.lon_deg},{"alt",start->anchor.alt_m},
        {"view_height",180},{"cloud_objects",true}});
    cfg.add(CloudFeature::kId,{{"cover",.18},{"wind_speed",3.5},{"wind_az",60.}});
    cfg.add(VoxelSceneFeature::kId,{{"external_only",true}});
    cfg.add(EngagementVisualFeature::kId);
    view.applyConfig(cfg);view.show();
    QElapsedTimer readyTime;readyTime.start();
    while(!ready&&failure.isEmpty()&&readyTime.elapsed()<60000)pump(10);
    require(ready&&failure.isEmpty(),"Taipei aircraft debris viewport not ready");
    auto* earth=view.featureAs<EarthViewFeature>(EarthView::kId);
    auto* voxel=view.featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto* fx=view.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId);
    require(earth&&earth->surfaceSnapshot()&&earth->surfaceSnapshot()->valid_samples&&voxel&&fx&&fx->particleMode(),
        "missing DEM, physical scene, or fire pipeline");
    earth->setCloudPresentationEnabled(false);
    const auto pose=[&](V eye,V look) {
        auto* camera=view.context().camera();
        auto target=camera->viewMatrix.cast<vsg::LookAt>();
        target->eye=chart*eye;target->center=chart*look;
        const auto u=chart*vsg::dvec4(0,1,0,0);target->up={u.x,u.y,u.z};
        camera->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=47;
    };
    pose({49,ground+33,28},{0,ground+18,-12});
    auto render=[&] {
        view.setAutoFrame(false);view.context().setSimulationTime(scene->snapshot()->timeSeconds());
        const auto serial=view.widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer waiting;waiting.start();
        do { view.renderFrame();if(view.widget()->property("renderFrameSerial").toULongLong()>serial)break;pump(18); }
        while(waiting.elapsed()<2500);
        view.context().viewer()->deviceWaitIdle();
        require(view.widget()->property("renderFrameSerial").toULongLong()>serial,"debris frame did not present");
        return view.widget()->property("renderFrameMs").toDouble();
    };
    auto capture=[&](const std::string& file) {
        const auto frame=view.grabFrame();
        require(!frame.isNull()&&frame.save(QString::fromStdString((directory/(file+".png")).string())),"debris capture failed");
    };
    if(impactCrush){
        pose({15,ground+7.1,13},{0,ground+1.2,-1.});
        const auto frame=[&](VoxelSceneSnapshotPtr value,const char* name){
            voxel->bindSnapshot(value);render();capture(name);
        };
        frame(scene->snapshot(),"01-before-contact");
        scene->setPaused(false);
        VoxelSceneSnapshotPtr before,compacted;
        double crushMs=0.;
        for(int tick=1;tick<=85&&!compacted;++tick){
            before=scene->snapshot();
            const auto result=scene->advanceTo(tick*.02,1);
            if(result.crushedBodies){compacted=scene->snapshot();crushMs=result.crushMs;}
        }
        require(bool(compacted),"airframe did not permanently crush at a Taipei ground contact");
        const auto& wreck=compacted->objects.at("aircraft");
        require(wreck.volume->occupiedCount()==intact.volume->occupiedCount()&&wreck.crushedAtTick==compacted->tick,
            "airframe crush removed matter");
        require(wreck.volume->appearance()&&wreck.surface().imagery_uv.size()==wreck.surface().vertices().size(),
            "the deformed airframe lost its material layer");
        // Hold the post-contact rigid pose and sky time for a direct geometry comparison.
        auto comparison=std::make_shared<VoxelSceneSnapshot>(*compacted);
        ++comparison->contentRevision;
        auto& reference=comparison->objects.at("aircraft");
        reference.volume=before->objects.at("aircraft").volume;
        reference.geometry=before->objects.at("aircraft").geometry;
        reference.collision=before->objects.at("aircraft").collision;
        frame(comparison,"02-at-contact-without-crush");
        frame(compacted,"03-at-contact-with-crush");
        require(voxel->display().placements.at("aircraft")->matrix==wreck.pose(compacted->tick,compacted->fixedStep),
            "display lost the fused impact pose");
        auto sensor=makeVoxelSceneSensorData(compacted);
        const auto exposed=std::find_if(sensor.instances.begin(),sensor.instances.end(),
            [](const auto& item){return item.id=="aircraft";});
        require(exposed!=sensor.instances.end()&&exposed->transform==wreck.pose(compacted->tick,compacted->fixedStep),
            "sensor topology did not receive the impact snapshot");
        unsigned rayMatches=0,changedColumns=0;
        const auto bodyPose=wreck.pose(compacted->tick,compacted->fixedStep);
        for(int z=-6;z<=6;++z)for(int x=-4;x<=4;++x){
            const auto eye=bodyPose*V(x*.63+.022,10.,z*.93+.036);
            const auto a=scene->raycast(eye,{0,-1,0},20);
            const auto b=scene->rigidRaycast(eye,{0,-1,0},20);
            if(a&&a->object=="aircraft"){
                require(b&&b->object=="aircraft"&&std::abs(a->distance-b->distance)<.006,
                    "the deformed hull and Bullet collision ray diverged");
                ++rayMatches;
            }
            const auto prior=raycastVoxelScene(*comparison,eye,{0,-1,0},20,false,"aircraft");
            if(prior&&(!a||a->object!="aircraft"||a->distance>prior->distance+.075))++changedColumns;
        }
        require(rayMatches>=8&&changedColumns>=4,"native shot did not verify a visibly lowered profile");
        const auto nextTick=compacted->tick+1;
        for(double x:{-2.4,2.4})
            scene->schedule({{"kind","fracture"},{"object","aircraft"},{"tick",nextTick},
                {"point_m",{x,.10,0}},{"normal",{1,0,0}},{"radius_m",8.},{"energy_j",1.e6}});
        fx->setEnabled(false);
        QElapsedTimer update;
        update.start();
        scene->advanceTo(nextTick*.02,1);
        const double breakMs=update.nsecsElapsed()/1e6;
        std::vector<double> steady;
        size_t major=0,total=0;
        for(unsigned i=1;i<=28;++i){
            scene->advanceTo((nextTick+i*2)*.02,2);
            voxel->bindSnapshot(scene->snapshot());steady.push_back(render());
        }
        for(const auto& [id,piece]:scene->snapshot()->objects)if(piece.role=="target"){
            total+=piece.volume->occupiedCount();
            if(id!="aircraft"&&piece.volume->occupiedCount()>60)++major;
        }
        require(major>=1&&total==intact.volume->occupiedCount(),"wing separation did not keep the compacted hull and rigid parts");
        capture("04-crushed-fuselage-and-fragments");
        auto rewind=VoxelScene::load(scene->checkpoint());
        require(rewind->checkpoint()==scene->checkpoint(),"impact wreckage did not survive save and reopen");
        std::ofstream(directory/"impact-acceptance.json")<<json{{"deformed_at_s",compacted->timeSeconds()},
            {"cell_size_m",wreck.volume->resolution_m},{"changed_surface_columns",changedColumns},
            {"ray_matches",rayMatches},{"major_wing_fragments",major},{"mass_kg",wreck.massKg()},
            {"crush_and_collider_update_ms",crushMs},{"wing_fracture_update_ms",breakMs},
            {"steady_render_ms_p95",p95(steady)}}.dump(2)<<'\n';
        view.hide();pump(20);
        std::cout<<"PASS: Taipei occupied impact deformation, rigid wing debris and Vulkan capture\n";
        return;
    }
    view.setAutoFrame(false);voxel->bindSnapshot(scene->snapshot());render();capture("01-intact");
    const auto totalVoxels=intact.volume->occupiedCount();
    const auto cut=[&](double z) {
        // Two authored structural break planes. No material is deleted and
        // the existing connected components become the actual rigid bodies.
        scene->schedule({{"kind","fracture"},{"object","aircraft"},{"tick",1},
            {"space","object"},{"point_m",{0,0,z}},{"normal",{0,0,1}},{"radius_m",9.},
            {"energy_j",1.0e6}});
    };
    cut(-2.05);cut(2.10);
    scene->setPaused(false);
    QElapsedTimer update;update.start();scene->advanceTo(.02);
    const double breakMs=update.nsecsElapsed()/1e6;
    auto fracture=scene->snapshot();
    require(fracture->journal.size()==2,"fracture operations were not consumed");
    std::vector<std::string> pieceIds;
    size_t count=0;double weight=0.;
    for(const auto& [id,o]:fracture->objects)if(o.role=="target"&&o.volume->occupiedCount()) {
        count+=o.volume->occupiedCount();weight+=o.massKg();
        if(o.volume->occupiedCount()>=totalVoxels/28)pieceIds.push_back(id);
    }
    require(count==totalVoxels&&std::abs(weight-intact.massKg())<.0001,"structural cuts did not preserve occupancy and mass");
    require(pieceIds.size()>=3&&pieceIds.size()<=6,"airframe did not produce a few large rigid pieces");
    // A gentle, deterministic off-centre disturbance is applied to the real
    // bodies so they separate and roll rather than sharing the same path.
    const auto bodyCentre=[&](const VoxelSceneSnapshot& s,const VoxelSceneObject& o){return o.pose(s.tick,s.fixedStep)*o.localCentreOfMass();};
    const auto reference=bodyCentre(*fracture,fracture->objects.at("aircraft"));
    const auto separation=[&](const VoxelSceneObject& o) {
        const double z=bodyCentre(*fracture,o).z-reference.z;
        return V(std::clamp(z*.93,-4.8,4.8),.16*z,std::clamp(z*.91,-5.2,5.2));
    };
    V weightedVelocity{};
    double selectedMass=0.;
    for(const auto& id:pieceIds) {
        const auto& o=fracture->objects.at(id);
        weightedVelocity+=separation(o)*o.massKg();
        selectedMass+=o.massKg();
    }
    weightedVelocity=weightedVelocity/selectedMass;
    for(const auto& id:pieceIds) {
        const auto& o=fracture->objects.at(id);
        require(o.body.motion=="dynamic","fractured piece did not enter rigid-body simulation");
        const auto com=bodyCentre(*fracture,o);
        const auto push=(separation(o)-weightedVelocity)*o.massKg();
        const auto point=com+V(.47,.13,.08);
        scene->schedule({{"kind","impulse"},{"object",id},{"tick",2},{"space","scene"},
            {"point_m",{point.x,point.y,point.z}},{"impulse_ns",{push.x,push.y,push.z}}});
    }
    voxel->bindSnapshot(fracture);render();capture("02-fracture");
    require(fx->burningDebrisCount()>=pieceIds.size(),"debris fire did not bind to fractured bodies");
    std::vector<double> physicsMs,renderMs;
    std::set<std::string> touched;
    bool rolling=false;
    double maxSpread=0.;
    json records=json::array();
    for(int frame=1;frame<=65;++frame) {
        const double t=.02+.10*frame;
        update.restart();scene->advanceTo(t,8);physicsMs.push_back(update.nsecsElapsed()/1e6);
        const auto s=scene->snapshot();
        for(const auto& c:s->contacts) {
            if(c.a=="terrain")touched.insert(c.b);
            if(c.b=="terrain")touched.insert(c.a);
        }
        voxel->bindSnapshot(s);renderMs.push_back(render());
        json parts=json::array();
        for(const auto& id:pieceIds) {
            const auto& o=s->objects.at(id);const auto p=bodyCentre(*s,o);
            for(const auto& other:pieceIds)
                maxSpread=std::max(maxSpread,vsg::length(p-bodyCentre(*s,s->objects.at(other))));
            if(vsg::length(o.angularVelocity)>.14)rolling=true;
            require(o.pose(s->tick,s->fixedStep)==voxel->display().placements.at(id)->matrix,
                    "debris viewport and collider truth disagree on pose");
            parts.push_back({{"id",id},{"x",p.x},{"y",p.y},{"z",p.z},{"angular_speed",vsg::length(o.angularVelocity)}});
        }
        records.push_back({{"t",s->timeSeconds()},{"pieces",parts},{"contacts",s->contacts.size()}});
        if(frame==7)capture("03-falling");
        if(frame==16)capture("04-tumbling");
        if(frame==32)capture("05-ground-contact");
        if(frame==65)capture("06-burning-wreckage");
        char name[40];
        std::snprintf(name,sizeof(name),"sequence-%03d",frame);
        capture(name);
    }
    require(rolling&&maxSpread>15.&&std::all_of(pieceIds.begin(),pieceIds.end(),[&](const auto& id){return touched.count(id);}),
            "major fragments did not separate, tumble and touch the DEM terrain");
    require(fx->burningDebrisCount()>=pieceIds.size()&&fx->smokeHistoryCount()>15,"ground debris lost its fire or smoke");
    const auto pausedSmoke=fx->smokeHistoryCount();render();
    require(fx->smokeHistoryCount()==pausedSmoke,"rendering a paused fracture spawned more smoke");
    const auto final=scene->snapshot();
    const auto sensor=makeVoxelSceneSensorData(final);
    for(const auto& id:pieceIds) {
        const auto& body=final->objects.at(id);
        const auto instance=std::find_if(sensor.instances.begin(),sensor.instances.end(),[&](const auto& value){return value.id==id;});
        require(instance!=sensor.instances.end()&&instance->transform==body.pose(final->tick,final->fixedStep),
                "sensor geometry did not receive the fragment pose");
        const auto p=bodyCentre(*final,body);
        const V eye=p+V(.021,32,.037);
        const auto a=scene->raycast(eye,{0,-1,0},80), b=scene->rigidRaycast(eye,{0,-1,0},80);
        require(bool(a)==bool(b)&&(!a||(a->object==b->object&&std::abs(a->distance-b->distance)<.035)),
            "raster terrain or fragment disagrees with collision ray");
    }
    fx->setEnabled(false);
    render();
    capture("07-wreckage-without-particles");
    std::ofstream file(directory/"debris-acceptance.json");
    file<<json{{"asset",asset.string()},{"earth_map",map.string()},{"utc","2026-11-18T04:00:00Z"},
        {"major_fragments",pieceIds},{"contact_body_ids",touched},{"voxel_size_m",intact.volume->resolution_m},
        {"mass_conserved",true},{"rotation_and_contact_verified",true},{"display_collision_pose_verified",true},
        {"sensor_geometry_pose_verified",true},
        {"paused_smoke_verified",true},{"max_piece_separation_m",maxSpread},
        {"fracture_update_ms",breakMs},{"steady_physics_100ms_batch_p95_ms",p95(physicsMs)},
        {"steady_render_p95_ms",p95(renderMs)},{"render_fenced",view.widget()->property("renderFrameFenced").toBool()},
        {"data",records}}.dump(2)<<'\n';
    view.hide();pump(20);
}
