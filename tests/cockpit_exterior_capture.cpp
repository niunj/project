#include "cockpit_exterior_capture.h"
#include "display/AircraftCockpitPanel.h"
#include "display/ControlWorkbench.h"
#include "display/CockpitProfile.h"
#include "display/EarthPanel.h"
#include "display/FlightPanel.h"
#include "viewport3d/AircraftCockpitFeature.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VsgQtWidget.h"
#include "PlatformManager.h"
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <simcore/Orientation.h>
#include <vsg/all.h>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QSplitter>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace vsgraysim;
using namespace vsgraysim::gui::display;
using namespace vsgraysim::viewport3d;
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int milliseconds){QElapsedTimer t;t.start();do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}while(t.elapsed()<milliseconds);}
}
void nativeCockpitExteriors(const QString& output,const QString& map){
    require(QApplication::platformName()=="xcb","exterior acceptance requires native X11");require(QDir().mkpath(output),"cannot create exterior artifacts");
    auto& net=BrokerNet::instance();net.stop();BrokerNet::Config config;config.enabled=false;net.startSimRole(config);SimDataBroker::instance().serverOnline(true);net.setFlightControlAvailable(true);
    PhysicsEngine physics;physics.setGravity({0,-9.81,0});platform::PlatformManager manager;require(manager.initialize({}),"cannot initialize authoritative platform");manager.setPhysicsEngine(&physics);
    platform::MotionState initial;initial.position={0,1100,0};initial.velocity={0,0,-120};const auto id=manager.createPlatform("机外视图验证平台",platform::PlatformCategory::AIRCRAFT,{},initial);manager.onStart();double time=0;
    const json context={{"geo_anchor",{{"lat",25.078},{"lon",121.772},{"alt",364.}}},{"earth",{{"map",std::filesystem::absolute(map.toStdString()).string()}}},
        {"simulation",{{"epoch_utc","2026-11-18T04:00:00Z"}}},{"atmosphere",{{"cloud_cover",.55},{"cloud_base",1800.}}},{"ocean",{{"enabled",false}}}};
    auto publish=[&]{auto samples=manager.getAllPlatformsSnapshot();for(auto& sample:samples)sample["sim_time"]=time;net.setSimMeta("exterior_acceptance",time,20,false);net.setPlatformTelemetry(samples.dump());net.setEarthContext(context.dump());};publish();
    ControlWorkbench workbench("aircraft",{},nullptr,false);workbench.resize(1280,1000);AircraftCockpitPanel* panel=nullptr;
    for(auto* dock:workbench.findChildren<QDockWidget*>())if(auto* p=dynamic_cast<AircraftCockpitPanel*>(dock->widget()))panel=p;require(panel,"native console factory missing");
    workbench.show();auto refresh=[&]{const auto meta=net.lastSimMeta();panel->updateState(json::parse(meta.earth_json),json::parse(meta.platforms_json),time,true,false,meta.session_id,true,id);};refresh();
    for(auto* splitter:panel->findChildren<QSplitter*>())if(splitter->orientation()==Qt::Vertical)splitter->setSizes({400,400});
    auto* view=panel->earthDisplay()->viewport();require(view,"combined console did not create its exterior viewport");QString failure;
    QObject::connect(view->widget(),&gui::VsgQtWidget::initializationFailed,&workbench,[&](const QString& error){failure=error;});
    if(!view->isReady())view->configureFeature(EarthView::kId,{{"cloud_grid",{{"width",32},{"height",48},{"depth",32},{"pressure_iterations",8},{"cell_size_m",24}}}});
    // Exterior/pose acceptance keeps the existing cloud solver but omits its
    // expensive colour pass on software Vulkan. Normal product rendering does
    // not use this setting. Cloud visual acceptance remains a separate task.
    auto boundedProbe=[view]{view->setAutoFrame(false);if(auto* globe=view->featureAs<EarthViewFeature>(EarthView::kId))globe->setCloudPresentationEnabled(false);};
    QObject::connect(view,&Viewport3DView::vsgReady,&workbench,boundedProbe);if(view->isReady())boundedProbe();
    QElapsedTimer wait;wait.start();while(!view->isReady()&&failure.isEmpty()&&wait.elapsed()<45000)pump(10);require(failure.isEmpty(),failure.toStdString().c_str());require(view->isReady(),"combined console Vulkan initialization timed out");
    for(auto* timer:workbench.findChildren<QTimer*>())if(timer->interval()==50)timer->stop();view->setAutoFrame(false);
    auto* earth=view->featureAs<EarthViewFeature>(EarthView::kId);require(earth&&earth->surfaceSnapshot()&&earth->surfaceSnapshot()->valid_samples,"exterior camera did not retain authoritative Earth imagery/DEM");
    auto render=[&]{view->setAutoFrame(false);view->renderFrame();pump(100);view->setAutoFrame(false);};render();render();
    json results=json::array();
    for(const auto& path:CockpitProfile::builtInProfiles()){
        QString error;require(panel->loadCockpitProfile(path,error),"cannot select an exterior console profile");const auto profile=CockpitProfile::load(path);refresh();view->setAutoFrame(false);render();
        auto* feature=view->featureAs<AircraftCockpitFeature>(AircraftCockpitFeature::kId);require(feature&&feature->exteriorReady(),"3D aircraft exterior did not become visible");require(feature->representativeExterior()==(profile.id=="a320"),"aircraft model missing or representative exterior not disclosed");
        QImage image;QElapsedTimer capture;capture.start();while(image.isNull()&&capture.elapsed()<30000){pump(50);view->setAutoFrame(false);QThread::msleep(40);image=view->grabFrame();}image=image.convertToFormat(QImage::Format_RGB32);
        if(image.isNull()||image.width()<320||image.height()<180)std::cerr<<"Exterior capture "<<profile.id.toStdString()<<" image="<<image.width()<<'x'<<image.height()<<" viewport="<<view->width()<<'x'<<view->height()<<" visible="<<view->isVisible()<<" widgetVisible="<<view->widget()->isVisible()<<" auto="<<view->autoFrame()<<" ready="<<view->isReady()<<" status="<<panel->earthDisplay()->findChild<QLabel*>("earth_context_status")->text().toStdString()<<std::endl;
        require(!image.isNull()&&image.width()>=320&&image.height()>=180,"empty exterior framebuffer");require(image.save(output+"/"+profile.id+"-exterior.png"),"cannot save exterior framebuffer");
        auto composite=workbench.grab().toImage();{QPainter painter(&composite);painter.drawImage(QRect(view->mapTo(&workbench,QPoint(0,0)),view->size()),image);}require(composite.save(output+"/"+profile.id+"-console-exterior.png"),"cannot save combined cockpit/exterior image");
        const auto state=panel->controls()->selectedTelemetry();const auto expected=AircraftCockpitFeature::chaseCamera(earth->anchor(),state,profile.chaseDistance,profile.chaseAzimuth,0);const auto camera=view->camera()->viewMatrix.cast<vsg::LookAt>();
        require(camera&&vsg::length(camera->eye-expected.eye)<1e-5&&vsg::length(camera->center-expected.center)<1e-5,"exterior camera does not match published platform truth");
        const auto& p=state["position"];const auto matrix=simcore::geo::makeEnuToWorldMatrix(earth->anchor())*vsg::translate(p[0].get<double>(),p[1].get<double>(),p[2].get<double>())*vsg::rotate(simcore::headingRotation(state["yaw"].get<double>(),state["pitch"].get<double>(),state["roll"].get<double>()));
        const auto drawn=feature->displayPoseMatrix();for(int c=0;c<4;++c)for(int r=0;r<4;++r)require(std::abs(drawn[c][r]-matrix[c][r])<1e-7,"drawn aircraft matrix diverged from authoritative pose");
        const auto held=manager.getPlatformSnapshot(id);const auto compiled=view->widget()->property("graphCompilationCount").toULongLong();render();const auto after=manager.getPlatformSnapshot(id);for(const auto* key:{"position","velocity","yaw","pitch","roll"})require(held[key]==after[key],"exterior rendering integrated a second flight model");
        require(compiled==view->widget()->property("graphCompilationCount").toULongLong(),"steady exterior view recompiled the complete scene");
        results.push_back({{"profile",profile.id.toStdString()},{"real_asset",!feature->representativeExterior()},{"camera_matches_truth",true},{"display_matrix_matches_truth",true},{"paused_render_preserved_physics",true},{"steady_graph_recompiles",0},{"frame_width",image.width()},{"frame_height",image.height()}});
        std::cout<<"PASS: native exterior "<<profile.id.toStdString()<<" / authoritative Earth + pose + console"<<std::endl;
    }
    json report={{"workspace","actual ControlWorkbench aircraft / combined textured console and external follow"},{"profiles",results},{"software_acceptance_cloud_grid",{32,48,32}},{"cloud_colour_drawn",false},{"cloud_visual_acceptance",false},{"earth_source",std::filesystem::absolute(map.toStdString()).string()},{"airliner_representation","generic twin-engine exterior; not a supplied A320 mesh"},{"voxel_architecture_changed",false}};
    std::ofstream(output.toStdString()+"/exterior-acceptance.json")<<report.dump(2)<<'\n';workbench.hide();manager.shutdown();
}
