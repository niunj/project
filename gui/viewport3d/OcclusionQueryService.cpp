/// @file OcclusionQueryService.cpp
#include "OcclusionQueryService.h"

#include <vsg/maths/common.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace viewport3d {

OcclusionQueryService::OcclusionQueryService() = default;
OcclusionQueryService::~OcclusionQueryService() = default;

void OcclusionQueryService::initialise(std::shared_ptr<ITerrainQuery> tq)
{
    terrain_query_ = std::move(tq);
    invalidateCache();
}

void OcclusionQueryService::invalidateCache()
{
    cache_valid_ = false;
}

bool OcclusionQueryService::hasTerrainFallback() const
{
    return terrain_query_ && terrain_query_->hasTerrain();
}

float OcclusionQueryService::rayMarchTerrain(
    const vsg::dvec3& origin,
    double az_deg, double el_deg, double max_dist) const
{
    if (!terrain_query_ || !terrain_query_->hasTerrain())
        return static_cast<float>(max_dist);

    // 方位/俯仰 → 方向向量
    // az: 绕 +Y, 0 → +Z; el: 0 → 水平, 90 → 正上方
    const double azr = vsg::radians(az_deg);
    const double elr = vsg::radians(el_deg);
    const double cos_el = std::cos(elr);
    const double sin_el = std::sin(elr);
    const vsg::dvec3 dir(std::sin(azr) * cos_el, sin_el,
                         -std::cos(azr) * cos_el);

    // 步进采样: 从原点沿 dir 前进, 每步检查射线高度 vs 地形高度。
    // 射线在某点的 y = origin.y + dir.y * t
    // 地形在该 (x, z) 的高度 = terrain_query_->heightAtWorld(x, z)
    // 若射线 y < 地形高度 → 被遮挡, 返回 t。
    const double step = std::max(max_dist / 200.0, 1.0);  // ~200 步
    const double t_start = 1.0;  // 从 1m 开始, 避免原点自交

    for (double t = t_start; t < max_dist; t += step) {
        const vsg::dvec3 p = origin + dir * t;
        const double terrain_h = terrain_query_->heightAtWorld(p.x, p.z);
        if (p.y < terrain_h) {
            // 射线穿入地形 → 遮挡
            return static_cast<float>(t);
        }
    }
    return static_cast<float>(max_dist);
}

OcclusionResult OcclusionQueryService::query(const OcclusionQuery& q)
{
    // 缓存检查: 相同参数 + 地形版本不变 → 直接返回
    if (cache_valid_ && terrain_query_) {
        const uint64_t gen = terrain_query_->nodeGeneration();
        const uint64_t ver = terrain_query_->heightsVersion();
        const bool same_q =
            cached_query_.origin.x == q.origin.x &&
            cached_query_.origin.y == q.origin.y &&
            cached_query_.origin.z == q.origin.z &&
            cached_query_.az_min_deg == q.az_min_deg &&
            cached_query_.az_max_deg == q.az_max_deg &&
            cached_query_.az_count == q.az_count &&
            cached_query_.el_min_deg == q.el_min_deg &&
            cached_query_.el_max_deg == q.el_max_deg &&
            cached_query_.el_count == q.el_count &&
            cached_query_.max_dist == q.max_dist;
        if (same_q && gen == cached_terrain_gen_ && ver == cached_heights_ver_) {
            return cached_result_;
        }
    }

    // 执行查询
    OcclusionResult result;
    result.az_count = q.az_count;
    result.el_count = q.el_count;
    result.hit_distances.resize(
        static_cast<size_t>(q.az_count) * static_cast<size_t>(q.el_count),
        static_cast<float>(q.max_dist));

    const bool use_terrain = hasTerrainFallback();

    for (int ei = 0; ei < q.el_count; ++ei) {
        const double el = q.el_min_deg + (q.el_max_deg - q.el_min_deg) *
                         (q.el_count > 1 ? double(ei) / double(q.el_count - 1) : 0.0);
        for (int ai = 0; ai < q.az_count; ++ai) {
            const double az = q.az_min_deg + (q.az_max_deg - q.az_min_deg) *
                             (q.az_count > 1 ? double(ai) / double(q.az_count - 1) : 0.0);
            const size_t idx = static_cast<size_t>(ei) * q.az_count + ai;
            if (use_terrain) {
                result.hit_distances[idx] =
                    rayMarchTerrain(q.origin, az, el, q.max_dist);
            } else {
                result.hit_distances[idx] = static_cast<float>(q.max_dist);
            }
        }
    }

    // 更新缓存
    cached_query_ = q;
    cached_result_ = result;
    if (terrain_query_) {
        cached_terrain_gen_ = terrain_query_->nodeGeneration();
        cached_heights_ver_ = terrain_query_->heightsVersion();
    }
    cache_valid_ = true;

    return result;
}

}} // namespace vsgraysim::viewport3d
