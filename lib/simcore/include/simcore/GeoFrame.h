#pragma once
/// @file GeoFrame.h
/// @brief 地理坐标框架 (rocky 地球底座的配套数学)。
///
/// 两级坐标策略:
///   规范坐标 = WGS84 大地坐标 (纬度/经度/椭球高), 用于场景文件与跨软件传递;
///   作业坐标 = 锚点切平面 ENU 局部米制 (x=东, y=上, z=... ), 现有物理/光追/
///             特效全部在该局部系中原样运行。
///
/// 本头文件只做标准 WGS84 大地→ECEF→ENU 解析变换, 不依赖 rocky, 因此在未启用
/// rocky 的构建中也可编译 (RockyMapFeature 负责真正的地球渲染)。
///
/// 局部系约定 (与项目既有场景一致): Y 轴朝上, X/Z 为地面。为保持右手系且
/// Y=Up, 局部轴映射为  X→East, Y→Up, Z→(East×Up)=-North (即局部 +Z 指南)。
/// 场景本身无固有的地理北方向, 该旋转不影响模型/特效的正确性。
#ifndef SIMCORE_GEOFRAME_H
#define SIMCORE_GEOFRAME_H

#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace simcore { namespace geo {

/// 地理锚点: 局部作业坐标系在地球上的位置与朝向。
struct GeoAnchor {
    double lat_deg     = 25.078;   ///< 纬度 (度), 默认台北附近
    double lon_deg     = 121.772;  ///< 经度 (度)
    double alt_m       = 0.0;      ///< 椭球高 (米)
    double heading_deg = 0.0;      ///< grid heading clockwise from true north (degrees)

    static constexpr double kEarthA  = 6378137.0;                 ///< WGS84 长半轴 (m)
    static constexpr double kEarthE2 = 0.0066943799901413165;      ///< 第一偏心率平方
};

/// 度 → 弧度。
inline double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }

/// WGS84 大地坐标 (纬度/经度/高度, 度/米) → 地心地固 ECEF (米)。
inline vsg::dvec3 geodeticToECEF(double lat_deg, double lon_deg, double alt_m)
{
    if (!std::isfinite(lat_deg) || std::abs(lat_deg) > 90.0 ||
        !std::isfinite(lon_deg) || !std::isfinite(alt_m))
        throw std::invalid_argument("invalid WGS84 geodetic coordinate");
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(std::remainder(lon_deg, 360.0));
    const double sinLat = std::sin(lat), cosLat = std::cos(lat);
    const double sinLon = std::sin(lon), cosLon = std::cos(lon);
    const double N = GeoAnchor::kEarthA /
                     std::sqrt(1.0 - GeoAnchor::kEarthE2 * sinLat * sinLat);
    return vsg::dvec3(
        (N + alt_m) * cosLat * cosLon,
        (N + alt_m) * cosLat * sinLon,
        (N * (1.0 - GeoAnchor::kEarthE2) + alt_m) * sinLat);
}

/// 锚点处的东向单位矢量 (ECEF)。
inline vsg::dvec3 geoEast(double lat_deg, double lon_deg)
{
    const double lon = deg2rad(lon_deg);
    return vsg::dvec3(-std::sin(lon), std::cos(lon), 0.0);
}

/// 锚点处的当地铅垂(上)单位矢量 (ECEF)。
inline vsg::dvec3 geoUp(double lat_deg, double lon_deg)
{
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(lon_deg);
    return vsg::dvec3(std::cos(lat) * std::cos(lon),
                      std::cos(lat) * std::sin(lon),
                      std::sin(lat));
}

/// 构造 局部 ENU 作业系 → ECEF/世界 的刚体变换矩阵。
/// 列向量: c0=East(局部X), c1=Up(局部Y), c2=East×Up(局部Z), c3=锚点ECEF。
inline vsg::dmat4 makeEnuToWorldMatrix(const GeoAnchor& a)
{
    const vsg::dvec3 E = geoEast(a.lat_deg, a.lon_deg);
    const vsg::dvec3 U = geoUp(a.lat_deg, a.lon_deg);
    const vsg::dvec3 Z = vsg::cross(E, U);   // = -North, 保证右手系
    const vsg::dvec3 P = geodeticToECEF(a.lat_deg, a.lon_deg, a.alt_m);
    const double h = deg2rad(a.heading_deg);
    if (!std::isfinite(h)) throw std::invalid_argument("invalid grid heading");
    const auto Xh = E * std::cos(h) + Z * std::sin(h);
    const auto Zh = Z * std::cos(h) - E * std::sin(h);
    return vsg::dmat4(vsg::dvec4(Xh, 0.0),
                      vsg::dvec4(U, 0.0),
                      vsg::dvec4(Zh, 0.0),
                      vsg::dvec4(P, 1.0));
}

inline vsg::dvec3 localToECEF(const GeoAnchor& anchor, const vsg::dvec3& local) {
    const auto value = makeEnuToWorldMatrix(anchor) * vsg::dvec4(local, 1.0);
    return {value.x, value.y, value.z};
}

inline vsg::dvec3 ecefToLocal(const GeoAnchor& anchor, const vsg::dvec3& world) {
    const auto matrix = makeEnuToWorldMatrix(anchor);
    const auto delta = world - vsg::dvec3(matrix[3].x, matrix[3].y, matrix[3].z);
    return {
        vsg::dot(delta, vsg::dvec3(matrix[0].x, matrix[0].y, matrix[0].z)),
        vsg::dot(delta, vsg::dvec3(matrix[1].x, matrix[1].y, matrix[1].z)),
        vsg::dot(delta, vsg::dvec3(matrix[2].x, matrix[2].y, matrix[2].z))};
}

/// ECEF (米) → WGS84 大地坐标, 返回 (纬度度, 经度度, 椭球高米)。
inline vsg::dvec3 ecefToGeodetic(double x, double y, double z)
{
    const double a   = GeoAnchor::kEarthA;
    const double e2  = GeoAnchor::kEarthE2;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::hypot(std::hypot(x, y), z) < 1.0)
        throw std::invalid_argument("undefined or nonfinite ECEF coordinate");
    const double lon = std::atan2(y, x);
    const double p = std::hypot(x, y);
    double lat = std::atan2(z, p * (1.0 - e2));
    for (int i = 0; i < 12; ++i) {
        const double sl = std::sin(lat);
        const double N = a / std::sqrt(1.0 - e2 * sl * sl);
        const double next = std::atan2(z + e2 * N * sl, p);
        if (std::abs(next - lat) < 1e-15) { lat = next; break; }
        lat = next;
    }
    // Projection onto the ellipsoid normal stays well conditioned at poles.
    const double sl = std::sin(lat);
    const double alt = p * std::cos(lat) + z * sl - a * std::sqrt(1.0 - e2 * sl * sl);
    return vsg::dvec3(lat * 180.0 / 3.14159265358979323846,
                      lon * 180.0 / 3.14159265358979323846,
                      alt);
}

/// 世界(ECEF)射线与 WGS84 椭球求交 (最近命中)。
/// @param origin 射线原点 (ECEF, 米)
/// @param dir    射线方向 (单位化)
/// @param hit    输出命中点 (ECEF, 米)
/// @return 是否命中 (射线在椭球外且指向前方)。
inline bool rayIntersectEllipsoid(const vsg::dvec3& origin, const vsg::dvec3& dir,
                                  vsg::dvec3& hit)
{
    const double a = GeoAnchor::kEarthA;
    const double f = 1.0 / 298.257223563;
    const double b = a * (1.0 - f);
    const vsg::dvec3 r(a, a, b);
    // 缩放到单位球: P' = P ./ r
    const vsg::dvec3 O(origin.x / r.x, origin.y / r.y, origin.z / r.z);
    const vsg::dvec3 D(dir.x / r.x,    dir.y / r.y,    dir.z / r.z);
    const double A = vsg::dot(D, D);
    const double B = 2.0 * vsg::dot(O, D);
    const double C = vsg::dot(O, O) - 1.0;
    if (!(A > 0.0) || !std::isfinite(A) || !std::isfinite(B) || !std::isfinite(C)) return false;
    double disc = B * B - 4.0 * A * C;
    const double tolerance = 16 * std::numeric_limits<double>::epsilon() *
                             (B * B + std::abs(4.0 * A * C));
    if (disc < -tolerance) return false;
    disc = std::max(disc, 0.0);
    const double q = -0.5 * (B + std::copysign(std::sqrt(disc), B));
    double t0 = q / A;
    double t1 = q != 0.0 ? C / q : t0;
    if (t0 > t1) std::swap(t0, t1);
    const double t = t0 >= 0.0 ? t0 : t1;
    if (t < 0.0) return false;
    hit = origin + dir * t;
    return true;
}

}} // namespace simcore::geo
#endif // SIMCORE_GEOFRAME_H
