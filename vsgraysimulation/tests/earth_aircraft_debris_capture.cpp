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

void runEarthAircraftDebrisCapture(const QString& output) {
    require(QApplication::platformName()=="xcb","debris capture requires native Vulkan");
    const auto directory=std::filesystem::absolute(output.toStdString());std::filesystem::create_directories(directory);
    const auto asset=std::filesystem::absolute("models/F-16/F-16.obj");
    const auto map=std::filesystem::absolute("config/earth/taipei-region.map.json");
    auto scene=VoxelScene::load({
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
        })}});
    const auto start=scene->snapshot();
    const auto& intact=start->objects.at("aircraft");
    const auto base=raycastVoxelScene(*start,{0,5000,0},{0,-1,0},10000,true);
    require(bool(base),"no Taipei DEM ground in debris fixture");
    const double ground=base->position.y;
    const double altitude=ground+35.;
    scene->edit(json::array({{{"kind","place"},{"object","aircraft"},{"position_m",{0,altitude,0}},
        {"rotation_deg",{0,180,0}},{"velocity_mps",{1,0,-7}}}}),start->revision);
    const auto chart=simcore::geo::makeEnuToWorldMatrix(start->anchor);
    Viewport3DView view;view.resize(1120,630);
    bool ready=false;QString failure;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;view.setAutoFrame(false);});
    QObject::connect(view.widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&view,[&](const QString& msg){failure=msg;});
    auto cfg=ViewportConfig::fromPreset("earth_display");cfg.remove(OceanFeature::kId);
    cfg.add(EarthView::kId,{{"map_json",map.string()},{"lat",start->anchor.lat_deg},{"lon",start->anchor.lon_deg},{"alt",start->anchor.alt_m},
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
