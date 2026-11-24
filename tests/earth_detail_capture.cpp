#include "earth_detail_capture.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/VsgQtWidget.h"
#include <vsgraysim/earth/SurfaceQuality.h>
#include <simcore/GeoNavigation.h>
#include <QApplication>
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <vsg/all.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace vsgraysim::viewport3d;
using namespace vsgraysim::earth;
using json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(int ms){QElapsedTimer t;t.start();do{QApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}while(t.elapsed()<ms);}
json imageStatistics(const QImage& source){auto image=source.convertToFormat(QImage::Format_RGB32);double sum=0,sum2=0;int lo=255,hi=0;size_t n=0;for(int y=0;y<image.height();y+=2)for(int x=0;x<image.width();x+=2){int v=qGray(image.pixel(x,y));sum+=v;sum2+=v*v;lo=std::min(lo,v);hi=std::max(hi,v);++n;}return {{"width",image.width()},{"height",image.height()},{"luminance_min",lo},{"luminance_max",hi},{"luminance_mean",sum/n},{"luminance_stddev",std::sqrt(std::max(0.,sum2/n-std::pow(sum/n,2)))}};}

constexpr float cloudEdgeThreshold=.001f;
constexpr float thinCloudLimit=.10f;
constexpr double windTick=.125;
constexpr int windHoldPose=36,windHoldFrames=16;

struct CloudEdgeSection {
    double entry,exit,y,z;
    float peak;
    double waterColumn;
    double midpoint() const { return .5*(entry+exit); }
};

template<class Density,class Score>
std::optional<CloudEdgeSection> findCloudEdge(const Density& density,double y,double z,
                                             double halfWindow,double centre,const Score& score) {
    const double step=rockyenv::planetCloudVoxelLayout().nearVoxelMetres*.25;
    const double start=centre-halfWindow,end=centre+halfWindow;
    std::optional<CloudEdgeSection> best;
    double bestScore=std::numeric_limits<double>::infinity();
    double entry=start;
    double column=0.;
    float previous=density({start,y,z}),peak=previous;
    for(double x=start+step;x<=end;x+=step) {
        const float water=density({x,y,z});
        if(previous<=cloudEdgeThreshold && water>cloudEdgeThreshold) {
            entry=x-step+step*(cloudEdgeThreshold-previous)/(water-previous);
            peak=water;
            column=.5*(water+cloudEdgeThreshold)*(x-entry);
        } else if(previous>cloudEdgeThreshold && water<=cloudEdgeThreshold) {
            const double exit=x-step+step*(previous-cloudEdgeThreshold)/(previous-water);
            const double span=exit-entry;
            column+=.5*(previous+cloudEdgeThreshold)*(exit-(x-step));
            if(entry>start && span>=56. && span<=530. && peak>=.012f && peak<=.20f) {
                const CloudEdgeSection section{entry,exit,y,z,peak,column};
                const double rating=score(section);
                if(rating<bestScore){best=section;bestScore=rating;}
            }
        } else if(water>cloudEdgeThreshold) {
            column+=.5*(previous+water)*step;
        }
        peak=std::max(peak,water);
        previous=water;
    }
    return best;
}

double changedLuminanceFraction(const QImage& first,const QImage& last) {
    require(!first.isNull() && first.size()==last.size(),"invalid fixed camera probe images");
    size_t changed=0,total=0;
    for(int y=0;y<first.height();y+=2)for(int x=0;x<first.width();x+=2) {
        changed+=std::abs(qGray(first.pixel(x,y))-qGray(last.pixel(x,y)))>=5;
        ++total;
    }
    return double(changed)/total;
}
}
void runEarthDetailCapture(const QString& output,bool interactive,EarthDetailRoute route,
                           CloudRegionWeather regionalWeather,const QString& routeFile){
    require(QApplication::platformName()=="xcb","scene detail acceptance requires a real X11 surface");
    if(!interactive)require(QDir().mkpath(output),"cannot create Earth detail output directory");
    const bool cloudRegion=route==EarthDetailRoute::CloudRegion;
    const bool seaFlight=route==EarthDetailRoute::CloudSeaFlight;
    const bool skyTimelapse=route==EarthDetailRoute::SkyLightTimelapse;
    const bool skyDiscs=route==EarthDetailRoute::SkyDiscDetail;
    const bool airLight=route==EarthDetailRoute::AtmosphereLight;
    const bool overcast=route==EarthDetailRoute::Overcast;
    const bool skyCycle=overcast||airLight||skyDiscs||skyTimelapse||route==EarthDetailRoute::SkyDayCycle;
    const bool objectFlight=route==EarthDetailRoute::ObjectCloudWindFlight;
    const bool objectClouds=skyCycle||objectFlight||route==EarthDetailRoute::ObjectCloudBenchmark;
    const bool cloudBenchmark=route==EarthDetailRoute::ObjectCloudBenchmark||route==EarthDetailRoute::VoxelCloudBenchmark;
    const bool manyClouds=skyCycle||objectFlight||cloudBenchmark||seaFlight||(cloudRegion&&regionalWeather==CloudRegionWeather::Many);
    QMainWindow window;window.resize(interactive?1160:cloudRegion?1040:720,interactive?760:cloudRegion?570:390);
    if(objectFlight||skyCycle)window.setFixedSize(window.size());
    window.setWindowTitle(QStringLiteral("地球场景精细化 — 实际DEM / 影像 / 体素云 / 天空"));
    auto* view=new Viewport3DView(&window);window.setCentralWidget(view);
    auto* dock=new QDockWidget(QStringLiteral("实际数据精度"),&window);auto* status=new QLabel;status->setWordWrap(true);status->setMinimumWidth(205);status->setMaximumWidth(265);dock->setWidget(status);window.addDockWidget(Qt::LeftDockWidgetArea,dock);
    auto* bar=window.addToolBar(QStringLiteral("地球场景"));
    auto config=ViewportConfig::fromPreset("earth_display");config.remove(OceanFeature::kId);
    config.add(EarthView::kId,{{"map_json",std::filesystem::absolute("config/earth/default.map.json").string()},{"view_height",18000.},
        {"dense_regional_clouds",manyClouds&&!objectClouds},{"cloud_objects",objectClouds},{"overcast",overcast},
        {"cloud_grid",{{"width",32},{"height",48},{"depth",32},{"pressure_iterations",8},{"cell_size_m",24}}}});
    json sky={{"year",2026},{"month",9},{"day",26},{"hours_utc",4.0},{"use_bruneton_lut",true}};
    if(skyCycle)sky.update({{"day",25},{"hours_utc",16.0}}); // 2026-09-26 00:00 at UTC+08
    json clouds={{"cover",.45},{"stratus_altitude",1600},{"cumulus_altitude",2600},{"wind_speed",8}};
    if(cloudRegion&&regionalWeather==CloudRegionWeather::Storm) {
        clouds.update({{"cover",.93},{"stratus_altitude",900},{"cumulus_altitude",3650},{"wind_speed",13},{"rain",.6}});
    }
    if(cloudRegion&&regionalWeather==CloudRegionWeather::Sunset) {
        sky.update({{"hours_utc",9.45},{"turbidity",2.1}});
        clouds.update({{"cover",.56},{"stratus_altitude",1750}});
    }
    if(manyClouds)clouds.update({{"cover",.88},{"stratus_altitude",1500},{"cumulus_altitude",2800}});
    if(overcast) {
        sky.update({{"day",26},{"hours_utc",4.0},{"overcast",true}});
        clouds.update({{"cover",1.0},{"stratus_altitude",1750},{"cumulus_altitude",2450},{"wind_speed",6.0}});
    }
    if(const char* fog=std::getenv("VSGRAYSIM_WEATHER_FOG_VISIBILITY"))config.add(EarthView::kId,{{"fog_visibility_m",std::stod(fog)},{"fog_height_m",220.f}});
    if(const char* time=std::getenv("VSGRAYSIM_WEATHER_UTC_HOURS"))sky["hours_utc"]=std::stod(time);
    config.add(SkyFeature::kId,sky);
    config.add(CloudFeature::kId,clouds);
    bool ready=false;QString failure;QObject::connect(view,&Viewport3DView::vsgReady,&window,[&]{ready=true;view->setAutoFrame(false);});
    QObject::connect(view->widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&window,[&](const QString& text){failure=text;});
    view->applyConfig(config);window.show();QElapsedTimer waiting;waiting.start();while(!ready&&failure.isEmpty()&&waiting.elapsed()<45000)pump(10);
    require(failure.isEmpty(),failure.toStdString().c_str());require(ready&&view->isReady(),"Earth detail viewport initialization timed out");view->setAutoFrame(false);
    auto* globe=view->featureAs<EarthViewFeature>(EarthView::kId);require(globe&&globe->surfaceSnapshot(),"actual DEM/imagery surface is unavailable");const auto surface=globe->surfaceSnapshot();const auto quality=surfaceQuality(*surface);const auto model=simcore::geo::makeEnuToWorldMatrix(surface->anchor);
    require(quality.imageryValid&&quality.heightMax-quality.heightMin>50&&quality.imageLumaStdDev>.005,"regional real-source DEM or imagery lacks measured variation");
    const double authoritativeSpan=surface->max_x-surface->min_x;
    const double contextSpan=view->widget()->property("earthRasterContextSpanMetres").toDouble();
    require(std::abs(contextSpan-3*authoritativeSpan)<1e-6,"raster continuation did not bind");
    // A ray over the visible continuation must not turn into authoritative
    // terrain through the display viewport's placement/query interface.
    const auto outsideEye=model*vsg::dvec3(surface->max_x+250.,4000.,0.);
    const auto down=model*vsg::dvec4(0,-1,0,0);
    double lat=0,lon=0,height=0;
    require(!view->context().geoQuery()->intersectRayGeo(outsideEye,{down.x,down.y,down.z},lat,lon,height),
            "raster continuation leaked into the pinned terrain query");
    auto* camera=view->context().camera()->viewMatrix.cast<vsg::LookAt>().get();require(camera,"Earth detail camera is unavailable");
    const auto up4=model*vsg::dvec4(0,1,0,0);const vsg::dvec3 up{up4.x,up4.y,up4.z};
    auto pose=[&](vsg::dvec3 eye,vsg::dvec3 focus){camera->eye=model*eye;camera->center=model*focus;camera->up=up;};
    auto render=[&](double seconds=0.){view->setAutoFrame(false);view->context().setSimulationTime(seconds);view->renderFrame();view->context().viewer()->deviceWaitIdle();};
    const auto ground=surface->sampleLocal(0,0);const double h=ground.status==SurfaceStatus::NoData?0.:ground.height;
    auto regional=[&]{pose({7800,17700,13000},{-2500,200,3000});};
    auto nearGround=[&]{pose({400,h+700,1900},{0,h,0});};
    auto skyward=[&](bool sunward){const auto* weather=globe->weather();require(weather,"actual Earth voxel weather unavailable");auto s=vsg::inverse(model)*vsg::dvec4(weather->sun.x,weather->sun.y,weather->sun.z,0);vsg::dvec3 horizontal{s.x,0,s.z};if(vsg::length(horizontal)<.01)horizontal={0,0,-1};horizontal=vsg::normalize(horizontal)*(sunward?1.:-1.);const vsg::dvec3 eye{0,h+350,0};pose(eye,eye+horizontal*10000+vsg::dvec3(0,4000,0));};
    auto cloudSide=[&]{pose({-4200,1250,2100},{0,2800,0});};
    vsg::dvec3 measuredPeak{0,h,0};
    for(const auto& point:surface->vertices())
        if(std::abs(point.x)<19000 && std::abs(point.z)<19000 && point.y>measuredPeak.y)
            measuredPeak=point;
    auto terrainRidge=[&]{pose(measuredPeak+vsg::dvec3(6000,4400,6500),measuredPeak+vsg::dvec3(-2000,-450,-2000));};
    const double startAltitude=h+180.;
    const double endAltitude=120000.;
    auto flightPose=[&](double progress) {
        const double altitude=startAltitude*std::pow(endAltitude/startAltitude,progress);
        const double pitch=vsg::radians(3.5-24.*progress*progress);
        const vsg::dvec3 eye{400+1900*progress,altitude,1900-550*progress};
        const vsg::dvec3 heading=vsg::normalize(vsg::dvec3(-.55,0,-1));
        pose(eye,eye+36000.*(std::cos(pitch)*heading+vsg::dvec3(0,std::sin(pitch),0)));
        return altitude;
    };
    struct CloudWaypoint { double progress; vsg::dvec3 eye,focus; const char* phase; };
    const std::array<CloudWaypoint,7> cloudRoute{{
        {0.,{-20200,1160,860},{0,2330,0},"distant"},
        {.22,{-6800,1300,840},{0,2300,0},"middle"},
        {.43,{-1440,1440,680},{0,2010,0},"under_base"},
        {.55,{-610,1725,580},{500,2310,10},"approach_edge"},
        {.69,{-110,2480,470},{950,2850,400},"cross_edge"},
        {.84,{780,3880,220},{50,2880,10},"emerge"},
        {1.,{1950,7100,1830},{0,2610,0},"above_top"}
    }};
    auto cloudPassPose=[&](double progress) {
        size_t i=0;
        while(i+2<cloudRoute.size() && progress>=cloudRoute[i+1].progress)++i;
        const auto& a=cloudRoute[i];const auto& b=cloudRoute[i+1];
        const double u=(progress-a.progress)/(b.progress-a.progress);
        const double eased=u*u*(3.-2.*u);
        pose(a.eye+(b.eye-a.eye)*eased,a.focus+(b.focus-a.focus)*eased);
        return progress>=1. ? cloudRoute.back().phase : a.phase;
    };
    status->setText(QStringLiteral("DEM源间距：%1 m\n影像源像素：%2 m\n实际地形范围：%3—%4 m\n最高顶面坡度：%5°\n影像亮度变化：%6\n\n以源间距为准；网格细分不会增加真实地理信息。\n\n表面版本：%7").arg(quality.demSpacingMetres,0,'f',1).arg(quality.imagerySpacingMetres,0,'f',1).arg(quality.heightMin,0,'f',1).arg(quality.heightMax,0,'f',1).arg(quality.slopeMaxDegrees,0,'f',2).arg(quality.imageLumaStdDev,0,'f',2).arg(surface->revision));
    auto action=[&](const QString& label,const std::function<void()>& fn){auto* a=bar->addAction(label);QObject::connect(a,&QAction::triggered,&window,[&,fn]{fn();if(interactive)view->markActivity();else render();});};
    action(QStringLiteral("区域地形"),regional);action(QStringLiteral("实际山脊"),terrainRidge);action(QStringLiteral("近地"),nearGround);action(QStringLiteral("云层侧视"),cloudSide);action(QStringLiteral("迎日天空"),[&]{skyward(true);});action(QStringLiteral("背日天空"),[&]{skyward(false);});
    QTimer playback;
    QElapsedTimer flightTimer;
    bool playCloud=false;
    if(interactive) {
        QObject::connect(&playback,&QTimer::timeout,&window,[&] {
            const double progress=std::min(1.,flightTimer.elapsed()/24000.);
            if(playCloud)cloudPassPose(progress);else flightPose(progress);
            view->markActivity();
            if(progress>=1.)playback.stop();
        });
        auto* play=bar->addAction(QStringLiteral("近地到高空"));
        QObject::connect(play,&QAction::triggered,&window,[&] {
            playCloud=false;
            flightTimer.start();
            flightPose(0.);
            playback.start(16);
            view->markActivity();
        });
        auto* cloudPlay=bar->addAction(QStringLiteral("云侧穿越"));
        QObject::connect(cloudPlay,&QAction::triggered,&window,[&] {
            playCloud=true;
            flightTimer.start();
            cloudPassPose(0.);
            playback.start(16);
            view->markActivity();
        });
        for(auto* other:bar->actions())if(other!=play&&other!=cloudPlay)
            QObject::connect(other,&QAction::triggered,&playback,&QTimer::stop);
    }
    const bool cloudFlight=route==EarthDetailRoute::CloudPass||route==EarthDetailRoute::CloudWind||
                           route==EarthDetailRoute::CloudWindEdge;
    if(route==EarthDetailRoute::Ascent)flightPose(0.);
    else if(cloudFlight)pose({0,1100,0},{1500,2000,0});
    else regional();
    for(int i=0;i<4;++i)render();
    require(globe->weather()&&globe->cloudResources(),"Earth detail weather is unavailable");
    const auto& cloudField=globe->cloudResources()->field;
    if(objectClouds)require(!cloudField,"object cloud mode allocated the voxel solver");
    else require(cloudField&&cloudField->ready(),"Earth detail view must retain the shared physical voxel cloud path");
    if(interactive){view->setAutoFrame(true);QEventLoop loop;QObject::connect(qApp,&QApplication::lastWindowClosed,&loop,&QEventLoop::quit);loop.exec();return;}
    json report={{"surface",quality.json()},{"source",surface->source},{"authoritative_span_m",authoritativeSpan},
        {"visible_context_span_m",contextSpan},{"context_query_isolated",true},{"ocean_acceptance",false},
        {"captures",json::object()}};
    report["shared_physical_voxel_cloud"]=!objectClouds;report["utc"]=sky;report["fog_visibility_m"]=globe->weather()->fogVisibility;
    if(!objectClouds)report["cloud_grid"]={32,48,32};
    auto saveFrame=[&](const char* name) {
        QImage image;
        QElapsedTimer deadline;
        deadline.start();
        while(image.isNull()&&deadline.elapsed()<10000) {
            pump(40);
            view->setAutoFrame(false);
            QThread::msleep(40);
            image=view->grabFrame();
        }
        if(image.isNull())throw std::runtime_error(std::string("Earth detail framebuffer capture failed: ")+name);
        image=image.convertToFormat(QImage::Format_RGB32);
        require(image.save(output+"/"+name+".png"),"cannot save Earth detail capture");
        report["captures"][name]=imageStatistics(image);
        return image;
    };
    auto capture=[&](const char* name,double seconds=0.) {
        render(seconds);
        return saveFrame(name);
    };
    if(overcast) {
        struct View { const char* name; vsg::dvec3 eye,focus; };
        const std::array<View,4> views{{
            {"ground",{400.,h+180.,1900.},{10400.,h+650.,-8100.}},
            {"cloud-base",{-8200.,1400.,7900.},{8800.,3600.,2200.}},
            {"below-cloud",{-8200.,1400.,7900.},{10300.,650.,-2000.}},
            {"cloud-top",{-8200.,6200.,7900.},{15500.,2700.,-9500.}}
        }};
        for(const auto& shot:views) {
            pose(shot.eye,shot.focus);
            QThread::msleep(21);
            capture(shot.name,27.25);
        }
        int blocked=0;
        double meanTransmission=0.;
        const auto up=model*vsg::dvec4(0.,1.,0.,0.);
        const auto& weather=*globe->weather();
        for(int z=-2;z<=2;++z)for(int x=-2;x<=2;++x) {
            const float transmission=rockyenv::cloudTransmittance(weather,model*vsg::dvec3(x*4800.,1200.,z*4800.),
                                         {up.x,up.y,up.z},weather.cloudHeight+1000.,96);
            if(transmission<.15f)++blocked;
            meanTransmission+=transmission/25.;
        }
        report["blocked_vertical_columns"]=blocked;
        report["mean_vertical_transmission"]=meanTransmission;
        require(blocked>=20,"overcast preset leaves too many directly sunlit columns");
        report["overcast"]=true;
        report["wind_speed_mps"]=weather.windSpeed;
        report["cloud_base_m"]=weather.cloudBase;
        report["cloud_top_m"]=weather.cloudHeight;
        report["objects"]=int((*rockyenv::planetSparseCloudParameters().cast<vsg::vec4Array>())[12].w);
        std::ofstream(output.toStdString()+"/overcast.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: native single-object overcast scene"<<std::endl;
        window.hide();pump(20);return;
    }
    if(airLight) {
        struct AirView { const char* name; double hour; vsg::dvec3 eye,focus; bool moon=true; };
        const std::array<AirView,9> shots{{
            {"low-day",12.,{-8200.,1250.,7900.},{10300.,640.,-2000.}},
            {"high-day",12.,{-8200.,12600.,7900.},{32500.,1510.,-9500.}},
            {"east-dawn",5.75,{-8200.,5200.,7900.},{8800.,6350.,2200.}},
            {"west-dusk",17.75,{-8200.,5200.,7900.},{-25200.,6350.,4800.}},
            {"blue-hour",18.25,{-8200.,5200.,7900.},{-25200.,6350.,4800.}},
            {"earth-shadow-dusk",17.+59./60.,{-8200.,5200.,7900.},{8800.,5950.,7900.}},
            {"moon-clouds",22.,{-8200.,5200.,7900.},{8800.,6350.,2200.}},
            {"moon-horizon",22.,{-8200.,1250.,7900.},{10300.,1070.,-2000.}},
            {"moon-disabled",22.,{-8200.,1250.,7900.},{10300.,1070.,-2000.},false}
        }};
        auto* skyFeature=view->featureAs<SkyFeature>(SkyFeature::kId);
        require(skyFeature&&skyFeature->skyControl(),"air light test requires sky controls");
        report["civil_time"]="2026-09-26 UTC+08:00";
        for(const auto& shot:shots) {
            skyFeature->skyControl()->setShowMoon(shot.moon);
            pose(shot.eye,shot.focus);
            if(std::string_view(shot.name)=="low-day"||std::string_view(shot.name)=="high-day") {
                for(int warm=0;warm<5;++warm){QThread::msleep(21);render(shot.hour*3600.);}
                std::array<double,15> timing;
                for(auto& ms:timing) {
                    QThread::msleep(21);
                    const auto stamp=view->context().viewer()->getFrameStamp()->frameCount;
                    QElapsedTimer timer;timer.start();
                    render(shot.hour*3600.);
                    ms=timer.nsecsElapsed()*1.e-6;
                    require(view->context().viewer()->getFrameStamp()->frameCount>stamp,"air benchmark frame was skipped");
                }
                std::sort(timing.begin(),timing.end());
                report["frame_benchmark"][shot.name]={{"frames",15},{"median_ms",timing[7]},{"p90_ms",timing[13]}};
            }
            QThread::msleep(21);
            capture(shot.name,shot.hour*3600.);
            report["views"][shot.name]={{"hour",shot.hour},{"moon",shot.moon},
                {"eye",{shot.eye.x,shot.eye.y,shot.eye.z}},{"focus",{shot.focus.x,shot.focus.y,shot.focus.z}}};
        }
        std::ofstream(output.toStdString()+"/atmosphere-light.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: terrestrial illumination at fixed day, twilight and lunar views"<<std::endl;
        window.hide();pump(20);return;
    }
    if(skyDiscs) {
        constexpr double verticalFov=3.5;
        const vsg::dvec3 eye{-8200.,5200.,7900.};
        auto* skyFeature=view->featureAs<SkyFeature>(SkyFeature::kId);
        require(skyFeature&&skyFeature->sunProvider(),"celestial disk test needs the shared ephemeris");
        auto projection=view->context().camera()->projectionMatrix.cast<vsg::Perspective>();
        require(bool(projection),"celestial disk test requires a perspective camera");
        const auto inverse=vsg::inverse(model);
        struct DiscView { const char* name; int day; double hoursUtc; bool lunar; double fov; };
        const std::array<DiscView,7> shots{{
            {"sun-high",26,4.,false,verticalFov}, {"sun-low",26,9.75,false,verticalFov},
            {"moon-full-high",26,14.,true,verticalFov}, {"moon-full-low",26,9.75,true,verticalFov},
            {"moon-crescent",15,10.5,true,verticalFov},
            {"moon-quarter-detail",19,11.,true,1.2}, {"moon-crescent-detail",15,10.5,true,1.2}
        }};
        report["vertical_field_of_view_deg"]=verticalFov;
        report["eye_local_m"]={eye.x,eye.y,eye.z};
        for(const auto& shot:shots) {
            projection->fieldOfViewY=shot.fov;
            skyFeature->setAtmosphereParams({{"year",2026},{"month",9},{"day",shot.day},{"hours_utc",shot.hoursUtc}});
            QThread::msleep(21);
            render();
            const auto& weather=*globe->weather();
            const auto direction=shot.lunar ? weather.moon : weather.sun;
            const auto local=inverse*vsg::dvec4(direction.x,direction.y,direction.z,0.);
            const double elevation=vsg::degrees(std::asin(std::clamp(local.y,-1.,1.)));
            const auto focus=eye+50000.*vsg::dvec3(local.x,local.y,local.z);
            pose(eye,focus);
            QThread::msleep(21);
            capture(shot.name);
            report["discs"][shot.name]={{"day",shot.day},{"hours_utc",shot.hoursUtc},{"elevation_deg",elevation},
                {"moon_phase",weather.moonPhase},{"vertical_field_of_view_deg",shot.fov},
                {"focus_local_m",{focus.x,focus.y,focus.z}}};
            if(std::string_view(shot.name)=="moon-full-low") {
                globe->setCloudPresentationEnabled(false);
                capture("moon-full-low-clear");
                globe->setCloudPresentationEnabled(true);
            }
        }
        std::ofstream(output.toStdString()+"/celestial-discs.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: fixed-FOV native solar and lunar disk captures"<<std::endl;
        window.hide();pump(20);return;
    }
    if(skyCycle) {
        const vsg::dvec3 eye{-8200.,5200.,7900.};
        const auto east=eye+vsg::dvec3(17000.,1150.,-5700.);
        const auto west=eye+vsg::dvec3(-17000.,1150.,-3100.);
        auto* skyFeature=view->featureAs<SkyFeature>(SkyFeature::kId);
        require(skyFeature&&skyFeature->skyControl(),"sky day-cycle controls are unavailable");
        report["civil_time"]="2026-09-26 UTC+08:00";
        if(skyTimelapse) {
            report["cloud_time_seconds"]=0.;
            report["local_time_step_minutes"]=10;
            for(int minute=0;minute<=1440;minute+=10) {
                const int utcMinutes=16*60+minute;
                skyFeature->setAtmosphereParams({{"year",2026},{"month",9},{"day",25+utcMinutes/1440},
                                                 {"hours_utc",(utcMinutes%1440)/60.}});
                const auto index=QString("%1").arg(minute/10,3,10,QChar('0')).toStdString();
                pose(eye,east);
                QThread::msleep(21);
                capture(("east-"+index).c_str());
                pose(eye,west);
                capture(("west-"+index).c_str());
            }
            std::ofstream(output.toStdString()+"/light-timelapse.json")<<report.dump(2)<<'\n';
            std::cout<<"PASS: 24-hour cloud lighting timelapse at a fixed weather state"<<std::endl;
            window.hide();pump(20);return;
        }
        const auto local=vsg::inverse(model);
        std::vector<int> minutes;
        for(int hour=0;hour<=24;++hour)minutes.push_back(60*hour);
        for(int minute:{315,330,345,1050,1065,1095,1110,1125})minutes.push_back(minute);
        std::sort(minutes.begin(),minutes.end());
        report["samples"]=json::array();
        auto elevation=[&](const vsg::vec3& direction) {
            const auto d=local*vsg::dvec4(direction.x,direction.y,direction.z,0.);
            return vsg::degrees(std::asin(std::clamp(d.y,-1.,1.)));
        };
        for(int minute:minutes) {
            const double seconds=60.*minute;
            const auto tag=QString("%1-%2").arg(minute/60,2,10,QChar('0')).arg(minute%60,2,10,QChar('0')).toStdString();
            pose(eye,east);
            QThread::msleep(21);
            const auto eastImage=capture(("east-"+tag).c_str(),seconds);
            const auto& weather=*globe->weather();
            const double sun=elevation(weather.sun),moon=elevation(weather.moon);
            const float phase=weather.moonPhase;
            pose(eye,west);
            const auto westImage=capture(("west-"+tag).c_str(),seconds);
            require(eastImage.size()==westImage.size(),"sky day-cycle camera dimensions changed");
            report["samples"].push_back({{"local_time",tag},{"seconds",seconds},
                                         {"sun_elevation_deg",sun},{"moon_elevation_deg",moon},{"moon_phase",phase}});
        }
        for(int hour:{1,4,12,19,22}) {
            pose(eye,eye+vsg::dvec3(0.,12000.,-9000.));
            QThread::msleep(21);
            capture(("stars-"+std::to_string(hour)).c_str(),3600.*hour);
        }
        pose(eye,west);
        skyFeature->skyControl()->setShowMoon(false);
        QThread::msleep(21);
        const auto noMoon=capture("west-22-no-moon",22.*3600.);
        skyFeature->skyControl()->setShowMoon(true);
        const auto moonlit=capture("west-22-moonlit",22.*3600.);
        const auto moonDifference=changedLuminanceFraction(noMoon,moonlit);
        require(moonDifference>.01,"Moon did not change the visible night sky or clouds");
        report["moon_changed_pixel_fraction"]=moonDifference;
        const auto& samples=report["samples"];
        require(samples.front().at("sun_elevation_deg").get<double>()<-18.&&
                std::any_of(samples.begin(),samples.end(),[](const auto& s){return s.at("sun_elevation_deg").template get<double>()>30.;})&&
                samples.back().at("sun_elevation_deg").get<double>()<-18.,"sky cycle missed day or astronomical night");
        std::ofstream(output.toStdString()+"/day-cycle.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: 24-hour native atmosphere, single-cloud and night sky captures"<<std::endl;
        window.hide();pump(20);return;
    }
    if(cloudRegion||cloudFlight||seaFlight||cloudBenchmark||objectFlight) {
        using namespace rockyenv;
        auto params=planetSparseCloudParameters().cast<vsg::vec4Array>();
        QElapsedTimer deadline;deadline.start();
        auto resident=[&] {
            return objectClouds ? (*params)[13].w>0.f :
                (*params)[0].w>1.f&&(*params)[7].w>0.f;
        };
        while(!resident()&&deadline.elapsed()<30000) {
            pump(50);render();
        }
        require(resident(),"cloud field did not become resident");
        if(objectFlight) {
            const auto xyz=[](const auto& p){return json::array({p.x,p.y,p.z});};
            const auto vec=[](const json& p){return vsg::dvec3(p.at(0),p.at(1),p.at(2));};
            const auto vector=[](const vsg::vec4& p){return vsg::dvec3(p.x,p.y,p.z);};
            const auto density=[&](const vsg::dvec3& p){return cloudDensity(*globe->weather(),model*p);};
            constexpr int approachFrames=16,holdFrames=12,crossFrames=24;
            constexpr double startTime=9.,tick=.4;
            json flight=json::array();
            if(!routeFile.isEmpty()) {
                std::ifstream input(routeFile.toStdString());
                require(bool(input),"cannot open frozen object cloud route");
                input>>flight;
                require(flight.is_array()&&flight.size()==approachFrames+holdFrames+crossFrames,
                        "invalid frozen object cloud route");
            } else {
                QThread::msleep(21);
                render(startTime+tick*(approachFrames+holdFrames+crossFrames/2));
                auto toLocal=vsg::inverse(model);
                const auto origin=vector((*params)[12]),east=vector((*params)[13]);
                const auto up=vector((*params)[14]),north=vector((*params)[15]);
                std::optional<CloudEdgeSection> best;
                double bestScore=std::numeric_limits<double>::infinity(),lookSide=0.;
                for(int i=0;i<int((*params)[12].w);++i) {
                    const auto& object=(*params)[16+3*i];
                    const auto& radius=(*params)[17+3*i];
                    const auto centre=toLocal*(origin+east*double(object.x)+up*double(object.y)+north*double(object.z));
                    if(std::hypot(centre.x,centre.z)>16500.||object.w<.025f)continue;
                    for(double height:{-.84,-.59,-.30,0.,.32,.67,.94})
                        for(double side:{-1.02,-.81,-.60,-.39,.39,.60,.81,1.02}) {
                            if(std::getenv("VSGRAYSIM_CLOUD_UPPER_EDGE")&&height<.65)continue;
                            const double y=centre.y+height*radius.y,z=centre.z+side*radius.z;
                            auto score=[&](const CloudEdgeSection& edge) {
                                if(edge.waterColumn<.45||edge.waterColumn>4.8||edge.peak>.08f||
                                   density({edge.entry-120.,y,z})>cloudEdgeThreshold||
                                   density({edge.exit+100.,y,z})>cloudEdgeThreshold)
                                    return std::numeric_limits<double>::infinity();
                                return .003*std::hypot(edge.midpoint(),z)+.14*std::abs(edge.exit-edge.entry-275.)+
                                       36.*std::abs(edge.waterColumn-2.2)+10.*std::abs(height+.4);
                            };
                            const auto edge=findCloudEdge(density,y,z,1.55*std::max(radius.x,radius.z),centre.x,score);
                            if(edge&&score(*edge)<bestScore) {
                                best=edge;bestScore=score(*edge);lookSide=side>0. ? -90. : 90.;
                            }
                        }
                }
                require(bool(best),"no visible thin object cloud edge for the wind route");
                std::cout<<"Object edge: x="<<best->entry<<" to "<<best->exit
                         <<" y="<<best->y<<" z="<<best->z<<" column="<<best->waterColumn<<std::endl;
                for(int i=0;i<approachFrames+holdFrames+crossFrames;++i) {
                    const char* phase="approach";
                    double x=best->entry-420.+325.*std::min(1.,double(i)/(approachFrames-1));
                    if(i>=approachFrames)phase="wind_hold";
                    if(i>=approachFrames+holdFrames) {
                        phase="cross";
                        x+=(best->exit-best->entry+215.)*double(i-approachFrames-holdFrames)/(crossFrames-1);
                    }
                    const vsg::dvec3 eye{x,best->y,best->z};
                    flight.push_back({{"phase",phase},{"seconds",startTime+tick*i},{"eye",xyz(eye)},
                                      {"focus",xyz(eye+vsg::dvec3(800.,65.,lookSide))}});
                }
            }
            std::ofstream(output.toStdString()+"/route.json")<<flight.dump(2)<<'\n';
            report["wind_speed_mps"]=globe->weather()->windSpeed;
            report["object_count"]=int((*params)[12].w);
            report["frames"]=json::array();
            std::array<std::vector<double>,3> times;
            QImage firstHold,lastHold;
            int thin=0,clearBefore=0,clearAfter=0;
            for(size_t i=0;i<flight.size();++i) {
                const auto& frame=flight[i];
                const auto eye=vec(frame.at("eye")),focus=vec(frame.at("focus"));
                const double seconds=frame.at("seconds");
                const int phase=i<approachFrames ? 0 : i<approachFrames+holdFrames ? 1 : 2;
                pose(eye,focus);
                if(times[phase].empty())for(int warm=0;warm<3;++warm){QThread::msleep(21);render(seconds);}
                QThread::msleep(21);
                const auto stamp=view->context().viewer()->getFrameStamp()->frameCount;
                QElapsedTimer timer;timer.start();
                render(seconds);
                const double milliseconds=timer.nsecsElapsed()*1.e-6;
                require(view->context().viewer()->getFrameStamp()->frameCount>stamp,"object cloud flight frame was not submitted");
                const auto name=QString("flight-%1").arg(i,3,10,QChar('0')).toStdString();
                const auto image=saveFrame(name.c_str());
                require(image.size()==QSize(640,360),"object cloud flight framebuffer size changed");
                const float water=density(eye);
                if(phase==2&&water>cloudEdgeThreshold&&water<thinCloudLimit)++thin;
                if(water<=cloudEdgeThreshold) {
                    if(thin&&phase==2)++clearAfter;
                    if(!thin)++clearBefore;
                }
                if(i==approachFrames)firstHold=image;
                if(i==approachFrames+holdFrames-1)lastHold=image;
                times[phase].push_back(milliseconds);
                auto result=frame;
                result["eye_condensate"]=water;result["frame_ms"]=milliseconds;
                report["frames"].push_back(std::move(result));
            }
            const auto motion=changedLuminanceFraction(firstHold,lastHold);
            report["thin_cloud_frames"]=thin;
            report["clear_before_frames"]=clearBefore;
            report["clear_after_frames"]=clearAfter;
            report["fixed_camera_changed_pixel_fraction"]=motion;
            const std::array<const char*,3> phases{"approach","wind_hold","cross"};
            for(size_t i=0;i<times.size();++i) {
                auto& phase=times[i];
                std::sort(phase.begin(),phase.end());
                report["frame_benchmark"][phases[i]]={{"frames",phase.size()},
                    {"median_ms",.5*(phase[(phase.size()-1)/2]+phase[phase.size()/2])},
                    {"p90_ms",phase[size_t(std::ceil(.90*phase.size()))-1]}};
            }
            std::ofstream(output.toStdString()+"/flight-acceptance.json")<<report.dump(2)<<'\n';
            std::cout<<"Object cloud wind flight: thin="<<thin<<" clear before="<<clearBefore
                     <<" clear after="<<clearAfter<<" fixed camera motion="<<motion<<std::endl;
            require(thin>=3&&clearBefore>=2&&clearAfter>=2,"object flight missed clear air or the real thin edge");
            require(motion>.001,"object cloud did not move visibly under wind");
            if(std::getenv("VSGRAYSIM_CLOUD_DENSE_FLIGHT")) {
                constexpr double fps=30.;
                const double first=flight.front().at("seconds"),last=flight.back().at("seconds");
                const int frames=int(std::llround((last-first)*fps))+1;
                require(QDir().mkpath(output+"/dense-flight"),"cannot create dense wind frame directory");
                json dense={{"source",report.at("source")},{"surface",report.at("surface")},
                    {"utc",report.at("utc")},{"nominal_simulation_hz",fps},
                    {"capture_is_wallclock_realtime",false},{"frames",json::array()}};
                int eligible=0,thinDense=0;
                for(int i=0;i<frames;++i){
                    const double seconds=std::min(last,first+i/fps);
                    const double key=(seconds-first)/tick;
                    const size_t a=std::min(size_t(std::floor(key)),flight.size()-2),b=a+1;
                    const double blend=std::clamp((seconds-flight[a].at("seconds").get<double>())/tick,0.,1.);
                    const auto eye=vec(flight[a].at("eye"))*(1.-blend)+vec(flight[b].at("eye"))*blend;
                    const auto focus=vec(flight[a].at("focus"))*(1.-blend)+vec(flight[b].at("focus"))*blend;
                    pose(eye,focus);
                    if(i==0)for(int warm=0;warm<8;++warm){QThread::msleep(21);render(seconds);}
                    QThread::msleep(21);QElapsedTimer timer;timer.start();render(seconds);
                    const double ms=timer.nsecsElapsed()*1.e-6;
                    const bool history=view->widget()->property("cloudTemporalHistoryEligible").toBool();
                    const float water=density(eye);
                    eligible+=history;if(water>cloudEdgeThreshold&&water<thinCloudLimit)++thinDense;
                    const auto name=QString("dense-flight/frame-%1").arg(i,4,10,QChar('0')).toStdString();
                    saveFrame(name.c_str());
                    dense["frames"].push_back({{"seconds",seconds},{"eye",xyz(eye)},{"focus",xyz(focus)},
                        {"eye_condensate",water},{"history_eligible",history},{"render_ms",ms}});
                }
                dense["history_eligible_draws"]=eligible;dense["thin_cloud_frames"]=thinDense;
                std::ofstream(output.toStdString()+"/dense-acceptance.json")<<dense.dump(2)<<'\n';
                require(eligible>frames*.85&&thinDense>3,"dense wind flight did not exercise temporal history and thin cloud");
                std::cout<<"PASS: dense 30 Hz wind flight frames="<<frames<<" eligible="<<eligible<<'\n';
            }
            std::cout<<"PASS: object cloud wind flight"<<std::endl;
            window.hide();pump(20);return;
        }
        if(cloudBenchmark) {
            struct Shot { const char* name; vsg::dvec3 eye,centre; };
            const std::array<Shot,4> shots{{
                {"overview",{-14700.,9700.,15300.},{3600.,1850.,-6900.}},
                {"cloud-tops",{-9300.,5100.,8700.},{9500.,2620.,-6500.}},
                {"side",{-7300.,3100.,5760.},{8600.,3160.,-5800.}},
                {"mist-horizon",{8500.,170.,-16000.},{2000.,1900.,-5000.}}
            }};
            report["mode"]=objectClouds ? "objects" : "voxels";
            report["cover_parameter"]=globe->weather()->cover;
            report["wind_speed_mps"]=globe->weather()->windSpeed;
            report["object_count"]=objectClouds ? int((*params)[12].w) : 0;
            for(const auto& shot:shots) {
                pose(shot.eye,shot.centre);
                for(int warm=0;warm<5;++warm)render();
                constexpr int frames=15;
                std::array<double,frames> timing;
                for(auto& milliseconds:timing) {
                    // The widget deliberately skips submissions less than
                    // 16 ms after presentation. Start timing after the gap.
                    QThread::msleep(21);
                    const auto before=view->context().viewer()->getFrameStamp()->frameCount;
                    QElapsedTimer timer;timer.start();
                    render();
                    milliseconds=timer.nsecsElapsed()*1.e-6;
                    require(view->context().viewer()->getFrameStamp()->frameCount>before,
                            "benchmark frame was throttled rather than submitted");
                }
                std::sort(timing.begin(),timing.end());
                report["frame_benchmark"][shot.name]={
                    {"eye_local_m",{shot.eye.x,shot.eye.y,shot.eye.z}},
                    {"focus_local_m",{shot.centre.x,shot.centre.y,shot.centre.z}},
                    {"frames",frames},{"median_ms",timing[frames/2]},
                    {"p90_ms",timing[13]}
                };
                report["frame_benchmark"][shot.name]["temporal_history_eligible"]=view->widget()->property("cloudTemporalHistoryEligible").toBool();
                report["frame_benchmark"][shot.name]["temporal_raw_width"]=view->widget()->property("cloudTemporalRawWidth").toInt();
                report["frame_benchmark"][shot.name]["temporal_output_width"]=view->widget()->property("cloudTemporalOutputWidth").toInt();
                const auto cloudy=capture(shot.name);
                if(std::string_view(shot.name)=="overview") {
                    globe->setCloudPresentationEnabled(false);
                    const auto clear=capture("overview-clear");
                    globe->setCloudPresentationEnabled(true);
                    const auto fraction=changedLuminanceFraction(cloudy,clear);
                    require(fraction>.10,"cloud benchmark needs a populated cloud scene");
                    report["overview_cloud_pixel_fraction"]=fraction;
                }
            }
            if(objectClouds && (!std::getenv("VSGRAYSIM_CLOUD_TEMPORAL")||std::string(std::getenv("VSGRAYSIM_CLOUD_TEMPORAL"))!="0")) {
                const auto width=int(view->context().window()->extent2D().width);
                require(view->widget()->property("cloudTemporalOutputWidth").toInt()==width&&view->widget()->property("cloudTemporalRawWidth").toInt()==(width+1)/2,"temporal renderer did not activate at the declared resolution");
            }
            if(objectClouds && view->widget()->property("cloudTemporalOutputWidth").toInt()>0) {
                auto submitted=[&](double seconds){QThread::msleep(21);render(seconds);};
                submitted(0.);submitted(0.);require(view->widget()->property("cloudTemporalHistoryEligible").toBool(),"static cloud history was not eligible");
                submitted(3.);require(!view->widget()->property("cloudTemporalHistoryEligible").toBool(),"large time jump reused old cloud history");
                submitted(3.);require(view->widget()->property("cloudTemporalHistoryEligible").toBool(),"paused cloud history did not recover");
                submitted(0.);require(!view->widget()->property("cloudTemporalHistoryEligible").toBool(),"rewind reused old cloud history");
                const auto& reset=shots[1];pose(reset.eye,reset.centre);submitted(0.);require(!view->widget()->property("cloudTemporalHistoryEligible").toBool(),"camera cut reused old cloud history");
                submitted(0.);require(view->widget()->property("cloudTemporalHistoryEligible").toBool(),"camera cut history did not recover");
                report["temporal_contract"]={{"static_reuse_eligible",true},{"time_jump_rejected",true},{"rewind_rejected",true},{"camera_cut_rejected",true},{"paused_recovery",true}};
            }
            if(objectClouds) {
                const auto count=int((*params)[12].w);
                require(count>=70,"object cloud field is undersized");
                std::vector<vsg::vec4> initial;
                for(int i=0;i<count;++i)initial.push_back((*params)[16+3*i]);
                const auto& shot=shots[1];
                pose(shot.eye,shot.centre);
                QImage first,last;
                for(int frame=0;frame<40;++frame) {
                    const auto name=QString("motion-%1").arg(frame,3,10,QChar('0')).toStdString();
                    const auto image=capture(name.c_str(),frame*.6);
                    if(frame==0)first=image;
                    last=image;
                }
                int moved=0,changedDensity=0;
                for(int i=0;i<count;++i) {
                    const auto difference=(*params)[16+3*i]-initial[i];
                    if(std::hypot(difference.x,difference.z)>20.f)++moved;
                    if(std::abs(difference.w)>.004f)++changedDensity;
                }
                require(moved>count/2&&changedDensity>0,"independent cloud states did not advance");
                report["motion"]={{"simulation_seconds",39*.6},{"clouds_displaced_over_20m",moved},
                    {"clouds_with_density_change",changedDensity},
                    {"fixed_camera_luminance_changed_fraction",changedLuminanceFraction(first,last)}};
            }
            std::ofstream(output.toStdString()+"/object-cloud-acceptance.json")<<report.dump(2)<<'\n';
            std::cout<<"PASS: native cloud comparison "<<(objectClouds ? "objects" : "voxels")<<std::endl;
            window.hide();pump(20);return;
        }
        if(seaFlight) {
            // The final point is the clear corridor measured by the regional
            // preset. Both builds follow the same camera and weather timeline.
            struct FlightPose { double progress; vsg::dvec3 eye,heading; };
            const auto outlook=vsg::normalize(vsg::dvec3(1.,0.,-.78));
            const std::array<FlightPose,4> path{{
                {0.,{-14700,9700,15300},outlook+vsg::dvec3(0.,-.34,0.)},
                {.42,{-11100,5860,9900},outlook+vsg::dvec3(0.,-.16,0.)},
                {.76,{-8710,4080,7290},outlook+vsg::dvec3(0.,-.035,0.)},
                {1.,{-7300,3440,5760},outlook+vsg::dvec3(0.,.073,0.)}
            }};
            constexpr int travelFrames=49,holdFrames=14;
            constexpr double tick=.20;
            report["frames"]=json::array();
            report["wind_speed_mps"]=globe->weather()->windSpeed;
            report["regional_span_metres"]=2.f*(*params)[11].y;
            const auto chart=(*params)[7];
            const auto sparseChart=(*params)[0];
            auto revision=globe->cloudResources()->field->revision();
            QImage firstHold,lastHold;
            int clearGapFrames=0;
            for(int frame=0;frame<travelFrames+holdFrames;++frame) {
                const double progress=std::min(1.,double(frame)/(travelFrames-1));
                size_t span=0;
                while(span+2<path.size()&&progress>path[span+1].progress)++span;
                const auto& a=path[span];const auto& b=path[span+1];
                const double t=(progress-a.progress)/(b.progress-a.progress);
                const auto eye=a.eye+(b.eye-a.eye)*t;
                const auto focus=eye+14000.*vsg::normalize(a.heading+(b.heading-a.heading)*t);
                pose(eye,focus);
                const auto name=QString("sea-%1").arg(frame,3,10,QChar('0')).toStdString();
                const double seconds=frame*tick;
                const auto image=capture(name.c_str(),seconds);
                const auto& weather=*globe->weather();
                const auto current=globe->cloudResources()->field->revision();
                require((*params)[7]==chart&&(*params)[0]==sparseChart&&
                        (frame==0||current>revision),
                        "cloud sea flight changed resident charts or stopped the wind clock");
                const float water=cloudDensity(weather,camera->eye);
                if(frame>=travelFrames-1) {
                    if(frame==travelFrames-1)firstHold=image;
                    lastHold=image;
                    if(water<=.001f)++clearGapFrames;
                }
                report["frames"].push_back({
                    {"frame",frame},{"simulation_seconds",seconds},{"gpu_cloud_revision",current},
                    {"eye_local_m",{eye.x,eye.y,eye.z}},{"focus_local_m",{focus.x,focus.y,focus.z}},
                    {"eye_condensate_normalized",water},{"fixed_camera",frame>=travelFrames-1}
                });
                revision=current;
            }
            report["clear_gap_frames"]=clearGapFrames;
            report["fixed_camera_luminance_changed_fraction"]=changedLuminanceFraction(firstHold,lastHold);
            std::ofstream(output.toStdString()+"/sea-flight-acceptance.json")<<report.dump(2)<<'\n';
            require(clearGapFrames>=holdFrames/2,"descent did not reach a measured clear gap");
            std::cout<<"PASS: wind-driven cloud sea descent, clear gap frames="<<clearGapFrames<<std::endl;
            window.hide();pump(20);return;
        }
        if(cloudRegion) {
            const auto& weather=*globe->weather();
            const auto sun= vsg::inverse(model)*vsg::dvec4(weather.sun.x,weather.sun.y,weather.sun.z,0.);
            const double sunElevation=vsg::degrees(std::asin(std::clamp(sun.y,-1.,1.)));
            if(regionalWeather==CloudRegionWeather::Sunset)
                require(sunElevation>0. && sunElevation<10.,"sunset preset requires low evening sunlight");
            const char* name=regionalWeather==CloudRegionWeather::Storm ? "storm" :
                regionalWeather==CloudRegionWeather::Sunset ? "sunset" : manyClouds ? "many" : "white";
            report["weather"]=name;
            report["solar_elevation_degrees"]=sunElevation;
            report["cover"]=weather.cover;
            report["wind_speed_mps"]=weather.windSpeed;
            report["cloud_base_m"]=weather.cloudBase;
            report["cloud_top_m"]=weather.cloudHeight;
            if(manyClouds) {
                pose({-14700,9700,15300},{3600,1850,-6900});
                require((*params)[8].w==500.f,"dense regional cloud spacing was not published");
                report["regional_spacing_metres"]=(*params)[8].w;
                report["regional_span_metres"]=2.f*(*params)[11].y;
            } else if(regionalWeather==CloudRegionWeather::Sunset) {
                const auto towardSun=vsg::normalize(vsg::dvec3(sun.x,0.,sun.z));
                const vsg::dvec3 lateral{-towardSun.z,0.,towardSun.x};
                pose(-7700.*towardSun+1400.*lateral+vsg::dvec3(0.,1650.,0.),{0.,2550.,0.});
            } else {
                pose({-6650,3250,4300},{100,2450,70});
            }
            const auto cloudy=capture("cloud-region");
            globe->setCloudPresentationEnabled(false);
            const auto clear=capture("cloud-region-clear");
            globe->setCloudPresentationEnabled(true);
            const auto visible=changedLuminanceFraction(cloudy,clear);
            require(visible>(manyClouds ? .14 : .006),"regional preset did not render a visible cloud field");
            report["cloud_pixel_fraction"]=visible;
            if(manyClouds) {
                pose({-9300,5000,8700},{9500,2620,-6500});
                capture("cloud-sea");
                const vsg::dvec3 nominal{-7300,3120,6400};
                const auto heading=vsg::normalize(vsg::dvec3(1.,.095,-.78));
                std::optional<vsg::dvec3> gap;
                double best=std::numeric_limits<double>::infinity();
                for(int y=-320;y<=320;y+=160)for(int z=-2560;z<=2560;z+=320)
                    for(int x=-2560;x<=2560;x+=320) {
                        const vsg::dvec3 delta(x,y,z),candidate=nominal+delta;
                        bool open=true;
                        for(int range=0;range<=1760;range+=160) {
                            if(cloudDensity(weather,model*(candidate+double(range)*heading))>.001f) {
                                open=false;
                                break;
                            }
                        }
                        if(!open)continue;
                        const double distance=vsg::length(delta);
                        if(distance<best){gap=candidate;best=distance;}
                    }
                require(gap.has_value(),"dense cloud preset has no nearby clear gap");
                report["corridor_eye_local_m"]={gap->x,gap->y,gap->z};
                pose(*gap,*gap+14000.*heading);
                capture("cloud-corridor");
            } else if(regionalWeather==CloudRegionWeather::Sunset) {
                const auto heading=vsg::normalize(vsg::dvec3(sun.x,0.,sun.z));
                const vsg::dvec3 lateral{-heading.z,0.,heading.x};
                pose(-4650.*heading+700.*lateral+vsg::dvec3(0.,1340.,0.),{0.,2350.,0.});
            } else {
                pose({-3700,1240,1680},{320,2470,0});
            }
            if(!manyClouds)capture("cloud-side");
            if(regionalWeather==CloudRegionWeather::Storm) {
                // View the underside opposite the sun, where the thicker
                // columns are visible by their own extinction and shade.
                const auto sunward=vsg::normalize(vsg::dvec3(sun.x,0.,sun.z));
                const vsg::dvec3 lateral{-sunward.z,0.,sunward.x};
                pose(-3220.*sunward+1350.*lateral+vsg::dvec3(0.,h+590.,0.),{0.,2300.,0.});
                capture("storm-underside");
            }
            std::ofstream(output.toStdString()+"/region-acceptance.json")<<report.dump(2)<<'\n';
            std::cout<<"PASS: "<<name<<" cloud region, sun elevation="<<sunElevation
                     <<" cloud frame fraction="<<visible<<std::endl;
            window.hide();pump(20);return;
        }
        auto xyz=[](const auto& p){return json::array({p.x,p.y,p.z});};
        const auto toLocal=vsg::inverse(model);
        const auto sparseChart=(*params)[0];
        const auto gpuChart=(*globe->cloudResources()->layout)[0];
        const auto gpuRevision=globe->cloudResources()->field->revision();
        constexpr int frames=65;
        if(route==EarthDetailRoute::CloudWindEdge) {
            constexpr int firstFrame=windHoldPose;
            constexpr int approachFrames=15,stationaryFrames=12;
            constexpr int clipFrames=frames+windHoldFrames-1-firstFrame;
            const auto& layout=*globe->cloudResources()->layout;
            auto inGpuCloud=[&](const vsg::dvec3& point) {
                const vsg::dvec3 offset=point-vsg::dvec3(layout[0].x,layout[0].y,layout[0].z);
                const auto on=[&](int axis){return vsg::dot(offset,vsg::dvec3(layout[axis].x,layout[axis].y,layout[axis].z));};
                const vsg::dvec3 local(on(1),on(2),on(3)),origin(layout[4].x,layout[4].y,layout[4].z);
                const auto high=origin+double(layout[1].w)*vsg::dvec3(layout[2].w,layout[3].w,layout[4].w);
                return layout[0].w>0.f &&
                    local.x>origin.x&&local.y>origin.y&&local.z>origin.z&&
                    local.x<high.x&&local.y<high.y&&local.z<high.z;
            };
            auto density=[&](const vsg::dvec3& local) {
                return cloudDensity(*globe->weather(),model*local);
            };
            auto clearSides=[&](const CloudEdgeSection& section,double margin) {
                return density({section.entry-margin,section.y,section.z})<=cloudEdgeThreshold &&
                       density({section.exit+margin,section.y,section.z})<=cloudEdgeThreshold;
            };
            // Step the same weather timeline as the original flight, then
            // steer from the most recently committed density each tick.
            globe->setCloudPresentationEnabled(false);
            for(int frame=1;frame<firstFrame;++frame) {
                cloudPassPose(double(frame)/(frames-1));
                render(frame*windTick);
            }
            report["wind_speed_mps"]=globe->weather()->windSpeed;
            report["edge_frames"]=json::array();
            std::optional<CloudEdgeSection> section;
            vsg::dvec3 heldEye,heldFocus;
            QImage firstHold,lastHold;
            int clearBefore=0,thinInside=0,clearAfter=0,controlThin=0;
            for(int frame=0;frame<clipFrames;++frame) {
                const int timeline=firstFrame+frame;
                const double seconds=timeline*windTick;
                const int controlPose=std::max(windHoldPose,timeline-(windHoldFrames-1));
                cloudPassPose(double(controlPose)/(frames-1));
                const auto controlEye=toLocal*camera->eye,controlFocus=toLocal*camera->center;
                const auto previousRevision=globe->cloudResources()->field->revision();
                const auto previous=section;
                double bestScore=std::numeric_limits<double>::infinity();
                section.reset();
                auto consider=[&](double y,double z,double window,double x) {
                    auto rating=[&](const CloudEdgeSection& edge) {
                        if(inGpuCloud(model*vsg::dvec3(edge.midpoint(),y,z)) ||
                           !clearSides(edge,previous ? 24. : 70.) ||
                           edge.waterColumn<(previous ? .30 : .70) ||
                           edge.waterColumn>(previous ? 6.5 : 4.1))return std::numeric_limits<double>::infinity();
                        if(previous) {
                            return std::abs(edge.midpoint()-previous->midpoint())+
                                   2.*std::hypot(y-previous->y,z-previous->z)+
                                   .3*std::abs((edge.exit-edge.entry)-(previous->exit-previous->entry));
                        }
                        return .55*std::abs(edge.midpoint()-40.)+std::hypot(y-2400.,z-610.)+
                               .27*std::abs((edge.exit-edge.entry)-180.)+24.*std::abs(edge.waterColumn-2.8);
                    };
                    const auto candidate=findCloudEdge(density,y,z,window,x,rating);
                    if(!candidate)return;
                    const auto score=rating(*candidate);
                    if(score<bestScore){section=candidate;bestScore=score;}
                };
                if(previous) {
                    for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz)
                        consider(previous->y+8.*dy,previous->z+8.*dz,340.,previous->midpoint());
                    require(section && std::abs(section->midpoint()-previous->midpoint())<64.,
                            "could not follow the same wind-driven cloud edge");
                } else {
                    for(double y=2048.;y<=2944.;y+=32.)
                        for(double z=448.;z<=1120.;z+=32.)consider(y,z,1184.,0.);
                    require(section.has_value(),"no traversable thin cloud section in the approach corridor");
                    std::cout<<"Selected cloud edge: x="<<section->entry<<" to "<<section->exit
                        <<" y="<<section->y<<" z="<<section->z<<" column="<<section->waterColumn<<std::endl;
                }
                const auto& edge=*section;
                const char* phase="approach";
                const bool fixedCamera=frame>=approachFrames&&frame<approachFrames+stationaryFrames;
                vsg::dvec3 eye,focus;
                if(frame<approachFrames) {
                    const double u=double(frame)/approachFrames;
                    eye={edge.entry-500.+420.*u,edge.y,edge.z};
                } else if(fixedCamera) {
                    phase="fixed_camera";
                    if(frame==approachFrames) {
                        heldEye={edge.entry-80.,edge.y,edge.z};
                        heldFocus=heldEye+vsg::dvec3(640.,60.,36.);
                    }
                    eye=heldEye;
                } else {
                    phase="cross";
                    const double u=double(frame-(approachFrames+stationaryFrames))/(clipFrames-approachFrames-stationaryFrames-1);
                    eye={edge.entry-80.+(edge.exit-edge.entry+160.)*u,edge.y,edge.z};
                }
                focus=fixedCamera ? heldFocus : eye+vsg::dvec3(640.,60.,36.);
                require(!inGpuCloud(model*eye),"tracked camera entered an unread GPU-only cloud region");
                const auto suffix=QString("%1").arg(frame,3,10,QChar('0')).toStdString();
                globe->setCloudPresentationEnabled(true);
                pose(eye,focus);
                const auto trackedName="tracked-"+suffix;
                const auto image=capture(trackedName.c_str(),seconds);
                const auto revision=globe->cloudResources()->field->revision();
                const auto water=density(eye),controlDensity=density(controlEye);
                if(controlDensity>cloudEdgeThreshold&&controlDensity<thinCloudLimit)++controlThin;
                if(water>cloudEdgeThreshold&&water<thinCloudLimit)++thinInside;
                if(!thinInside&&water<=cloudEdgeThreshold&&eye.x<edge.entry)++clearBefore;
                if(thinInside&&water<=cloudEdgeThreshold&&eye.x>edge.exit)++clearAfter;
                if(frame==approachFrames)firstHold=image;
                if(frame==approachFrames+stationaryFrames-1)lastHold=image;
                require((*params)[0]==sparseChart&&layout[0]==gpuChart&&
                        revision>previousRevision,"wind step did not advance the resident cloud");
                report["edge_frames"].push_back({
                    {"frame",frame},{"timeline_frame",timeline},{"phase",phase},{"simulation_seconds",seconds},
                    {"gpu_cloud_revision",revision},{"control_route_pose",controlPose},
                    {"control_eye_local_m",xyz(controlEye)},{"control_focus_local_m",xyz(controlFocus)},
                    {"eye_local_m",xyz(eye)},{"focus_local_m",xyz(focus)},
                    {"control_eye_condensate_normalized",controlDensity},{"eye_condensate_normalized",water},
                    {"section_entry_local_m",xyz(vsg::dvec3(edge.entry,edge.y,edge.z))},
                    {"section_exit_local_m",xyz(vsg::dvec3(edge.exit,edge.y,edge.z))},
                    {"section_water_column_normalized_metres",edge.waterColumn},
                    {"section_peak_condensate_normalized",edge.peak}
                });
                if(frame%8==0)std::cout<<"Tracked wind frame "<<frame<<" water="<<water<<std::endl;
            }
            const auto motion=changedLuminanceFraction(firstHold,lastHold);
            require(clearBefore>=2&&thinInside>=3&&clearAfter>=2,
                    "tracked route did not record clear air, thin cloud and clear exit");
            require(motion>.001,"thin cloud scene did not change with the camera fixed");
            report["cloud_edge_threshold_normalized"]=cloudEdgeThreshold;
            report["thin_cloud_upper_normalized"]=thinCloudLimit;
            report["tracked_clear_before_frames"]=clearBefore;
            report["tracked_thin_frames"]=thinInside;
            report["tracked_clear_after_frames"]=clearAfter;
            report["control_thin_frames"]=controlThin;
            report["fixed_camera_luminance_changed_fraction"]=motion;
            std::ofstream(output.toStdString()+"/edge-acceptance.json")<<report.dump(2)<<'\n';
            std::cout<<"PASS: matched wind timeline, tracked cloud entry and exit"<<std::endl;
            window.hide();pump(20);return;
        }
        if(route==EarthDetailRoute::CloudWind) {
            report["wind_speed_mps"]=globe->weather()->windSpeed;
            report["wind_frames"]=json::array();
            int frame=0;
            auto previousRevision=gpuRevision;
            QImage firstHold,lastHold;
            for(int poseIndex=0;poseIndex<frames;++poseIndex) {
                const char* phase=cloudPassPose(double(poseIndex)/(frames-1));
                const int views=poseIndex==windHoldPose ? windHoldFrames : 1;
                for(int index=0;index<views;++index,++frame) {
                    const double seconds=frame*windTick;
                    const auto name=QString("wind-%1").arg(frame,3,10,QChar('0')).toStdString();
                    const auto image=capture(name.c_str(),seconds);
                    const auto revision=globe->cloudResources()->field->revision();
                    require((*params)[0]==sparseChart&&(*globe->cloudResources()->layout)[0]==gpuChart,
                            "wind flight changed the resident cloud charts");
                    require(frame==0 || revision>previousRevision,"wind flight did not advance the GPU field");
                    report["wind_frames"].push_back({
                        {"frame",frame},{"route_pose",poseIndex},{"phase",phase},{"simulation_seconds",seconds},
                        {"eye_local_m",xyz(toLocal*camera->eye)},{"focus_local_m",xyz(toLocal*camera->center)},
                        {"gpu_cloud_revision",revision},
                        {"eye_condensate_normalized",cloudDensity(*globe->weather(),camera->eye)}
                    });
                    if(poseIndex==windHoldPose) {
                        if(index==0)firstHold=image;
                        lastHold=image;
                    }
                    if(frame%8==0)std::cout<<"Wind flight frame "<<frame<<" time="<<seconds<<" s"<<std::endl;
                    previousRevision=revision;
                }
            }
            const auto motion=changedLuminanceFraction(firstHold,lastHold);
            require(motion>0.,"cloud edge did not change while the camera held");
            report["stationary_view_changed_fraction"]=motion;
            report["stationary_view_route_pose"]=windHoldPose;
            std::ofstream(output.toStdString()+"/wind-acceptance.json")<<report.dump(2)<<'\n';
            std::cout<<"PASS: Earth cloud traversal with running wind and fixed camera probe"<<std::endl;
            window.hide();pump(20);return;
        }
        constexpr double thinPathMin=.00045,thinPathMax=.0075;
        auto thinPath=[](double path){return path>=thinPathMin&&path<=thinPathMax;};
        // Parallel rays through a fixed cross-section of the main cloud.
        // Track both its footprint and liquid-water path as the camera moves.
        auto profile=[&] {
            constexpr int step=48;
            const auto& weather=*globe->weather();
            const float referenceWater=(*params)[6].x;
            std::vector<double> columns;
            double area=0.,thinArea=0.,peakPath=0.,sumPath=0.;
            int base=4000,top=1400,maxChord=0;
            for(int y=1400;y<4000;y+=step)for(int z=-1104;z<=1104;z+=step) {
                double path=0.;
                int chord=0;
                for(int x=-1392;x<=1392;x+=step) {
                    const auto density=cloudDensity(weather,model*vsg::dvec3(x,y,z));
                    path+=density*referenceWater*step;
                    if(density>=.002f)chord+=step;
                }
                columns.push_back(path);
                if(thinPath(path))thinArea+=step*step;
                if(path>=.015) {
                    area+=step*step;
                    base=std::min(base,y);top=std::max(top,y);
                }
                maxChord=std::max(maxChord,chord);
                peakPath=std::max(peakPath,path);
                sumPath+=path;
            }
            return std::make_pair(std::move(columns),json{
                {"projected_area_m2",area},{"base_m",base},{"top_m",top},
                {"max_occupied_chord_m",maxChord},{"peak_liquid_water_path_kg_m2",peakPath},
                {"sum_liquid_water_path_kg_m2",sumPath},{"thin_cloud_projected_area_m2",thinArea}
            });
        };
        report["simulation_seconds"]=0.;
        report["cloud_base_m"]=globe->weather()->cloudBase;
        report["cloud_top_m"]=globe->weather()->cloudHeight;
        report["pass_frames"]=json::array();
        report["cross_sections"]=json::array();
        std::vector<double> referenceColumns;
        int thinEdgeFrames=0;
        for(int frame=0;frame<frames;++frame) {
            const double progress=double(frame)/(frames-1);
            const char* phase=cloudPassPose(progress);
            const auto name=QString("pass-%1").arg(frame,3,10,QChar('0')).toStdString();
            QElapsedTimer elapsed;elapsed.start();
            capture(name.c_str());
            const auto& weather=*globe->weather();
            const auto& chart=(*params)[0];
            const auto condensate=cloudDensity(weather,camera->eye);
            if(condensate>.001f&&condensate<.10f)++thinEdgeFrames;
            require(chart==sparseChart&&(*globe->cloudResources()->layout)[0]==gpuChart&&
                    globe->cloudResources()->field->revision()==gpuRevision,
                    "camera movement changed the paused cloud chart or GPU revision");
            report["pass_frames"].push_back({
                {"frame",frame},{"phase",phase},
                {"eye_local_m",xyz(toLocal*camera->eye)},{"focus_local_m",xyz(toLocal*camera->center)},
                {"sparse_origin_ecef",xyz(chart)},{"sparse_authority",chart.w-1.f},
                {"eye_condensate_normalized",condensate},{"render_capture_ms",elapsed.elapsed()},
                {"gpu_cloud_revision",gpuRevision}
            });
            if(frame==0||frame==14||frame==28||frame==44||frame==64) {
                auto [columns,section]=profile();
                require(section["projected_area_m2"].get<double>()>0. &&
                        section["peak_liquid_water_path_kg_m2"].get<double>()>.02,
                        "fixed cross-section does not intersect a resolved cloud");
                if(referenceColumns.empty())referenceColumns=columns;
                double maxDelta=0.;
                for(size_t i=0;i<columns.size();++i)
                    maxDelta=std::max(maxDelta,std::abs(columns[i]-referenceColumns[i]));
                require(maxDelta<1.e-6,"cloud cross-section changed with camera distance");
                section["frame"]=frame;
                section["max_path_delta_from_distant_kg_m2"]=maxDelta;
                report["cross_sections"].push_back(section);
            }
            if(frame%8==0)std::cout<<"Cloud pass "<<frame<<"/"<<frames-1<<" "<<phase<<std::endl;
        }
        report["thin_edge_frames"]=thinEdgeFrames;
        require(thinEdgeFrames>0,"route did not enter dilute condensate at the cloud edge");
        report["thin_cloud_path_range_kg_m2"]={thinPathMin,thinPathMax};
        cloudPassPose(20./(frames-1));
        capture("wisps-after-dynamics",.12);
        const auto resumedRevision=globe->cloudResources()->field->revision();
        require(resumedRevision>gpuRevision&&(*params)[0]==sparseChart,
                "cloud dynamics update lost the resident cloud chart");
        report["resumed_gpu_cloud_revision"]=resumedRevision;
        report["resumed_sparse_authority"]=(*params)[0].w-1.f;
        auto [advancedColumns,section]=profile();
        size_t thinColumns=0,retainedColumns=0;
        for(size_t i=0;i<referenceColumns.size();++i) {
            if(!thinPath(referenceColumns[i]))continue;
            ++thinColumns;
            if(advancedColumns[i]>=.25*referenceColumns[i])++retainedColumns;
        }
        require(retainedColumns>0,"all dilute cloud paths dispersed on the first dynamics update");
        section["simulation_seconds"]=.12;
        section["initial_thin_columns"]=thinColumns;
        section["retained_thin_columns"]=retainedColumns;
        report["resumed_cross_section"]=section;
        std::ofstream(output.toStdString()+"/pass-acceptance.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: continuous Earth cloud traversal framebuffer capture"<<std::endl;
        window.hide();pump(20);return;
    }
    if(route==EarthDetailRoute::Ascent) {
        constexpr int frames=49;
        report["flight_frames"]=json::array();
        report["simulation_seconds"]=0.;
        report["cloud_base_m"]=globe->weather()->cloudBase;
        report["cloud_top_m"]=globe->weather()->cloudHeight;
        for(int frame=0;frame<frames;++frame) {
            const double progress=double(frame)/(frames-1);
            const double altitude=flightPose(progress);
            const auto name=QString("flight-%1").arg(frame,3,10,QChar('0')).toStdString();
            QElapsedTimer elapsed;
            elapsed.start();
            capture(name.c_str());
            report["flight_frames"].push_back({
                {"frame",frame},{"progress",progress},{"local_altitude_m",altitude},
                {"render_capture_ms",elapsed.elapsed()},{"cloud_revision",globe->cloudResources()->field->revision()}
            });
            if(frame%8==0)std::cout<<"Flight frame "<<frame<<"/"<<frames-1
                <<" altitude="<<altitude<<" m"<<std::endl;
        }
        std::ofstream(output.toStdString()+"/flight-acceptance.json")<<report.dump(2)<<'\n';
        std::cout<<"PASS: continuous Earth ascent with real cloud and DEM rendering"<<std::endl;
        window.hide();pump(20);return;
    }
    capture("regional-dem-imagery");
    terrainRidge();capture("measured-terrain-ridge");
    nearGround();capture("near-ground");
    cloudSide();
    // Camera motion can publish a newly resident cloud atlas even with a
    // paused physics clock. Let the same fixed viewpoint settle first.
    auto changedFraction=[](const QImage& first,const QImage& second) {
        require(first.size()==second.size(),"Earth framebuffer size changed at a fixed viewpoint");
        const auto a=first.convertToFormat(QImage::Format_RGB32);
        const auto b=second.convertToFormat(QImage::Format_RGB32);
        size_t changed=0,count=0;
        for(int y=0;y<a.height();y+=2)for(int x=0;x<a.width();x+=2) {
            const auto pa=a.pixel(x,y),pb=b.pixel(x,y);
            changed+=std::abs(qRed(pa)-qRed(pb))+std::abs(qGreen(pa)-qGreen(pb))+
                     std::abs(qBlue(pa)-qBlue(pb))>3;
            ++count;
        }
        return double(changed)/count;
    };
    const auto field=globe->cloudResources()->field;
    // Repositioning may initially rebase the local field.
    render();
    const auto revision=field->revision();
    auto paused=capture("cloud-side");
    double change=1.;
    int repeat=0;
    for(;repeat<4;++repeat) {
        const auto repeated=capture("cloud-side-paused-repeat");
        change=changedFraction(paused,repeated);
        std::cout<<"Paused cloud frame change="<<change<<", field revision="<<field->revision()<<std::endl;
        if(change<.01)break;
        paused=repeated;
        require(paused.save(output+"/cloud-side.png"),"cannot save resident cloud capture");
        report["captures"]["cloud-side"]=imageStatistics(paused);
    }
    require(field->revision()==revision,"paused cloud field advanced after rebasing");
    require(change<.01,"paused physical Earth cloud rendering did not settle");
    report["paused_changed_pixel_fraction"]=change;
    report["cloud_revision"]=revision;
    report["residency_settle_retries"]=repeat;
    skyward(true);capture("sky-sunward");skyward(false);capture("sky-antisunward");
    terrainRidge();view->setFeatureEnabled(CloudFeature::kId,false);capture("measured-terrain-clear-reference");view->setFeatureEnabled(CloudFeature::kId,true);report["measured_peak_local_m"]={measuredPeak.x,measuredPeak.y,measuredPeak.z};
    std::ofstream(output.toStdString()+"/scene-detail-acceptance.json")<<report.dump(2)<<'\n';std::cout<<"PASS: actual Earth detail, DEM/imagery variation, physical voxel clouds, directional sky and paused framebuffer stability\n";window.hide();pump(20);
}
