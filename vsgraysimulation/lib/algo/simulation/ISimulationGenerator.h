#pragma once
/// @file ISimulationGenerator.h
/// @brief 仿真信号生成器抽象接口族 (雷达/SAR/红外回波生成)。

#include "../IAlgoBase.h"
#include <cstdint>
#include <complex>
#include <vector>

namespace vsgraysim::algo::simulation {

/// 雷达回波生成器接口
class IRadarSignalGenerator : public IAlgoBase {
public:
    ~IRadarSignalGenerator() override = default;

    /// 生成一帧雷达回波
    /// @param pulse_samples    每脉冲采样数
    /// @param pulses_per_frame 每帧脉冲数
    /// @param sim_time         仿真时间
    /// @param output           输出回波数据 (脉冲 x 采样, 行主序)
    virtual void generate(uint32_t pulse_samples,
                          uint32_t pulses_per_frame,
                          double sim_time,
                          std::vector<std::complex<float>>& output) = 0;
};

/// SAR 回波生成器接口
class ISarSignalGenerator : public IAlgoBase {
public:
    ~ISarSignalGenerator() override = default;

    /// 生成一帧 SAR 原始回波
    virtual void generate(uint32_t range_bins,
                          uint32_t azimuth_bins,
                          double sim_time,
                          std::vector<std::complex<float>>& output) = 0;
};

/// 红外辐射亮度生成器接口
class IIrRadianceGenerator : public IAlgoBase {
public:
    ~IIrRadianceGenerator() override = default;

    /// 生成一帧红外辐射亮度图
    /// @param width   图像宽度
    /// @param height  图像高度
    /// @param sim_time 仿真时间
    /// @param output  输出辐射亮度数据 (浮点, 行主序)
    virtual void generate(uint32_t width, uint32_t height,
                          double sim_time,
                          std::vector<float>& output) = 0;
};

} // namespace vsgraysim::algo::simulation