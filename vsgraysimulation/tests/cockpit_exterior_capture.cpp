#include "cockpit_exterior_capture.h"
#include "display/AircraftCockpitPanel.h"
#include "display/ControlWorkbench.h"
#include "display/CockpitProfile.h"
#include "display/EarthPanel.h"
#include "display/FlightPanel.h"
#include "display/PlatformInstruments.h"
#include "viewport3d/AircraftCockpitFeature.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VsgQtWidget.h"
#include "viewport3d/VoxelSceneFeature.h"
#include <vsgraysim/earth/VoxelEquipmentRuntime.h>
#include <QMouseEvent>
#include "PlatformManager.h"
#include <vsgraysim/broker/BrokerNet.h>
#include <vsgraysim/layer0_phys/PhysicsEngine.h>
#include <simcore/Orientation.h>
#include <vsg/maths/transform.h>
#include <vsg/all.h>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QSplitter>
#include <algorithm>
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
    auto* modes=panel->findChild<QComboBox*>("cockpit_view_mode");require(modes,"platform view switch missing");
    auto* cameraModes=panel->findChild<QComboBox*>("cockpit_camera_mode");require(cameraModes,"platform camera switch missing");
    require(cameraModes->currentData()=="pilot","3D mode must default to the pilot eye");
    cameraModes->setCurrentIndex(cameraModes->findData("external"));modes->setCurrentIndex(modes->findData("hud"));
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
    QString error;require(panel->loadCockpitProfile("config/cockpits/f16c.json",error),"cannot restore F-16C HUD preset");
    refresh();render();
    auto* engineFx=view->featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId);
    require(engineFx&&engineFx->particleMode()&&engineFx->visibleParticles()==0,"clean F-16 emitted a luminous plume at idle");
    const auto nominal=net.lastSimMeta().platforms_json;
    auto fullThrust=json::parse(nominal);
    for(auto& sample:fullThrust)sample["flight_control"]["actual"]["throttle"]=.98;
    net.setPlatformTelemetry(fullThrust.dump());refresh();render();render();
    require(engineFx->visibleJetFlames()==1&&engineFx->smokeHistoryCount()>0,"jet flame and smoke did not follow actual throttle telemetry");
    require(view->grabFrame().save(output+"/f16c-exterior-full-thrust.png"),"could not capture exhaust on the live cockpit viewport");
    net.setPlatformTelemetry(nominal);refresh();render();
    require(engineFx->visibleJetFlames()==0,"external jet flame lingered after thrust reduced");
    cameraModes->setCurrentIndex(cameraModes->findData("pilot"));refresh();
    auto* feature=view->featureAs<AircraftCockpitFeature>(AircraftCockpitFeature::kId);
    auto* toggle=panel->findChild<QToolButton*>("cockpit_hud");require(toggle&&toggle->isVisible(),"HUD control missing from the 3D mode");
    toggle->setChecked(false);render();const auto clear=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    toggle->setChecked(true);render();const auto hud=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    require(view->widget()->imGuiReady()&&!hud.isNull()&&clear.size()==hud.size(),"Vulkan ImGui HUD frame unavailable");
    require(feature&&feature->spatialPanelActive()&&!feature->exteriorReady(),"first-person view did not mount the spatial cockpit");
    auto checkPilot=[&](const CockpitProfile& profile,double yaw=0,double pitch=0){
        const auto actual=view->camera()->viewMatrix.cast<vsg::LookAt>();
        const auto spec=cockpitInterior(profile.category.toStdString());
        const auto expected=AircraftCockpitFeature::pilotCamera(earth->anchor(),panel->controls()->selectedTelemetry(),yaw,pitch,spec.eye);
        require(actual&&vsg::length(actual->eye-expected.eye)<1e-5&&vsg::length(actual->center-expected.center)<1e-5,
                "cockpit camera or head turn does not match the pilot pose");
        require(feature->photoPanelActive()&&feature->spatialPanelActive()==(profile.id=="f16c"),"incorrect cockpit presentation after profile switch");
    };
    const auto f16=CockpitProfile::load("config/cockpits/f16c.json");checkPilot(f16);
    int differences=0;for(int y=0;y<hud.height();++y)for(int x=0;x<hud.width();++x)if(hud.pixel(x,y)!=clear.pixel(x,y))++differences;
    require(differences>100&&differences<hud.width()*hud.height()/3,"HUD toggle did not change only the overlay region");
    require(hud.save(output+"/f16c-pilot-hud.png")&&clear.save(output+"/f16c-pilot-clear.png"),"cannot write native pilot comparison");
    toggle->setChecked(false);
    const auto beforeTelemetry=net.lastSimMeta().platforms_json;
    auto updated=json::parse(beforeTelemetry);
    for(auto& sample:updated)sample["velocity"][2]=-160.;
    net.setPlatformTelemetry(updated.dump());refresh();render();
    const auto faster=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    require(!faster.isNull()&&faster.size()==clear.size(),"no updated cockpit image");
    const auto& mfd=f16.instruments.front().rect;
    const auto perspective=view->camera()->projectionMatrix.cast<vsg::Perspective>();
    const auto panelPixel=[&](float u,float v) {
        const auto p=cockpitPanelPoint(u,v);
        const double focal=clear.height()*.5/std::tan(vsg::radians(perspective->fieldOfViewY*.5));
        return QPoint(int(clear.width()*.5-p.x*focal/p.z),int(clear.height()*.5+p.y*focal/p.z));
    };
    const auto mfdArea=QRect(panelPixel(mfd.left(),mfd.top()),panelPixel(mfd.right(),mfd.bottom()))
        .normalized().intersected(clear.rect());
    int liveInstrumentPixels=0;
    for(int y=mfdArea.top();y<=mfdArea.bottom();++y)for(int x=mfdArea.left();x<=mfdArea.right();++x)
        if(faster.pixel(x,y)!=clear.pixel(x,y))++liveInstrumentPixels;
    require(liveInstrumentPixels>12,"changing broker speed did not update the ImGui panel instrument with HUD off");
    net.setPlatformTelemetry(beforeTelemetry);refresh();toggle->setChecked(true);render();
    require(faster.save(output+"/f16c-pilot-live-instrument.png"),"cannot save instrument telemetry evidence");
    const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){
        QMouseEvent event(type,p,view->widget()->mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(view->widget(),&event);
    };
    mouse(QEvent::MouseButtonPress,{70,70},Qt::RightButton,Qt::RightButton);
    mouse(QEvent::MouseMove,{594,133},Qt::NoButton,Qt::RightButton);
    mouse(QEvent::MouseButtonRelease,{594,133},Qt::RightButton,Qt::NoButton);
    render();checkPilot(f16,83.84,-10.08);
    const auto side=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    toggle->setChecked(false);render();const auto sideClear=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    require(!side.isNull()&&side.size()==sideClear.size(),"no side windscreen frame");
    require(side==sideClear,"HUD remained on screen while looking away from its combiner");
    require(side.save(output+"/f16c-pilot-look-right.png"),"cannot capture head turn");
    auto* reset=panel->findChild<QToolButton*>("cockpit_reset_view");require(reset,"head reset control missing");reset->click();
    toggle->setChecked(true);render();checkPilot(f16);
    const auto compiled=view->widget()->property("graphCompilationCount").toULongLong();
    render();require(compiled==view->widget()->property("graphCompilationCount").toULongLong(),"steady cockpit recompiled the world");
    // Same UTC and seat: look down to verify that the lower controls occupy
    // their own volume instead of moving as a single screen-space layer.
    mouse(QEvent::MouseButtonPress,{70,70},Qt::RightButton,Qt::RightButton);
    mouse(QEvent::MouseMove,{70,193},Qt::NoButton,Qt::RightButton);
    mouse(QEvent::MouseButtonRelease,{70,193},Qt::RightButton,Qt::NoButton);
    render();checkPilot(f16,0,-19.68);
    require(view->grabFrame().save(output+"/f16c-pilot-look-down.png"),"cannot capture instrument close-up");
    reset->click();render();checkPilot(f16);

    auto nightContext=context;
    nightContext["simulation"]["epoch_utc"]="2026-11-18T18:00:00Z";
    net.setEarthContext(nightContext.dump());refresh();render();render();checkPilot(f16);
    const auto night=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    require(night.size()==hud.size()&&!night.isNull(),"night cockpit framebuffer unavailable");
    const auto metal=panelPixel(.41f,.27f); // unlit paint, outside the displays
    require(hud.rect().contains(metal)&&qGray(night.pixel(metal))+8<qGray(hud.pixel(metal)),
            "cabin paint did not darken with the scene at night");
    require(night.save(output+"/f16c-pilot-night.png"),"cannot save night cockpit");
    const auto click=[&](CabinAction action,double yaw=0.,double pitch=0.) {
        const auto controls=fighterCabinControls();
        const auto item=std::find_if(controls.begin(),controls.end(),[&](const auto& c){return c.action==action;});
        require(item!=controls.end(),"unknown cockpit control in acceptance");
        const auto p=vsg::inverse(vsg::rotate(simcore::headingRotation(yaw,pitch)))*vsg::dvec3(item->center);
        const double w=view->widget()->width(),h=view->widget()->height();
        const double f=h*.5/std::tan(vsg::radians(perspective->fieldOfViewY*.5));
        const QPointF screen(w*.5-p.x*f/p.z,h*.5+p.y*f/p.z);
        if(feature->cabinPick(screen.x(),screen.y(),w,h)!=action)
            std::cerr<<"cabin pick failed: "<<item->label<<" at "<<screen.x()<<','<<screen.y()<<std::endl;
        require(feature->cabinPick(screen.x(),screen.y(),w,h)==action,"projected 3D switch could not be picked");
        mouse(QEvent::MouseButtonPress,screen,Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,screen,Qt::LeftButton,Qt::NoButton);
        QEvent leave(QEvent::Leave);QApplication::sendEvent(view->widget(),&leave);
        render();
    };
    click(CabinAction::PanelLight);
    const auto lit=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    require(lit.size()==night.size()&&qGray(lit.pixel(metal))>qGray(night.pixel(metal))+1,"lighting knob did not illuminate the cockpit at night");
    require(lit.save(output+"/f16c-pilot-night-lamp.png"),"cannot save cabin light comparison");
    click(CabinAction::PanelLight);click(CabinAction::PanelLight); // restore the default dim detent
    net.setEarthContext(context.dump());refresh();render();render();

    click(CabinAction::Hud);require(!toggle->isChecked(),"HUD cockpit toggle did not change the existing HUD control");
    click(CabinAction::Hud);require(toggle->isChecked(),"HUD toggle did not restore the HUD");
    click(CabinAction::LeftControl);
    const auto switched=view->grabFrame().convertToFormat(QImage::Format_RGB32);
    int pagePixels=0;
    for(int y=mfdArea.top();y<=mfdArea.bottom();++y)for(int x=mfdArea.left();x<=mfdArea.right();++x)
        if(switched.pixel(x,y)!=hud.pixel(x,y))++pagePixels;
    require(pagePixels>200,"MFD soft key did not change the rendered instrument page");
    click(CabinAction::LeftFlight);
    click(CabinAction::FlightHold);click(CabinAction::LandingGear);
    FlightControlCommand pending;
    require(!net.pollFlightControl(&pending),"3D flight switch bypassed control ownership");
    auto* take=panel->findChild<QPushButton*>("flight_take_control");
    require(take&&take->isEnabled(),"flight control is not available in acceptance");
    take->click();
    const auto install=[&] {
        std::string mode;
        FlightControlCommand c;
        while(net.pollFlightControl(&c)) {
            auto result=manager.applyFlightControl(c,time);net.completeFlightControl(result);
            if(result.stage!="applied")throw std::runtime_error("simulator refused cockpit flight mode: "+result.code+" ("+result.message+")");
            mode=c.mode;
        }
        auto state=manager.getAllPlatformsSnapshot();for(auto& object:state)object["sim_time"]=time;
        net.setPlatformTelemetry(state.dump());refresh();render();
        return mode;
    };
    require(install()=="manual","cockpit could not acquire control");
    click(CabinAction::FlightHold);require(install()=="altitude_hold","3D switch did not engage existing flight hold");
    require(manager.getPlatformSnapshot(id).at("flight_control").at("mode")=="altitude_hold","server did not publish hold mode");
    click(CabinAction::FlightHold);require(install()=="manual","3D switch did not return to manual");
    // Drive the actual flight model, rather than moving the stick by test-only
    // UI state. Held pose screenshots above remain at the reference UTC/seat.
    const auto neutralStick=feature->cabinStickTransform();
    require(panel->controls()->applyManualInputs(.65,.32,-.38,.20)&&install()=="manual",
            "could not send actuator demand through the flight lease");
    for(int k=0;k<10;++k) {
        TimeTick tick;tick.dt=.05;tick.sim_time=time+.05;
        manager.onPreTick(tick);physics.step(tick.dt);manager.onPostTick(tick);time=tick.sim_time;
    }
    install();
    const auto deflected=feature->cabinStickTransform();
    const auto actualAxes=manager.getPlatformSnapshot(id).at("flight_control").at("actual");
    require(neutralStick&&deflected&&std::abs(actualAxes.at("aileron").get<double>())>.1&&
        vsg::length((*neutralStick)[0]-(*deflected)[0])>.015,"3D sidestick did not follow applied actuator telemetry");
    // Fly the switches through the original Qt event filter and broker into
    // PlatformManager, then require a revision and read back the real output.
    auto systems=[&]{return manager.getPlatformSnapshot(id).at("flight_control").at("systems");};
    const auto commandGear=[&](CabinAction action,const char* key,bool expected) {
        const auto revision=systems().at("revision").get<uint64_t>();
        click(action);install();pump(140);
        require(systems().at(key)==expected&&systems().at("revision").get<uint64_t>()==revision+1,
                "aircraft switch value or revision did not reach authoritative telemetry");
        require(panel->controls()->commandStatusText().contains(QStringLiteral("仿真已确认")),
                "display did not show confirmed system telemetry");
    };
    const auto flightBeforeSystems=manager.getPlatformSnapshot(id).at("flight_control").at("command");
    commandGear(CabinAction::NavLights,"nav_lights",true);
    commandGear(CabinAction::LandingLight,"landing_light",true);
    require(systems().at("landing_light_active")==false,"landing lamp is powered while gear is stowed");
    commandGear(CabinAction::LandingGear,"gear_down",true);
    require(systems().at("gear_extension")==0.,"paused gear advanced without simulation stepping");
    for(int k=0;k<61;++k) {
        TimeTick tick;tick.dt=.05;tick.sim_time=time+.05;
        manager.onPreTick(tick);physics.step(tick.dt);manager.onPostTick(tick);time=tick.sim_time;
        if(k==11)require(systems().at("gear_extension").get<double>()>0.&&
                           systems().at("gear_extension").get<double>()<.5,"gear travel was not time based");
    }
    require(systems().at("gear_extension")==1.&&systems().at("landing_light_active")==true,
            "gear did not lock down and enable landing light");
    require(manager.getPlatformSnapshot(id).at("flight_control").at("command")==flightBeforeSystems,
            "system switch overwrote flight setpoints");
    install();
    require(view->grabFrame().save(output+"/f16c-gear-locked.png"),"cannot save systems readback");
    commandGear(CabinAction::LandingGear,"gear_down",false);
    require(systems().at("landing_light_active")==false,"landing lamp did not cut out on gear up");
    commandGear(CabinAction::NavLights,"nav_lights",false);
    commandGear(CabinAction::LandingLight,"landing_light",false);
    time=0; // Keep downstream paused scene probes at their original reference time.
    panel->releaseControl("test release");install();
    net.setPlatformTelemetry(beforeTelemetry);refresh();render();
    const auto cockpitTriangles=feature->cabinTriangles();
    require(cockpitTriangles>4500,"detailed cockpit mesh was not active");

    for(const auto& path:CockpitProfile::builtInProfiles()) {
        const auto profile=CockpitProfile::load(path);
        if(profile.id=="f16c")continue;
        require(panel->loadCockpitProfile(path,error),"could not switch first-person cockpit");refresh();render();checkPilot(profile);
        require(view->grabFrame().save(output+"/"+profile.id+"-first-person.png"),"could not capture another first-person view");
    }
    require(panel->loadCockpitProfile("config/cockpits/f16c.json",error),"cannot restore F-16C cockpit");refresh();render();
    {
        Viewport3DView auxiliary;
        auxiliary.resize(640,380);auxiliary.show();
        QElapsedTimer ready;ready.start();while(!auxiliary.isReady()&&ready.elapsed()<15000)pump(20);
        require(auxiliary.isReady()&&auxiliary.widget()->imGuiReady(),"second viewport could not initialize its ImGui context");
        auxiliary.setAutoFrame(false);auxiliary.renderFrame();render();
    }
    render();require(!view->grabFrame().isNull(),"closing a second viewport broke the cockpit HUD renderer");
    // Use the production voxel adapter to derive ownship identity, then verify
    // that hiding a pilot's exterior leaves the shared simulation snapshot intact.
    const auto ownship=earth::VoxelScene::load({{"format","voxel-scene-v1"},
        {"geo_anchor",{{"lat",25.08},{"lon",121.78},{"alt",364.}}},
        {"objects",json::array({{{"id","ownship"},{"position_m",{-4,1100,-4}},
                                  {"source",{{"type","box"},{"dimensions",{8,8,8}},{"cell_size_m",1}}}},
                                {{"id","other"},{"position_m",{30,1100,-40}},
                                  {"source",{{"type","box"},{"dimensions",{8,8,8}},{"cell_size_m",1}}}}})}});
    earth::VoxelEquipmentConfig mounted;mounted.platforms.push_back({id,"ownship","Ownship","aircraft",0});
    const auto truth=ownship->snapshot();const auto voxelTelemetry=earth::voxelEquipmentPlatformTelemetry(mounted,truth);
    require(voxelTelemetry.size()==1&&voxelTelemetry.front().value("scene_object","")=="ownship","voxel telemetry lost the platform object ID");
    require(view->addFeature(VoxelSceneFeature::kId,{{"external_only",true}}),"could not add viewport-only voxel snapshot consumer");
    auto* voxel=view->featureAs<VoxelSceneFeature>(VoxelSceneFeature::kId);require(voxel,"missing voxel viewport feature");
    voxel->bindSnapshot(truth);
    net.setPlatformTelemetry(voxelTelemetry.dump());refresh();
    feature->configure({{"scene_exterior",true}});render();
    const auto& display=voxel->display();const auto actual=view->camera()->viewMatrix.cast<vsg::LookAt>();
    const auto expected=AircraftCockpitFeature::pilotCamera(truth->anchor,voxelTelemetry.front());
    const double eyeError=actual?vsg::length(actual->eye-expected.eye):-1;
    if(eyeError<0||eyeError>=1e-5)std::cerr<<"voxel pilot eye error (m): "<<eyeError
        <<"; ownship hidden="<<voxel->cockpitOwnshipHidden()<<"; selected="<<panel->controls()->selectedTelemetry().value("scene_object","")<<std::endl;
    require(actual&&eyeError<1e-5,"voxel cockpit used the basemap chart instead of the platform chart");
    require(voxel->cockpitOwnshipHidden()&&voxel->snapshot()==truth&&truth->objects.size()==2,
            "ownship view cull did not preserve the snapshot and other object");
    const auto placementInRoot=[&](const char* key){const auto& children=display.node->children;
        return std::find(children.begin(),children.end(),display.placements.at(key))!=children.end();};
    require(!placementInRoot("ownship")&&placementInRoot("other"),"cockpit masked a non-ownship target");
    cameraModes->setCurrentIndex(cameraModes->findData("external"));render();
    require(!voxel->cockpitOwnshipHidden()&&placementInRoot("ownship")&&!feature->interiorVisible()&&!feature->photoPanelActive(),"chase view failed to restore the voxel exterior");
    view->removeFeature(VoxelSceneFeature::kId);
    modes->setCurrentIndex(modes->findData("panel"));require(panel->instrumentPanel()->isVisible()&&!panel->earthDisplay()->isVisible(),"could not restore Qt mode after native HUD");
    json report={{"workspace","actual ControlWorkbench platform / Qt instruments and Vulkan ImGui HUD"},{"profiles",results},{"pilot_hud_changed_pixels",differences},{"spatial_panel_and_pilot_eye_verified",true},{ "cockpit_triangles",cockpitTriangles},{"mfd_page_pixels",pagePixels},{"hud_and_panel_light_clicks_verified",true},{"server_hold_and_manual_verified",true},{"gear_pause_transit_and_lock_verified",true},{"position_and_landing_light_switches_verified",true},{"authoritative_systems_revision",systems().at("revision")},{"sidestick_follows_actuator_telemetry",true},{"cabin_paint_luma_day",qGray(hud.pixel(metal))},{"cabin_paint_luma_night",qGray(night.pixel(metal))},{"live_panel_instrument_changed_pixels",liveInstrumentPixels},{"hud_does_not_follow_head_away_from_combiner",true},{"authoritative_voxel_ownship_cull_is_local",true},{"two_viewports_survived_one_closing",true},{"software_acceptance_cloud_grid",{32,48,32}},{"cloud_colour_drawn",false},{"cloud_visual_acceptance",false},{"earth_source",std::filesystem::absolute(map.toStdString()).string()},{"airliner_representation","generic twin-engine exterior; not a supplied A320 mesh"},{"throttle_exhaust_verified",true},{"voxel_architecture_changed",false}};
    std::ofstream(output.toStdString()+"/exterior-acceptance.json")<<report.dump(2)<<'\n';workbench.hide();manager.shutdown();
}
