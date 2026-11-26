#include "ConsoleSelector.h"
#include "CockpitProfile.h"
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>

namespace vsgraysim::gui::display {
WorkbenchSelector::WorkbenchSelector(QWidget* parent):QDialog(parent){
    setObjectName("workbench_selector");setWindowTitle(tr("大显控 · 选择工作台"));resize(900,620);
    auto* layout=new QVBoxLayout(this);auto* title=new QLabel(tr("场景仿真 → 大显控"));title->setStyleSheet("font-size:26px;font-weight:600;padding:14px;");layout->addWidget(title);
    auto* description=new QLabel(tr("连接同一仿真场景；飞机、雷达、红外、光学各有独立界面。进入后可随时切换，保留设备连接。"));description->setWordWrap(true);layout->addWidget(description);
    auto* grid=new QGridLayout;int index=0;
    const std::vector<std::pair<QString,QString>> modes={{"mission",tr("综合任务台\n平台、传感器与数据链")},{"aircraft",tr("平台操作台\n飞机、舰船与地面车辆")},{"radar",tr("雷达显控\nRD、检测与航迹")},{"infrared",tr("红外显控\nMWIR / LWIR 独立设备")},{"optical",tr("光学显控\n可见光相机与图像")}};
    for(const auto& [id,label]:modes){auto* button=new QPushButton(label,this);button->setObjectName("workbench_"+id);button->setMinimumSize(250,125);button->setStyleSheet("QPushButton{min-height:125px;max-height:180px;padding:12px;}");grid->addWidget(button,index/2,index%2);++index;connect(button,&QPushButton::clicked,this,[this,id]{mode_=id;accept();});}
    auto* local=new QPushButton(tr("打开体素场景联调…\n使用本机场景与真实传感器数据"));local->setObjectName("workbench_voxel");local->setMinimumSize(250,125);local->setStyleSheet("QPushButton{min-height:125px;max-height:180px;padding:12px;}");grid->addWidget(local,index/2,index%2);connect(local,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("选择体素装备配置"),"config/equipment",tr("装备场景 (*.vequipment.json *.json)"));if(!path.isEmpty()){equipment_=path;mode_="mission";accept();}});layout->addLayout(grid,1);
    auto* cancel=new QPushButton(tr("取消"));connect(cancel,&QPushButton::clicked,this,&QDialog::reject);layout->addWidget(cancel,0,Qt::AlignRight);
}
ConsoleSelector::ConsoleSelector(QWidget* parent,const QString& preferred):QDialog(parent){
    setObjectName("console_selector");setWindowTitle(tr("选择飞行显控台"));resize(1080,760);setMinimumSize(820,620);
    auto* layout=new QVBoxLayout(this);auto* title=new QLabel(tr("选择机型与操控台"));title->setStyleSheet("font-size:24px;font-weight:600;padding:12px;");layout->addWidget(title);
    auto* subtitle=new QLabel(tr("各机型使用独立面板、仪表量程与操纵布局 · 外部三维视图读取同一平台遥测"));subtitle->setWordWrap(true);layout->addWidget(subtitle);
    auto* grid=new QGridLayout;grid->setSpacing(14);int index=0;
    for(const auto& path:CockpitProfile::builtInProfiles()){
        const auto p=CockpitProfile::load(path);auto* card=new QPushButton(this);card->setObjectName("console_"+p.id);card->setCheckable(true);card->setChecked(p.id==preferred||path==preferred);card->setMinimumSize(370,260);card->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        card->setStyleSheet("QPushButton{min-height:260px;max-height:380px;padding:10px;background:#202a35;border:1px solid #394c60;border-radius:6px;}QPushButton:hover,QPushButton:checked{background:#273d50;border:2px solid #77b5d9;}QLabel{background:transparent;border:none;}");
        auto* content=new QVBoxLayout(card);content->setContentsMargins(22,20,22,20);
        auto* name=new QLabel(p.name);name->setStyleSheet("font-weight:600;font-size:17px;");name->setTextFormat(Qt::PlainText);name->setAttribute(Qt::WA_TransparentForMouseEvents);content->addWidget(name);
        QStringList instruments;for(const auto& instrument:p.instruments)instruments.push_back(instrument.label);
        auto* list=new QLabel(instruments.join("  ·  "));list->setWordWrap(true);list->setTextFormat(Qt::PlainText);list->setAttribute(Qt::WA_TransparentForMouseEvents);list->setStyleSheet("color:#b5d9e6;");content->addWidget(list,1);
        auto* detail=new QLabel(p.description);detail->setWordWrap(true);detail->setTextFormat(Qt::PlainText);detail->setAttribute(Qt::WA_TransparentForMouseEvents);content->addWidget(detail);grid->addWidget(card,index/2,index%2);++index;
        connect(card,&QPushButton::clicked,this,[this,path]{selected_=path;accept();});
    }
    layout->addLayout(grid,1);auto* cancel=new QPushButton(tr("取消"));connect(cancel,&QPushButton::clicked,this,&QDialog::reject);layout->addWidget(cancel,0,Qt::AlignRight);
}
}
