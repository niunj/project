#include "SonarDisplayPanel.h"
#include "EquipmentSession.h"
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vsgraysim::gui::display {

class SonarDisplayPanel::DepthHistory final : public QWidget {
public:
    explicit DepthHistory(QWidget* parent):QWidget(parent){setMinimumHeight(210);}
    void clear(){points_.clear();update();}
    void append(const SonarFrame& frame){
        if(!points_.empty()&&points_.back().frame_id==frame.frame_id)return;
        points_.push_back(frame);
        while(points_.size()>240||points_.back().sim_time-points_.front().sim_time>windowSeconds())points_.pop_front();
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(),QColor("#0d1922"));
        const auto area=QRectF(rect()).adjusted(55,15,-16,-34);
        if(area.isEmpty())return;
        p.setRenderHint(QPainter::Antialiasing);
        double maxDepth=10.;
        for(const auto& point:points_){
            if(point.bottom==BottomReturn::Valid)maxDepth=std::max(maxDepth,point.water_depth_m);
            if(point.echo_range_m>0.)maxDepth=std::max(maxDepth,point.echoDepthM());
        }
        maxDepth=10.*std::ceil(maxDepth/10.);
        for(int row=0;row<=4;++row){
            const double y=area.top()+area.height()*row/4.;
            p.setPen(QColor("#243b49"));p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
            p.setPen(QColor("#b7cdda"));
            p.drawText(QRectF(0,y-10,49,20),Qt::AlignRight|Qt::AlignVCenter,
                QString::number(maxDepth*row/4.,'g',4)+QStringLiteral(" m"));
        }
        p.setPen(QColor("#b7cdda"));
        const bool column=!points_.empty()&&points_.back().water_column_enabled;
        p.drawText(QRectF(area.left(),area.bottom()+8,area.width(),22),Qt::AlignCenter,
            column?tr("青线 海床 · 黄点 水体回波 · 最近 %1 秒").arg(windowSeconds()):
                tr("海床水深 · 最近 60 秒仿真时间"));
        if(points_.empty())return;
        const auto now=points_.back().sim_time;
        const auto chartPoint=[&](double time,double depth){
            return QPointF(area.right()+(time-now)/windowSeconds()*area.width(),
                area.top()+depth/maxDepth*area.height());
        };
        p.save();
        p.setClipRect(area.adjusted(-3,-3,3,3));
        p.setPen(QPen(QColor("#65dfcf"),2));
        std::optional<QPointF> previous;
        for(const auto& frame:points_){
            if(frame.bottom!=BottomReturn::Valid){previous.reset();continue;}
            const auto point=chartPoint(frame.sim_time,frame.water_depth_m);
            if(previous)p.drawLine(*previous,point);
            else p.drawPoint(point);
            previous=point;
        }
        p.setPen(QPen(QColor("#ffc068"),5,Qt::SolidLine,Qt::RoundCap));
        for(const auto& frame:points_)
            if(frame.echo_range_m>0.)p.drawPoint(chartPoint(frame.sim_time,frame.echoDepthM()));
        p.restore();
    }
private:
    int windowSeconds() const {return !points_.empty()&&points_.back().water_column_enabled?12:60;}
    std::deque<SonarFrame> points_;
};

SonarDisplayPanel::SonarDisplayPanel(QWidget* parent):DisplayPanel(parent) {
    setObjectName("sonar_survey_panel");
    auto* layout=new QVBoxLayout(this);
    status_=new QLabel(tr("等待测深仪数据"),this);
    reading_=new QLabel(this);reading_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    timing_=new QLabel(tr("数据源为仿真场景海床"),this);
    timing_->setWordWrap(true);
    history_=new DepthHistory(this);
    power_=new QPushButton(tr("开启测深仪"),this);
    power_->setEnabled(false);
    layout->addWidget(status_);layout->addWidget(reading_);layout->addWidget(timing_);
    layout->addWidget(history_,1);layout->addWidget(power_,0,Qt::AlignLeft);
    connect(power_,&QPushButton::clicked,this,[this]{if(session_)session_->setPowered(!session_->powered());});
}

void SonarDisplayPanel::tick(EquipmentSession* session) {
    const auto generation=session?session->bindingGeneration():0;
    if(session_!=session||generation_!=generation){
        history_->clear();session_=session;generation_=generation;
    }
    status_->setText(session?session->statusText():tr("等待绑定船载测深仪"));
    const bool powered=session&&session->powered();
    power_->setText(powered?tr("关闭测深仪"):tr("开启测深仪"));
    power_->setEnabled(session&&session->connected()&&!session->powerRequestPending());
    const auto frame=session&&session->hasFrame()?session->sonarSnapshot():nullptr;
    if(!frame){reading_->clear();timing_->setText(tr("数据源为仿真场景海床"));return;}
    history_->append(*frame);
    if(frame->echo_range_m>0.)
        reading_->setText(tr("水体回波深度 %1 m · 距换能器 %2 m · 仿真往返时间 %3 ms")
            .arg(frame->echoDepthM(),0,'f',2).arg(frame->echo_range_m,0,'f',2).arg(frame->echoTwoWayMs(),0,'f',2));
    else if(frame->bottom==BottomReturn::Valid)
        reading_->setText(tr("水深 %1 m · 换能器距海床 %2 m · 仿真往返时间 %3 ms")
            .arg(frame->water_depth_m,0,'f',2).arg(frame->clearance_m,0,'f',2).arg(frame->two_way_ms,0,'f',2));
    else reading_->setText(frame->bottom==BottomReturn::DryTransducer?tr("换能器离水"):tr("量程内无指定海床回波"));
    timing_->setText(tr("时间 %1 s · Tick %2 · 地表版本 %3 · 量程 %4 m\n%5")
        .arg(frame->sim_time,0,'f',2).arg(frame->tick).arg(frame->surface_revision).arg(frame->range_limit_m,0,'f',0)
        .arg(frame->echo_range_m>0.?tr("近处回波遮挡了海床"):tr("垂直单波束 · 体素场景")));
}

}
