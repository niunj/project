#include "RockyGlobeBackend.h"
#if VSGRAYSIM_WITH_ROCKY
#include <vsgraysim/earth/EarthDataService.h>
#include "ViewportContext.h"
#include "VsgQtWidget.h"
#include <rocky_env/RuntimePaths.h>

#include <rocky/vsg/VSGContext.h>
#include <rocky/vsg/MapNode.h>
#include <rocky/TMSImageLayer.h>
#include <rocky/TMSElevationLayer.h>
#include <rocky/GDALImageLayer.h>
#include <rocky/GDALElevationLayer.h>
#include <rocky/Profile.h>
#include <rocky/GeoPoint.h>
#include <rocky/SRS.h>
#include <rocky/Layer.h>
#include <rocky/vsg/terrain/TerrainSettings.h>
#include <vsg/utils/LineSegmentIntersector.h>

#include <vsg/app/Camera.h>
#include <vsg/nodes/Group.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace vsgraysim { namespace viewport3d {

// ---------------------------------------------------------------------------
// 默认运行期路径 (可用 configure 参数覆盖)
// ---------------------------------------------------------------------------
namespace {
constexpr const char* kDefaultRockyFilePath = rockyenv::runtime::rocky_files;
constexpr const char* kDefaultProjData      = rockyenv::runtime::proj_data;
constexpr const char* kDefaultGdalData      = rockyenv::runtime::gdal_data;
constexpr const char* kDefaultImageryBase   =
    "/home/mte/EarthData/World.earth.tms_repo/worldTexture_tiff";
constexpr const char* kDefaultElevationBase =
    "/home/mte/EarthData/World.earth.tms_repo/worldHeight";

void setEnvIfUnset(const char* name, const char* value)
{
    if (!value || !*value) return;
    if (!std::getenv(name)) ::setenv(name, value, 1);
}

std::string tmsUri(const std::string& base)
{
    if (base.empty()) return {};
    if (base.find("{z}") != std::string::npos) return base;  // 已是完整模板
    return base + "/{z}/{x}/{-y}.tif";
}

/// 按 rocky 地图 JSON 的单个 layer 描述创建并加入图层 (代码方式, 不依赖 rocky
/// 自身的 JSON 反序列化类型别名)。支持 TMS 粗地球与 GDAL 精细 GeoTIFF 图层。
rocky::Layer::Ptr layerFromJson(const nlohmann::json& l, const std::string& document = {})
{
    if (!l.is_object()) throw std::invalid_argument("map layer must be an object");
    const std::string type = l.value("type", std::string());
    const std::string reference = l.value("uri", std::string());
    if (reference.empty()) throw std::invalid_argument("map layer URI is empty");
    const std::string uri = document.empty() ? reference : earth::EarthDataService::resolveUri(reference, document);
    const std::string profile = l.value("profile", std::string("global-geodetic"));
    rocky::Layer::Ptr result;
    if (type == "TMSImage") {
        auto layer = rocky::TMSImageLayer::create(); layer->uri=tmsUri(uri);
        layer->profile=rocky::Profile(profile); layer->maxLevel=l.value("maxLevel",3); result=layer;
    } else if (type == "TMSElevation") {
        auto layer = rocky::TMSElevationLayer::create(); layer->uri=tmsUri(uri);
        layer->profile=rocky::Profile(profile); layer->maxLevel=l.value("maxLevel",5); result=layer;
    }
#ifdef ROCKY_HAS_GDAL
    else if (type == "GDALImage") { auto layer=rocky::GDALImageLayer::create(); layer->uri=uri; result=layer; }
    else if (type == "GDALElevation") { auto layer=rocky::GDALElevationLayer::create(); layer->uri=uri; result=layer; }
#endif
    else throw std::invalid_argument("unsupported map layer type: "+type);
    result->name=l.value("name",type);
    return result;
}
} // namespace
void RockyGlobeBackend::configureEnvironment(const nlohmann::json& params_)
{
    const auto earthRoot=params_.value("earth_data_root",std::string("/home/mte/EarthData/World.earth.tms_repo"));
    if(std::filesystem::is_directory(earthRoot))setEnvIfUnset("VSGRAYSIM_EARTH_DATA_ROOT",earthRoot.c_str());
    const auto taipeiRoot=params_.value("taipei_data_root",std::string("/home/mte/EarthData/Taipei80km"));
    if(std::filesystem::is_directory(taipeiRoot))setEnvIfUnset("VSGRAYSIM_TAIPEI_DATA_ROOT",taipeiRoot.c_str());
    setEnvIfUnset("ROCKY_FILE_PATH",
                  params_.value("rocky_file_path", kDefaultRockyFilePath).c_str());
    setEnvIfUnset("PROJ_DATA", params_.value("proj_data", kDefaultProjData).c_str());
    setEnvIfUnset("PROJ_LIB",  params_.value("proj_data", kDefaultProjData).c_str());
    setEnvIfUnset("GDAL_DATA", params_.value("gdal_data", kDefaultGdalData).c_str());
}

void RockyGlobeBackend::attachContext(ViewportContext& ctx)
{
    auto* viewer = ctx.viewer();
    if (!viewer) {

        return;
    }

    // ---- 1. rocky 运行时上下文 (绑定到视口现有 viewer) ----
    // VSGContextFactory 需要 vsg::ref_ptr<vsg::Viewer>; 用裸 viewer 构造一个
    // 临时 ref_ptr (引用计数 +1, 出作用域 -1 平衡), rocky 内部自持其引用。
    vsg::ref_ptr<vsg::Viewer> viewer_ref(viewer);
    auto singleton = rocky::VSGContextFactory::create(viewer_ref);
    rocky_context_ = singleton.get();
    if (!rocky_context_) {

        rocky_singleton_.reset();
        return;
    }
    rocky_singleton_ = std::make_shared<rocky::VSGContextSingleton>(std::move(singleton));
    // The shared Qt view owns the extended VDS before first compilation.
    // Register the same instance so Rocky can populate its descriptor data.
    if(auto* widget=ctx.widget())if(auto* view=widget->view()) {
        auto* state=rocky::viewDependentState(view);
        if(!state)throw std::runtime_error("Rocky terrain requires ViewDependentStateEx");
        rocky_context_->sharedRenderData->viewDependentState[view->viewID]=state;
    }

    // Initial compilation belongs to the widget after all features attach.
    // A device-only CompileManager Context has no View/VDS/render pass and
    // cannot compile Rocky tile descriptor layouts correctly.

}

void RockyGlobeBackend::ensureMap(const nlohmann::json& params_)
{
    if (map_node_) return;
    if (!rocky_context_) throw std::runtime_error("Rocky renderer is unavailable");
    auto mapNode = rocky::MapNode::create(rocky_context_);
    if (!mapNode) {
        throw std::runtime_error("Rocky MapNode creation failed");
    }

    const std::string profile = params_.value("profile", std::string("global-geodetic"));

    // 无 map_json (或加载失败/未产出图层) → 内置离线粗地球 TMS 底图兜底。
    if (mapNode->map->layers().empty()) {
        const auto* earthRoot=std::getenv("VSGRAYSIM_EARTH_DATA_ROOT");
        const std::string imgUri = tmsUri(params_.value(
            "imagery_uri", earthRoot ? std::string(earthRoot)+"/worldTexture_tiff" : std::string(kDefaultImageryBase)));
        if (!imgUri.empty()) {
            auto layer = rocky::TMSImageLayer::create();
            layer->uri     = imgUri;
            layer->profile = rocky::Profile(profile);
            layer->name    = "Earth Imagery";
            layer->maxLevel = params_.value("imagery_maxlevel", 3);
            mapNode->map->add(layer);
        }
        const std::string demUri = tmsUri(params_.value(
            "elevation_uri", earthRoot ? std::string(earthRoot)+"/worldHeight" : std::string(kDefaultElevationBase)));
        if (!demUri.empty()) {
            auto layer = rocky::TMSElevationLayer::create();
            layer->uri     = demUri;
            layer->profile = rocky::Profile(profile);
            layer->name    = "Earth Elevation";
            layer->maxLevel = params_.value("elevation_maxlevel", 5);
            mapNode->map->add(layer);
        }
    }

#ifdef ROCKY_HAS_GDAL
    // 可选: 直接追加精细 GeoTIFF 图层 (不经 map.json 时使用)。
    {
        const auto* taipei=std::getenv("VSGRAYSIM_TAIPEI_DATA_ROOT");
        const std::string dImg = params_.value("detail_imagery_uri", taipei?std::string(taipei)+"/taipei-sentinel2-20240214-rgb10.tif":std::string());
        const std::string dDem = params_.value("detail_elevation_uri", taipei?std::string(taipei)+"/taipei-dsm30-ellipsoid.tif":std::string());
        if (!dImg.empty()) {
            auto layer = rocky::GDALImageLayer::create();
            layer->uri = dImg;
            layer->name = "Detail Imagery";
            mapNode->map->add(layer);
        }
        if (!dDem.empty()) {
            auto layer = rocky::GDALElevationLayer::create();
            layer->uri = dDem;
            layer->name = "Detail Elevation";
            mapNode->map->add(layer);
        }
    }
#endif

    mapNode->terrainSettings().pixelError=params_.value("pixel_error",4.0f);
    mapNode->terrainSettings().tileCacheSize=params_.value("tile_cache_size",256u);
    mapNode->terrainSettings().concurrency=params_.value("loading_threads",4u);
    mapNode->terrainSettings().lighting=params_.value("lighting",true);
    mapNode->terrainSettings().castShadows=params_.value("cast_shadows",true);
    base_layers_=mapNode->map->layers();
    map_node_ = mapNode;
}

void RockyGlobeBackend::selectMap(const std::string& map_json_path)
{
    auto* map = dynamic_cast<rocky::MapNode*>(map_node_.get());
    if (!map) throw std::logic_error("Rocky map is not attached");
    if (map_json_path.empty()) {
        map->map->setLayers(base_layers_);
    } else {
        const auto absolute=std::filesystem::absolute(map_json_path).lexically_normal().string();
        std::ifstream input(absolute);nlohmann::json document;input>>document;
        if(!document.is_object() || !document.contains("layers") || !document["layers"].is_array())
            throw std::invalid_argument("map requires a layer array");
        rocky::Map::Layers candidate;
        bool hasBase=false;
        for(const auto& spec:document["layers"]) {
            const auto type=spec.value("type",std::string());
            hasBase=hasBase || type=="TMSImage" || type=="TMSElevation";
            auto layer=layerFromJson(spec,absolute);
            const auto opened=layer->open(rocky_context_->io);
            if(!opened.ok())throw std::runtime_error("map layer failed to open: "+spec.value("uri",std::string()));
            candidate.push_back(std::move(layer));
        }
        if(candidate.empty())throw std::invalid_argument("map has no supported layers");
        if(!hasBase)candidate.insert(candidate.begin(),base_layers_.begin(),base_layers_.end());
        // Replace the previous package once all its layers have opened. Repeated
        // handoff therefore cannot accumulate duplicate imagery/DEM layers.
        map->map->setLayers(std::move(candidate));
    }
    ++map_revision_;
}

bool RockyGlobeBackend::intersect(const vsg::dvec3& origin,const vsg::dvec3& direction,vsg::dvec3& hit) const
{
    if (!map_node_) return false;
    auto intersector=vsg::LineSegmentIntersector::create(origin,origin+direction*1e8);
    map_node_->accept(*intersector);
    if(intersector->intersections.empty())return false;
    const auto nearest=std::min_element(intersector->intersections.begin(),intersector->intersections.end(),
        [](const auto& x,const auto& y){return x->ratio<y->ratio;});
    hit=(*nearest)->worldIntersection;
    return true;
}

bool RockyGlobeBackend::elevation(double lat_deg,double lon_deg,double& elev_m) const
{
    auto* map=dynamic_cast<rocky::MapNode*>(map_node_.get());
    if(!map || !map->terrainNode)return false;
    const auto result=map->terrainNode->intersect(rocky::GeoPoint(rocky::SRS::WGS84,lon_deg,lat_deg,0.0));
    if(!result.ok())return false;
    elev_m=result.value().point.transform(rocky::SRS::WGS84).z;return true;
}

}}

#else
#include <stdexcept>
namespace vsgraysim::viewport3d {
void RockyGlobeBackend::configureEnvironment(const nlohmann::json&) {}
void RockyGlobeBackend::attachContext(ViewportContext&) {
    throw std::runtime_error("This build requires a voxel Earth package; the legacy Rocky backend is disabled.");
}
void RockyGlobeBackend::ensureMap(const nlohmann::json&) {throw std::logic_error("Rocky backend disabled");}
void RockyGlobeBackend::selectMap(const std::string&) {throw std::logic_error("Rocky backend disabled");}
bool RockyGlobeBackend::intersect(const vsg::dvec3&,const vsg::dvec3&,vsg::dvec3&) const {return false;}
bool RockyGlobeBackend::elevation(double,double,double&) const {return false;}
}
#endif
