#pragma once
/// @file SceneBVH.h
/// @brief Layer 2: 仿真侧自建三角形 BVH 加速结构 (射线求交)。
///
/// 取代每射线调用 Bullet 物理世界遍历 (btCollisionWorld::rayTest 需检查
/// 全部刚体, 长程雷达射线下代价过高)。构建完成后只读, 可被多个追踪
/// 工作线程无锁并发查询; 静态几何一次构建, 动态几何按刚体独立建树。

#include <cstdint>
#include <vector>

#include "vsgraysim/core/RayPayload.h"

namespace vsgraysim { namespace detail {

/// 单次命中结果 (沿射线距离 + 命中面法线, 法线已朝向射线来源)。
struct BvhHit {
    float t = 0.0f;
    Vec3 normal{};
    uint32_t primitive_id = 0; // triangle index in the ORIGINAL input mesh
};

/// 中位分割三角形 BVH。构建后只读 (线程安全)。
class SceneBVH {
public:
    /// 以世界系 (或同一局部系) 三角形构建; `tris` 每 3 个顶点一个三角形。
    void build(const std::vector<Vec3>& tris);
    void clear();
    bool empty() const { return nodes_.empty(); }
    std::size_t triangleCount() const { return verts_.size() / 3; }

    /// 最近命中查询: 仅报告 t < max_t 的命中。
    bool intersect(const Vec3& origin, const Vec3& dir,
                   float max_t, BvhHit& out) const;

private:
    struct Node {
        float mn[3];
        float mx[3];
        int32_t child0 = -1;   ///< <0 表示叶子
        int32_t child1 = -1;
        uint32_t first = 0;    ///< 叶子: 首个三角形下标 (按 3 顶点计)
        uint32_t count = 0;    ///< 叶子: 三角形数
    };

    int32_t buildRec(std::vector<uint32_t>& order, std::size_t lo, std::size_t hi);

    std::vector<Vec3> verts_;              ///< 叶子引用的三角形顶点 (3/三角形)
    std::vector<uint32_t> primitive_ids_;
    std::vector<Node> nodes_;
    // 构建期暂存
    const std::vector<Vec3>* src_ = nullptr;
    std::vector<Vec3> cent_;               ///< 每三角形质心
};

}} // namespace vsgraysim::detail
