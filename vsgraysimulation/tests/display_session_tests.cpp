#include "display/EquipmentSession.h"
#include "display/SensorPanels.h"
#include "display/PanelProcessing.h"
#include "ScrollableImageView.h"
#include "CfarProcessor.h"
#include "vsgraysim/broker/EquipmentInfo.h"
#include "vsgraysim/broker/SensorProductCodec.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void settle(vsgraysim::gui::display::DisplayPanel* panel,vsgraysim::gui::display::EquipmentSession* session){for(int n=0;n<100;++n){panel->tick(session);QApplication::processEvents();if(n>2&&panel->property("processedFrameId").isValid()&&panel->property("processedSimTime").toDouble()==session->lastSimTime()&&panel->property("processedFrameId").toULongLong()==session->lastFrameId()){panel->tick(session);return;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("background display processing timed out");}
bool labelContains(QWidget* panel,const QString& value) {
    for(auto* label:panel->findChildren<QLabel*>()) if(label->text().contains(value)) return true;
    return false;
}
void captureRadarDisplays(const QString& directory) {
    using namespace vsgraysim;
    using namespace vsgraysim::gui::display;
    require(QDir().mkpath(directory),"cannot create radar display capture directory");
    auto& broker=SimDataBroker::instance();
    auto fixture=[&](uint32_t id,SensorType type,const char* name){
        EquipmentInfo info;info.equipment_id=id;info.type=type;info.name=name;
        EquipmentRegistry::instance().registerEquipment(info);
        EquipmentChannelSpec spec;spec.sensor_type=type;
        auto session=std::make_unique<EquipmentSession>(spec);
        require(session->bind(id,name),"cannot bind radar capture fixture");
        return session;
    };
    auto photo=[&](DisplayPanel* panel,const QString& title,const QString& file){
        QWidget frame;frame.setWindowTitle(title);frame.resize(1024,550);
        auto* layout=new QVBoxLayout(&frame);
        auto* caption=new QLabel(title,&frame);caption->setStyleSheet("font-weight:600;font-size:16px;");
        layout->addWidget(caption);layout->addWidget(panel,1);frame.show();
        QApplication::processEvents();
        const bool saved=frame.grab().save(QDir(directory).filePath(file));
        panel->setParent(nullptr);
        require(saved,"radar display capture failed");
    };

    auto marine=fixture(6201,SensorType::RADAR,"civil display fixture");
    auto radarPanel=createPanelFor(SensorType::RADAR);
    RadarFrame sweep;sweep.sensor_id=6201;sweep.stage=0;sweep.resize(160,8);
    sweep.latitude=25.16;sweep.longitude=121.75;sweep.altitude=36;
    sweep.prf=80;sweep.sample_rate_hz=4.0e6;sweep.pulse_width=float(1./sweep.sample_rate_hz);
    sweep.range_max=5900;
    struct Patch {double bearing,bin,angular,radial,amplitude;};
    const std::array patches{
        Patch{35,55,5,2,1},Patch{102,86,3,3,.82},Patch{195,41,7,2,.72},
        Patch{281,140,17,3,.25},Patch{310,113,19,6,.26},Patch{325,120,14,7,.21}
    };
    for(int azimuth=0;azimuth<360;++azimuth){
        sweep.frame_id=azimuth+1;sweep.sim_time=azimuth*.02;sweep.beam_az_deg=float(azimuth);
        for(size_t bin=0;bin<sweep.range_bins;++bin){
            double amplitude=.0002;
            for(const auto& patch:patches){
                const double angle=std::remainder(azimuth-patch.bearing,360.)/patch.angular;
                const double range=(bin-patch.bin)/patch.radial;
                amplitude+=patch.amplitude*std::exp(-.5*(angle*angle+range*range));
            }
            for(size_t p=0;p<sweep.doppler_bins;++p)
                sweep.rd_map[p*sweep.range_bins+bin]=std::polar(float(amplitude),float(.18*bin+.1*p));
        }
        require(broker.publishRadar(sweep),"marine fixture publish failed");marine->tick();settle(radarPanel.get(),marine.get());
    }
    sweep.frame_id++;sweep.sim_time+=.02;sweep.beam_az_deg=35;
    for(size_t p=0;p<sweep.doppler_bins;++p)for(size_t bin=0;bin<sweep.range_bins;++bin){
        const double distance=(double(bin)-55.)/2.;
        sweep.rd_map[p*sweep.range_bins+bin]=std::polar(float(.0002+std::exp(-.5*distance*distance)),float(.18*bin+.1*p));
    }
    require(broker.publishRadar(sweep),"marine range fixture publish failed");marine->tick();settle(radarPanel.get(),marine.get());
    auto* ppi=radarPanel->findChild<vsgraysim::gui::PpiDisplay*>();
    require(ppi&&ppi->echoBearingCount()==360,"marine display fixture lost its observed radials");
    auto* mode=radarPanel->findChild<QComboBox*>("radarPlotMode");
    require(mode->currentIndex()==1,"P0 should default to I/Q traces");
    photo(radarPanel.get(),QStringLiteral("脉冲 I/Q · 合成回波验收"),"pulse-iq-curves.png");
    mode->setCurrentIndex(0);settle(radarPanel.get(),marine.get());
    photo(radarPanel.get(),QStringLiteral("海事 PPI · 合成回波验收"),"marine-ppi-a-scope.png");
    auto imaging=fixture(6202,SensorType::SAR,"illustrative ISAR display fixture");
    auto panel=createPanelFor(SensorType::SAR);
    panel->findChild<QCheckBox*>("isarFocusedMode")->setChecked(true);
    SarFrame focused;focused.sensor_id=6202;focused.stage=3;focused.frame_id=1;focused.resize(192,124);
    focused.isar_axes=IsarImageAxes{480,.22,-13,.22,true};
    // Precomputed arbitrary pixels for a civil aircraft-shaped test pattern.
    // This fixture tests presentation only and has no signal-formation input.
    auto spot=[&](double cx,double cy,float intensity,double width){
        for(uint32_t y=0;y<focused.azimuth_samples;++y)for(uint32_t x=0;x<focused.range_samples;++x){
            const double dx=(x-cx)/width,dy=(y-cy)/width;
            focused.echo_data[size_t(y)*focused.range_samples+x]+=
                std::complex<float>(intensity*std::exp(-.5*(dx*dx+dy*dy)),0);
        }
    };
    for(int x=32;x<=153;x+=4)spot(x,62,float(.14+.10*std::sin(.27*x)),2.2);
    for(int n=0;n<11;++n)for(int sign:{-1,1}){
        spot(90+3.1*n,62+sign*3.8*n,float(.19+.38*(n%3==0)),2.2);
        if(n<6)spot(137+1.9*n,62+sign*2.7*n,float(.12+.14*(n%2==0)),1.6);
    }
    spot(34,62,.85f,2.2);spot(104,50,1.f,2.6);spot(104,74,.90f,2.8);
    require(broker.publishSar(focused),"ISAR picture fixture publish failed");imaging->tick();settle(panel.get(),imaging.get());
    photo(panel.get(),QStringLiteral("ISAR · 聚焦图演示数据"),"isar-focused-display-example.png");
}
}
int main(int argc,char** argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication application(argc,argv);
    using namespace vsgraysim;
    using namespace vsgraysim::gui::display;
    auto& broker=SimDataBroker::instance();
    struct Cleanup { SimDataBroker& broker; ~Cleanup(){ broker.serverOnline(false); EquipmentRegistry::instance().clear(); } } cleanup{broker};
    try {
        {
            PanelProcessor processor;auto first=std::make_shared<VisFrame>();first->sensor_id=9001;first->resize(1024,1024);first->intensity.assign(1024*1024,1);first->frame_id=1;
            PanelInput input;input.identity="old_binding";input.sequence=1;input.equipment=9001;input.vis=first;processor.submit(input);
            auto second=std::make_shared<VisFrame>();second->sensor_id=9002;second->resize(2,2);second->intensity={40,41,42,43};second->frame_id=2;input.identity="new_binding";input.sequence=2;input.equipment=9002;input.vis=second;processor.submit(input);
            std::optional<PanelResult> result;for(int n=0;n<200&&!result;++n){result=processor.take();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
            require(result&&result->identity=="new_binding"&&result->values==second->intensity,"late background result replaced a new binding or altered raw values");require(result->workerThread!=std::hash<std::thread::id>{}(std::this_thread::get_id()),"processing ran on the GUI thread");
        }
        vsgraysim::gui::CfarProcessor cfar;auto parameters=cfar.params();parameters.guard_bins=0;parameters.ref_bins=1;parameters.threshold_db=25;cfar.setParams(parameters);
        require(cfar.detect({0,20,0,20,40,20,0,20,0},3,3).empty(),"CA-CFAR averaged logarithmic dB instead of linear power");
        parameters.guard_bins=2;parameters.ref_bins=6;parameters.threshold_db=18;cfar.setParams(parameters);std::vector<float> spectrum(64*32,-240);spectrum[16*64+10]=0;spectrum[16*64+50]=-130;
        auto detections=cfar.detect(spectrum,64,32);require(detections.size()==1&&detections[0].col==10,"float32 FFT residue became a distant radar target");
        EquipmentInfo camera;camera.equipment_id=99;camera.type=SensorType::VIS;camera.name="optical camera";camera.platform_kind="sensor_site";
        const auto inferredCamera=WorkModeRegistry::instance().inferProfile({camera});require(inferredCamera.id=="visible_imaging"&&inferredCamera.channels[0].role==EquipmentWorkRole::Imaging,"ordinary optical camera incorrectly opened cockpit");
        broker.serverOnline(true);
        auto registration=[](uint32_t id,SensorType type) {
            EquipmentInfo info; info.equipment_id=id; info.type=type; info.name="display test"; info.powered=true;
            EquipmentRegistry::instance().registerEquipment(info);
        };
        registration(6101,SensorType::VIS); registration(6102,SensorType::IR);
        registration(6103,SensorType::SAR); registration(6104,SensorType::RADAR);
        EquipmentChannelSpec visualSpec; visualSpec.sensor_type=SensorType::VIS;
        EquipmentSession visual(visualSpec); require(visual.bind(6101,"camera"),"VIS bind");
        VisFrame image; image.sensor_id=6101; image.resize(2,2); image.intensity={0,.25f,.75f,1}; image.stage=4;
        require(broker.publishVis(image),"VIS publish"); visual.tick();
        require(visual.hasFrame() && visual.frames()==1 && visual.lastFrameId()==0 && visual.connected(),"VIS first frame zero/session data");
        require(visual.vis() && visual.vis()->intensity==image.intensity,"VIS accessor lost physical payload");
        visual.tick({},true,true);require(visual.paused()&&!visual.active()&&!visual.stale()&&visual.statusText().contains(QStringLiteral("暂停")),"local paused source reported stream failure");visual.tick();
        auto visualPanel=createPanelFor(SensorType::VIS); require(bool(visualPanel),"VIS panel missing"); settle(visualPanel.get(),&visual);
        require(labelContains(visualPanel.get(),QStringLiteral("归一化 DN")),"VIS product units missing");
        visual.tick(); require(visual.frames()==1,"duplicate display tick counted a new frame");
        image.sim_time=.1; require(broker.publishVis(image),"same id new-time publish"); visual.tick(); require(visual.frames()==2,"scenario restart/new time frame was ignored");

        image.surface_revision=101; require(broker.publishVis(image),"versioned VIS publish");
        visual.tick(100); require(!visual.hasFrame(),"workbench accepted an out-of-version frame");
        visual.tick(101,false); require(!visual.hasFrame(),"unloaded Earth view released new frame");
        visual.tick(101,true); require(visual.hasFrame() && visual.vis()->surface_revision==101,"matching frame did not resume");

        EquipmentChannelSpec irSpec; irSpec.sensor_type=SensorType::IR;
        EquipmentSession infrared(irSpec); require(infrared.bind(6102,"thermal"),"IR bind");
        IrFrame thermal; thermal.sensor_id=6102; thermal.resize(2,2); thermal.radiance={1,2,3,4}; thermal.stage=0;
        require(broker.publishIr(thermal),"P0 IR publish"); infrared.tick();
        auto irPanel=createPanelFor(SensorType::IR); settle(irPanel.get(),&infrared);
        require(infrared.ir()->stage==0 && labelContains(irPanel.get(),QStringLiteral("W/m²/sr")),"P0 radiance presented as temperature/DN");
        registration(6105,SensorType::IR);
        EquipmentSession alternate(irSpec); require(alternate.bind(6105,"alternate thermal"),"alternate IR bind");
        auto alternateFrame=thermal; alternateFrame.sensor_id=6105; alternateFrame.radiance={40,40,40,40};
        require(broker.publishIr(alternateFrame),"alternate IR publish"); alternate.tick();
        require(alternate.frames()==infrared.frames(),"rebind fixture requires colliding frame counters");
        auto* irView=irPanel->findChild<vsgraysim::gui::ScrollableImageView*>(); require(irView && bool(irView->pixelText),"IR physical readout callback");
        settle(irPanel.get(),&alternate);
        require(irView->pixelText(0,0).contains(QStringLiteral("40 W/")),"rebound equipment reused previous panel pixels at same frame counter");
        const auto firstBinding=alternate.bindingGeneration();
        alternate.unbind(); require(alternate.bind(6105,"same equipment new session"),"same id rebind");
        require(alternate.bindingGeneration()!=firstBinding,"binding generation reused");
        alternateFrame.radiance={7,7,7,7}; require(broker.publishIr(alternateFrame),"same frame identity new binding publish"); alternate.tick(); settle(irPanel.get(),&alternate);
        require(irView->pixelText(0,0).contains(QStringLiteral("7 W/")),"same equipment/id/count rebind reused previous pixels");
        irPanel->tick(nullptr); require(irView->pixelText(0,0).isEmpty(),"unbound panel retained previous pixel readout");

        EquipmentChannelSpec sarSpec; sarSpec.sensor_type=SensorType::SAR;
        EquipmentSession sar(sarSpec); require(sar.bind(6103,"SAR"),"SAR bind");
        SarFrame echo; echo.sensor_id=6103; echo.resize(2,2); echo.stage=0; echo.sample_rate_hz=30e6f;
        echo.echo_data={{1,0},{0,1},{1,1},{0,0}}; require(broker.publishSar(echo),"SAR echo publish"); sar.tick();
        auto sarPanel=createPanelFor(SensorType::SAR); settle(sarPanel.get(),&sar); settle(sarPanel.get(),&sar);
        require(labelContains(sarPanel.get(),QStringLiteral("未聚焦")),"raw SAR echo mislabeled as focused image");
        auto* isarMode=sarPanel->findChild<QCheckBox*>("isarFocusedMode");
        require(isarMode,"missing focused-image selection");isarMode->setChecked(true);sarPanel->tick(&sar);
        auto* sarView=sarPanel->findChild<vsgraysim::gui::ScrollableImageView*>();
        require(labelContains(sarPanel.get(),QStringLiteral("等待 ISAR 聚焦图像"))&&sarView->pixelText(0,0).isEmpty(),
                "raw SAR frame appeared in focused ISAR mode");
        auto focus=echo;focus.frame_id=1;focus.sim_time=.1;
        focus.isar_axes=IsarImageAxes{25,.5,3,-.25,true};
        require(!broker.publishSar(focus),"invalid raw frame accepted ISAR calibration");
        focus.stage=3;
        const auto wireBytes=wire::encodeSensorProduct(focus);
        wire::SensorProduct wireFrame;
        require(wire::decodeSensorProduct(wireBytes.data(),wireBytes.size(),wireFrame)&&wire::encodeSensorProduct(wireFrame)==wireBytes,
                "focused ISAR axes lost at the transport boundary");
        require(broker.publishSar(std::get<SarFrame>(wireFrame)),"focused image publish failed");
        sar.tick();settle(sarPanel.get(),&sar);
        require(labelContains(sarPanel.get(),QStringLiteral("ISAR 聚焦图 · 演示数据"))&&
                sarView->pixelText(1,1).contains(QStringLiteral("斜距 25.50 m · 横向距离 2.75 m")),
                "focused image axes and pixel coordinates incorrect");

        EquipmentChannelSpec radarSpec; radarSpec.sensor_type=SensorType::RADAR;
        {
            EquipmentSession radar(radarSpec); require(radar.bind(6104,"radar"),"radar bind");
            RadarFrame rd; rd.sensor_id=6104; rd.resize(4,4); rd.stage=2; rd.sample_rate_hz=3e6; rd.prf=100;
            rd.rd_map.assign(16,{1,0}); require(broker.publishRadar(rd),"RD publish"); radar.tick();
            auto radarPanel=createPanelFor(SensorType::RADAR); settle(radarPanel.get(),&radar);
            require(labelContains(radarPanel.get(),QStringLiteral("距离包络")),"radar did not default to the range profile");
            auto* radarMode=radarPanel->findChild<QComboBox*>("radarPlotMode");require(radarMode,"radar plot mode missing");
            radarMode->setCurrentIndex(2);settle(radarPanel.get(),&radar);
            require(labelContains(radarPanel.get(),QStringLiteral("Doppler")),"RD axes missing");
            auto* rdView=radarPanel->findChild<vsgraysim::gui::ScrollableImageView*>(); require(rdView && !rdView->pixelText(0,0).isEmpty(),"RD readout missing");
            radarPanel->tick(nullptr); require(rdView->pixelText(0,0).isEmpty(),"unbound radar panel retained previous product"); settle(radarPanel.get(),&radar);
            auto* ppi=radarPanel->findChild<vsgraysim::gui::PpiDisplay*>();
            require(ppi&&ppi->echoBearingCount()==1,"initial observed bearing missing");
            rd.frame_id=1;rd.sim_time=.1;rd.stage=0;rd.pulse_width=float(1./rd.sample_rate_hz);
            rd.beam_az_deg=78;rd.range_max=170;rd.rd_map[1]={2,3};
            require(broker.publishRadar(rd),"raw pulse publish");radar.tick();
            settle(radarPanel.get(),&radar);
            require(radarMode->currentIndex()==1&&ppi->echoBearingCount()==2&&ppi->sweepBearing()==78&&labelContains(radarPanel.get(),QStringLiteral("回波脉冲 1/4")),
                    "raw echo did not update its curve and observed PPI bearing");
            require(!radar.radarModeSupported() && !radar.setRadarMode(1) && broker.isEquipmentPowered(6104),"unsupported radar mode altered equipment power");
            require(radar.setPowered(false),"power request not submitted"); radar.tick(); require(!radar.powered(),"local backend power acknowledgement not reflected");
        }
        require(!broker.isSubscribed(6104),"session destruction leaked subscription");
        visual.unbind(); require(!visual.hasFrame() && visual.vis()->empty() && !broker.isSubscribed(6101),"unbind retained old imagery/subscription");
        broker.serverOnline(false); infrared.tick(); require(!infrared.connected() && !infrared.active() && infrared.stale(),"offline stream still shown live");
        broker.serverOnline(true); thermal.frame_id=1; thermal.sim_time=.2; require(broker.publishIr(thermal),"reconnected IR publish"); infrared.tick();
        require(infrared.connected() && infrared.active() && !infrared.stale() && infrared.lastFrameId()==1,"reconnected stream did not accept new measurement");
        EquipmentInfo remote; remote.equipment_id=6106; remote.type=SensorType::VIS; remote.name="remote camera"; remote.remote=true; remote.powered=true;
        EquipmentRegistry::instance().registerEquipment(remote);
        EquipmentSession remoteSession(visualSpec); require(remoteSession.bind(6106,"remote"),"remote bind"); remoteSession.tick();
        require(!remoteSession.connected(),"local broker availability impersonated remote simulation heartbeat");
        EquipmentRegistry::instance().unregisterEquipment(6106); remoteSession.tick();
        require(!remoteSession.connected() && !remoteSession.active() && !remoteSession.setPowered(false),"expired remote equipment became a local connected session");
        if(application.arguments().size()==3&&application.arguments()[1]=="--radar-capture")
            captureRadarDisplays(application.arguments()[2]);
        std::cout<<"PASS: VIS/P0 IR/P0 SAR/P2 RD sessions, units, offline state, controls and subscription ownership\n";
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
