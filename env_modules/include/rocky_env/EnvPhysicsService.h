#pragma once
/// @file EnvPhysicsService.h —— 环境物理服务 (设计文档 §6 环境契约的实现者)。
///
/// 职责: 把 rocky_env 环境栈 (sky/cloud 模块) 的运行时状态映射为传感器侧的
/// 波段级 EnvAnswer:
///   - AtmosphereState (ILightingProvider): 昼因子/太阳灯光/能见度消光 —— 昼夜
///     与太阳高度真值 (星历单一真值源, 与天空渲染画面同源)
///   - ICloudQuery: 总云量/云底高度 —— 云消光与云顶辐射 (与云渲染画面同源)
/// 无环境栈时 update(EnvFrame) 独立计算同一星历/天气；旧标量刷新保留默认空间状态。
///
/// 映射物理 (波段级):
///   σ(λ)      = σ_分子(λ, 晴空基线) + σ_气溶胶(λ, 能见度 Koschmieder+Ångström)
///               + σ_云(λ, 云量×层云消光)
///   sky_rad   = 晴空基线 × (昼因子插值) + 云贡献 (云顶按层云温度的普朗克发射)
///   sun_irr   = 晴空基线 × sun_light 强度 × 云遮蔽 (1-0.8·coverage) × is_day
///
/// 时序: 宿主每帧驱动环境栈 onFrame 后调用 update(simTime) 刷新缓存, 再对
/// 每个传感器请求 answer(req) —— 参数优先级 Param 消息 > 场景 JSON > 缺省。

#include "rocky_env/SensorApi.h"

#include <vsg/maths/vec3.h>
#include <cstdint>
#include <mutex>

namespace rockyenv {

class IEnvStack;
struct EnvFrame;
class IAtmosphereQuery;   // 批次2: 谱大气 bandSky 能力接口 (EnvApi.h)
class IOceanSampler;

namespace sensor {

class EnvPhysicsService {
public:
    EnvPhysicsService() = default;
    ~EnvPhysicsService() = default;

    /// 绑定环境栈 (不拥有生命周期; nullptr = 晴空回退)。
    void bind(IEnvStack* stack);

    /// 每帧刷新环境真值缓存 (宿主已在栈上调用 onFrame 后调用)。
    /// @param simTime 仿真时刻 (s) @param hoursUtc UTC 时刻 (h) @param cloudCover
    /// 外部云量覆盖 (0..1, <0 = 用栈内值) @param windSpeed 风速 (m/s)
    /// @param precip 降水强度 (0..1, 1=暴雨; <0 = 无雨/未提供)
    void update(double simTime, double hoursUtc, double cloudCover, double windSpeed,
                double precip = -1.0);
    /// Headless physical environment: same Rocky ephemeris and atmosphere as sky.
    void update(const EnvFrame& frame);

    /// 完整环境应答；首次按需计算当前 update 批次的精确海洋白沫均值。
    /// 同批次可并发查询；bind/update 与环境栈变更须在查询批次之间进行。
    EnvAnswer answer(const EnvRequest& req) const;
    /// 仅查询大气/光照，不进行海洋空间采样。海洋扩展字段保留缺省值；
    /// 消费白沫/海温/海况的调用者必须使用完整 answer()。
    EnvAnswer answerAtmosphere(const EnvRequest& req) const;

    /// 批次2: 云顶发射天空贡献 (W/m²/sr; 与 answer() 云项同式 —— 谱天空图
    /// 烘焙复用, 保证点查询与图烘焙云行为一致)。
    double cloudContribution(const BandSpec& band) const;

    /// 雨消光系数 (1/km, Napier; Marshall-Palmer 按波段) —— 供天空图烘焙
    /// 计算雨幕衰减/雨幕亮度, 与 answer() 的 σ 雨项同式同源。
    double rainExtinction(const BandSpec& band) const;

    // ---- 缓存真值 (调试/UI 回显) ----
    double cloudCoverage() const { return cloudCover_; }
    double precipitation() const { return precip_; }
    double hoursUtc() const { return hoursUtc_; }
    /// 昼因子真值 (AtmosphereState; update 后有效) —— B1 贴图升温幅度判据
    double isDay() const { return isDay_; }
    /// 晴空气溶胶能见度真值 (m; AtmosphereState 同源 —— bandSky 气溶胶 σ 与
    /// 灰天谱形权值 κ 共用此单一真值; 注意不含雨 Koschmieder 降视, 那是
    /// EnvAnswer::visibility_m 的合成值)
    double visibilityM() const { return visibilityM_; }
    bool   stackBound() const { return envStack_ != nullptr; }
    /// 指向太阳的 ENU 单位方向 (AtmosphereState 同源; 无栈回退旧缺省方向)。
    /// 宿主可用于管线 sun dir, 与天空渲染/传感器链保持单一真值。
    vsg::vec3 sunDirEnu() const { return sunDirEnu_; }

private:
    EnvAnswer answerImpl(const EnvRequest& req, bool includeOcean) const;
    double representativeFoam() const;
    IEnvStack* envStack_ = nullptr;   ///< 环境栈 (sky/cloud; 不拥有)
    IAtmosphereQuery* bandAtmo_ = nullptr;  ///< 批次2: 谱大气能力缓存 (update 时 dynamic_cast)
    double cloudCover_ = 0.0;         ///< 总云量缓存 (栈内 ICloudQuery 或外部覆盖)
    double cloudBaseM_ = 1200.0;      ///< 云底高度缓存 (m)
    double precip_ = 0.0;             ///< 降水强度缓存 (0..1; R = p·50 mm/h)
    double sunLight_ = 1.0;           ///< 太阳灯光强度因子 (AtmosphereState LDR)
    double isDay_ = 1.0;              ///< 昼因子 (AtmosphereState)
    double sunElevationDeg_ = 65.75;  ///< Cached ephemeris elevation, independent of a viewer.
    bool physical_state_valid_ = false;
    double visibilityM_ = 10000.0;    ///< 能见度 (m)
    double hoursUtc_ = 4.0;
    double simTime_ = 0.0;
    // ---- 批次3: 海洋真值缓存 (IOceanSampler; 无海洋 = 0/-1) ----
    double seaTempK_ = 0.0;           ///< 海表温度 (K); 0 = 无海洋
    IOceanSampler* oceanSampler_ = nullptr; ///< 栈持有；仅在完整应答中进行空间采样
    mutable std::mutex ocean_cache_mutex_;
    uint64_t ocean_generation_ = 0;
    mutable uint64_t foam_generation_ = 0;
    mutable bool foam_cached_ = false;
    mutable double foam_time_ = 0.0;
    mutable double foamFraction_ = 0.0; ///< 本批次精确的原点周边 4×4 均值
    int    beaufort_ = -1;            ///< 蒲福风级; -1 = 无海洋
    vsg::vec3 sunDirEnu_{0.3f, 0.8f, 0.2f};   ///< 指向太阳 (ENU, 星历真值缓存)
};

} // namespace sensor
} // namespace rockyenv
