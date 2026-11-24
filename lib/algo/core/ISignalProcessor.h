#pragma once
/// @file ISignalProcessor.h
/// @brief 通用信号处理器基元接口 (FFT/滤波/窗函数等)。

#include <cstdint>
#include <complex>
#include <vector>

namespace vsgraysim::algo::core {

/// FFT 处理器接口
class IFftProcessor {
public:
    virtual ~IFftProcessor() = default;

    /// 一维 FFT (复数 -> 复数)
    virtual std::vector<std::complex<float>> fft(
        const std::vector<std::complex<float>>& input) = 0;

    /// 一维 IFFT (复数 -> 复数)
    virtual std::vector<std::complex<float>> ifft(
        const std::vector<std::complex<float>>& input) = 0;

    /// 设置 FFT 长度
    virtual void setSize(uint32_t n) = 0;
    virtual uint32_t size() const = 0;
};

/// 窗函数接口
class IWindowFunction {
public:
    virtual ~IWindowFunction() = default;

    /// 生成窗函数系数
    virtual std::vector<float> generate(uint32_t length) const = 0;

    /// 窗函数名称
    virtual std::string name() const = 0;
};

/// 通用滤波器接口
class IFilter {
public:
    virtual ~IFilter() = default;

    /// 滤波处理 (输入 -> 输出)
    virtual std::vector<float> process(const std::vector<float>& input) = 0;

    /// 重置滤波器状态
    virtual void reset() = 0;
};

/// 阈值检测器接口
class IThresholdDetector {
public:
    virtual ~IThresholdDetector() = default;

    /// 检测峰值/过阈值位置
    /// @param data     输入数据
    /// @param threshold 检测门限
    /// @return 满足条件的索引列表
    virtual std::vector<uint32_t> detect(
        const std::vector<float>& data, float threshold) = 0;
};

} // namespace vsgraysim::algo::core