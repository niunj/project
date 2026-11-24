#pragma once
/// @file PlatformMotionFactory.h
/// @brief 平台运动工厂 — 从场景实体创建平台实例。
///
/// 读取 SceneProduct::entities 中的实体配置,
/// 根据实体标签/组件自动创建对应的平台运动模型和装备。

#include "PlatformTypes.h"
#include "PlatformManager.h"

#include "vsgraysim/products/SceneProduct.h"

namespace vsgraysim::platform {

/// 平台运动工厂
class PlatformMotionFactory {
public:
    /// 从场景产品创建所有平台
    static size_t createFromScene(const SceneProduct& scene, PlatformManager& manager, uint32_t& next_equip_id);

    /// 从单个实体创建平台
    static uint32_t createFromEntity(const Entity& entity, PlatformManager& manager, uint32_t& next_equip_id);

    /// 从 JSON 实体创建平台 (在场景文件 JSON 中)
    static uint32_t createFromJSON(const nlohmann::json& entity_json, PlatformManager& manager, uint32_t& next_equip_id);

    /// 从 JSON 对象创建平台 (兼容旧格式场景文件)
    static uint32_t createFromObjectJSON(const nlohmann::json& obj_json, PlatformManager& manager, uint32_t& next_equip_id);

    /// 判断实体是否为可移动平台 (基于标签)
    static bool isPlatformEntity(const Entity& entity);

    /// 从实体标签推断平台类别
    static PlatformCategory detectCategory(const Entity& entity);

    /// 构建运动模型配置 JSON (从实体组件)
    static nlohmann::json buildMotionConfig(const Entity& entity,
                                            PlatformCategory cat);

    /// 构建初始运动状态 (从实体变换)
    static MotionState buildInitialState(const Entity& entity);

    /// 检测并创建装备 (从实体标签/组件)
    static void detectAndCreateEquipment(
        uint32_t platform_id,
        const Entity& entity,
        PlatformManager& manager,
        uint32_t& next_equip_id);

private:
    /// 从 JSON 构建运动配置 (内部辅助)
    static nlohmann::json buildMotionConfigJSON(const nlohmann::json& json, PlatformCategory cat);

    /// 创建默认装备 (内部辅助)
    static void createDefaultEquipment(uint32_t platform_id, const nlohmann::json& json,
                                       PlatformCategory cat, PlatformManager& manager,
                                       uint32_t& next_equip_id);
};

} // namespace vsgraysim::platform