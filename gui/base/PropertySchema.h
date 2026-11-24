#pragma once
/// @file PropertySchema.h
/// @brief 统一「属性参数」模块 —— 场景树各元素的属性字段描述。
///
/// 由场景设计 / 仿真控制 / 统一显控 三个软件共用:
///   - 统一的场景树节点类型枚举 (NodeKind) 与其在树上的 role 常量;
///   - 每一类节点(场景/地形/实体/传感器/环境)的属性分组定义 (Group+Item);
///   - 一份只读属性表格渲染器 renderReadonlyTable, 两个软件用同一套 Schema
///     驱动表格, 仅传入各自的取值回调 (读 SceneObject 或 读场景 JSON/运行时)。
///
/// 三种取值来源 (Source):
///   Config  — 静态配置, 唯一编辑入口在场景设计软件, 落盘到场景 JSON;
///   Runtime — 运行时状态, 由仿真软件实时产生 (平台快照/设备帧);
///   Derived — 派生统计, 由引擎回填或遍历计算 (三角形数/传感器数等)。
///
/// 键 (Item::key) 与场景 JSON 字段路径对齐, 保证 设计→仿真 字段名唯一一致。
#ifndef VSGRAYSIM_GUI_BASE_PROPERTY_SCHEMA_H
#define VSGRAYSIM_GUI_BASE_PROPERTY_SCHEMA_H

#include <QString>
#include <QVector>

#include <QColor>
#include <QFont>
#include <QObject>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QSignalBlocker>

#include <functional>

namespace vsgraysim { namespace gui { namespace prop {

/// 统一「场景树节点类型」枚举 (三软件共用; 存储在树节点 UserRole 中的数据即其值)。
enum class NodeKind : int {
    Scenario    = 0,  ///< 场景根
    Terrain     = 1,  ///< 地形
    Entity      = 2,  ///< 实体
    Sensor      = 3,  ///< 传感器 (实体子节点)
    Environment = 4,  ///< 环境
    FaceGroup   = 5,  ///< 电磁分组 (实体子节点)
    Waypoint    = 6,  ///< 航路点 (实体子节点)
};

/// 属性取值来源。
enum class Source : int {
    Config  = 0,  ///< 静态配置 (场景 JSON, 设计器可编辑)
    Runtime = 1,  ///< 运行时状态 (仿真实时产生)
    Derived = 2,  ///< 派生统计 (引擎回填/遍历计算)
};

/// 属性值类型。
enum class Type : int {
    Number = 0,  ///< 数值
    Vec3   = 1,  ///< 三维向量 (以 "(x, y, z) unit" 展示)
    Text   = 2,  ///< 文本
    Enum   = 3,  ///< 枚举 (下拉)
    Bool   = 4,  ///< 布尔
};

/// 单个属性字段描述。
struct Item {
    QString key;        ///< 与场景 JSON 字段路径一致的键
    QString label;      ///< 中文显示名
    Type    type      = Type::Text;
    QString unit;       ///< 单位, 如 "W" / "m" / "°" / "Hz"
    QString group;      ///< 所属折叠分组 id (冗余于 Group::id, 便于按需过滤)
    int     source     = int(Source::Config);
    bool    editableOnDesign = true;  ///< 场景设计是否提供编辑
};

/// 属性分组 (树节点内按分区展示)。
struct Group {
    QString id;
    QString title;
    QVector<Item> items;
};

/// 场景根节点属性分组。
inline const QVector<Group>& scenarioGroups()
{
    static const QVector<Group> g = {{
        {QStringLiteral("场景"), QStringLiteral("场景"),
         {
             {QStringLiteral("scenario_name"), QStringLiteral("场景名称"), Type::Text,
              QString(), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("description"), QStringLiteral("场景描述"), Type::Text,
              QString(), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("terrain_ref"), QStringLiteral("地形引用"), Type::Text,
              QString(), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("frame_dt"), QStringLiteral("仿真步长"), Type::Number,
              QStringLiteral("s"), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("physics_step"), QStringLiteral("最大物理积分步长"), Type::Number,
              QStringLiteral("s"), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("history_seconds"), QStringLiteral("真值历史时长"), Type::Number,
              QStringLiteral("s"), QStringLiteral("场景"), int(Source::Config), true},
             {QStringLiteral("epoch_utc"), QStringLiteral("起始时刻 UTC"), Type::Text,
              QString(), QStringLiteral("场景"), int(Source::Config), true},
         }},
        {QStringLiteral("统计"), QStringLiteral("统计"),
         {
             {QStringLiteral("entity_count"), QStringLiteral("实体数量"), Type::Number,
              QString(), QStringLiteral("统计"), int(Source::Derived), false},
             {QStringLiteral("sensor_count"), QStringLiteral("传感器数量"), Type::Number,
              QString(), QStringLiteral("统计"), int(Source::Derived), false},
             {QStringLiteral("binding_count"), QStringLiteral("导引/绑定关系"), Type::Number,
              QString(), QStringLiteral("统计"), int(Source::Derived), false},
         }},
    }};
    return g;
}

/// 地形节点属性分组。
inline const QVector<Group>& terrainGroups()
{
    static const QVector<Group> g = {{
        {QStringLiteral("地形"), QStringLiteral("地形"),
         {
             {QStringLiteral("terrain_type"), QStringLiteral("地形类型"), Type::Text,
              QString(), QStringLiteral("地形"), int(Source::Config), true},
             {QStringLiteral("terrain_ref"), QStringLiteral("地形文件"), Type::Text,
              QString(), QStringLiteral("地形"), int(Source::Config), true},
         }},
        {QStringLiteral("地形配置"), QStringLiteral("地形配置"),
         {
             // 地形工程 (.vsgterrain) 配置, 在地形编辑软件生成/编辑,
             // 场景设计与仿真控制中均只读展示。
             {QStringLiteral("terrain_grid"), QStringLiteral("网格"), Type::Number,
              QString(), QStringLiteral("地形配置"), int(Source::Config), false},
             {QStringLiteral("terrain_extent"), QStringLiteral("范围"), Type::Number,
              QStringLiteral("m"), QStringLiteral("地形配置"), int(Source::Config), false},
             {QStringLiteral("terrain_height_scale"), QStringLiteral("高度比例"), Type::Number,
              QStringLiteral("m"), QStringLiteral("地形配置"), int(Source::Config), false},
             {QStringLiteral("terrain_seed"), QStringLiteral("种子"), Type::Number,
              QString(), QStringLiteral("地形配置"), int(Source::Config), false},
             {QStringLiteral("terrain_water"), QStringLiteral("水面"), Type::Enum,
              QString(), QStringLiteral("地形配置"), int(Source::Config), false},
             {QStringLiteral("terrain_water_level"), QStringLiteral("水位"), Type::Number,
              QStringLiteral("m"), QStringLiteral("地形配置"), int(Source::Config), false},
         }},
    }};
    return g;
}

/// 地形程序化类型名 (索引 = layer1 TerrainType 枚举 0..9)。
inline QString terrainTypeName(int t)
{
    static const char* kNames[] = {
        "平坦", "平原", "丘陵", "山地", "沙漠",
        "峡谷", "海岛", "戈壁", "沼泽", "海洋"};
    return (t >= 0 && t <= 9) ? QObject::tr(kNames[t]) : QStringLiteral("—");
}

/// 水面类型名 (索引 = WaterType 枚举 0..4, 三软件统一)。
inline QString waterTypeName(int w)
{
    static const char* kNames[] = {"无水", "海洋", "湖泊", "河流", "池塘"};
    return (w >= 0 && w <= 4) ? QObject::tr(kNames[w]) : QStringLiteral("—");
}

/// 实体节点属性分组。
inline const QVector<Group>& entityGroups()
{
    static const QVector<Group> g = {{
        {QStringLiteral("模型"), QStringLiteral("模型"),
         {
             {QStringLiteral("object_id"), QStringLiteral("实体 ID"), Type::Text,
              QString(), QStringLiteral("模型"), int(Source::Config), false},
             {QStringLiteral("mesh_path"), QStringLiteral("模型文件"), Type::Text,
              QString(), QStringLiteral("模型"), int(Source::Config), false},
             {QStringLiteral("type"), QStringLiteral("平台类型"), Type::Enum,
              QString(), QStringLiteral("模型"), int(Source::Config), true},
         }},
        {QStringLiteral("变换"), QStringLiteral("变换"),
         {
             {QStringLiteral("faction"), QStringLiteral("阵营"), Type::Enum,
              QString(), QStringLiteral("变换"), int(Source::Config), true},
             {QStringLiteral("position"), QStringLiteral("位置"), Type::Vec3,
              QStringLiteral("m"), QStringLiteral("变换"), int(Source::Config), true},
             {QStringLiteral("yaw_deg"), QStringLiteral("航向"), Type::Number,
              QStringLiteral("°"), QStringLiteral("变换"), int(Source::Config), true},
             {QStringLiteral("scale"), QStringLiteral("缩放"), Type::Number,
              QString(), QStringLiteral("变换"), int(Source::Config), true},
             {QStringLiteral("mass"), QStringLiteral("质量"), Type::Number,
              QStringLiteral("kg"), QStringLiteral("变换"), int(Source::Config), true},
             {QStringLiteral("motion_authority"), QStringLiteral("运动方式"), Type::Enum,
              QString(), QStringLiteral("变换"), int(Source::Config), true},
         }},
        {QStringLiteral("统计"), QStringLiteral("统计"),
         {
             {QStringLiteral("sensor_count"), QStringLiteral("传感器"), Type::Number,
              QString(), QStringLiteral("统计"), int(Source::Derived), false},
             {QStringLiteral("waypoint_count"), QStringLiteral("航迹点数"), Type::Number,
              QString(), QStringLiteral("统计"), int(Source::Derived), false},
         }},
    }};
    return g;
}

/// 传感器节点属性分组。
inline const QVector<Group>& sensorGroups()
{
    static const QVector<Group> g = {{
        {QStringLiteral("传感器"), QStringLiteral("传感器"),
         {
             {QStringLiteral("type"), QStringLiteral("传感器类型"), Type::Enum,
              QString(), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("role"), QStringLiteral("角色(搜索/制导)"), Type::Text,
              QString(), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("band"), QStringLiteral("频段"), Type::Text,
              QString(), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("power"), QStringLiteral("发射功率"), Type::Number,
              QStringLiteral("W"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("wavelength"), QStringLiteral("波长"), Type::Number,
              QStringLiteral("m"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("beam_width"), QStringLiteral("波束宽度"), Type::Number,
              QStringLiteral("°"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("pulse_rate"), QStringLiteral("脉冲重频"), Type::Number,
              QStringLiteral("Hz"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("duty_cycle"), QStringLiteral("占空比"), Type::Number,
              QString(), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("fov_h"), QStringLiteral("水平视场"), Type::Number,
              QStringLiteral("°"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("fov_v"), QStringLiteral("垂直视场"), Type::Number,
              QStringLiteral("°"), QStringLiteral("传感器"), int(Source::Config), true},
             {QStringLiteral("range_m"), QStringLiteral("作用距离"), Type::Number,
              QStringLiteral("m"), QStringLiteral("传感器"), int(Source::Config), true},
         }},
    }};
    return g;
}

/// 环境节点属性分组。
inline const QVector<Group>& environmentGroups()
{
    static const QVector<Group> g = {{
        {QStringLiteral("环境"), QStringLiteral("环境"),
         {
             {QStringLiteral("weather"), QStringLiteral("天气"), Type::Enum,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("severity"), QStringLiteral("强度等级"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             // W2: 细粒度大气参数 (实时下发到 rocky_env Control 接口; 全部可编辑)
             {QStringLiteral("cloud_cover"), QStringLiteral("云量"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("cloud_base"), QStringLiteral("云底高 (m)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("rain"), QStringLiteral("降雨强度"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("snow"), QStringLiteral("降雪强度"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("visibility"), QStringLiteral("能见度 (m)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("wind_speed"), QStringLiteral("风速 (m/s)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("wind_az"), QStringLiteral("风向 (°)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("sun_el"), QStringLiteral("太阳高度 (°)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("sun_az"), QStringLiteral("太阳方位 (°)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
             {QStringLiteral("wave_height"), QStringLiteral("浪高 (m)"), Type::Number,
              QString(), QStringLiteral("环境"), int(Source::Config), true},
         }},
    }};
    return g;
}

/// 按节点类型返回属性分组定义 (三者共用的统一 Schema)。
inline const QVector<Group>& groups(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Scenario:    return scenarioGroups();
    case NodeKind::Terrain:     return terrainGroups();
    case NodeKind::Sensor:      return sensorGroups();
    case NodeKind::Environment: return environmentGroups();
    case NodeKind::Entity:
    default:                    return entityGroups();
    }
}

// ---- 场景树节点 UserRole 常量 (三软件共用) ----
constexpr int RoleKeyNodeKind  = Qt::UserRole;      ///< NodeKind
constexpr int RoleKeyObjectId  = Qt::UserRole + 1;  ///< 对象 id
constexpr int RoleKeySensorIdx = Qt::UserRole + 2;  ///< 传感器序号

/// 只读属性表格渲染: 按分组插入分区标题行 + 属性名/值行。
/// @param valueOf 每个字段取值回调: 入参 Item, 返回展示文本 (由各软件读自身数据)。
inline void renderReadonlyTable(
    QTableWidget* tbl,
    const QVector<Group>& groups,
    const std::function<QString(const Item&)>& valueOf)
{
    if (!tbl) return;
    const QSignalBlocker blocker(tbl);
    tbl->setRowCount(0);

    int r = 0;
    for (const auto& g : groups) {
        if (g.items.isEmpty()) continue;
        // 分区标题行 (跨两列)
        tbl->insertRow(r);
        tbl->setSpan(r, 0, 1, 2);
        QTableWidgetItem* hdr = new QTableWidgetItem(g.title);
        QFont f = hdr->font();
        f.setBold(true);
        hdr->setFont(f);
        hdr->setForeground(QColor(0x7f, 0xc8, 0xff));
        hdr->setBackground(QColor(0x1a, 0x21, 0x30));
        hdr->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        tbl->setItem(r, 0, hdr);
        tbl->setRowHeight(r, 22);
        ++r;

        for (const auto& it : g.items) {
            tbl->insertRow(r);
            auto* k = new QTableWidgetItem(it.label);
            auto* v = new QTableWidgetItem(it.key.isEmpty() ? QString() : valueOf(it));
            k->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            v->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            tbl->setItem(r, 0, k);
            tbl->setItem(r, 1, v);
            ++r;
        }
    }
}

}}} // namespace vsgraysim::gui::prop

#endif // VSGRAYSIM_GUI_BASE_PROPERTY_SCHEMA_H
