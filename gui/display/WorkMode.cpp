#include "WorkMode.h"

#include <algorithm>

namespace vsgraysim { namespace gui { namespace display {

const char* roleName(EquipmentWorkRole role)
{
    switch (role) {
        case EquipmentWorkRole::Search:   return "search";
        case EquipmentWorkRole::Guidance: return "guidance";
        case EquipmentWorkRole::Imaging:  return "imaging";
        case EquipmentWorkRole::Seek:     return "seek";
        case EquipmentWorkRole::Cockpit:  return "cockpit";
        case EquipmentWorkRole::Fusion:   return "fusion";
    }
    return "search";
}

WorkModeRegistry& WorkModeRegistry::instance()
{
    static WorkModeRegistry reg;
    return reg;
}

namespace {

/// 由装备名判断是否为"导引头/寻的"类 (导弹末制导)
bool isSeekerLike(const std::string& name)
{
    const char* hits[] = {"导引头", "seeker", "seek", "missile", "导弹", "寻"};
    for (const char* h : hits)
        if (name.find(h) != std::string::npos) return true;
    return false;
}

/// 由装备类型 + 名称/角色推断其"工作方式"(影响装配什么处理/控制/面板)
EquipmentWorkRole roleForEquipment(const vsgraysim::EquipmentInfo& e)
{
    if (isSeekerLike(e.name)) return EquipmentWorkRole::Seek;
    switch (e.type) {
    case SensorType::RADAR:
        return e.role == 1 ? EquipmentWorkRole::Guidance : EquipmentWorkRole::Search;
    case SensorType::SAR:  return EquipmentWorkRole::Imaging;
    case SensorType::IR:   return EquipmentWorkRole::Imaging;
    case SensorType::VIS:  return EquipmentWorkRole::Imaging;
    case SensorType::LASER:return EquipmentWorkRole::Guidance;
    }
    return EquipmentWorkRole::Search;
}

} // namespace

void WorkModeRegistry::registerMode(const WorkModeProfile& profile)
{
    for (auto& m : modes_) {
        if (m.id == profile.id) { m = profile; return; }
    }
    modes_.push_back(profile);
}

std::optional<WorkModeProfile> WorkModeRegistry::find(const QString& id) const
{
    for (const auto& m : modes_)
        if (m.id == id) return m;
    return std::nullopt;
}

std::vector<QString> WorkModeRegistry::listIds() const
{
    std::vector<QString> ids;
    ids.reserve(modes_.size());
    for (const auto& m : modes_) ids.push_back(m.id);
    return ids;
}

WorkModeRegistry::WorkModeRegistry()
{
    registerBuiltins();
}

WorkModeProfile WorkModeRegistry::inferProfile(
    const std::vector<vsgraysim::EquipmentInfo>& equip) const
{
    WorkModeProfile p;
    if (equip.empty()) return p;

    // 1) 统计召唤到的类型 / 寻的 / 制导提示
    bool has_radar = false, has_sar = false, has_ir = false, has_vis = false;
    bool any_seek = false, any_guidance = false;
    bool aircraft_platform = false, uav_platform = false;
    for (const auto& e : equip) {
        const auto platform = QString::fromStdString(e.platform_kind).toLower();
        uav_platform |= platform.contains("uav") || platform.contains("drone") || platform.contains(QStringLiteral("无人机"));
        aircraft_platform |= platform.contains("aircraft") || platform.contains("plane") || platform.contains(QStringLiteral("飞机"));
        switch (e.type) {
            case SensorType::RADAR: has_radar = true; break;
            case SensorType::SAR:   has_sar = true;   break;
            case SensorType::IR:    has_ir = true;    break;
            case SensorType::VIS:   has_vis = true;   break;
            default: break;
        }
        if (isSeekerLike(e.name)) any_seek = true;
        if (e.type == SensorType::RADAR && e.role == 1) any_guidance = true;
    }

    // 2) 按"装备 + 平台特征"挑选最贴合的模板, 决定面板/处理链/控制件
    const char* tid = "search_radar";
    if (has_sar && !has_radar && !has_ir && !has_vis)
        tid = "sar_imaging";                                  // 单一 SAR 平台
    else if (has_vis && !has_sar && !has_radar)
        tid = aircraft_platform && !uav_platform ? "aircraft_cockpit" : uav_platform ? "uav_cockpit" : has_ir ? "multispectral" : "visible_imaging";
    else if (has_radar && has_ir)
        tid = any_seek ? "missile_guidance" : "fused_radar_ir"; // 导弹双导引头 / 复合火控台
    else if (has_ir && !has_sar && !has_vis)
        tid = any_seek ? "missile_ir" : "infrared";           // 导弹红外 vs 普通红外
    else if (has_radar && !has_sar && !has_ir && !has_vis)
        tid = any_guidance ? "guidance_radar" : "search_radar"; // 制导雷达 vs 搜索雷达

    auto base = find(QString::fromLatin1(tid));
    if (base) p = *base;

    // 3) 通道改为"与所选装备一一对应" (每个装备一个会话, 顺序由装备决定)
    p.channels.clear();
    for (const auto& e : equip) {
        EquipmentChannelSpec s;
        s.sensor_type = e.type;
        s.role        = roleForEquipment(e);
        s.required    = true;
        s.min_count   = 1;
        const char* name = e.name.empty() ? sensorTypeName(e.type) : e.name.c_str();
        s.label = QString::fromUtf8(name);
        p.channels.push_back(std::move(s));
    }

    // 4) 装配信息: 无模板时按单类型给一个最小集合
    if (!base) {
        p.panels.clear(); p.processors.clear(); p.controls.clear();
        bool first = true;
        for (const auto& e : equip) {
            if (first) { // 面板只放一个, 避免重复
                if (e.type == SensorType::RADAR) p.panels = { QStringLiteral("ppi"), QStringLiteral("rd") };
                else if (e.type == SensorType::IR) p.panels = { QStringLiteral("ir_image") };
                else if (e.type == SensorType::SAR) p.panels = { QStringLiteral("sar_image") };
                else p.panels = { QStringLiteral("vis_image") };
                first = false;
            }
            switch (e.type) {
                case SensorType::RADAR: p.processors.push_back(QStringLiteral("radar.cfar"));
                                        p.processors.push_back(QStringLiteral("radar.track"));
                                        break;
                default: break;
            }
        }
        p.controls = { QStringLiteral("power") };
        p.id = QStringLiteral("custom_%1").arg(QString::number(equip.size()));
    }

    for(const auto& e:equip){const QString panel=e.type==SensorType::VIS?"vis_image":e.type==SensorType::IR?"ir_image":e.type==SensorType::SAR?"sar_image":"rd";if(!p.panels.contains(panel))p.panels.push_back(panel);}
    // 5) 界面标题: 显式标出"推断"来源
    if (base)
        p.label = QStringLiteral("推断显控 · ") + p.label;
    else
        p.label = QStringLiteral("推断显控 · 多传感器%1").arg(int(equip.size()));
    p.description = QStringLiteral("根据接入的 %1 个装备/平台装配。%2")
        .arg(int(equip.size())).arg(base ? base->description : QStringLiteral("仅显示实际收到的数据。"));
    return p;
}

void WorkModeRegistry::registerBuiltins()
{
    auto ch = [](SensorType t, EquipmentWorkRole r, const QString& label,
                 int min_count = 1) {
        EquipmentChannelSpec s;
        s.sensor_type = t;
        s.role = r;
        s.label = label;
        s.min_count = std::max(1, min_count);
        return s;
    };

    {
        WorkModeProfile p;p.id="visible_imaging";p.label=QStringLiteral("可见光成像显控");
        p.description=QStringLiteral("接收可见光装备的实际辐亮度或探测器产品，显示拉伸不改变原始像素。");
        p.channels={ch(SensorType::VIS,EquipmentWorkRole::Imaging,QStringLiteral("可见光相机"))};p.panels={"vis_image","link_state"};p.controls={"power","contrast"};registerMode(p);
        p.id="multispectral";p.label=QStringLiteral("多谱段装备显控");p.channels.push_back(ch(SensorType::IR,EquipmentWorkRole::Imaging,QStringLiteral("红外")));p.panels={"vis_image","ir_image","link_state"};registerMode(p);
    }
    // ---- 搜索雷达显控 ----
    {
        WorkModeProfile p;
        p.id = "search_radar";
        p.label = QStringLiteral("搜索雷达显控");
        p.description = QStringLiteral("显示实际 RD 数据、本地 CFAR 检测与航迹分析；检测航迹不是世界真值，阈值只影响本地分析。");
        p.channels = { ch(SensorType::RADAR, EquipmentWorkRole::Search, QStringLiteral("搜索雷达")) };
        p.panels    = { QStringLiteral("ppi"), QStringLiteral("rd") };
        p.processors = { QStringLiteral("radar.cfar"), QStringLiteral("radar.track") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("threshold") };
        registerMode(p);
    }

    // ---- 制导雷达显控 ----
    {
        WorkModeProfile p;
        p.id = "guidance_radar";
        p.label = QStringLiteral("制导雷达显控");
        p.description = QStringLiteral("监控制导雷达的实际数据、本地检测与链路状态；本工作台未接入制导解算或火控发射。");
        p.channels = { ch(SensorType::RADAR, EquipmentWorkRole::Guidance, QStringLiteral("制导雷达")) };
        p.panels    = { QStringLiteral("ppi"), QStringLiteral("rd"), QStringLiteral("link_state") };
        p.processors = { QStringLiteral("radar.cfar"), QStringLiteral("radar.track") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("threshold") };
        registerMode(p);
    }

    // ---- SAR 成像显控 ----
    {
        WorkModeProfile p;
        p.id = "sar_imaging";
        p.label = QStringLiteral("SAR 回波分析（未聚焦）");
        p.description = QStringLiteral("显示实际 SAR 复回波幅度和采样元数据；原始 IQ 与 RD 均明确标为未聚焦，未提供 SAR 聚焦或 ATR。");
        p.channels = { ch(SensorType::SAR, EquipmentWorkRole::Imaging, QStringLiteral("SAR")) };
        p.panels     = { QStringLiteral("sar_image"), QStringLiteral("link_state") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("contrast") };
        registerMode(p);
    }

    // ---- 红外显控 ----
    {
        WorkModeProfile p;
        p.id = "infrared";
        p.label = QStringLiteral("红外显控");
        p.description = QStringLiteral("显示实际红外辐亮度/探测器产品及像素读数；对比度拉伸仅作用于显示，不输出未经标定的温度。");
        p.channels = { ch(SensorType::IR, EquipmentWorkRole::Imaging, QStringLiteral("红外")) };
        p.panels     = { QStringLiteral("ir_image"), QStringLiteral("link_state") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("contrast") };
        registerMode(p);
    }

    // ---- 导弹红外显控 (前视红外/导引头) ----
    {
        WorkModeProfile p;
        p.id = "missile_ir";
        p.label = QStringLiteral("导弹红外显控");
        p.description = QStringLiteral("查看导引头实际红外数据、像素读数与链路状态；未接入自动寻的或脱靶量解算。");
        p.channels = { ch(SensorType::IR, EquipmentWorkRole::Seek, QStringLiteral("红外导引头")) };
        p.panels     = { QStringLiteral("ir_image"), QStringLiteral("link_state") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("contrast") };
        registerMode(p);
    }

    // ---- 导弹下显控 (红外 + 雷达导引头, 多装备协同) ----
    {
        WorkModeProfile p;
        p.id = "missile_guidance";
        p.label = QStringLiteral("导弹下显控(红外+导引头)");
        p.description = QStringLiteral("分别查看红外与雷达导引头的数据、时间戳与链路状态；未接入融合导引或前置量解算。");
        p.channels = {
            ch(SensorType::IR,    EquipmentWorkRole::Seek, QStringLiteral("红外导引头")),
            ch(SensorType::RADAR, EquipmentWorkRole::Seek, QStringLiteral("雷达导引头"))
        };
        p.panels     = { QStringLiteral("ir_image"), QStringLiteral("ppi"), QStringLiteral("link_state") };
        p.processors = { QStringLiteral("radar.cfar"), QStringLiteral("radar.track") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("threshold"), QStringLiteral("contrast") };
        registerMode(p);
    }

    // ---- 雷达 + 红外复合显控 (地面平台多装备协同) ----
    {
        WorkModeProfile p;
        p.id = "fused_radar_ir";
        p.label = QStringLiteral("复合显控(雷达+红外)");
        p.description = QStringLiteral("同一工作台分别查看雷达与红外真实数据和各自时间戳；本地雷达检测不自动控制红外指向。");
        p.channels = {
            ch(SensorType::RADAR, EquipmentWorkRole::Search, QStringLiteral("搜索雷达")),
            ch(SensorType::IR,    EquipmentWorkRole::Imaging, QStringLiteral("红外"))
        };
        p.panels     = { QStringLiteral("ppi"), QStringLiteral("ir_image"), QStringLiteral("link_state") };
        p.processors = { QStringLiteral("radar.cfar"), QStringLiteral("radar.track") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("threshold"), QStringLiteral("contrast") };
        registerMode(p);
    }

    // ---- 无人机显控 ----
    {
        WorkModeProfile p;
        p.id = "uav_cockpit";
        p.label = QStringLiteral("无人机显控");
        p.description = QStringLiteral("查看真实平台遥测与双光载荷；平台支持时可接管油门、舵量及高度/航向保持，控制结果由仿真端回报。");
        p.channels = {
            ch(SensorType::VIS,    EquipmentWorkRole::Cockpit, QStringLiteral("可见光")),
            ch(SensorType::IR,     EquipmentWorkRole::Cockpit, QStringLiteral("红外吊舱"))
        };
        p.panels     = { QStringLiteral("vis_image"), QStringLiteral("ir_image"), QStringLiteral("flight"), QStringLiteral("link_state") };
        p.controls   = { QStringLiteral("power"), QStringLiteral("contrast"), QStringLiteral("flight_manual"), QStringLiteral("flight_hold"), QStringLiteral("flight_route"), QStringLiteral("flight_release") };
        registerMode(p);
    }

    {
        WorkModeProfile p;
        p.id = "aircraft_cockpit";
        p.label = QStringLiteral("飞机操作台");
        p.description = QStringLiteral("显示真实姿态、速度、高度和传感器产品；接管后支持油门、三轴舵量、保持/航迹模式及执行回报，能力以平台声明为准。");
        p.channels = { ch(SensorType::VIS, EquipmentWorkRole::Cockpit, QStringLiteral("机载可见光")) };
        p.panels = { QStringLiteral("vis_image"), QStringLiteral("flight"), QStringLiteral("link_state") };
        p.controls = { QStringLiteral("power"), QStringLiteral("contrast"), QStringLiteral("flight_manual"), QStringLiteral("flight_hold"), QStringLiteral("flight_route"), QStringLiteral("flight_release") };
        registerMode(p);
    }
}

}}} // namespace vsgraysim::gui::display
