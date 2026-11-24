#pragma once
/// @file OcclusionQueryService.h
/// @brief RT 遮挡查询服务: 从传感器原点沿扇形半球角度范围发射射线,
///        返回每条射线的命中距离, 供 DetectionEffectFeature 雕刻探测体积。
///
/// 实现策略:
///   - 有 RT 支持时: 复用 Vulkan RT 管线 (TLAS/BLAS + vkCmdTraceRaysKHR),
///     对场景中的地形+建筑+模型做精确遮挡。
///   - 无 RT 支持时: 回退到 ITerrainQuery 地形高度采样,
///     沿射线方向逐步前进采样地形高度, 被地形遮挡的方向截断。
///   - 结果缓存: 相同参数+地形版本不变时直接返回缓存。
#ifndef VSGRAYSIM_VIEWPORT3D_OCCLUSIONQUERYSERVICE_H
#define VSGRAYSIM_VIEWPORT3D_OCCLUSIONQUERYSERVICE_H

#include "ViewportTypes.h"

#include <vsg/maths/vec3.h>

#include <memory>
#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

/// 单次遮挡查询的输入参数。
struct OcclusionQuery {
    vsg::dvec3 origin{0.0, 0.0, 0.0};  ///< 射线原点 (世界系)
    double az_min_deg = 0.0;           ///< 方位范围最小值 (deg)
    double az_max_deg = 360.0;         ///< 方位范围最大值 (deg)
    int    az_count    = 36;           ///< 方位方向射线数
    double el_min_deg = 0.0;           ///< 俯仰范围最小值 (deg, 0=水平)
    double el_max_deg = 90.0;          ///< 俯仰范围最大值 (deg, 90=正上方)
    int    el_count    = 10;           ///< 俯仰方向射线数
    double max_dist    = 500.0;        ///< 最大作用距离 (m)
};

/// 遮挡查询结果: 每条射线的命中距离 (未命中返回 max_dist)。
/// 数组布局: [el_idx * az_count + az_idx], 按俯仰外层、方位内层排列。
struct OcclusionResult {
    std::vector<float> hit_distances;  ///< az_count * el_count 条射线的距离
    int az_count = 0;
    int el_count = 0;
};

class OcclusionQueryService {
public:
    OcclusionQueryService();
    ~OcclusionQueryService();

    /// 初始化: 注入地形查询接口 (来自 TerrainLayerFeature)。
    /// ITerrainQuery 可为空 (纯 RT 模式); 非空则提供地形回退。
    void initialise(std::shared_ptr<ITerrainQuery> terrain_query);

    /// 执行遮挡查询。返回每条射线的命中距离。
    /// 相同参数 + 地形版本不变时使用缓存。
    OcclusionResult query(const OcclusionQuery& q);

    /// 使缓存失效 (场景结构变化时调用)。
    void invalidateCache();

    /// 地形查询是否可用 (回退路径的前提)。
    bool hasTerrainFallback() const;

    /// RT 是否可用 (需硬件支持 + 已初始化 RT 管线)。
    bool rtAvailable() const { return false; }  // 当前: RT 管线占位, 仅地形回退

private:
    /// 地形回退: 沿射线方向步进采样地形高度, 被遮挡时返回遮挡距离。
    float rayMarchTerrain(const vsg::dvec3& origin,
                          double az_deg, double el_deg,
                          double max_dist) const;

    std::shared_ptr<ITerrainQuery> terrain_query_;
    // 缓存
    OcclusionQuery cached_query_{};
    OcclusionResult cached_result_{};
    uint64_t cached_terrain_gen_ = 0;
    uint64_t cached_heights_ver_ = 0;
    bool cache_valid_ = false;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_OCCLUSIONQUERYSERVICE_H
