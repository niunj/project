#pragma once
/// @file EnvFeatures.h
/// @brief rocky 环境模块 (sky/cloud/ocean) 的视口特性薄封装。
///
/// 天空挂 ECEF 世界根，云与海面使用地理锚点。三个特性把相机、锚点和气象数据
/// 传给共享环境模块。体素天空使用 VSG 灯光，兼容模式使用 Rocky 上下文。
#ifndef VSGRAYSIM_VIEWPORT3D_ENVFEATURES_H
#define VSGRAYSIM_VIEWPORT3D_ENVFEATURES_H

#include "IViewportFeature.h"

#include <rocky_env/EnvApi.h>

#include <memory>
#include <vsg/nodes/Switch.h>

namespace vsgraysim { namespace viewport3d {

/// 共用的气象/天体参数与 EnvFrame 组装。
class EnvFeatureBase : public IViewportFeature {
public:
    void onDetach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;
    void setEnabled(bool on) override;

protected:
    /// 读取共用气象参数 (sun_el/sun_az/wind_speed/wind_az/cover)。
    void readCommonParams(const nlohmann::json& params);
    /// W2: 细粒度大气参数实时下发 (GUI 面板 → rocky_env Control 接口)。
    /// 仅当对应模块 Control 已就绪时生效; 参数键见各 Feature::configure。
    virtual void setAtmosphereParams(const nlohmann::json& params);
    /// 组装本帧环境参数 (相机矩阵/ENU 变换/太阳/风)。
    rockyenv::EnvFrame makeFrame(ViewportContext& ctx, const FrameTick& tick) const;
    /// 挂载模块根 (to_world=true 挂世界根[天空], 否则挂锚点 ENU 内容根[云/海])。
    void attachNode(ViewportContext& ctx, bool to_world);

    /// 底层环境模块 (Control/Query 能力的 dynamic_cast 源)。
    rockyenv::IEnvModule* module() const { return module_.get(); }

    std::unique_ptr<rockyenv::IEnvModule> module_;
    vsg::Node* root_node_ = nullptr;
    vsg::ref_ptr<vsg::Group> mount_;
    bool  to_world_root_ = false;
    ViewportContext* context_ = nullptr;

    // 气象/天体 (可被 configure 覆盖)
    float sun_el_    = 55.0f;   ///< 太阳高度角 (deg)
    float sun_az_    = 160.0f;  ///< 太阳方位角 (deg, 北=0 顺时针)
    float wind_speed_ = 6.0f;
    float wind_az_   = 80.0f;
    float cover_     = 0.5f;
    float visibility_ = 10000.0f;  ///< 能见度 (米), 驱动云/海距离雾
    float precipitation_ = 0.0f;

    // 时间参数 (configure 设定基准; 运行时推进由 SkyFeature 仿真时钟统一计算)
    int    year_    = 2026;
    int    month_   = 9;
    int    day_     = 10;
    double hours_utc_ = 4.0;
    double time_scale_ = 1.0;
};

class SkyFeature : public EnvFeatureBase {
public:
    static constexpr const char* kId = "rocky_sky";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void configure(const nlohmann::json& params) override;
    void setAtmosphereParams(const nlohmann::json& params) override;  // W2: 曝光/浊度实时下发

    /// 星历太阳单一真值源 (云/海 Feature 每帧查询); 未就绪返回 nullptr。
    rockyenv::ISunProvider* sunProvider();

    /// 大气光照真值源 (云/海 shader 的太阳色/环境色/雾色); 未就绪返回 nullptr。
    rockyenv::ILightingProvider* lightingProvider();

    /// 大气查询能力 (视线透过率/路径辐射, 供传感器链路); 未就绪返回 nullptr。
    rockyenv::IAtmosphereQuery* atmosphereQuery();

    /// 天空运行时控制 (曝光/浊度/天体开关/锚点); 未就绪返回 nullptr。
    rockyenv::ISkyControl* skyControl();

    /// 当前 SkyParams (只读; 供验证面板显示后端/时间基准)。
    const rockyenv::SkyParams& skyParams() const { return sky_; }

    /// 卸载时把默认灯光特性恢复 (天空挂载期间由天空独占场景灯光)。
    void onDetach(ViewportContext& ctx) override;

    /// 仿真时钟: 以 configure 设定的 UTC 为基准, 按帧 dt * time_scale 推进
    /// (同帧多次调用只推进一次), 输出归一化后的当前 UTC 日期时间。
    /// 云/海/天空共享此结果, 杜绝各模块各算一份时间。
    double clockSeconds() const { return sim_seconds_; }
    void currentDateTime(const FrameTick& tick,
                         int& year, int& month, int& day, double& hoursUtc);

private:
    rockyenv::SkyParams sky_;
    uint64_t clock_frame_ = 0;     ///< 上次推进时钟的帧号 (帧内幂等)
    double   sim_seconds_ = 0.0;   ///< 基准时刻起累计的仿真秒
};

class CloudFeature : public EnvFeatureBase {
public:
    static constexpr const char* kId = "rocky_cloud";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;
    void configure(const nlohmann::json& params) override;
    void setAtmosphereParams(const nlohmann::json& params) override;  // W2: 云量/云高实时下发

    /// 云运行时控制 (云量/分层/开关); 未就绪返回 nullptr。
    float sceneCover() const { return cover_; }
    float rain() const { return precipitation_; }
    float visibility() const { return visibility_; }
    float windSpeed() const { return wind_speed_; }
    float windAzimuth() const { return wind_az_; }
    const rockyenv::CloudParams& parameters() const { return cloud_; }
    rockyenv::ICloudControl* cloudControl();
    /// 云查询能力 (云底/云顶/云影因子/总云量); 未就绪返回 nullptr。
    rockyenv::ICloudQuery* cloudQuery();
private:
    rockyenv::CloudParams cloud_;
    bool local_rendered_ = false;
};

class OceanFeature : public EnvFeatureBase, public rockyenv::ITerrainBridge {
public:
    void onFrame(ViewportContext&,const FrameTick&) override;
    void onDetach(ViewportContext& ctx) override;
    void setEnabled(bool on) override;
    static constexpr const char* kId = "rocky_ocean";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void configure(const nlohmann::json& params) override;
    void setAtmosphereParams(const nlohmann::json& params) override;  // W2: 风/浪实时下发

    /// 海洋运行时控制 (风/波高/涌浪/反射/曲率/水色/边界); 未就绪返回 nullptr。
    float windSpeed() const { return wind_speed_; }
    float windAzimuth() const { return wind_az_; }
    const rockyenv::OceanParams& parameters() const { return ocean_; }
    rockyenv::IOceanControl* oceanControl();
    /// 海洋仿真采样 (波高/法线/轨道速度/白沫/海况); 未就绪返回 nullptr。
    rockyenv::IOceanSampler* oceanSampler();

    // Phase 2: ITerrainBridge 实现
    float waterBedENU(double ex,double ez,float seaLevel) const override;
    float heightENU(double ex, double ez) const override;
    vsg::vec3 normalENU(double ex, double ez) const override;
    bool isLand(double ex, double ez, float seaLevel) const override;
    float waterDepth(double ex, double ez, float seaLevel) const override;
    uint64_t revision() const override;
    
private:
    rockyenv::OceanParams ocean_;
    vsg::ref_ptr<vsg::Switch> compute_switch_;
    ViewportContext* ctx_ = nullptr;  // 用于地形查询
};

}} // namespace vsgraysim::viewport3d
#endif  // VSGRAYSIM_VIEWPORT3D_ENVFEATURES_H
