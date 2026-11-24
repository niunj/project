#pragma once
/// @file PlatformEquipment.h
/// @brief 平台装备联动机制。
///
/// 管理平台上所有装备的工作状态，装备性能受平台运动状态影响：
///   - 转弯时雷达扫描范围受限
///   - 高机动时红外精度下降
///   - 高速转弯时武器系统不稳定
///   - 恶劣海况下声纳性能下降等

#include "PlatformTypes.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace vsgraysim::platform {

/// 装备状态 (由 PlatformEquipment 更新)
struct EquipmentState {
    uint32_t equipment_id = 0;
    bool powered = true;
    bool active = false;
    double current_power = 0.0;     // 当前功率 (0~1)
    double current_range = 0.0;     // 当前探测距离 (m)
    double perf_degradation = 0.0;  // 性能衰减 (0~1, 0=正常)
    double signal_quality = 1.0;    // 信号质量 (0~1)

    // 工作模式
    enum class RadarMode {
        SEARCH,     // 搜索
        TRACK,      // 跟踪
        TWS,        // 边搜索边跟踪
        OFF         // 关闭
    };
    RadarMode radar_mode = RadarMode::SEARCH;

    // 累积统计
    uint64_t total_frames = 0;
    double total_on_time = 0.0;
};

/// 装备联动控制器 (每个平台一个)
class PlatformEquipment {
public:
    explicit PlatformEquipment(uint32_t platform_id);
    ~PlatformEquipment() = default;

    /// 注册装备
    void registerEquipment(uint32_t equipment_id, EquipmentType type,
                           const std::string& name);

    /// 注销装备
    void unregisterEquipment(uint32_t equipment_id);

    /// 基于平台运动状态更新所有装备
    void updateFromMotion(const MotionState& motion, double dt);

    /// 获取指定装备状态
    EquipmentState getEquipmentState(uint32_t equipment_id) const;

    /// 获取所有装备状态
    std::vector<EquipmentState> getAllEquipmentStates() const;

    /// 设置装备电源
    void setEquipmentPower(uint32_t equipment_id, bool powered);

    /// 获取平台 ID
    uint32_t platformId() const { return platform_id_; }

    // ---- 装备工作模式控制 ----

    /// 设置雷达工作模式
    void setRadarMode(uint32_t equipment_id, EquipmentState::RadarMode mode);

    /// 激活/停用装备
    void setEquipmentActive(uint32_t equipment_id, bool active);

private:
    // 更新单个装备的状态
    EquipmentState& updateEquipment(EquipmentState& state,
                                    const MotionState& motion,
                                    double dt);

    // 计算雷达装备受运动影响
    void applyRadarMotionEffect(EquipmentState& state, const MotionState& motion);

    // 计算红外装备受运动影响
    void applyInfraredMotionEffect(EquipmentState& state, const MotionState& motion);

    // 计算武器系统受运动影响
    void applyWeaponMotionEffect(EquipmentState& state, const MotionState& motion);

    // 计算通信装备受运动影响
    void applyCommMotionEffect(EquipmentState& state, const MotionState& motion);

    // 计算声纳装备受运动影响
    void applySonarMotionEffect(EquipmentState& state, const MotionState& motion);

    uint32_t platform_id_;
    std::unordered_map<uint32_t, EquipmentState> equipment_states_;
    std::unordered_map<uint32_t, EquipmentType> equipment_types_;
    std::unordered_map<uint32_t, std::string> equipment_names_;
};

} // namespace vsgraysim::platform