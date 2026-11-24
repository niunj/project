#pragma once
/// @file WorkMode.h
/// @brief 显控软件"工作模式(WorkMode)"注册模型。
///
/// 核心思想: 显控软件不再为每类装备单独写一个程序, 而是由导航页/用户
/// 选定一种"工作模式", 该模式声明:
///   - 需要链接哪些装备通道 (EquipmentChannelSpec: 传感器类型 + 工作角色)
///   - 该工作模式下要装配的面板 / 处理链 / 控制件 (由 display 平台按 id 装配)
///
/// 装配式 (装配已有注册模块), 而非运行时生成新界面; 声明式 Profile 驱动。
/// 由 WorkModeRegistry 持有内置工作模式, 可落 JSON 以便跨部署形态切换。
#ifndef VSGRAYSIM_GUI_DISPLAY_WORKMODE_H
#define VSGRAYSIM_GUI_DISPLAY_WORKMODE_H

#include <QString>
#include <QStringList>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "vsgraysim/core/SensorTypes.h"
#include "vsgraysim/broker/EquipmentInfo.h"

namespace vsgraysim { namespace gui { namespace display {

/// 传感器工作角色 (区分同类型装备的不同工作方式, 如搜索/制导雷达)
enum class EquipmentWorkRole : uint8_t {
    Search     = 0,   ///< 搜索 / 监视 (搜索雷达/红外预警)
    Guidance   = 1,   ///< 制导 / 火控 (制导雷达, 为导弹提供信息)
    Imaging    = 2,   ///< 成像 (SAR / 红外成像 / 导引头视场)
    Seek       = 3,   ///< 寻的 (导弹导引头, 视场式)
    Cockpit    = 4,   ///< 无人机等平台座舱 / 遥测
    Fusion     = 5,   ///< 多装备协同(雷达+红外等)
};

const char* roleName(EquipmentWorkRole role);

/// 一个工作模式需要的单个装备通道
struct EquipmentChannelSpec {
    SensorType         sensor_type = SensorType::RADAR;
    EquipmentWorkRole  role = EquipmentWorkRole::Search;
    bool               required = true;     ///< 是否必须 (缺则需重选工作模式)
    int                min_count = 1;       ///< 该通道最少装备数 (多装备协同时 >1)
    QString            label;               ///< 通道显示名, 如 "雷达/前视红外"
};

/// 工作模式声明式描述: 声明"需要哪些通道 + 应装配哪些面板/处理/控制", 由平台装配。
struct WorkModeProfile {
    QString id;                  ///< 稳定 id, 命令行/JSON 用, 如 "guidance_radar"
    QString label;               ///< 中文名, 界面显示, 如 "制导雷达显控"
    QString description;         ///< 一句话说明
    std::vector<EquipmentChannelSpec> channels;   ///< 需要的装备通道
    QStringList panels;          ///< 装配的面板 id (由 display 平台查找 IPanelProvider)
    QStringList processors;      ///< 装配的处理链 id (由本地 AlgoFactory 按名取)
    QStringList controls;        ///< 装配的控制件 id (开关机/增益/门限/导引发射等)
};

/// 工作模式注册表: 提供内置工作模式, 支持按 id 查找。
class WorkModeRegistry {
public:
    static WorkModeRegistry& instance();

    /// 注册一个工作模式 (重复 id 覆盖)
    void registerMode(const WorkModeProfile& profile);

    /// 按 id 查找
    std::optional<WorkModeProfile> find(const QString& id) const;

    /// 列出所有已注册模式 id
    std::vector<QString> listIds() const;

    /// 所有已注册模式
    const std::vector<WorkModeProfile>& all() const { return modes_; }

    /// 根据"所选装备集合(可按平台分组)"推断应装订的工作模式。
    /// profile.channels 与 equip 一一对应 (每个装备 = 一个通道), 面板/处理/控制取匹配模板。
    /// equip 为空返回空 Profile (id 为空)。
    WorkModeProfile inferProfile(const std::vector<vsgraysim::EquipmentInfo>& equip) const;

private:
    WorkModeRegistry();
    void registerBuiltins();

    std::vector<WorkModeProfile> modes_;
};

}}} // namespace vsgraysim::gui::display

#endif // VSGRAYSIM_GUI_DISPLAY_WORKMODE_H