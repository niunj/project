#include "PpiDisplay.h"
#include "base/PaintTiming.h"

#include <QPainter>
#include <QMouseEvent>
#include <QRadialGradient>
#include <QColor>
#include <QFont>
#include <QtMath>

#include <cmath>
#include <algorithm>

namespace vsgraysim { namespace gui {

namespace {
constexpr int echoSize=320;
constexpr double echoLifetime=16.;
int bearingBin(double degrees) {
    return int(std::floor(std::fmod(std::fmod(degrees,360.)+360.,360.)+.5))%360;
}
struct PolarPixel { float fraction; uint16_t bearing; };
const std::vector<PolarPixel>& polarPixels() {
    static const auto pixels=[]{
        std::vector<PolarPixel> result(echoSize*echoSize);
        for(int y=0;y<echoSize;++y)for(int x=0;x<echoSize;++x){
            const double e=(x+.5)/(echoSize*.5)-1.,n=1.-(y+.5)/(echoSize*.5);
            result[y*echoSize+x]={float(std::hypot(e,n)),uint16_t(bearingBin(qRadiansToDegrees(std::atan2(e,n))))};
        }
        return result;
    }();
    return pixels;
}
} // anonymous namespace

PpiDisplay::PpiDisplay(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(360, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    sweep_timer_.setInterval(16); // ~60 Hz
    connect(&sweep_timer_, &QTimer::timeout, this, &PpiDisplay::onSweepTick);
    sweep_timer_.start();
}

void PpiDisplay::setRangeScale(double km)
{
    if (!std::isfinite(km)||km<=0.0||km==range_scale_km_) return;
    range_scale_km_ = km;
    echo_dirty_=true;
    ground_dirty_=true;
    update();
}

void PpiDisplay::clearEcho()
{
    echo_layer_ = QImage();
    radials_={};
    latest_echo_time_=0;
    echo_dirty_=false;
    update();
}

void PpiDisplay::setEchoRadial(double bearingDeg,double firstRangeKm,double stepKm,std::vector<float> db,double simTime) {
    if(!std::isfinite(bearingDeg)||!std::isfinite(firstRangeKm)||firstRangeKm<0||
       !std::isfinite(stepKm)||stepKm<=0||!std::isfinite(simTime)||db.empty())return;
    if(simTime<latest_echo_time_)clearEcho();
    float peak=-240.f;
    for(float level:db)if(std::isfinite(level))peak=std::max(peak,level);
    radials_[bearingBin(bearingDeg)]={std::move(db),firstRangeKm,stepKm,simTime,peak};
    latest_echo_time_=simTime;
    echo_dirty_=true;
    notifyFrameSweep(bearingDeg);
}

size_t PpiDisplay::echoBearingCount() const {
    return std::count_if(radials_.begin(),radials_.end(),[this](const auto& radial){
        return !radial.db.empty() && latest_echo_time_-radial.time<echoLifetime;
    });
}

void PpiDisplay::setTargets(const std::vector<PpiTarget>& targets)
{
    targets_ = targets;
    update();
}

void PpiDisplay::setGroundLayer(std::vector<RadarFrame::GroundSample> ground) {
    ground_=std::move(ground);
    ground_dirty_=true;
    update();
}

void PpiDisplay::setGroundVisible(bool enabled) {
    if(ground_visible_==enabled)return;
    ground_visible_=enabled;
    update();
}

void PpiDisplay::notifyFrameSweep(double bearing_deg)
{
    if(!std::isfinite(bearing_deg))return;
    // B7 §7.2: 帧头 CPI 中心波束方位直接驱动扫描线 (机械雷达天线指向真值);
    // 本地匀速扫描仅在无帧超时后兜底 (显控未联调/仿真停发时画面仍活动)。
    sweep_bearing_ = std::fmod(std::fmod(bearing_deg,360.0)+360.0,360.0);
    sweep_integration_ = sweep_bearing_;
    if (!frame_driven_) {
        frame_driven_ = true;
    }
    frame_timer_.restart();
    emit sweepBearingChanged(sweep_bearing_);
    update();
}

void PpiDisplay::onSweepTick()
{
    if (!powered_||sweep_speed_==0.) return;
    // 帧驱动模式: 本地累加暂停; >1.5s 无帧回落 (帧源丢失兜底)
    if (frame_driven_) {
        if (frame_timer_.elapsed() < 1500) { update(); return; }
        frame_driven_ = false;
    }
    sweep_bearing_ += sweep_speed_ * 0.016;
    if (sweep_bearing_ >= 360.0) sweep_bearing_ -= 360.0;
    sweep_integration_ = sweep_bearing_;
    emit sweepBearingChanged(sweep_bearing_);
    update();
}

void PpiDisplay::paintEvent(QPaintEvent*)
{
    PaintTiming timing{this};
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = width()  * 0.5;
    const double cy = height() * 0.5;
    const double R  = qMin(width(), height()) * 0.5 - 26.0;

    if (R <= 0.0) return;

    drawBackground(p, cx, cy, R);
    drawSweep(p, cx, cy, R);
    drawEcho(p, cx, cy, R);
    drawTargets(p, cx, cy, R);
}

void PpiDisplay::mouseMoveEvent(QMouseEvent* event)
{
    const double radius=qMin(width(),height())*.5-26.;
    const QPointF local=event->position()-QPointF(width()*.5,height()*.5);
    const double distance=std::hypot(local.x(),local.y());
    if(radius<=0||distance>radius){setToolTip({});return;}
    const auto bearing=std::fmod(qRadiansToDegrees(std::atan2(local.x(),-local.y()))+360.,360.);
    const auto km=distance/radius*range_scale_km_;
    setToolTip(tr("方位 %1° · 距离 %2").arg(bearing,0,'f',1)
        .arg(km<1.?tr("%1 m").arg(km*1000.,0,'f',1):tr("%1 km").arg(km,0,'f',2)));
}

void PpiDisplay::drawBackground(QPainter& p, double cx, double cy, double R)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#0a0f14"));
    p.drawRect(rect());

    // 圆形窗口底色
    QRadialGradient rg(cx, cy, R);
    rg.setColorAt(0.0, QColor("#0d1a14"));
    rg.setColorAt(1.0, QColor("#06100c"));
    p.setBrush(rg);
    p.setPen(QPen(QColor("#1f7a4d"), 2.0));
    p.drawEllipse(QPointF(cx, cy), R, R);

    if(ground_visible_){
        if(ground_dirty_)rebuildGround();
        if(!ground_layer_.isNull()){
            p.save();p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(QRectF(cx-R,cy-R,2*R,2*R),ground_layer_);p.restore();
        }
    }

    // 距离环
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(63, 182, 139, 90), 1.0, Qt::DotLine));
    const int rings = 5;
    for (int i = 1; i <= rings; ++i) {
        double r = R * i / rings;
        p.drawEllipse(QPointF(cx, cy), r, r);
    }

    // 方位刻度
    p.setPen(QPen(QColor(140, 200, 170, 160), 1.2));
    QFont f = p.font();
    f.setPointSize(7);
    p.setFont(f);
    for (int deg = 0; deg < 360; deg += 30) {
        double a = qDegreesToRadians(double(deg));
        QPointF outer(cx + R * std::sin(a), cy - R * std::cos(a));
        QPointF inner(cx + (R - 8) * std::sin(a), cy - (R - 8) * std::cos(a));
        p.drawLine(inner, outer);
        if (deg % 90 == 0) {
            QPointF label(cx + (R - 18) * std::sin(a),
                          cy - (R - 18) * std::cos(a));
            p.drawText(label.x() - 8, label.y() + 4, QString::number(deg) + "°");
        }
    }

    // 量程标注
    p.setPen(QColor(120, 180, 150, 200));
    for (int i = 1; i <= rings; ++i) {
        double r = R * i / rings;
        const double km=i*range_scale_km_/rings;
        p.drawText(int(cx + 4), int(cy - r + 4),range_scale_km_<1.?
            tr("%1 m").arg(km*1000.,0,'g',3):tr("%1 km").arg(km,0,'g',3));
    }
}

void PpiDisplay::rebuildGround() {
    ground_dirty_=false;
    if(ground_.empty()){ground_layer_={};return;}
    ground_layer_=QImage(echoSize,echoSize,QImage::Format_ARGB32_Premultiplied);
    ground_layer_.fill(Qt::transparent);
    const double radius=echoSize*.5;
    const double maxRange=range_scale_km_*1000.;
    const auto& disk=polarPixels();
    const auto peak=std::max_element(ground_.begin(),ground_.end(),[](const auto& a,const auto& b){return a.power_relative<b.power_relative;})->power_relative;
    for(const auto& point:ground_){
        if(point.slant_range_m>maxRange)continue;
        const double level=std::clamp(1.+10.*std::log10(double(point.power_relative)/peak)/30.,0.,1.);
        if(level<=0.)continue;
        const double az=qDegreesToRadians(double(point.bearing_deg));
        const double range=radius*point.slant_range_m/maxRange;
        const double x=radius-.5+range*std::sin(az),y=radius-.5-range*std::cos(az);
        const int left=int(std::floor(x)),top=int(std::floor(y));
        for(int row=top;row<=top+1;++row)for(int col=left;col<=left+1;++col){
            if(row<0||col<0||row>=echoSize||col>=echoSize||disk[row*echoSize+col].fraction>1.f)continue;
            const auto alpha=int(190.*level*(1.-std::abs(x-col))*(1.-std::abs(y-row)));
            auto& pixel=reinterpret_cast<QRgb*>(ground_layer_.scanLine(row))[col];
            if(alpha>qAlpha(pixel))pixel=qPremultiply(qRgba(73,219,194,alpha));
        }
    }
}

void PpiDisplay::drawEcho(QPainter& p, double cx, double cy, double R)
{
    if(echo_dirty_)rebuildEcho();
    if(echo_layer_.isNull())return;
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRectF(cx-R,cy-R,2*R,2*R),echo_layer_);
    p.restore();
}

void PpiDisplay::rebuildEcho() {
    echo_dirty_=false;
    float peak=-240.f;
    for(const auto& radial:radials_)if(latest_echo_time_-radial.time<echoLifetime)peak=std::max(peak,radial.peak);
    if(peak<=-240.f){echo_layer_={};return;}

    echo_layer_=QImage(echoSize,echoSize,QImage::Format_ARGB32_Premultiplied);
    echo_layer_.fill(Qt::transparent);
    const auto& coords=polarPixels();
    for(int y=0;y<echoSize;++y){
        auto* row=reinterpret_cast<QRgb*>(echo_layer_.scanLine(y));
        for(int x=0;x<echoSize;++x){
            const auto& coordinate=coords[y*echoSize+x];
            if(coordinate.fraction>1.f)continue;
            const auto& radial=radials_[coordinate.bearing];
            const double age=latest_echo_time_-radial.time;
            if(radial.db.empty()||age>=echoLifetime)continue;
            const double index=(coordinate.fraction*range_scale_km_-radial.firstKm)/radial.stepKm;
            if(index<=-.5||index>=double(radial.db.size())-.5)continue;
            const float value=radial.db[size_t(std::lround(index))];
            if(!std::isfinite(value)||value<=-240.f)continue;
            const double strength=std::clamp((value-peak+45.)/45.,0.,1.);
            const int alpha=int(255*strength*(1.-age/echoLifetime));
            row[x]=qPremultiply(qRgba(255,179,57,alpha));
        }
    }
}

void PpiDisplay::drawSweep(QPainter& p, double cx, double cy, double R)
{
    if (!powered_) return;

    const double a = qDegreesToRadians(sweep_bearing_);
    QPointF tip(cx + R * std::sin(a), cy - R * std::cos(a));

    // 扫描线
    p.setPen(QPen(QColor("#3fb68b"), 1.6));
    p.drawLine(QPointF(cx, cy), tip);

    // 天线头
    p.setBrush(QColor("#3fb68b"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(tip, 3.0, 3.0);
}

void PpiDisplay::drawTargets(QPainter& p, double cx, double cy, double R)
{
    for (const auto& t : targets_) {
        if (t.range_km > range_scale_km_) continue;
        const double a  = qDegreesToRadians(t.bearing_deg);
        const double rr = t.range_km / range_scale_km_ * R;
        QPointF pos(cx + rr * std::sin(a), cy - rr * std::cos(a));

        const double mag = qBound(0.3, 1.0 + t.snr_db / 20.0, 2.2);

        // 回波光晕
        QRadialGradient g(pos, 6.0 * mag);
        g.setColorAt(0.0, QColor(255, 190, 60, 200));
        g.setColorAt(1.0, QColor(255, 190, 60, 0));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pos, 6.0 * mag, 6.0 * mag);

        // 已跟踪目标: 方框 + 编号
        if (t.tracked) {
            p.setPen(QPen(QColor("#6fd4ff"), 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(pos.x() - 7, pos.y() - 7, 14, 14));
        } else {
            p.setBrush(QColor(255, 190, 60, 220));
            p.setPen(Qt::NoPen);
            p.drawEllipse(pos, 2.6, 2.6);
        }
    }
}

}} // namespace vsgraysim::gui
