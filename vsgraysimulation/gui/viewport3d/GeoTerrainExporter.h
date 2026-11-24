#pragma once
/// @file GeoTerrainExporter.h
/// @brief 精细地形地理导出 (地形编辑软件 → rocky 可部署地形包)。
///
/// 地形编辑软件在 rocky 地球锚点 ENU 切平面上用画刷编辑局部高度场
/// (TerrainSystem, 米制)。本导出器把编辑后的高度场写为**地理配准 GeoTIFF**
/// (WGS84 / EPSG:4326, 完整锚点姿态及椭球转换后重采样):
///   - `<name>_dem.tif` : 浮点椭球高，旋转覆盖区外为 NoData，
///                        rocky GDALElevationLayer 原生加载, 参与地形求交/LOD/贴地;
///   - `<name>_img.tif` : 诊断用高程/坡度 RGBA 影像，缺数透明;
///   - `<name>.map.json`: rocky 地图 (粗地球 TMS 底图 + 上述 GDAL 精细图层叠加),
///                        场景设计/仿真软件直接加载即得"粗地球 + 精细地形"。
///
/// 输出为北向上的地理栅格，输入仍是带 heading 的局部 Y-up 高度场。
///
#ifndef VSGRAYSIM_VIEWPORT3D_GEOTERRAINEXPORTER_H
#define VSGRAYSIM_VIEWPORT3D_GEOTERRAINEXPORTER_H

#include "GeoFrame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vsgraysim { namespace viewport3d {

struct GeoTerrainExportParams {
    /// 编辑后的高度场 (n*n, 行优先 index = z*n + x, 单位米, 局部 ENU y)。
    const std::vector<double>* heights = nullptr;
    uint32_t n = 0;                 ///< 高度场每边采样数
    double   extent_m = 1000.0;     ///< 地形边长 (米), 决定地理覆盖跨度
    GeoAnchor anchor;               ///< 精细地形 footprint 中心 (lat/lon/alt)

    std::string out_dir;            ///< 输出目录
    std::string name = "detail_terrain";  ///< 文件名前缀
    bool write_imagery = true;      ///< 是否同时导有着色影像 GeoTIFF

    /// 可选粗地球 URI；留空由宿主继承底图，包内不写机器专属默认路径。
    std::string base_imagery_uri;
    std::string base_elevation_uri;
    int base_imagery_maxlevel = 3;
    int base_elevation_maxlevel = 5;
};

struct GeoTerrainExportResult {
    bool        ok = false;
    std::string dem_path;         ///< 高程 GeoTIFF (绝对路径)
    std::string imagery_path;     ///< 影像 GeoTIFF (绝对路径)
    std::string map_json_path;    ///< rocky 地图 (绝对路径)
    std::string error;
};

/// 导出精细地形地理包 (GeoTIFF + rocky .map.json)。
/// 失败时 result.ok=false 且 error 含 GDAL 错误信息。
GeoTerrainExportResult exportGeoTerrainPackage(const GeoTerrainExportParams& p);

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_GEOTERRAINEXPORTER_H
