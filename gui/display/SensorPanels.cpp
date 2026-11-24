#include "SensorPanels.h"
#include "PanelProcessing.h"
#include "RadarTerrainMap.h"
#include <QVariant>
#include "EquipmentSession.h"
#include "ScrollableImageView.h"
#include "CfarProcessor.h"
#include "TrackManager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim { namespace gui { namespace display {
namespace {
bool validSize(uint32_t w,uint32_t h) { return w && h && uint64_t(w)*h<=(1u<<24); }
void metrics(QWidget* panel,const PanelResult& out,uint64_t coalesced){
    auto values=panel->property("processingSamples").toList();values.push_back(out.processingMs);if(values.size()>600)values.removeFirst();panel->setProperty("processingSamples",values);panel->setProperty("processingMs",out.processingMs);panel->setProperty("processedFrameId",qulonglong(out.frame));panel->setProperty("processedSimTime",out.time);panel->setProperty("coalescedDisplayInputs",qulonglong(coalesced));panel->setProperty("processingIdentity",QString::fromStdString(out.identity));panel->setProperty("processingOffGui",out.workerThread!=std::hash<std::thread::id>{}(std::this_thread::get_id()));
}
QString stateText(EquipmentSession* session) { return session ? session->statusText() : QStringLiteral("未绑定数据源"); }

enum RadarView { RangeAmplitude, InPhaseQuadrature, RangeDoppler, EchoMatrix };

class PulseTimingStrip final : public QWidget {
public:
    explicit PulseTimingStrip(QWidget* parent):QWidget(parent){setFixedHeight(23);}
    void setTiming(uint32_t count,uint32_t selected,double receiveFraction){
        count_=count;selected_=selected;receive_=std::clamp(receiveFraction,0.,1.);update();
    }
private:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.fillRect(rect(),QColor("#0a121a"));
        if(!count_)return;
        const double left=10.,right=width()-10.;
        const double step=(right-left)/count_;
        p.setPen(QColor("#426275"));p.drawLine(QPointF(left,15),QPointF(right,15));
        for(uint32_t i=0;i<count_;++i){
            const double x=left+i*step;
            p.setPen(QColor(i==selected_?"#f9bc6b":"#5ca8a4"));
            p.drawLine(QPointF(x,5),QPointF(x,16));
        }
        p.setPen(QColor("#f9bc6b"));
        const auto x=left+selected_*step;
        p.drawLine(QPointF(x,19),QPointF(x+receive_*step,19));
    }
    uint32_t count_=0,selected_=0;
    double receive_=0.;
};

class RadarCurve final : public QWidget {
public:
    explicit RadarCurve(QWidget* parent):QWidget(parent) {
        setMinimumSize(240,132);
        setMouseTracking(true);
        setObjectName("radarRangeCurve");
    }
    void setTrace(std::vector<float> db,std::vector<std::complex<float>> iq,double first,double step,
                  bool showIq,bool fastTime=false,double noiseRms=0.,bool noiseScale=false) {
        db_=std::move(db);iq_=std::move(iq);first_=first;step_=step;showIq_=showIq;
        fastTime_=fastTime;noiseRms_=noiseRms;noiseScale_=noiseScale;
        cursor_=-1;
        update();
    }
private:
    static double delayUs(double slant){return slant*(2e6/299792458.);}
    size_t sampleCount()const{return showIq_?iq_.size():db_.size();}
    QRectF graph()const{return QRectF(rect()).adjusted(54,24,-26,-42);}
    void mouseMoveEvent(QMouseEvent* event)override{
        const auto area=graph();
        cursor_=!sampleCount()||area.width()<=0||!area.contains(event->position())?-1:
            int(std::lround((event->position().x()-area.left())*(sampleCount()-1)/area.width()));
        update();
    }
    void leaveEvent(QEvent*)override{cursor_=-1;update();}
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);
        painter.fillRect(rect(),QColor("#0a121a"));
        const auto area=graph();
        if(area.width()<=0||area.height()<=0)return;
        const auto count=sampleCount();
        if(!count){
            painter.setPen(QColor("#91a9ba"));
            painter.drawText(area,Qt::AlignCenter,tr("等待距离回波"));
            return;
        }
        const bool iq=showIq_;
        double upper=0,lower=-90;
        if(iq){
            double peak=0.;
            for(const auto& sample:iq_)if(std::isfinite(std::abs(sample)))peak=std::max(peak,double(std::abs(sample)));
            if(peak==0.)peak=1.;
            upper=noiseScale_&&noiseRms_>0.?3.*noiseRms_/std::sqrt(2.):peak;lower=-upper;
        }else{
            const auto max=*std::max_element(db_.begin(),db_.end());
            upper=max<=-240.?0.:std::ceil(max/10.)*10.;
            lower=upper-80.;
            if(noiseRms_>0.)lower=std::min(lower,20.*std::log10(noiseRms_)-10.);
        }
        for(int n=0;n<=4;++n){
            const double y=area.top()+n*area.height()/4.;
            const double x=area.left()+n*area.width()/4.;
            painter.setPen(QColor("#243746"));
            painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
            painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
            painter.setPen(QColor("#9eb8c7"));
            painter.drawText(QRectF(2,y-9,area.left()-8,18),Qt::AlignRight|Qt::AlignVCenter,
                             QString::number(upper-n*(upper-lower)/4.,'g',3));
            const auto range=first_+(count-1)*step_*n/4.;
            painter.drawText(QRectF(x-26,area.bottom()+3,52,17),Qt::AlignCenter,
                             QString::number(fastTime_?delayUs(range):range,'g',4));
        }
        painter.drawText(QRectF(area.left(),2,area.width(),18),Qt::AlignRight|Qt::AlignVCenter,
                         area.width()<200.? (fastTime_?tr("µs"):tr("m")):
                                             (fastTime_?tr("快时间 / 往返时延 (µs)"):tr("斜距 (m)")));
        if(iq){
            painter.setPen(QColor("#7ce5e0"));
            painter.drawText(QPointF(area.left(),17),tr("I 同相"));
            painter.setPen(QColor("#ffc275"));
            painter.drawText(QPointF(area.left()+60,17),tr("Q 正交"));
            painter.setPen(QColor("#566d7e"));
            painter.drawLine(QPointF(area.left(),area.center().y()),QPointF(area.right(),area.center().y()));
        }else painter.drawText(QPointF(area.left(),17),tr("相对幅度 (dB)"));
        if(noiseRms_>0.){
            const auto drawNoise=[&](double value){
                if(value<lower||value>upper)return;
                const double y=area.bottom()-(value-lower)/(upper-lower)*area.height();
                painter.setPen(QPen(QColor("#a88fbd"),1.,Qt::DashLine));
                painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
            };
            if(iq){const auto sigma=noiseRms_/std::sqrt(2.);drawNoise(sigma);drawNoise(-sigma);}
            else drawNoise(20.*std::log10(noiseRms_));
        }
        auto drawTrace=[&](auto value,const QColor& color){
            QPainterPath path;
            for(size_t i=0;i<count;++i){
                const double datum=value(i);
                const auto scaled=std::isfinite(datum)?std::clamp((datum-lower)/(upper-lower),0.,1.):0.;
                const QPointF point(area.left()+i*area.width()/std::max(size_t(1),count-1),area.bottom()-scaled*area.height());
                if(i==0)path.moveTo(point);else path.lineTo(point);
            }
            painter.setPen(QPen(color,1.6));
            painter.drawPath(path);
        };
        painter.setRenderHint(QPainter::Antialiasing);
        if(iq){
            drawTrace([&](size_t i){return iq_[i].real();},QColor("#7ce5e0"));
            drawTrace([&](size_t i){return iq_[i].imag();},QColor("#ffc275"));
        }else drawTrace([&](size_t i){return db_[i];},QColor("#ffc275"));
        if(cursor_>=0&&size_t(cursor_)<count){
            const auto x=area.left()+cursor_*area.width()/std::max(size_t(1),count-1);
            painter.setPen(QPen(QColor("#7ce5e0"),1,Qt::DashLine));
            painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
            const auto value=iq?tr("I %1  Q %2").arg(iq_[cursor_].real(),0,'g',4).arg(iq_[cursor_].imag(),0,'g',4):
                                tr("%1 dB").arg(db_[cursor_],0,'f',1);
            const auto range=first_+cursor_*step_;
            painter.drawText(area.adjusted(5,7,-5,0),Qt::AlignRight|Qt::AlignTop,
                             tr("#%1 · %2 µs · %3 m\n%4").arg(cursor_).arg(delayUs(range),0,'f',3).arg(range,0,'f',1).arg(value));
        }
    }
    std::vector<float> db_;
    std::vector<std::complex<float>> iq_;
    double first_=0,step_=0;
    bool showIq_=false;
    bool fastTime_=false,noiseScale_=false;
    double noiseRms_=0.;
    int cursor_=-1;
};
}

class RadarDisplayPanel::Impl {
public:
    CfarProcessor cfar;
    uint64_t parameters=0;
    PanelProcessor processor;
    PpiDisplay* ppi=nullptr;
    QTabWidget* mapTabs=nullptr;
    RadarTerrainMap* terrain=nullptr;
    QLabel* terrainState=nullptr;
    QCheckBox* ppiChart=nullptr;
    QLabel* ppiChartState=nullptr;
    ScrollableImageView* rd=nullptr;
    QLabel* status=nullptr;
    QLabel* axes=nullptr;
    QComboBox* representation=nullptr;
    QSpinBox* pulse=nullptr;
    QComboBox* timeAxis=nullptr;
    QCheckBox* noiseZoom=nullptr;
    PulseTimingStrip* timing=nullptr;
    QLabel* timingReadout=nullptr;
    QStackedWidget* plots=nullptr;
    RadarCurve* curve=nullptr;
    QWidget* detectionControls=nullptr;
    uint64_t revision=~0ull;
    uint64_t binding=0;
    uint32_t equipment=0;
    std::vector<float> displayDb;
    std::vector<std::complex<float>> iq;
    int width=0,height=0;
    double prf=0,rangeMin=0,rangeStep=0;
    bool rdStage=false;
    bool echoRangeInitialized=false;
    std::optional<uint8_t> productStage;
    void clearPresentation() {
        ++parameters;revision=~0ull; width=height=0;
        displayDb.clear(); iq.clear(); cfar.reset(); rdStage=false;
        ppi->setPowered(false); ppi->setTargets({}); ppi->clearEcho();
        terrain->clearStation();productStage.reset();
        ppi->setGroundLayer({});ppiChartState->setText(QStringLiteral("等待地物散射"));
        echoRangeInitialized=false;
        timing->hide();timingReadout->hide();
        curve->setTrace({},{},0,0,false);
        rd->clear(); axes->setText(QStringLiteral("等待回波数据"));
    }
};
RadarDisplayPanel::RadarDisplayPanel(QWidget* parent):DisplayPanel(parent),d_(new Impl) {
    auto* root=new QVBoxLayout(this); root->setContentsMargins(12,12,12,12); root->setSpacing(8);
    auto* tools=new QHBoxLayout;
    d_->representation=new QComboBox(this);
    d_->representation->setObjectName("radarPlotMode");
    d_->representation->addItems({tr("A 扫幅度"),tr("I/Q 曲线"),tr("距离–多普勒"),tr("原始 I/Q 矩阵")});
    tools->addWidget(d_->representation);
    connect(d_->representation,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{++d_->parameters;setProperty("processedFrameId",QVariant{});});
    d_->pulse=new QSpinBox(this);
    d_->pulse->setObjectName("radarPulse");
    d_->pulse->setRange(1,1);d_->pulse->setPrefix(tr("脉冲 "));
    tools->addWidget(d_->pulse);
    connect(d_->pulse,qOverload<int>(&QSpinBox::valueChanged),this,[this]{++d_->parameters;});
    d_->timeAxis=new QComboBox(this);d_->timeAxis->addItems({tr("快时间"),tr("斜距")});tools->addWidget(d_->timeAxis);
    d_->noiseZoom=new QCheckBox(tr("噪声细看"),this);
    d_->noiseZoom->setObjectName("radarNoiseZoom");
    d_->noiseZoom->setToolTip(tr("纵轴放大到每路噪声标准差的 ±3 倍，大回波可超出显示刻度。虚线标记每路噪声标准差。"));
    tools->addWidget(d_->noiseZoom);
    connect(d_->timeAxis,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{++d_->parameters;});
    connect(d_->noiseZoom,&QCheckBox::toggled,this,[this]{++d_->parameters;});
    d_->detectionControls=new QWidget(this);
    auto* detectionLayout=new QHBoxLayout(d_->detectionControls);
    detectionLayout->setContentsMargins(0,0,0,0);
    detectionLayout->addWidget(new QLabel(tr("本地 CA-CFAR 门限"),d_->detectionControls));
    auto* threshold=new QDoubleSpinBox(d_->detectionControls); threshold->setObjectName("localCfarThreshold");
    threshold->setRange(3,60); threshold->setValue(d_->cfar.params().threshold_db); threshold->setSuffix(tr(" dB"));
    threshold->setToolTip(tr("只影响此工作台的检测与航迹分析，不修改仿真发射参数。参考窗在线性功率域平均，峰值以下 100 dB 的数值尾迹不参与检测；这不是接收机噪声标定。")); detectionLayout->addWidget(threshold);
    connect(threshold,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){auto parameters=d_->cfar.params(); parameters.threshold_db=float(value); d_->cfar.setParams(parameters); ++d_->parameters; d_->revision=~0ull;});
    detectionLayout->addWidget(new QLabel(tr("波束中心投影 · 非测角解算"),d_->detectionControls));
    tools->addWidget(d_->detectionControls);tools->addStretch(); root->addLayout(tools);
    auto* splitter=new QSplitter(Qt::Horizontal,this);
    d_->mapTabs=new QTabWidget(splitter);d_->mapTabs->setObjectName("radarMapTabs");
    auto* ppiPane=new QWidget(d_->mapTabs);auto* ppiLayout=new QVBoxLayout(ppiPane);ppiLayout->setContentsMargins(0,0,0,0);
    d_->ppi=new PpiDisplay(ppiPane); d_->ppi->setMinimumSize(220,164); d_->ppi->setSweepSpeed(0); d_->ppi->setPowered(false);
    ppiLayout->addWidget(d_->ppi,1);
    auto* layers=new QHBoxLayout;
    d_->ppiChart=new QCheckBox(tr("叠加地物散射"),ppiPane);d_->ppiChart->setObjectName("radarPpiTerrainEnabled");d_->ppiChart->setChecked(true);layers->addWidget(d_->ppiChart);
    d_->ppiChartState=new QLabel(tr("等待地物散射"),ppiPane);d_->ppiChartState->setWordWrap(true);layers->addWidget(d_->ppiChartState,1);
    connect(d_->ppiChart,&QCheckBox::toggled,d_->ppi,&PpiDisplay::setGroundVisible);
    ppiLayout->addLayout(layers);
    d_->mapTabs->addTab(ppiPane,tr("PPI 回波"));
    auto* mapPane=new QWidget(d_->mapTabs);
    auto* mapLayout=new QVBoxLayout(mapPane);mapLayout->setContentsMargins(0,0,0,0);
    d_->terrain=new RadarTerrainMap(mapPane);mapLayout->addWidget(d_->terrain,1);
    d_->terrainState=new QLabel(tr("等待场景底图"),mapPane);d_->terrainState->setWordWrap(true);mapLayout->addWidget(d_->terrainState);
    d_->mapTabs->addTab(mapPane,tr("地理参考"));
    auto* imageArea=new QWidget(splitter); auto* right=new QVBoxLayout(imageArea); right->setContentsMargins(0,0,0,0);
    d_->plots=new QStackedWidget(imageArea);
    d_->curve=new RadarCurve(d_->plots);d_->plots->addWidget(d_->curve);
    d_->rd=new ScrollableImageView(d_->plots); d_->rd->setMinimumSize(240,132);d_->plots->addWidget(d_->rd);
    d_->rd->setToolTip(tr("滚轮缩放，拖动平移；十字光标显示距离、多普勒与相对幅度。"));
    d_->rd->pixelText=[this](int x,int y) {
        if(x<0 || y<0 || x>=d_->width || y>=d_->height) return QString();
        const auto index=size_t(y)*d_->width+x;
        if(!d_->rdStage) {
            if(!d_->iq.empty())return tr("R %1 m · 脉冲 %2 · I %3 · Q %4").arg(d_->rangeMin+x*d_->rangeStep,0,'f',1).arg(y).arg(d_->iq[index].real(),0,'g',6).arg(d_->iq[index].imag(),0,'g',6);
            if(index>=d_->displayDb.size())return QString{};
            return tr("bin %1 / row %2 · %3 dB (相对)").arg(x).arg(y).arg(d_->displayDb[index],0,'f',1);
        }
        if(index>=d_->displayDb.size())return QString{};
        const double range=d_->rangeMin+x*d_->rangeStep;
        const double doppler=(y-d_->height/2)*(d_->prf/d_->height);
        return tr("R %1 m · fD %2 Hz · %3 dB (相对)").arg(range,0,'f',1).arg(doppler,0,'f',1).arg(d_->displayDb[size_t(y)*d_->width+x],0,'f',1);
    };
    right->addWidget(d_->plots,1);
    d_->timing=new PulseTimingStrip(imageArea);d_->timing->hide();right->addWidget(d_->timing);
    d_->timingReadout=new QLabel(imageArea);d_->timingReadout->setWordWrap(true);d_->timingReadout->hide();right->addWidget(d_->timingReadout);
    d_->axes=new QLabel(tr("等待回波数据"),imageArea);d_->axes->setWordWrap(true);right->addWidget(d_->axes);
    splitter->addWidget(d_->mapTabs); splitter->addWidget(imageArea); splitter->setStretchFactor(0,1); splitter->setStretchFactor(1,1);
    root->addWidget(splitter,1); d_->status=new QLabel(tr("等待测量数据"),this); d_->status->setWordWrap(true); root->addWidget(d_->status);
}
RadarDisplayPanel::~RadarDisplayPanel()=default;
void RadarDisplayPanel::setEarthMap(const std::string& path){d_->terrain->setSource(path);}
void RadarDisplayPanel::tick(EquipmentSession* session) {
    if(!session||!session->bound()){if(d_->binding||d_->width)d_->clearPresentation();d_->binding=0;d_->equipment=0;d_->status->setText(stateText(nullptr));return;}
    if(d_->binding!=session->bindingGeneration()||d_->equipment!=session->equipmentId()){d_->clearPresentation();setProperty("processedFrameId",QVariant{});d_->binding=session->bindingGeneration();d_->equipment=session->equipmentId();}
    d_->ppi->setPowered(session->hasFrame()&&session->connected()&&session->powered()&&!session->stale());
    if(!session->hasFrame()&&d_->width)d_->clearPresentation();
    if(!session->hasFrame()||!session->connected()||!session->powered()||session->stale()){d_->status->setText(stateText(session));return;}
    if(!isVisible()&&window()->isVisible())return;
    const auto* frame=session->radar();if(!frame||!validSize(frame->range_bins,frame->doppler_bins))return;
    const bool raw=frame->stage==0, rd=frame->stage==2;
    const bool canTransform=raw&&frame->sample_rate_hz>0&&std::abs(frame->pulse_width*frame->sample_rate_hz-1.)<1e-5&&
                            frame->doppler_bins>=2&&frame->doppler_bins<=256&&!(frame->doppler_bins&(frame->doppler_bins-1))&&frame->range_bins<=8192;
    auto* choices=qobject_cast<QStandardItemModel*>(d_->representation->model());
    choices->item(RangeAmplitude)->setEnabled(raw||rd);
    choices->item(InPhaseQuadrature)->setEnabled(raw);
    choices->item(EchoMatrix)->setEnabled(!rd);
    choices->item(EchoMatrix)->setText(raw?tr("原始 I/Q 矩阵"):tr("复数幅度矩阵"));
    choices->item(RangeDoppler)->setEnabled(rd||canTransform);
    if(d_->productStage!=frame->stage){
        d_->productStage=frame->stage;
        d_->representation->setCurrentIndex(raw?InPhaseQuadrature:rd?RangeAmplitude:EchoMatrix);
    }
    if(!choices->item(d_->representation->currentIndex())->isEnabled())d_->representation->setCurrentIndex(raw||rd?RangeAmplitude:EchoMatrix);
    const auto mode=d_->representation->currentIndex();
    const bool showImage=mode==RangeDoppler||mode==EchoMatrix;
    d_->pulse->setVisible(raw&&!showImage);d_->pulse->setMaximum(raw?int(frame->doppler_bins):1);
    d_->timeAxis->setVisible(mode==InPhaseQuadrature&&frame->sample_rate_hz>0);
    d_->noiseZoom->setVisible(mode==InPhaseQuadrature&&frame->receiver&&frame->receiver->noise_iq_rms>0);
    d_->plots->setCurrentIndex(showImage?1:0);
    if(d_->mapTabs->currentIndex()==1){
        d_->terrain->updateStation(*frame);
        const auto state=d_->terrain->property("terrainState").toString();
        if(state!=d_->terrainState->text())d_->terrainState->setText(state);
    }
    d_->detectionControls->setVisible(frame->stage==2);
    const auto identity=std::to_string(d_->binding)+"/"+std::to_string(d_->equipment)+"/"+std::to_string(d_->parameters);
    PanelInput input;input.identity=identity;input.sequence=session->frames();input.equipment=d_->equipment;input.type=SensorType::RADAR;input.cfar=d_->cfar.params();input.showRadarImage=showImage;input.showSpectrum=mode==RangeDoppler;input.pulseIndex=uint32_t(d_->pulse->value()-1);input.radar=session->radarSnapshot();if(!input.radar)input.radar=std::make_shared<RadarFrame>(*frame);d_->processor.submit(std::move(input));
    if(auto result=d_->processor.take();result&&result->identity==identity){
        if(!result->error.empty()){d_->status->setText(QString::fromStdString(result->error));return;}
        auto& out=*result;d_->revision=out.sequence;d_->displayDb=std::move(out.values);d_->iq=std::move(out.iq);d_->width=int(out.width);d_->height=int(out.height);d_->prf=out.prf;d_->rangeMin=out.rangeMin;d_->rangeStep=out.rangeStep;d_->rdStage=out.stage==2||out.localSpectrum;d_->rd->setImage(out.image);
        const double noise=out.stage==0&&out.receiver?out.receiver->noise_iq_rms:0.;
        d_->curve->setTrace(std::move(out.curveDb),std::move(out.curveIq),out.rangeMin,out.rangeStep,mode==InPhaseQuadrature,
                           mode==InPhaseQuadrature&&out.sampleRate>0&&d_->timeAxis->currentIndex()==0,noise,d_->noiseZoom->isChecked());
        const bool timing=out.stage==0&&out.receiver&&out.prf>0.&&out.sampleRate>0.;
        d_->timing->setVisible(timing);d_->timingReadout->setVisible(timing);
        if(timing){
            const auto selected=uint32_t(d_->pulse->value()-1);
            d_->timing->setTiming(out.height,selected,out.width*out.prf/out.sampleRate);
            const auto tx=out.receiver->first_transmit_s+selected/out.prf;
            d_->timingReadout->setText(tr("PRI %1 ms · 脉宽 %2 ns · Tx %3 s\n采样 %4 MS/s · I/Q 噪声 RMS %5")
                .arg(1000./out.prf,0,'g',5).arg(out.pulseWidth*1e9,0,'g',4).arg(tx,0,'f',6)
                .arg(out.sampleRate/1e6,0,'g',5).arg(noise,0,'g',4));
            const double firstDelay=out.rangeMin*(2e6/299792458.);
            d_->timingReadout->setToolTip(tr("接收采样 %1–%2 µs · 自由空间往返时延\n脉冲等效模型 · 帧时刻为 CPI 结束时刻 · 噪声为相对复基带量")
                .arg(firstDelay,0,'f',3).arg(firstDelay+(out.width-1)*1e6/out.sampleRate,0,'f',3));
        }
        if(!out.radialDb.empty()&&out.rangeStep>0){
            d_->ppi->setRangeScale(std::max(.01,out.rangeGateMax*.001));
            d_->ppi->setEchoRadial(out.beam,out.rangeMin*.001,out.rangeStep*.001,std::move(out.radialDb),out.time);
            d_->echoRangeInitialized=true;
        }
        d_->ppi->setTargets(out.tracks);
        const auto groundCount=out.ground?out.ground->size():0;
        const auto groundStatus=out.ground?(groundCount?tr("仿真地物散射 · %1 点").arg(groundCount):tr("本帧无地物散射")):
            tr("通道未发布地物散射");
        if(d_->ppiChartState->text()!=groundStatus)d_->ppiChartState->setText(groundStatus);
        d_->ppi->setGroundLayer(out.ground?std::move(*out.ground):std::vector<RadarFrame::GroundSample>{});
        QString axes;
        if(mode==RangeDoppler)axes=tr("距离 %1–%2 m · Doppler %3…%4 Hz").arg(out.rangeMin,0,'f',1).arg(out.rangeMin+(out.width-1)*out.rangeStep,0,'f',1).arg(-out.prf*.5,0,'f',1).arg(out.prf*(int(out.height)-1-int(out.height/2))/out.height,0,'f',1);
        else if(mode==EchoMatrix)axes=raw?tr("原始 I/Q · 快时间 × 脉冲"):tr("P%1 复数幅度矩阵").arg(out.stage);
        else axes=raw?tr("回波脉冲 %1/%2 · 距离门 %3–%4 m").arg(d_->pulse->value()).arg(out.height).arg(out.rangeMin,0,'f',1).arg(out.rangeGateMax,0,'f',1):
                      tr("距离包络 · 各距离单元的多普勒峰值");
        if(d_->echoRangeInitialized)axes+=tr(" · PPI 方位 %1° · 已观测 %2/360").arg(d_->ppi->sweepBearing(),0,'f',1).arg(d_->ppi->echoBearingCount());
        d_->axes->setText(axes);
        metrics(this,out,d_->processor.coalesced());setProperty("processedSummary",tr("P%1 · 帧 %2 · t=%3 s · %4×%5 · 后台 %6 ms").arg(out.stage).arg(out.frame).arg(out.time,0,'f',3).arg(out.width).arg(out.height).arg(out.processingMs,0,'f',2));
    }
    d_->status->setText(stateText(session)+" · "+property("processedSummary").toString());
}

class ImageDisplayPanel::Impl {
public:
    SensorType type=SensorType::VIS;
    uint64_t parameters=0;
    PanelProcessor processor;
    ScrollableImageView* image=nullptr;
    QLabel* status=nullptr;
    QLabel* imageAxes=nullptr;
    QWidget* isarScale=nullptr;
    QLabel* isarScaleCaption=nullptr;
    QLabel* isarScaleBar=nullptr;
    QSpinBox* isarSpan=nullptr;
    QCheckBox* contrast=nullptr;
    QCheckBox* logarithmic=nullptr;
    QCheckBox* spectrum=nullptr;
    QCheckBox* isar=nullptr;
    QComboBox* palette=nullptr;
    uint64_t revision=~0ull;
    uint64_t binding=0;
    uint32_t equipment=0;
    std::vector<float> values;
    std::vector<std::complex<float>> iq;
    std::optional<IsarImageAxes> isarAxes;
    double isarPeak=0.;
    uint32_t width=0,height=0;
    QString units;
    QString summary;
    void clearPresentation() {
        ++parameters;revision=~0ull; width=height=0; values.clear(); iq.clear(); isarAxes.reset();
        units.clear(); summary.clear(); image->clear();imageAxes->hide();isarScale->hide();isarPeak=0.;
    }
};
ImageDisplayPanel::ImageDisplayPanel(SensorType type,QWidget* parent,bool compact):DisplayPanel(parent),d_(new Impl) {
    d_->type=type;
    auto* root=new QVBoxLayout(this); root->setContentsMargins(compact?2:12,compact?2:12,compact?2:12,compact?2:12); root->setSpacing(compact?2:8);
    auto* tools=new QHBoxLayout;
    d_->contrast=new QCheckBox(tr("自动拉伸（仅显示）"),this); d_->contrast->setChecked(type!=SensorType::VIS); tools->addWidget(d_->contrast);
    d_->logarithmic=new QCheckBox(tr("对数幅度"),this); d_->logarithmic->setChecked(type==SensorType::SAR); d_->logarithmic->setVisible(type==SensorType::SAR); tools->addWidget(d_->logarithmic);
    d_->spectrum=new QCheckBox(tr("距离–多普勒预览"),this);d_->spectrum->setVisible(type==SensorType::SAR);tools->addWidget(d_->spectrum);
    d_->isar=new QCheckBox(tr("仅看 ISAR 聚焦图"),this);d_->isar->setObjectName("isarFocusedMode");d_->isar->setVisible(type==SensorType::SAR);tools->addWidget(d_->isar);
    d_->palette=new QComboBox(this); d_->palette->addItems({tr("灰度"),tr("热色")}); tools->addWidget(d_->palette);
    auto* crosshair=new QCheckBox(tr("十字光标"),this); crosshair->setChecked(true); tools->addWidget(crosshair);
    auto* roi=new QCheckBox(tr("ROI"),this); tools->addWidget(roi); tools->addStretch(); root->addLayout(tools);
    d_->image=new ScrollableImageView(this); d_->image->setMinimumSize(compact?1:240,compact?1:180);
    d_->image->setToolTip(tr("滚轮缩放、拖动平移；像素读数保留原始物理值。")); root->addWidget(d_->image,1);
    d_->imageAxes=new QLabel(this);d_->imageAxes->setWordWrap(true);d_->imageAxes->hide();root->addWidget(d_->imageAxes);
    d_->isarScale=new QWidget(this);auto* scale=new QHBoxLayout(d_->isarScale);scale->setContentsMargins(0,0,0,0);
    d_->isarScaleCaption=new QLabel(d_->isarScale);scale->addWidget(d_->isarScaleCaption);
    d_->isarScaleBar=new QLabel(d_->isarScale);d_->isarScaleBar->setFixedHeight(9);scale->addWidget(d_->isarScaleBar,1);
    scale->addWidget(new QLabel(tr("0 dB 峰值"),d_->isarScale));
    d_->isarSpan=new QSpinBox(d_->isarScale);d_->isarSpan->setRange(12,90);d_->isarSpan->setValue(45);
    d_->isarSpan->setPrefix(tr("动态范围 "));d_->isarSpan->setSuffix(tr(" dB"));scale->addWidget(d_->isarSpan);
    root->addWidget(d_->isarScale);d_->isarScale->hide();
    d_->image->pixelText=[this](int x,int y) {
        if(x<0 || y<0 || uint32_t(x)>=d_->width || uint32_t(y)>=d_->height) return QString();
        const auto index=size_t(y)*d_->width+x;
        if(d_->isarAxes){
            const auto& axes=*d_->isarAxes;
            const auto value=d_->values[index];
            auto reading=tr("斜距 %1 m · 横向距离 %2 m · 散射幅度 %3")
                .arg(axes.range_origin_m+x*axes.range_spacing_m,0,'f',2)
                .arg(axes.cross_range_origin_m+y*axes.cross_range_spacing_m,0,'f',2).arg(value,0,'g',6);
            if(d_->isarPeak>0.&&value>0.)reading+=tr(" · %1 dB 相对峰值").arg(20.*std::log10(value/d_->isarPeak),0,'f',1);
            return reading;
        }
        if(!d_->iq.empty()) return tr("(%1, %2) · I %3 · Q %4 · |IQ| %5").arg(x).arg(y).arg(d_->iq[index].real(),0,'g',6).arg(d_->iq[index].imag(),0,'g',6).arg(d_->values[index],0,'g',6);
        return tr("(%1, %2) · %3 %4").arg(x).arg(y).arg(d_->values[index],0,'g',6).arg(d_->units);
    };
    connect(d_->image,&ScrollableImageView::cursorMoved,this,[this](int x,int y){
        d_->image->setToolTip(x<0||y<0?tr("滚轮缩放，拖动平移"):d_->image->pixelText(x,y));
    });
    connect(crosshair,&QCheckBox::toggled,d_->image,&ScrollableImageView::setCrosshairEnabled);
    connect(roi,&QCheckBox::toggled,d_->image,&ScrollableImageView::setRoiEnabled);
    auto refresh=[this]{++d_->parameters;d_->revision=~0ull;};
    connect(d_->contrast,&QCheckBox::toggled,this,refresh); connect(d_->logarithmic,&QCheckBox::toggled,this,refresh);
    connect(d_->spectrum,&QCheckBox::toggled,this,refresh);
    connect(d_->isar,&QCheckBox::toggled,this,[this]{d_->clearPresentation();});
    connect(d_->palette,qOverload<int>(&QComboBox::currentIndexChanged),this,refresh);
    connect(d_->isarSpan,qOverload<int>(&QSpinBox::valueChanged),this,refresh);
    d_->status=new QLabel(tr("等待测量数据"),this); d_->status->setWordWrap(true); root->addWidget(d_->status);
    if(compact){for(int n=0;n<tools->count();++n)if(auto* item=tools->itemAt(n)->widget())item->hide();tools->setContentsMargins(0,0,0,0);d_->status->setStyleSheet("font-size:9px;color:#bedacb;");d_->status->setMaximumHeight(42);setStyleSheet("background:#070c10;");}
}
void ImageDisplayPanel::tick(EquipmentSession* session) {
    if(!session||!session->bound()){if(d_->binding||d_->width)d_->clearPresentation();d_->binding=0;d_->equipment=0;d_->status->setText(stateText(nullptr));return;}
    if(d_->binding!=session->bindingGeneration()||d_->equipment!=session->equipmentId()){d_->clearPresentation();setProperty("processedFrameId",QVariant{});d_->binding=session->bindingGeneration();d_->equipment=session->equipmentId();}
    if(!session->hasFrame()&&d_->width)d_->clearPresentation();
    if(!session->hasFrame()||!session->connected()||!session->powered()||session->stale()){d_->status->setText(stateText(session));return;}
    if(!isVisible()&&window()->isVisible())return;
    const auto identity=std::to_string(d_->binding)+"/"+std::to_string(d_->equipment)+"/"+std::to_string(d_->parameters);
    PanelInput input;input.identity=identity;input.sequence=session->frames();input.type=d_->type;input.equipment=d_->equipment;input.stretch=d_->contrast->isChecked();input.thermal=d_->palette->currentIndex()==1;input.logarithmic=d_->logarithmic->isChecked();input.showSpectrum=d_->spectrum->isChecked();input.focusedIsarOnly=d_->isar->isChecked();
    input.isarDynamicRangeDb=d_->isarSpan->value();
    if(d_->type==SensorType::IR){input.ir=session->irSnapshot();if(!input.ir&&session->ir())input.ir=std::make_shared<IrFrame>(*session->ir());}
    if(d_->type==SensorType::VIS){input.vis=session->visSnapshot();if(!input.vis&&session->vis())input.vis=std::make_shared<VisFrame>(*session->vis());}
    if(d_->type==SensorType::SAR){
        input.sar=session->sarSnapshot();if(!input.sar&&session->sar())input.sar=std::make_shared<SarFrame>(*session->sar());
        if(input.focusedIsarOnly&&(!input.sar||!input.sar->hasFocusedIsar())){
            if(d_->width)d_->clearPresentation();
            d_->status->setText(tr("等待 ISAR 聚焦图像 · 当前通道没有距离–横向距离图像"));
            return;
        }
    }
    d_->processor.submit(std::move(input));
    if(auto result=d_->processor.take();result&&result->identity==identity){
        if(!result->error.empty()){d_->status->setText(QString::fromStdString(result->error));return;}
        auto& out=*result;d_->revision=out.sequence;d_->width=out.width;d_->height=out.height;d_->values=std::move(out.values);d_->iq=std::move(out.iq);d_->isarAxes=out.isarAxes;
        d_->image->setPixelAspect(out.isarAxes?std::abs(out.isarAxes->cross_range_spacing_m)/out.isarAxes->range_spacing_m:1.);
        std::optional<std::pair<ScrollableImageView::Axis,ScrollableImageView::Axis>> axes;
        if(out.isarAxes){
            const auto& a=*out.isarAxes;
            axes=std::pair{ScrollableImageView::Axis{a.range_origin_m,a.range_spacing_m,tr("斜距 (m)")},
                           ScrollableImageView::Axis{a.cross_range_origin_m,a.cross_range_spacing_m,tr("横向距离 (m)")}};
        }
        d_->image->setAxes(std::move(axes));
        d_->image->setImage(out.image);
        d_->imageAxes->setVisible(out.isarAxes.has_value());
        d_->isarPeak=out.maximum;
        d_->isarScale->setVisible(out.isarAxes&&d_->logarithmic->isChecked());
        QString kind;if(out.isarAxes){
            const auto& a=*out.isarAxes;
            kind=a.illustrative?tr("ISAR 聚焦图 · 演示数据"):tr("ISAR 聚焦图");
            d_->units=tr("相对散射幅度");
            d_->imageAxes->setText(tr("斜距 %1–%2 m · 横向距离 %3 → %4 m · 像素间距 %5 × %6 m")
                .arg(a.range_origin_m,0,'f',2).arg(a.range_origin_m+(out.width-1)*a.range_spacing_m,0,'f',2)
                .arg(a.cross_range_origin_m,0,'f',2).arg(a.cross_range_origin_m+(out.height-1)*a.cross_range_spacing_m,0,'f',2)
                .arg(a.range_spacing_m,0,'g',5).arg(std::abs(a.cross_range_spacing_m),0,'g',5));
            d_->isarScaleCaption->setText(tr("−%1 dB").arg(d_->isarSpan->value()));
            d_->isarScaleBar->setStyleSheet(d_->palette->currentIndex()==0?
                "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 black,stop:1 white);":
                "background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 black,stop:0.333 red,stop:0.667 yellow,stop:1 white);");
        }else if(d_->type==SensorType::SAR){kind=out.localSpectrum?tr("SAR 距离–多普勒预览（未聚焦）"):out.stage==0?tr("SAR 原始回波 · 快时间 × 脉冲（未聚焦）"):out.stage==1?tr("SAR 距离压缩（未聚焦）"):out.stage==2?tr("SAR 距离-多普勒（未聚焦）"):tr("SAR 复数产品（未声明聚焦）");d_->units=tr("|IQ| 相对幅度");}
        else {kind=d_->type==SensorType::IR?tr("红外"):tr("可见光");d_->units=out.stage<=2?tr("W/m²/sr"):out.stage==3?tr("满阱比例"):tr("归一化 DN");}
        d_->summary=tr("%1 · P%2 · 帧 %3 · t=%4 s · %5×%6 · [%7, %8] %9 · 后台 %10 ms").arg(kind).arg(out.stage).arg(out.frame).arg(out.time,0,'f',3).arg(out.width).arg(out.height).arg(out.minimum,0,'g',5).arg(out.maximum,0,'g',5).arg(d_->units).arg(out.processingMs,0,'f',2);
        if(out.fovRadians>0&&out.exposureSeconds>0)
            d_->summary+=tr(" · 水平视场 %1° · 积分 %2 ms").arg(out.fovRadians*180/3.141592653589793,0,'f',1).arg(out.exposureSeconds*1000,0,'f',1);
        if(out.bandMaxMicrons>out.bandMinMicrons&&out.bandMinMicrons>0)
            d_->summary+=tr(" · %1–%2 μm").arg(out.bandMinMicrons,0,'g',3).arg(out.bandMaxMicrons,0,'g',3);
        metrics(this,out,d_->processor.coalesced());
    }
    d_->status->setText(stateText(session)+" · "+d_->summary);
}
ImageDisplayPanel::~ImageDisplayPanel()=default;
std::unique_ptr<DisplayPanel> createPanelFor(SensorType type,QWidget* parent) {
    switch(type) {
    case SensorType::RADAR: return std::make_unique<RadarDisplayPanel>(parent);
    case SensorType::IR: case SensorType::SAR: case SensorType::VIS: return std::make_unique<ImageDisplayPanel>(type,parent);
    case SensorType::LASER: return {};
    }
    return {};
}
}}}
