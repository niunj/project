#pragma once
/// @file IIrProcessor.h
/// @brief 红外装备处理抽象接口族 (图像处理/测温/检测/融合)。

#include "../IAlgoBase.h"
#include <cstdint>
#include <vector>

namespace vsgraysim::algo::infrared {

/// 红外图像处理参数
struct IrProcessParams {
    bool   nuc_enabled = true;
    float  nuc_gain = 1.0f;
    float  nuc_offset = 0.0f;
    bool   bad_pixel_fix = true;
    bool   agc_enabled = true;
    float  agc_low_pct = 2.0f;
    float  agc_high_pct = 98.0f;
    float  manual_gain = 1.0f;
    float  manual_offset = 0.0f;
    enum class FilterType : uint8_t { None, Median3, Gaussian3, Mean3 };
    FilterType filter = FilterType::Median3;
};

/// 红外图像处理器接口
/// 输入: 原始辐射亮度浮点数据
/// 输出: [0,255] 灰度图像 (供 GUI 调色板映射)
class IIrImageProcessor : public IAlgoBase {
public:
    ~IIrImageProcessor() override = default;

    /// 处理一帧红外图像信号
    /// @param raw_frame 原始辐射亮度数据 (浮点, 行主序)
    /// @param w         图像宽度
    /// @param h         图像高度
    /// @param output    输出灰度图像 [0,255]
    virtual void processFrame(const std::vector<float>& raw_frame,
                              uint32_t w, uint32_t h,
                              std::vector<uint8_t>& output) = 0;

    virtual void setParams(const IrProcessParams& p) = 0;
    virtual const IrProcessParams& params() const = 0;

    /// 当前帧统计
    virtual float minLevel() const = 0;
    virtual float maxLevel() const = 0;
    virtual float meanLevel() const = 0;
    virtual float contrast() const = 0;
};

/// 测温点
struct MeasurePoint {
    enum class Type : uint8_t { Crosshair, Region, Isotherm };
    Type   type = Type::Crosshair;
    std::string name;        ///< 显示名称
    double x = 0, y = 0;
    double width = 0, height = 0;
    float  iso_level_c = 40.0f;
    float  temp_c = 0.0f;
    float  temp_min_c = 0.0f, temp_max_c = 0.0f, temp_avg_c = 0.0f;
    float  temp_sigma_c = 0.0f;
};

/// 测温引擎接口
class ITemperatureMeasure : public IAlgoBase {
public:
    ~ITemperatureMeasure() override = default;

    /// 辐射亮度 -> 表观温度 (°C)
    virtual float radianceToTemp(float radiance) const = 0;

    /// 更新全部测温点 (输入整幅温度图 °C)
    virtual void updateMeasurePoints(const std::vector<float>& temp_c,
                                     uint32_t w, uint32_t h) = 0;

    virtual void addPoint(const MeasurePoint& p) = 0;
    virtual void removePoint(size_t idx) = 0;
    virtual void clearPoints() = 0;
    virtual const std::vector<MeasurePoint>& points() const = 0;

    /// 整幅 NETD 估计
    virtual float estimateNetd(const std::vector<float>& temp_c) const = 0;
};

/// 小目标检测结果
struct IrDetection {
    double x = 0, y = 0;
    float  contrast_c = 0;
    float  snr = 0;
    int    area_px = 0;
    int    id = 0;
    bool   tracked = false;
    double vx = 0, vy = 0;
    int    coast = 0;
};

/// 红外小目标检测与跟踪接口
class IIrTargetDetector : public IAlgoBase {
public:
    ~IIrTargetDetector() override = default;

    /// 检测 + 跟踪一帧温度图
    virtual std::vector<IrDetection> process(
        const std::vector<float>& temp_c,
        uint32_t w, uint32_t h) = 0;
};

/// 图像融合接口
class IFusionEngine : public IAlgoBase {
public:
    ~IFusionEngine() override = default;

    /// 融合两幅同尺寸灰度图 [0,1], 输出 [0,1]
    virtual std::vector<float> fuse(const std::vector<float>& ir,
                                    const std::vector<float>& vis,
                                    uint32_t w, uint32_t h) = 0;
};

} // namespace vsgraysim::algo::infrared