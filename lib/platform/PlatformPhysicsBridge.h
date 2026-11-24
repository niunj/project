#pragma once
/// @file PlatformPhysicsBridge.h
/// @brief 平台-Bullet3 物理桥接层。
///
/// 将平台运动模型产生的期望速度应用到 Bullet3 刚体，由物理引擎
/// 处理重力、碰撞、地形交互后，读取结果回平台运动状态。

#include "PlatformTypes.h"
#include "vsgraysim/layer0_phys/PhysicsEngine.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace vsgraysim::platform {

/// 物理桥接层 — 管理平台刚体的创建/同步/销毁
class PlatformPhysicsBridge {
public:
    explicit PlatformPhysicsBridge(PhysicsEngine* physics);
    ~PlatformPhysicsBridge();

    // ---- 刚体管理 ----

    /// 复用同名场景刚体；不存在时创建物理引擎持有的盒形刚体。
    /// @param platform_id  平台 ID
    /// @param name         平台名称
    /// @param category     平台类别 (决定碰撞形状/质量/物理属性)
    /// @param initial_pos  初始位置
    /// @param initial_rot  初始姿态 (yaw, pitch, roll 度)
    /// @param extents      碰撞盒半尺寸 (m)
    bool createBody(uint32_t platform_id, const std::string& name,
                    PlatformCategory category,
                    const vsg::dvec3& initial_pos,
                    const vsg::dvec3& initial_rot,
                    const vsg::dvec3& extents);

    /// 解除控制；只删除此桥创建的刚体，保留复用的场景刚体。
    void removeBody(uint32_t platform_id);

    // ---- 同步接口 ----

    /// 将运动模型期望速度施加到刚体 (pre-step)
    void applyVelocity(uint32_t platform_id, const vsg::dvec3& velocity);
    void setAuthority(uint32_t platform_id, bool kinematic);
    void applyMotionCommand(uint32_t platform_id, const MotionState& desired,
                            double end_time, double dt, bool kinematic);

    /// 施加中央力 (导弹助推/飞机推力)
    void applyCentralForce(uint32_t platform_id, const vsg::dvec3& force);

    /// 读取物理引擎结果回 MotionState (post-step)
    bool readMotionState(uint32_t platform_id, MotionState& state) const;

    // ---- 查询 ----

    bool bodyExists(uint32_t platform_id) const;
    vsg::dvec3 getPosition(uint32_t platform_id) const;
    vsg::dvec3 getVelocity(uint32_t platform_id) const;
    vsg::dquat getRotation(uint32_t platform_id) const;

    /// 移除所有刚体
    void removeAllBodies();

    size_t bodyCount() const { return records_.size(); }

// ---- 默认参数 (公共, 供创建时使用) ----
    static vsg::dvec3 defaultExtents(PlatformCategory cat);
    static float defaultMass(PlatformCategory cat);

private:
    // 内部记录 (隐藏 Bullet 类型)
    struct BodyRecord;

    PhysicsEngine* physics_ = nullptr;
    std::unordered_map<uint32_t, std::unique_ptr<BodyRecord>> records_;
};

} // namespace vsgraysim::platform
