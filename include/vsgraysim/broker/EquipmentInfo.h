#pragma once
/// @file EquipmentInfo.h
/// @brief 装备信息结构与装备注册表。
///
/// 仿真软件初始化时，根据场景配置注册所有传感器装备。
/// 显控软件启动时，通过注册表扫描在线装备，选择要链接的装备。

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "vsgraysim/core/SensorTypes.h"

namespace vsgraysim {

// =============================================================================
// 装备信息
// =============================================================================
struct EquipmentInfo {
    uint32_t    equipment_id = 0;       // 装备唯一 ID
    std::string name;                   // 装备名称（如 "雷达_01"、"红外_前视"）
    SensorType  type = SensorType::RADAR;
    uint32_t    platform_id = 0;        // 所属平台 ID
    std::string platform_name;          // 平台名称（如 "M1A2_01"）

    // 装备参数摘要（供显控软件显示）
    float       frame_rate = 0.0f;      // 帧率 (Hz)
    float       range_max = 0.0f;       // 最大探测距离 (m)
    std::string description;            // 描述信息

    // 雷达角色 (显控软件据此初始化界面功能):
    //   0 = search   (搜索雷达: 完整 PPI + 信号处理分析)
    //   1 = guidance (制导雷达: 聚焦 PPI + 火控/发射面板, 为导弹提供信息)
    int         role = 0;

    bool        powered = true;         // 当前电源状态
    bool        active = false;         // 是否活跃（正在生成数据）

    // ---- 跨进程链路扩展 (BrokerNet) ----
    int         faction = 0;            // 平台阵营: 0=中立 1=蓝方 2=红方 (对齐 PlatformManager/Faction)
    std::string platform_kind;          // 平台类型 (radar_site/sam_base/missile/uav/...)
    bool        subscribed = false;     // 已被显控链接 (订阅驱动产帧)
    bool        remote = false;         // true=远端镜像条目 (显控进程内)
    uint64_t    last_seen_ms = 0;       // 远端心跳最近时刻 (epoch ms)
    std::string host;                   // 来源仿真进程名 (远端条目)
};

// =============================================================================
// 装备注册表（仿真软件维护真源, 显控软件镜像查询）
// =============================================================================
class EquipmentRegistry {
public:
    static EquipmentRegistry& instance();

    // === 注册/注销（仿真软件初始化时调用）===
    void registerEquipment(const EquipmentInfo& info);
    void unregisterEquipment(uint32_t equipment_id);
    void clear();   // 清空所有注册

    // === 查询（显控软件扫描时调用）===
    std::vector<EquipmentInfo> listOnline() const;              // 所有在线装备
    std::vector<EquipmentInfo> listByType(SensorType t) const;  // 按类型筛选
    std::optional<EquipmentInfo> findById(uint32_t id) const;
    std::optional<EquipmentInfo> findByName(const std::string& name) const;

    // === 状态更新 ===
    void setPowered(uint32_t equipment_id, bool powered);
    void setActive(uint32_t equipment_id, bool active);
    void setSubscribed(uint32_t equipment_id, bool subscribed); // 链接状态回显

    // === 远端镜像 (显控进程内, BrokerNet 维护) ===
    /// 用远端仿真进程的装备快照合并进本地镜像 (upsert; 不在本快照中的
    /// 同 host 远端条目会被移除)
    void mergeRemote(const std::vector<EquipmentInfo>& list,
                     const std::string& host);
    /// 清除某远端主机的全部镜像条目 (仿真进程下线)
    void clearRemote(const std::string& host);
    /// 清理超时未心跳的远端条目; 返回清除数
    size_t sweepStale(uint64_t timeout_ms);

    // === 统计 ===
    size_t count() const;
    size_t countByType(SensorType t) const;

private:
    EquipmentRegistry() = default;

    mutable std::mutex mtx_;
    std::unordered_map<uint32_t, EquipmentInfo> registry_;
};

} // namespace vsgraysim
