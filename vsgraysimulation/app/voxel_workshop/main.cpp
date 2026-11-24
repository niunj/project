#include <QApplication>
#include <QAction>
#include <QInputDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QScrollArea>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QDir>
#include <QCoreApplication>
#include "base/ThemeManager.h"
#include "viewport3d/Viewport3DView.h"
#include "viewport3d/ViewportConfig.h"
#include "viewport3d/VoxelSceneFeature.h"
#include "viewport3d/EnvFeatures.h"
#include "viewport3d/EarthViewFeature.h"
#include "viewport3d/VsgQtWidget.h"
#include <vsgraysim/earth/VoxelScene.h>
#include <vsg/all.h>
#include <simcore/Orientation.h>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using json=nlohmann::json;
using namespace vsgraysim;
class Workshop final : public QMainWindow {
    viewport3d::Viewport3DView* view_;
    QTableWidget* table_;QLabel* info_;QAction* play_;
    std::string path_,saved_;uint64_t shown_=~uint64_t(0);QTimer refresh_;
    std::string smoke_;
    QElapsedTimer frameClock_;earth::VoxelScene* clockScene_=nullptr;
    double requestedSeconds_=0;uint64_t lastClockTick_=0,pendingSteps_=0;
    viewport3d::VoxelSceneFeature* feature() const {return view_->featureAs<viewport3d::VoxelSceneFeature>("voxel_scene");}
    earth::VoxelScene& scene() const {auto* f=feature();if(!f||!f->runtime())throw std::runtime_error("视口尚未就绪，请稍后重试");return *f->runtime();}
    template<class F>void act(F work){try{work();view_->markActivity();refresh();}catch(const std::exception& e){QMessageBox::warning(this,tr("体素场景"),QString::fromUtf8(e.what()));}}
    std::string state() const {auto value=scene().checkpoint();value["paused"]=true;return value.dump();}
    std::string selected() const {if(table_->currentRow()<0)throw std::runtime_error("请先选择一个对象");return table_->item(table_->currentRow(),0)->text().toStdString();}
    QDoubleSpinBox* numeric(QFormLayout* form,const QString& label,double value,double lo,double hi,int precision=3) {
        auto* spin=new QDoubleSpinBox;spin->setDecimals(precision);spin->setRange(lo,hi);spin->setValue(value);form->addRow(label,spin);return spin;
    }
    bool save() {
        auto file=QFileDialog::getSaveFileName(this,tr("保存体素场景检查点"),{},tr("体素检查点 (*.vcheckpoint.json)"));
        if(file.isEmpty())return false;scene().saveCheckpoint(file.toStdString());path_=file.toStdString();saved_=state();return true;
    }
    bool discard() {
        if(!feature()||!feature()->runtime()||state()==saved_)return true;
        auto choice=QMessageBox::question(this,tr("保存体素场景"),tr("场景有未保存的编辑或仿真结果，是否保存？"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        return choice==QMessageBox::Discard||(choice==QMessageBox::Save&&save());
    }
    void open(const std::string& file) {
        auto validated=earth::VoxelScene::open(file);auto checkpoint=validated->checkpoint();
        view_->configureFeature("voxel_scene",{{"checkpoint",checkpoint},{"paused",true}});
        clockScene_=nullptr;path_=file;saved_=state();shown_=~uint64_t(0);setWindowTitle(tr("体素场景工坊 — %1").arg(QString::fromStdString(fs::path(file).filename().string())));
    }
    void objectDialog(bool create,bool mesh) {
        auto& runtime=scene();const auto snapshot=runtime.snapshot();std::string id;
        if(!create)id=selected();const bool wasPaused=runtime.paused();runtime.setPaused(true);
        QDialog dialog(this);dialog.setWindowTitle(create?(mesh?tr("导入体素目标"):tr("新增体素盒体")):tr("对象位姿与材质"));
        auto* layout=new QVBoxLayout(&dialog);auto* fields=new QWidget;auto* form=new QFormLayout(fields);
        auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(fields);layout->addWidget(scroll);dialog.resize(520,740);
        auto* name=new QLineEdit(create?QString("object-%1").arg(snapshot->objects.size()+1):QString::fromStdString(id));name->setReadOnly(!create);form->addRow(tr("稳定对象 ID"),name);
        auto* role=new QComboBox;role->addItems({"target","structure","terrain"});if(!create){role->setCurrentText(QString::fromStdString(snapshot->objects.at(id).role));role->setEnabled(false);}form->addRow(tr("对象角色"),role);
        std::string asset;QDoubleSpinBox* cell=nullptr;QDoubleSpinBox* units=nullptr;QComboBox* upAxis=nullptr;QCheckBox* interior=nullptr;QSpinBox* dimensions[3]{};QSpinBox* detail=nullptr;
        if(create&&mesh) {
            const auto file=QFileDialog::getOpenFileName(this,tr("选择目标模型"),{},tr("目标模型 (*.osg *.osgb *.ive *.vsg *.vsgt *.vsgb *.obj)"));
            if(file.isEmpty()){runtime.setPaused(wasPaused);return;}asset=file.toStdString();form->addRow(tr("源模型"),new QLabel(file));
            cell=numeric(form,tr("体素边长（米；0 自动）"),0,0,1000,4);
            detail=new QSpinBox;detail->setRange(16,512);detail->setValue(512);form->addRow(tr("最长轴格数上限（自动适配预算）"),detail);
            units=numeric(form,tr("源单位换算为米"),1,1e-9,1e6,9);upAxis=new QComboBox;upAxis->addItems({"y","z"});
            const auto ext=fs::path(asset).extension().string();if(ext==".osg"||ext==".osgb"||ext==".ive")upAxis->setCurrentText("z");form->addRow(tr("源模型向上轴"),upAxis);
            interior=new QCheckBox(tr("封闭模型填充内部"));interior->setChecked(true);form->addRow(interior);
        } else if(create) {
            cell=numeric(form,tr("体素边长（米）"),.5,.001,1000,4);
            for(int a=0;a<3;++a){dimensions[a]=new QSpinBox;dimensions[a]->setRange(1,256);dimensions[a]->setValue(8);form->addRow(tr("%1 轴格数").arg(QChar("XYZ"[a])),dimensions[a]);}
        }
        const auto* previous=create?nullptr:&snapshot->objects.at(id);vsg::dmat4 pose;
        if(previous)pose=previous->pose(snapshot->tick,snapshot->fixedStep);
        QDoubleSpinBox* position[3];QDoubleSpinBox* velocity[3];QDoubleSpinBox* angles[3];QDoubleSpinBox* angular[3];
        vsg::dvec3 rotation;
        if(previous){vsg::dvec3 translation,scale;vsg::dquat orientation;if(!vsg::decompose(pose,translation,orientation,scale))throw std::runtime_error("无法分解对象位姿");const auto hpr=simcore::headingAngles(orientation);rotation=vsg::dvec3(hpr.y,hpr.x,hpr.z);}
        for(int a=0;a<3;++a)position[a]=numeric(form,tr("%1 位置（米）").arg(QChar("XYZ"[a])),previous?pose[3][a]:0,-100000,100000);
        for(int a=0;a<3;++a)velocity[a]=numeric(form,tr("%1 速度（米/秒）").arg(QChar("XYZ"[a])),previous?previous->velocity[a]:0,-10000,10000);
        const QString angleNames[3]={tr("俯仰（度）"),tr("航向（度）"),tr("横滚（度）")};
        for(int a=0;a<3;++a)angles[a]=numeric(form,angleNames[a],rotation[a],-360,360);
        for(int a=0;a<3;++a)angular[a]=numeric(form,tr("%1 角速度（弧度/秒）").arg(QChar("XYZ"[a])),previous?previous->angularVelocity[a]:0,-1000,1000);
        auto* motion=new QComboBox;motion->addItems({"dynamic","static","prescribed"});motion->setCurrentText(previous?QString::fromStdString(previous->body.motion):"dynamic");form->addRow(tr("运动模式（动态/静态/预设）"),motion);
        auto* fracture=new QCheckBox(tr("启用体素面键断裂"));fracture->setChecked(previous?previous->body.fracture:true);form->addRow(fracture);
        auto* support=new QComboBox;if(previous)support->addItem("preserve");support->addItems({"none","base","all"});form->addRow(tr("固定支撑（保留/无/底层/全部）"),support);
        auto* energy=numeric(form,tr("断裂能（焦耳/平方米）"),previous?previous->body.fractureEnergyPerArea:1000,1e-6,1e12,6);
        auto* friction=numeric(form,tr("接触摩擦系数"),previous?previous->body.friction:.6,0,2);
        auto* bounce=numeric(form,tr("恢复系数"),previous?previous->body.restitution:.1,0,1);
        connect(motion,&QComboBox::currentTextChanged,&dialog,[&](const QString& mode){
            if(mode=="static")for(int a=0;a<3;++a){velocity[a]->setValue(0);angular[a]->setValue(0);}
            else if(mode=="dynamic")support->setCurrentText("none");
            else for(auto* spin:angular)spin->setValue(0);
        });
        auto* reflectance=numeric(form,tr("可见光反射率"),previous?previous->material.at("rho_tv").get<double>():.25,0,1);
        auto* swir=numeric(form,tr("短波红外反射率"),previous?previous->material.at("rho_swir").get<double>():.2,0,1);
        auto* density=numeric(form,tr("密度（千克/立方米）"),previous?previous->material.at("density_kg_m3").get<double>():2400,.001,1e6);
        auto* temperature=numeric(form,tr("温度（K）"),previous?previous->material.at("temperature_k").get<double>():288.15,1,5000);
        auto* emissivity=numeric(form,tr("发射率"),previous?previous->material.at("emissivity").get<double>():.92,0,1);
        QColor color(create&&mesh?255:150,create&&mesh?255:155,create&&mesh?255:160);if(previous){auto c=previous->material.at("color_rgba");color=QColor::fromRgbF(c[0].get<double>(),c[1].get<double>(),c[2].get<double>());}
        auto* colorButton=new QPushButton(tr("选择表面颜色"));form->addRow(colorButton);connect(colorButton,&QPushButton::clicked,&dialog,[&]{auto c=QColorDialog::getColor(color,&dialog);if(c.isValid())color=c;});
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted){runtime.setPaused(wasPaused);return;}
        json mat=previous?previous->material:json::object();mat["rho_tv"]=reflectance->value();mat["rho_swir"]=swir->value();mat["density_kg_m3"]=density->value();mat["temperature_k"]=temperature->value();mat["emissivity"]=emissivity->value();mat["color_rgba"]={color.redF(),color.greenF(),color.blueF(),1.};
        json p=json::array(),v=json::array(),r=json::array(),w=json::array();for(int a=0;a<3;++a){p.push_back(position[a]->value());v.push_back(velocity[a]->value());r.push_back(angles[a]->value());w.push_back(angular[a]->value());}
        json physics={{"motion",motion->currentText().toStdString()},{"fracture",fracture->isChecked()},{"fracture_energy_j_m2",energy->value()},{"friction",friction->value()},{"restitution",bounce->value()},
            {"linear_damping",previous?previous->body.linearDamping:.02},{"angular_damping",previous?previous->body.angularDamping:.02}};
        if(support->currentText()!="preserve")physics["support"]=support->currentText().toStdString();
        else if(previous->body.anchorAll)physics["support"]="all";
        json commands=json::array();
        if(create) {
            json source=mesh?json{{"type","mesh"},{"path",asset},{"voxel_size_m",cell->value()},{"max_dimension",detail?detail->value():64},{"fill_interior",interior->isChecked()},{"units_to_metres",units->value()},{"up_axis",upAxis->currentText().toStdString()}}:
                json{{"type","box"},{"dimensions",{dimensions[0]->value(),dimensions[1]->value(),dimensions[2]->value()}},{"cell_size_m",cell->value()}};
            commands.push_back({{"kind","add"},{"object",{{"id",name->text().toStdString()},{"role",role->currentText().toStdString()},{"source",source},{"rotation_deg",r},{"position_m",p},{"velocity_mps",v},{"angular_velocity_rad_s",w},{"physics",physics},{"material",mat}}}});
        } else {
            commands.push_back({{"kind","place"},{"object",id},{"position_m",p},{"rotation_deg",r},{"velocity_mps",v},{"angular_velocity_rad_s",w}});
            commands.push_back({{"kind","material"},{"object",id},{"material",mat}});
            commands.push_back({{"kind","physics"},{"object",id},{"physics",physics}});
        }
        runtime.edit(commands,snapshot->revision);
    }
    void damage() {
        auto& runtime=scene();const auto id=selected();const auto snapshot=runtime.snapshot();const auto box=snapshot->objects.at(id).localBounds();
        const bool wasPaused=runtime.paused();runtime.setPaused(true);
        QDialog dialog(this);dialog.setWindowTitle(tr("安排损伤事件"));auto* form=new QFormLayout(&dialog);QDoubleSpinBox* p[3];
        for(int a=0;a<3;++a)p[a]=numeric(form,tr("对象局部 %1（米）").arg(QChar("XYZ"[a])),.5*(box.min[a]+box.max[a]),-100000,100000);
        auto* radius=numeric(form,tr("显式损伤半径（米）"),1,.001,10000);
        auto* impulse=numeric(form,tr("碎块径向总冲量（牛顿秒）"),0,0,1e12);
        impulse->setToolTip(tr("0 表示仅损伤。冲量按质量分配给此次毁伤后的动态碎块；固定支撑不受影响。"));
        auto* delay=new QSpinBox;delay->setRange(1,1000000);delay->setValue(1);form->addRow(tr("延迟步数"),delay);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted){runtime.setPaused(wasPaused);return;}
        runtime.schedule({{"tick",snapshot->tick+uint64_t(delay->value())},{"kind","damage"},{"object",id},{"space","object"},{"point_m",{p[0]->value(),p[1]->value(),p[2]->value()}},{"radius_m",radius->value()},{"blast_impulse_ns",impulse->value()}});
    }
    void physicalEvent(bool fractureEvent) {
        auto& runtime=scene();const auto id=selected();const auto snapshot=runtime.snapshot();const auto& object=snapshot->objects.at(id);const auto bounds=object.localBounds();
        if(fractureEvent&&!object.body.fracture)throw std::runtime_error("请先在对象属性中启用面键断裂");
        if(!fractureEvent&&object.body.motion!="dynamic")throw std::runtime_error("请选择动态目标或已分离的碎块");
        const bool wasPaused=runtime.paused();runtime.setPaused(true);QDialog dialog(this);dialog.setWindowTitle(fractureEvent?tr("安排断裂面"):tr("施加刚体冲量"));auto* form=new QFormLayout(&dialog);
        QDoubleSpinBox* point[3];QDoubleSpinBox* vector[3];
        for(int a=0;a<3;++a)point[a]=numeric(form,tr("局部 %1 作用点（米）").arg(QChar("XYZ"[a])),.5*(bounds.min[a]+bounds.max[a]),-1e6,1e6);
        for(int a=0;a<3;++a)vector[a]=numeric(form,(fractureEvent?tr("%1 断裂面法向"):tr("%1 冲量（牛顿秒）")).arg(QChar("XYZ"[a])),fractureEvent?(a==1?1:0):(a==0?std::min(1e8,object.massKg()*2):0),-1e8,1e8);
        QDoubleSpinBox* radius=nullptr;QDoubleSpinBox* energy=nullptr;
        if(fractureEvent){radius=numeric(form,tr("断裂影响半径（米）"),std::max(1.,vsg::length(bounds.max-bounds.min)),1e-6,1e6,6);energy=numeric(form,tr("可用断裂能（焦耳）"),100000,1e-6,1e18,6);}
        auto* delay=new QSpinBox;delay->setRange(1,1000000);delay->setValue(1);form->addRow(tr("延迟步数"),delay);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted){runtime.setPaused(wasPaused);return;}
        json event={{"kind",fractureEvent?"fracture":"impulse"},{"object",id},{"tick",snapshot->tick+uint64_t(delay->value())},{"space","object"},{"point_m",{point[0]->value(),point[1]->value(),point[2]->value()}}};
        event[fractureEvent?"normal":"impulse_ns"]={vector[0]->value(),vector[1]->value(),vector[2]->value()};
        if(fractureEvent){event["radius_m"]=radius->value();event["energy_j"]=energy->value();}runtime.schedule(event);
    }
    void importEarth() {
        auto path=QFileDialog::getOpenFileName(this,tr("导入影像与高程地形"),QStringLiteral("config/earth"),tr("地球地图 / 地形工程 (*.json *.ter *.vsgterrain)"));
        if(path.isEmpty())return;
        auto& runtime=scene();const bool paused=runtime.paused();runtime.setPaused(true);
        QDialog dialog(this);dialog.setWindowTitle(tr("影像高程物理工作区"));auto* form=new QFormLayout(&dialog);
        auto* name=new QLineEdit(QStringLiteral("earth-terrain"));form->addRow(tr("对象 ID"),name);
        const auto& anchor=runtime.snapshot()->anchor;
        auto* note=new QLabel(tr("使用当前场景锚点：%1°, %2°，椭球高 %3 m。\n范围采用东、上、南米制坐标；底面是显式仿真深度。\n地图中的影像、高程和垂直基准声明将共同加载。").arg(anchor.lat_deg,0,'f',6).arg(anchor.lon_deg,0,'f',6).arg(anchor.alt_m));note->setWordWrap(true);form->addRow(note);
        auto* xmin=numeric(form,tr("最小东向 X（米）"),-40,-100000,100000);auto* xmax=numeric(form,tr("最大东向 X（米）"),40,-100000,100000);
        auto* zmin=numeric(form,tr("最小南向 Z（米）"),-40,-100000,100000);auto* zmax=numeric(form,tr("最大南向 Z（米）"),40,-100000,100000);
        auto* bottom=numeric(form,tr("体积底面 Y（米）"),-20,-100000,100000);auto* cell=numeric(form,tr("体素边长（米）"),2,.01,10000,4);
        auto* sub=new QSpinBox;sub->setRange(1,8);sub->setValue(2);form->addRow(tr("每格高清细分"),sub);
        auto* image=new QSpinBox;image->setRange(32,1024);image->setSingleStep(32);image->setValue(256);form->addRow(tr("影像图集边长（像素）"),image);
        auto* mode=new QComboBox;mode->addItems({"hd","voxel"});form->addRow(tr("初始模式"),mode);
        auto* support=new QComboBox;support->addItems({"base","all"});form->addRow(tr("固定支撑"),support);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted){runtime.setPaused(paused);return;}
        const double count=std::max(xmax->value()-xmin->value(),zmax->value()-zmin->value())/cell->value();
        const int grid=std::clamp(int(std::ceil(count*sub->value()))+1,2,513);
        json settings={{"strict",true},{"grid_size",grid},{"imagery_size",image->value()},
            {"coverage",{{"min_x",xmin->value()},{"max_x",xmax->value()},{"min_z",zmin->value()},{"max_z",zmax->value()}}}};
        // Source vertical datum remains the map's declaration; do not label
        // orthometric or unknown data as ellipsoidal merely to accept an import.
        json object={{"id",name->text().toStdString()},{"role","terrain"},
            {"source",{{"type","earth"},{"map",path.toStdString()},{"earth",settings},{"cell_size_m",cell->value()},
                       {"bottom_m",bottom->value()},{"hd_subdivisions",sub->value()},{"mode",mode->currentText().toStdString()}}},
            {"physics",{{"motion","static"},{"fracture",true},{"support",support->currentText().toStdString()},{"friction",.8},{"restitution",.05}}}};
        runtime.edit(json::array({{{"kind","add"},{"object",object}}}),runtime.snapshot()->revision);
    }
    void terrainMode() {
        auto& runtime=scene();const auto id=selected();const auto snapshot=runtime.snapshot();const auto& object=snapshot->objects.at(id);
        if(!object.earth)throw std::runtime_error("请选择由影像高程导入的地形对象");
        if(object.body.motion!="static")throw std::runtime_error("运动地形碎块使用体素刚体模式");
        json command={{"kind","terrain_mode"},{"object",id},{"mode",object.terrainMode=="hd"?"voxel":"hd"}};
        if(runtime.paused())runtime.edit(json::array({command}),snapshot->revision);
        else {command["tick"]=snapshot->tick+1;runtime.schedule(command);}
    }
    void refresh() {
        if(!feature()||!feature()->runtime())return;auto& runtime=scene();auto snapshot=runtime.snapshot();play_->setText(runtime.paused()?tr("运行"):tr("暂停"));
        info_->setText(tr("步 %1  ·  %2 s  ·  版本 %3\n%4 个对象  ·  %5 个已执行事件\n追赶余量 %6 步 · 接触点 %7\n%8").arg(snapshot->tick).arg(snapshot->timeSeconds(),0,'f',3).arg(snapshot->revision).arg(snapshot->objects.size()).arg(snapshot->journal.size()).arg(pendingSteps_).arg(snapshot->contacts.size()).arg(QString::fromStdString(feature()->lastError())));
        if(shown_==snapshot->revision)return;shown_=snapshot->revision;
        const auto selection=table_->currentRow()>=0?table_->item(table_->currentRow(),0)->text():QString();table_->setRowCount(int(snapshot->objects.size()));int row=0;
        for(const auto& [id,o]:snapshot->objects){table_->setItem(row,0,new QTableWidgetItem(QString::fromStdString(id)));table_->setItem(row,1,new QTableWidgetItem(QString::fromStdString(o.role)));table_->setItem(row,2,new QTableWidgetItem(QString::number(o.massKg(),'g',8)));table_->setItem(row,3,new QTableWidgetItem(QString::fromStdString(o.body.motion)));table_->setItem(row,4,new QTableWidgetItem(o.earth?QString::fromStdString(o.terrainMode):QStringLiteral("voxel")));if(selection==QString::fromStdString(id))table_->selectRow(row);++row;}
    }
protected:
    void closeEvent(QCloseEvent* event) override {try{if(discard())event->accept();else event->ignore();}catch(const std::exception& e){QMessageBox::warning(this,tr("保存失败"),e.what());event->ignore();}}
public:
    Workshop(const std::string& file,const std::string& smoke):path_(file),smoke_(smoke) {
        resize(smoke_.empty()?1360:1000,smoke_.empty()?900:650);setWindowTitle(tr("体素场景工坊"));
        view_=new viewport3d::Viewport3DView(this);setCentralWidget(view_);
        auto config=viewport3d::ViewportConfig::fromPreset("voxel_workshop");
        config.add("voxel_scene",{{"scene_json",file},{"advance_from_frames",false}});
        view_->applyConfig(config);view_->setAutoFrame(true,60);frameClock_.start();
        // Advance truth before any environment/display feature samples the frame.
        // Every module sees the committed fixed-step time, including while paused.
        connect(view_,&viewport3d::Viewport3DView::beforeFrameTick,this,[this]{
            const double dt=frameClock_.nsecsElapsed()*1e-9;frameClock_.restart();
            if(!feature()||!feature()->runtime())return;auto& runtime=scene();
            if(clockScene_!=&runtime||lastClockTick_!=runtime.snapshot()->tick){clockScene_=&runtime;requestedSeconds_=runtime.snapshot()->timeSeconds();}
            try {if(!runtime.paused()){requestedSeconds_+=dt;pendingSteps_=runtime.advanceTo(requestedSeconds_).pendingSteps;}}
            catch(const std::exception& error){runtime.setPaused(true);statusBar()->showMessage(QString::fromUtf8(error.what()));}
            lastClockTick_=runtime.snapshot()->tick;view_->context().setSimulationTime(runtime.snapshot()->timeSeconds());
        });
        auto* tools=addToolBar(tr("场景"));
        auto action=[&](const QString& text,auto callback){auto* a=tools->addAction(text);connect(a,&QAction::triggered,this,[this,callback]{act(callback);});return a;};
        action(tr("新建"),[this]{
            if(!discard())return;const auto previous=scene().snapshot();const auto& a=previous->anchor;
            auto empty=earth::VoxelScene::load({{"format","voxel-scene-v1"},{"objects",json::array()},
                {"fixed_step_seconds",previous->fixedStep},{"utc_origin",previous->utcOrigin},{"gravity_mps2",{previous->gravity.x,previous->gravity.y,previous->gravity.z}},
                {"geo_anchor",{{"lat",a.lat_deg},{"lon",a.lon_deg},{"alt",a.alt_m},{"heading_deg",a.heading_deg}}}});
            view_->configureFeature("voxel_scene",{{"checkpoint",empty->checkpoint()},{"paused",true}});
            clockScene_=nullptr;path_.clear();saved_=state();shown_=~uint64_t(0);setWindowTitle(tr("体素场景工坊 — 未命名"));
        });
        action(tr("打开"),[this]{if(!discard())return;auto path=QFileDialog::getOpenFileName(this,tr("打开体素场景"),{},tr("体素场景 / 检查点 (*.json)"));if(!path.isEmpty())open(path.toStdString());});
        action(tr("保存检查点"),[this]{save();});tools->addSeparator();
        play_=action(tr("运行"),[this]{scene().setPaused(!scene().paused());});
        action(tr("单步"),[this]{auto& runtime=scene();runtime.setPaused(false);try{runtime.advanceTo(runtime.snapshot()->timeSeconds()+runtime.snapshot()->fixedStep,1);}catch(...){runtime.setPaused(true);throw;}runtime.setPaused(true);});
        tools->addSeparator();action(tr("新增盒体"),[this]{objectDialog(true,false);});action(tr("导入模型"),[this]{objectDialog(true,true);});action(tr("编辑对象"),[this]{objectDialog(false,false);});
        action(tr("导入影像高程"),[this]{importEarth();});action(tr("高清/体素切换"),[this]{terrainMode();});
        action(tr("删除"),[this]{auto& r=scene();r.setPaused(true);r.edit(json::array({{{"kind","remove"},{"object",selected()}}}),r.snapshot()->revision);});
        action(tr("安排损伤"),[this]{damage();});action(tr("断裂面"),[this]{physicalEvent(true);});action(tr("刚体冲量"),[this]{physicalEvent(false);});
        action(tr("取消事件"),[this]{
            auto& runtime=scene();const auto pending=runtime.checkpoint().at("pending_events");
            if(pending.empty())throw std::runtime_error("没有待执行事件");
            QStringList labels;for(const auto& e:pending)labels<<tr("#%1 · 步 %2 · %3 · %4").arg(e.at("sequence").get<uint64_t>()).arg(e.at("tick").get<uint64_t>()).arg(QString::fromStdString(e.at("object").get<std::string>())).arg(QString::fromStdString(e.at("kind").get<std::string>()));
            const bool wasPaused=runtime.paused();runtime.setPaused(true);bool ok=false;auto chosen=QInputDialog::getItem(this,tr("取消待执行事件"),tr("选择事件"),labels,0,false,&ok);
            if(ok)runtime.cancel(pending.at(labels.indexOf(chosen)).at("sequence").get<uint64_t>());runtime.setPaused(wasPaused);
        });action(tr("离散沉降"),[this]{auto& r=scene();r.schedule({{"tick",r.snapshot()->tick+1},{"kind","settle"},{"object",selected()}});});
        auto* dock=new QDockWidget(tr("体素对象与运行状态"),this);auto* pane=new QWidget;auto* layout=new QVBoxLayout(pane);
        info_=new QLabel(tr("正在准备场景…"));info_->setWordWrap(true);layout->addWidget(info_);
        table_=new QTableWidget(0,5);table_->setHorizontalHeaderLabels({tr("对象 ID"),tr("角色"),tr("质量 kg"),tr("运动"),tr("模式")});table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);layout->addWidget(table_);
        auto* hint=new QLabel(tr("米制坐标：X 东、Y 上、Z 南。\n影像高程地形共享损伤、碰撞与测量版本。\n高清模式保留高程，体素模式显示占据格。\n脱离支撑的碎块使用 Bullet 体素刚体。\n工作区外不生成隐式碰撞地面。"));hint->setWordWrap(true);layout->addWidget(hint);dock->setWidget(pane);addDockWidget(Qt::LeftDockWidgetArea,dock);dock->setMinimumWidth(340);
        connect(&refresh_,&QTimer::timeout,this,[this]{refresh();});refresh_.start(200);
        connect(view_->context().widget(),&gui::VsgQtWidget::initializationFailed,this,[this](const QString& error){
            info_->setText(tr("视口初始化失败：%1").arg(error));statusBar()->showMessage(error);play_->setEnabled(false);
            if(!smoke_.empty()){std::cerr<<"FAIL: viewport initialization: "<<error.toStdString()<<'\n';qApp->exit(1);}
        });
        connect(view_,&viewport3d::Viewport3DView::vsgReady,this,[this]{
            try {
                // A metre-scale stage needs a shorter shadow cascade than the
                // globe viewport; otherwise individual voxels self-shadow in bands.
                if(auto* view=view_->context().widget()->view()){view->viewDependentState->maxShadowDistance=400.;view->viewDependentState->shadowMapBias=.0005;}
                saved_=state();refresh();if(!smoke_.empty())QTimer::singleShot(1000,this,[this]{
                try{
                    const auto initial=scene().snapshot();const double target=(initial->tick+60)*initial->fixedStep;
                    scene().setPaused(false);while(scene().advanceTo(target,64).pendingSteps){}scene().setPaused(true);
                    const auto checkpoint=scene().checkpoint();
                    // Exercise the same restore path as Open, then prove a further
                    // step is identical to an independently restored runtime.
                    auto reference=earth::VoxelScene::load(checkpoint);reference->setPaused(false);reference->advanceTo(target+initial->fixedStep,1);reference->setPaused(true);
                    view_->configureFeature("voxel_scene",{{"checkpoint",checkpoint},{"paused",true},{"frame_scene",false}});clockScene_=nullptr;
                    scene().setPaused(false);scene().advanceTo(target+initial->fixedStep,1);scene().setPaused(true);
                    if(scene().checkpoint()!=reference->checkpoint())throw std::runtime_error("native checkpoint continuation diverged");
                    view_->renderFrame();
                    for(const auto& [id,object]:scene().snapshot()->objects)if(object.earth) {
                        const auto s=scene().snapshot();const auto box=object.localBounds();const auto p=object.pose(s->tick,s->fixedStep)*((box.min+box.max)*.5);
                        const auto hit=earth::raycastVoxelScene(*s,{p.x,1e6,p.z},{0,-1,0},2e6,true);double height=0;
                        const auto* query=view_->context().geoQuery();const auto* map=view_->featureAs<viewport3d::EarthViewFeature>("rocky_map");
                        if(!query||!map||!map->usesSceneTerrain()||query->revision()!=s->contentRevision||query->heightLocal(p.x,p.z,height)!=bool(hit)||(hit&&std::abs(height-hit->position.y)>.001))throw std::runtime_error("native terrain picking or preview masking differs from unified physical scene");
                        if(hit) {
                            const auto world=simcore::geo::localToECEF(s->anchor,hit->position);const auto llh=simcore::geo::ecefToGeodetic(world.x,world.y,world.z);
                            if(!query->groundElevation(llh.x,llh.y,height)||std::abs(height-llh.z)>.001)throw std::runtime_error("native geodetic terrain elevation differs from unified physical scene");
                        }
                    }
                    const auto* sky=view_->featureAs<viewport3d::SkyFeature>("rocky_sky");
                    if(!sky||std::abs(sky->clockSeconds()-scene().snapshot()->timeSeconds())>1e-10)throw std::runtime_error("sky and voxel simulation clocks diverged");
                    fs::create_directories(smoke_);scene().saveCheckpoint((fs::path(smoke_)/"workshop.vcheckpoint.json").string());
                    view_->setAutoFrame(false);
                    QTimer::singleShot(150,this,[this]{
                        auto image=view_->grabFrame();
                        if(image.isNull()||!image.save(QString::fromStdString((fs::path(smoke_)/"workshop.png").string()))){std::cerr<<"FAIL: workshop capture\n";qApp->exit(1);return;}
                        std::cout<<"PASS: native voxel workshop, timed damage, restored checkpoint and shared sky clock at tick "<<scene().snapshot()->tick<<'\n';qApp->exit(0);
                    });
                }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';qApp->exit(1);}
            });}catch(const std::exception& e){statusBar()->showMessage(e.what());}
        });
    }
};
int main(int argc,char** argv)try {
    const auto launchDirectory=fs::current_path();std::string file,smoke;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help"){std::cout<<"Usage: vsgraysim_voxel_workshop [SCENE_OR_CHECKPOINT] [--smoke OUTPUT_DIRECTORY]\n";return 0;}
        if(arg=="--smoke") {if(++i==argc)throw std::invalid_argument("--smoke requires a directory");smoke=fs::absolute(launchDirectory/argv[i]).string();}
        else if(file.empty())file=fs::absolute(launchDirectory/arg).string();
        else throw std::invalid_argument("unexpected workshop argument: "+arg);
    }
    std::error_code error;auto executable=fs::read_symlink("/proc/self/exe",error);
    auto directory=(error?fs::absolute(argv[0]):executable).parent_path();
    for(int i=0;i<8&&!directory.empty();++i,directory=directory.parent_path())if(fs::exists(directory/"config/voxel/workshop.vscene.json")){fs::current_path(directory);break;}
    if(file.empty())file=fs::absolute("config/voxel/workshop.vscene.json").string();
#ifdef QT6_PLUGIN_PATH
    qputenv("QT_PLUGIN_PATH",QT6_PLUGIN_PATH);
#endif
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))qputenv("QT_QPA_PLATFORM","xcb");
    QApplication app(argc,argv);app.setApplicationName("vsgraysim_voxel_workshop");app.setOrganizationName("vsgraysim");ui::ThemeManager::applyDarkTheme(&app);
    auto validated=earth::VoxelScene::open(file);(void)validated;
    Workshop window(file,smoke);window.show();return app.exec();
}catch(const std::exception& e){std::cerr<<"voxel workshop: "<<e.what()<<'\n';return 1;}
