#include "vsgraysim/earth/EarthDataService.h"
#include "VoxelTerrain.h"
#include "TiledVoxelTerrain.h"
#if VSGRAYSIM_EARTH_RASTER
#include <gdal.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <list>
#include <numeric>

namespace vsgraysim::earth {
namespace {
namespace fs=std::filesystem;
using json=nlohmann::json;
constexpr double pi=3.14159265358979323846;
struct Fingerprint {
    uintmax_t size=0; fs::file_time_type time;
    explicit Fingerprint(const std::string& path):size(fs::file_size(path)),time(fs::last_write_time(path)){}
    bool unchanged(const std::string& path) const {return fs::file_size(path)==size && fs::last_write_time(path)==time;}
};
json readJson(const std::string& path) {
    std::ifstream in(path); if(!in)throw std::runtime_error("cannot open Earth data document: "+path);
    json j;in>>j;if(!j.is_object())throw std::invalid_argument("Earth data document must be an object");return j;
}
bool finite(const vsg::dvec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
uint64_t hashBytes(uint64_t h,const void* p,std::size_t n){const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}return h;}
void replaceAll(std::string& s,const std::string& from,const std::string& to){std::size_t p=0;while((p=s.find(from,p))!=std::string::npos){s.replace(p,from.size(),to);p+=to.size();}}
json canonicalMaterials(const json& config) {
    // These are the effective PhysParamManager material fields. Normalize
    // omitted/explicit defaults and integer/float JSON representations before
    // both hashing and simulation consumption. Unused editor keys do not change
    // the physical surface identity.
    json result={
        {"vis_params",{{"brdf_type","diffuse"},{"albedo",.2f},{"laser_scatter",0.f},{"laser_absorb",0.f}}},
        {"lwir_params",{{"emissivity",.9f},{"emissivity_swir",.9f},{"base_temp",293.15f},{"thermal_conduct",0.f},{"temp_tex_id",uint32_t(0)}}},
        {"radar_x_params",{{"rcs_lut_id",uint32_t(0)},{"dielectric_real",1.f},{"dielectric_imag",0.f},{"surface_rough",.5f}}},
        {"sar_c_params",{{"sigma0_db",-10.f},{"polar_couple",0.f},{"doppler_corr",0.f}}}};
    for(auto& category:result.items()) {
        const auto supplied=config.value(category.key(),json::object());
        if(!supplied.is_object())throw std::invalid_argument("Earth material parameters must be objects");
        for(auto& parameter:category.value().items())if(supplied.contains(parameter.key())) {
            const auto& value=supplied[parameter.key()];
            if(parameter.value().is_string()) {
                if(!value.is_string())throw std::invalid_argument("invalid Earth BRDF type");
                const auto brdf=value.get<std::string>();parameter.value()=(brdf=="roughmetal"||brdf=="mirror"||brdf=="rubber")?brdf:"diffuse";
            } else if(parameter.value().is_number_unsigned()) {
                if(!value.is_number_integer()||value.get<double>()<0||value.get<double>()>UINT32_MAX)throw std::invalid_argument("invalid Earth material resource index");
                parameter.value()=value.get<uint32_t>();
            } else {
                if(!value.is_number()||!std::isfinite(value.get<double>())||!std::isfinite(value.get<float>()))throw std::invalid_argument("invalid Earth material number");
                const float number=value.get<float>();parameter.value()=number==0?0.f:number;
            }
        }
        if(category.key()=="lwir_params"&&!supplied.contains("emissivity_swir"))category.value()["emissivity_swir"]=category.value()["emissivity"];
    }
    for(auto key:{"albedo","laser_scatter","laser_absorb"})result["vis_params"][key]=std::clamp(result["vis_params"][key].get<float>(),0.f,1.f);
    for(auto key:{"emissivity","emissivity_swir"})result["lwir_params"][key]=std::clamp(result["lwir_params"][key].get<float>(),0.f,1.f);
    const float temperature=result["lwir_params"]["base_temp"].get<float>();if(temperature<100||temperature>1000)result["lwir_params"]["base_temp"]=293.15f;
    result["radar_x_params"]["dielectric_real"]=std::clamp(result["radar_x_params"]["dielectric_real"].get<float>(),1.f,100.f);
    result["radar_x_params"]["dielectric_imag"]=std::clamp(result["radar_x_params"]["dielectric_imag"].get<float>(),0.f,100.f);
    return result;
}
#if VSGRAYSIM_EARTH_RASTER
struct Raster {
    struct TileReference { double west=0,north=0,span=0; bool nodes=true; };
    GDALDatasetH dataset=nullptr;GDALRasterBandH band=nullptr;
    std::unique_ptr<OGRCoordinateTransformation,decltype(&OCTDestroyCoordinateTransformation)> transform{nullptr,OCTDestroyCoordinateTransformation};
    std::array<double,6> inverse{};int width=0,height=0;double nodata=0,pixel_x=0,pixel_y=0;bool has_nodata=false,geographic=false;
    Fingerprint fingerprint;std::string path;
    struct ColorBlock {int width=0,height=0,count=0;std::vector<uint8_t> pixels,valid;uint64_t used=0;};
    mutable std::map<std::pair<int,int>,ColorBlock> colorBlocks;
    mutable uint64_t colorUse=0;
    const ColorBlock& colorBlock(int ix,int iy)const {
        constexpr int side=128;
        const auto key=std::make_pair(ix/side,iy/side);
        if(auto it=colorBlocks.find(key);it!=colorBlocks.end()){it->second.used=++colorUse;return it->second;}
        // A bounded, loader-local cache removes millions of tiny GDAL calls.
        // The extra row/column retains exact 2x2 interpolation at block edges.
        if(colorBlocks.size()>=128) {
            auto oldest=colorBlocks.begin();for(auto it=colorBlocks.begin();it!=colorBlocks.end();++it)if(it->second.used<oldest->second.used)oldest=it;
            colorBlocks.erase(oldest);
        }
        ColorBlock b;const int left=key.first*side,top=key.second*side;
        b.width=std::min(side+1,width-left);b.height=std::min(side+1,height-top);b.count=GDALGetRasterCount(dataset);b.used=++colorUse;
        if(b.count!=1&&b.count!=3&&b.count!=4)throw std::runtime_error("Earth imagery must be Byte gray, RGB or RGBA: "+path);
        const size_t area=size_t(b.width)*b.height;b.pixels.resize(area*b.count);b.valid.assign(area,255);
        for(int c=0;c<b.count;++c) {
            const auto channel=GDALGetRasterBand(dataset,c+1);
            if(GDALGetRasterDataType(channel)!=GDT_Byte||GDALGetRasterColorInterpretation(channel)==GCI_PaletteIndex)
                throw std::runtime_error("Earth imagery requires unpacked Byte color bands: "+path);
            auto* pixels=b.pixels.data()+area*c;
            if(GDALRasterIO(channel,GF_Read,left,top,b.width,b.height,pixels,b.width,b.height,GDT_Byte,0,0)!=CE_None)throw std::runtime_error("Earth imagery block read failed: "+path);
            int hasNo=0;const double no=GDALGetRasterNoDataValue(channel,&hasNo);
            if(GDALGetMaskFlags(channel)!=GMF_ALL_VALID) {
                std::vector<uint8_t> mask(area);
                if(GDALRasterIO(GDALGetMaskBand(channel),GF_Read,left,top,b.width,b.height,mask.data(),b.width,b.height,GDT_Byte,0,0)!=CE_None)throw std::runtime_error("Earth imagery mask read failed: "+path);
                for(size_t i=0;i<area;++i)if(!mask[i])b.valid[i]=0;
            }
            for(size_t i=0;i<area;++i)if((hasNo&&pixels[i]==no)||(c==3&&pixels[i]!=255))b.valid[i]=0;
        }
        return colorBlocks.emplace(key,std::move(b)).first->second;
    }
    explicit Raster(const std::string& p,const TileReference* tile=nullptr):fingerprint(p),path(p) {
        dataset=GDALOpenEx(p.c_str(),GDAL_OF_RASTER|GDAL_OF_READONLY,nullptr,nullptr,nullptr);
        if(!dataset)throw std::runtime_error("cannot open elevation raster: "+p);
        try {
            width=GDALGetRasterXSize(dataset);height=GDALGetRasterYSize(dataset);band=GDALGetRasterBand(dataset,1);
            double gt[6];if(!band||width<2||height<2)throw std::runtime_error("elevation raster has invalid dimensions: "+p);
            OGRSpatialReference src,target;src.SetWellKnownGeogCS("WGS84");src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if(tile) {
                // Rocky elevation tiles are grids: the first/last samples lie
                // on the tile extent (Image::read_bilinear uses u*(width-1)).
                // Express those grid nodes as pixel centers for the GDAL
                // inverse below, preserving TIFF's north-to-south row order.
                // Only a declared TMS profile/key supplies this placement;
                // never infer a CRS for an unreferenced GDALElevation raster.
                gt[1]=tile->span/(tile->nodes?width-1:width);gt[2]=0;gt[0]=tile->west-(tile->nodes?.5*gt[1]:0);
                gt[4]=0;gt[5]=-tile->span/(tile->nodes?height-1:height);gt[3]=tile->north-(tile->nodes?.5*gt[5]:0);
                target.SetWellKnownGeogCS("WGS84");
            } else {
                if(GDALGetGeoTransform(dataset,gt)!=CE_None)throw std::runtime_error("elevation raster has no usable georeferencing: "+p);
                const auto* projection=GDALGetProjectionRef(dataset);
                if(!projection||!*projection||target.SetFromUserInput(projection)!=OGRERR_NONE)throw std::runtime_error("elevation CRS is missing: "+p);
            }
            if(!GDALInvGeoTransform(gt,inverse.data()))throw std::runtime_error("noninvertible elevation georeferencing: "+p);
            target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            geographic=target.IsGeographic();const double units=geographic?111320.:target.GetLinearUnits();
            pixel_x=std::hypot(gt[1],gt[4])*units;pixel_y=std::hypot(gt[2],gt[5])*units;
            transform.reset(OGRCreateCoordinateTransformation(&src,&target));if(!transform)throw std::runtime_error("cannot transform elevation CRS: "+p);
            int valid=0;nodata=GDALGetRasterNoDataValue(band,&valid);has_nodata=valid!=0;
        } catch(...){GDALClose(dataset);dataset=nullptr;throw;}
    }
    ~Raster(){if(dataset)GDALClose(dataset);}
    bool sample(double lon,double lat,double& elevation,double& resolution)const {
        double px=lon,py=lat,pz=0;if(!transform->Transform(1,&px,&py,&pz))return false;
        // GDAL transforms reference pixel corners; interpolation references centers.
        const double x=inverse[0]+inverse[1]*px+inverse[2]*py-.5;
        const double y=inverse[3]+inverse[4]*px+inverse[5]*py-.5;
        if(x<-.500001||y<-.500001||x>width-.5+.000001||y>height-.5+.000001)return false;
        const int ix=std::clamp(int(std::floor(x)),0,width-2),iy=std::clamp(int(std::floor(y)),0,height-2);
        const double u=std::clamp(x-ix,0.,1.),v=std::clamp(y-iy,0.,1.);double values[4];
        if(GDALRasterIO(band,GF_Read,ix,iy,2,2,values,2,2,GDT_Float64,0,0)!=CE_None)return false;
        for(double n:values)if(!std::isfinite(n)||(has_nodata&&(std::isnan(nodata)?std::isnan(n):n==nodata)))return false;
        // The mask is part of validity, not a zero-elevation fallback.
        auto mask=GDALGetMaskBand(band);if(mask&&GDALGetMaskFlags(band)!=GMF_ALL_VALID){unsigned char valid[4];if(GDALRasterIO(mask,GF_Read,ix,iy,2,2,valid,2,2,GDT_Byte,0,0)!=CE_None)return false;for(auto n:valid)if(!n)return false;}
        int has_scale=0,has_offset=0;const double scale=GDALGetRasterScale(band,&has_scale),offset=GDALGetRasterOffset(band,&has_offset);
        elevation=((1-u)*(1-v)*values[0]+u*(1-v)*values[1]+(1-u)*v*values[2]+u*v*values[3])*(has_scale?scale:1)+(has_offset?offset:0);
        // Estimate source sampling in meters by transforming adjacent pixels.
        resolution=std::max(pixel_x*(geographic?std::max(.01,std::cos(lat*pi/180.)):1),pixel_y);
        return std::isfinite(elevation);
    }
    bool color(double lon,double lat,std::array<uint8_t,4>& rgba,double& resolution)const {
        double px=lon,py=lat,pz=0;if(!transform->Transform(1,&px,&py,&pz))return false;
        const double x=inverse[0]+inverse[1]*px+inverse[2]*py-.5,y=inverse[3]+inverse[4]*px+inverse[5]*py-.5;
        if(x<-.500001||y<-.500001||x>width-.5+.000001||y>height-.5+.000001)return false;
        const int ix=std::clamp(int(std::floor(x)),0,width-2),iy=std::clamp(int(std::floor(y)),0,height-2);
        const double u=std::clamp(x-ix,0.,1.),v=std::clamp(y-iy,0.,1.);
        const auto& block=colorBlock(ix,iy);const size_t area=size_t(block.width)*block.height;
        static const auto linear=[] {std::array<double,256> table{};for(size_t i=0;i<table.size();++i){const double n=i/255.;table[i]=n<=.04045?n/12.92:std::pow((n+.055)/1.055,2.4);}return table;}();
        auto encoded=[](double n){return uint8_t(std::clamp(std::lround(255*(n<=.0031308?12.92*n:1.055*std::pow(n,1/2.4)-.055)),0L,255L));};
        rgba={0,0,0,255};
        for(int c=0;c<3;++c) {
            const auto* pixels=block.pixels.data()+area*(block.count==1?0:c);
            double value=0;
            for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
                const double weight=(i?u:1-u)*(j?v:1-v);if(weight<=1e-12)continue;
                const size_t k=size_t(iy%128+j)*block.width+ix%128+i;if(!block.valid[k])return false;
                value+=weight*linear[pixels[k]];
            }
            rgba[c]=encoded(value);
        }
        resolution=std::max(pixel_x*(geographic?std::max(.01,std::cos(lat*pi/180.)):1),pixel_y);return true;
    }
};
struct ElevationSource {std::string type,uri,profile="global-geodetic";int min_level=0,max_level=5;};
struct RasterSet {
    std::vector<ElevationSource> layers;
    std::map<std::string,std::unique_ptr<Raster>> rasters;
    std::size_t limit=64;
    Raster* raster(const std::string& path,const Raster::TileReference* tile=nullptr){auto found=rasters.find(path);if(found!=rasters.end())return found->second.get();if(!fs::is_regular_file(path))return nullptr;if(rasters.size()>=limit)throw std::runtime_error("Earth ROI exceeded the 64 raster preload budget; reduce coverage or LOD");auto value=std::make_unique<Raster>(path,tile);auto* p=value.get();rasters.emplace(path,std::move(value));return p;}
    bool sample(double lon,double lat,double& h,double& resolution,bool& coarse){
        for(auto it=layers.rbegin();it!=layers.rend();++it){
            if(it->type=="GDALElevation"){if(auto* r=raster(it->uri);r&&r->sample(lon,lat,h,resolution)){coarse=it!=layers.rbegin();return true;}continue;}
            if(it->profile!="global-geodetic")throw std::runtime_error("Earth TMSElevation currently requires global-geodetic profile");
            for(int level=it->max_level;level>=it->min_level;--level){
                const uint32_t ny=uint32_t(1)<<level,nx=ny*2;
                const auto x=std::min(nx-1,uint32_t(std::clamp((lon+180.)/360.,0.,1.)*nx));
                const auto y=std::min(ny-1,uint32_t(std::clamp((90.-lat)/180.,0.,1.)*ny));
                auto path=it->uri;replaceAll(path,"{z}",std::to_string(level));replaceAll(path,"{x}",std::to_string(x));replaceAll(path,"{-y}",std::to_string(ny-y-1));replaceAll(path,"{y}",std::to_string(y));
                const double span=180./ny;const Raster::TileReference placement{-180.+x*span,90.-y*span,span};
                if(auto* r=raster(path,&placement);r&&r->sample(lon,lat,h,resolution)){coarse=it!=layers.rbegin()||level<it->max_level;return true;}
            }
        }return false;
    }
};
void loadImagery(EarthSurfaceSnapshot& surface,const json& map,const json& config,const std::string& document,json& identities) {
    RasterSet images;
    for(const auto& layer:map.at("layers")) {
        const auto type=layer.value("type",std::string());if(type!="GDALImage"&&type!="TMSImage")continue;
        ElevationSource source;source.type=type;source.uri=EarthDataService::resolveUri(layer.at("uri").get<std::string>(),document);
        if(type=="TMSImage") {
            if(layer.value("profile",std::string())!="global-geodetic")throw std::invalid_argument("Earth TMSImage requires explicit global-geodetic profile");
            for(const char* k:{"minLevel","maxLevel"})if(layer.contains(k)&&!layer[k].is_number_integer())throw std::invalid_argument("Earth imagery LOD must be integer");
            source.min_level=layer.value("minLevel",0);source.max_level=layer.value("maxLevel",5);
            if(source.min_level<0||source.max_level<source.min_level||source.max_level>22)throw std::invalid_argument("invalid Earth imagery LOD range");
            if(source.uri.find("{z}")==std::string::npos)source.uri+="/{z}/{x}/{-y}.png";
            if(source.uri.find("{x}")==std::string::npos||((source.uri.find("{y}")==std::string::npos)==(source.uri.find("{-y}")==std::string::npos)))throw std::invalid_argument("Earth imagery URI requires z/x and one y convention");
        }
        identities.push_back({{"type",type},{"uri",source.uri},{"minLevel",source.min_level},{"maxLevel",source.max_level}});images.layers.push_back(std::move(source));
    }
    if(images.layers.empty())return;
    const auto size=config.value("imagery_size",256);
    if(!config.value("imagery_size",json(256)).is_number_integer()||size<2||size>4096)throw std::invalid_argument("Earth imagery_size must be integer in [2,4096]");
    surface.imagery_width=surface.imagery_height=size;surface.imagery_rgba.resize(size_t(size)*size*4);
    for(int z=0;z<size;++z)for(int x=0;x<size;++x) {
        const double ex=surface.min_x+(x+.5)*(surface.max_x-surface.min_x)/size,ez=surface.min_z+(z+.5)*(surface.max_z-surface.min_z)/size;
        const auto sample=surface.sampleLocal(ex,ez);if(sample.status==SurfaceStatus::NoData)continue;
        const auto ecef=simcore::geo::localToECEF(surface.anchor,{ex,sample.height,ez});const auto llh=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z);
        std::array<uint8_t,4> color{};double resolution=0;bool found=false;
        for(auto it=images.layers.rbegin();it!=images.layers.rend()&&!found;++it) {
            if(it->type=="GDALImage") {if(auto* raster=images.raster(it->uri))found=raster->color(llh.y,llh.x,color,resolution);}
            else for(int level=it->max_level;level>=it->min_level&&!found;--level) {
                const uint32_t ny=uint32_t(1)<<level,nx=ny*2;const double span=180./ny;
                const auto tx=std::min(nx-1,uint32_t(std::clamp((llh.y+180.)/360.,0.,1.)*nx));
                const auto ty=std::min(ny-1,uint32_t(std::clamp((90.-llh.x)/180.,0.,1.)*ny));
                auto path=it->uri;replaceAll(path,"{z}",std::to_string(level));replaceAll(path,"{x}",std::to_string(tx));replaceAll(path,"{-y}",std::to_string(ny-ty-1));replaceAll(path,"{y}",std::to_string(ty));
                const Raster::TileReference placement{-180.+tx*span,90.-ty*span,span,false};
                if(auto* raster=images.raster(path,&placement))found=raster->color(llh.y,llh.x,color,resolution);
            }
        }
        if(!found)throw std::runtime_error("Earth imagery is missing or transparent within the active elevation working set");
        std::copy(color.begin(),color.end(),surface.imagery_rgba.begin()+4*(size_t(z)*size+x));surface.imagery_spacing_m=std::max(surface.imagery_spacing_m,resolution);
    }
    for(const auto& [path,raster]:images.rasters)if(!raster->fingerprint.unchanged(path))throw std::runtime_error("Earth imagery changed while loading");
    for(const auto& p:surface.vertices())surface.imagery_uv.emplace_back(float((p.x-surface.min_x)/(surface.max_x-surface.min_x)),float((p.z-surface.min_z)/(surface.max_z-surface.min_z)));
}
#endif
void triangleNormal(const vsg::dvec3& a,const vsg::dvec3& b,const vsg::dvec3& c,vsg::dvec3& n){n=vsg::cross(b-a,c-a);const double size=vsg::length(n);n=size>1e-15?n/size:vsg::dvec3(0,1,0);}
}

std::string EarthDataService::resolveUri(const std::string& ref,const std::string& document) {
    if(ref.empty())return {};
    if(ref.find("://")!=std::string::npos)throw std::invalid_argument("Earth P1 preload requires local data: "+ref);
    std::string resolved=ref;
    if(resolved.rfind("${",0)==0) {
        const auto end=resolved.find('}');const auto name=end==std::string::npos?std::string():resolved.substr(2,end-2);
        if(name!="VSGRAYSIM_EARTH_DATA_ROOT"&&name!="VSGRAYSIM_TAIPEI_DATA_ROOT")throw std::invalid_argument("unsupported Earth data environment reference");
        const auto* value=std::getenv(name.c_str());if(!value||!*value||!fs::path(value).is_absolute())throw std::runtime_error(name+" must name an existing absolute data directory");
        resolved=std::string(value)+resolved.substr(end+1);
    }
    fs::path path(resolved);if(path.is_relative())path=fs::absolute(fs::path(document)).parent_path()/path;
    return path.lexically_normal().string();
}
std::string EarthDataService::resolveMapPath(const json& scenario,const std::string& scenario_path) {
    const auto earth=scenario.value("earth",json::object());if(!earth.is_object())throw std::invalid_argument("earth must be an object");
    std::string reference=earth.value("map",std::string());
    if(reference.empty())reference=scenario.value("terrain_ref",std::string());
    return resolveUri(reference,scenario_path);
}
EarthSurfaceSnapshotPtr EarthDataService::loadScenario(const json& scenario,const std::string& path) {
    const auto map=resolveMapPath(scenario,path);if(map.empty()){if(scenario.contains("earth"))throw std::invalid_argument("earth configuration requires map or terrain_ref");return {};}
    simcore::geo::GeoAnchor anchor;const auto a=scenario.value("geo_anchor",json::object());
    anchor.lat_deg=a.value("lat",anchor.lat_deg);anchor.lon_deg=a.value("lon",anchor.lon_deg);anchor.alt_m=a.value("alt",a.value("alt_m",0.));anchor.heading_deg=a.value("heading_deg",0.);
    return loadMap(map,anchor,scenario.value("earth",json::object()));
}
EarthSurfaceSnapshotPtr EarthDataService::loadMap(const std::string& reference,const simcore::geo::GeoAnchor& anchor,const json& config) {
    if(!config.is_object())throw std::invalid_argument("earth configuration must be an object");
    simcore::geo::makeEnuToWorldMatrix(anchor);
    const auto map_path=fs::absolute(reference).lexically_normal().string();const Fingerprint original(map_path);
    auto surface=std::make_shared<EarthSurfaceSnapshot>();surface->anchor=anchor;surface->source=map_path;
    bool strict=config.value("strict",true);
    surface->strict=strict;
    json map;std::string heightmap;json effective_sources=json::array();json effective_config=config;
    if(fs::path(map_path).extension()==".ter")heightmap=map_path;
    else {map=readJson(map_path);if(map.value("format",std::string())=="vsgterrain-project")heightmap=resolveUri(map.at("heightmap").get<std::string>(),map_path);}
    const auto format=map.is_object() ? map.value("format",std::string()) : std::string();
    const bool tiled=format=="voxel-geodetic-v1" || format=="voxel-geodetic-volume-v1";
    const bool voxel = tiled || (map.is_object() && map.value("format", std::string()) == "voxel-terrain-v1");
    if (map.is_object()) effective_config = map.value("earth", json::object());
    effective_config.update(config);
    const auto backend = effective_config.value("backend", std::string(voxel ? "voxel" : "rocky"));
    if (backend != (voxel ? "voxel" : "rocky"))
        throw std::invalid_argument("Earth backend does not match terrain package: " + backend);
    if(format=="voxel-geodetic-v1" || (!voxel && heightmap.empty())) {
#if VSGRAYSIM_EARTH_RASTER
        static std::once_flag registered;
        std::call_once(registered,[]{GDALAllRegister();});
#else
        throw std::invalid_argument("raster input requires the GDAL adapter; open a native voxel package");
#endif
    }
    if (tiled) {
        surface = TiledVoxelTerrain::load(map,effective_config,anchor,map_path);
        surface->anchor=anchor;
        surface->source=map_path;
        effective_sources.push_back({{"type","GeodeticVoxel"},{"tile",effective_config.at("tile")}});
        if(format=="voxel-geodetic-volume-v1") {
            const auto volume=std::static_pointer_cast<TiledVoxelTerrain>(surface);
            effective_sources.back()["format"]=format;
            if(surface->orbital) effective_sources.back()["orbital_dataset"]=surface->orbital->dataset;
            effective_sources.back()["occupancy"]=volume->occupancySignature();
        }
    } else if (voxel) {
        surface = VoxelTerrain::load(map, effective_config);
        surface->anchor = anchor;
        surface->source = map_path;
        effective_sources.push_back({{"type", "VoxelTerrainV1"},
                                    {"origin_m", map.at("origin_m").get<std::array<double,3>>()},
                                    {"dimensions", map.at("dimensions")}});
    } else if(!heightmap.empty()) {
        effective_sources.push_back({{"type","LocalHeightmap"},{"uri",fs::weakly_canonical(heightmap).string()}});
        std::ifstream in(heightmap,std::ios::binary);char magic[8];uint32_t n=0,wt=0,tt=0;double extent=0,scale=0,seed=0,water=0;
        in.read(magic,8);in.read(reinterpret_cast<char*>(&n),4);in.read(reinterpret_cast<char*>(&extent),8);in.read(reinterpret_cast<char*>(&scale),8);in.read(reinterpret_cast<char*>(&seed),8);in.read(reinterpret_cast<char*>(&water),8);in.read(reinterpret_cast<char*>(&wt),4);in.read(reinterpret_cast<char*>(&tt),4);
        if(!in||std::memcmp(magic,"VSGTER1\n",8)!=0||n<2||n>1025||!std::isfinite(extent)||extent<=0)throw std::runtime_error("invalid or oversized legacy Earth heightmap: "+heightmap);
        surface->grid_size=n;surface->min_x=surface->min_z=-extent*.5;surface->max_x=surface->max_z=extent*.5;
        surface->resolution_m=extent/(n-1);surface->vertices_.reserve(std::size_t(n)*n);surface->valid_.assign(std::size_t(n)*n,1);
        std::vector<double> heights(std::size_t(n)*n);in.read(reinterpret_cast<char*>(heights.data()),heights.size()*sizeof(double));if(!in)throw std::runtime_error("truncated Earth heightmap");
        for(uint32_t z=0;z<n;++z)for(uint32_t x=0;x<n;++x){const auto h=heights[std::size_t(z)*n+x];if(!std::isfinite(h))throw std::runtime_error("nonfinite Earth height");surface->vertices_.push_back({surface->min_x+x*surface->resolution_m,h,surface->min_z+z*surface->resolution_m});}
        surface->valid_samples=heights.size();surface->provenance_assumed=true;
    } else {
#if VSGRAYSIM_EARTH_RASTER
        auto meta=map.value("earth",json::object());if(!meta.is_object())throw std::invalid_argument("map earth metadata must be an object");
        meta.update(config);strict=meta.value("strict",true);surface->strict=strict;
        effective_config=meta;
        const auto datum=meta.value("vertical_datum",map.value("vertical_datum",std::string()));
        surface->provenance_assumed=datum.empty()||datum=="unknown"||meta.value("provenance_assumed",false);
        if(strict&&datum.empty()&&!meta.value("provenance_assumed",false))throw std::invalid_argument("strict Earth data requires a declared vertical datum or an explicit provenance_assumed reference package");
        if((datum=="unknown"&&strict)||(!datum.empty()&&datum!="unknown"&&datum!="ellipsoidal"&&datum!="EPSG:4979"&&datum!="WGS84_ELLIPSOID"))throw std::invalid_argument("unsupported Earth vertical datum; convert data to ellipsoidal height or declare an explicit correction");
        const auto coverage=meta.value("coverage",json::object());
        surface->min_x=coverage.value("min_x",-5000.);surface->max_x=coverage.value("max_x",5000.);surface->min_z=coverage.value("min_z",-5000.);surface->max_z=coverage.value("max_z",5000.);
        if(meta.contains("grid_size")&&(!meta["grid_size"].is_number_integer()||meta["grid_size"].get<double>()<2||meta["grid_size"].get<double>()>1025))throw std::invalid_argument("Earth grid_size must be an integer in [2,1025]");
        const auto n=meta.value("grid_size",65u);surface->grid_size=n;
        if(n<2||n>1025||!std::isfinite(surface->min_x)||!std::isfinite(surface->max_x)||!std::isfinite(surface->min_z)||!std::isfinite(surface->max_z)||surface->max_x<=surface->min_x||surface->max_z<=surface->min_z||surface->max_x-surface->min_x>200000||surface->max_z-surface->min_z>200000)throw std::invalid_argument("invalid Earth ROI/grid; maximum 200 km and 1025 samples per side");
        RasterSet sources;
        if(!map.contains("layers")||!map["layers"].is_array())throw std::invalid_argument("Earth map requires layers array");
        for(const auto& layer:map["layers"]){
            if(!layer.is_object())throw std::invalid_argument("Earth layer must be an object");
            ElevationSource source;source.type=layer.value("type",std::string());if(source.type!="GDALElevation"&&source.type!="TMSElevation")continue;
            if(source.type=="TMSElevation"&&(!layer.contains("profile")||layer["profile"]!="global-geodetic"))throw std::invalid_argument("Earth TMSElevation requires an explicit global-geodetic profile");
            source.uri=resolveUri(layer.at("uri").get<std::string>(),map_path);source.profile=layer.value("profile",std::string("global-geodetic"));source.min_level=layer.value("minLevel",0);source.max_level=layer.value("maxLevel",5);
            for(const char* key:{"minLevel","maxLevel"})if(layer.contains(key)&&!layer[key].is_number_integer())throw std::invalid_argument("Earth LOD must be an integer");
            if(source.min_level<0||source.max_level<source.min_level||source.max_level>22)throw std::invalid_argument("invalid Earth TMS LOD range");
            if(source.type=="TMSElevation"&&source.uri.find("{z}")==std::string::npos)source.uri+="/{z}/{x}/{-y}.tif";
            if(source.type=="TMSElevation"&&(source.uri.find("{x}")==std::string::npos||(source.uri.find("{y}")==std::string::npos&&source.uri.find("{-y}")==std::string::npos)))throw std::invalid_argument("Earth TMS URI must identify z/x/y tile coordinates");
            json identity={{"type",source.type},{"uri",fs::weakly_canonical(source.uri).string()}};
            if(source.type=="TMSElevation")identity.update({{"profile",source.profile},{"minLevel",source.min_level},{"maxLevel",source.max_level}});
            effective_sources.push_back(std::move(identity));
            sources.layers.push_back(std::move(source));
        }
        if(sources.layers.empty())throw std::runtime_error("Earth map has no elevation source");
        const double dx=(surface->max_x-surface->min_x)/(n-1),dz=(surface->max_z-surface->min_z)/(n-1);surface->resolution_m=std::max(dx,dz);
        surface->vertices_.reserve(std::size_t(n)*n);surface->valid_.reserve(std::size_t(n)*n);
        for(uint32_t z=0;z<n;++z)for(uint32_t x=0;x<n;++x){
            const double ex=surface->min_x+x*dx,ez=surface->min_z+z*dz;double local_y=0;bool valid=true,coarse=false;
            for(int iteration=0;iteration<5;++iteration){
                const auto ecef=simcore::geo::localToECEF(anchor,{ex,local_y,ez});const auto llh=simcore::geo::ecefToGeodetic(ecef.x,ecef.y,ecef.z);
                double h=0,resolution=0;if(!sources.sample(llh.y,llh.x,h,resolution,coarse)){valid=false;break;}
                surface->dem_spacing_m=std::max(surface->dem_spacing_m,resolution);
                surface->resolution_m=std::max(surface->resolution_m,resolution);
                const auto local=simcore::geo::ecefToLocal(anchor,simcore::geo::geodeticToECEF(llh.x,llh.y,h));
                const double change=std::abs(local.y-local_y);local_y=local.y;if(change<1e-7)break;
            }
            surface->vertices_.push_back({ex,valid?local_y:0,ez});surface->valid_.push_back(valid?(coarse?2:1):0);if(valid)surface->valid_samples++;
        }
        for(const auto& [path,raster]:sources.rasters)if(!raster->fingerprint.unchanged(path))throw std::runtime_error("Earth elevation changed while loading; retry from a stable dataset");
        if(!surface->valid_samples || (strict&&surface->valid_samples!=surface->vertices_.size()))throw std::runtime_error("Earth coverage incomplete: "+std::to_string(surface->valid_samples)+" / "+std::to_string(surface->vertices_.size())+" valid samples");
#endif
    }
    if (!voxel) {
        const auto n=surface->grid_size;
        auto emit=[&](uint32_t a,uint32_t b,uint32_t c){if(surface->valid_[a]&&surface->valid_[b]&&surface->valid_[c])surface->indices_.insert(surface->indices_.end(),{a,b,c});};
        for(uint32_t z=0;z+1<n;++z)for(uint32_t x=0;x+1<n;++x){const auto i=z*n+x;emit(i,i+n,i+1);emit(i+1,i+n,i+n+1);}
        if(surface->indices_.empty())throw std::runtime_error("Earth ROI has no valid triangles");
        surface->min_height=std::numeric_limits<double>::infinity();surface->max_height=-surface->min_height;
        for(std::size_t i=0;i<surface->vertices_.size();++i)if(surface->valid_[i]){surface->min_height=std::min(surface->min_height,surface->vertices_[i].y);surface->max_height=std::max(surface->max_height,surface->vertices_[i].y);}
    }
#if VSGRAYSIM_EARTH_RASTER
    if(!voxel&&heightmap.empty())loadImagery(*surface,map,effective_config,map_path,effective_sources);
#endif
    if(!original.unchanged(map_path))throw std::runtime_error("Earth document changed while loading");
    surface->material_config=canonicalMaterials(effective_config);
    surface->declaration = effective_config;
    surface->declaration["map"] = map_path;
    surface->declaration["backend"] = voxel ? "voxel" : "rocky";
    surface->has_map_layers = !voxel && heightmap.empty();
    // Hash semantic input and published data, not the raw scene/map syntax.
    // GUI normalization, map references, descriptive names and editor hashes
    // must not make equal physical snapshots look like different versions.
    uint64_t h=1469598103934665603ull;const auto settings=json{{"sources",effective_sources},{"materials",surface->material_config},{"strict",surface->strict}}.dump();
    h=hashBytes(h,settings.data(),settings.size());h=hashBytes(h,&surface->grid_size,sizeof(surface->grid_size));
    for(double coordinate:{anchor.lat_deg,anchor.lon_deg,anchor.alt_m,anchor.heading_deg})h=hashBytes(h,&coordinate,sizeof(coordinate));
    for(const auto& v:surface->vertices_)for(double coordinate:{v.x,v.y,v.z})h=hashBytes(h,&coordinate,sizeof(coordinate));
    h=hashBytes(h,surface->indices_.data(),surface->indices_.size()*sizeof(uint32_t));h=hashBytes(h,surface->valid_.data(),surface->valid_.size());
    if (tiled || !surface->imagery_rgba.empty()) {
        h=hashBytes(h,surface->imagery_rgba.data(),surface->imagery_rgba.size());
        h=hashBytes(h,&surface->dem_spacing_m,sizeof(double));
        h=hashBytes(h,&surface->imagery_spacing_m,sizeof(double));
        h=hashBytes(h,&surface->imagery_width,sizeof(surface->imagery_width));
        h=hashBytes(h,&surface->imagery_height,sizeof(surface->imagery_height));
        for(auto uv:surface->imagery_uv) for(float c:{uv.x,uv.y}) h=hashBytes(h,&c,sizeof(c));
    }
    h=hashBytes(h,&surface->resolution_m,sizeof(double));h=hashBytes(h,&surface->provenance_assumed,sizeof(bool));surface->revision=h?h:1;
    return surface;
}

SurfaceSample EarthSurfaceSnapshot::sampleLocal(double x,double z) const {
    SurfaceSample result;result.revision=revision;result.resolution_m=resolution_m;
    if(!std::isfinite(x)||!std::isfinite(z)||x<min_x||x>max_x||z<min_z||z>max_z||grid_size<2)return result;
    const double gx=(x-min_x)/(max_x-min_x)*(grid_size-1),gz=(z-min_z)/(max_z-min_z)*(grid_size-1);
    const auto ix=std::min(grid_size-2,uint32_t(std::floor(gx))),iz=std::min(grid_size-2,uint32_t(std::floor(gz)));const double u=gx-ix,v=gz-iz;
    const uint32_t a=iz*grid_size+ix,b=a+1,c=a+grid_size,d=c+1;
    if(u+v<=1){if(!valid_[a]||!valid_[b]||!valid_[c])return result;result.height=(1-u-v)*vertices_[a].y+u*vertices_[b].y+v*vertices_[c].y;triangleNormal(vertices_[a],vertices_[c],vertices_[b],result.normal);}
    else {if(!valid_[b]||!valid_[c]||!valid_[d])return result;result.height=(1-v)*vertices_[b].y+(1-u)*vertices_[c].y+(u+v-1)*vertices_[d].y;triangleNormal(vertices_[b],vertices_[c],vertices_[d],result.normal);}
    const bool coarse=u+v<=1 ? (valid_[a]>1||valid_[b]>1||valid_[c]>1):(valid_[b]>1||valid_[c]>1||valid_[d]>1);
    result.status=provenance_assumed||coarse?SurfaceStatus::Coarse:SurfaceStatus::Ready;return result;
}
namespace {
// Immutable snapshots share this bounded acceleration cache. Indices still
// address the exact display/sensor triangles; no collision geometry is fitted.
struct SurfaceRayIndex {
    struct Node {vsg::dvec3 low,high;uint32_t first=0,count=0,right=0;};
    std::vector<Node> nodes;std::vector<uint32_t> triangles;
    explicit SurfaceRayIndex(const EarthSurfaceSnapshot& s) {
        const auto& v=s.vertices();const auto& ix=s.indices();triangles.resize(ix.size()/3);std::iota(triangles.begin(),triangles.end(),0u);
        auto centre=[&](uint32_t t,int a){return (v[ix[3*t]][a]+v[ix[3*t+1]][a]+v[ix[3*t+2]][a])/3;};
        auto build=[&](auto&& self,uint32_t first,uint32_t count)->uint32_t {
            const uint32_t at=nodes.size();nodes.push_back({});Node node;node.first=first;node.count=count;
            const double inf=std::numeric_limits<double>::infinity();node.low={inf,inf,inf};node.high=-node.low;
            for(uint32_t i=first;i<first+count;++i)for(int j=0;j<3;++j)for(int a=0;a<3;++a){const auto n=v[ix[3*triangles[i]+j]][a];node.low[a]=std::min(node.low[a],n);node.high[a]=std::max(node.high[a],n);}
            if(count>8) {
                const auto span=node.high-node.low;int axis=0;for(int a=1;a<3;++a)if(span[a]>span[axis])axis=a;
                const uint32_t half=count/2;
                std::nth_element(triangles.begin()+first,triangles.begin()+first+half,triangles.begin()+first+count,[&](auto a,auto b){const auto x=centre(a,axis),y=centre(b,axis);return x!=y?x<y:a<b;});
                node.count=0;self(self,first,half);node.right=self(self,first+half,count-half);
            }
            nodes[at]=node;return at;
        };
        nodes.reserve(triangles.size()/2+1);if(!triangles.empty())build(build,0,uint32_t(triangles.size()));
    }
    size_t bytes()const{return nodes.capacity()*sizeof(Node)+triangles.capacity()*sizeof(uint32_t);}
};
std::shared_ptr<const SurfaceRayIndex> surfaceRayIndex(const EarthSurfaceSnapshot& surface,const std::shared_ptr<const uint8_t>& identity,uint64_t generation) {
    struct Entry {std::shared_ptr<const uint8_t> identity;uint64_t generation,revision;const void* vertices;const void* indices;size_t vertexCount,indexCount;std::shared_ptr<const SurfaceRayIndex> index;};
    static std::mutex mutex;static std::list<Entry> entries;static size_t bytes=0;
    auto matches=[&](const Entry& e){return e.identity==identity&&e.generation==generation&&e.revision==surface.revision&&e.vertices==surface.vertices().data()&&e.indices==surface.indices().data()&&e.vertexCount==surface.vertices().size()&&e.indexCount==surface.indices().size();};
    auto find=[&]()->std::shared_ptr<const SurfaceRayIndex>{for(auto it=entries.begin();it!=entries.end();++it)if(matches(*it)){auto found=it->index;entries.splice(entries.begin(),entries,it);return found;}return {};};
    {std::lock_guard lock(mutex);if(auto found=find())return found;}
    auto index=std::make_shared<SurfaceRayIndex>(surface);
    std::lock_guard lock(mutex);if(auto found=find())return found;
    while(!entries.empty()&&(entries.size()>=16||bytes+index->bytes()>128ULL*1024*1024)){bytes-=entries.back().index->bytes();entries.pop_back();}
    entries.push_front({identity,generation,surface.revision,surface.vertices().data(),surface.indices().data(),surface.vertices().size(),surface.indices().size(),index});bytes+=index->bytes();return index;
}
}
bool EarthSurfaceSnapshot::intersectLocal(const vsg::dvec3& origin,const vsg::dvec3& direction,double max_distance,vsg::dvec3& hit,vsg::dvec3& normal)const {
    if(!finite(origin)||!finite(direction)||!std::isfinite(max_distance)||max_distance<=0||vsg::length(direction)<1e-12)return false;
    if(indices_.empty())return false;
    const auto index=surfaceRayIndex(*this,rayIdentity_,rayGeneration_);const auto ray=direction/vsg::length(direction);double nearest=max_distance;bool found=false;uint32_t best=0;
    auto box=[&](const SurfaceRayIndex::Node& n,double& entry){entry=0;double exit=nearest;const double epsilon=1e-9*(1+nearest);for(int a=0;a<3;++a){if(std::abs(ray[a])<1e-20){if(origin[a]<n.low[a]-epsilon||origin[a]>n.high[a]+epsilon)return false;continue;}double low=(n.low[a]-origin[a])/ray[a],high=(n.high[a]-origin[a])/ray[a];if(low>high)std::swap(low,high);entry=std::max(entry,low);exit=std::min(exit,high);if(entry>exit+epsilon)return false;}return true;};
    std::array<uint32_t,64> stack{};size_t size=1;
    while(size) {
        const uint32_t at=stack[--size];const auto& node=index->nodes[at];double entry;if(!box(node,entry))continue;
        if(!node.count){double a,b;const bool left=box(index->nodes[at+1],a),right=box(index->nodes[node.right],b);if(left&&right){stack[size++]=a<b?node.right:at+1;stack[size++]=a<b?at+1:node.right;}else if(left)stack[size++]=at+1;else if(right)stack[size++]=node.right;continue;}
        for(uint32_t j=node.first;j<node.first+node.count;++j){const auto triangle=index->triangles[j];const size_t i=size_t(triangle)*3;const auto& a=vertices_[indices_[i]];const auto& b=vertices_[indices_[i+1]];const auto& c=vertices_[indices_[i+2]];const auto e1=b-a,e2=c-a,p=vsg::cross(ray,e2);const double determinant=vsg::dot(e1,p);if(std::abs(determinant)<1e-12)continue;const double inverse=1/determinant;const auto t=origin-a;const double u=vsg::dot(t,p)*inverse;if(u<0||u>1)continue;const auto q=vsg::cross(t,e1);const double v=vsg::dot(ray,q)*inverse;if(v<0||u+v>1)continue;const double distance=vsg::dot(e2,q)*inverse;if(distance<0||distance>nearest||(found&&distance==nearest&&triangle<best))continue;nearest=distance;best=triangle;hit=origin+ray*distance;triangleNormal(a,b,c,normal);found=true;}
    }
    return found;
}
} // namespace vsgraysim::earth
