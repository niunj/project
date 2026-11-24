#pragma once
/// @file OceanFFTField.h —— P3 GPU FFT 波场 (wave_mode=2, 对标 Triton FFT 管线)。
///
/// 定位: 把 WaveField 的 16 谐波 Gerstner 升级为 512² 频谱 IFFT 全谱波场。
/// 谱参数 (JONSWAP Hs/Tp + Mitsuyasu 方向谱) 由 WaveField CPU 侧同源产出,
/// h0 复振幅在 CPU 预计算后经纹理上传; 频域演化 + IFFT + 合成在 GPU compute
/// 完成 (ocean_fft_spectrum/ocean_ifft/ocean_fft_compose 三段)。
///
/// 输出 (供渲染与 band_shade 采样):
///   displacementMap (RGBA32F: chopX, h, chopZ, 0)  —— 顶点位移
///   slopeFoamMap    (RGBA32F: Nx, Ny, Nz, J)       —— 法线 + 水平雅可比行列式
///
/// 同源契约: CPU IOceanSampler 与 GPU 使用同一份 h0、色散、相位及 IFFT 归一化。
/// CPU 按测量时刻缓存完整空间场，并按与 GPU 纹理一致的周期双线性规则采样；
/// 无 Window 时仍可构建和查询该 FFT 场。
#ifndef ROCKY_ENV_OCEAN_FFT_FIELD_H
#define ROCKY_ENV_OCEAN_FFT_FIELD_H

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/core/Value.h>
#include <vsg/maths/vec4.h>

#include <cstdint>
#include <complex>
#include <mutex>
#include <limits>
#include <vector>

namespace vsg {
class Window;
class Device;
class ImageView;
class Sampler;
class Commands;
class Data;
}

namespace rockyenv {

class WaveField;

/// P3 GPU FFT 波场。由 OceanModule 在 wave_mode=2 时持有。
class OceanFFTField {
public:
    struct Result {
        bool ok = false;
        const char* error = "";
    };

    OceanFFTField();
    ~OceanFFTField();

    /// 建 compute 管线 (spectrum/ifft/compose 三段) + 输出纹理 + 每帧 commands。
    /// @param window  有效 Vulkan 窗口 (取 device/queue)
    /// @param N       FFT 网格 (2 的幂, ≤512)
    /// @param patchSize  平铺边长 (m, 即 Lx; 决定 k 网格 2π/L)
    Result build(vsg::Window& window, uint32_t N = 512, float patchSize = 3200.0f);
    Result buildOffscreen(vsg::Device& device,uint32_t N=512,float patchSize=3200.f);
    /// CPU-only construction uses exactly the spectrum uploaded by build().
    void initializeCpu(uint32_t N, float patchSize);

    /// 由 WaveField 谱参数重建 h0 复振幅纹理 (风/水深/海况变化时调用)。
    /// 内部按 JONSWAP 频谱 + Mitsuyasu 方向谱 + 确定性高斯相位生成 h0(k)。
    /// @param wf       谱参数源 (Hs/Tp/方向已就绪)
    /// @param depth_m  代表水深 (<=0 深水)
    void updateSpectrum(const WaveField& wf, float depth_m, float wind_x=1, float wind_z=0);
    struct Sample {
        vsg::vec3 displacement{0,0,0}, normal{0,1,0}, velocity{0,0,0};
        float jacobian=1;
    };
    /// Lagrangian sample at the same periodic coordinates used by ocean_fft.vert.
    /// One immutable spatial field is lazily cached per time/parameter version.
    Sample sample(double x, double z, double time) const;

    /// 每帧驱动: 更新 push 常量 (time/depth/Q/heightScale), commands 可 record。
    void onFrame(float time_sec, float depth_m, float chop_q, float height_scale);

    /// 输出 (渲染/band_shade 采样)。未 build 返回空。
    vsg::ImageView* displacementMap() const { return dispView_.get(); }
    vsg::ImageView* slopeFoamMap() const { return foamView_.get(); }
    vsg::Sampler*   sampler() const { return sampler_.get(); }
    vsg::Commands*  commands() const { return commands_.get(); }

    bool ready() const { return ok_; }
    uint32_t gridSize() const { return N_; }
    float patchSize() const { return patch_; }

private:
    // push 常量块 (与 shader push_constant 布局严格一致)
    struct SpecPush { float time, patchSize, gravity, depth, chopScale, heightScale; };
    struct IfftPush { uint32_t N, count; float scale, transpose; };
    struct CompPush { float cellSize, chopScale, foamEnabled, sprayThresh; };

    struct Impl;
    Impl* impl_ = nullptr;
    bool ok_ = false;
    uint32_t N_ = 512;
    float patch_ = 3200.0f;
    float time_ = 0.0f;
    float depth_ = -1.0f;
    float chopQ_ = 0.65f;
    float heightScale_ = 1.0f;
    mutable std::mutex cpu_mutex_;
    std::vector<std::complex<float>> h0_;
    mutable std::vector<vsg::vec4> cpu_displacement_,cpu_normal_,cpu_velocity_;
    mutable float cpu_time_=std::numeric_limits<float>::quiet_NaN();
    void updateCpuFields(float time) const;

    vsg::ref_ptr<vsg::ImageView> dispView_;
    vsg::ref_ptr<vsg::ImageView> foamView_;
    vsg::ref_ptr<vsg::Sampler> sampler_;
    vsg::ref_ptr<vsg::Commands> commands_;
    vsg::ref_ptr<vsg::Data> h0Data_;   // RG32F N×N 复振幅 (CPU 预计算)
    // push 常量 (每帧改值; PushConstants record 时读取)
    vsg::ref_ptr<vsg::Value<SpecPush>> specPush_;
    vsg::ref_ptr<vsg::Value<IfftPush>> ifftPushRow_;
    vsg::ref_ptr<vsg::Value<IfftPush>> ifftPushCol_;
    vsg::ref_ptr<vsg::Value<CompPush>> compPush_;
};

}  // namespace rockyenv

#endif  // ROCKY_ENV_OCEAN_FFT_FIELD_H
