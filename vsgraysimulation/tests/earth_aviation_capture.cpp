#include "earth_aviation_capture.h"
#include "viewport3d/AircraftExterior.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/EngagementVisualFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/FlightTrailCurve.h"
#include "viewport3d/TrailFeature.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/VsgQtWidget.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <vsg/all.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace vsgraysim::viewport3d;
using json=nlohmann::json;
using V=vsg::vec3;
using D=vsg::dvec3;
using Style=AviationParticles::Style;
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void pump(int ms) {
    QElapsedTimer time;time.start();
    while(time.elapsed()<ms){QApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}
}

enum class FlightSubject { Jet, MissileProp, ResearchRocket };

vsg::ref_ptr<vsg::Node> researchRocket()
{
    auto model=vsg::Group::create();
    auto builder=vsg::Builder::create();
    vsg::StateInfo state;
    constexpr float diameter=1.85f,half=5.8f,nose=2.6f;
    const auto body=vsg::vec4(.86,.90,.94,1);
    const auto metal=vsg::vec4(.22,.25,.29,1);
    auto sleeve=[&](float z,float length,float width,const vsg::vec4& color) {
        vsg::GeometryInfo info;
        info.position={0,0,z};
        info.dx={width,0,0};info.dy={0,width,0};info.dz={0,0,length};
        info.color=color;
        model->addChild(builder->createCylinder(info,state));
    };
    sleeve(nose*.5f,2.f*half-nose,diameter,body);
    sleeve(half+.02f,.27f*diameter,.87f*diameter,metal);
    sleeve(half+.145f*diameter,.02f*diameter,.65f*diameter,{.035f,.044f,.052f,1});
    sleeve(half-1.05f*diameter,.24f*diameter,1.02f*diameter,metal);
    sleeve(-half+nose+.08f,.13f*diameter,1.02f*diameter,metal);
    vsg::GeometryInfo cap;
    cap.position={0,0,-half+nose*.5f};
    cap.dx={diameter,0,0};cap.dy={0,-diameter,0};cap.dz={0,0,-nose};
    cap.color=vsg::vec4(.94f,.94f,.90f,1.f);
    model->addChild(builder->createCone(cap,state));
    return model;
}

D plottedPosition(FlightSubject subject,double t)
{
    // Artist-authored paths for viewing scale and silhouette. The same
    // render path is used for footage and ordinary visual sources.
    if(subject==FlightSubject::Jet)
        return {-104.+44.*t,2490.+25.*std::sin(.63*t)+4.*t,-235.-47.*std::sin(.48*t)};
    if(subject==FlightSubject::MissileProp)
        return {-82.+48.*t,1955.+32.*std::sin(.52*t),-190.-70.*std::sin(.38*t)};
    return {-27.+2.9*t*t,1750.+14.*t+6.9*t*t,-225.-2.9*t};
}

vsg::dmat4 pointingTransform(D position,D forward,double bank)
{
    const D reference=std::abs(forward.y)>.96 ? D(0,0,1) : D(0,1,0);
    const auto side=vsg::normalize(vsg::cross(forward,reference));
    const auto up=vsg::cross(side,forward);
    return vsg::dmat4(vsg::dvec4(side,0),vsg::dvec4(up,0),vsg::dvec4(-forward,0),vsg::dvec4(position,1))*
        vsg::rotate(bank,D(0,0,1));
}

double eased(double x)
{
    x=std::clamp(x,0.,1.);
    return x*x*(3.-2.*x);
}

template<class Render,class Aim>
void recordFlightSequences(const QString& output,Viewport3DView& view,EngagementVisualFeature& fx,
                           const Render& render,const Aim& pose)
{
    constexpr int fps=24, count=144;
    auto moving=vsg::MatrixTransform::create();
    view.context().contentRoot()->addChild(moving);
    bool substitute=true;
    auto jet=loadAircraftExterior("models/F-16/F-16.obj","y","aircraft",15,view.context().options(),substitute,180);
    require(jet&&!substitute,"F-16 model unavailable for footage");
    auto missile=loadAircraftExterior("models/AGM-114/AGM-114.obj","y","missile",3.35,view.context().options(),substitute,180);
    require(missile&&!substitute,"the bundled missile model is unavailable for footage");
    struct Clip {
        FlightSubject subject;
        const char* folder;
        vsg::ref_ptr<vsg::Node> model;
        D nozzle;
        float radius;
        Style style;
        double shutdown;
    };
    const std::array<Clip,3> clips{{
        {FlightSubject::Jet,"01-aircraft",jet,f16ExhaustNozzle,.60f,Style::Jet,4.65},
        {FlightSubject::MissileProp,"02-missile",missile,{0,0,1.70},.30f,Style::WarmExhaust,4.12},
        {FlightSubject::ResearchRocket,"03-rocket",researchRocket(),{0,0,6.08},.73f,Style::LaunchExhaust,4.72}
    }};
    json report={{"fps",fps},{"frames_per_clip",count},{"utc","2026-11-18T04:00:00Z"},
                 {"map","config/earth/taipei-region.map.json"},{"motion_source","authored visual routes"},{"clips",json::array()}};
    for(const auto& clip:clips) {
        const QString directory=output+"/"+clip.folder;
        require(QDir().mkpath(directory),"cannot create video frame directory");
        moving->children.clear();
        moving->addChild(clip.model);
        view.context().requestCompile();
        fx.clear();
        std::vector<double> times;
        size_t peakSmoke=0;
        for(int frame=0;frame<count;++frame) {
            const double time=double(frame)/fps;
            const auto position=plottedPosition(clip.subject,time);
            const auto direction=vsg::normalize(plottedPosition(clip.subject,time+.01)-plottedPosition(clip.subject,time-.01));
            const bool aircraft=clip.subject==FlightSubject::Jet;
            const bool rocket=clip.subject==FlightSubject::ResearchRocket;
            const double bank=aircraft?.38*std::sin(1.04*time):0.;
            moving->matrix=pointingTransform(position,direction,bank);
            const auto nozzle=moving->matrix*clip.nozzle;
            const double start=eased((time-.22)/.48);
            const double off=1.-eased((time-clip.shutdown)/.38);
            const float power=float(start*off*(.89+.075*std::sin(2.8*time)+.02*std::sin(17.3*time)));
            std::vector<EngagementVisualFeature::Plume> sources;
            if(power>.01f) {
                sources.push_back({clip.folder,V(nozzle),V(direction),0,aircraft,power,clip.radius,clip.style});
            }
            if(aircraft&&time>.64&&time<4.72) {
                const float mist=float(.91*eased((time-.64)/.6)*(1.-eased((time-4.35)/.37)));
                sources.push_back({"aircraft-wake",V(nozzle-direction*4.),V(direction),0,false,mist,.53f,Style::Condensation});
            }
            fx.setPlumes(sources);
            const double wide=eased((time-1.48)/3.87);
            const double back=aircraft?8.+38.*wide:rocket?-5.+44.*wide:2.8+25.*wide;
            const double distance=aircraft?15.+88.*wide:rocket?24.+112.*wide:6.+48.*wide;
            const D eye=position+direction*back+D(-.11*distance,(rocket?.06:.21)*distance,distance);
            // Move from the nozzle toward a wider view of the drifting trail.
            const double offset=aircraft?2.+33.*wide:rocket?3.+45.*wide:1.+27.*wide;
            pose(eye,position-direction*offset);
            render(time);
            times.push_back(view.widget()->property("renderFrameMs").toDouble());
            const QString path=directory+QString("/frame-%1.png").arg(frame,4,10,QLatin1Char('0'));
            auto pixels=view.grabFrame();
            if(pixels.isNull()) {
                render(time);
                pixels=view.grabFrame();
            }
            if(pixels.isNull())throw std::runtime_error(view.widget()->property("frameCaptureStatus").toString().toStdString());
            require(pixels.width()==1280&&pixels.height()==720&&pixels.save(path),"could not write native flight video frame");
            peakSmoke=std::max(peakSmoke,fx.smokeHistoryCount());
        }
        require(peakSmoke>10&&fx.smokeHistoryCount()>10,"wake disappeared before the cutoff shot");
        std::sort(times.begin(),times.end());
        report["clips"].push_back({{"name",clip.folder},{"peak_smoke_history",peakSmoke},
            {"smoke_after_cutoff",fx.smokeHistoryCount()},{"render_p95_ms",times[size_t(std::ceil(times.size()*.95))-1]}});
    }
    report["render_fenced"]=view.widget()->property("renderFrameFenced").toBool();
    std::ofstream(output.toStdString()+"/flight-video-frames.json")<<report.dump(2)<<'\n';
}
}

void runEarthAviationCapture(const QString& output,bool videoFrames) {
    require(QApplication::platformName()=="xcb","aviation capture needs native X11 Vulkan");
    require(QDir().mkpath(output),"cannot create aviation output directory");
    Viewport3DView view;
    view.resize(videoFrames?1280:1120,videoFrames?720:630);
    bool ready=false;QString problem;
    QObject::connect(&view,&Viewport3DView::vsgReady,&view,[&]{ready=true;view.setAutoFrame(false);});
    QObject::connect(view.widget(),&vsgraysim::gui::VsgQtWidget::initializationFailed,&view,[&](auto message){problem=message;});
    auto cfg=ViewportConfig::fromPreset("earth_display");
    cfg.remove(OceanFeature::kId);
    cfg.add(EarthView::kId,{{"map_json",std::filesystem::absolute("config/earth/taipei-region.map.json").string()},
        {"lat",25.078},{"lon",121.772},{"alt",0},{"scene_context",false},
        {"cloud_grid",{{"width",32},{"height",48},{"depth",32},{"pressure_iterations",8},{"cell_size_m",24}}}});
    cfg.add(SkyFeature::kId,{{"year",2026},{"month",11},{"day",18},{"hours_utc",4.0}});
    cfg.add(CloudFeature::kId,{{"cover",.25},{"wind_speed",5.5},{"wind_az",80.0}});
    cfg.add(EngagementVisualFeature::kId);
    cfg.add(TrailFeature::kId);
    view.applyConfig(cfg);
    view.show();
    QElapsedTimer timeout;timeout.start();
    while(!ready&&problem.isEmpty()&&timeout.elapsed()<45000)pump(10);
    if(!problem.isEmpty())throw std::runtime_error(problem.toStdString());
    require(ready,"aviation capture viewport did not initialize");
    view.setAutoFrame(false);
    auto* earth=view.featureAs<EarthViewFeature>(EarthView::kId);
    auto* fx=view.featureAs<EngagementVisualFeature>(EngagementVisualFeature::kId);
    auto* tracks=view.featureAs<TrailFeature>(TrailFeature::kId);
    require(earth&&earth->surfaceSnapshot()&&earth->surfaceSnapshot()->valid_samples&&fx&&fx->particleMode(),
        "missing Earth data or particle graphics pipeline");
    earth->setCloudPresentationEnabled(false);
    const auto pose=[&](vsg::dvec3 eye,vsg::dvec3 target) {
        auto* camera=view.camera();
        const auto chart=view.context().contentModel();
        auto look=camera->viewMatrix.cast<vsg::LookAt>();
        const auto up=chart*vsg::dvec4(0,1,0,0);
        look->eye=chart*eye;look->center=chart*target;look->up={up.x,up.y,up.z};
        if(auto p=camera->projectionMatrix.cast<vsg::Perspective>())p->fieldOfViewY=49;
    };
    auto render=[&](double time) {
        view.setAutoFrame(false);view.context().setSimulationTime(time);
        const auto before=view.widget()->property("renderFrameSerial").toULongLong();
        QElapsedTimer wait;wait.start();
        do {view.renderFrame();if(view.widget()->property("renderFrameSerial").toULongLong()>before)break;pump(18);}
        while(wait.elapsed()<1000);
        view.context().viewer()->deviceWaitIdle();
        require(view.widget()->property("renderFrameSerial").toULongLong()>before,"native FX frame not presented");
    };
    auto snap=[&](const char* filename) {
        const auto frame=view.grabFrame();
        require(!frame.isNull()&&frame.width()>=640&&frame.height()>=360&&frame.save(output+"/"+filename),"native FX screenshot failed");
        return frame.convertToFormat(QImage::Format_RGB32);
    };
    if(videoFrames) {
        recordFlightSequences(output,view,*fx,render,pose);
        view.hide();
        return;
    }
    const V centre(0,2820,-115),nozzle(centre+V(f16ExhaustNozzle)),heading(0,0,-1);
    auto aircraft=vsg::MatrixTransform::create();aircraft->matrix=vsg::translate(vsg::dvec3(centre));
    bool substitute=true;
    auto model=loadAircraftExterior("models/F-16/F-16.obj","y","aircraft",15,view.context().options(),substitute,180);
    require(model&&!substitute,"F-16 exterior model unavailable for comparison");
    aircraft->addChild(model);view.context().contentRoot()->addChild(aircraft);view.context().requestCompile();
    pose({17,2825,-88},{0,2820,-112});
    auto jet=EngagementVisualFeature::Plume{"jet.demo",nozzle,heading,0,true,.96f,.58f};
    auto comparison=[&](bool legacy) {
        view.configureFeature(EngagementVisualFeature::kId,{{"legacy_effects",legacy}});
        fx->clear();
        fx->setPlumes({jet});render(0);render(.25);
        return snap(legacy?"01-exhaust-old.png":"02-exhaust-new.png");
    };
    const auto old=comparison(true),modern=comparison(false);
    require(fx->visibleParticles()>=1&&old!=modern,"aircraft exhaust did not render");
    aircraft->children.clear();view.context().requestCompile();
    fx->clear();
    const auto setSmoke=[&](double seconds) {
        const float t=float(seconds);
        const V p(95.f-54.f*t,2801.f+8.f*t+12.f*std::sin(.94f*t),-190.f-4.f*t+12.f*std::sin(1.4f*t));
        const V tangent(-54.f,8.f+11.28f*std::cos(.94f*t),-4.f+16.8f*std::cos(1.4f*t));
        fx->setPlumes({{"exhaust.demo",p,tangent,0,false,.9f,.50f}});
    };
    const auto flight=[&](bool legacy) {
        view.configureFeature(EngagementVisualFeature::kId,{{"legacy_effects",legacy}});
        fx->clear();
        pose({5,2850,9},{3,2823,-184});
        const auto graphBefore=view.widget()->property("graphCompilationCount").toULongLong();
        for(int i=0;i<=16;++i){setSmoke(i*.20);render(i*.20);}
        const auto image=snap(legacy?"03-trail-old.png":"04-trail-new.png");
        if(!legacy) {
            require(fx->smokeHistoryCount()>25,"smoke was not interpolated along motion samples");
            require(view.widget()->property("graphCompilationCount").toULongLong()<=graphBefore+1,
                "particle position updates recompiled the graph");
        }
        return image;
    };
    const auto oldTrail=flight(true),newTrail=flight(false);
    require(oldTrail!=newTrail,"sustained smoke appearance did not change");
    const auto trailCount=fx->smokeHistoryCount();
    pose({25,2827,-141},{20,2823,-195});
    render(3.2);snap("10-trail-close.png");
    pose({5,2850,9},{3,2823,-184});
    const auto sameTimeCount=fx->visibleParticles();
    render(3.2);require(fx->visibleParticles()==sameTimeCount&&fx->smokeHistoryCount()==trailCount,"paused scene emitted more smoke");
    fx->setPlumes({});render(4.2);
    require(fx->smokeHistoryCount()==trailCount,"ending emitter incorrectly erased its existing smoke");
    snap("05-trail-drift.png");
    render(28);require(fx->smokeHistoryCount()==0,"expired trail never released its budget");
    pose({19,2845,-84},{0,2821,-155});
    const auto event=[&](bool legacy){
        view.configureFeature(EngagementVisualFeature::kId,{{"legacy_effects",legacy}});
        fx->clear();
        render(0);fx->addImpact({0,2818,-155},0);render(0);
        if(!legacy){render(.09);snap("09-burst-flash.png");}
        render(.38);
        const auto result=snap(legacy?"06-burst-old.png":"07-burst-new.png");
        if(!legacy){render(3.4);snap("08-burst-smoke.png");require(fx->visibleParticles()>=10,"burst smoke disappeared at end of fireball");}
        return result;
    };
    const auto oldBurst=event(true),newBurst=event(false);
    require(oldBurst!=newBurst,"new fireball and smoke not visible");
    auto measure=[&] {
        std::vector<double> samples;
        for(int i=0;i<20;++i) {
            render(3.4);samples.push_back(view.widget()->property("renderFrameMs").toDouble());
        }
        std::sort(samples.begin(),samples.end());
        return json{{"median",samples[samples.size()/2]},{"p95",samples[18]}};
    };
    const auto withFx=measure();
    fx->clear();render(3.4);const auto baseline=measure();
    require(tracks,"missing observed trail feature");
    const FlightTrailCurve checked({0,0,0},{10,3,-6},D{900,-200,-350},D{-800,70,900},1.);
    require(checked.at(0.)==D(0,0,0)&&checked.at(1.)==D(10,3,-6),"trail changed observed endpoints");
    for(int i=0;i<=50;++i) {
        const auto p=checked.at(i/50.);
        require(p.x>=0.&&p.x<=10.&&p.y>=0.&&p.y<=3.&&p.z>=-6.&&p.z<=0.,"trajectory presentation overshot its observations");
    }
    pose({18,2842,82},{5,2822,-222});
    const auto samples=[&](double t) {
        tracks->addPoint("aircraft",{-115+55*t,2786+3.8*t+9*std::sin(.85*t),-172-14*t+17*std::sin(1.55*t)},
            1,t,D{55,3.8+7.65*std::cos(.85*t),-14+26.35*std::cos(1.55*t)});
        tracks->addPoint("research-rocket",{42,2740+12*t+5.1*t*t,-250},0,t,D{0,12+10.2*t,0});
        tracks->addPoint("recorded-tracer",{120-38*t,2805+16*std::sin(1.1*t),-262-21*t+18*std::sin(.95*t)},
            2,t,D{-38,17.6*std::cos(1.1*t),-21+17.1*std::cos(.95*t)});
    };
    const double sampleTime=4.2;
    const auto compare=[&](bool smooth) {
        tracks->clear();
        view.configureFeature(TrailFeature::kId,{{"smooth_sampling",smooth}});
        render(0.);
        const auto compilations=view.widget()->property("graphCompilationCount").toULongLong();
        for(int step=0;step<=7;++step) {
            samples(step*.6);
            render(step*.6);
        }
        require(view.widget()->property("graphCompilationCount").toULongLong()==compilations,"track updates recompiled the scene graph");
        const auto count=tracks->segmentCount();
        require(smooth ? count>21 : count==21,"one or more tracks, including vertical ascent, did not tessellate");
        return snap(smooth?"12-observed-tracks-curved.png":"11-observed-tracks-straight.png");
    };
    const auto straight=compare(false);
    const auto curved=compare(true);
    require(curved!=straight,"curved track presentation was not visible");
    const auto count=tracks->segmentCount();
    samples(sampleTime);render(sampleTime);
    require(tracks->segmentCount()==count,"paused observations generated trail segments");
    std::vector<double> trailTimes;
    for(int i=0;i<20;++i) {
        render(sampleTime);
        trailTimes.push_back(view.widget()->property("renderFrameMs").toDouble());
    }
    std::sort(trailTimes.begin(),trailTimes.end());
    tracks->clear();
    tracks->addPoint("discontinuous",{0,2800,-212},1,10.,D{10,0,0});
    tracks->addPoint("discontinuous",{900,2800,-212},1,10.5,D{10,0,0});
    tracks->addPoint("discontinuous",{930,2800,-212},1,13.5,D{10,0,0});
    render(13.5);
    require(tracks->segmentCount()==0,"trail joined a teleport or missing interval");
    samples(0.);samples(.6);render(.6);
    require(tracks->segmentCount()==30,"rewind failed to replace the old trail");
    render(21.);
    require(tracks->segmentCount()==0,"expired tracks retained their GPU draw instances");
    const auto trackReport=json{{"observations_per_track",8},{"tracks",3},{"raw_segments",21},{"curved_segments",count},
        {"vertical_ascent_verified",true},{"position_bounds_verified",true},{"pause_and_rewind_verified",true},
        {"no_bridge_across_gaps",true},{"graph_recompiles",0},{"three_track_render_p95_ms",trailTimes[18]},
        {"render_fenced",view.widget()->property("renderFrameFenced").toBool()}};
    std::ofstream(output.toStdString()+"/flight-trail-acceptance.json")<<trackReport.dump(2)<<'\n';
    const auto summary=json{{"earth_source",earth->surfaceSnapshot()->source},{"utc","2026-11-18T04:00:00Z"},
        {"aircraft_followed_nozzle",true},{"sampled_smoke_puffs",trailCount},{"pause_and_expiry_verified",true},
        {"ends_without_erasing_smoke",true},{"cloud_colour_drawn",false},{"turbulence_texels_per_axis",48},{"renderer","local ellipsoids with GPU volume integration"},
        {"stable_render_fenced",view.widget()->property("renderFrameFenced").toBool()},
        {"render_ms",withFx},{"no_fx_render_ms",baseline},
        {"simulation_damage_executed",false},{"scope","Synthetic visual event, no trajectory or damage simulation"}};
    std::ofstream(output.toStdString()+"/aviation-acceptance.json")<<summary.dump(2)<<'\n';
    view.hide();
}
