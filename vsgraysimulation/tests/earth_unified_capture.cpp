#include <vsgraysim/earth/VoxelSensorSession.h>
#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/SurfaceQuality.h>
#include <vsgraysim/earth/SurfaceClusters.h>
#include <rocky_env/GpuContext.h>
#include "earth_unified_capture.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/VsgQtWidget.h"
#include <simcore/GeoNavigation.h>
#include "../src/earth/VoxelTerrain.h"
#include "../src/earth/VoxelDynamics.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMainWindow>
#include <QDockWidget>
#include <QLabel>
#include <QToolBar>
#include <QTimer>
#include <QThread>
#include <vsg/all.h>
#include "../src/earth/OsgVoxelImport.h"
#include <vsgXchange/all.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <limits>
#include <algorithm>

namespace {
using namespace vsgraysim;using namespace vsgraysim::earth;using namespace vsgraysim::viewport3d;
using namespace rockyenv::sensor;using json=nlohmann::json;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void pump(int ms){QElapsedTimer timer;timer.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(timer.elapsed()<ms);}
vsg::dvec3 occupiedCentre(const VoxelSceneObject& o) {
    const auto n=o.volume->dimensions();const auto origin=o.volume->gridOrigin();const auto target=o.localCentreOfMass();
    vsg::dvec3 result=target;double best=std::numeric_limits<double>::infinity();
    for(size_t i=0;i<o.volume->cellCount();++i)if(o.volume->occupancy()[i]) {
        const auto p=origin+vsg::dvec3(i%n[0]+.5,i/n[0]%n[1]+.5,i/(n[0]*n[1])+.5)*o.volume->resolution_m;
        const double d=vsg::length(p-target);if(d<best){best=d;result=p;}
    }
    return result;
}
}
void runEarthTargetDestruction(const QString& output,int maxDimension) {
    require(maxDimension>=16&&maxDimension<=512,"target resolution must be in [16,512]");
    require(QApplication::platformName()=="xcb","target acceptance needs a real native surface");
    const auto directory=std::filesystem::absolute(output.toStdString());
    std::filesystem::create_directories(directory);
    const auto asset=std::filesystem::absolute("models/MQ-9/MQ-9.osgb");
    const auto map=std::filesystem::absolute("config/earth/default.map.json");
    json definition={{"format","voxel-scene-v1"},{"fixed_step_seconds",.02},
        {"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",364}}},
        {"objects",json::array({
            {{"id","terrain"},{"role","terrain"},{"source",{{"type","earth"},{"map",map.string()},
                {"earth",{{"coverage",{{"min_x",-80},{"max_x",80},{"min_z",-80},{"max_z",80}}},
                          {"grid_size",81},{"imagery_size",128},{"strict",true}}},
                {"cell_size_m",4},{"bottom_m",-32},{"mode","hd"}}}},
            {{"id","aircraft"},{"role","target"},{"position_m",{0,30,0}},
             {"source",{{"type","mesh"},{"path",asset.string()},{"max_dimension",maxDimension},{"up_axis","z"},{"fill_interior",true}}},
             {"physics",{{"motion","static"},{"fracture",true},{"support","none"},{"restitution",.08},{"friction",.72}}},
             {"material",{{"density_kg_m3",320},{"color_rgba",{1.,1.,1.,1.}},{"temperature_k",315}}}}
        })}};
    auto prepared=VoxelScene::load(definition);
    const auto initial=prepared->snapshot();
    const auto& aircraft=initial->objects.at("aircraft");
    const auto box=aircraft.localBounds();
    const auto com=aircraft.localCentreOfMass();
    const double span=vsg::length(box.max-box.min);
    const auto ground=raycastVoxelScene(*initial,{0,5000,0},{0,-1,0},10000,true);
    require(bool(ground),"destruction fixture has no physical DEM ground");
    const vsg::dvec3 offset{-com.x,ground->position.y+8.-box.min.y,-com.z};
    prepared->edit(json::array({{{"kind","place"},{"object","aircraft"},{"position_m",{offset.x,offset.y,offset.z}}}}),initial->revision);
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    QMainWindow window;window.setFixedSize(960,540);
    auto* view=new Viewport3DView(&window);window.setCentralWidget(view);
    bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);
    config.add(EarthView::kId,{{"map_json",map.string()},{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},
                              {"cloud_objects",true},{"view_height",1200}});
    config.add(CloudFeature::kId,{{"cover",.45},{"wind_speed",6},{"stratus_altitude",1900},{"cumulus_altitude",3200}});
    config.add(VoxelSceneFeature::kId,{{"checkpoint",prepared->checkpoint()},{"paused",true},
                                      {"advance_from_frames",false},{"frame_scene",false}});
    view->applyConfig(config);window.show();
    QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<45000)pump(10);
    require(ready&&failure.isEmpty(),"target scene viewport failed to initialise");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto& scene=*feature->runtime();
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    const vsg::dvec3 focus=offset+com-vsg::dvec3(0,3,0);
    camera->eye=chart*(focus+vsg::dvec3(.80,.56,.95)*span*1.65);
    camera->center=chart*focus;
    const auto up=chart*vsg::dvec4(0,1,0,0);camera->up={up.x,up.y,up.z};
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    json report={{"asset",asset.string()},{"cell_size_m",aircraft.volume->resolution_m},
                 {"initial_target_voxels",aircraft.volume->occupiedCount()},{"frames",json::array()},
                 {"geo_anchor",{{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m}}},{"source_dem_spacing_m",initial->objects.at("terrain").surface().dem_spacing_m},
                 {"source_imagery_spacing_m",initial->objects.at("terrain").surface().imagery_spacing_m}};
    std::vector<double> frameTimes;
    auto render=[&]{view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();view->context().setSimulationTime(scene.snapshot()->timeSeconds());
        auto viewer=view->context().viewer();const auto first=viewer->getFrameStamp()->frameCount;QElapsedTimer timeout;timeout.start();
        while(timeout.elapsed()<3000){QThread::msleep(21);QElapsedTimer elapsed;elapsed.start();view->renderFrame();viewer->deviceWaitIdle();const double ms=elapsed.nsecsElapsed()/1e6;
            if(viewer->getFrameStamp()->frameCount>first){frameTimes.push_back(ms);return ms;}}
        throw std::runtime_error("target timing did not submit a native frame");};
    auto capture=[&](const std::string& name) {
        render();QImage image;QElapsedTimer timeout;timeout.start();
        while(image.isNull()&&timeout.elapsed()<10000){view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();pump(10);QThread::msleep(50);image=view->grabFrame();if(image.isNull()){window.showNormal();window.raise();render();}}
        require(!image.isNull(),"target native framebuffer capture failed");
        require(image.save(QString::fromStdString((directory/(name+".png")).string())),"target snapshot save failed");
    };
    scene.saveCheckpoint((directory/"intact-checkpoint.json").string());
    capture("intact");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=20.;capture("intact-detail");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    // Render the exact original OSGB triangle mesh and source UV atlas at
    // the identical geodetic placement, lighting and camera as its OSGB voxels.
    struct SourceTriangles : vsg::Visitor {
        using vsg::Visitor::apply;
        struct Batch{vsg::ref_ptr<vsg::Data> image;std::vector<vsg::vec3> p,n;std::vector<vsg::vec2> uv;std::vector<vsg::vec4> color;};
        std::map<const vsg::Data*,Batch> batches;
        void apply(vsg::Node& n)override{n.traverse(*this);}
        void apply(vsg::VertexIndexDraw& draw)override {
            auto image=draw.getObject<vsg::Data>("voxel.import.image");auto& batch=batches[image];batch.image=image;
            const auto* p=draw.arrays[0]->data->cast<vsg::dvec3Array>();const auto* uv=draw.getObject<vsg::vec2Array>("voxel.import.uv");const auto* colors=draw.getObject<vsg::vec4Array>("voxel.import.color");const auto* normals=draw.getObject<vsg::vec3Array>("voxel.import.normal");
            const auto cross=vsg::cross((*p)[1]-(*p)[0],(*p)[2]-(*p)[0]);const auto normal=vsg::length(cross)>1e-12?vsg::normalize(cross):vsg::dvec3(0,0,1);
            for(size_t i=0;i<3;++i){batch.p.emplace_back((*p)[i]);batch.n.emplace_back(normals?vsg::dvec3((*normals)[i]):normal);batch.uv.push_back((*uv)[i]);batch.color.push_back((*colors)[i]);}
        }
    } source;
    auto raw=loadOsgVoxelMesh(asset.string());raw->accept(source);
    auto original=vsg::Group::create();
    for(const auto& [image,batch]:source.batches) {
        auto p=vsg::vec3Array::create(batch.p.size());std::copy(batch.p.begin(),batch.p.end(),p->begin());
        auto n=vsg::vec3Array::create(batch.n.size());std::copy(batch.n.begin(),batch.n.end(),n->begin());
        auto uv=vsg::vec2Array::create(batch.uv.size());std::copy(batch.uv.begin(),batch.uv.end(),uv->begin());
        auto color=vsg::vec4Array::create(batch.color.size());std::copy(batch.color.begin(),batch.color.end(),color->begin());
        auto indices=vsg::uintArray::create(batch.p.size());for(size_t i=0;i<indices->size();++i)(*indices)[i]=i;
        auto draw=vsg::VertexIndexDraw::create();draw->assignArrays({p,n,uv,color});draw->assignIndices(indices);draw->indexCount=indices->size();draw->instanceCount=1;
        auto builder=vsg::Builder::create();builder->shaderSet=vsg::createPhongShaderSet();
        auto material=vsg::PhongMaterialValue::create();material->value().diffuse=vsg::vec4(1,1,1,1);material->value().specular=vsg::vec4(0,0,0,1);
        for(auto& binding:builder->shaderSet->descriptorBindings)if(binding.name=="material")binding.data=material;
        vsg::StateInfo state;state.image=batch.image;state.instance_colors_vec4=false;auto group=builder->createStateGroup(state);group->addChild(draw);original->addChild(group);
    }
    require(bool(original),"original target reference failed to load");
    auto originalPose=vsg::MatrixTransform::create(chart*vsg::translate(offset)*vsg::rotate(-vsg::PI*.5,vsg::dvec3(1,0,0)));originalPose->addChild(original);
    auto reference=vsg::Switch::create();reference->addChild(true,originalPose);view->context().worldRoot()->addChild(reference);
    auto placement=feature->display().placements.at("aircraft");auto voxelChildren=placement->children;placement->children.clear();
    view->context().requestCompile();capture("original");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=20.;capture("original-detail");
    view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=43.;
    reference->setAllChildren(false);placement->children=voxelChildren;
    const auto point=occupiedCentre(scene.snapshot()->objects.at("aircraft"));
    scene.schedule({{"kind","damage"},{"object","aircraft"},{"tick",1},{"space","object"},
                    {"point_m",{point.x,point.y,point.z}},{"radius_m",span*.115},
                    {"blast_impulse_ns",aircraft.massKg()*5.}});
    GpuSensorSpec ir;ir.id=191;ir.period_seconds=.08;ir.output=ProductStage::Scene;ir.profile.width=9;ir.profile.height=7;ir.profile.fov_deg=2;
    ir.profile.detector.integration_ms=1;ir.profile.band=BandSpec::make(BandType::MWIR);auto tv=ir;tv.id=192;tv.profile.band=BandSpec::make(BandType::TV);
    VoxelSensorSession measurements(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"target-contact-sensors");
    auto aperture=[&](uint32_t,double seconds){const auto state=scene.sample(seconds);const auto& o=state->objects.at("aircraft");const auto centre=o.pose(state->tick,state->fixedStep)*o.localCentreOfMass();GpuAperture a;a.position={centre.x+.137,ground->position.y+50.,centre.z+.193};a.forward={0,-1,0};a.up={0,0,-1};return a;};
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_el_deg=90;e.sun_az_deg=0;return e;};
    std::map<uint32_t,size_t> measuredFrames;double maximumGpuRangeError=0;
    auto sensor=makeVoxelSceneSensorData(scene.snapshot());
    size_t contacts=0,referenceContactFrames=0,referenceContacts=0;double maximumRayError=0,maximumPenetration=0,maximumComSpeed=0,maximumAngularSpeed=0;
    VoxelDynamics contactIndex,contactReference(false);
    std::vector<double> physicsTimes,sensorTimes,completeTimes,steadyPhysicsTimes,steadyCompleteTimes;
    for(int frame=0;frame<70;++frame) {
        QElapsedTimer physicsTime;physicsTime.start();scene.setPaused(false);scene.advanceTo(.02+frame*.08,8);scene.setPaused(true);physicsTimes.push_back(physicsTime.nsecsElapsed()/1e6);
        const auto s=scene.snapshot();
        if(frame==0||frame==12||frame==69)std::cout<<"advance "<<(frame?80:20)<<" ms at frame "<<frame<<": "<<physicsTimes.back()<<" ms"<<std::endl;
        contacts+=s->contacts.size();
        for(const auto& c:s->contacts)maximumPenetration=std::max(maximumPenetration,c.penetrationM);
        size_t voxels=0;
        for(const auto& [id,o]:s->objects)if(o.role=="target")voxels+=o.volume->occupiedCount();
        const double renderMs=render();
        const auto& display=feature->display();
        if(sensor.snapshot->contentRevision!=s->contentRevision) {
            sensor=makeVoxelSceneSensorData(s);
            for(const auto& instance:sensor.instances) {
                auto group=display.placements.at(instance.id)->children.at(0).cast<vsg::StateGroup>();auto draw=group->children.at(0).cast<vsg::VertexIndexDraw>();
                const auto* a=draw->arrays[0]->data.cast<vsg::vec3Array>().get();const auto* b=instance.geometry->arrays[0]->data.cast<vsg::vec3Array>().get();
                require(a&&b&&a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin()),"native target display/sensor vertices differ");
                const auto* ai=draw->indices->data.cast<vsg::uintArray>().get();const auto* bi=instance.geometry->indices->data.cast<vsg::uintArray>().get();
                require(ai&&bi&&ai->size()==bi->size()&&std::equal(ai->begin(),ai->end(),bi->begin()),"native target display/sensor triangles differ");
            }
        }
        for(auto& instance:sensor.instances) {
            const auto& o=s->objects.at(instance.id);instance.transform=o.pose(s->tick,s->fixedStep);
            require(instance.transform==display.placements.at(instance.id)->matrix,"target display/sensor transforms disagree");
            if(o.body.motion=="dynamic") {const auto offset=o.pose(s->tick,s->fixedStep)*vsg::dvec4(o.localCentreOfMass(),0.);maximumComSpeed=std::max(maximumComSpeed,vsg::length(o.velocity+vsg::cross(o.angularVelocity,vsg::dvec3(offset.x,offset.y,offset.z))));maximumAngularSpeed=std::max(maximumAngularSpeed,vsg::length(o.angularVelocity));}
        }
        std::vector<GpuMeasurementProduct> products;QElapsedTimer sensorTime;sensorTime.start();
        measurements.advanceTo(s->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product){products.push_back(std::move(product));});
        sensorTimes.push_back(sensorTime.nsecsElapsed()/1e6);completeTimes.push_back(physicsTimes.back()+renderMs+sensorTimes.back());
        if(frame){steadyPhysicsTimes.push_back(physicsTimes.back());steadyCompleteTimes.push_back(completeTimes.back());}
        for(const auto& product:products) {
            ++measuredFrames[product.sensor_id];for(float value:product.rgba)require(std::isfinite(value),"native target sensor produced a nonfinite result");
            if(product.sensor_id!=ir.id)continue;
            const double t=.5*(product.start_time+product.end_time);const auto a=aperture(ir.id,t);const auto truth=scene.sample(t);const auto hit=raycastVoxelScene(*truth,a.position,a.forward,3000);
            const size_t centre=(3*9+4)*4;require(bool(hit)==(product.rgba[centre+1]>1),"native target IR hit mask disagrees with historical geometry");
            if(hit){maximumGpuRangeError=std::max(maximumGpuRangeError,std::abs(product.rgba[centre+3]*1000-hit->distance));require(maximumGpuRangeError<.03,"native target IR range disagrees with historical displayed geometry");require(std::abs(product.rgba[centre+1]-truth->objects.at(hit->object).material.at("temperature_k").get<double>())<.15,"native target IR used stale fragment material");}
        }
        for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x) {
            // Avoid rays lying exactly on a voxel edge: Bullet uses float while DDA uses double.
            const vsg::dvec3 origin{3.*x+.137,ground->position.y+50.,3.*z+.193};
            const auto a=scene.raycast(origin,{0,-1,0},100),b=scene.rigidRaycast(origin,{0,-1,0},100);
            require(bool(a)==bool(b),"damaged target lost geometric collision agreement");
            if(a&&b)maximumRayError=std::max(maximumRayError,std::abs(a->distance-b->distance));
            if(a&&!(a->object==b->object&&std::abs(a->distance-b->distance)<.03)) {
                std::cerr<<"probe "<<x<<","<<z<<" at "<<s->timeSeconds()<<" DDA "<<a->object<<" "<<a->distance<<" Bullet "<<b->object<<" "<<b->distance<<std::endl;
                throw std::runtime_error("target ray and collider differ after destruction");
            }
        }
        const auto removed=s->journal.at(0).at("removed_voxels").get<size_t>();
        require(voxels+removed==aircraft.volume->occupiedCount(),"target destruction lost surviving voxel material");
        if(frame==0||frame==9||frame==12||frame==20||frame==35||frame==50||frame==69) {
            auto indexed=*s,referenceState=*s;contactIndex.detect(indexed,1.);contactReference.detect(referenceState,1.);
            auto signature=[](const std::vector<VoxelContact>& input){std::vector<std::array<double,10>> out;for(const auto& c:input)out.push_back({c.pointA.x,c.pointA.y,c.pointA.z,c.pointB.x,c.pointB.y,c.pointB.z,c.normalOnB.x,c.normalOnB.y,c.normalOnB.z,c.penetrationM});std::sort(out.begin(),out.end());return out;};
            const auto a=signature(indexed.contacts),b=signature(referenceState.contacts);
            if(a.size()!=b.size()){std::cerr<<"native raw contact oracle frame "<<frame<<": indexed="<<a.size()<<" reference="<<b.size()<<std::endl;throw std::runtime_error("native fine target index lost or invented an original Bullet contact");}
            for(size_t i=0;i<a.size();++i)for(size_t k=0;k<a[i].size();++k)require(std::abs(a[i][k]-b[i][k])<.00005,"native fine target index changed an original Bullet contact");
            ++referenceContactFrames;referenceContacts+=a.size();
        }
        const auto name=QString("debris-%1").arg(frame,3,10,QChar('0')).toStdString();
        capture(name);
        report["frames"].push_back({{"seconds",s->timeSeconds()},{"bodies",s->objects.size()},
            {"surviving_voxels",voxels},{"contacts",s->contacts.size()},
            {"advance_simulation_ms",frame?80:20},{"advance_wall_ms",physicsTimes.back()},
            {"render_wall_ms",renderMs},{"sensor_wall_ms",sensorTimes.back()},{"complete_wall_ms",completeTimes.back()}});
        if(frame==0)capture("damaged");
        if(frame==12)capture("fragments-airborne");
        if(frame==69)capture("fragments-ground");
    }
    require(scene.snapshot()->objects.size()>2,"aircraft damage did not produce disconnected fragments");
    require(contacts>0,"target fragments did not contact the DEM ground");
    require(measuredFrames[ir.id]>50&&measuredFrames[tv.id]>50,"native target IR/TV acquisition cadence stopped after fracture");
    report["contacts_observed"]=contacts;report["events"]=scene.snapshot()->journal;
    report["reference_contact_frames"]=referenceContactFrames;report["reference_raw_contacts"]=referenceContacts;
    report["utc_origin"]=initial->utcOrigin;
    report["maximum_collision_ray_error_m"]=maximumRayError;report["range_probes_per_frame"]=49;std::sort(physicsTimes.begin(),physicsTimes.end());report["advance_80ms_median_ms"]=physicsTimes[physicsTimes.size()/2];
    report["advance_80ms_p95_ms"]=physicsTimes[size_t(.95*(physicsTimes.size()-1))];report["maximum_gpu_ir_range_error_m"]=maximumGpuRangeError;
    report["ir_frames"]=measuredFrames[ir.id];report["tv_frames"]=measuredFrames[tv.id];report["maximum_penetration_m"]=maximumPenetration;report["maximum_com_speed_mps"]=maximumComSpeed;report["maximum_angular_speed_rad_s"]=maximumAngularSpeed;
    std::sort(frameTimes.begin(),frameTimes.end());report["frame_ms_median"]=frameTimes[frameTimes.size()/2];report["frame_ms_p95"]=frameTimes[size_t(.95*(frameTimes.size()-1))];
    auto timing=[&](const char* name,std::vector<double> samples){std::sort(samples.begin(),samples.end());report[std::string(name)+"_median_ms"]=samples[samples.size()/2];report[std::string(name)+"_p95_ms"]=samples[size_t(.95*(samples.size()-1))];report[std::string(name)+"_maximum_ms"]=samples.back();};
    timing("sensor_acquire",sensorTimes);timing("complete_simulation",completeTimes);
    timing("steady_advance_80ms",steadyPhysicsTimes);timing("steady_complete_80ms",steadyCompleteTimes);
    size_t withinBudget=0;double activeWall=0;std::vector<double> steadyRender,lateContact;
    for(size_t i=1;i<report["frames"].size();++i){const auto& row=report["frames"][i];const double total=row["complete_wall_ms"].get<double>();withinBudget+=total<=80.;activeWall+=total;steadyRender.push_back(row["render_wall_ms"].get<double>());if(row["seconds"].get<double>()>=4.&&row["contacts"].get<size_t>())lateContact.push_back(total);}
    timing("steady_render",steadyRender);if(!lateContact.empty())timing("late_contact_complete_80ms",lateContact);
    report["steady_complete_within_80ms_frames"]=withinBudget;report["steady_complete_frame_count"]=steadyCompleteTimes.size();
    report["steady_complete_active_wall_ms"]=activeWall;report["steady_complete_realtime_factor"]=(steadyCompleteTimes.size()*80.)/activeWall;
    report["destruction_first_20ms_wall_ms"]=report["frames"][0]["advance_wall_ms"];
    report["complete_timing_scope"]="scene advance and publication (including topology/sensor preparation) + synchronized display render + actual IR/TV acquisition; excludes screenshot encoding, pacing sleeps and acceptance-only geometry/ray checks";
    scene.saveCheckpoint((directory/"target-checkpoint.json").string());
    std::ofstream(directory/"target-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Earth model voxelization, blast fragments, DEM contacts and consistent target sensors"<<std::endl;
    window.hide();pump(20);
}
void runUnifiedEarth(const QString& sceneFile,const QString& contextMap,const QString& output,bool interactive) {
    const auto validated=VoxelScene::open(sceneFile.toStdString());const auto initial=validated->snapshot();const auto model=simcore::geo::makeEnuToWorldMatrix(initial->anchor);
    QMainWindow window;window.resize(920,580);window.setWindowTitle(QStringLiteral("地球场景集成验证 — 影像/高程/目标/体素云/测量"));
    auto* view=new Viewport3DView(&window);window.setCentralWidget(view);bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,view,[&]{ready=true;});
    QObject::connect(view->context().widget(),&gui::VsgQtWidget::initializationFailed,view,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);
    config.add(EarthView::kId,{{"map_json",std::filesystem::absolute(contextMap.toStdString()).string()},{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},
        {"scene_context",true},{"view_height",1200},{"cloud_grid",{{"width",32},{"height",48},{"depth",32},{"pressure_iterations",8},{"cell_size_m",24}}}});
    config.add(SkyFeature::kId,initial->utcOrigin);
    config.add(CloudFeature::kId,{{"cover",1.0},{"stratus_altitude",450},{"cumulus_altitude",650},{"stratus_size",4000},{"wind_speed",8},{"wind_az",70}});
    config.add(VoxelSceneFeature::kId,{{"checkpoint",validated->checkpoint()},{"paused",true},{"advance_from_frames",false},{"frame_scene",false}});
    view->applyConfig(config);
    auto* dock=new QDockWidget(QStringLiteral("统一场景状态"),&window);auto* status=new QLabel;status->setWordWrap(true);status->setMinimumWidth(230);dock->setWidget(status);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* tools=window.addToolBar(QStringLiteral("地球仿真"));
    window.show();QElapsedTimer waiting;waiting.start();while(!ready&&failure.isEmpty()&&waiting.elapsed()<45000)pump(10);
    require(failure.isEmpty(),failure.toStdString().c_str());require(ready&&view->isReady(),"Earth integrated viewport initialization timed out");view->setAutoFrame(false);
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);auto* globe=view->featureAs<EarthViewFeature>(EarthView::kId);auto* sky=view->featureAs<SkyFeature>(SkyFeature::kId);
    require(feature&&feature->runtime()&&globe&&globe->showsSceneContext()&&sky,"Earth test did not retain globe, physical scene and sky");auto& scene=*feature->runtime();
    if(auto* native=view->context().widget()->view()) {
        native->viewDependentState->maxShadowDistance=2000.;native->viewDependentState->shadowMapBias=.0005;
    }
    require(globe->surfaceSnapshot()->orbital&&!globe->surfaceSnapshot()->imagery_rgba.empty(),"Earth context requires a native volume with global imagery roots");
    auto* camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>().get();require(camera,"Earth camera missing");
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus){camera->eye=model*eye;camera->center=model*focus;const auto up=model*vsg::dvec4(0,1,0,0);camera->up={up.x,up.y,up.z};};
    auto render=[&]{view->context().setSimulationTime(scene.snapshot()->timeSeconds());view->renderFrame();view->context().viewer()->deviceWaitIdle();};
    std::filesystem::path directory=output.toStdString();if(!interactive)std::filesystem::create_directories(directory);
    auto capture=[&](const char* name){render();pump(160);auto image=view->grabFrame();require(!image.isNull(),"Earth integrated framebuffer missing");require(image.save(QString::fromStdString((directory/(std::string(name)+".png")).string())),"Earth integrated capture failed");return image.convertToFormat(QImage::Format_RGB32);};
    if(!interactive) {
        const auto orbit=simcore::geo::initialCamera(initial->anchor,16000000.);camera->eye=orbit.eye;camera->center=orbit.center;camera->up=orbit.up;capture("earth-orbit");
    }
    pose({180,130,240},{-20,90,0});view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=75;
    for(int i=0;i<3;++i)render();
    auto resources=globe->cloudResources();require(resources&&resources->field&&resources->field->ready(),"Earth renderer did not own a physical GPU cloud field");
    auto binding=globe->sceneCloud(model);require(binding->resources==resources,"Earth sensor bound a separate cloud instance");
    GpuSensorSpec ir;ir.id=91;ir.period_seconds=initial->fixedStep;ir.output=ProductStage::Scene;ir.profile.width=9;ir.profile.height=7;ir.profile.fov_deg=4;
    ir.profile.detector.integration_ms=1;ir.profile.band=BandSpec::make(BandType::MWIR);auto tv=ir;tv.id=92;tv.profile.band=BandSpec::make(BandType::TV);
    VoxelSensorSession clear(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"earth-clear-reference");
    VoxelSensorSession measured(scene,{ir,tv},view->context().window()->getOrCreateDevice(),"earth-shared-cloud",binding);
    std::map<uint32_t,std::vector<GpuMeasurementProduct>> reference,products;
    auto aperture=[&](uint32_t,double seconds){const auto s=scene.sample(seconds);const auto& o=s->objects.at("ground-MQ9");const auto p=o.pose(s->tick,s->fixedStep)*occupiedCentre(o);GpuAperture a;a.position={p.x,1500,p.z};a.forward={0,-1,0};a.up={0,0,-1};return a;};
    auto environment=[&](uint32_t,double time,const BandSpec& band){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=band.type==BandType::TV?100:0;
        auto* sun=sky->sunProvider();sun->syncDateTime(initial->utcOrigin.at("year"),initial->utcOrigin.at("month"),initial->utcOrigin.at("day"),initial->utcOrigin.at("hours_utc").get<double>()+time/3600);
        rockyenv::EnvSunInfo light;if(sun->sunInfo(model,light)){e.sun_el_deg=light.elevationDeg;e.sun_az_deg=std::atan2(light.dirENU.x,-light.dirENU.z)*180/3.141592653589793;}
        return e;};
    auto refresh=[&]{const auto s=scene.snapshot();status->setText(QString::fromStdString("Earth + OSG targets + GPU clouds\n\nTick "+std::to_string(s->tick)+" / "+std::to_string(s->timeSeconds())+" s\nTerrain: "+s->objects.at("earth-terrain").terrainMode+"\nBodies: "+std::to_string(s->objects.size())+"\nContacts: "+std::to_string(s->contacts.size())+"\nCloud revision: "+std::to_string(resources->field->revision())+"\nAircraft wakes: "+std::to_string(globe->sceneCloudColliders())+"\nIR/TV frames: "+std::to_string(products[91].size())+" / "+std::to_string(products[92].size())+"\n\nSame geometry, shared cloud image.\nCloud held over each measurement interval."));};
    double minimumTransmission=1.;size_t contacts=0,maxWakes=0;uint64_t frames=0;
    auto step=[&] {
        const auto next=scene.snapshot()->timeSeconds()+initial->fixedStep;scene.setPaused(false);scene.advanceTo(next,1);scene.setPaused(true);
        // The cloud at the previous boundary is held through this acquisition
        // interval. Drain measurements before executing the next native cloud
        // step, avoiding the use of a future preview field for old exposures.
        clear.advanceTo(next,aperture,environment,[&](auto&& p){reference[p.sensor_id].push_back(std::move(p));});
        measured.advanceTo(next,aperture,environment,[&](auto&& p){products[p.sensor_id].push_back(std::move(p));});
        const size_t centre=(3*9+4)*4;const auto& p=products[91].back();const auto& ref=reference[91].back();const auto time=(p.start_time+p.end_time)*.5;const auto a=aperture(91,time);
        const auto hit=raycastVoxelScene(*scene.sample(time),a.position,a.forward,3000);require(hit&&std::abs(p.rgba[centre+3]*1000-hit->distance)<.02,"Earth combined measurement range disagrees with historical terrain/target geometry");
        require(std::abs(p.rgba[centre+3]-ref.rgba[centre+3])<1e-6&&std::abs(p.rgba[centre+1]-ref.rgba[centre+1])<.01,"shared clouds changed geometric range or target temperature");
        if(ref.rgba[centre]>1e-5)minimumTransmission=std::min(minimumTransmission,double(p.rgba[centre]/ref.rgba[centre]));
        globe->setCloudPresentationEnabled(interactive);render();globe->setCloudPresentationEnabled(true);
        contacts+=scene.snapshot()->contacts.size();maxWakes=std::max(maxWakes,globe->sceneCloudColliders());++frames;refresh();
    };
    auto action=[&](const QString& text,auto callback){auto* a=tools->addAction(text);QObject::connect(a,&QAction::triggered,&window,[&,callback]{try{callback();render();refresh();}catch(const std::exception& e){status->setText(QString::fromUtf8(e.what()));}});};
    action(QStringLiteral("单步"),[&]{step();});action(QStringLiteral("高清/体素"),[&]{const auto s=scene.snapshot();scene.edit(json::array({{{"kind","terrain_mode"},{"object","earth-terrain"},{"mode",s->objects.at("earth-terrain").terrainMode=="hd"?"voxel":"hd"}}}),s->revision);});
    action(QStringLiteral("地形毁伤"),[&]{scene.schedule({{"kind","damage"},{"object","earth-terrain"},{"tick",scene.snapshot()->tick+1},{"point_m",{-20,0,0}},{"radius_m",12}});});
    action(QStringLiteral("目标毁伤"),[&]{const auto s=scene.snapshot();const auto p=occupiedCentre(s->objects.at("ground-MQ9"));scene.schedule({{"kind","damage"},{"object","ground-MQ9"},{"tick",s->tick+1},{"space","object"},{"point_m",{p.x,p.y,p.z}},{"radius_m",1.044718724489212}});});
    action(QStringLiteral("云开关"),[&]{view->setFeatureEnabled(CloudFeature::kId,!view->featureAs<CloudFeature>(CloudFeature::kId)->enabled());});
    action(QStringLiteral("地球轨道"),[&]{auto p=simcore::geo::initialCamera(initial->anchor,16000000.);camera->eye=p.eye;camera->center=p.center;camera->up=p.up;});
    action(QStringLiteral("工作区"),[&]{pose({180,130,240},{-20,90,0});});
    QTimer timer;bool playing=false;QObject::connect(&timer,&QTimer::timeout,&window,[&]{try{step();}catch(const std::exception& e){timer.stop();playing=false;status->setText(QString::fromUtf8(e.what()));}});
    action(QStringLiteral("运行/暂停"),[&]{playing=!playing;if(playing)timer.start(50);else timer.stop();});refresh();
    if(interactive){QEventLoop loop;QObject::connect(&window,&QMainWindow::destroyed,&loop,&QEventLoop::quit);QObject::connect(qApp,&QApplication::lastWindowClosed,&loop,&QEventLoop::quit);loop.exec();return;}
    pump(50);render();const auto pausedImage=capture("earth-hd-target-cloud");
    if(std::getenv("VSGRAYSIM_EARTH_SCENE_DIAGNOSTIC")) {
        view->setFeatureEnabled(CloudFeature::kId,false);capture("earth-hd-target-clear");
        std::cout<<"DIAGNOSTIC: Earth visual captures only; full integrated acceptance was not executed\n";view->hide();pump(20);return;
    }
    const auto pausedRevision=resources->field->revision();render();render();require(resources->field->revision()==pausedRevision,"native cloud evolved while the unified Earth clock was paused");
    const auto repeated=capture("earth-paused-repeat");require(repeated.size()==pausedImage.size(),"Earth paused viewport changed dimensions");
    size_t changed=0,sampled=0;
    for(int y=10;y<repeated.height()/3;++y)for(int x=10;x<repeated.width()-10;++x) {
        const auto a=pausedImage.pixel(x,y),b=repeated.pixel(x,y);++sampled;
        if(std::abs(qRed(a)-qRed(b))+std::abs(qGreen(a)-qGreen(b))+std::abs(qBlue(a)-qBlue(b))>3)++changed;
    }
    require(changed<=sampled/200,"paused native Earth cloud image flickers without physical evolution");
    for(int tick=1;tick<=50;++tick){step();if(tick==12)capture("earth-voxel-target-cloud");if(tick==24)capture("earth-crater-target-cloud");if(tick==40)capture("earth-hd-damaged-target-cloud");if(tick%10==0)std::cout<<"Earth integrated tick="<<tick<<" cloud="<<resources->field->revision()<<" wake_bodies="<<maxWakes<<" IR transmittance="<<minimumTransmission<<std::endl;}
    require(contacts>0&&maxWakes>0,"Earth scene failed to couple target contact and visible aircraft to cloud forcing");
    require(scene.snapshot()->objects.size()>initial->objects.size(),"imported OSGB target did not fracture in Earth scene");
    require(scene.snapshot()->journal[1]["removed_voxels"].get<size_t>()>0,"Earth terrain damage was not executed");
    require(minimumTransmission<.99,"native Earth cloud had no effect on the shared sensor path");
    require(scene.snapshot()->objects.at("earth-terrain").terrainMode=="hd"&&globe->weather()&&globe->weather()->clouds,"Earth context or physical cloud disappeared after terrain mode changes");
    const auto flight=scene.snapshot()->objects.at("flying-AH64");const auto point=flight.pose(scene.snapshot()->tick,initial->fixedStep)*flight.localCentreOfMass();pose(point+vsg::dvec3(-70,35,110),point);capture("earth-aircraft-in-cloud");
    scene.saveCheckpoint((directory/"earth-physical-checkpoint.json").string());
    json report={{"program","sim_earth_gui_tests"},{"scene",std::filesystem::absolute(sceneFile.toStdString()).string()},{"earth_context",std::filesystem::absolute(contextMap.toStdString()).string()},
        {"vulkan_device",std::string(view->context().window()->getOrCreateDevice()->getPhysicalDevice()->getProperties().deviceName)},
        {"source_dem_spacing_m",scene.snapshot()->objects.at("earth-terrain").provenance.at("dem_spacing_m")},
        {"source_imagery_spacing_m",scene.snapshot()->objects.at("earth-terrain").provenance.at("imagery_spacing_m")},
        {"tick",scene.snapshot()->tick},{"physical_frames",frames},{"contacts_observed",contacts},{"cloud_revision",resources->field->revision()},{"cloud_wake_bodies",maxWakes},{"minimum_ir_transmission",minimumTransmission},
        {"ir_frames",products[91].size()},{"tv_frames",products[92].size()},{"geometry_revision",scene.snapshot()->contentRevision},{"shared_cloud_image",true},
        {"paused_cloud_changed_pixels",changed},{"paused_cloud_compared_pixels",sampled},
        {"cloud_colour_draws","captured probe frames; physical cloud compute executes at every fixed step"},{"cloud_time_model","held between fixed boundaries; measurements drain before next cloud step"},
        {"cloud_wake_model","resolved bounding wake envelope; rigid contact and measurement geometry remain voxel-exact"},{"gpu_cloud_field_checkpointed",false}};
    std::ofstream(directory/"earth-scene-report.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: Earth program integrated real imagery/DEM, globe context, OSGB targets, terrain HD/voxel damage, rigid contacts, fractured targets, shared GPU cloud/aircraft wakes and sensor range/extinction\n";
    view->hide();pump(20);
}

void runEarthTerrainRegionCapture(const QString& output,bool highResolution) {
    require(QApplication::platformName()=="xcb","terrain region capture needs a native X11 surface");
    const auto directory=std::filesystem::absolute(output.toStdString());std::filesystem::create_directories(directory);
    const auto file=std::filesystem::absolute(highResolution?"config/earth/taipei-80km-hires.vscene.json":"config/earth/taipei-80km.vscene.json");
    QElapsedTimer importTimer;importTimer.start();
    auto prepared=VoxelScene::open(file.string());const auto initial=prepared->snapshot();const auto& terrain=initial->objects.at("taipei-terrain");
    const double importMs=importTimer.nsecsElapsed()/1e6;
    const auto chart=simcore::geo::makeEnuToWorldMatrix(initial->anchor);const auto box=terrain.earth->bounds();
    require(box.max.x-box.min.x==80000&&box.max.z-box.min.z==80000,"terrain working set must be exactly 80 by 80 km");
    auto smooth=prepared->checkpoint(),blocks=smooth;blocks["earth_patches"]["earth-patch-1"]["voxel_surface"]="blocks";
    auto baseline=highResolution?VoxelScene::open(std::filesystem::absolute("config/earth/taipei-80km-coarse.vscene.json").string()):nullptr;
    if(baseline)blocks=baseline->checkpoint();
    QMainWindow window;window.setFixedSize(1024,640);auto* view=new Viewport3DView(&window);window.setCentralWidget(view);
    bool ready=false;QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);config.remove(CloudFeature::kId);
    config.add(EarthView::kId,{{"map_json",std::filesystem::absolute(highResolution?"config/earth/taipei-coarse.map.json":"config/earth/taipei-region.map.json").string()},
        {"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m},{"scene_context",true},{"view_height",60000}});
    config.add(SkyFeature::kId,initial->utcOrigin);
    config.add(VoxelSceneFeature::kId,{{"checkpoint",blocks},{"paused",true},{"advance_from_frames",false},{"frame_scene",false},{"terrain_atmosphere",false}});
    view->applyConfig(config);window.show();QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<60000)pump(10);
    require(ready&&failure.isEmpty(),"terrain viewport failed to initialize");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);require(feature&&feature->runtime(),"terrain runtime missing");
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    const auto up4=chart*vsg::dvec4(0,1,0,0);const vsg::dvec3 up(up4.x,up4.y,up4.z);
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus,double fov){camera->eye=chart*eye;camera->center=chart*focus;camera->up=up;view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;};
    auto render=[&]{view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();QThread::msleep(21);view->context().setSimulationTime(0);QElapsedTimer t;t.start();view->renderFrame();view->context().viewer()->deviceWaitIdle();return t.nsecsElapsed()/1e6;};
    auto capture=[&](const std::string& name){render();QImage image;QElapsedTimer timeout;timeout.start();while(image.isNull()&&timeout.elapsed()<10000){pump(10);QThread::msleep(50);image=view->grabFrame();}require(!image.isNull(),"terrain readback failed");require(image.save(QString::fromStdString((directory/(name+".png")).string())),"terrain image save failed");};
    vsg::dvec3 peak(0,-1e9,0),gentle(0,0,0);double flat=1e9;
    const auto& surface=baseline?baseline->snapshot()->objects.at("taipei-terrain").surface():terrain.surface();
    for(size_t i=0;i<surface.vertices().size();++i) {
        const auto& p=surface.vertices()[i];if(std::abs(p.x)>28000||std::abs(p.z)>28000)continue;
        if(p.y>peak.y)peak=p;
        if(p.y>20&&p.y<500&&!surface.surface_normals.empty()&&surface.surface_normals[i].y>.98) {
            const double score=1.-surface.surface_normals[i].y+vsg::length(vsg::dvec3(p.x,0,p.z))*1e-8;
            if(score<flat){flat=score;gentle=p;}
        }
    }
    require(peak.y>500&&flat<1e8,"real DEM must contain both mountain relief and gently sloping land");
    struct Camera {const char* name;vsg::dvec3 eye,focus;double fov;};
    std::array<Camera,4> cameras{{
        {"overview",{60000,90000,85000},{0,300,0},55},
        {"ridge",peak+vsg::dvec3(2400,1700,3500),peak-vsg::dvec3(1800,250,1300),45},
        {"gentle",gentle+vsg::dvec3(1800,900,2500),gentle-vsg::dvec3(1800,100,1200),50},
        {"boundary",{-37000,6500,22000},{-40000,200,12000},48}
    }};
    json report={{"region","Taipei 80 x 80 km"},{"area_km2",6400},{"anchor",{{"lat",initial->anchor.lat_deg},{"lon",initial->anchor.lon_deg},{"alt",initial->anchor.alt_m}}},
        {"source",terrain.earth->provenance()},{"grid_cell_size_m",terrain.volume->resolution_m},{"terrain_quality",surfaceQuality(terrain.surface()).json()},{"cameras",json::array()},
        {"comparison",highResolution?"existing coarse DEM/RGB versus public GLO-30 DSM/Sentinel-2; same camera and UTC":"blocks versus smooth DEM"},{"source_import_ms",importMs}};
    for(const auto& c:cameras)report["cameras"].push_back({{"name",c.name},{"eye",{c.eye.x,c.eye.y,c.eye.z}},{"focus",{c.focus.x,c.focus.y,c.focus.z}},{"fov_y",c.fov}});
    auto probe=[&]{double maximum=0;auto& scene=*feature->runtime();for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x){const vsg::dvec3 p{12000.*x+73,15000,12000.*z+137};const auto a=scene.raycast(p,{0,-1,0},30000),b=scene.rigidRaycast(p,{0,-1,0},30000);require(bool(a)==bool(b),"regional DEM collider hit mask mismatch");if(a){require(a->object==b->object,"regional collider hit a different object");maximum=std::max(maximum,std::abs(a->distance-b->distance));}}require(maximum<.03,"regional DEM collision/range error exceeds 3 cm");return maximum;};
    for(const std::string phase:{"before","after"}) {
        if(phase=="after") {
            if(highResolution) {
                auto* map=view->featureAs<EarthViewFeature>(EarthView::kId);
                const auto path=std::filesystem::absolute("config/earth/taipei-hires.map.json").string();
                map->configure({{"map_json",path}});map->bindSurface(EarthDataService::loadMap(path,initial->anchor),false);
            }
            view->configureFeature(VoxelSceneFeature::kId,{{"checkpoint",smooth},{"paused",true},{"frame_scene",false}});
        }
        report[phase]["maximum_collision_range_error_m"]=probe();
        double demError=0;const auto& heights=(phase=="before"?blocks:smooth).at("earth_patches").at("earth-patch-1").at("heights_m");
        const int pitch=int(std::lround(std::sqrt(double(heights.size()))));
        for(int j=0;j<8;++j)for(int i=0;i<8;++i){const int x=(pitch-1)*(16+i*32)/256,z=(pitch-1)*(16+j*32)/256;const double step=80000./(pitch-1);const auto hit=feature->runtime()->raycast({-40000+x*step,15000,-40000+z*step},{0,-1,0},30000);require(bool(hit),"DEM node disappeared from the physical surface");demError=std::max(demError,std::abs(hit->position.y-heights.at(z*pitch+x).get<double>()));}
        const auto& current=feature->runtime()->snapshot()->objects.at("taipei-terrain");
        report[phase]["terrain_quality"]=surfaceQuality(current.surface()).json();
        report[phase]["atlas_size"]={current.surface().imagery_width,current.surface().imagery_height};
        report[phase]["height_grid_size"]=pitch;
        report[phase]["maximum_dem_node_error_m"]=demError;
        if(phase=="after")require(demError<.005,"smooth voxel terrain flattened or displaced a source DEM sample");
        require(!feature->runtime()->raycast({50000,15000,0},{0,-1,0},30000),"display-only context leaked into terrain physics");

        for(const auto& c:cameras){pose(c.eye,c.focus,c.fov);capture(phase+"-"+c.name);}
        pose(cameras[1].eye,cameras[1].focus,cameras[1].fov);for(int i=0;i<4;++i)render();std::vector<double> times;for(int i=0;i<12;++i)times.push_back(render());std::sort(times.begin(),times.end());
        report[phase]["render_median_ms"]=times[times.size()/2];report[phase]["render_p95_ms"]=times[size_t(.95*(times.size()-1))];
    }
    report["presentation"]="DEM inspection: terrain atmospheric veiling disabled; matching full-atmosphere ridge snapshot also provided";
    pose(cameras[1].eye,cameras[1].focus,cameras[1].fov);view->configureFeature(VoxelSceneFeature::kId,{{"terrain_atmosphere",true}});capture("after-ridge-atmosphere");view->configureFeature(VoxelSceneFeature::kId,{{"terrain_atmosphere",false}});
    // Same camera flight from mountain flank into gentler terrain; all positions
    // come from the real DEM, and interpolation introduces no synthetic relief.
    for(int i=0;i<24;++i){const double t=i/23.,u=t*t*(3-2*t);const auto& a=cameras[1];const auto& b=cameras[2];pose(a.eye*(1-u)+b.eye*u,a.focus*(1-u)+b.focus*u,a.fov*(1-u)+b.fov*u);capture(QString("flight-%1").arg(i,3,10,QChar('0')).toStdString());}
    auto& scene=*feature->runtime();const auto original=scene.snapshot();const auto h=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(bool(h),"regional destruction probe missing");
    scene.schedule({{"kind","damage"},{"object","taipei-terrain"},{"tick",1},{"space","object"},{"point_m",{peak.x,h->position.y,peak.z}},{"radius_m",1500}});
    QElapsedTimer damageTimer;damageTimer.start();scene.setPaused(false);scene.advanceTo(.02,1);scene.setPaused(true);const double damageMs=damageTimer.nsecsElapsed()/1e6;render();
    const auto crater=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(crater&&crater->position.y<h->position.y-100,"regional damage did not remove the actual DEM surface");
    pose(peak+vsg::dvec3(3500,5500,4500),peak-vsg::dvec3(0,400,0),45);capture("crater-voxel");
    scene.schedule({{"kind","terrain_mode"},{"object","taipei-terrain"},{"tick",2},{"mode","hd"}});scene.setPaused(false);scene.advanceTo(.04,1);scene.setPaused(true);render();
    const auto hd=scene.raycast({peak.x,15000,peak.z},{0,-1,0},30000);require(hd&&std::abs(hd->position.y-crater->position.y)<.01,"HD mode resurrected or moved damaged terrain");capture("crater-hd");
    report["damage"]={{"events",scene.snapshot()->journal},{"original_height_m",h->position.y},{"crater_height_m",crater->position.y},{"hd_height_m",hd->position.y},{"maximum_collision_range_error_m",probe()},{"advance_and_prepare_ms",damageMs}};
    scene.saveCheckpoint((directory/"terrain-checkpoint.json").string());std::ofstream(directory/"terrain-acceptance.json")<<report.dump(2)<<'\n';
    std::cout<<"PASS: native 80 km DEM/imagery, smooth voxel terrain, same cameras, collider/range probes and persistent crater across display modes"<<std::endl;
    window.hide();pump(20);
}

void runEarthUrbanCapture(const QString& baseline, const QString& district, const QString& output,
                         const QString& inspection,bool compareClusters) {
    namespace fs=std::filesystem;
    require(QApplication::platformName()=="xcb","urban capture requires a native Vulkan window");
    const auto directory=fs::absolute(output.toStdString());
    fs::create_directories(directory);
    const auto basePath=fs::absolute(baseline.toStdString()).string();
    const auto urbanPath=fs::absolute(district.toStdString()).string();
    const auto map=fs::absolute("config/earth/taipei-urban-context.map.json").string();
    json plan;
    if(!inspection.isEmpty()) {
        std::ifstream file(inspection.toStdString());
        require(bool(file),"street inspection file is missing");
        file>>plan;
    }
    const bool street=!plan.is_null();
    auto xyz=[](const json& point) {
        const auto p=point.get<std::array<double,3>>();
        return vsg::dvec3(p[0],p[1],p[2]);
    };
    auto source=VoxelScene::open(urbanPath);
    const auto built=source->snapshot();
    const auto before=compareClusters?built:VoxelScene::open(basePath)->snapshot();
    const auto chart=simcore::geo::makeEnuToWorldMatrix(built->anchor);
    require(chart==simcore::geo::makeEnuToWorldMatrix(before->anchor)&&before->utcOrigin==built->utcOrigin,
            "urban comparison requires identical anchor and UTC");
    std::string building;
    vsg::dvec3 roof;
    double score=0.;
    for(const auto& [id,object]:built->objects)if(object.provenance.contains("district_provenance")) {
        const auto& record=object.provenance.at("district_provenance");
        const auto p=record.at("roof_probe_m").get<std::array<double,3>>();
        const double area=record.at("area_m2");
        const double candidate=record.at("height_m").get<double>()*std::min(1.,area/1700.);
        if(candidate>score){score=candidate;building=id;roof={p[0],p[1],p[2]};}
    }
    require(!building.empty(),"urban scene has no compiled footprints");
    const auto ground=built->objects.at("district-ground").earth->bounds();
    const double span=ground.max.x-ground.min.x;
    const vsg::dvec3 center((ground.min.x+ground.max.x)*.5,35.,(ground.min.z+ground.max.z)*.5);
    const auto upper=chart*vsg::dvec4(0,1,0,0);

    QMainWindow window;
    window.setFixedSize(1024,640);
    auto* view=new Viewport3DView(&window);
    window.setCentralWidget(view);
    bool ready=false;
    QString failure;
    QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    auto config=ViewportConfig::fromPreset("earth_display");
    config.remove(OceanFeature::kId);config.remove(CloudFeature::kId);
    config.add(EarthView::kId,{{"map_json",map},{"lat",built->anchor.lat_deg},{"lon",built->anchor.lon_deg},
        {"alt",built->anchor.alt_m},{"scene_context",true},{"view_height",1500}});
    config.add(SkyFeature::kId,built->utcOrigin);
    config.add(VoxelSceneFeature::kId,{{"scene_json",basePath},{"paused",true},{"frame_scene",false},
                                     {"advance_from_frames",false},{"terrain_atmosphere",false}});
    if(compareClusters)config.add(VoxelSceneFeature::kId,{{"clustered_rendering",false}});
    view->applyConfig(config);window.show();
    QElapsedTimer deadline;deadline.start();
    while(!ready&&failure.isEmpty()&&deadline.elapsed()<60000)pump(10);
    require(ready&&failure.isEmpty(),"urban viewport failed to initialize");
    auto* feature=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);
    auto camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>();
    require(feature&&feature->runtime()&&camera,"urban native view is incomplete");
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus,double fov){
        camera->eye=chart*eye;camera->center=chart*focus;camera->up={upper.x,upper.y,upper.z};
        view->context().camera()->projectionMatrix.cast<vsg::Perspective>()->fieldOfViewY=fov;
    };
    auto render=[&] {
        view->setAutoFrame(false);view->setIdleSuspend(false);view->markActivity();
        view->context().setSimulationTime(feature->runtime()->snapshot()->timeSeconds());
        auto viewer=view->context().viewer();
        const auto first=view->widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer timeout;timeout.start();
        while(timeout.elapsed()<3000) {
            QThread::msleep(22);
            QElapsedTimer timer;timer.start();view->renderFrame();viewer->deviceWaitIdle();
            if(view->widget()->property("renderFrameSerial").toULongLong()>first)return timer.nsecsElapsed()/1e6;
        }
        throw std::runtime_error("urban test did not submit a Vulkan frame");
    };
    auto capture=[&](const std::string& name) {
        render();
        QImage image;QElapsedTimer timeout;timeout.start();
        while(image.isNull()&&timeout.elapsed()<10000){pump(10);QThread::msleep(35);image=view->grabFrame();}
        require(!image.isNull()&&image.save(QString::fromStdString((directory/(name+".png")).string())),"urban native readback failed");
        return image.convertToFormat(QImage::Format_RGB32);
    };
    auto stats=[](std::vector<double> samples) {
        std::sort(samples.begin(),samples.end());
        return json{{"samples",samples.size()},{"median",samples[samples.size()/2]},
                    {"p95",samples[size_t(std::ceil(samples.size()*.95))-1]}};
    };
    auto renderStatistics=[&] {
        std::vector<double> timings;
        for(int i=0;i<24;++i)timings.push_back(render());
        return stats(timings);
    };
    const auto nearEye=roof+vsg::dvec3(140,62,160),nearTarget=roof-vsg::dvec3(0,22,0);
    const auto highEye=center+vsg::dvec3(.55,.65,.85)*span;
    auto plannedPose=[&](const json& p){pose(xyz(p.at("eye_m")),xyz(p.at("target_m")),p.at("fov_y"));};
    json report={{"utc",built->utcOrigin},{"building_probe",building},{"objects",built->objects.size()},
                 {"span_m",span},{"capture", "1024x640 X11 Vulkan. Stable render includes device idle; excludes capture and forced inter-frame sleep"},
                 {"cameras",{{"overview_eye_m",{highEye.x,highEye.y,highEye.z}},{"overview_target_m",{center.x,center.y,center.z}},
                              {"detail_eye_m",{nearEye.x,nearEye.y,nearEye.z}},{"detail_target_m",{nearTarget.x,nearTarget.y,nearTarget.z}}}}};
    if(street)report["cameras"]=plan.at("cameras");
    if(compareClusters) {
        const auto& display=feature->display();
        require(display.contextDrawable.has_value(),"cluster comparison did not mount the surrounding Taipei terrain");
        const auto context=display.node->children.at(*display.contextDrawable).cast<vsg::StateGroup>();
        const auto draw=context->children.front().cast<vsg::VertexIndexDraw>();
        const auto vertices=draw->arrays.front()->data.cast<vsg::vec3Array>();
        require(vertices->size()<draw->indexCount,"the terrain context did not reuse surface vertices");
        report["terrain_context"]={{"vertices",vertices->size()},{"triangles",draw->indexCount/3},
                                   {"vertices_if_triangle_expanded",draw->indexCount}};
    }
    std::map<std::string,QImage> references;
    auto readClusters=[&] {
        const auto& clusters=feature->display().clusters;
        require(view->widget()->supportsClusteredDraws(),"cluster comparison Vulkan graphics queue lacks compute support");
        require(!feature->runtime()->snapshot()->ocean,"cluster comparison needs the land scene");
        require(bool(clusters),"cluster comparison did not activate GPU clustered rendering");
        GpuContext device;device.initialize(false,view->context().window()->getOrCreateDevice());
        uint64_t available=0,visible=0,triangles=0,lowerTriangles=0,selected=0,selectedLower=0;
        const auto batches=clusters->indirectDraws();
        for(const auto& draw:batches) {
            std::vector<VkDrawIndexedIndirectCommand> data(draw.count+1);
            device.readbackBatch({{nullptr,draw.buffer.get(),VK_IMAGE_LAYOUT_UNDEFINED,data.size()*sizeof(data.front()),data.data()}});
            available+=draw.count;triangles+=draw.sourceTriangles;lowerTriangles+=draw.lowerLodTriangles;
            uint64_t batchIndices=0;
            for(size_t i=1;i<data.size();++i) {
                const auto& command=data[i];
                require(command.indexCount%3==0&&command.instanceCount==1&&command.firstInstance==0&&
                        uint64_t(command.firstIndex)+command.indexCount<=3*(draw.sourceTriangles+draw.lowerLodTriangles),
                        "cluster GPU produced an invalid indirect draw");
                visible+=command.indexCount!=0;selected+=command.indexCount/3;
                selectedLower+=command.indexCount!=0&&command.firstIndex>=3*draw.sourceTriangles;
                batchIndices+=command.indexCount;
            }
            require(data.front().indexCount==batchIndices&&data.front().firstIndex==0&&data.front().instanceCount==1,
                    "cluster dispatch did not compact its selected indices into the combined draw");
        }
        require(triangles>0&&selected>0&&selected<triangles&&visible<available&&selectedLower>0&&lowerTriangles<triangles,
                "cluster compute did not cull visible-cut geometry or build a terrain LOD");
        return json{{"draw_batches",batches.size()},{"input_clusters",available},{"visible_clusters",visible},{"triangles_input",triangles},
                    {"triangles_selected",selected},{"lower_lod_visible_clusters",selectedLower},
                    {"triangles_with_all_lower_levels",lowerTriangles}};
    };
    for(const std::string phase:{"before","after"}) {
        if(phase=="after") {
            QElapsedTimer timer;timer.start();
            if(compareClusters)view->configureFeature(VoxelSceneFeature::kId,{{"clustered_rendering",true}});
            else view->configureFeature(VoxelSceneFeature::kId,{{"scene_json",urbanPath},{"paused",true},{"frame_scene",false}});
            require(feature->runtime()->snapshot()->objects.size()==built->objects.size(),"district package did not mount");
            report["district_load_and_viewport_prepare_ms"]=timer.nsecsElapsed()/1e6;
        }
        if(street) {
            for(const auto& camera:plan.at("cameras")) {
                plannedPose(camera);for(int i=0;i<4;++i)render();
                const auto name=camera.at("name").get<std::string>();
                const auto image=capture(phase+"-"+name);
                if(compareClusters&&phase=="before")references.emplace(name,image);
                if(compareClusters&&phase=="after") {
                    render();
                    auto result=readClusters();
                    const auto& reference=references.at(name);
                    require(reference.size()==image.size(),"cluster comparison changed output dimensions");
                    uint64_t difference=0;
                    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                        const QRgb a=reference.pixel(x,y),b=image.pixel(x,y);
                        difference+=abs(qRed(a)-qRed(b))+abs(qGreen(a)-qGreen(b))+abs(qBlue(a)-qBlue(b));
                    }
                    result["mean_rgb_error_8bit"]=double(difference)/(3.*image.width()*image.height());
                    require(result.at("mean_rgb_error_8bit").get<double>()<6.,"clustered display diverged visually from the canonical surface");
                    report["clustered_views"][name]=std::move(result);
                }
                if(compareClusters)report["stable_render_by_view_ms"][name][phase]=renderStatistics();
            }
            plannedPose(plan.at("cameras").front());
        } else {
            pose(highEye,center,42.);for(int i=0;i<4;++i)render();capture(phase+"-overview");
            pose(nearEye,nearTarget,46.);for(int i=0;i<4;++i)render();capture(phase+"-detail");
        }
        if(compareClusters)report[phase+"_stable_render_ms"]=report.at("stable_render_by_view_ms").at("street").at(phase);
        else report[phase+"_stable_render_ms"]=renderStatistics();
    }
    auto& scene=*feature->runtime();
    const auto initialProbe=street?xyz(plan.at("road_point_m")):roof+vsg::dvec3(.03,0,.03);
    const auto origin=initialProbe+vsg::dvec3(0,30,0);
    const vsg::dvec3 down(0,-1,0);
    auto probe=[&](const vsg::dvec3& origin) {
        const auto hit=scene.raycast(origin,down,600.),rigid=scene.rigidRaycast(origin,down,600.);
        require(hit&&rigid&&hit->object==rigid->object&&std::abs(hit->distance-rigid->distance)<.03,
                "urban authoritative surface and collision disagree");
        return *hit;
    };
    const auto surfaceBefore=probe(origin);
    const auto damageObject=street?plan.at("terrain_id").get<std::string>():building;
    require(surfaceBefore.object==damageObject&&std::abs(surfaceBefore.position.y-initialProbe.y)<.08,
            "urban geometry lost its authored surface height");
    size_t n=0;
    if(street) {
        auto groundHeight=[&](const json& value) {
            const auto point=xyz(value);
            const auto hit=probe(point+vsg::dvec3(0,20,0));++n;
            require(hit.object==damageObject&&std::abs(hit.position.y-point.y)<.09,
                    "road cross-section does not match the published voxel surface");
            return hit.position.y;
        };
        const double edge=groundHeight(plan.at("road_edge_m")),walk=groundHeight(plan.at("sidewalk_m"));
        const double start=groundHeight(plan.at("road_section_endpoints_m")[0]),end=groundHeight(plan.at("road_section_endpoints_m")[1]);
        require(walk-edge>.06&&walk-edge<.35,"the designed curb height is missing in collision geometry");
        report["road_section"]={{"edge_m",edge},{"walkway_m",walk},{"edge_to_walkway_rise_m",walk-edge},
                                {"route_start_m",start},{"route_end_m",end},
                                {"section_grade_ratio",plan.at("longitudinal_grade_ratio")}};
    }
    for(int z=-3;z<=3;++z)for(int x=-3;x<=3;++x) {
        probe(center+vsg::dvec3(x*span/8.+.31,240.,z*span/8.+.17));++n;
    }
    auto sensorData=makeVoxelSceneSensorData(scene.snapshot());
    for(const auto& instance:sensorData.instances) {
        const auto& drawing=feature->display().drawings.at(instance.id);
        const auto node=drawing.node.cast<vsg::StateGroup>();
        auto draw=node->children.front().cast<vsg::VertexIndexDraw>();
        const auto a=draw->arrays[0]->data.cast<vsg::vec3Array>(),b=instance.geometry->arrays[0]->data.cast<vsg::vec3Array>();
        const auto ai=draw->indices->data.cast<vsg::uintArray>(),bi=instance.geometry->indices->data.cast<vsg::uintArray>();
        require(a&&b&&a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin())&&ai&&bi&&ai->size()==bi->size()
                &&std::equal(ai->begin(),ai->end(),bi->begin()),"urban renderer and sensor geometry differ");
    }
    report["sensor_instances"]=sensorData.instances.size();
    report["sensor_texture_layers"]=sensorData.textures.layers.size();
    if(!sensorData.textures.layers.empty()) {
        const auto layer=sensorData.textures.layers.front().cast<vsg::ubvec4Array2D>();
        report["sensor_texture_bytes"]=size_t(layer->width())*layer->height()*4*sensorData.textures.layers.size();
    }
    report["collision_probes"]=n+1;
    sensorData={};
    QElapsedTimer timer;
    auto advance=[&]() {
        const auto state=scene.snapshot();
        timer.start();scene.setPaused(false);const auto step=scene.advanceTo((state->tick+1)*state->fixedStep,1);scene.setPaused(true);
        require(step.steps==1,"district simulation missed a fixed step");return timer.nsecsElapsed()/1e6;
    };
    std::vector<double> steps;
    for(int i=0;i<24;++i)steps.push_back(advance());
    report["stable_simulation_step_without_equipment_ms"]=stats(steps);
    GpuSensorSpec infrared;
    infrared.id=81;infrared.period_seconds=.02;infrared.output=ProductStage::Scene;
    infrared.profile.width=9;infrared.profile.height=7;infrared.profile.fov_deg=2.;
    infrared.profile.detector.integration_ms=1.;infrared.profile.band=BandSpec::make(BandType::MWIR);
    auto visible=infrared;visible.id=82;visible.profile.band=BandSpec::make(BandType::TV);
    timer.start();
    VoxelSensorSession measurements(scene,{infrared,visible},view->context().window()->getOrCreateDevice(),"urban-sensors");
    report["sensor_initial_prepare_ms"]=timer.nsecsElapsed()/1e6;
    auto aperture=[&](uint32_t,double){GpuAperture a;a.position=origin;a.forward=down;a.up={0,0,-1};return a;};
    auto environment=[](uint32_t,double,const BandSpec&){EnvAnswer e;e.sigma_per_km=0;e.sky_radiance=0;e.sun_irradiance=100;e.sun_el_deg=70;e.sun_az_deg=0;return e;};
    size_t irFrames=0,tvFrames=0;
    double maxGpuError=0.;
    auto measure=[&] {
        timer.start();
        measurements.advanceTo(scene.snapshot()->timeSeconds(),aperture,environment,[&](GpuMeasurementProduct&& product) {
            const size_t pixel=(3*9+4)*4;
            require(product.rgba.size()>pixel+3,"urban GPU product has no centre pixel");
            if(product.sensor_id==infrared.id) {
                const auto truth=scene.sample(.5*(product.start_time+product.end_time));
                const auto hit=raycastVoxelScene(*truth,origin,down,600.);
                require(hit&&product.rgba[pixel+1]>1,"urban GPU range lost the authored surface");
                maxGpuError=std::max(maxGpuError,std::abs(product.rgba[pixel+3]*1000-hit->distance));
                ++irFrames;
            } else {
                const double response=product.rgba[pixel]+product.rgba[pixel+1]+product.rgba[pixel+2];
                require(std::isfinite(response)&&response>0.,"urban GPU optical return is empty");
                ++tvFrames;
            }
        });
        return timer.nsecsElapsed()/1e6;
    };
    std::vector<double> complete;
    for(int i=0;i<12;++i) {const double simulationMs=advance();complete.push_back(simulationMs+measure());}
    report["stable_simulation_with_two_gpu_products_ms"]=stats(complete);
    const auto undamaged=scene.snapshot();
    if(street) {
        plannedPose(plan.at("crater_camera"));capture("before-damage");
    }
    scene.schedule({{"kind","damage"},{"object",damageObject},{"tick",undamaged->tick+1},{"space","scene"},
                    {"point_m",{surfaceBefore.position.x,surfaceBefore.position.y,surfaceBefore.position.z}},
                    {"radius_m",street?4.5:8.5}});
    const auto damageMs=advance();
    const auto surfaceAfter=probe(origin);
    require(surfaceAfter.position.y<surfaceBefore.position.y-2.,"urban voxel removal did not change the physical surface");
    report["damage"]={{"advance_publish_ms",damageMs},{"render_after_update_ms",render()},
                      {street?"road_before_m":"roof_before_m",surfaceBefore.position.y},
                      {street?"road_after_m":"roof_after_m",surfaceAfter.position.y}};
    measure();
    const auto previousFrames=irFrames;
    for(int i=0;i<3;++i) {advance();measure();}
    require(previousFrames>0&&irFrames>previousFrames&&tvFrames>0&&maxGpuError<.03,
            "urban GPU measurements failed after geometry publication");
    report["gpu_sensor"]={{"visible_frames",tvFrames},{"infrared_frames",irFrames},
                          {"maximum_range_error_m",maxGpuError}};
    capture("after-damage");
    const auto checkpoint=scene.checkpoint();
    auto reopened=VoxelScene::load(checkpoint);
    require(reopened->checkpoint()==checkpoint,"urban checkpoint failed damage round-trip");
    std::ofstream log(directory/(compareClusters?"cluster-acceptance.json":street?"street-acceptance.json":"urban-acceptance.json"));log<<report.dump(2)<<'\n';
    require(bool(log),"could not save urban acceptance report");
    std::cout<<"PASS: native Taipei district capture, fixed-step damage, collision and canonical sensor geometry\n";
    window.hide();pump(20);
}
