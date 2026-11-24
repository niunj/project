#pragma once
/// @file SensorInstance.h —— 多传感器仿真渲染: 传感器实例模型 (设计文档 §12c B1)。
///
/// SensorInstance = SensorDesc (场景 JSON 投影) 的运行态解析:
///   场景描述 → 完整 SensorProfile (渲染链可消费) + 世界系安装位姿/初始 LOS。
/// SensorRegistry::createFromScene = 场景驱动设备注册 (多实例, 显控设备树数据源)。
///
/// 参数优先级 (工程硬约束): Param 消息 (运行态覆盖) > 场景 JSON > SensorApi.h
/// 硬编码默认 —— SensorProfile 由 SensorDesc 填充, 运行态由 Param 消息覆盖。
///
/// 帧类型分型:
///   成像类 (VIS/SWIR/MWIR/LWIR): renderable=true, 可直接进 SensorPipeline;
///   相干类 (RADAR): 仅注册 (B6 回波链接入前不进图像渲染链)。
#ifndef ROCKY_ENV_SENSOR_INSTANCE_H
#define ROCKY_ENV_SENSOR_INSTANCE_H

#include "rocky_env/SceneSource.h"
#include "rocky_env/SensorApi.h"

#include <vsg/maths/vec3.h>

#include <string>
#include <vector>

namespace rockyenv {
namespace sensor {

/// 传感器实例 (场景驱动的运行态注册项)。
struct SensorInstance {
    const SensorDesc* desc = nullptr;   ///< 场景来源描述 (SceneData 持有, 非拥有)
    SensorProfile profile;              ///< 渲染链完整参数 (kind→BandSpec 映射)
    vsg::dvec3 position_enu{0.0, 0.0, 0.0};  ///< 平台位置 + aperture_offset (世界系, m)
    vsg::dvec3 los_dir{0.0, 0.0, -1.0};      ///< 初始 LOS (机体系 direction, 单位化)
    bool renderable = false;            ///< 成像类可进渲染链; RADAR 仅注册
};

/// 传感器注册表 (场景 → 多实例)。
class SensorRegistry {
public:
    /// SensorDesc → 渲染链 SensorProfile (同名映射; λ 显式值覆盖规范范围)。
    static SensorProfile profileFromDesc(const SensorDesc& d);

    /// 场景 → 实例注册表 (保持 sensor_list 顺序)。
    /// 平台未绑定时 ENU 原点安装并告警; LOS = 机体系 direction 单位化。
    static std::vector<SensorInstance> createFromScene(const SceneData& data);

    /// 按 sensor_id / name / 序号选择实例 (CLI/Param 选择语义); 无命中 nullptr。
    static const SensorInstance* select(const std::vector<SensorInstance>& registry,
                                        const std::string& key);
};

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_SENSOR_INSTANCE_H
