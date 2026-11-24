#pragma once
/// @file EquipmentDataSource.h
/// @brief 装备数据源子系统 (时序子系统 TimingBus 驱动)。
///
/// 仿真控制软件把本子系统注册到 TimingBus:
///   onTick     —— 按 TimingBus 时序推进仿真 (场景 + RT 射线追踪数据生成),
///                 并把雷达/SAR 回波、红外辐射亮度帧上传 GPU, 经 SimDataBroker
///                 以 GPU 句柄分发给各显控软件。
///   onPostTick —— 轮询显控软件发来的控制命令 (开关机闭环反馈)。

#include "vsgraysim/subsystems/ISubsystem.h"
#include "vsgraysim/kernel/TimeTick.h"

namespace vsgraysim {

class Simulation;
class FrameRecorder;

class EquipmentDataSource : public ISimulationSubsystem {
public:
    /// sim 由仿真控制窗口持有, 生命周期必须覆盖本子系统
    explicit EquipmentDataSource(Simulation* sim) : sim_(sim) {}

    // ---- ISubsystem ----
    std::string name() const override { return "EquipmentDataSource"; }
    bool initialize(const nlohmann::json& config) override;
    void shutdown() override {}

    // ---- ISimulationSubsystem ----
    void onPreTick(const TimeTick& tick) override;
    void onTick(const TimeTick& tick) override;
    void onPostTick(const TimeTick& tick) override;
    void onStart() override {}
    void onStop() override;
    void* getMutableProduct(const std::string&) override { return nullptr; }
    const void* getConstProduct(const std::string&) const override { return nullptr; }
    int tickPriority() const override { return 0; }

    /// 最近一次 onTick 是否成功推进 (诊断用)
    bool lastTickOk() const { return last_tick_ok_; }

    /// 挂接帧录制器 (RADAR/SAR/IR 各一) — 在数据分发处录制实际分发的帧;
    /// 录制器由仿真控制窗口持有, 生命周期须覆盖本子系统。传 nullptr 解除。
    void attachFrameRecorders(FrameRecorder* radar, FrameRecorder* sar,
                              FrameRecorder* ir);
    void detachFrameRecorders();

private:
    Simulation* sim_ = nullptr;
    bool last_tick_ok_ = false;
    FrameRecorder* recorders_[3] = {nullptr, nullptr, nullptr};
};

} // namespace vsgraysim
