#pragma once
/// @file BandRT.h —— B2/P1.1: 波段辐亮度 P0 模块 (per-波段 compute 着色)。
///
/// P1.1 改造: RT 求交收敛至 GBufferPool (全场景唯一 TraceRays → 波段无关
/// G-buffer); 本模块只做 per-波段着色 compute (band_shade.comp):
///   读 G0 (T/eps/range/hit) + 波段 LUT + 环境 UBO → 写 P0 图。
/// P0 通道契约不变: r=辐亮度 g=T b=eps a=range_km, RGBA32F storage image
/// (GENERAL 布局全程), optics pass 直接采样 —— 下游级联零改动。
///
/// 天空 (hit=0): L = sky_radiance, a = 1e6 km 标记 (与旧 rmiss 契约一致)。
/// 构建失败 (如 G-buffer 池缺失) → 上层回退光栅化 rad pass, 下游零感知。

#include <rocky_env/SensorApi.h>      // SensorProfile, BandRadianceLut, EnvAnswer

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>

#include <string>

namespace vsg {
class Window;
class Device;
class Data;
class ImageView;
class Sampler;
class Commands;
}

namespace rockyenv {
namespace sensor {

class SharedSceneResources;   // P0 场景级资源池 (上层创建, 引用传递)
class GBufferPool;            // P1.1 全场景唯一求交 (上层创建, 引用传递)

/// 波段 P0 着色模块 (P1.1: compute 着色, 无 RT)。
class BandRT {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    /// 建 band_shade compute 管线 + P0 图 + 每帧 commands。
    ///   gbuffer  P1.1 G-buffer 池 (必需; 提供 G0 与分辨率校验)
    ///   shared   P0 资源池 (可空: env UBO/LUT/sampler 池复用或本地回退)
    Result build(vsg::Window& window,
                 const SensorProfile& profile,
                 const BandRadianceLut& lut,
                 const EnvAnswer& env,
                 GBufferPool* gbuffer,
                 SharedSceneResources* shared = nullptr);
    Result build(vsg::Device& device,
                 const SensorProfile& profile,
                 const BandRadianceLut& lut,
                 const EnvAnswer& env,
                 GBufferPool* gbuffer,
                 SharedSceneResources* shared = nullptr);

    /// B3 环境注入: 每帧更新 env UBO (band_shade.comp 消费)。
    /// P0.3 池化: 池就绪时写入共享 env UBO (数值去重 → 每帧全链一次上传)。
    void setEnv(const EnvAnswer& env);

    /// 批次2: 谱天空图注入 (CPU bandSky 烘焙, 256×128 latlong:
    /// u=方位/2π+0.5, v=仰角/(π/2); #1: TV 波段 4 float/纹素 (r,g,b,luma)
    /// RGBA32F, 其它波段 1 float/纹素 R32F; 数值去重, 变化时 dirty 上传)。
    void updateSkyMap(const float* data, uint32_t w, uint32_t h);

    /// #5 体积云: 每帧注入云体积参数 (band2.zw + band3/band4/band5.x, 数值去重)。
    ///   base/top m (slab 上下界, base<=0 禁用), coverage/density 0..1,
    ///   windX/windZ 风向单位向量, windSpeed m/s, timeSec 漂移时间,
    ///   cloudEmitL IR 云发射辐亮度 (ε·M(T_c)/π, TV 未用), strength 云影强度,
    ///   cloudType 云型 (r10: 0卷云/1层云/2层积云/3积云/4积雨云 → shader 云型参数表)。
    void setCloudVolume(float baseM, float topM, float coverage, float density,
                        float windX, float windZ, float windSpeed, float timeSec,
                        float cloudEmitL, float strength, float cloudType);

    /// #4 海面逐像素波场: CPU WaveField 谐波表快照 (64 float = 16×vec4:
    ///   x=L y=A z=cosθ w=sinθ) + Gerstner Q。waveCount<=0 / Q<=0 → shader
    ///   海面细节禁用 (恒等退化)。像素角 (fov/宽度, build 期) 随 oceanOpt.z
    ///   下发驱动视距滤波。数值去重。
    void setOceanWave(const float* wave64, int waveCount, float q);

    /// #2 观察口共享: 天空图/天空采样器 (云体已 3D 化, 无云图视图)。
    vsg::ref_ptr<vsg::ImageView> skyView() const { return skyView_; }
    vsg::ref_ptr<vsg::Sampler> skySampler() const { return skySampler_; }

    vsg::ref_ptr<vsg::ImageView> p0;       ///< P0 输出 (STORAGE|SAMPLED, GENERAL)
    vsg::ref_ptr<vsg::Commands> commands;  ///< [P0→GENERAL] + dispatch + barrier

    /// 调试/验收: 回读 P0 到磁盘 (raw float RGBA32F, w*h*4 floats, 行优先)。
    bool dumpP0(vsg::Window& window, const char* path);

private:
    vsg::ref_ptr<vsg::floatArray> envData_;   ///< env UBO (仅回退路径: 池缺失时本地更新)
    vsg::ref_ptr<vsg::floatArray> bandData_;  ///< 波段差量 UBO (92 float = 23 vec4:
                                              ///<  band/band2/体积云 band3/band4 +
                                              ///<  #4 oceanWave[16]+oceanOpt +
                                              ///<  #6 thermal + r10 band5.x 云型, 动态)
    float volCache_[11] = {};                 ///< setCloudVolume 数值去重缓存
    float waveCache_[66] = {};                ///< setOceanWave 数值去重缓存 (64+count+Q)
    float pxAngRad_ = 0.0f;                   ///< #4 像素角 rad/px (build 期由 fov/宽度算, 视距滤波真值)
    // 批次2: 谱天空图 (per-波段; CPU 烘焙注入, DYNAMIC_DATA dirty 上传)
    // #1: TV=vec4Array2D RGBA32F (r,g,b,luma) / 其它=floatArray2D R32F
    vsg::ref_ptr<vsg::ImageView> skyView_;
    vsg::ref_ptr<vsg::Data> skyData_;
    vsg::ref_ptr<vsg::Sampler> skySampler_;   ///< REPEAT U (latlong 方位环绕; #2 观察口共享)
    uint32_t skyCh_ = 1;                      ///< 每纹素 float 通道数 (TV=4, 其它=1)
    std::vector<float> skyCache_;             ///< 上次注入内容 (memcmp 去重)
    uint32_t skyW_ = 0, skyH_ = 0;
    SharedSceneResources* shared_ = nullptr;  ///< P0 池 (非拥有; 上层保证生命周期)
};

} // namespace sensor
} // namespace rockyenv
