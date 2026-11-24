#include "WorkspaceProfile.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
namespace vsgraysim::gui::display {
QStringList WorkspaceProfile::presetIds(){ return {"mission","aircraft","radar","infrared","optical","uav","helicopter","airliner"}; }
QStringList WorkspaceProfile::panelKinds(){ return {"radar","ir","vis","sar","flight","cockpit","earth","links","events"}; }
WorkspaceProfile WorkspaceProfile::preset(const QString& id) {
    WorkspaceProfile p; p.id=id;
    if(id=="aircraft"||id=="helicopter"||id=="airliner") {p.title=QStringLiteral("飞机操作台"); p.panels={{"cockpit","cockpit",QStringLiteral("飞机座舱 · 实景与操纵"),0,"left"},{"radar","radar",QStringLiteral("机载雷达"),0},{"vision","vis",QStringLiteral("前视图像"),0}};}
    else if(id=="radar") {p.title=QStringLiteral("雷达显控台"); p.panels={{"radar","radar",QStringLiteral("雷达信号与航迹"),0,"left"}};}
    else if(id=="infrared") {p.title=QStringLiteral("红外显控台");p.panels={{"ir","ir",QStringLiteral("红外观测 · 选择独立设备"),0,"left"}};}
    else if(id=="optical") {p.title=QStringLiteral("光学显控台");p.panels={{"vision","vis",QStringLiteral("可见光观测 · 选择独立设备"),0,"left"}};}
    else if(id=="uav") {p.title=QStringLiteral("无人机操作台");p.cockpit_profile="config/cockpits/mq9.json"; p.panels={{"cockpit","cockpit",QStringLiteral("无人机地面站 · 操纵与载荷"),0,"left"},{"vision","vis",QStringLiteral("可见光载荷"),0},{"ir","ir",QStringLiteral("红外载荷"),0}};}
    else { p.id="mission"; p.panels={{"radar","radar",QStringLiteral("雷达任务"),0,"left"},{"ir","ir",QStringLiteral("红外任务"),0},{"vision","vis",QStringLiteral("光学任务"),0},{"cockpit","cockpit",QStringLiteral("飞机操作台"),0}}; }
    if(id=="helicopter"){p.title=QStringLiteral("直升机操作台");p.cockpit_profile="config/cockpits/ah64d.json";}
    if(id=="airliner"){p.title=QStringLiteral("客机操作台");p.cockpit_profile="config/cockpits/a320.json";}
    p.panels.push_back({"earth","earth",QStringLiteral("地球态势"),0,p.id=="mission"?"left":"right"});
    p.panels.push_back({"links","links",QStringLiteral("装备与数据链"),0,"bottom"});
    p.panels.push_back({"events","events",QStringLiteral("操作事件"),0,"bottom"}); return p;
}
QByteArray WorkspaceProfile::serialize() const {
    QJsonArray rows; for(const auto& panel:panels) rows.append(QJsonObject{{"id",panel.id},{"kind",panel.kind},{"title",panel.title},{"equipment_id",double(panel.equipment_id)},{"area",panel.area}});
    return QJsonDocument(QJsonObject{{"schema","vsgraysim.workspace"},{"version",1},{"id",id},{"title",title},{"cockpit_profile",cockpit_profile},{"panels",rows},{"dock_state",QString::fromLatin1(dock_state.toBase64())}}).toJson();
}
bool WorkspaceProfile::parse(const QByteArray& bytes,WorkspaceProfile& out,QString& error) {
    error.clear(); auto fail=[&](const QString& reason){error=reason;return false;};
    if(bytes.size()>1024*1024) return fail(QStringLiteral("工作台文件超过 1 MiB"));
    QJsonParseError e; const auto doc=QJsonDocument::fromJson(bytes,&e); const auto j=doc.object();
    if(e.error!=QJsonParseError::NoError || !doc.isObject() || j["schema"]!="vsgraysim.workspace" || j["version"]!=1)
        return fail(QStringLiteral("无效工作台文件或不支持的版本"));
    WorkspaceProfile p; p.id=j["id"].toString(); p.title=j["title"].toString();
    if(j.contains("cockpit_profile")){if(!j["cockpit_profile"].isString()||j["cockpit_profile"].toString().isEmpty()||j["cockpit_profile"].toString().size()>4096)return fail(QStringLiteral("机型面板路径无效"));p.cockpit_profile=j["cockpit_profile"].toString();}
    QRegularExpression identifier("^[A-Za-z0-9_-]{1,64}$");
    if(!identifier.match(p.id).hasMatch() || p.title.trimmed().isEmpty() || p.title.size()>100 || !j["panels"].isArray()) return fail(QStringLiteral("工作台名称或标识无效"));
    const auto rows=j["panels"].toArray(); if(rows.isEmpty() || rows.size()>24) return fail(QStringLiteral("工作台需要 1–24 个面板"));
    QSet<QString> ids;
    for(const auto& row:rows) {
        if(!row.isObject()) return fail(QStringLiteral("面板描述必须为对象"));
        const auto r=row.toObject(); WorkspacePanel panel;
        panel.id=r["id"].toString(); panel.kind=r["kind"].toString(); panel.title=r["title"].toString(); panel.area=r["area"].toString("right");
        const double eq=r["equipment_id"].toDouble(-1);
        if(!identifier.match(panel.id).hasMatch() || ids.contains(panel.id) || !panelKinds().contains(panel.kind) || panel.title.trimmed().isEmpty() || panel.title.size()>100 ||
           !std::isfinite(eq) || eq<0 || eq>UINT32_MAX || std::floor(eq)!=eq || !QStringList{"left","right","bottom","top"}.contains(panel.area)) return fail(QStringLiteral("面板 ID、类型、标题、区域或装备绑定无效"));
        ids.insert(panel.id); panel.equipment_id=uint32_t(eq); p.panels.push_back(panel);
    }
    const auto encoded=j["dock_state"].toString().toLatin1();
    if(j.contains("dock_state") && !j["dock_state"].isString()) return fail(QStringLiteral("停靠布局必须为 base64 字符串"));
    const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if(!decoded || decoded.decoded.size()>256*1024) return fail(QStringLiteral("停靠布局数据无效"));
    p.dock_state=decoded.decoded; out=std::move(p); return true;
}
bool WorkspaceProfile::save(const QString& path,QString& error) const {
    WorkspaceProfile checked; const auto bytes=serialize(); if(!parse(bytes,checked,error)) return false;
    QSaveFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()){error=f.errorString();return false;} return true;
}
bool WorkspaceProfile::load(const QString& path,WorkspaceProfile& p,QString& error) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}
    if(f.size()>1024*1024){error=QStringLiteral("工作台文件过大");return false;} return parse(f.readAll(),p,error);
}
}
