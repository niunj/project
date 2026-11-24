#pragma once
/// @file SkyTypes.h
/// @brief rocky_env 内部: 天空后端抽象 (ISkyBackend) 与天体/光照中间结构。
///
/// 唯一后端: RockySkyBackend (ECEF 地心模式, 封装 rocky::SkyNode; rocky 硬依赖)。
/// 原生平面天空后端已随"全产品线 rocky 化"删除。
/// SkyModule 作为 facade 持有后端, 并对外实现 IEnvModule/ISunProvider/ILightingProvider。
#ifndef ROCKY_ENV_SKY_TYPES_H
#define ROCKY_ENV_SKY_TYPES_H

#include <rocky_env/EnvApi.h>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

namespace rockyenv {

class AtmosphereLUT;  // 前向声明 (SkyTypes.h 不依赖其定义)

/// 单个天体 (太阳/月亮) 的方向与角位置。
struct SkyBody {
    vsg::vec3 dirECEF{0.0f, 1.0f, 0.0f};  ///< 指向天体 (ECEF/世界)
    vsg::vec3 dirENU{0.0f, 1.0f, 0.0f};   ///< 指向天体 (锚点 ENU; x=东 y=上 z=南)
    float     elDeg = 0.0f;               ///< 视高度角 (度, 含蒙气差)
    float     azDeg = 0.0f;               ///< 方位角 (度, 北=0 顺时针)
    float     angularRadius = 0.0f;       ///< Observer-centred angular radius in radians
};

/// 一帧内的天体状态。
struct SkyBodies {
    SkyBody sun;
    SkyBody moon;
    float   moonPhase = 0.0f;  ///< 月面照明比例 0..1
    double greenwichSiderealRadians = 0.;
    bool    valid = false;
};

/// 天空运行时状态 (ISkyControl 下发)。
struct SkyRuntime {
    float  exposure = 1.0f;
    float  turbidity = 1.0f;
    bool   show_sun = true;
    bool   show_moon = true;
    bool   show_atmosphere = true;
    bool   params_dirty = false;     // 本帧有参数更新
};

/// 天空后端接口 (内部)。
class ISkyBackend {
public:
    virtual ~ISkyBackend() = default;

    /// 挂到场景的节点 (由 facade 包进 Switch)。
    virtual vsg::Node* node() = 0;

    /// true=ECEF 世界根 (rocky 地球天空)。
    virtual bool worldMounted() const = 0;

    /// 每帧更新 (相机/时间/UBO)。
    virtual void onFrame(const EnvFrame& f) = 0;

    /// 同步 UTC 时刻 (脏标记由实现掌握)。
    virtual void syncDateTime(int year, int month, int day, double hoursUtc) = 0;

    /// 计算天体方向/月相 (时刻由 syncDateTime/onFrame 设定)。
    virtual bool bodies(const vsg::dmat4& enuToWorld, SkyBodies& out) const = 0;

    /// 按物理光照状态驱动场景灯光 (平行光/环境光/月光)。
    virtual void applyLights(const AtmosphereState& s, const SkyBodies& b) = 0;

    /// 应用运行时控制 (曝光/浊度/开关)。facade 在 onFrame 头部调用;
    /// 实现内部按 dirty 标记增量更新。
    virtual void applyRuntime(const SkyRuntime& rt) = 0;
    /// The facade owns sky pixels; backend retains ephemeris and scene lights.
    virtual void setExternalDome(bool) {}

    /// 后端持有的大气 LUT (IAtmosphereQuery 太阳侧透过率采样用)。
    /// 无 LUT 的后端返回 nullptr (查询回退解析公式)。
    virtual const AtmosphereLUT* atmosphereLUT() const { return nullptr; }
};

}  // namespace rockyenv

#endif  // ROCKY_ENV_SKY_TYPES_H
