/// @file SceneBVH.cpp
/// @brief 仿真侧自建三角形 BVH (中位分割 + slab 遍历 + Möller–Trumbore)。
#include "SceneBVH.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace vsgraysim { namespace detail {

namespace {

constexpr uint32_t kLeafTris = 8;
constexpr float kInf = std::numeric_limits<float>::infinity();

inline float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 sub3(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 norm3(const Vec3& v) {
    const float len = std::sqrt(dot3(v, v));
    if (len < 1e-12f) return {0.0f, 1.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

/// slab 射线-盒测试 (t ∈ [0, tmax] 内有交)。
inline bool rayBox(const Vec3& o, const Vec3& inv, float tmax,
                   const float mn[3], const float mx[3]) {
    const float* ov = &o.x;
    const float* iv = &inv.x;
    float tmin = 0.0f, tfar = tmax;
    for (int a = 0; a < 3; ++a) {
        float t1 = (mn[a] - ov[a]) * iv[a];
        float t2 = (mx[a] - ov[a]) * iv[a];
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tfar = std::min(tfar, t2);
        if (tmin > tfar) return false;
    }
    return true;
}

/// Möller–Trumbore 射线-三角形。
inline bool rayTri(const Vec3& o, const Vec3& d,
                   const Vec3& v0, const Vec3& v1, const Vec3& v2, float& t) {
    const Vec3 e1 = sub3(v1, v0);
    const Vec3 e2 = sub3(v2, v0);
    const Vec3 p = cross3(d, e2);
    const float det = dot3(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv_det = 1.0f / det;
    const Vec3 s = sub3(o, v0);
    const float u = dot3(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = cross3(s, e1);
    const float v = dot3(d, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = dot3(e2, q) * inv_det;
    return t > 1e-4f;
}

} // anonymous namespace

void SceneBVH::build(const std::vector<Vec3>& tris) {
    clear();
    const std::size_t n = tris.size() / 3;
    if (n == 0) return;

    src_ = &tris;
    cent_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& a = tris[3 * i];
        const Vec3& b = tris[3 * i + 1];
        const Vec3& c = tris[3 * i + 2];
        cent_[i] = {(a.x + b.x + c.x) / 3.0f,
                    (a.y + b.y + c.y) / 3.0f,
                    (a.z + b.z + c.z) / 3.0f};
    }
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);

    nodes_.reserve(n / 4 + 16);
    verts_.reserve(tris.size());
    buildRec(order, 0, n);

    src_ = nullptr;
    cent_.clear();
    cent_.shrink_to_fit();
}

void SceneBVH::clear() {
    nodes_.clear();
    verts_.clear();
    primitive_ids_.clear();
    src_ = nullptr;
    cent_.clear();
}

int32_t SceneBVH::buildRec(std::vector<uint32_t>& order, std::size_t lo, std::size_t hi) {
    const int32_t id = static_cast<int32_t>(nodes_.size());
    nodes_.emplace_back();

    // 节点包围盒 (覆盖全部三角形顶点)
    float mn[3] = {kInf, kInf, kInf};
    float mx[3] = {-kInf, -kInf, -kInf};
    for (std::size_t i = lo; i < hi; ++i) {
        const uint32_t t = order[i];
        for (int k = 0; k < 3; ++k) {
            const Vec3& v = (*src_)[3 * t + k];
            const float* vv = &v.x;
            for (int a = 0; a < 3; ++a) {
                mn[a] = std::min(mn[a], vv[a]);
                mx[a] = std::max(mx[a], vv[a]);
            }
        }
    }

    const std::size_t count = hi - lo;
    if (count <= kLeafTris) {
        Node& nd = nodes_[id];
        for (int a = 0; a < 3; ++a) { nd.mn[a] = mn[a]; nd.mx[a] = mx[a]; }
        nd.child0 = -1;
        nd.child1 = -1;
        nd.first = static_cast<uint32_t>(verts_.size() / 3);
        nd.count = static_cast<uint32_t>(count);
        for (std::size_t i = lo; i < hi; ++i) {
            const uint32_t t = order[i];
            primitive_ids_.push_back(t);
            verts_.push_back((*src_)[3 * t]);
            verts_.push_back((*src_)[3 * t + 1]);
            verts_.push_back((*src_)[3 * t + 2]);
        }
        return id;
    }

    // 最长轴中位分割
    const float ext[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;
    const std::size_t mid = lo + count / 2;
    std::nth_element(order.begin() + lo, order.begin() + mid, order.begin() + hi,
                     [&](uint32_t a, uint32_t b) {
                         return (&cent_[a].x)[axis] < (&cent_[b].x)[axis];
                     });
    const int32_t c0 = buildRec(order, lo, mid);
    const int32_t c1 = buildRec(order, mid, hi);

    Node& nd = nodes_[id];   // 子节点构建可能使 vector 重分配, 此处重新取址
    for (int a = 0; a < 3; ++a) { nd.mn[a] = mn[a]; nd.mx[a] = mx[a]; }
    nd.child0 = c0;
    nd.child1 = c1;
    return id;
}

bool SceneBVH::intersect(const Vec3& origin, const Vec3& dir,
                         float max_t, BvhHit& out) const {
    if (nodes_.empty() || max_t <= 0.0f) return false;

    Vec3 inv;
    {
        const float* dv = &dir.x;
        float* ivv = &inv.x;
        for (int a = 0; a < 3; ++a)
            ivv[a] = std::fabs(dv[a]) > 1e-20f
                         ? 1.0f / dv[a]
                         : (dv[a] >= 0.0f ? 1e20f : -1e20f);
    }

    int32_t stack[96];
    int top = 0;
    stack[top++] = 0;
    bool hit = false;
    float tbest = max_t;
    uint32_t best_tri = 0;

    while (top > 0) {
        const Node& nd = nodes_[stack[--top]];
        if (!rayBox(origin, inv, tbest, nd.mn, nd.mx)) continue;
        if (nd.child0 < 0) {
            for (uint32_t i = 0; i < nd.count; ++i) {
                const Vec3* tv = &verts_[3 * (nd.first + i)];
                float t;
                if (rayTri(origin, dir, tv[0], tv[1], tv[2], t) && t < tbest) {
                    tbest = t;
                    hit = true;
                    best_tri = nd.first + i;
                }
            }
        } else if (top + 2 <= 96) {
            stack[top++] = nd.child0;
            stack[top++] = nd.child1;
        }
    }

    if (!hit) return false;
    const Vec3* tv = &verts_[3 * best_tri];
    Vec3 n = norm3(cross3(sub3(tv[1], tv[0]), sub3(tv[2], tv[0])));
    if (dot3(n, dir) > 0.0f)
        n = {-n.x, -n.y, -n.z};
    out.t = tbest;
    out.normal = n;
    out.primitive_id=primitive_ids_[best_tri];
    return true;
}

}} // namespace vsgraysim::detail
