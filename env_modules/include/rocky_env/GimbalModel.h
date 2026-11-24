#pragma once
/// @file GimbalModel.h —— 扫描机构纯函数组件 (设计文档 §7.2, B7)。
///
/// "帧率/PRF 决定何时算一帧 (§7.1), 扫描决定每一帧往哪看"。GimbalModel
/// 不产生帧、不产生分发, 只输出视线偏转角 (机体系 az/el):
///   LOS(t) = platformPose(t) ⊗ gimbalOffset(t)
///            ↑ StateRingBuffer 插值   ↑ scan 模式时间函数 (本组件)
///
/// 两类指向采样 (§7.2):
///   成像类: 一帧一个曝光时刻 t_exp → los(t_exp) 单指向 (视图矩阵 push);
///   相干类: CPI 内逐脉冲指向 los(t0 + n/PRF) 数组 (回波合成的方位多普勒
///           历史); RD 图/PPI 的标称波束指向取 CPI 中心时刻 los(t_mid)。
///
/// 相干性约束 (调度器校验点): CPI 内波束扫过角度 = scan_rate×T_cpi 必须
/// ≤ 波束宽度 beam_width, 否则回波跨波束、相干积累失效 (超限报警或自动
/// 缩短 CPI, 由调用方决策 —— cpiSweepRad() 提供纯函数查询)。
///
/// 状态机 (衔接 OSD 三模式): SEARCH 按扫描模式搜索 → ACQUIRE 停驻/慢扫 →
/// TRACK slaved 指向目标 + 速率限幅 slew。迁移由制导逻辑 (Param 消息)
/// 驱动, 场景 JSON 只给初始值 (Param 消息 > 场景 JSON > 硬编码默认)。
#ifndef ROCKY_ENV_GIMBAL_MODEL_H
#define ROCKY_ENV_GIMBAL_MODEL_H

#include "rocky_env/SceneSource.h"

#include <cstdint>
#include <string>

namespace rockyenv {
namespace sensor {

/// 机体系视线偏转 (°): az 绕天顶轴, el 仰角; 0,0 = 安装基座标称指向。
struct GimbalLos {
    double az_deg = 0.0;
    double el_deg = 0.0;
};

class GimbalModel {
public:
    /// 导引头状态机 (§7.2, OSD SEARCH/ACQUIRE/TRACK 三模式同源)。
    enum class State : uint8_t { Search, Acquire, Track };

    /// scan 配置 + 波束宽度 (rad, 相干性校验用)。mode 缺省/未知 → fixed。
    void configure(const ScanSpec& scan, double beam_width_rad);

    /// t = 相对扫描启动时刻 (s)。slaved 态返回内部跟踪指向 (setTargetAzEl
    /// 注入 + slew 限幅, 按 t 单调推进 —— 测试程序/仿真控制单线程顺序驱动)。
    GimbalLos los(double t) const;

    /// 状态机迁移 (制导逻辑 Param 消息驱动)。Search→Acquire→Track;
    /// 回退 Track→Search 允许 (目标丢失)。
    void setState(State s) { state_ = s; }
    State state() const { return state_; }

    /// slaved 目标指向 (机体系 az/el, °) —— 制导逻辑由绑定关系目标解算注入。
    void setTargetAzEl(double az_deg, double el_deg);

    /// TRACK 态指向限幅 (°/s; 机械云台典型 60-120, 电扫近似无穷)。
    void setSlewRate(double dps) { slewRate_ = dps; }

    /// ACQUIRE 态行为: 停驻 (检测确认窗) —— 返回进入 ACQUIRE 时刻的指向。
    void holdAt(const GimbalLos& los);

    /// 相干性纯函数查询: CPI 内波束扫过角度 (rad, 取方位扫描速率;
    /// circular/rosette/conical 取局部角速率峰值)。≤ beam_width 相干积累有效。
    double cpiSweepRad(double t_cpi) const;

    /// 相干性校验 + CPI 自动缩短建议: 返回满足约束的最大脉冲数
    /// (pow2 递减); nPulses=原值 表示无需缩短。
    uint32_t clampCpiPulses(uint32_t nPulses, double prf_hz) const;

    const ScanSpec& spec() const { return scan_; }

    // ---- az/el ↔ 世界方向 (ENU Y-up 球坐标约定, sensor_viewer 消费) ----
    // az: 水平面内从 +X (东) 绕 +Y 向 +Z (北); el: 相对水平面仰角。
    static vsg::dvec3 azElToDir(double az_deg, double el_deg);
    static void dirToAzEl(const vsg::dvec3& d, double& az_deg, double& el_deg);

private:
    GimbalLos scanLos(double t) const;   // SEARCH 态扫描模式时间函数

    ScanSpec scan_;
    double beamWidth_ = 0.05;            // rad
    State state_ = State::Search;
    double targetAz_ = 0.0, targetEl_ = 0.0;
    double slewRate_ = 90.0;             // °/s
    GimbalLos hold_{};                   // ACQUIRE 停驻指向
    mutable double lastT_ = 0.0;         // slew 积分时轴 (los 单调驱动)
    mutable GimbalLos cur_{};            // TRACK 当前指向 (slew 限幅后)
    mutable bool slewInit_ = false;
};

// ---- 自检 (构造期纯函数断言, 与 StateRingBuffer 自检同风格) ----
void gimbalSelfCheck();

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_GIMBAL_MODEL_H
