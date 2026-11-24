#pragma once
/// @file AtmosphereLUT.h
/// @brief Bruneton 预计算大气 LUT 系统 (性能关键路径)。
///
/// 核心思想: 初始化时积分，运行时查表替代逐像素嵌套积分。
/// 散射积分按相对方位切片打包，Rayleigh/Mie 相函数在采样时解析计算。
///
/// LUT 说明:
///   1. transmittance (2D: 256x64 RGBA32F)
///      - 输入: 视角高度 × 太阳天顶角
///      - 输出: RGB 透过率 (大气消光)
///      - 用途: 太阳/月亮/星光消光
///
///   2. scattering (3D: 64x32x384 RGBA32F)
///      - 输入: 高度 × 视角天顶角 × 太阳天顶角 × 相对方位余弦
///      - 输出: 三个 RGB 积分块 (Rayleigh / Mie / 多散射)
///      - 用途: 天空颜色
///
///   3. irradiance (2D: 64x16 RGBA32F)
///      - 输入: 高度 × 太阳天顶角
///      - 输出: RGB 环境辐照度
///      - 用途: 天光/环境光
///
/// 参考: Bruneton et al. "Precomputed Atmospheric Scattering" (2008/2017)
#ifndef ROCKY_ENV_ATMOSPHERE_LUT_H
#define ROCKY_ENV_ATMOSPHERE_LUT_H

#include <vsg/core/ref_ptr.h>
#include <vsg/state/Image.h>
#include <vsg/state/Sampler.h>
#include <vsg/state/DescriptorSet.h>

namespace rockyenv {

/// LUT 尺寸常量
namespace AtmosphereLUTConfig {
    // Transmittance LUT (2D)
    constexpr uint32_t TRANSMITTANCE_WIDTH  = 256;
    constexpr uint32_t TRANSMITTANCE_HEIGHT = 64;

    // Scattering LUT (4D → 3D 打包, 省显存)
    // WIDTH=64: x 维是 sunCosZenith, 暮光/掠射几何 (cos→±1) 处散射随太阳高度角
    // 剧烈变化, 32 格线性插值会把黄昏红染抹平 (6.6° 高度角仍金黄的原因之一)。
    constexpr uint32_t SCATTERING_WIDTH  = 64;
    constexpr uint32_t SCATTERING_HEIGHT = 32;
    constexpr uint32_t SCATTERING_DEPTH  = 32;
    constexpr uint32_t SCATTERING_AZIMUTHS = 4;
    constexpr uint32_t SCATTERING_COMPONENTS = 3;
    constexpr uint32_t SCATTERING_PACKED_DEPTH = SCATTERING_DEPTH * SCATTERING_AZIMUTHS * SCATTERING_COMPONENTS;
    constexpr uint32_t SCATTERING_SLICES = SCATTERING_AZIMUTHS;

    // Irradiance LUT (2D)
    constexpr uint32_t IRRADIANCE_WIDTH  = 64;
    constexpr uint32_t IRRADIANCE_HEIGHT = 16;

    // 大气参数
    constexpr float PLANET_RADIUS    = 6371000.0f;  // 地球半径 (m)
    constexpr float ATMOSPHERE_HEIGHT = 100000.0f;   // 大气厚度 (m)
    constexpr float RAYLEIGH_HEIGHT  = 8500.0f;     // Rayleigh 标高 (m)
    constexpr float MIE_HEIGHT       = 1200.0f;     // Mie 标高 (m)
    constexpr float OZONE_CENTER     = 25000.0f;    // 臭氧层中心高度 (m)
    constexpr float OZONE_WIDTH      = 15000.0f;    // 臭氧层宽度 (m, 兼容保留)
    constexpr float OZONE_SIGMA      = 9000.0f;     // 臭氧高斯廓线 σ (m)
}

/// LUT 纹理资源
struct AtmosphereLUTTextures {
    vsg::ref_ptr<vsg::Image> transmittance;  // 2D: 256x64 RGBA32F
    vsg::ref_ptr<vsg::Image> scattering;     // 3D: 64x32x384 RGBA32F
    vsg::ref_ptr<vsg::Image> irradiance;     // 2D: 64x16 RGBA32F
};

/// Bruneton 预计算大气 LUT 生成器
class AtmosphereLUT {
public:
    AtmosphereLUT();
    ~AtmosphereLUT();

    /// 初始化: 创建纹理 + 采样器
    bool init();
    bool generateReference();

    /// 生成 LUT (GPU compute 或 CPU fallback)
    /// @param rayleighScattering  RGB Rayleigh 散射系数
    /// @param mieScattering       RGB Mie 散射系数
    /// @param mieExtinction       RGB Mie 消光系数
    /// @param ozoneAbsorption     RGB 臭氧吸收系数
    bool generate(const float rayleighScattering[3],
                  const float mieScattering[3],
                  const float mieExtinction[3],
                  const float ozoneAbsorption[3], unsigned scatteringOrders = 4);

    /// 获取纹理资源 (供描述符集绑定)
    const AtmosphereLUTTextures& textures() const { return textures_; }

    /// 获取采样器
    vsg::ref_ptr<vsg::Sampler> sampler() const { return sampler_; }

    /// 是否已初始化
    bool isValid() const { return valid_; }

    // ---- CPU 采样 (IAtmosphereQuery / 仿真侧消费; 归一化与 sky_dome_lut.frag
    //      中 sampleTransmittance/sampleIrradiance 完全一致) ----
    /// 透过率 T(高度, 太阳天顶角余弦)。高度米 (0..100000)。
    vsg::vec3 sampleTransmittanceCPU(float heightM, float sunCosZenith) const;
    /// 环境辐照度 E(高度, 太阳天顶角余弦)。
    vsg::vec3 sampleIrradianceCPU(float heightM, float sunCosZenith) const;
    /// Unit-solar-irradiance sky radiance, using the same packed lookup and
    /// analytic phase functions as sky_dome_lut.frag (all cosines in [-1,1]).
    vsg::vec3 sampleScatteringCPU(float heightM,float viewCosZenith,float sunCosZenith,float viewSunCosTheta) const;

private:
    /// CPU 端 LUT 生成 (fallback)
    void generateCPU(const float rayleighScattering[3],
                     const float mieScattering[3],
                     const float mieExtinction[3],
                     const float ozoneAbsorption[3]);

    /// 计算 transmittance (CPU 版本)
    void computeTransmittance(float* out, uint32_t width, uint32_t height,
                              const float rayleighScattering[3],
                              const float mieExtinction[3],
                              const float ozoneAbsorption[3]);

    /// 计算 single-scattering + 多次散射近似 (CPU 版本)
    /// @param irradianceData 已生成的 irradiance LUT (width*height*4),
    ///                       供各向同性多次散射项使用 (T1)。
    void computeScattering(float* out, uint32_t width, uint32_t height, uint32_t depth,
                           const float rayleighScattering[3],
                           const float mieScattering[3],
                           const float mieExtinction[3],
                           const float ozoneAbsorption[3],
                           const float* irradianceData);

    void computeMultipleScattering(float* out, const float rayleigh[3],
                                   const float mieScattering[3],const float mieExtinction[3],
                                   const float ozone[3]);

    /// 计算 irradiance (CPU 版本)
    void computeIrradiance(float* out, uint32_t width, uint32_t height,
                           const float rayleighScattering[3],
                           const float mieExtinction[3],
                           const float ozoneAbsorption[3]);

    AtmosphereLUTTextures textures_;
    vsg::ref_ptr<vsg::Sampler> sampler_;
    unsigned scatteringOrders_=4;
    bool valid_ = false;
};

}  // namespace rockyenv

#endif  // ROCKY_ENV_ATMOSPHERE_LUT_H
