/// @file TerrainAnalyzer.cpp
/// @brief 纯算法地形分析引擎实现。

#include "TerrainAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim { namespace gui {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline double radToDeg(double r) { return r * 180.0 / kPi; }

inline uint8_t clampU8(double v)
{
    return static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
}

/// 在两个 RGB 颜色之间按 t ∈ [0,1] 线性插值。
inline void lerpRgb(const uint8_t a[3], const uint8_t b[3], double t, uint8_t out[3])
{
    out[0] = clampU8(double(a[0]) + (double(b[0]) - double(a[0])) * t);
    out[1] = clampU8(double(a[1]) + (double(b[1]) - double(a[1])) * t);
    out[2] = clampU8(double(a[2]) + (double(b[2]) - double(a[2])) * t);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 输入校验 / 通用采样
// ---------------------------------------------------------------------------
bool TerrainAnalyzer::validField(const std::vector<double>& heights, uint32_t n)
{
    return n >= 2 && heights.size() == size_t(n) * n;
}

double TerrainAnalyzer::sampleBilinear(const std::vector<double>& heights,
                                       uint32_t n, double extent,
                                       double x, double z)
{
    if (!validField(heights, n) || extent <= 0.0) return 0.0;
    const double cell = extent / double(n - 1);
    const double gx = std::clamp(x / cell, 0.0, double(n - 1));
    const double gz = std::clamp(z / cell, 0.0, double(n - 1));
    const int ix = std::min(int(n) - 2, int(std::floor(gx)));
    const int iz = std::min(int(n) - 2, int(std::floor(gz)));
    const double fx = gx - ix, fz = gz - iz;

    const double h00 = heights[size_t(iz) * n + ix];
    const double h10 = heights[size_t(iz) * n + (ix + 1)];
    const double h01 = heights[size_t(iz + 1) * n + ix];
    const double h11 = heights[size_t(iz + 1) * n + (ix + 1)];
    return h00 * (1.0 - fx) * (1.0 - fz) + h10 * fx * (1.0 - fz) +
           h01 * (1.0 - fx) * fz + h11 * fx * fz;
}

// ---------------------------------------------------------------------------
// 高程统计
// ---------------------------------------------------------------------------
TerrainAnalyzer::ElevationStats
TerrainAnalyzer::elevationStats(const std::vector<double>& heights, uint32_t n)
{
    ElevationStats st;
    if (!validField(heights, n)) return st;
    double lo = std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    double sum = 0.0;
    for (double h : heights) {
        lo = std::min(lo, h);
        hi = std::max(hi, h);
        sum += h;
    }
    if (lo > hi) lo = hi = 0.0;
    st.min_h  = lo;
    st.max_h  = hi;
    st.mean_h = sum / double(heights.size());
    return st;
}

// ---------------------------------------------------------------------------
// 坡度 / 坡向 (中心差分, 边界单侧差分)
// ---------------------------------------------------------------------------
std::vector<double> TerrainAnalyzer::slopeMap(const std::vector<double>& heights,
                                              uint32_t n, double extent)
{
    std::vector<double> out;
    if (!validField(heights, n) || extent <= 0.0) return out;
    out.assign(size_t(n) * n, 0.0);
    const double cell = extent / double(n - 1);
    if (cell <= 0.0) return out;

    for (uint32_t j = 0; j < n; ++j) {
        const uint32_t jm = (j > 0) ? j - 1 : 0;
        const uint32_t jp = (j + 1 < n) ? j + 1 : n - 1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t im = (i > 0) ? i - 1 : 0;
            const uint32_t ip = (i + 1 < n) ? i + 1 : n - 1;
            const double dx = double(ip - im) * cell;
            const double dz = double(jp - jm) * cell;
            const double gx = (heights[size_t(j) * n + ip] -
                               heights[size_t(j) * n + im]) / dx;
            const double gz = (heights[size_t(jp) * n + i] -
                               heights[size_t(jm) * n + i]) / dz;
            out[size_t(j) * n + i] = radToDeg(std::atan(std::sqrt(gx * gx + gz * gz)));
        }
    }
    return out;
}

std::vector<double> TerrainAnalyzer::aspectMap(const std::vector<double>& heights,
                                               uint32_t n, double extent)
{
    std::vector<double> out;
    if (!validField(heights, n) || extent <= 0.0) return out;
    out.assign(size_t(n) * n, -1.0);
    const double cell = extent / double(n - 1);
    if (cell <= 0.0) return out;

    for (uint32_t j = 0; j < n; ++j) {
        const uint32_t jm = (j > 0) ? j - 1 : 0;
        const uint32_t jp = (j + 1 < n) ? j + 1 : n - 1;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t im = (i > 0) ? i - 1 : 0;
            const uint32_t ip = (i + 1 < n) ? i + 1 : n - 1;
            const double dx = double(ip - im) * cell;
            const double dz = double(jp - jm) * cell;
            const double gx = (heights[size_t(j) * n + ip] -
                               heights[size_t(j) * n + im]) / dx;
            const double gz = (heights[size_t(jp) * n + i] -
                               heights[size_t(jm) * n + i]) / dz;
            if (std::sqrt(gx * gx + gz * gz) < 1.0e-6) {
                out[size_t(j) * n + i] = -1.0;   // 平地
                continue;
            }
            // 下坡方向 = -梯度; 罗盘方位角: 0°=北(-Z), 顺时针。
            double a = radToDeg(std::atan2(-gx, gz));
            if (a < 0.0) a += 360.0;
            out[size_t(j) * n + i] = a;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 两点通视 (LOS 采样判定)
// ---------------------------------------------------------------------------
TerrainAnalyzer::LosResult
TerrainAnalyzer::lineOfSight(const std::vector<double>& heights, uint32_t n,
                             double extent,
                             double x0, double z0, double x1, double z1,
                             double eye_lift, double target_lift, int samples)
{
    LosResult res;
    res.visible = true;
    if (!validField(heights, n) || extent <= 0.0) {
        res.visible = false;
        return res;
    }
    samples = std::clamp(samples, 16, 4096);

    const double y0 = sampleBilinear(heights, n, extent, x0, z0) + eye_lift;
    const double y1 = sampleBilinear(heights, n, extent, x1, z1) + target_lift;

    // 采样容差: 抵消双线性插值与视线插值的数值噪声。
    constexpr double kEps = 0.02;
    for (int k = 1; k < samples; ++k) {
        const double t = double(k) / double(samples);
        const double x = x0 + (x1 - x0) * t;
        const double z = z0 + (z1 - z0) * t;
        const double y = y0 + (y1 - y0) * t;
        const double h = sampleBilinear(heights, n, extent, x, z);
        if (h > y + kEps) {
            res.visible   = false;
            res.blocker_x = x;
            res.blocker_z = z;
            res.blocker_h = h;
            res.blocker_t = t;
            break;
        }
    }
    return res;
}

// ---------------------------------------------------------------------------
// 两点剖面
// ---------------------------------------------------------------------------
std::vector<TerrainAnalyzer::ProfileSample>
TerrainAnalyzer::profile(const std::vector<double>& heights, uint32_t n,
                         double extent,
                         double x0, double z0, double x1, double z1, int samples)
{
    std::vector<ProfileSample> out;
    if (!validField(heights, n) || extent <= 0.0) return out;
    samples = std::clamp(samples, 2, 4096);
    out.reserve(size_t(samples) + 1);

    const double horiz = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    for (int k = 0; k <= samples; ++k) {
        const double t = double(k) / double(samples);
        ProfileSample s;
        s.dist   = horiz * t;
        s.height = sampleBilinear(heights, n, extent,
                                  x0 + (x1 - x0) * t, z0 + (z1 - z0) * t);
        out.push_back(s);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 材质分类 (按高程 / 坡度)
// ---------------------------------------------------------------------------
std::vector<TerrainAnalyzer::Material>
TerrainAnalyzer::materialMap(const std::vector<double>& heights, uint32_t n,
                             double extent, double water_level)
{
    std::vector<Material> out;
    if (!validField(heights, n) || extent <= 0.0) return out;

    const auto slope = slopeMap(heights, n, extent);
    const ElevationStats st = elevationStats(heights, n);
    const double span = std::max(st.max_h - st.min_h, 1.0e-6);

    out.resize(size_t(n) * n, Material::Grass);
    for (size_t k = 0; k < out.size(); ++k) {
        const double h = heights[k];
        if (h <= water_level) {
            out[k] = Material::Water;
        } else if (slope[k] >= 30.0) {
            out[k] = Material::Rock;
        } else if ((h - st.min_h) / span >= 0.75) {
            out[k] = Material::Snow;
        } else {
            out[k] = Material::Grass;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 颜色输出 (RGB888)
// ---------------------------------------------------------------------------
std::vector<uint8_t> TerrainAnalyzer::slopeColors(const std::vector<double>& slope_deg,
                                                  uint32_t n)
{
    std::vector<uint8_t> out;
    if (!validField(slope_deg, n)) return out;
    out.resize(size_t(n) * n * 3, 0);

    // 分段线性色带: 0° 绿 / 30° 黄 / 60° 橙 / 90° 深红。
    const uint8_t c0[3] = { 46, 204,  84};
    const uint8_t c1[3] = {244, 208,  66};
    const uint8_t c2[3] = {240, 120,  42};
    const uint8_t c3[3] = {178,  24,  24};

    for (size_t k = 0; k < size_t(n) * n; ++k) {
        const double s = std::clamp(slope_deg[k], 0.0, 90.0);
        uint8_t rgb[3];
        if (s <= 30.0)      lerpRgb(c0, c1, s / 30.0, rgb);
        else if (s <= 60.0) lerpRgb(c1, c2, (s - 30.0) / 30.0, rgb);
        else                lerpRgb(c2, c3, (s - 60.0) / 30.0, rgb);
        out[3 * k + 0] = rgb[0];
        out[3 * k + 1] = rgb[1];
        out[3 * k + 2] = rgb[2];
    }
    return out;
}

std::vector<uint8_t> TerrainAnalyzer::aspectColors(const std::vector<double>& aspect_deg,
                                                   uint32_t n)
{
    std::vector<uint8_t> out;
    if (!validField(aspect_deg, n)) return out;
    out.resize(size_t(n) * n * 3, 0);

    // 罗盘色环: N(0°)红, E(90°)绿, S(180°)蓝, W(270°)紫, 相邻方位插值。
    const uint8_t cN[3] = {232,  72,  62};
    const uint8_t cE[3] = { 86, 196,  96};
    const uint8_t cS[3] = { 70, 130, 232};
    const uint8_t cW[3] = {188,  96, 218};

    for (size_t k = 0; k < size_t(n) * n; ++k) {
        const double a = aspect_deg[k];
        uint8_t rgb[3];
        if (a < 0.0) {
            rgb[0] = 128; rgb[1] = 128; rgb[2] = 132;   // 平地灰
        } else {
            const double deg = std::clamp(a, 0.0, 360.0);
            if (deg <= 90.0)       lerpRgb(cN, cE, deg / 90.0, rgb);
            else if (deg <= 180.0) lerpRgb(cE, cS, (deg - 90.0) / 90.0, rgb);
            else if (deg <= 270.0) lerpRgb(cS, cW, (deg - 180.0) / 90.0, rgb);
            else                   lerpRgb(cW, cN, (deg - 270.0) / 90.0, rgb);
        }
        out[3 * k + 0] = rgb[0];
        out[3 * k + 1] = rgb[1];
        out[3 * k + 2] = rgb[2];
    }
    return out;
}

std::vector<uint8_t> TerrainAnalyzer::materialColors(const std::vector<Material>& materials,
                                                     uint32_t n)
{
    std::vector<uint8_t> out;
    if (n < 2 || materials.size() != size_t(n) * n) return out;
    out.resize(size_t(n) * n * 3, 0);

    // 水蓝 / 草绿 / 岩灰 / 雪白。
    static const uint8_t kPal[4][3] = {
        { 52, 120, 222},   // Water
        { 88, 168,  72},   // Grass
        {146, 140, 130},   // Rock
        {242, 246, 250},   // Snow
    };
    for (size_t k = 0; k < size_t(n) * n; ++k) {
        const int mi = std::clamp(int(materials[k]), 0, 3);
        out[3 * k + 0] = kPal[mi][0];
        out[3 * k + 1] = kPal[mi][1];
        out[3 * k + 2] = kPal[mi][2];
    }
    return out;
}

}} // namespace vsgraysim::gui
