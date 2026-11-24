/// @file PlatformEquipment.cpp
/// @brief 平台装备联动机制实现。

#include "PlatformEquipment.h"
#include <algorithm>
#include <cmath>

namespace vsgraysim::platform {

static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

// =============================================================================
PlatformEquipment::PlatformEquipment(uint32_t platform_id)
    : platform_id_(platform_id) {}

// =============================================================================
void PlatformEquipment::registerEquipment(uint32_t equipment_id, EquipmentType type,
                                           const std::string& name) {
    EquipmentState state;
    state.equipment_id = equipment_id;
    state.powered = true;
    state.active = false;
    state.current_power = 1.0;
    state.perf_degradation = 0.0;
    state.signal_quality = 1.0;

    // 根据装备类型设置默认参数
    switch (type) {
        case EquipmentType::RADAR:
            state.current_range = 50000.0;  // 50km
            break;
        case EquipmentType::SAR:
            state.current_range = 30000.0;  // 30km
            break;
        case EquipmentType::INFRARED:
            state.current_range = 10000.0;  // 10km
            break;
        case EquipmentType::WEAPON:
            state.current_range = 20000.0;  // 20km
            break;
        case EquipmentType::SONAR:
            state.current_range = 15000.0;  // 15km
            break;
        case EquipmentType::COMMUNICATION:
            state.current_range = 100000.0; // 100km
            break;
        default:
            state.current_range = 10000.0;
            break;
    }

    equipment_states_[equipment_id] = state;
    equipment_types_[equipment_id] = type;
    equipment_names_[equipment_id] = name;
}

// =============================================================================
void PlatformEquipment::unregisterEquipment(uint32_t equipment_id) {
    equipment_states_.erase(equipment_id);
    equipment_types_.erase(equipment_id);
    equipment_names_.erase(equipment_id);
}

// =============================================================================
void PlatformEquipment::updateFromMotion(const MotionState& motion, double dt) {
    for (auto& [id, state] : equipment_states_) {
        updateEquipment(state, motion, dt);
    }
}

// =============================================================================
EquipmentState PlatformEquipment::getEquipmentState(uint32_t equipment_id) const {
    auto it = equipment_states_.find(equipment_id);
    if (it != equipment_states_.end()) {
        return it->second;
    }
    return EquipmentState{};
}

// =============================================================================
std::vector<EquipmentState> PlatformEquipment::getAllEquipmentStates() const {
    std::vector<EquipmentState> result;
    result.reserve(equipment_states_.size());
    for (auto& [id, state] : equipment_states_) {
        result.push_back(state);
    }
    return result;
}

// =============================================================================
void PlatformEquipment::setEquipmentPower(uint32_t equipment_id, bool powered) {
    auto it = equipment_states_.find(equipment_id);
    if (it != equipment_states_.end()) {
        it->second.powered = powered;
        if (!powered) {
            it->second.active = false;
        }
    }
}

// =============================================================================
void PlatformEquipment::setRadarMode(uint32_t equipment_id, EquipmentState::RadarMode mode) {
    auto it = equipment_states_.find(equipment_id);
    if (it != equipment_states_.end()) {
        it->second.radar_mode = mode;
    }
}

// =============================================================================
void PlatformEquipment::setEquipmentActive(uint32_t equipment_id, bool active) {
    auto it = equipment_states_.find(equipment_id);
    if (it != equipment_states_.end() && it->second.powered) {
        it->second.active = active;
    }
}

// =============================================================================
EquipmentState& PlatformEquipment::updateEquipment(EquipmentState& state,
                                                    const MotionState& motion,
                                                    double dt) {
    if (!state.powered) {
        state.active = false;
        return state;
    }

    // 累积在线时间
    state.total_on_time += dt;

    // 根据装备类型应用运动影响
    auto type_it = equipment_types_.find(state.equipment_id);
    if (type_it == equipment_types_.end()) return state;

    switch (type_it->second) {
        case EquipmentType::RADAR:
            applyRadarMotionEffect(state, motion);
            break;
        case EquipmentType::INFRARED:
            applyInfraredMotionEffect(state, motion);
            break;
        case EquipmentType::WEAPON:
            applyWeaponMotionEffect(state, motion);
            break;
        case EquipmentType::COMMUNICATION:
            applyCommMotionEffect(state, motion);
            break;
        case EquipmentType::SONAR:
            applySonarMotionEffect(state, motion);
            break;
        default:
            break;
    }

    state.total_frames++;
    return state;
}

// =============================================================================
// 雷达受运动影响:
//   转弯时 (高偏航角速度) → 雷达扫描范围受限, 探测距离下降
//   剧烈俯仰 → 雷达波束俯仰覆盖受限
void PlatformEquipment::applyRadarMotionEffect(EquipmentState& state,
                                                const MotionState& motion) {
    double degradation = 0.0;

    // 偏航角速度影响
    double yaw_rate_abs = std::abs(motion.yaw_rate);
    if (yaw_rate_abs > 10.0) {
        degradation += (yaw_rate_abs - 10.0) / 90.0 * 0.4;
    }

    // 俯仰角速度影响
    double pitch_rate_abs = std::abs(motion.pitch_rate);
    if (pitch_rate_abs > 5.0) {
        degradation += (pitch_rate_abs - 5.0) / 60.0 * 0.3;
    }

    // 滚转角影响
    double roll_abs = std::abs(motion.roll);
    if (roll_abs > 15.0) {
        degradation += (roll_abs - 15.0) / 45.0 * 0.3;
    }

    state.perf_degradation = clamp(degradation, 0.0, 0.9);
    state.current_range = 50000.0 * (1.0 - state.perf_degradation);
    state.signal_quality = 1.0 - state.perf_degradation;

    // 高机动时自动切换到搜索模式
    if (state.perf_degradation > 0.5 && state.radar_mode == EquipmentState::RadarMode::TRACK) {
        state.radar_mode = EquipmentState::RadarMode::SEARCH;
    }

    state.active = state.powered && state.perf_degradation < 0.9;
}

// =============================================================================
// 红外受运动影响:
//   高机动 → 红外图像模糊, 探测距离下降
//   高速 → 气动加热影响
void PlatformEquipment::applyInfraredMotionEffect(EquipmentState& state,
                                                    const MotionState& motion) {
    double degradation = 0.0;

    // 速度影响 (高速时气动加热)
    double speed = vsg::length(motion.velocity);
    if (speed > 100.0) {
        degradation += (speed - 100.0) / 500.0 * 0.3;
    }

    // 机动影响 (角速度)
    double yaw_rate_abs = std::abs(motion.yaw_rate);
    if (yaw_rate_abs > 15.0) {
        degradation += (yaw_rate_abs - 15.0) / 90.0 * 0.4;
    }

    // 俯仰角速度
    double pitch_rate_abs = std::abs(motion.pitch_rate);
    if (pitch_rate_abs > 10.0) {
        degradation += (pitch_rate_abs - 10.0) / 60.0 * 0.3;
    }

    state.perf_degradation = clamp(degradation, 0.0, 0.85);
    state.current_range = 10000.0 * (1.0 - state.perf_degradation);
    state.signal_quality = 1.0 - state.perf_degradation * 0.8;
    state.active = state.powered && state.perf_degradation < 0.85;
}

// =============================================================================
// 武器系统受运动影响:
//   高速转弯 → 瞄准线不稳定, 发射窗口受限
void PlatformEquipment::applyWeaponMotionEffect(EquipmentState& state,
                                                  const MotionState& motion) {
    double degradation = 0.0;

    // 机动影响
    double yaw_rate_abs = std::abs(motion.yaw_rate);
    if (yaw_rate_abs > 5.0) {
        degradation += (yaw_rate_abs - 5.0) / 90.0 * 0.5;
    }

    // 滚转影响
    double roll_abs = std::abs(motion.roll);
    if (roll_abs > 10.0) {
        degradation += (roll_abs - 10.0) / 50.0 * 0.4;
    }

    // 加速度影响
    double accel = vsg::length(motion.accel);
    if (accel > 20.0) {
        degradation += (accel - 20.0) / 100.0 * 0.3;
    }

    state.perf_degradation = clamp(degradation, 0.0, 0.95);
    state.current_range = 20000.0 * (1.0 - state.perf_degradation * 0.5);
    state.signal_quality = 1.0 - state.perf_degradation;

    // 高机动时武器系统锁定
    state.active = state.powered && state.perf_degradation < 0.8;
}

// =============================================================================
// 通信装备受运动影响:
//   高速 → 多普勒效应, 信号质量下降
//   机动 → 天线指向变化
void PlatformEquipment::applyCommMotionEffect(EquipmentState& state,
                                                const MotionState& motion) {
    double degradation = 0.0;

    double speed = vsg::length(motion.velocity);
    if (speed > 200.0) {
        degradation += (speed - 200.0) / 500.0 * 0.2;
    }

    double yaw_rate_abs = std::abs(motion.yaw_rate);
    if (yaw_rate_abs > 20.0) {
        degradation += (yaw_rate_abs - 20.0) / 90.0 * 0.2;
    }

    state.perf_degradation = clamp(degradation, 0.0, 0.5);
    state.signal_quality = 1.0 - state.perf_degradation;
    state.current_range = 100000.0 * (1.0 - state.perf_degradation * 0.3);
    state.active = state.powered;
}

// =============================================================================
// 声纳受运动影响:
//   舰船高速 → 自噪声增大, 声纳性能下降
//   机动 → 拖曳阵不稳定
void PlatformEquipment::applySonarMotionEffect(EquipmentState& state,
                                                 const MotionState& motion) {
    double degradation = 0.0;

    double speed = vsg::length(motion.velocity);
    if (speed > 5.0) {
        degradation += (speed - 5.0) / 15.0 * 0.5;
    }

    double yaw_rate_abs = std::abs(motion.yaw_rate);
    if (yaw_rate_abs > 3.0) {
        degradation += (yaw_rate_abs - 3.0) / 10.0 * 0.3;
    }

    state.perf_degradation = clamp(degradation, 0.0, 0.8);
    state.current_range = 15000.0 * (1.0 - state.perf_degradation);
    state.signal_quality = 1.0 - state.perf_degradation * 0.7;
    state.active = state.powered && state.perf_degradation < 0.8;
}

} // namespace vsgraysim::platform