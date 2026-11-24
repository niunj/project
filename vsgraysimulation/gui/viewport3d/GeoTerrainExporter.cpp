/// @file GeoTerrainExporter.cpp
/// @brief 精细地形地理导出实现 (GDAL C API 写 GeoTIFF + rocky .map.json)。
#include "GeoTerrainExporter.h"
#include <vsg/maths/transform.h>

#include <spdlog/spdlog.h>

#include <gdal.h>
#include <ogr_srs_api.h>
#include <cpl_conv.h>
#include <cpl_string.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace vsgraysim { namespace viewport3d {

namespace {

constexpr float kNoData = -32768.f;

inline double clampv(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// 高程 + 坡度 → RGB (沙/草/岩/雪 分层, 近似 TerrainSystem 程序纹理)。
void colorFor(double h, double slope_deg, double h_min, double h_range,
              uint8_t& r, uint8_t& g, uint8_t& b)
{
    const double t = h_range > 1e-6 ? clampv((h - h_min) / h_range, 0.0, 1.0) : 0.5;
    // 基色随高程: 近水沙黄 → 低地草绿 → 高地灰褐。
    double rr, gg, bb;
    if (t < 0.12) {                       // 岸线沙地
        rr = 0.76; gg = 0.70; bb = 0.47;
    } else if (t < 0.55) {                // 草被
        const double k = (t - 0.12) / 0.43;
        rr = 0.36 + 0.10 * k; gg = 0.55 - 0.12 * k; bb = 0.28;
    } else {                              // 高地岩褐
        const double k = (t - 0.55) / 0.45;
        rr = 0.46 + 0.28 * k; gg = 0.42 + 0.26 * k; bb = 0.38 + 0.28 * k;
    }
    // 陡坡 → 裸露岩石灰。
    const double rock = clampv((slope_deg - 28.0) / 25.0, 0.0, 1.0);
    rr = rr * (1.0 - rock) + 0.52 * rock;
    gg = gg * (1.0 - rock) + 0.50 * rock;
    bb = bb * (1.0 - rock) + 0.48 * rock;
    // 高海拔 → 雪。
    const double snow = clampv((t - 0.82) / 0.12, 0.0, 1.0);
    rr = rr * (1.0 - snow) + 0.95 * snow;
    gg = gg * (1.0 - snow) + 0.96 * snow;
    bb = bb * (1.0 - snow) + 0.98 * snow;
    r = static_cast<uint8_t>(clampv(rr, 0.0, 1.0) * 255.0);
    g = static_cast<uint8_t>(clampv(gg, 0.0, 1.0) * 255.0);
    b = static_cast<uint8_t>(clampv(bb, 0.0, 1.0) * 255.0);
}

/// 写单层浮点高程 GeoTIFF (EPSG:4326)。
bool writeDemTiff(const std::string& path, const std::vector<float>& grid,
                  uint32_t n, double lon_min, double lat_max,
                  double dlon, double dlat, std::string& err)
{
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    if (!drv) { err = "GDAL GTiff driver unavailable"; return false; }
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "PREDICTOR", "3");
    GDALDatasetH ds = GDALCreate(drv, path.c_str(), n, n, 1, GDT_Float32, opts);
    CSLDestroy(opts);
    if (!ds) { err = "GDALCreate(dem) failed"; return false; }

    double gt[6] = { lon_min, dlon, 0.0, lat_max, 0.0, -dlat };
    GDALSetGeoTransform(ds, gt);

    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(srs, 4326);
    char* wkt = nullptr;
    OSRExportToWkt(srs, &wkt);
    GDALSetProjection(ds, wkt);
    if (wkt) CPLFree(wkt);
    OSRDestroySpatialReference(srs);

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    GDALSetRasterNoDataValue(band, -32768.0);
    CPLErr e = GDALRasterIO(band, GF_Write, 0, 0, n, n,
                           const_cast<float*>(grid.data()), n, n,
                           GDT_Float32, 0, 0);
    GDALClose(ds);
    if (e != CE_None) { err = "GDALRasterIO(dem) failed"; return false; }
    return true;
}

/// 写 3 波段字节 RGB 影像 GeoTIFF (EPSG:4326)。
bool writeImageTiff(const std::string& path, const std::vector<uint8_t>& rgb,
                    uint32_t n, double lon_min, double lat_max,
                    double dlon, double dlat, std::string& err)
{
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    if (!drv) { err = "GDAL GTiff driver unavailable"; return false; }
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "PHOTOMETRIC", "RGB");
    GDALDatasetH ds = GDALCreate(drv, path.c_str(), n, n, 4, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!ds) { err = "GDALCreate(img) failed"; return false; }

    double gt[6] = { lon_min, dlon, 0.0, lat_max, 0.0, -dlat };
    GDALSetGeoTransform(ds, gt);
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(srs, 4326);
    char* wkt = nullptr;
    OSRExportToWkt(srs, &wkt);
    GDALSetProjection(ds, wkt);
    if (wkt) CPLFree(wkt);
    OSRDestroySpatialReference(srs);

    GDALSetRasterColorInterpretation(GDALGetRasterBand(ds, 4), GCI_AlphaBand);
    int bandMap[4] = { 1, 2, 3, 4 };
    CPLErr e = GDALDatasetRasterIO(ds, GF_Write, 0, 0, n, n,
                                  const_cast<uint8_t*>(rgb.data()), n, n,
                                  GDT_Byte, 4, bandMap, 4,
                                  static_cast<GSpacing>(n) * 4, 1);
    GDALClose(ds);
    if (e != CE_None) { err = "GDALDatasetRasterIO(img) failed"; return false; }
    return true;
}

} // namespace

GeoTerrainExportResult exportGeoTerrainPackage(const GeoTerrainExportParams& p)
{
    GeoTerrainExportResult res;
    if (!p.heights || p.n == 0 || p.heights->size() <
        static_cast<size_t>(p.n) * p.n) {
        res.error = "invalid heightfield (null or size mismatch)";
        return res;
    }
    if (p.out_dir.empty() || p.name.empty()) {
        res.error = "out_dir/name not set";
        return res;
    }
    std::error_code ec;
    fs::create_directories(p.out_dir, ec);

    GDALAllRegister();

    const uint32_t n = p.n;
    const auto& H = *p.heights;

    if(n<3 || !(p.extent_m>0) || !std::isfinite(p.extent_m)) {res.error="invalid terrain extent/grid";return res;}
    for(double height:H)if(!std::isfinite(height)){res.error="nonfinite terrain height";return res;}
    const double half=p.extent_m*.5,cell_m=p.extent_m/double(n-1);
    const auto model=makeEnuToWorldMatrix(p.anchor),inverse=vsg::inverse(model);
    auto sourceHeight=[&](double x,double z,double& height) {
        if(x < -half || x > half || z < -half || z > half)return false;
        const double gx=(x+half)/cell_m,gz=(z+half)/cell_m;
        const auto ix=std::min(uint32_t(gx),n-2),iz=std::min(uint32_t(gz),n-2);
        const double u=gx-ix,v=gz-iz;
        height=H[iz*n+ix]*(1-u)*(1-v)+H[iz*n+ix+1]*u*(1-v)+H[(iz+1)*n+ix]*(1-u)*v+H[(iz+1)*n+ix+1]*u*v;
        return true;
    };
    double lon_min=180,lon_max=-180,lat_min=90,lat_max=-90;
    double h_min=std::numeric_limits<double>::infinity(),h_max=-h_min;
    for(uint32_t z=0;z<n;++z)for(uint32_t x=0;x<n;++x) {
        const auto point=model*vsg::dvec4(-half+x*cell_m,H[z*n+x],-half+z*cell_m,1.);
        const auto llh=ecefToGeodetic(point.x,point.y,point.z);
        lon_min=std::min(lon_min,llh.y);lon_max=std::max(lon_max,llh.y);
        lat_min=std::min(lat_min,llh.x);lat_max=std::max(lat_max,llh.x);
        h_min=std::min(h_min,H[z*n+x]);h_max=std::max(h_max,H[z*n+x]);
    }
    if(lon_max-lon_min>180 || lat_max-lat_min<1e-12 || lon_max-lon_min<1e-12) {
        res.error="terrain crosses a pole/dateline; split the export region";return res;
    }
    const double dlat_pix=(lat_max-lat_min)/n,dlon_pix=(lon_max-lon_min)/n;
    std::vector<float> dem(size_t(n)*n,kNoData);
    std::vector<uint8_t> rgb(p.write_imagery ? size_t(n)*n*4 : 0,0);
    for(uint32_t row=0;row<n;++row)for(uint32_t col=0;col<n;++col) {
        const double latitude=lat_max-(row+.5)*dlat_pix,longitude=lon_min+(col+.5)*dlon_pix;
        const auto up=inverse*vsg::dvec4(geoUp(latitude,longitude),0.);
        double height=p.anchor.alt_m;vsg::dvec3 local;double terrain=0;bool valid=false;
        for(int iteration=0;iteration<12;++iteration) {
            const auto point=geodeticToECEF(latitude,longitude,height);
            const auto value=inverse*vsg::dvec4(point,1.);local={value.x,value.y,value.z};
            if(!sourceHeight(local.x,local.z,terrain))break;
            const double correction=(terrain-local.y)/up.y;
            height+=correction;
            if(std::abs(correction)<1e-5){valid=true;break;}
        }
        if(!valid)continue;
        const auto index=size_t(row)*n+col;dem[index]=float(height);
        if(p.write_imagery) {
            double left=terrain,right=terrain,top=terrain,bottom=terrain;
            sourceHeight(std::max(-half,local.x-cell_m),local.z,left);
            sourceHeight(std::min(half,local.x+cell_m),local.z,right);
            sourceHeight(local.x,std::max(-half,local.z-cell_m),top);
            sourceHeight(local.x,std::min(half,local.z+cell_m),bottom);
            const double slope=std::atan(std::hypot(right-left,bottom-top)/(2*cell_m))*180/3.141592653589793;
            colorFor(terrain,slope,h_min,h_max-h_min,rgb[index*4],rgb[index*4+1],rgb[index*4+2]);rgb[index*4+3]=255;
        }
    }

    // ---- 写 GeoTIFF ----
    res.dem_path     = (fs::path(p.out_dir) / (p.name + "_dem.tif")).string();
    res.imagery_path = (fs::path(p.out_dir) / (p.name + "_img.tif")).string();

    if (!writeDemTiff(res.dem_path, dem, n, lon_min, lat_max, dlon_pix, dlat_pix, res.error))
        return res;
    if (p.write_imagery) {
        if (!writeImageTiff(res.imagery_path, rgb, n, lon_min, lat_max,
                            dlon_pix, dlat_pix, res.error))
            return res;
    } else {
        res.imagery_path.clear();
    }

    // ---- rocky .map.json (粗地球底图 + GDAL 精细图层) ----
    nlohmann::json j;
    j["name"] = p.name;
    j["layers"] = nlohmann::json::array();
    j["schema"]="vsgraysim.earth.map";j["version"]=1;
    j["geo_anchor"]={{"lat",p.anchor.lat_deg},{"lon",p.anchor.lon_deg},{"alt",p.anchor.alt_m},{"heading_deg",p.anchor.heading_deg}};
    const double margin=std::min(half*.25,cell_m*2.);
    j["earth"]={{"vertical_datum","ellipsoidal"},{"strict",true},{"grid_size",std::min(n,257u)},
        {"coverage",{{"min_x",-half+margin},{"max_x",half-margin},{"min_z",-half+margin},{"max_z",half-margin}}}};
    if(!p.base_imagery_uri.empty())j["layers"].push_back({{"name","Earth Imagery"},{"type","TMSImage"},{"uri",p.base_imagery_uri},{"profile","global-geodetic"},{"maxLevel",p.base_imagery_maxlevel}});
    if(!p.base_elevation_uri.empty())j["layers"].push_back({{"name","Earth Elevation"},{"type","TMSElevation"},{"uri",p.base_elevation_uri},{"profile","global-geodetic"},{"maxLevel",p.base_elevation_maxlevel}});
    if (p.write_imagery) {
        j["layers"].push_back({
            {"name", p.name + " Imagery"},
            {"type", "GDALImage"},
            {"uri", fs::path(res.imagery_path).filename().string()}
        });
    }
    j["layers"].push_back({
        {"name", p.name + " Elevation"},
        {"type", "GDALElevation"},
        {"uri", fs::path(res.dem_path).filename().string()}
    });

    res.map_json_path = (fs::path(p.out_dir) / (p.name + ".map.json")).string();
    std::ofstream ofs(res.map_json_path);
    if (!ofs) { res.error = "cannot write map.json"; return res; }
    ofs << j.dump(2) << '\n';
    ofs.close();

    res.ok = true;
    spdlog::info("GeoTerrainExporter: package '{}' exported ({}x{}, {:.0f} m, "
                 "center lat={:.5f} lon={:.5f}): dem='{}', img='{}', map='{}'",
                 p.name, n, n, p.extent_m, p.anchor.lat_deg, p.anchor.lon_deg,
                 res.dem_path, res.imagery_path, res.map_json_path);
    return res;
}

}} // namespace vsgraysim::viewport3d
