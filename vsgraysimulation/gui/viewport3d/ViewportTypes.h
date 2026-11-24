#pragma once
/// @file ViewportTypes.h
/// @brief viewport3d 库公共类型: 帧节拍 / 鼠标射线事件 / 地形查询接口。
#ifndef VSGRAYSIM_VIEWPORT3D_VIEWPORTTYPES_H
#define VSGRAYSIM_VIEWPORT3D_VIEWPORTTYPES_H

#include <vsg/maths/vec3.h>

#include <cstdint>

namespace rockyenv { class ITerrainBridge; }
namespace vsgraysim { namespace viewport3d {

/// 每帧节拍信息 (由 Viewport3DView 帧循环产生, 分发给各特性)。
struct FrameTick {
    double   dt_sec = 0.0;       ///< 距上一帧时长 (秒)
    double   elapsed_sec = 0.0;  ///< 累计运行时长 (秒)
    uint64_t frame_index = 0;    ///< 帧序号
    bool simulation_clock = false; ///< external authoritative time (pause/step/replay)
};

/// 鼠标世界射线事件 (由宿主编辑/拾取模式反投影产生)。
struct MouseRayEvent {
    enum class Kind { Press, Drag, Release };
    Kind       kind = Kind::Press;
    vsg::dvec3 origin;           ///< 射线原点 (世界系)
    vsg::dvec3 direction;        ///< 射线方向 (单位化)
    bool       consumed = false; ///< 特性处理后置位, 阻止后续分发
};

/// 地形高度查询接口 (由 `terrain_layer` 特性注册到 ViewportContext,
/// 供参考网格贴地 / 相机限高 / 点击摆放等特性消费)。
class ITerrainQuery {
public:
    virtual ~ITerrainQuery() = default;
    virtual bool hasTerrain() const = 0;
    /// 世界坐标 (地形居中后) 处的高度 (m)。
    virtual double heightAtWorld(double x, double z) const = 0;
    virtual double cellSize() const = 0;
    virtual double extent() const = 0;
    virtual uint32_t grid() const = 0;
    /// 地形网格代数: 重建/编辑后递增, 供贴地覆盖层失效判定。
    virtual uint64_t nodeGeneration() const = 0;
    /// 高度场版本: 笔刷/撤销/加载等任意高度变化后递增,
    /// 供贴地网格/分析覆盖层内容刷新判定 (区别于网格重建)。
    virtual uint64_t heightsVersion() const = 0;
};

/// 地理(地球)查询接口 (由 `rocky_map` 特性注册到 ViewportContext)。
/// 仅在 rocky 地球底座启用时有效; 坐标为 ECEF/世界系与 WGS84 大地坐标。
class IGeoTerrainQuery {
public:
    virtual ~IGeoTerrainQuery() = default;
    /// 地球底座是否已就绪 (rocky MapNode 已挂载)。
    virtual bool geoActive() const = 0;
    /// 世界(ECEF)射线与地球地形求交, 返回命中点的 WGS84 大地坐标 (度/米)。
    virtual bool intersectRayGeo(const vsg::dvec3& origin, const vsg::dvec3& dir,
                                 double& lat_deg, double& lon_deg,
                                 double& elev_m) const = 0;
    /// 查询某经纬度处的地表椭球高 (米); 不可用返回 false。
    virtual bool groundElevation(double lat_deg, double lon_deg,
                                 double& elev_m) const = 0;
    virtual uint64_t revision() const { return 0; }
    virtual const rockyenv::ITerrainBridge* environmentTerrain() const { return nullptr; }
    virtual bool heightLocal(double, double, double&) const { return false; }
    virtual bool waterBedLocal(double x,double z,double,double& bed) const { return heightLocal(x,z,bed); }
    virtual bool hasAuthoritativeSurface() const { return false; }
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_VIEWPORTTYPES_H
