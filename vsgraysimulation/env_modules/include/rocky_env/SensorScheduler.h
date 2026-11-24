#pragma once
/// @file SensorScheduler.h —— 传感器帧调度器 (设计文档 §7/§7.1, B4 统一 3D 时序)。
///
/// 每传感器实例一个调度器: 由 SensorDesc 配置节拍, update(simTime) 返回
/// 自上次以来到期的帧事件 (绝对时轴, 与仿真时统对齐 —— 验收标准
/// "成像/相干帧节拍正确; sim_time 对齐")。
///
/// 两类节拍 (§7.1 PRF/CPI 两级调度):
///   成像类 (VIS/SWIR/MWIR/LWIR): period = 1/rate_hz (rate 缺省按
///       1000/integration_ms 推导); 事件 = 曝光时刻 t_exp (单指向 LOS 消费点)。
///   相干类 (RADAR/SAR): PRF 仅计数不触发渲染, CPI 边界为帧事件;
///       period = cpi_pulses / PRF, 事件 t = CPI 结束时刻 (帧时统, §7.1 闭合),
///       t_start = CPI 起始 (StateRingBuffer 位姿窗口), pulses = CPI 脉冲数。
///
/// Bounded batches: at most 1024 events per call. Drain while backlogged();
/// pending measurement events are retained, never silently discarded.
#ifndef ROCKY_ENV_SENSOR_SCHEDULER_H
#define ROCKY_ENV_SENSOR_SCHEDULER_H

#include "rocky_env/SceneSource.h"
#include <simcore/SensorRuntime.h>

#include <cstdint>
#include <vector>

namespace rockyenv {
namespace sensor {

/// 调度器输出的帧事件 (绝对时轴, 秒)。
struct SchedulerEvent {
    enum class Kind : uint8_t { Image, CpiBoundary };
    Kind kind = Kind::Image;
    double t = 0.0;        ///< 成像: 曝光时刻; CPI: 结束时刻 (帧时统)
    double t_start = 0.0;  ///< CPI/exposure start time
    uint32_t pulses = 1;   ///< CPI 脉冲数 (成像 = 1)
    uint64_t frame_id = 0; ///< 实例内帧序号
};

class SensorScheduler {
public:
    /// SensorDesc → 节拍配置。返回 false = 无法推导节拍 (period 无效)。
    bool configure(const SensorDesc& desc);

    /// 推进到 simTime, 返回自上次调用以来到期的帧事件 (按时间升序)。
    std::vector<SchedulerEvent> update(double simTime);

    double period() const { return period_; }
    bool coherent() const { return coherent_; }
    bool valid() const { return period_ > 0.0; }
    uint64_t frames() const { return runtime_.frames(sensor_id_); }
    bool backlogged() const { return backlogged_; }
    /// Next measurement boundary, for event-driven backends. Consume due events
    /// with update() before asking the clock to advance to this deadline.
    simcore::TimePoint nextDeadline() const { return runtime_.nextDeadline().value_or(std::numeric_limits<simcore::TimePoint>::max()); }

private:
    bool coherent_ = false;
    double period_ = 0.0;      ///< 成像: 1/rate; 相干: cpi_pulses/PRF (s)
    simcore::SensorRuntime runtime_;
    uint32_t sensor_id_=0;
    uint32_t cpiPulses_ = 128;
    bool backlogged_ = false;
};

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_SENSOR_SCHEDULER_H
