#pragma once
/// @file ISarProcessor.h
/// @brief SAR 成像与 ATR 抽象接口族。

#include "../IAlgoBase.h"
#include <cstdint>
#include <complex>
#include <vector>

namespace vsgraysim::algo::sar {

/// SAR 原始回波数据
struct SarEchoData {
    std::vector<std::complex<float>> samples;  ///< 复数采样
    uint32_t range_bins = 0;                    ///< 距离 bin 数
    uint32_t azimuth_bins = 0;                  ///< 方位 bin 数
};

/// SAR 成像质量指标
struct ImageQuality {
    float snr_db = 0.0f;
    float focus_sharpness = 0.0f;
    float pslr_db = 0.0f;
    float islr_db = 0.0f;
    float dynamic_range_db = 0.0f;
};

/// SAR 成像引擎接口
class ISarImagingEngine : public IAlgoBase {
public:
    ~ISarImagingEngine() override = default;

    /// 输入原始回波, 输出 dB 幅度图
    /// @return 幅度图 (行主序)
    virtual std::vector<float> image(const SarEchoData& echo) = 0;

    /// 成像质量指标
    virtual const ImageQuality& quality() const = 0;
};

/// ATR 检测结果
struct AtrDetection {
    double center_x = 0, center_y = 0;
    double width_m = 0, height_m = 0;
    double peak_db = 0;
    double contrast_db = 0;
    double confidence = 0;
    enum class Class : uint8_t {
        Vehicle, Building, Aircraft, Ship, Unknown
    };
    Class cls = Class::Unknown;
};

/// SAR ATR 处理器接口
class IAtrProcessor : public IAlgoBase {
public:
    ~IAtrProcessor() override = default;

    /// 处理一帧 dB 图像
    /// @param db_img       幅度图 (dB, 行主序)
    /// @param w            图像宽度
    /// @param h            图像高度
    /// @param pixel_size_m 像素物理尺寸 (米)
    virtual std::vector<AtrDetection> process(
        const std::vector<float>& db_img,
        uint32_t w, uint32_t h,
        float pixel_size_m) = 0;
};

} // namespace vsgraysim::algo::sar