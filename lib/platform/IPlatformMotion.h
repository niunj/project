#pragma once
/// @file IPlatformMotion.h
/// @brief 平台运动模型抽象接口。
///
/// 所有平台运动模型 (地面/空中/水面/导弹) 均实现此接口。
/// 由 PlatformManager 每帧调度更新。

#include "PlatformTypes.h"

namespace vsgraysim::platform {

/// 运动模型抽象基类
class IPlatformMotion {
public:
    virtual ~IPlatformMotion() = default;

    /// 返回平台类别
    virtual PlatformCategory category() const = 0;

    /// 初始化运动模型 (从场景实体配置加载参数)
    /// @param config  JSON 配置 (从场景文件或预制件读取)
    /// @param initial  初始运动状态 (位置/姿态)
    virtual bool initialize(const nlohmann::json& config,
                            const MotionState& initial) = 0;

    /// 更新一帧运动
    /// @param dt      时间步长 (秒)
    /// @param env     环境上下文 (地形/海浪/风等)
    /// @param cmd     控制指令 (速度/方向/高度等, 由 AI 或用户输入产生)
    /// @param state   [输出] 更新后的运动状态
    /// @return        更新后的运动状态引用
    virtual MotionState& update(double dt,
                                const EnvironmentContext& env,
                                const ControlCommand& cmd,
                                MotionState& state) = 0;

    /// 获取运动参数
    virtual nlohmann::json getParams() const = 0;

    /// 设置运动参数
    virtual void setParams(const nlohmann::json& params) = 0;

    /// 重置运动模型
    virtual void reset() = 0;
};

} // namespace vsgraysim::platform