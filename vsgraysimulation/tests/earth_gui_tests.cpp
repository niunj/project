#include "display/VoxelEquipmentWorkbench.h"
#include <set>
#include "terrain_stream_benchmark.h"
#include "earth_unified_capture.h"
#include "earth_detail_capture.h"
#include "earth_ocean_capture.h"
#include "earth_aviation_capture.h"
#include "viewport3d/GeoTerrainExporter.h"
#include <vsg/app/Viewer.h>
#include <vsg/app/View.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/app/Window.h>
#include "viewport3d/RockyMapFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/VsgQtWidget.h"
#include <QKeyEvent>
#include <simcore/GeoNavigation.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/TerrainDisplay.h>
#include <vsg/utils/LineSegmentIntersector.h>
#include <vsgraysim/layer1_scene/TerrainSystem.h>
#include <vsgraysim/terrain/TerrainSubsystem.h>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <gdal.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void pump(int milliseconds) {
    QElapsedTimer elapsed;elapsed.start();
    do{QApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(2);}while(elapsed.elapsed()<milliseconds);
}
// A Qt timer cannot interrupt a synchronous Vulkan call. Bound this optional
// manual acceptance run even if driver initialization or a frame blocks.
class NativeDeadline {
    std::mutex mutex_;std::condition_variable changed_;bool finished_=false;
    std::thread watchdog_;
public:
    explicit NativeDeadline(int recordingAllowance=0):watchdog_([this,recordingAllowance]{
        std::unique_lock<std::mutex> lock(mutex_);
        const int seconds=120+recordingAllowance+(std::getenv("VSGRAYSIM_SOAK_SECONDS")?std::stoi(std::getenv("VSGRAYSIM_SOAK_SECONDS")):0);
        if(!changed_.wait_for(lock,std::chrono::seconds(seconds),[this]{return finished_;})){
            std::fprintf(stderr,"FAIL: native Earth GUI acceptance exceeded %d seconds\n",seconds);std::fflush(stderr);std::_Exit(EXIT_FAILURE);
        }
    }){}
    ~NativeDeadline(){
        {std::lock_guard<std::mutex> lock(mutex_);finished_=true;}changed_.notify_one();watchdog_.join();
    }
};
void navigation() {
    using namespace simcore::geo;
    for(double lat:{-89.9,-65.,0.,65.,89.9})for(double lon:{-179.9,0.,121.772,179.9}) {
        GeoAnchor anchor;anchor.lat_deg=lat;anchor.lon_deg=lon;anchor.heading_deg=37;
        auto pose=initialCamera(anchor,900.);
        require(vsg::length(vsg::cross(vsg::normalize(pose.center-pose.eye),pose.up))>.99,"initial globe camera basis is degenerate");
        auto orbitalPan=panCamera(initialCamera(anchor,18000000.),750000.,-250000.);
        require(vsg::length(vsg::cross(vsg::normalize(orbitalPan.center-orbitalPan.eye),orbitalPan.up))>.99,
                "pan from a nadir orbit collapsed the camera basis");
        pose=orbitCamera(pose,.1,-.3);
        require(vsg::length(vsg::cross(vsg::normalize(pose.center-pose.eye),pose.up))>.1,"geographic orbit lost tangent basis");
        const auto before=ecefToGeodetic(pose.center.x,pose.center.y,pose.center.z);
        pose=panCamera(pose,100.,60.);
        const auto after=ecefToGeodetic(pose.center.x,pose.center.y,pose.center.z);
        require(std::abs(before.z-after.z)<1e-5,"globe panning changed focus height");
        const auto clip=cameraClip(pose);require(clip.first>0&&clip.second>clip.first&&std::isfinite(clip.second),"invalid globe depth interval");
    }
    static_assert(!std::is_same_v<vsgraysim::TerrainSystem,vsgraysim::TerrainSubsystem>);
    vsgraysim::TerrainSubsystem subsystem;
    require(subsystem.initialize(nlohmann::json::object()),"product terrain subsystem initialization");
    subsystem.shutdown();
}
void packageRoundtrip(const std::filesystem::path& root) {
    namespace fs=std::filesystem;
    using namespace vsgraysim;
    using namespace viewport3d;
    std::vector<double> heights(33*33);
    for(int z=0;z<33;++z)for(int x=0;x<33;++x)heights[z*33+x]=10+.005*(-500+x*1000./32)+.01*(-500+z*1000./32);
    GeoTerrainExportParams params;params.heights=&heights;params.n=33;params.extent_m=1000;
    params.anchor.lat_deg=65;params.anchor.lon_deg=121;params.anchor.alt_m=100;params.anchor.heading_deg=37;
    params.out_dir=(root/"original").string();params.name="rotated";
    const auto result=exportGeoTerrainPackage(params);require(result.ok,result.error.c_str());
    nlohmann::json manifest;{std::ifstream input(result.map_json_path);input>>manifest;}
    for(const auto& layer:manifest["layers"])require(fs::path(layer["uri"].get<std::string>()).is_relative(),"export retained machine-specific asset paths");
    GDALAllRegister();auto image=GDALOpen(result.imagery_path.c_str(),GA_ReadOnly);
    require(image&&GDALGetRasterCount(image)==4,"rotated terrain image needs a real validity alpha channel");GDALClose(image);
    fs::copy(root/"original",root/"moved",fs::copy_options::recursive);
    fs::rename(root/"original",root/"archived");
    const auto moved=(root/"moved/rotated.map.json").string();
    auto surface=earth::EarthDataService::loadMap(moved,params.anchor);
    require(surface&&surface->valid_samples>0,"moved terrain package failed to load");
    auto context=earth::makeTerrainContextDisplay(*surface,params.anchor,{{{-50,-1000,-50},{50,1000,50}}});
    auto hole=vsg::LineSegmentIntersector::create(vsg::dvec3(0,500,0),vsg::dvec3(0,-500,0));context->accept(*hole);
    require(hole->intersections.empty(),"Earth preview context still covers the authoritative damage window");
    auto outside=vsg::LineSegmentIntersector::create(vsg::dvec3(100,500,100),vsg::dvec3(100,-500,100));context->accept(*outside);
    require(!outside->intersections.empty(),"Earth context clipping removed valid surrounding terrain");
    for(double x:{-250.,0.,250.})for(double z:{-250.,0.,250.}) {
        const auto sample=surface->sampleLocal(x,z);
        require(sample.status!=earth::SurfaceStatus::NoData,"export lost a declared interior sample");
        require(std::abs(sample.height-(10+.005*x+.01*z))<.12,"heading or WGS84 height was lost during terrain export");
    }
    nlohmann::json scenario={{"terrain_ref",moved},{"geo_anchor",{{"lat",65},{"lon",121},{"alt",100},{"heading_deg",37}}}};
    RockyMapFeature globe;std::string error;
    require(globe.loadEarthScenario(scenario,(root/"scene.json").string(),error),error.c_str());
    require(globe.surfaceSnapshot()->revision==surface->revision,"GUI and core earth snapshots differ");
    const auto previous=globe.surfaceSnapshot();scenario["terrain_ref"]="missing.map.json";
    require(!globe.loadEarthScenario(scenario,(root/"scene.json").string(),error),"invalid package was accepted by GUI handoff");
    require(globe.surfaceSnapshot()==previous,"failed earth handoff discarded the previous world");
    const auto voxelPath=root/"terrace.map.json";
    std::ofstream(voxelPath) << nlohmann::json({{"format","voxel-terrain-v1"},{"origin_m",{0,0,0}},
        {"dimensions",{1,1,1}},{"cell_size_m",10},{"occupied",{{0,0,0}}}});
    auto voxelScene=scenario;
    voxelScene["earth"]={{"backend","voxel"},{"map",voxelPath.string()}};
    require(globe.loadEarthScenario(voxelScene,{},error),error.c_str());
    const auto pinned=globe.surfaceSnapshot();
    require(pinned->backend()==earth::EarthBackend::Voxel && globe.earthConfiguration()==pinned->declaration,
            "Earth view did not publish the voxel declaration");
    voxelScene["surface_revision"]=pinned->revision+1;
    require(!globe.loadEarthScenario(voxelScene,{},error) && globe.surfaceSnapshot()==pinned,
            "stale display handoff replaced the active terrain");
    scenario["terrain_ref"]=moved;
    require(globe.loadEarthScenario(scenario,{},error) && globe.surfaceSnapshot()->revision==previous->revision,
            "switching back to the Rocky package failed");
}
void nativeStreamBenchmark(const std::filesystem::path& root,bool orbital=false) {
    using namespace vsgraysim::viewport3d;
    require(QGuiApplication::platformName()=="xcb","native stream benchmark requires X11");
    NativeDeadline deadline;
    Viewport3DView view;
    view.resize(960,640);
    view.setAutoFrame(false);
    auto config=ViewportConfig::fromPreset("earth_display");
    config.add(EarthView::kId,{{"map_json",terrain_benchmark::scenario().at("earth").at("map")}});
    if(const auto* disabled=std::getenv("VSGRAYSIM_EARTH_PROBE_DISABLE")){const std::string list=disabled;for(const auto& item:{std::pair{"cloud", "rocky_cloud"},std::pair{"ocean", "rocky_ocean"},std::pair{"sky", "rocky_sky"}})if(list.find(item.first)!=std::string::npos)config.remove(item.second);}
    view.applyConfig(config);
    view.show();
    QElapsedTimer waiting;waiting.start();
    while(!view.isReady() && waiting.elapsed()<45000) pump(10);
    require(view.isReady(),"native Earth viewport initialization");
    auto* globe=view.featureAs<EarthView>(EarthView::kId);
    require(globe,"native Earth view unavailable");
    require(!view.featureAs<RockyMapFeature>(EarthView::kId)->rockyContext(),"voxel startup created a Rocky context");
    view.renderFrame();
    auto viewer=view.context().viewer();
    auto device=view.context().window()->getOrCreateDevice();
    size_t observerFrames=0,frame=0;
    std::set<std::array<int,3>> observerKeys;
    std::vector<double> bindings,draws,completions;
    terrain_benchmark::run(root,device,globe->captureViewPreparation(),[&](const auto& sim) {
        // Include queued Qt work, surface installation, actual draw/present and GPU completion.
        QCoreApplication::processEvents(QEventLoop::AllEvents,1);
        auto snapshot=sim.earthSurface();
        const bool switched=globe->surfaceSnapshot()!=snapshot;
        const auto start=std::chrono::steady_clock::now();
        if(switched) globe->bindSurface(snapshot,false);
        const auto bound=std::chrono::steady_clock::now();
        auto* camera=dynamic_cast<vsg::LookAt*>(view.context().camera()->viewMatrix.get());
        const auto pos=sim.physics()->snapshot()->bodies.at("survey").position;
        camera->center=simcore::geo::localToECEF(snapshot->anchor,{pos.x,0,pos.z});
        camera->eye=simcore::geo::localToECEF(snapshot->anchor,{pos.x,pos.y+3000,pos.z+2500});
        camera->up=simcore::geo::geoUp(snapshot->anchor.lat_deg,snapshot->anchor.lon_deg);
        if(orbital) {
            require(bool(snapshot->orbital),"orbital benchmark requires a native global catalog");
            // Cross Asia and Europe looking down from orbit, descend to the Alps,
            // then pan on the ground before climbing back to the published scene.
            const double t=double(frame%700);
            const double crossing=std::min(t/135.,1.);
            const double descent=std::clamp((t-135.)/135.,0.,1.);
            const double ascent=std::clamp((t-460.)/135.,0.,1.);
            const double away=crossing*(1.-std::clamp((t-595.)/105.,0.,1.));
            simcore::geo::GeoAnchor focus;
            focus.lat_deg=snapshot->anchor.lat_deg+((frame/700%2 ? 42. : 46.)-snapshot->anchor.lat_deg)*away;
            focus.lon_deg=snapshot->anchor.lon_deg+(8.-snapshot->anchor.lon_deg)*away;
            focus.lon_deg+=std::clamp((t-290.)/150.,0.,1.)*.12*away;
            const double range=18000000.*std::pow(650./18000000.,descent*(1.-ascent));
            double elevation=0;
            if(view.context().geoQuery()->groundElevation(focus.lat_deg,focus.lon_deg,elevation)) focus.alt_m=std::max(0.,elevation);
            const auto pose=simcore::geo::initialCamera(focus,range);
            camera->center=pose.center;camera->eye=pose.eye;camera->up=pose.up;
            ++frame;
        }
        view.context().setSimulationTime(simcore::toSeconds(sim.physics()->snapshot()->time));
        const auto prior=viewer->getFrameStamp()->frameCount;
        QElapsedTimer submitTimeout;
        submitTimeout.start();
        do {
            view.renderFrame();
            if(viewer->getFrameStamp()->frameCount>prior) break;
            QThread::msleep(1);
        } while(submitTimeout.elapsed()<2000);
        const auto submitted=std::chrono::steady_clock::now();
        require(viewer->getFrameStamp()->frameCount>prior,"viewport did not submit a frame within two seconds");
        const auto fence = viewer->waitForFences(0,5000000000ULL);
        if(fence != VK_SUCCESS) throw std::runtime_error("viewport completion fence VkResult="+std::to_string(int(fence)));
        if(orbital) {
            require(globe->surfaceSnapshot()==snapshot && sim.physics()->snapshot()->surface_revision==snapshot->revision,
                    "orbit camera replaced the authoritative terrain");
            if(auto observer=view.featureAs<RockyMapFeature>(EarthView::kId)->observerSnapshot()) {
                require(observer->orbital==snapshot->orbital && std::abs(observer->anchor.lon_deg-8.)<1.,
                        "observer used a different dataset or failed to recenter");
                observerKeys.insert(observer->declaration.at("tile").get<std::array<int,3>>());
                ++observerFrames;
            }
        }
        if(switched && prior>2) {
            auto ms=[](auto d){return std::chrono::duration<double,std::milli>(d).count();};
            bindings.push_back(ms(bound-start));draws.push_back(ms(submitted-bound));
            completions.push_back(ms(std::chrono::steady_clock::now()-submitted));
        }
        require(globe->surfaceSnapshot()==snapshot,"viewport consumed different terrain");
    },orbital ? std::function<uint64_t()>([&]{
        auto observer=view.featureAs<RockyMapFeature>(EarthView::kId)->observerSnapshot();
        return observer ? observer->revision : 0;
    }) : std::function<uint64_t()>{}, orbital ? std::function<float()>([&] {
        auto* w=view.featureAs<RockyMapFeature>(EarthView::kId)->weather();
        require(w && w->clouds && w->ocean,"missing planet weather");
        require(std::abs(w->seconds-view.context().simulationTime())<1e-6,"planet weather clock drift");
        return w->detail;
    }) : std::function<float()>{});
    if(orbital) {
        require(observerKeys.size()>=3 && observerFrames>50,"cross-continent observer LOD handoff missing");
        std::cout<<"Orbital route frames="<<frame<<" resident observer frames="<<observerFrames<<" distinct observer windows="<<observerKeys.size()<<"\n";
    }
    auto p95=[](auto& samples){std::sort(samples.begin(),samples.end());return samples.at(size_t(std::ceil(samples.size()*.95))-1);};
    std::cout<<"Native switch stages p95_ms bind="<<p95(bindings)<<" through_present="<<p95(draws)<<" fence="<<p95(completions)<<"\n";
}
void nativeCapture(const QString& path, bool voxel,bool tiled,bool volume,bool orbit=false,const std::string& preset="earth_display",bool weatherCapture=false,bool lightCapture=false,bool cloudCapture=false,bool descentCapture=false,bool viewpointsCapture=false) {
    using namespace vsgraysim::viewport3d;
    require(QGuiApplication::platformName()==QStringLiteral("xcb") || QGuiApplication::platformName()==QStringLiteral("offscreen"),
            "--native-capture requires a native X11 display or QT_QPA_PLATFORM=offscreen (headless Vulkan)");
    NativeDeadline deadline;
    Viewport3DView view;view.resize(960,640);view.setWindowTitle(QStringLiteral("Earth native acceptance"));
    bool ready=false;int frames=0;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;});
    QObject::connect(&view,&Viewport3DView::frameTick,&view,[&](const FrameTick&){++frames;});
    auto config=ViewportConfig::fromPreset(preset);
    if(volume || tiled) {
        auto map=volume && !std::getenv("VSGRAYSIM_BENCH_EARTH_SCENE") ?
            std::filesystem::absolute("config/earth/native-volume.map.json").string() :
            terrain_benchmark::scenario().at("earth").at("map").get<std::string>();
        config.add(EarthView::kId,{{"map_json",map},{"view_height",1400.}});
    }
    if(voxel) config.add(EarthView::kId,{{"map_json",std::filesystem::absolute("config/earth/voxel-terraces.map.json").string()},{"view_height",550.}});
    view.applyConfig(config);
    view.show();
    QElapsedTimer waiting;waiting.start();
    while(!ready&&waiting.elapsed()<45000)pump(10);
    require(ready&&view.isReady(),"native Earth viewport did not emit vsgReady within 45 seconds");
    auto* globe=view.featureAs<RockyMapFeature>("rocky_map");
    require(globe,"earth_display did not install the shared Rocky feature");
    const auto snapshot=globe->surfaceSnapshot();
    if(volume) require(snapshot && !globe->rockyContext() &&
            snapshot->occupiedGeodetic(snapshot->anchor.lat_deg,snapshot->anchor.lon_deg,0).has_value(),"native volume view did not load independently");
    if(tiled || volume) require(snapshot && snapshot->resident_tiles==9 && !snapshot->imagery_rgba.empty(), "native view did not bind tiled DEM imagery");
    if(voxel) require(snapshot && snapshot->backend()==vsgraysim::earth::EarthBackend::Voxel && snapshot->sampleLocal(1,1).height==120, "native view did not bind voxel terraces");
    require(snapshot&&snapshot->revision&&snapshot->valid_samples&&!snapshot->indices().empty(),"native default Earth dataset is unavailable or has no valid triangles");
    if(lightCapture) {
        auto* sky=view.featureAs<SkyFeature>(SkyFeature::kId);
        auto* camera=dynamic_cast<vsg::LookAt*>(view.context().camera()->viewMatrix.get());
        view.context().setSimulationTime(0.);
        const auto capture=[&](const QString& name) {
            int first=frames; waiting.restart();
            while(frames-first<20 && waiting.elapsed()<5000) {pump(10);view.renderFrame();}
            QImage image;waiting.restart();
            while(image.isNull() && waiting.elapsed()<3000) {pump(50);image=view.grabFrame();}
            require(!image.isNull() && image.save(path+"-"+name+".png"),"celestial capture failed");
            return image;
        };
        auto focus=snapshot->anchor;focus.alt_m=0;
        for(auto [name,hour] : {std::pair{"day",4.},{"dusk",10.},{"night",16.}}) {
            view.configureFeature(SkyFeature::kId,{{"year",2026},{"month",9},{"day",26},{"hours_utc",hour}});
            auto pose=simcore::geo::initialCamera(focus,18000000.);
            camera->eye=pose.eye;camera->center=pose.center;camera->up=pose.up;
            capture(name);
        }
        auto pose=simcore::geo::initialCamera(focus,500000.);
        camera->eye=pose.eye;
        auto observer=focus;observer.alt_m=500000.;
        const auto model=simcore::geo::makeEnuToWorldMatrix(observer);
        auto* perspective=dynamic_cast<vsg::Perspective*>(view.context().camera()->projectionMatrix.get());
        perspective->fieldOfViewY=1.6;
        for(auto name : {"sun","moon","quarter"}) {
            const bool sun=std::string_view(name)=="sun";
            const bool quarter=std::string_view(name)=="quarter";
            const int month=quarter?10:9,day=quarter?4:26;
            const double hours=sun?4.:(quarter?0.:16.);
            view.configureFeature(SkyFeature::kId,{{"year",2026},{"month",month},{"day",day},{"hours_utc",hours}});
            sky->sunProvider()->syncDateTime(2026,month,day,hours);
            rockyenv::EnvSunInfo body;float phase;
            require(sun ? sky->sunProvider()->sunInfo(model,body) : sky->sunProvider()->moonInfo(model,body,phase),"celestial ephemeris unavailable");
            camera->eye=pose.eye;
            camera->center=camera->eye+vsg::dvec3(body.dirECEF)*1e7;
            camera->up=simcore::geo::geoUp(focus.lat_deg,focus.lon_deg);
            auto image=capture(name).convertToFormat(QImage::Format_RGB32);
            int litPixels=0;
            for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
                litPixels+=qGray(image.pixel(x,y))>80;
            require(litPixels>100,"celestial disc invisible outside atmosphere");
            std::cout<<name<<" bright_pixels="<<litPixels<<"\n";
        }
        return;
    }
    if (cloudCapture) {
        auto* camera=dynamic_cast<vsg::LookAt*>(view.context().camera()->viewMatrix.get());
        require(camera && view.featureAs<CloudFeature>(CloudFeature::kId),"voxel cloud capture requires the shared cloud feature");
        // Isolate volumetric weather from the separate ocean stage while
        // comparing cloud optical depth at different camera heights.
        if(!descentCapture)view.setFeatureEnabled(OceanFeature::kId,false);
        view.context().setSimulationTime(1234.5);
        for(int i=0;i<4;++i){pump(20);view.renderFrame();}
        const auto* weather=globe->weather();
        require(weather && weather->clouds && weather->cloudHeight>weather->cloudBase,"native volume weather unavailable");
        auto target=snapshot->anchor;target.alt_m=0;
        float best=-1,bestScore=10;
        for(double lat=20;lat<=30;lat+=.5)for(double lon=115;lon<=130;lon+=.5) {
            auto value=vsgraysim::earth::cloudFraction(*weather,simcore::geo::geoUp(lat,lon));
            const float density=descentCapture ? vsgraysim::earth::cloudDensity(*weather,
                simcore::geo::geodeticToECEF(lat,lon,
                    weather->cloudBase+.45*(weather->cloudHeight-weather->cloudBase))) : 0.f;
            const float score=descentCapture ?
                18.f*std::abs(density-.0025f)+.3f*std::abs(value-.40f) : std::abs(value-.68f);
            if(value>.30f && (!descentCapture || density>.0002f) && score<bestScore){bestScore=score;best=value;target.lat_deg=lat;target.lon_deg=lon;}
        }
        require(best>.25f,"no dense three-dimensional cloud column in validation area");
        std::cout<<"cloud capture lat="<<target.lat_deg<<" lon="<<target.lon_deg
                 <<" fraction="<<best<<" mid_density="
                 <<vsgraysim::earth::cloudDensity(*weather,simcore::geo::geodeticToECEF(
                       target.lat_deg,target.lon_deg,
                       weather->cloudBase+.45*(weather->cloudHeight-weather->cloudBase)))
                 <<" base="<<weather->cloudBase<<" top="<<weather->cloudHeight<<"\n";
        if(viewpointsCapture) {
            // Six fixed camera heights x three times of day, at the densest
            // resolved cloud column. Orbit/high look down through the column,
            // cloud_top straddles the top, cloud_inside is inside the volume,
            // cloud_base sits below the base looking up, ground looks across.
            const auto up=simcore::geo::geoUp(target.lat_deg,target.lon_deg);
            const auto east=simcore::geo::geoEast(target.lat_deg,target.lon_deg);
            const double cloudMid=.5*(weather->cloudBase+weather->cloudHeight);
            const auto cloudPoint=simcore::geo::geodeticToECEF(target.lat_deg,target.lon_deg,cloudMid);
            auto* sky=view.featureAs<SkyFeature>(SkyFeature::kId);
            for(auto [tname,hour] : {std::pair{"noon",4.},{"dusk",10.},{"night",16.}}) {
                if(sky)view.configureFeature(SkyFeature::kId,{{"year",2026},{"month",9},{"day",26},{"hours_utc",hour}});
                for(auto [vname,altitude,lookUp] : {std::tuple{"orbit",300000.,false},
                                                    {"high",30000.,false},
                                                    {"cloud_top",double(weather->cloudHeight+800.),false},
                                                    {"cloud_inside",cloudMid,true},
                                                    {"cloud_base",std::max(120.,double(weather->cloudBase)-300.),true},
                                                    {"ground",30.,true}}) {
                    auto focus=target;focus.alt_m=0;
                    auto pose=simcore::geo::initialCamera(focus,altitude);
                    camera->eye=pose.eye;
                    camera->up=up;
                    camera->center=lookUp ? cloudPoint : pose.center;
                    const int before=frames;QElapsedTimer timer;timer.start();
                    while(frames-before<16 && timer.elapsed()<6000){pump(10);view.renderFrame();}
                    QImage image;timer.restart();
                    while(image.isNull() && timer.elapsed()<3000){pump(30);image=view.grabFrame();}
                    const auto filename=path+QString("-%1-%2.png").arg(tname).arg(vname);
                    require(!image.isNull() && image.save(filename),"cloud viewpoint screenshot failed");
                    // Log the pixel statistics that actually describe cloud
                    // realism: brightness spread and sky/cloud contrast.
                    const auto rgb=image.convertToFormat(QImage::Format_RGB32);
                    long sum=0,sum2=0;int bright=0,dark=0;
                    for(int y=0;y<rgb.height();++y)for(int x=0;x<rgb.width();++x){
                        const int l=qGray(rgb.pixel(x,y));sum+=l;sum2+=l*l;
                        if(l>150)++bright;if(l<40)++dark;
                    }
                    const double n=double(rgb.width())*rgb.height();
                    const double mean=sum/n,sd=std::sqrt(std::max(0.,sum2/n-mean*mean));
                    std::cout<<"cloud_view "<<tname<<"/"<<vname<<" alt="<<altitude
                             <<" mean="<<int(mean)<<" sd="<<int(sd)<<" bright="<<int(100.*bright/n)
                             <<"% dark="<<int(100.*dark/n)<<"% file="<<filename.toStdString()<<"\n";
                }
            }
            return;
        }
        if(descentCapture) {
            constexpr int steps=97;
            int insideSamples=0,saved=0;float maximumRouteDensity=0.f;
            std::vector<double> frameTimes;
            const auto up=simcore::geo::geoUp(target.lat_deg,target.lon_deg);
            const auto east=simcore::geo::geoEast(target.lat_deg,target.lon_deg);
            for(int i=0;i<steps;++i) {
                const double progress=double(i)/(steps-1);
                const double altitude=std::exp(std::log(16000000.)+
                    (std::log(120.)-std::log(16000000.))*progress);
                auto focus=target;
                focus.lon_deg+=2.*std::clamp((altitude-100000.)/16000000.,0.,1.);
                auto pose=simcore::geo::initialCamera(focus,altitude);
                camera->eye=pose.eye;camera->center=pose.center;camera->up=pose.up;
                const double approach=std::clamp((18000.-altitude)/
                    (18000.-weather->cloudHeight),0.,1.);
                const double penetration=std::clamp((weather->cloudHeight-altitude)/
                    (weather->cloudHeight-weather->cloudBase),0.,1.);
                const double tilt=.92*approach+.18*penetration;
                if(tilt>0.) {
                    camera->center=camera->eye+vsg::normalize(-up*(1.-1.2*tilt)+east*(.85*tilt))*10000.;
                    camera->up=vsg::normalize(pose.up*(1.-tilt)+up*tilt);
                }
                const auto before=frames;
                QElapsedTimer timer;timer.start();
                while(frames==before && timer.elapsed()<3000){pump(8);view.renderFrame();}
                require(frames>before,"cloud descent viewport stopped submitting frames");
                require(view.context().viewer()->waitForFences(0,5000000000ULL)==VK_SUCCESS,
                        "cloud descent GPU frame did not complete");
                frameTimes.push_back(timer.nsecsElapsed()/1e6);
                if(altitude>weather->cloudBase && altitude<weather->cloudHeight) {
                    const float routeDensity=vsgraysim::earth::cloudDensity(*weather,camera->eye);
                    maximumRouteDensity=std::max(maximumRouteDensity,routeDensity);
                    if(routeDensity>.05f)++insideSamples;
                }
                if(i%8==0 || i==steps-1) {
                    QImage image;timer.restart();
                    while(image.isNull() && timer.elapsed()<3000){pump(20);image=view.grabFrame();}
                    const auto filename=path+QString("-%1.png").arg(i,3,10,QChar('0'));
                    require(!image.isNull() && image.save(filename),"cloud descent screenshot failed");
                    ++saved;
                    std::cout<<"cloud descent frame="<<i<<" altitude_m="<<altitude
                             <<" image="<<filename.toStdString()<<"\n";
                }
            }
            std::cout<<"cloud descent maximum_route_density="<<maximumRouteDensity<<"\n";
            require(insideSamples>1 && saved>=13,"cloud descent did not cross a resolved volume");
            std::sort(frameTimes.begin(),frameTimes.end());
            const double p95=frameTimes[size_t(std::ceil(frameTimes.size()*.95))-1];
            std::cout<<"cloud descent completed frames="<<steps<<" inside_samples="<<insideSamples
                     <<" render_fence_p95_ms="<<p95<<" max_ms="<<frameTimes.back()<<"\n";
            // After the continuous descent, inspect the cloud wall from the
            // clearest nearby column. A vertical route through a cloud is
            // necessarily foggy and cannot by itself reveal cloud parallax.
            auto vantage=target;float clearest=1.f;
            // Stay in the committed sparse-cloud workspace while moving far
            // enough to expose side parallax. A 0.12 degree jump changed the
            // workspace itself and compared a different cloud revision.
            for(double dlat=-.018;dlat<=.018;dlat+=.006)
                for(double dlon=-.018;dlon<=.018;dlon+=.006) {
                    if(std::abs(dlat)+std::abs(dlon)<.008)continue;
                    const float fraction=vsgraysim::earth::cloudFraction(*weather,
                        simcore::geo::geoUp(target.lat_deg+dlat,target.lon_deg+dlon));
                    if(fraction<clearest) {clearest=fraction;vantage.lat_deg=target.lat_deg+dlat;vantage.lon_deg=target.lon_deg+dlon;}
                }
            const auto cloudPoint=simcore::geo::geodeticToECEF(target.lat_deg,target.lon_deg,
                weather->cloudBase+.55*(weather->cloudHeight-weather->cloudBase));
            for(auto [name,altitude] : {std::pair{"side_above",6000.},
                                        {"side_below",std::max(100.,double(weather->cloudBase)-150.)}}) {
                auto pose=simcore::geo::initialCamera(vantage,altitude);
                camera->eye=pose.eye;
                camera->center=cloudPoint;
                camera->up=simcore::geo::geoUp(vantage.lat_deg,vantage.lon_deg);
                const int before=frames;QElapsedTimer timer;timer.start();
                while(frames==before && timer.elapsed()<3000){pump(8);view.renderFrame();}
                require(frames>before && view.context().viewer()->waitForFences(0,5000000000ULL)==VK_SUCCESS,
                        "cloud side probe GPU frame did not complete");
                QImage image;timer.restart();
                while(image.isNull() && timer.elapsed()<3000){pump(20);image=view.grabFrame();}
                require(!image.isNull() && image.save(path+"-"+name+".png"),"cloud side probe screenshot failed");
            }
            std::cout<<"cloud side vantage lat="<<vantage.lat_deg<<" lon="<<vantage.lon_deg
                     <<" fraction="<<clearest<<"\n";
            return;
        }
        const auto capture=[&](const QString& label) {
            int first=frames;QElapsedTimer timer;timer.start();
            while(frames-first<12 && timer.elapsed()<5000){pump(10);view.renderFrame();}
            QImage image;timer.restart();
            while(image.isNull() && timer.elapsed()<3000){pump(50);image=view.grabFrame();}
            require(!image.isNull() && image.save(path+"-"+label+".png"),"voxel cloud altitude screenshot failed");
            return image.convertToFormat(QImage::Format_RGB32);
        };
        for(auto [name,altitude] : {std::pair{"below",double(std::max(100.f,weather->cloudBase-250.f))},
                                    {"inside",double((weather->cloudBase+weather->cloudHeight)*.5f)},
                                    {"above",double(weather->cloudHeight+2000.f)},
                                    {"high",15000.},{"orbit",16000000.}}) {
            auto pose=simcore::geo::initialCamera(target,altitude);
            camera->eye=pose.eye;camera->center=pose.center;camera->up=pose.up;
            if(std::string_view(name)=="below" || std::string_view(name)=="inside") {
                auto up=simcore::geo::geoUp(target.lat_deg,target.lon_deg);
                auto east=simcore::geo::geoEast(target.lat_deg,target.lon_deg);
                camera->center=camera->eye+east*16000.+up*(std::string_view(name)=="below"?10000.:1200.);
                camera->up=up;
            }
            view.setFeatureEnabled(CloudFeature::kId,false);
            auto clear=capture(QString::fromUtf8(name)+"-clear");
            view.setFeatureEnabled(CloudFeature::kId,true);
            auto cloudy=capture(QString::fromUtf8(name)+"-cloud");
            int changed=0;
            int brightened=0;
            for(int y=80;y<560;++y)for(int x=100;x<860;++x) {
                const auto a=clear.pixel(x,y),b=cloudy.pixel(x,y);
                if(std::abs(qRed(a)-qRed(b))+std::abs(qGreen(a)-qGreen(b))+
                   std::abs(qBlue(a)-qBlue(b))>24)++changed;
                if(qRed(b)+qGreen(b)+qBlue(b)>qRed(a)+qGreen(a)+qBlue(a)+80 &&
                   qRed(b)>55 && qGreen(b)>55 && qBlue(b)>55)++brightened;
            }
            require(changed>1000,"cloud volume made no visible contribution at this altitude");
            if(std::string_view(name)=="orbit")
                require(brightened>5000,"orbital cloud depth failed against the globe surface");
            std::cout<<"cloud altitude "<<name<<" metres="<<altitude<<" changed_pixels="<<changed
                     <<" brightened_pixels="<<brightened<<"\n";
        }
        return;
    }
    if (weatherCapture) {
        const bool clearWeather=std::getenv("VSGRAYSIM_CAPTURE_CLEAR")!=nullptr;
        if(clearWeather)view.setFeatureEnabled(CloudFeature::kId,false);
        for(int i=0;i<3;++i) {pump(20);view.renderFrame();}
        struct InspectShadowMaps : vsg::Visitor {
            vsg::Camera* camera; unsigned maps=0;
            explicit InspectShadowMaps(vsg::Camera* value):camera(value){}
            void apply(vsg::Node& node) override { node.traverse(*this); }
            void apply(vsg::View& view) override {
                if(view.camera.get()==camera && view.viewDependentState)
                    maps=view.viewDependentState->shadowMaps.size();
            }
        } inspect(view.context().camera());
        for(const auto& task:view.context().viewer()->recordAndSubmitTasks)
            for(const auto& graph:task->commandGraphs) graph->accept(inspect);
        if(std::getenv("VSGRAYSIM_DUMP_PIPELINES")) {
            struct Dump : vsg::Visitor {
                uint32_t device,view=0;
                explicit Dump(uint32_t id):device(id){}
                void apply(vsg::Node& n) override {n.traverse(*this);}
                void apply(vsg::View& n) override {view=n.viewID;n.traverse(*this);}
                void apply(vsg::StateGroup& n) override {
                    for(auto& c:n.stateCommands) {
                        if(auto p=c.cast<vsg::BindGraphicsPipeline>()) {
                            std::cout<<"PIPE "<<p->pipeline->validated_vk(view)<<" layout "<<p->pipeline->layout->vk(device)<<" ranges";
                            for(auto r:p->pipeline->layout->pushConstantRanges)std::cout<<" "<<r.stageFlags<<"/"<<r.offset<<"/"<<r.size;
                            std::cout<<" source "<<p->pipeline->stages.front()->module->source.substr(0,80)<<"\n";
                        }
                        if(auto b=c.cast<vsg::BindDescriptorSet>())std::cout<<"SET "<<b->firstSet<<" layout "<<b->layout->vk(device)<<"\n";
                    }
                    n.traverse(*this);
                }
            } dump(view.context().window()->getOrCreateDevice()->deviceID);
            for(const auto& task:view.context().viewer()->recordAndSubmitTasks)
                for(const auto& graph:task->commandGraphs)graph->accept(dump);
        }
        require(inspect.maps>=3,"solar cascades or lunar shadow map missing from the actual view");

        auto* camera=dynamic_cast<vsg::LookAt*>(view.context().camera()->viewMatrix.get());
        auto coast=snapshot->anchor; coast.lat_deg=25.35; coast.lon_deg=122.2; coast.alt_m=0;
        view.context().setSimulationTime(1234.5);
        for(auto [name,altitude] : {std::pair{"orbit",16000000.},{"overlap",10000.},{"surface",950.}}) {
            auto pose=simcore::geo::initialCamera(coast,altitude);
            camera->eye=pose.eye; camera->center=pose.center; camera->up=pose.up;
            int first=frames;
            waiting.restart();
            while(frames-first<25 && waiting.elapsed()<5000) { pump(10); view.renderFrame(); }
            auto* w=globe->weather();
            require(w && w->seconds==1234.5 && std::abs(w->detail-vsgraysim::earth::localWeatherDetail(altitude))<.001,
                    "weather altitude or frozen time mismatch");
            require(w->ocean && w->clouds!=clearWeather,"incomplete planet weather stack");
            if(altitude<20000) require(bool(globe->observerSnapshot()),"coastal observer snapshot missing");
            QImage image;waiting.restart();
            while(image.isNull() && waiting.elapsed()<3000) { pump(50); image=view.grabFrame(); }
            require(!image.isNull() && image.save(path+"-"+name+".png"),"weather capture failed");
            std::cout<<preset<<" "<<name<<" near="<<w->detail<<" t="<<w->seconds
                     <<" cloud_base="<<w->cloudBase<<" cloud_top="<<w->cloudHeight
                     <<" cloud_fraction="<<vsgraysim::earth::cloudFraction(*w,simcore::geo::geoUp(coast.lat_deg,coast.lon_deg))
                     <<" revision="<<snapshot->revision<<"\n";
        }
        return;
    }
    if(orbit) {
        auto* camera=dynamic_cast<vsg::LookAt*>(view.context().camera()->viewMatrix.get());
        auto send=[&](Qt::Key key) {
            QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);
            QApplication::sendEvent(view.context().widget(),&event);
        };
        send(Qt::Key_Home);
        require(vsg::length(camera->eye-camera->center)>1.e7,"Home did not reach orbit");
        send(Qt::Key_End);
        require(vsg::length(camera->eye-camera->center)<1.e6,"End did not return to the working surface");
        send(Qt::Key_Home);
        const auto side=simcore::geo::geoEast(snapshot->anchor.lat_deg,snapshot->anchor.lon_deg);
        const auto direction=vsg::normalize(camera->center-camera->eye+side*2200000.);
        double lat,lon,alt;
        require(view.context().geoQuery()->intersectRayGeo(camera->eye,direction,lat,lon,alt) &&
                std::abs(std::remainder(lon-snapshot->anchor.lon_deg,360.))<90 && std::abs(alt)<16000,
                "orbital picking missed the visible hemisphere");
    }
    for(int frame=0;frame<24;++frame) {
        if((tiled || volume) && frame==8) {
            auto next=vsgraysim::earth::EarthDataService::selectTiles(snapshot,snapshot->anchor.lat_deg,snapshot->anchor.lon_deg,snapshot->declaration.at("initial_lod").get<int>()-1);
            globe->bindSurface(next,false);
            require(next!=snapshot && globe->surfaceSnapshot()==next,"viewport failed to switch tile LOD");
        }
        if((tiled || volume) && frame==16) globe->bindSurface(snapshot,false);
        pump(10);view.renderFrame();
    }
    require(frames==24&&view.isReady(),"native Earth viewport did not complete 24 feature/render ticks");
    require(globe->surfaceSnapshot()==snapshot,"native frame loop replaced its pinned Earth snapshot");
    // grabFrame deliberately throttles captures shortly after a presentation.
    // Wait for that interval, retaining a bounded retry for X11 exposure.
    QImage capture;waiting.restart();
    while(capture.isNull()&&waiting.elapsed()<3000){pump(50);capture=view.grabFrame();}
    require(!capture.isNull()&&capture.width()>=320&&capture.height()>=240,"native Earth framebuffer capture is empty or undersized");
    require(QDir().mkpath(QFileInfo(path).absolutePath())&&capture.save(path,"PNG"),"native Earth screenshot could not be saved");
    const auto pixels=capture.convertToFormat(QImage::Format_RGB32);
    std::array<bool,4096> colors{};int distinct=0,minLuma=255,maxLuma=0;
    // Ignore window edges so decorations cannot make a blank viewport pass.
    for(int y=pixels.height()/10;y<pixels.height()*9/10;y+=2){
        const auto* row=reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for(int x=pixels.width()/10;x<pixels.width()*9/10;x+=2){
            const int red=qRed(row[x]),green=qGreen(row[x]),blue=qBlue(row[x]);
            const int color=((red>>4)<<8)|((green>>4)<<4)|(blue>>4);if(!colors[color]){colors[color]=true;++distinct;}
            const int luma=(54*red+183*green+19*blue)/256;minLuma=std::min(minLuma,luma);maxLuma=std::max(maxLuma,luma);
        }
    }
    if(orbit) {
        require(snapshot->orbital && qGray(pixels.pixel(pixels.width()/2,pixels.height()/2))>12 &&
                qGray(pixels.pixel(pixels.width()/8,pixels.height()/8))<12,"orbital screenshot missing Earth disk or space");
    }
    require(distinct>=8&&maxLuma-minLuma>=12,"native Earth screenshot is blank or effectively monochrome");
    std::cout<<"PASS: native "<<preset<<" frames="<<frames<<" revision="<<snapshot->revision<<" valid="<<snapshot->valid_samples
             <<" triangles="<<snapshot->indices().size()/3<<" source="<<snapshot->source<<" colors="<<distinct<<" capture="<<path.toStdString()<<'\n';
    view.hide();pump(20);
}
}
void topicContracts(const QString& root){
    namespace fs=std::filesystem;const fs::path directory=root.toStdString();nlohmann::json scene={{"format","voxel-scene-v1"},{"geo_anchor",{{"lat",25.22},{"lon",121.8},{"alt",19.8}}},{"utc_origin",{{"year",2026},{"month",9},{"day",10},{"hours_utc",4}}},{"objects",nlohmann::json::array()}};
    {std::ofstream file(directory/"scene.vscene.json");file<<scene.dump();}{std::ofstream file(directory/"map.json");file<<"{}";}
    nlohmann::json topic={{"format","earth-scene-topic-v1"},{"scene","scene.vscene.json"},{"earth_map","map.json"},{"clouds",{{"cover",.55}}},{"camera",{{"eye_m",{160,58,200}},{"target_m",{0,10,0}}}}};const auto path=directory/"scene.vtopic.json";{std::ofstream file(path);file<<topic.dump();}
    auto config=vsgraysim::viewport3d::ViewportConfig::fromTopicFile(path.string());bool cloud=false,runtime=false,map=false;for(const auto& entry:config.entries()){require(entry.id!="rocky_ocean","topic duplicated the authoritative scene ocean");if(entry.id=="rocky_cloud")cloud=entry.params.at("cover")==.55;if(entry.id=="rocky_map")map=entry.params.at("cloud_objects")==true&&entry.params.at("scene_context")==true;if(entry.id=="voxel_scene")runtime=entry.params.at("background_loading")==true&&entry.params.at("scene_json")==fs::weakly_canonical(directory/"scene.vscene.json").string();}
    require(cloud&&runtime&&map,"topic lost shared cloud/terrain/background-scene configuration");topic["unexpected"]=1;{std::ofstream file(path);file<<topic.dump();}bool rejected=false;try{vsgraysim::viewport3d::ViewportConfig::fromTopicFile(path.string());}catch(const std::exception&){rejected=true;}require(rejected,"unknown topic field was silently ignored");std::cout<<"PASS: unified scene topic paths, GPU cloud configuration, background staging and schema validation\n";
}
int main(int argc,char** argv){
    QApplication application(argc,argv);QTemporaryDir root;
    if(std::getenv("VSGRAYSIM_GPU_DEBUG"))vsg::Logger::instance()->level=vsg::Logger::LOGGER_DEBUG;
    try{
        const auto arguments=application.arguments();if(arguments.size()==1)topicContracts(root.path());
        if(arguments.size()==3&&arguments[1]=="--aviation-fx-capture"){NativeDeadline deadline;runEarthAviationCapture(arguments[2]);return 0;}
        if(arguments.size()==3&&arguments[1]=="--aviation-video-frames"){NativeDeadline deadline(180);runEarthAviationCapture(arguments[2],true);return 0;}
        if(arguments.size()==3&&arguments[1]=="--aircraft-debris-capture"){NativeDeadline deadline;runEarthAircraftDebrisCapture(arguments[2]);return 0;}
        if(arguments.size()==3&&arguments[1]=="--aircraft-impact-capture"){NativeDeadline deadline(180);runEarthAircraftDebrisCapture(arguments[2],true);return 0;}
        if(arguments.size()==4&&arguments[1]=="--ah64-rotor-capture"){
            NativeDeadline deadline(180);runEarthAh64RotorCapture(arguments[2],arguments[3]);return 0;
        }
        if(arguments.size()>=4&&arguments.size()<=5&&arguments[1]=="--platform-motion-capture"){
            NativeDeadline deadline(180);
            // A shared asset directory can still be used for a compact capture command.
            if(arguments.size()==4)runEarthPlatformMotionCapture(arguments[2],arguments[3],arguments[2]);
            else runEarthPlatformMotionCapture(arguments[2],arguments[4],arguments[3]);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--published-platform-motion-capture"){
            NativeDeadline deadline(180);runEarthPublishedMotionCapture(arguments[2],arguments[3]);return 0;
        }
        if(arguments.size()==5&&(arguments[1]=="--formal-platform-motion-capture"||arguments[1]=="--formal-platform-motion-broker")){
            NativeDeadline deadline(180);runEarthFormalPlatformCapture(arguments[2],arguments[3],arguments[4],arguments[1]=="--formal-platform-motion-broker");return 0;
        }
        if(arguments.size()==3&&arguments[1]=="--vehicle-effects-capture"){
            NativeDeadline deadline;runEarthPlatformMotionCapture({},arguments[2],{},true);return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--m1a2-motion-capture"){
            NativeDeadline deadline;runEarthPlatformMotionCapture(arguments[2],arguments[3]);return 0;
        }
        if(arguments.size()==2&&arguments[1]=="--scene-detail-view") {
            runEarthDetailCapture({},true);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-flight-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::Ascent);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-cloud-pass-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::CloudPass);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-cloud-wind-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::CloudWind);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-cloud-edge-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::CloudWindEdge);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-cloud-sea-flight-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::CloudSeaFlight);return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-sky-day-cycle-capture") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::SkyDayCycle);
            return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-sky-light-timelapse") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::SkyLightTimelapse);
            return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-sky-disc-capture") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::SkyDiscDetail);
            return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-atmosphere-light-capture") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::AtmosphereLight);
            return 0;
        }
        if(arguments.size()==3 && arguments[1]=="--scene-overcast-capture") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::Overcast);
            return 0;
        }
        if((arguments.size()==3||arguments.size()==4) && arguments[1]=="--scene-cloud-object-wind-flight") {
            NativeDeadline deadline;
            runEarthDetailCapture(arguments[2],false,EarthDetailRoute::ObjectCloudWindFlight,
                                  CloudRegionWeather::Many,arguments.value(3));
            return 0;
        }
        if(arguments.size()==4 && arguments[1]=="--scene-cloud-object-capture") {
            require(arguments[3]=="objects"||arguments[3]=="voxels","cloud benchmark mode must be objects or voxels");
            const auto method=arguments[3]=="objects" ? EarthDetailRoute::ObjectCloudBenchmark :
                EarthDetailRoute::VoxelCloudBenchmark;
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,method);return 0;
        }
        if(arguments.size()==4 && arguments[1]=="--scene-cloud-region-capture") {
            const auto name=arguments[3];
            require(name=="white"||name=="storm"||name=="sunset"||name=="many",
                    "cloud region weather must be white, storm, sunset or many");
            const auto weather=name=="storm" ? CloudRegionWeather::Storm :
                name=="sunset" ? CloudRegionWeather::Sunset :
                name=="many" ? CloudRegionWeather::Many : CloudRegionWeather::White;
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false,EarthDetailRoute::CloudRegion,weather);return 0;
        }
        if(arguments.size()==3&&arguments[1]=="--scene-detail-capture") {
            NativeDeadline deadline;runEarthDetailCapture(arguments[2],false);return 0;
        }
        if(arguments.size()==3&&arguments[1]=="--scene-terrain-region-capture") {
            NativeDeadline deadline;runEarthTerrainRegionCapture(arguments[2]);return 0;
        }
        if(arguments.size()==3&&arguments[1]=="--scene-terrain-hires-capture") {
            NativeDeadline deadline;runEarthTerrainRegionCapture(arguments[2],true);return 0;
        }
        if(arguments.size()==5&&arguments[1]=="--scene-urban-capture") {
            NativeDeadline deadline;runEarthUrbanCapture(arguments[2],arguments[3],arguments[4]);return 0;
        }
        if(arguments.size()==6&&arguments[1]=="--scene-street-capture") {
            NativeDeadline deadline;runEarthUrbanCapture(arguments[2],arguments[3],arguments[5],arguments[4]);return 0;
        }
        if(arguments.size()==5&&arguments[1]=="--scene-cluster-capture") {
            NativeDeadline deadline;runEarthUrbanCapture(arguments[2],arguments[2],arguments[4],arguments[3],true);return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--equipment-capture"){NativeDeadline deadline;return vsgraysim::gui::display::runVoxelEquipmentWorkbench(QFileInfo(arguments[2]).absoluteFilePath(),QFileInfo(arguments[3]).absoluteFilePath());}
        if(arguments.size()==4&&arguments[1]=="--earth-g63-motion"){
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::G63Motion);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--earth-deck-takeoff"){
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::DeckTakeoff);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--earth-sonar-crossing"){
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::SonarCrossing);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--earth-diesel-propeller"){
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::DieselPropeller);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--earth-environment-motion"){
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::EnvironmentMotion);
            return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--earth-marine-audio"){
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::MarineAudio);
            return 0;
        }
        if(arguments.size()==4&&(arguments[1]=="--scene-topic-capture"||arguments[1]=="--scene-topic-demo")){qputenv("VSGRAYSIM_SCENE_TOPIC",QFileInfo(arguments[2]).absoluteFilePath().toUtf8());NativeDeadline deadline;runEarthOceanCapture(arguments[3],arguments[1]=="--scene-topic-demo");return 0;}
        if(arguments.size()==3&&arguments[1]=="--scene-ocean-capture") {NativeDeadline deadline;runEarthOceanCapture(arguments[2]);return 0;}
        if(arguments.size()==2&&arguments[1]=="--scene-ocean-demo") {runEarthOceanCapture({},true);return 0;}
        if(arguments.size()>=4&&arguments.size()<=6&&arguments[1]=="--earth-compare-model"){
            NativeDeadline deadline(180);
            bool valid=true;
            const double scale=arguments.size()>=5?arguments[4].toDouble(&valid):1.;
            require(valid&&scale>0.,"comparison assets require a positive metre scale");
            const int dimension=arguments.size()==6?arguments[5].toInt():180;
            runEarthTargetDestruction(arguments[3],dimension,arguments[2],scale);return 0;
        }
        if((arguments.size()==3||arguments.size()==4)&&arguments[1]=="--scene-target-destruction-capture") {
            NativeDeadline deadline;runEarthTargetDestruction(arguments[2],arguments.size()==4?arguments[3].toInt():512);return 0;
        }
        if (arguments.size()==4 && arguments[1]=="--scene-topic-still") {
            NativeDeadline deadline;
            runEarthTopicStillCapture(arguments[2], arguments[3]);
            return 0;
        }
        if (arguments.size()==4 && arguments[1]=="--scene-terrain-page-traverse") {
            NativeDeadline deadline;
            runEarthTopicStillCapture(arguments[2], arguments[3], EarthTopicCapture::TerrainTraverse);
            return 0;
        }
        if (arguments.size()==4 && arguments[1]=="--scene-terrain-collision-capture") {
            NativeDeadline deadline(180);
            runEarthTopicStillCapture(arguments[2],arguments[3],EarthTopicCapture::TerrainCollision);
            return 0;
        }
        if (arguments.size()==4 && arguments[1]=="--scene-thermal-capture") {
            NativeDeadline deadline;
            runEarthTopicStillCapture(arguments[2], arguments[3], EarthTopicCapture::Thermal);
            return 0;
        }
        if(arguments.size()==5&&arguments[1]=="--unified-scene-capture") {
            NativeDeadline deadline;runUnifiedEarth(arguments[3],arguments[4],arguments[2],false);return 0;
        }
        if(arguments.size()==4&&arguments[1]=="--unified-scene-view") {
            runUnifiedEarth(arguments[2],arguments[3],{},true);return 0;
        }
        if(arguments.size()==2 && (arguments[1]=="--native-stream-benchmark" || arguments[1]=="--orbital-stream-benchmark")) {
            nativeStreamBenchmark(root.path().toStdString(),arguments[1]=="--orbital-stream-benchmark");
            return 0;
        }
        if(arguments.size()==4 && (arguments[1]=="--orbital-capture" || arguments[1]=="--weather-capture" || arguments[1]=="--lighting-capture" || arguments[1]=="--cloud-volume-capture" || arguments[1]=="--cloud-descent-capture" || arguments[1]=="--cloud-viewpoints-capture")) {
            const auto preset=arguments[3].toStdString();
            require(preset=="earth_display" || preset=="scene_designer" || preset=="simulation_display","unknown Earth viewport");
            nativeCapture(arguments[2],false,false,true,true,preset,arguments[1]=="--weather-capture",arguments[1]=="--lighting-capture",arguments[1]=="--cloud-volume-capture" || arguments[1]=="--cloud-descent-capture" || arguments[1]=="--cloud-viewpoints-capture",arguments[1]=="--cloud-descent-capture",arguments[1]=="--cloud-viewpoints-capture");
            return 0;
        }
        require(arguments.size()==1||(arguments.size()==3&&(arguments[1]==QStringLiteral("--native-capture")||arguments[1]==QStringLiteral("--native-voxel-capture")||arguments[1]==QStringLiteral("--native-tiled-capture")||(arguments[1]==QStringLiteral("--native-volume-capture")||arguments[1]=="--orbital-capture"))&&!arguments[2].isEmpty()),"usage: sim_earth_gui_tests [--native-capture PATH]");
        require(root.isValid(),"Earth GUI fixture temporary directory unavailable");
        navigation();packageRoundtrip(root.path().toStdString());std::cout<<"PASS: geographic camera, distinct terrain types, rotated portable export, GUI/core revision and transactional handoff\n";
        if(arguments.size()==3)nativeCapture(arguments[2],arguments[1]==QStringLiteral("--native-voxel-capture"),arguments[1]==QStringLiteral("--native-tiled-capture"),arguments[1]==QStringLiteral("--native-volume-capture")||arguments[1]=="--orbital-capture",arguments[1]=="--orbital-capture");
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
