#pragma once
/// @file PlatformManager.h
/// @brief 平台管理器 — 管理所有平台运动模型与装备联动。
///
/// 作为 ISimulationSubsystem 注册到 TimingBus:
///   - Priority: -10 (在 EquipmentDataSource 之前运行)
///   - onTick: 更新所有平台的运动状态 → 更新装备联动状态
///   - 提供平台状态查询接口供其他子系统 (EquipmentDataSource) 调用

#include "vsgraysim/subsystems/ISubsystem.h"
#include "vsgraysim/kernel/TimeTick.h"
#include "PlatformTypes.h"

#include "IPlatformMotion.h"
#include "PlatformEquipment.h"
#include "PlatformPhysicsBridge.h"
#include "RoutePlan.h"
#include "RelationshipGraph.h"
#include <vsgraysim/broker/FlightControl.h>
#include <chrono>

#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace vsgraysim::platform {

/// 平台状态查询结果
struct PlatformQueryResult {
    bool found = false;
    PlatformInstance* platform = nullptr;
    IPlatformMotion* motion = nullptr;
    PlatformEquipment* equipment = nullptr;
};

/// 平台管理器
class PlatformManager : public ISimulationSubsystem {
public:
    PlatformManager() = default;
    ~PlatformManager() override;

    // ---- ISubsystem ----
    std::string name() const override { return "PlatformManager"; }
    std::string version() const override { return "2.0.0"; }
    bool initialize(const nlohmann::json& config) override;
    void shutdown() override;

    // ---- ISimulationSubsystem ----
    void onPreTick(const TimeTick& tick) override;
    void onTick(const TimeTick& tick) override;
    void onPostTick(const TimeTick& tick) override;
    void onStart() override;
    void onStop() override;
    void* getMutableProduct(const std::string& product_type) override;
    const void* getConstProduct(const std::string& product_type) const override;
    int tickPriority() const override { return -10; }

    // ---- 物理引擎集成 ----

    /// 设置物理引擎指针 (由 SimulationControlWindow 传递)
    void setPhysicsEngine(PhysicsEngine* physics);

    /// 获取物理桥接层
    PhysicsEngine* physicsEngine() const { return physics_; }

    // ---- 平台管理 ----

    /// 创建平台 (从场景实体配置)
    uint32_t createPlatform(const std::string& name,
                            PlatformCategory category,
                            const nlohmann::json& motion_config,
                            const MotionState& initial_state);

    /// 移除平台
    void removePlatform(uint32_t platform_id);

    /// 查询平台
    PlatformQueryResult queryPlatform(uint32_t platform_id);

    /// 获取所有平台
    std::vector<PlatformInstance*> allPlatforms();

    /// 获取指定类别的平台
    std::vector<PlatformInstance*> platformsByCategory(PlatformCategory cat);

    /// 给平台发送控制指令
    void sendCommand(uint32_t platform_id, const ControlCommand& cmd);
    vsgraysim::FlightControlResult applyFlightControl(const vsgraysim::FlightControlCommand&, double sim_time);
    void updateFlightControlLeases(double sim_time);
    void resetFlightControls();

    /// 设置环境上下文 (由仿真控制软件每帧更新)
    void setEnvironmentContext(const EnvironmentContext& env);

    // ---- 航迹规划调度 ----

    /// 为平台分配航迹计划 (场景设计软件提前规划, 带时间参数)
    bool assignRoute(uint32_t platform_id, const RoutePlan& route);

    /// 清除平台的航迹计划 (恢复自由控制)
    void clearRoute(uint32_t platform_id);

    /// 平台是否已有航迹计划
    bool hasRoute(uint32_t platform_id) const;

    /// 获取平台当前航迹 (拷贝)
    RoutePlan getRoute(uint32_t platform_id) const;

    /// 外部是否可通过 sendCommand 覆盖航迹 (若 true, 手动指令优先)
    void setManualOverride(bool enabled) { manual_override_ = enabled; }

    /// 获取平台数量
    size_t platformCount() const { return platforms_.size(); }

    /// 获取活跃平台数量 (alive=true)
    size_t activePlatformCount() const;

    // ---- 装备联动 ----

    /// 给平台添加装备
    bool addEquipment(uint32_t platform_id, uint32_t equipment_id,
                      EquipmentType type, const std::string& name);

    /// 移除平台装备
    bool removeEquipment(uint32_t platform_id, uint32_t equipment_id);

    /// 设置装备电源状态
    bool setEquipmentPower(uint32_t platform_id, uint32_t equipment_id, bool powered);

    // ---- 绑定关系图 ----

    /// 访问场景绑定关系图 (装备挂载 / 导引链 / 协同关系)
    RelationshipGraph& relationships() { return graph_; }
    const RelationshipGraph& relationships() const { return graph_; }

    // ---- 阵营 ----

    /// 设置平台阵营 (0=中立 1=蓝方进攻 2=红方防御)
    void setPlatformFaction(uint32_t platform_id, int faction);
    int  platformFaction(uint32_t platform_id) const;

    // ---- 交战闭环 (制导雷达 -> 红方地空导弹拦截) ----

    /// 请求交战: 在 target_pos 附近 gate_m 门限内关联最近的蓝方飞行目标,
    /// 指派一枚红方地空导弹解锁发射并制导拦截.
    /// 返回关联到的蓝方目标平台 ID (0=未找到目标或无可发射拦截弹).
    uint32_t requestEngagement(const vsg::dvec3& target_pos, double gate_m,
                               double sim_time);

    /// 拦截弹状态快照 (供仿真控制软件回传显控"制导雷达为导弹提供信息")
    nlohmann::json engagementsSnapshot() const;

    // ---- 查询 ----

    /// 获取平台状态快照 (JSON, 供 UI 显示)
    nlohmann::json getPlatformSnapshot(uint32_t platform_id) const;

    /// 获取所有平台状态快照
    nlohmann::json getAllPlatformsSnapshot() const;

private:
    struct FlightState {
        vsgraysim::FlightControlCommand command;
        std::string mode="route", owner;
        uint64_t revision=0;
        bool failsafe=false;
        bool gearDown=false, navLights=false, landingLight=false;
        double gearExtension=0;
        uint64_t systemsRevision=0;
        std::chrono::steady_clock::time_point deadline;
    };
    void stabilizeFlight(uint32_t id, FlightState&);
    nlohmann::json flightSnapshot(const PlatformInstance&) const;
    std::unordered_map<uint32_t,FlightState> flight_controls_;
    // 每帧更新所有平台的运动模型 (计算期望速度)
    void updateAllPlatformsMotion(double dt, const EnvironmentContext& env, double end_time);

    // 每帧更新所有平台的装备联动
    void updateAllPlatformsEquipment(double dt);

    // 为平台创建对应的运动模型
    std::unique_ptr<IPlatformMotion> createMotionModel(PlatformCategory cat);

    // 航迹调度: 根据仿真时间生成运动控制指令
    void scheduleRoutes(double sim_time, double dt);

    // 将航点目标转换为控制指令 (目标方位/速度 → desired_yaw_rate/desired_speed)
    ControlCommand makeRouteCommand(uint32_t platform_id,
                                    const RoutePlan& route,
                                    const MotionState& state,
                                    double sim_time) const;

    // 工具: 归一化角度到 [-180, 180]
    static double wrapAngle(double deg);

    // 交战引擎内部
    struct EngagementRec {
        uint32_t interceptor_id = 0;   // 红方拦截弹平台
        uint32_t target_id = 0;        // 蓝方目标平台
        bool     launched = false;     // 已发射
        bool     in_flight = false;    // 飞行中 (制导信息持续下传)
        bool     impacted = false;     // 已结束 (命中或脱靶/自毁)
        bool     target_destroyed = false;
        double   launch_time = 0.0;
        double   impact_time = 0.0;
    };

    // 每帧: 为在交战中的拦截弹喂给目标实时位置 (制导雷达信息的仿真侧载体)
    void injectEngagementGuidance();
    // 每帧: 判定拦截弹命中/结束, 摧毁目标
    void resolveEngagements(double dt, double sim_time);

    std::unordered_map<uint32_t, EngagementRec> engagements_; // by interceptor_id

    std::unordered_map<uint32_t, std::unique_ptr<PlatformInstance>> platforms_;
    std::unordered_map<uint32_t, std::unique_ptr<IPlatformMotion>> motion_models_;
    std::unordered_map<uint32_t, std::unique_ptr<PlatformEquipment>> equipment_links_;
    std::unordered_map<uint32_t, ControlCommand> pending_commands_;

    // 航迹计划
    std::unordered_map<uint32_t, RoutePlan> route_plans_;
    std::unordered_map<uint32_t, int>      route_segments_; // 当前所在段 (调试/断点)
    bool manual_override_ = false;

    EnvironmentContext env_context_;
    uint32_t next_platform_id_ = 1;

    // 物理引擎
    PhysicsEngine* physics_ = nullptr;
    std::unique_ptr<PlatformPhysicsBridge> physics_bridge_;

    // 场景绑定关系图 (装备挂载 / 导引链)
    RelationshipGraph graph_;

    // 上一帧速度缓存 (用于加速度估算)
    std::unordered_map<uint32_t, vsg::dvec3> prev_velocities_;

    mutable std::mutex mtx_;
    bool initialized_ = false;
    bool running_ = false;
};

} // namespace vsgraysim::platform
