#include "ControlWorkbench.h"
#include "EquipmentViewProcesses.h"
#include "ScrollableImageView.h"
#include "FlightPanel.h"
#include "EarthPanel.h"
#include "AircraftCockpitPanel.h"
#include "vsgraysim/broker/BrokerNet.h"
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <QProcess>
#include <QSignalBlocker>
#include <QCoreApplication>
#include <algorithm>

namespace vsgraysim::gui::display {
namespace {
SensorType panelType(const QString& kind) {
    if(kind=="ir") return SensorType::IR;
    if(kind=="vis") return SensorType::VIS;
    if(kind=="sar") return SensorType::SAR;
    return SensorType::RADAR;
}
Qt::DockWidgetArea dockArea(const QString& area) {
    if(area=="left") return Qt::LeftDockWidgetArea;
    if(area=="bottom") return Qt::BottomDockWidgetArea;
    if(area=="top") return Qt::TopDockWidgetArea;
    return Qt::RightDockWidgetArea;
}
void cell(QTableWidget* table,int r,int c,const QString& text) {
    auto* item=table->item(r,c);
    if(!item){item=new QTableWidgetItem;table->setItem(r,c,item);}
    if(item->text()!=text)item->setText(text);
}
QTableWidget* linkTable(QWidget* parent) {
    auto* table=new QTableWidget(0,6,parent);
    table->setHorizontalHeaderLabels({QObject::tr("装备 / 平台"),QObject::tr("数据类型"),QObject::tr("已收帧"),QObject::tr("测量时刻 / s"),QObject::tr("链路"),QObject::tr("电源")});
    table->horizontalHeader()->setStretchLastSection(true); table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    return table;
}
}
ControlWorkbench::ControlWorkbench(const QString& mode,const QStringList& forced,QWidget* parent,bool remote,bool separate,bool equipmentView)
    :ui::BaseWindow(tr("显控工作台"),parent),forced_ids_(forced),connect_remote_(remote),separate_views_(remote&&separate),equipment_view_(equipmentView) {
    platform_console_=remote&&!separate_views_&&QStringList{"aircraft","helicopter","airliner","uav","uav_cockpit","aircraft_cockpit"}.contains(mode);
    setWorkspacePersistenceEnabled(false);
    if(remote) BrokerNet::instance().startDisplayRole();
    layout_=WorkspaceProfile::preset(mode=="uav_cockpit" ? "uav" : mode=="aircraft_cockpit" ? "aircraft" : mode.contains("radar") ? "radar" : mode);
    if(mode.isEmpty() && remote && !separate_views_ && !equipment_view_) {
        QSettings settings("vsgraysim","DisplayWorkspace"); QString error; WorkspaceProfile saved;
        if(WorkspaceProfile::parse(settings.value("profile").toByteArray(),saved,error)) layout_=saved;
    }
    if(separate_views_){views_=std::make_unique<EquipmentViewProcesses>(this);layout_.id="assembly";layout_.title=tr("总显控装订");layout_.dock_state.clear();layout_.panels={{"earth","earth",tr("地球三维态势"),0,"left"},{"links","links",tr("装备与显示进程"),0,"bottom"},{"events","events",tr("操作事件"),0,"bottom"}};setWindowTitle(tr("总显控装订 — 在线平台、装备与地球三维"));}
    if(equipment_view_){layout_.dock_state.clear();layout_.panels.erase(std::remove_if(layout_.panels.begin(),layout_.panels.end(),[](const auto& p){return p.kind=="earth"||p.kind=="cockpit"||p.kind=="flight";}),layout_.panels.end());if(mode=="sar"){layout_.id="sar";layout_.title=tr("SAR 显控");layout_.panels={{"sar","sar",tr("SAR 显控"),0,"left"},{"links","links",tr("装备链接"),0,"bottom"}};}setWindowTitle(tr("装备显示子进程 — %1").arg(forced.join(",")));}
    buildBaseStatusBar(); buildBaseMenuBar(); buildBaseToolBar(); buildAssembleUi(); rebuildWorkspace();
    if(equipment_view_){preset_->setEnabled(false);if(auto* dock=findChild<QDockWidget*>("equipment_binding"))dock->hide();}
    resetCameraAction()->setVisible(false); // this workbench has no navigation camera
    timer_=new QTimer(this); connect(timer_,&QTimer::timeout,this,&ControlWorkbench::onTick); timer_->start(50);
    refresh_elapsed_.start();
    if(remote) QTimer::singleShot(0,this,[this]{BrokerNet::instance().pullSnapshot(150);buildSelectTree();});
}
ControlWorkbench::~ControlWorkbench() {
    timer_->stop();
    if(connect_remote_&&!separate_views_&&!equipment_view_) { QSettings s("vsgraysim","DisplayWorkspace");s.setValue("profile",workspaceProfile().serialize()); }
    disconnectEquipment();
}
nlohmann::json ControlWorkbench::connectionStatus() const {
    auto sessions=nlohmann::json::array();for(const auto& s:sessions_)sessions.push_back({{"id",s->equipmentId()},{"frames",s->frames()},{"has_frame",s->hasFrame()},{"connected",s->connected()},{"powered",s->powered()},{"power_pending",s->powerRequestPending()},{"sim_time",s->lastSimTime()},{"status",s->statusText().toStdString()}});
    auto out=nlohmann::json{{"remote_source",connect_remote_},{"linked_ids",linked_ids_},{"sessions",sessions},{"scene_revision",display_surface_revision_},{"separate_views",separate_views_},{"equipment_view",equipment_view_},{"leases",BrokerNet::instance().subscriptionStatus()},{"transport",BrokerNet::instance().transportStatistics()}};
    out["views"]=views_?views_->status():nlohmann::json::array();out["console_processes"]=views_?views_->consoleStatus():nlohmann::json::array();out["processing"]=nlohmann::json::array();
    for(const auto& b:bindings_){auto values=b.panel->property("processingSamples").toList();if(values.empty())continue;std::vector<double> sorted;for(const auto& value:values)sorted.push_back(value.toDouble());std::sort(sorted.begin(),sorted.end());out["processing"].push_back({{"panel",b.spec.id.toStdString()},{"samples",sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]},{"off_gui",b.panel->property("processingOffGui").toBool()},{"frame_id",b.panel->property("processedFrameId").toULongLong()},{"sim_time",b.panel->property("processedSimTime").toDouble()},{"coalesced",b.panel->property("coalescedDisplayInputs").toULongLong()}});}
    out["render_2d"]=nlohmann::json::array();for(auto* image:findChildren<ScrollableImageView*>()){const auto values=image->property("paintSamples").toList();if(values.empty())continue;std::vector<double> sorted;for(const auto& value:values)sorted.push_back(value.toDouble());std::sort(sorted.begin(),sorted.end());out["render_2d"].push_back({{"samples",sorted.size()},{"p50_ms",sorted[(sorted.size()-1)/2]},{"p95_ms",sorted[size_t((sorted.size()-1)*.95)]},{"width",image->width()},{"height",image->height()}});}return out;
}
void ControlWorkbench::openPlatformConsole(const QString& mode,uint32_t platform){if(views_&&!views_->openConsole(mode,platform,selected_equip_,BrokerNet::instance().lastSimMeta().scene_session_id))logEvent(tr("无法打开平台控制台"));}
void ControlWorkbench::closePlatformConsoles(){if(views_)views_->closeConsoles();}
void ControlWorkbench::openEquipmentView(uint32_t id){if(!views_||!linked_ids_.count(id))return;for(const auto& e:selected_equip_)if(e.equipment_id==id){if(!views_->open(e,BrokerNet::instance().lastSimMeta().scene_session_id))logEvent(tr("无法打开装备显示：进程数量或状态限制"));return;}}
void ControlWorkbench::closeEquipmentView(uint32_t id,bool force){if(views_)views_->close(id,force);}
void ControlWorkbench::buildAssembleUi() {
    setDockOptions(AnimatedDocks|AllowNestedDocks|AllowTabbedDocks|GroupedDragging);
    auto* center=new QWidget(this); center->setMaximumSize(0,0); setCentralWidget(center);
    auto* tools=addToolBar(tr("工作台")); tools->setObjectName("workspace_tools");
    preset_=new QComboBox(tools); preset_->setObjectName("workspace_preset"); preset_->setAccessibleName(tr("工作台模板"));
    for(const auto& id:WorkspaceProfile::presetIds()) preset_->addItem(WorkspaceProfile::preset(id).title,id);
    preset_->setCurrentIndex(std::max(0,preset_->findData(layout_.id))); tools->addWidget(new QLabel(tr("显控台："),tools));tools->addWidget(preset_);
    connect(preset_,&QComboBox::activated,this,[this](int index){selectConsole(preset_->itemData(index).toString());});
    tools->addAction(tr("定制面板…"),this,&ControlWorkbench::editWorkspace);
    auto* importAction=tools->addAction(tr("导入工作台…"),this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,tr("导入工作台"),{},tr("工作台 (*.workspace.json *.json)"));
        if(path.isEmpty())return; QString error;if(!importWorkspace(path,error))QMessageBox::warning(this,tr("导入失败"),error);
    });
    importAction->setShortcut(QKeySequence::Open);
    auto* exportAction=tools->addAction(tr("导出工作台…"),this,[this]{
        const auto path=QFileDialog::getSaveFileName(this,tr("导出工作台"),layout_.id+".workspace.json",tr("工作台 (*.workspace.json)"));
        if(path.isEmpty())return; QString error;
        if(!exportWorkspace(path,error))QMessageBox::warning(this,tr("保存失败"),error);else logEvent(tr("工作台已保存：%1").arg(path));
    });
    exportAction->setShortcut(QKeySequence::Save);
    auto* dock=new QDockWidget(tr("装备连接"),this); dock->setObjectName("equipment_binding");
    auto* root=new QWidget(dock); auto* v=new QVBoxLayout(root);
    auto* intro=new QLabel(tr("选择仿真装备并连接。切换模板只改变界面，保留数据会话。"),root); intro->setWordWrap(true);v->addWidget(intro);
    filter_=new QLineEdit(root); filter_->setPlaceholderText(tr("筛选装备 / 平台"));filter_->setClearButtonEnabled(true);v->addWidget(filter_);
    tree_select_=new QTreeWidget(root);tree_select_->setObjectName("equipment_tree");tree_select_->setHeaderLabels({tr("在线平台与装备")});tree_select_->setMinimumWidth(230);v->addWidget(tree_select_,1);
    connect(filter_,&QLineEdit::textChanged,this,[this](const QString& value){
        for(int i=0;i<tree_select_->topLevelItemCount();++i){auto* top=tree_select_->topLevelItem(i);bool any=false;
            for(int k=0;k<top->childCount();++k){auto* row=top->child(k);const bool show=(top->text(0)+row->text(0)).contains(value,Qt::CaseInsensitive);row->setHidden(!show);any|=show;}
            top->setHidden(!any && top->childCount()>0);
        }
    });
    auto* row=new QHBoxLayout;auto* refresh=new QPushButton(tr("发现装备"));auto* bind=new QPushButton(tr("连接所选"));bind->setObjectName("bind_equipment");row->addWidget(refresh);row->addWidget(bind);v->addLayout(row);
    connect(refresh,&QPushButton::clicked,this,[this]{if(connect_remote_)BrokerNet::instance().pullSnapshot(150);buildSelectTree();});connect(bind,&QPushButton::clicked,this,&ControlWorkbench::onBindRequested);
    auto* disconnect=new QPushButton(tr("断开全部装备"));v->addWidget(disconnect);
    connect(disconnect,&QPushButton::clicked,this,[this]{disconnectEquipment();updateSessions();logEvent(tr("已断开全部装备"));});
    dock->setWidget(root);addDockWidget(Qt::LeftDockWidgetArea,dock);
    dock->setMaximumWidth(340);
    state_=new QLabel(tr("等待仿真连接"),this);statusBar()->addPermanentWidget(state_,1);
    auto* consoles=menuBar()->addMenu(tr("显控台"));
    for(const auto& id:WorkspaceProfile::presetIds())consoles->addAction(WorkspaceProfile::preset(id).title,this,[this,id]{selectConsole(id);});
    consoles->addSeparator();consoles->addAction(tr("打开体素场景联调…"),this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("选择体素装备场景"),"config/equipment",tr("装备场景 (*.vequipment.json *.json)"));if(path.isEmpty())return;const auto executable=QCoreApplication::applicationDirPath()+"/vsgraysim_control";if(!QProcess::startDetached(executable,{"--voxel-equipment="+path}))QMessageBox::warning(this,tr("无法打开场景"),tr("无法启动大显控，请检查程序部署目录。"));});
    auto* service=menuBar()->addMenu(tr("数据连接"));
    service->addAction(dock->toggleViewAction());
    service->addAction(tr("发现在线装备"),this,&ControlWorkbench::buildSelectTree);
    service->addAction(tr("连接所选"),this,&ControlWorkbench::onBindRequested);
}
void ControlWorkbench::logEvent(const QString& text) {
    const auto line=QDateTime::currentDateTime().toString("HH:mm:ss")+"  "+text;events_.push_back(line);
    while(events_.size()>300)events_.removeFirst();
    for(auto* view:event_views_)view->appendPlainText(line);
}
void ControlWorkbench::selectConsole(const QString& id){
    if(separate_views_){if(QStringList{"aircraft","helicopter","airliner","uav"}.contains(id)){uint32_t platform=0;auto* current=tree_select_->currentItem();if(current){if(current->parent())current=current->parent();platform=current->data(0,Qt::UserRole).toUInt();}openPlatformConsole(id,platform);}else for(const auto& e:selected_equip_)openEquipmentView(e.equipment_id);return;}
    if(!cockpit_panels_.empty())console_profiles_[layout_.id]=cockpit_panels_.front().panel->cockpitProfilePath();
    auto workspace=WorkspaceProfile::preset(id);if(auto saved=console_profiles_.find(id);saved!=console_profiles_.end())workspace.cockpit_profile=saved->second;
    if(id=="mission"||id=="radar"||id=="infrared"||id=="optical"){
        std::vector<WorkspacePanel> panels;
        for(const auto& e:selected_equip_){const bool matches=id=="mission"||(id=="radar"&&(e.type==SensorType::RADAR||e.type==SensorType::SAR))||(id=="infrared"&&e.type==SensorType::IR)||(id=="optical"&&e.type==SensorType::VIS);if(!matches||e.type==SensorType::LASER)continue;if(panels.size()>=20){logEvent(tr("同屏最多装配20个传感器面板，其余设备可通过来源选择查看"));break;}
            const auto kind=e.type==SensorType::RADAR?"radar":e.type==SensorType::IR?"ir":e.type==SensorType::SAR?"sar":"vis";
            panels.push_back({"device_"+QString::number(e.equipment_id),kind,QString::fromStdString(e.name)+" #"+QString::number(e.equipment_id),e.equipment_id,"right"});}
        if(!panels.empty()){for(const auto& panel:workspace.panels)if(panel.kind=="earth"||panel.kind=="cockpit"||panel.kind=="links"||panel.kind=="events")panels.push_back(panel);workspace.panels=std::move(panels);}
    }
    if(shared_scene_view_)workspace.panels.erase(std::remove_if(workspace.panels.begin(),workspace.panels.end(),[](const auto& panel){return panel.kind=="earth";}),workspace.panels.end());
    applyWorkspace(workspace);
}
WorkspaceProfile ControlWorkbench::workspaceProfile() const {auto p=layout_;if(!cockpit_panels_.empty())p.cockpit_profile=cockpit_panels_.front().panel->cockpitProfilePath();p.dock_state=saveState(1);return p;}
bool ControlWorkbench::setCockpitProfile(const QString& path,QString& error){for(auto& binding:cockpit_panels_)if(!binding.panel->loadCockpitProfile(path,error))return false;layout_.cockpit_profile=path;return true;}
void ControlWorkbench::applyWorkspace(const WorkspaceProfile& p) {
    WorkspaceProfile checked;QString error;
    if(!WorkspaceProfile::parse(p.serialize(),checked,error)){QMessageBox::warning(this,tr("工作台无效"),error);return;}
    if(separate_views_){checked.id="assembly";checked.panels={{"earth","earth",tr("地球三维态势"),0,"left"},{"links","links",tr("装备与显示进程"),0,"bottom"},{"events","events",tr("操作事件"),0,"bottom"}};checked.dock_state.clear();}
    if(equipment_view_)checked.panels.erase(std::remove_if(checked.panels.begin(),checked.panels.end(),[](const auto& panel){return panel.kind=="earth"||panel.kind=="cockpit"||panel.kind=="flight";}),checked.panels.end());
    layout_=checked;rebuildWorkspace();logEvent(tr("已切换到 %1").arg(p.title));
}
bool ControlWorkbench::importWorkspace(const QString& path,QString& error) {
    WorkspaceProfile p;if(!WorkspaceProfile::load(path,p,error))return false;applyWorkspace(p);logEvent(tr("已导入工作台；请连接当前场景装备"));return true;
}
bool ControlWorkbench::exportWorkspace(const QString& path,QString& error)const{return workspaceProfile().save(path,error);}
void ControlWorkbench::rebuildWorkspace() {
    bindings_.clear();flight_panels_.clear();earth_panels_.clear();cockpit_panels_.clear();link_tables_.clear();event_views_.clear();
    for(auto* dock:panel_docks_){removeDockWidget(dock);delete dock;}panel_docks_.clear();
    QDockWidget* left=nullptr;QDockWidget* right=nullptr;QDockWidget* bottom=nullptr;QDockWidget* top=nullptr;int rightCount=0;
    for(const auto& spec:layout_.panels) {
        auto* dock=new QDockWidget(spec.title,this);dock->setObjectName("panel_"+spec.id);QWidget* content=nullptr;
        dock->setProperty("workspacePrimary",spec.kind!="links" && spec.kind!="events");
        if(spec.kind=="cockpit"){auto* panel=new AircraftCockpitPanel(dock);QString error;if(layout_.cockpit_profile!="config/cockpits/f16c.json"&&!panel->loadCockpitProfile(layout_.cockpit_profile,error))logEvent(tr("机型面板加载失败：%1").arg(error));panel->payloadConnectionRequested=[this]{buildSelectTree();if(auto* binding=findChild<QDockWidget*>("equipment_binding")){binding->show();binding->raise();}};
            panel->payloadPowerRequested=[this,panel](uint32_t id){const auto platform=panel->controls()->selectedTelemetry().value("platform_id",uint32_t(0));for(const auto& e:selected_equip_)if(e.equipment_id==id&&e.platform_id==platform)for(auto& session:sessions_)if(session->equipmentId()==id&&session->connected()&&!session->powerRequestPending()){session->setPowered(!session->powered());logEvent(session->lastControlStatus());return;}};
            const bool shared=shared_scene_view_||(layout_.id=="mission"&&std::any_of(layout_.panels.begin(),layout_.panels.end(),[](const auto& p){return p.kind=="earth";}));
            panel->useSharedSceneView(shared);cockpit_panels_.push_back({panel,spec.equipment_id});content=panel;}
        else if(spec.kind=="flight"){auto* panel=new FlightPanel(dock);flight_panels_.push_back({panel,spec.equipment_id});content=panel;}
        else if(spec.kind=="earth"){auto* panel=new EarthPanel(dock);earth_panels_.push_back({panel,spec.equipment_id});content=panel;}
        else if(spec.kind=="links"){auto* table=linkTable(dock);table->setMinimumHeight(160);link_tables_.push_back(table);content=table;}
        else if(spec.kind=="events"){auto* view=new QPlainTextEdit(dock);view->setReadOnly(true);view->document()->setMaximumBlockCount(300);view->setPlainText(events_.join('\n'));event_views_.push_back(view);content=view;}
        else {
            auto* box=new QWidget(dock);auto* v=new QVBoxLayout(box);v->setContentsMargins(4,4,4,4);
            auto* label=new QLabel(spec.equipment_id ? tr("绑定装备 #%1").arg(spec.equipment_id) : tr("自动绑定首个已连接的 %1 装备").arg(spec.kind.toUpper()),box);v->addWidget(label);
            auto* source=new QComboBox(box);source->setObjectName("source_"+spec.id);source->addItem(tr("自动选择同类设备"),0u);
            for(const auto& e:selected_equip_)if(e.type==panelType(spec.kind))source->addItem(QString("%1 #%2").arg(QString::fromStdString(e.name)).arg(e.equipment_id),e.equipment_id);
            if(spec.equipment_id&&source->findData(spec.equipment_id)<0)source->addItem(tr("等待设备 #%1").arg(spec.equipment_id),spec.equipment_id);source->setCurrentIndex(std::max(0,source->findData(spec.equipment_id)));v->addWidget(source);
            auto panel=createPanelFor(panelType(spec.kind),box);
            if(panel){auto* ptr=panel.release();v->addWidget(ptr,1);bindings_.push_back({spec,ptr});connect(source,&QComboBox::activated,this,[this,source,id=spec.id,label](int index){const auto equipment=source->itemData(index).toUInt();for(auto& binding:bindings_)if(binding.spec.id==id){binding.spec.equipment_id=equipment;binding.panel->tick(sessionFor(binding.spec));}for(auto& entry:layout_.panels)if(entry.id==id)entry.equipment_id=equipment;label->setText(equipment?tr("绑定设备 #%1").arg(equipment):tr("自动绑定同类设备"));});}content=box;
        }
        dock->setWidget(content);addDockWidget(dockArea(spec.area),dock);panel_docks_.push_back(dock);
        if(spec.area=="bottom"){if(bottom)tabifyDockWidget(bottom,dock);else bottom=dock;}
        else if(spec.area=="top"){if(top)tabifyDockWidget(top,dock);else top=dock;}
        else if(spec.area=="left"){if(left)splitDockWidget(left,dock,Qt::Vertical);else {left=dock;splitDockWidget(findChild<QDockWidget*>("equipment_binding"),dock,Qt::Horizontal);}}
        else {if(right){if(rightCount<2)splitDockWidget(right,dock,Qt::Vertical);else tabifyDockWidget(right,dock);}else right=dock;++rightCount;}
    }
    if(bottom)bottom->raise();
    if(layout_.dock_state.isEmpty()) {
        resizeDocks({findChild<QDockWidget*>("equipment_binding")},{260},Qt::Horizontal);
        if(left)resizeDocks({left},{440},Qt::Horizontal);
        if(right)resizeDocks({right},{620},Qt::Horizontal);
        if(bottom)resizeDocks({bottom},{190},Qt::Vertical);
        findChild<QDockWidget*>("equipment_binding")->setVisible(layout_.id!="aircraft"&&layout_.id!="uav"&&layout_.id!="helicopter"&&layout_.id!="airliner");
        if((layout_.id=="aircraft"||layout_.id=="uav"||layout_.id=="helicopter"||layout_.id=="airliner")&&left){
            // The cockpit owns the initial workspace. Auxiliary sensor/Earth
            // docks remain available from View without opening a second globe.
            for(auto* dock:panel_docks_)if(!dynamic_cast<AircraftCockpitPanel*>(dock->widget()))dock->hide();
            resizeDocks({left},{1050},Qt::Horizontal);resizeDocks({left},{650},Qt::Vertical);
        }
    }
    if(!layout_.dock_state.isEmpty() && !restoreState(layout_.dock_state,1))logEvent(tr("已恢复模板布局：停靠状态不可用"));
    setWindowTitle(tr("显控工作台 · %1").arg(layout_.title));preset_->setCurrentIndex(preset_->findData(layout_.id));
    finalizeWorkspace(true);updateSessions();
    if((layout_.id=="aircraft"||layout_.id=="uav"||layout_.id=="helicopter"||layout_.id=="airliner")&&layout_.dock_state.isEmpty())QTimer::singleShot(0,this,[this]{
        const bool auxiliaryVisible=std::any_of(panel_docks_.begin(),panel_docks_.end(),[](auto* dock){return !dynamic_cast<AircraftCockpitPanel*>(dock->widget())&&dock->isVisible();});
        if(!auxiliaryVisible)for(auto* bar:findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly))bar->hide();
    });
}
EquipmentSession* ControlWorkbench::sessionFor(const WorkspacePanel& spec) const {
    for(const auto& s:sessions_)if(s->sensorType()==panelType(spec.kind) && (!spec.equipment_id || spec.equipment_id==s->equipmentId()))return s.get();
    return nullptr;
}
void ControlWorkbench::buildSelectTree() {
    std::set<uint32_t> checked;
    for(int t=0;t<tree_select_->topLevelItemCount();++t)for(int i=0;i<tree_select_->topLevelItem(t)->childCount();++i){auto* row=tree_select_->topLevelItem(t)->child(i);if(row->checkState(0)==Qt::Checked)checked.insert(row->data(0,Qt::UserRole).toUInt());}
    bool online=connect_remote_ ? BrokerNet::instance().simOnline() : SimDataBroker::instance().isServerOnline();
    online_equip_=SimDataBroker::instance().scanOnlineEquipment();tree_select_->clear();std::map<uint32_t,QTreeWidgetItem*> platforms;
    for(const auto& e:online_equip_) {
        auto*& top=platforms[e.platform_id];if(!top){top=new QTreeWidgetItem(tree_select_);top->setText(0,QString::fromStdString(e.platform_name.empty()?"未挂载平台":e.platform_name));top->setData(0,Qt::UserRole,e.platform_id);}
        auto* item=new QTreeWidgetItem(top);item->setText(0,QString("%1 · %2 · #%3").arg(QString::fromStdString(e.name),QString::fromUtf8(sensorTypeName(e.type))).arg(e.equipment_id));item->setData(0,Qt::UserRole,e.equipment_id);
        item->setCheckState(0,checked.count(e.equipment_id)||linked_ids_.count(e.equipment_id)||forced_ids_.contains(QString::number(e.equipment_id)) ? Qt::Checked : Qt::Unchecked);
    }
    const auto telemetry=connect_remote_?nlohmann::json::parse(BrokerNet::instance().lastSimMeta().platforms_json,nullptr,false):local_platforms_;
    if(online&&telemetry.is_array())for(const auto& p:telemetry)if(p.is_object()&&p.contains("platform_id")&&p["platform_id"].is_number_unsigned()){
        const auto id=p["platform_id"].get<uint32_t>();if(!platforms.count(id)){auto* item=new QTreeWidgetItem(tree_select_);item->setText(0,QString::fromStdString(p.value("name",std::string("在线平台")))+tr(" · 无挂载装备"));item->setData(0,Qt::UserRole,id);platforms[id]=item;}
    }
    if(online_equip_.empty()){auto* item=new QTreeWidgetItem(tree_select_);item->setText(0,tr("尚未发现装备"));item->setFlags(Qt::NoItemFlags);}
    tree_select_->expandAll();state_->setText(online ? tr("仿真已连接 · 请选择装备") : tr("仿真未连接 · 请启动主控并初始化场景"));
    if(!filter_->text().isEmpty()) {
        const auto filter=filter_->text();filter_->clear();filter_->setText(filter);
    }
    if(!forced_bound_ && !forced_ids_.isEmpty() && !online_equip_.empty()) {
        bool found=true; for(const auto& id:forced_ids_)found=found && std::any_of(online_equip_.begin(),online_equip_.end(),[&](const auto& e){return QString::number(e.equipment_id)==id;});
        if(found&& (expected_scene_.empty()||BrokerNet::instance().lastSimMeta().scene_session_id==expected_scene_)){onBindRequested();forced_bound_=linked_ids_.size()>=std::size_t(forced_ids_.size());}
        else state_->setText(tr("仿真已连接 · 等待指定装备上线"));
    }
}
void ControlWorkbench::onBindRequested() {
    std::set<uint32_t> selected;
    for(int t=0;t<tree_select_->topLevelItemCount();++t)for(int i=0;i<tree_select_->topLevelItem(t)->childCount();++i){auto* item=tree_select_->topLevelItem(t)->child(i);if(item->checkState(0)==Qt::Checked)selected.insert(item->data(0,Qt::UserRole).toUInt());}
    std::vector<EquipmentInfo> equip;for(const auto& e:online_equip_)if(selected.count(e.equipment_id))equip.push_back(e);
    if(equip.empty()){logEvent(tr("请先勾选需要连接的装备"));return;}rebuildForSelection(equip);
}
void ControlWorkbench::disconnectEquipment() {
    if(views_)views_->closeAll();
    if(world_lease_){BrokerNet::instance().linkDetach(0);world_lease_=false;}
    for(auto& binding:cockpit_panels_){binding.panel->releaseControl(tr("装备连接已断开"));binding.panel->updatePayloads({});}
    for(auto& binding:flight_panels_)binding.panel->releaseControl(tr("装备连接已断开"));
    for(auto& binding:bindings_)binding.panel->tick(nullptr);
    for(auto& session:sessions_)session->unbind();sessions_.clear();selected_equip_.clear();
    for(auto* table:link_tables_)table->setRowCount(0);
    if(connect_remote_)for(auto id:linked_ids_)BrokerNet::instance().linkDetach(id);
    linked_ids_.clear();
}
void ControlWorkbench::rebuildForSelection(const std::vector<EquipmentInfo>& equip) {
    disconnectEquipment();
    for(const auto& e:equip) {
        if(connect_remote_ && !BrokerNet::instance().linkAttach(e.equipment_id,!separate_views_)){logEvent(tr("装备 #%1 建链失败").arg(e.equipment_id));continue;}
        if(separate_views_){linked_ids_.insert(e.equipment_id);selected_equip_.push_back(e);openEquipmentView(e.equipment_id);continue;}
        EquipmentChannelSpec spec;spec.sensor_type=e.type;spec.label=QString::fromStdString(e.name);
        auto session=std::make_unique<EquipmentSession>(spec);
        if(!session->bind(e.equipment_id,e.name)){if(connect_remote_)BrokerNet::instance().linkDetach(e.equipment_id);logEvent(tr("装备 #%1 订阅失败").arg(e.equipment_id));continue;}
        linked_ids_.insert(e.equipment_id);selected_equip_.push_back(e);sessions_.push_back(std::move(session));
    }
    logEvent(tr("已连接 %1 / %2 个装备").arg(sessions_.size()).arg(equip.size()));updateSessions();
    if(separate_views_)return;
    if(layout_.id=="radar"||layout_.id=="infrared"||layout_.id=="optical"){selectConsole(layout_.id);return;}
    if(layout_.id=="mission" && layout_.dock_state.isEmpty()&&!shared_scene_view_) {
        auto p=WorkspaceProfile::preset("mission");p.panels.clear();
        for(const auto& e:selected_equip_) {
            if(p.panels.size()>=20){logEvent(tr("综合台最多显示 20 个观测面板，可定制绑定其余装备"));break;}
            QString kind;
            switch(e.type){case SensorType::RADAR:kind="radar";break;case SensorType::IR:kind="ir";break;case SensorType::SAR:kind="sar";break;case SensorType::VIS:kind="vis";break;default:continue;}
            p.panels.push_back({"eq_"+QString::number(e.equipment_id),kind,QString::fromStdString(e.name),e.equipment_id,p.panels.empty()?"left":"right"});
        }
        p.panels.push_back({"cockpit","cockpit",tr("飞机操作台"),0,"right"});
        p.panels.push_back({"earth","earth",tr("地球态势"),0,"left"});
        p.panels.push_back({"links","links",tr("装备与数据链"),0,"bottom"});p.panels.push_back({"events","events",tr("操作事件"),0,"bottom"});
        layout_=p;rebuildWorkspace();
    }
}
void ControlWorkbench::updateSessions() {
    if(separate_views_){for(auto* table:link_tables_){table->setHorizontalHeaderLabels({tr("装备 / 平台"),tr("数据类型"),tr("显示 PID / 状态"),tr("后台产品"),tr("链接 / 租约"),tr("显示控制")});table->setRowCount(int(selected_equip_.size()));const auto views=views_->status();for(int row=0;row<int(selected_equip_.size());++row){const auto& e=selected_equip_[row];cell(table,row,0,QString::fromStdString(e.name)+QString(" #%1").arg(e.equipment_id));cell(table,row,1,QString::fromUtf8(sensorTypeName(e.type)));const auto it=std::find_if(views.begin(),views.end(),[&](const auto& v){return v.at("id")==e.equipment_id;});QString process=tr("未打开"),frames=tr("等待子进程");if(it!=views.end()){process=QString("%1 · %2").arg(it->value("pid",int64_t(0))).arg(it->value("process_state",0)==0?tr("已退出"):tr("运行中"));if(it->contains("sessions"))for(const auto& session:it->at("sessions"))frames=QString::number(session.value("frames",uint64_t(0)))+tr(" 帧");}cell(table,row,2,process);cell(table,row,3,frames);cell(table,row,4,BrokerNet::instance().leaseActive(e.equipment_id)?tr("装订有效 · 数据由子进程订阅"):tr("装订失效，请重新连接"));auto* button=qobject_cast<QPushButton*>(table->cellWidget(row,5));if(!button){button=new QPushButton(tr("打开 / 恢复显示"),table);table->setCellWidget(row,5,button);const auto id=e.equipment_id;connect(button,&QPushButton::clicked,this,[this,id]{openEquipmentView(id);});}}}return;}

    for(const auto& binding:bindings_)if(auto* source=findChild<QComboBox*>("source_"+binding.spec.id)){
        std::vector<std::pair<uint32_t,QString>> choices{{0,tr("自动选择同类设备")}};
        for(const auto& e:selected_equip_)if(e.type==panelType(binding.spec.kind))choices.push_back({e.equipment_id,QString("%1 #%2").arg(QString::fromStdString(e.name)).arg(e.equipment_id)});
        if(binding.spec.equipment_id&&std::none_of(choices.begin(),choices.end(),[&](const auto& p){return p.first==binding.spec.equipment_id;}))choices.push_back({binding.spec.equipment_id,tr("等待设备 #%1").arg(binding.spec.equipment_id)});
        bool changed=source->count()!=int(choices.size());for(int i=0;!changed&&i<int(choices.size());++i)changed=source->itemData(i).toUInt()!=choices[i].first||source->itemText(i)!=choices[i].second;
        if(changed){QSignalBlocker block(source);source->clear();for(const auto& [id,label]:choices)source->addItem(label,id);source->setCurrentIndex(std::max(0,source->findData(binding.spec.equipment_id)));}
    }
    for(auto* table:link_tables_) {
        table->setRowCount(int(sessions_.size()));
        for(int i=0;i<int(sessions_.size());++i){auto& s=*sessions_[i];cell(table,i,0,QString::fromStdString(s.equipmentName()));cell(table,i,1,QString::fromUtf8(sensorTypeName(s.sensorType())).toUpper());cell(table,i,2,QString::number(s.frames()));cell(table,i,3,s.hasFrame()?QString::number(s.lastSimTime(),'f',3):tr("等待首帧"));cell(table,i,4,s.statusText());
            auto* power=qobject_cast<QPushButton*>(table->cellWidget(i,5));
            if(!power){power=new QPushButton(table);table->setCellWidget(i,5,power);const auto id=s.equipmentId();connect(power,&QPushButton::clicked,this,[this,id]{for(auto& session:sessions_)if(session->equipmentId()==id){session->setPowered(!session->powered());logEvent(session->lastControlStatus());}});}
            power->setText(s.powerRequestPending()?tr("等待确认"):s.powered()?tr("关闭"):tr("开启"));power->setEnabled(s.connected() && !s.powerRequestPending());
        }
    }
}
void ControlWorkbench::onTick() {
    if(views_)views_->poll();
    if(!cockpit_panels_.empty()) {
        auto meta=BrokerNet::instance().lastSimMeta();meta.online=BrokerNet::instance().simOnline();if(!connect_remote_&&local_scene_state_){meta.online=SimDataBroker::instance().isServerOnline();meta.sim_time=local_sim_time_;meta.running=local_running_;meta.session_id="local-voxel";meta.flight_control_available=false;meta.platforms_json=local_platforms_.dump();}auto telemetry=nlohmann::json::parse(meta.platforms_json,nullptr,false);if(!telemetry.is_array())telemetry=nlohmann::json::array();
        auto earth=nlohmann::json::parse(meta.earth_json,nullptr,false);if(!earth.is_object())earth=nlohmann::json::object();
        for(auto& binding:cockpit_panels_) {
            uint32_t preferred=preferred_platform_?preferred_platform_:selected_equip_.empty()?0:selected_equip_.front().platform_id;auto samples=telemetry;
            if(binding.equipment_id){preferred=0;for(const auto& e:selected_equip_)if(e.equipment_id==binding.equipment_id)preferred=e.platform_id;samples=nlohmann::json::array();for(const auto& s:telemetry)if(preferred&&s.is_object()&&s.contains("platform_id")&&s["platform_id"].is_number_integer()&&s["platform_id"].get<double>()>=1&&s["platform_id"].get<double>()<=UINT32_MAX&&s["platform_id"].get<uint32_t>()==preferred)samples.push_back(s);}
            binding.panel->updateState(earth,samples,meta.sim_time,meta.online,meta.running,meta.session_id,meta.flight_control_available,preferred);
        }
    }
    auto refreshProducts=[&] {
        const bool ready=!display_surface_revision_ || (std::all_of(earth_panels_.begin(),earth_panels_.end(),
            [&](const auto& binding){return binding.panel->surfaceReady(display_surface_revision_);})&&std::all_of(cockpit_panels_.begin(),cockpit_panels_.end(),[&](const auto& b){return b.panel->usesSharedSceneView()||b.panel->earthDisplay()->surfaceReady(display_surface_revision_);}));
        for(auto& s:sessions_) s->tick(display_surface_revision_,equipment_view_?true:ready,!connect_remote_&&local_scene_state_&&!local_running_);
        for(auto& b:bindings_) b.panel->tick(sessionFor(b.spec));
        std::vector<std::pair<EquipmentSession*,uint32_t>> payloads;for(auto& session:sessions_)for(const auto& e:selected_equip_)if(e.equipment_id==session->equipmentId())payloads.emplace_back(session.get(),e.platform_id);
        for(auto& binding:cockpit_panels_)binding.panel->updatePayloads(payloads);
        // Power/pause state is part of the measurement display, not the slower
        // discovery refresh. Keep the table and source badge in the same UI tick.
        updateSessions();
        if(connect_remote_){const auto current=BrokerNet::instance().lastSimMeta();state_->setText(current.online?tr("%1 · %2 · t=%3 s · %4 个装备").arg(QString::fromStdString(current.scene),current.running?tr("运行中"):tr("已暂停")).arg(current.sim_time,0,'f',3).arg(sessions_.size()):tr("仿真未连接 · 当前画面保留最后测量"));}
    };
    if(++ticks_%10){refreshProducts();return;}
    updateSessions();auto meta=BrokerNet::instance().lastSimMeta();
    if(platform_console_&&meta.online&&!world_lease_&&!forced_bound_&&(expected_scene_.empty()||expected_scene_==meta.scene_session_id))world_lease_=BrokerNet::instance().linkAttach(0,false);
    uint64_t received=0;for(const auto& session:sessions_)received+=session->frames();setFrameLabel(received);
    const auto elapsed=refresh_elapsed_.restart();if(elapsed>0)setFpsLabel(10000./elapsed);
    meta.online=connect_remote_?BrokerNet::instance().simOnline():SimDataBroker::instance().isServerOnline();
    if(!connect_remote_){meta.scene="体素装备场景";meta.sim_time=local_sim_time_;meta.running=local_running_;meta.platforms_json=local_platforms_.dump();meta.flight_control_available=false;meta.session_id="local-voxel";}
    const auto scene=QString::fromStdString(meta.scene);
    const auto sceneSession=meta.scene_session_id.empty()?meta.session_id:meta.scene_session_id;
    if(meta.online) {
        if((!last_session_.empty() && sceneSession!=last_session_) || (!last_scene_.isEmpty() && scene!=last_scene_) || (last_sim_time_>=0 && meta.sim_time+1e-9<last_sim_time_)) {
            qWarning("Display reset: scene %s -> %s, session %s -> %s, time %.9f -> %.9f",qPrintable(last_scene_),qPrintable(scene),last_session_.c_str(),sceneSession.c_str(),last_sim_time_,meta.sim_time);
            disconnectEquipment(); updateSessions(); logEvent(tr("仿真场景已切换或时间已复位，请重新连接装备"));
            forced_bound_=true; // explicit operator reconnect after a scenario reset
        }
        last_scene_=scene; last_sim_time_=meta.sim_time;last_session_=sceneSession;
    }
    auto platforms=nlohmann::json::parse(meta.platforms_json,nullptr,false);if(platforms.is_discarded())platforms=nlohmann::json::array();
    const uint32_t preferred=preferred_platform_?preferred_platform_:selected_equip_.empty()?0:selected_equip_.front().platform_id;
    for(const auto& binding:flight_panels_) {
        binding.panel->updateControlContext(meta.session_id,meta.flight_control_available);
        if(!binding.equipment_id){binding.panel->updateTelemetry(platforms,meta.online,meta.running,preferred);continue;}
        auto samples=nlohmann::json::array();uint32_t platform=0;
        for(const auto& e:selected_equip_)if(e.equipment_id==binding.equipment_id)platform=e.platform_id;
        if(platform && platforms.is_array())for(const auto& sample:platforms)
            if(sample.is_object() && sample.contains("platform_id") && sample["platform_id"].is_number_integer() &&
               sample["platform_id"].get<double>()>=0 && sample["platform_id"].get<double>()<=UINT32_MAX && sample["platform_id"].get<uint32_t>()==platform)samples.push_back(sample);
        binding.panel->updateTelemetry(samples,meta.online,meta.running,platform);
    }
    auto earth=nlohmann::json::parse(meta.earth_json,nullptr,false);
    if(!earth.is_object())earth=nlohmann::json::object();
    std::string earthMap=local_earth_map_;
    if(connect_remote_&&earth.contains("earth")&&earth["earth"].is_object()){
        const auto map=earth["earth"].find("map");
        if(map!=earth["earth"].end()&&map->is_string())earthMap=map->get<std::string>();
    }
    for(auto& binding:bindings_)binding.panel->setEarthMap(earthMap);
    for(const auto& binding:earth_panels_) {
        uint32_t platform=preferred;
        if(binding.equipment_id) {
            platform=0;
            for(const auto& e:selected_equip_)if(e.equipment_id==binding.equipment_id)platform=e.platform_id;
        }
        binding.panel->updateContext(earth,meta.sim_time,meta.online,meta.running);
        binding.panel->updateTelemetry(platforms,platform);
    }
    if(connect_remote_)display_surface_revision_=earth.contains("voxel_scene")?earth.at("voxel_scene").at("content_revision").get<uint64_t>():earth.value("surface_revision",uint64_t(0));
    refreshProducts();
    state_->setText(meta.online ? tr("%1 · %2 · t=%3 s · %4 个装备").arg(QString::fromStdString(meta.scene),meta.running?tr("运行中"):tr("已暂停")).arg(meta.sim_time,0,'f',3).arg(sessions_.size()) : tr("仿真未连接 · 当前画面保留最后测量"));
    if(connect_remote_ && ticks_%40==0)buildSelectTree();
}
void ControlWorkbench::editWorkspace() {
    QDialog dialog(this);dialog.setWindowTitle(tr("定制工作台面板"));dialog.resize(820,520);auto* v=new QVBoxLayout(&dialog);
    auto* help=new QLabel(tr("添加面板并绑定已连接装备；自动选择表示首个匹配类型。拖动停靠标题可调整位置，视图菜单可隐藏面板。"));help->setWordWrap(true);v->addWidget(help);
    auto* title=new QLineEdit(layout_.title,&dialog);title->setPlaceholderText(tr("工作台名称"));v->addWidget(title);
    auto* table=new QTableWidget(0,4,&dialog);table->setHorizontalHeaderLabels({tr("面板标题"),tr("面板类型"),tr("装备绑定"),tr("默认区域")});table->horizontalHeader()->setStretchLastSection(true);v->addWidget(table,1);
    auto add=[&](const WorkspacePanel& spec){int r=table->rowCount();table->insertRow(r);auto* item=new QTableWidgetItem(spec.title);item->setData(Qt::UserRole,spec.id);table->setItem(r,0,item);
        auto* kind=new QComboBox(table);kind->addItems(WorkspaceProfile::panelKinds());kind->setCurrentText(spec.kind);table->setCellWidget(r,1,kind);
        auto* eq=new QComboBox(table);eq->addItem(tr("自动选择"),0u);for(const auto& e:selected_equip_)eq->addItem(QString("#%1 %2").arg(e.equipment_id).arg(QString::fromStdString(e.name)),e.equipment_id);
        if(spec.equipment_id && eq->findData(spec.equipment_id)<0)eq->addItem(tr("#%1（尚未连接）").arg(spec.equipment_id),spec.equipment_id);eq->setCurrentIndex(std::max(0,eq->findData(spec.equipment_id)));table->setCellWidget(r,2,eq);
        auto* area=new QComboBox(table);area->addItems({"left","right","bottom","top"});area->setCurrentText(spec.area);table->setCellWidget(r,3,area);
    };
    for(const auto& panel:layout_.panels)add(panel);
    auto* row=new QHBoxLayout;auto* plus=new QPushButton(tr("添加面板"));auto* minus=new QPushButton(tr("删除所选面板"));row->addWidget(plus);row->addWidget(minus);row->addStretch();v->addLayout(row);
    connect(plus,&QPushButton::clicked,&dialog,[&]{if(table->rowCount()<24)add({QUuid::createUuid().toString(QUuid::Id128),"vis",tr("观测图像"),0});});
    connect(minus,&QPushButton::clicked,&dialog,[&]{if(table->currentRow()>=0)table->removeRow(table->currentRow());});
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);v->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{WorkspaceProfile p;p.id="custom";p.title=title->text().trimmed();
        for(int r=0;r<table->rowCount();++r)p.panels.push_back({table->item(r,0)->data(Qt::UserRole).toString(),qobject_cast<QComboBox*>(table->cellWidget(r,1))->currentText(),table->item(r,0)->text(),qobject_cast<QComboBox*>(table->cellWidget(r,2))->currentData().toUInt(),qobject_cast<QComboBox*>(table->cellWidget(r,3))->currentText()});
        WorkspaceProfile checked;QString error;if(!WorkspaceProfile::parse(p.serialize(),checked,error)){QMessageBox::warning(&dialog,tr("面板配置无效"),error);return;}applyWorkspace(checked);dialog.accept();
    });dialog.exec();
}
}
