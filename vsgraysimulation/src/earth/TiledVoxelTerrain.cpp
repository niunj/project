#include "TiledVoxelTerrain.h"
#include <vsgraysim/earth/EarthDataService.h>
#if VSGRAYSIM_EARTH_RASTER
#include <gdal.h>
#endif
#include <vsg/maths/transform.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <list>
#include <mutex>
#include <fstream>
#include <stdexcept>
#if defined(__linux__)
#include <sys/stat.h>
#endif

namespace vsgraysim::earth {
namespace {
using json=nlohmann::json;
namespace fs=std::filesystem;
using Key=std::array<int,3>;
constexpr double inf=std::numeric_limits<double>::infinity();
int integer(const json& j,int lo,int hi,const char* name) {
    if(!j.is_number_integer() || j.get<double>()<lo || j.get<double>()>hi)
        throw std::invalid_argument(std::string("invalid tiled terrain ")+name);
    return j.get<int>();
}
void replace(std::string& s,const std::string& token,int n) {
    auto p=s.find(token);
    if(p!=s.npos) s.replace(p,token.size(),std::to_string(n));
}
uint64_t digest(std::string_view bytes) {
    uint64_t value=14695981039346656037ULL;
    for(unsigned char byte:bytes) {value^=byte;value*=1099511628211ULL;}
    return value;
}
std::string tileName(const Key& k) {
    return std::to_string(k[0])+"/"+std::to_string(k[1])+"/"+std::to_string(k[2]);
}
struct Source {
    std::string uri;
    int level;
    explicit Source(const json& j,const std::string& path):
        uri(EarthDataService::resolveUri(j.at("uri"),path)),level(integer(j.at("max_level"),0,22,"source LOD")) {
        if(uri.find("{z}")==uri.npos || uri.find("{x}")==uri.npos ||
           ((uri.find("{y}")==uri.npos)==(uri.find("{-y}")==uri.npos)))
            throw std::invalid_argument("global-geodetic source requires z, x and one y placeholder");
    }
};
// mtime alone cannot distinguish replacement with a timestamp-preserving copy.
struct FileVersion {
    uintmax_t size=0,inode=0;
    int64_t time=0,ns=0;
    auto key() const {return std::make_tuple(size,inode,time,ns);}
    bool operator==(const FileVersion& other) const {return key()==other.key();}
    static FileVersion read(const std::string& path) {
#if defined(__linux__)
        struct stat st{};
        if(::stat(path.c_str(),&st)!=0) return {};
        return {uintmax_t(st.st_size),st.st_ino,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};
#else
        if(!fs::is_regular_file(path)) return {};
        return {fs::file_size(path),0,fs::last_write_time(path).time_since_epoch().count(),0};
#endif
    }
};
struct RasterTile {
    std::string path;
    std::array<FileVersion,3> versions;
    std::array<FileVersion,3> fingerprint() const {
        return {FileVersion::read(path),FileVersion::read(path+".msk"),FileVersion::read(path+".aux.xml")};
    }
    std::optional<uint64_t> expected;
    int divisor,x,y;
    RasterTile(const Source& source,Key key,const json* catalog=nullptr) {
        for(int z=std::min(source.level,key[0]);z>=0;--z) {
            divisor=1<<(key[0]-z);
            x=key[1]%divisor; y=key[2]%divisor;
            if(catalog) {
                const auto it=catalog->find(tileName({z,key[1]/divisor,key[2]/divisor}));
                if(it==catalog->end()) continue;
                expected=it->get<uint64_t>();
            }
            path=source.uri;
            replace(path,"{z}",z); replace(path,"{x}",key[1]/divisor);
            replace(path,"{y}",key[2]/divisor); replace(path,"{-y}",(1<<z)-1-key[2]/divisor);
            if(fs::is_regular_file(path)) {
                versions=fingerprint(); return;
            }
            if(expected) throw std::runtime_error("missing catalogued volume tile: "+path);
        }
        throw std::runtime_error("missing tiled terrain coverage: "+path);
    }
    bool stable() const {return versions==fingerprint();}
    auto identity() const {return std::tie(path,versions,divisor,x,y);}
};
// Only decode the source window containing the requested child, with an extra
// sample for bilinear elevation interpolation. Imagery is sampled at pixel centres.
#if VSGRAYSIM_EARTH_RASTER
std::vector<float> readBand(GDALRasterBandH band,const RasterTile& tile,int n,bool nodes) {
    if(!band) throw std::runtime_error("raster band missing: "+tile.path);
    const int sx=GDALGetRasterBandXSize(band),sy=GDALGetRasterBandYSize(band);
    if(sx<2 || sy<2 || sx>4096 || sy>4096) throw std::runtime_error("unsupported TMS tile size");
    auto coordinate=[&](int i,int sub,int size) {
        double u=(sub+(i+.5)/n)/tile.divisor;
        return std::clamp(nodes ? u*(size-1) : u*size-.5,0.,double(size-1));
    };
    int x0=int(coordinate(0,tile.x,sx)),y0=int(coordinate(0,tile.y,sy));
    int w=int(std::ceil(coordinate(n-1,tile.x,sx)))-x0+1,h=int(std::ceil(coordinate(n-1,tile.y,sy)))-y0+1;
    std::vector<float> pixels(size_t(w)*h),result(n*n);
    if(GDALRasterIO(band,GF_Read,x0,y0,w,h,pixels.data(),w,h,GDT_Float32,0,0)!=CE_None)
        throw std::runtime_error("failed to read raster window: "+tile.path);
    std::vector<uint8_t> mask;
    if(GDALGetMaskFlags(band)!=GMF_ALL_VALID) {
        mask.resize(pixels.size());
        if(GDALRasterIO(GDALGetMaskBand(band),GF_Read,x0,y0,w,h,mask.data(),w,h,GDT_Byte,0,0)!=CE_None)
            throw std::runtime_error("unreadable terrain validity mask");
    }
    int scaled=0,offsetted=0;
    const double scale=GDALGetRasterScale(band,&scaled),offset=GDALGetRasterOffset(band,&offsetted);
    for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
        double x=coordinate(i,tile.x,sx)-x0,y=coordinate(j,tile.y,sy)-y0;
        int a=int(x),b=int(y);
        double v=0;
        for(int dy=0;dy<=1;++dy) for(int dx=0;dx<=1;++dx) {
            double weight=(dx?x-a:1-(x-a))*(dy?y-b:1-(y-b));
            if(weight==0) continue;
            int k=std::min(b+dy,h-1)*w+std::min(a+dx,w-1);
            if(!std::isfinite(pixels[k]) || (!mask.empty()&&!mask[k]))
                throw std::runtime_error("NoData in requested terrain tile: "+tile.path);
            v+=pixels[k]*weight;
        }
        result[j*n+i]=v*(scaled?scale:1)+(offsetted?offset:0);
    }
    return result;
}
struct Dataset {
    GDALDatasetH handle;
    explicit Dataset(const std::string& path):handle(GDALOpen(path.c_str(),GA_ReadOnly)) {
        if(!handle) throw std::runtime_error("cannot open "+path);
    }
    ~Dataset(){GDALClose(handle);}
};
struct Block {
    RasterTile dem,image;
    int cells,texels,dem_pixels,image_pixels;
    std::vector<float> heights;
    std::vector<uint8_t> rgba;
    Block(RasterTile d,RasterTile im,int n,int t):dem(std::move(d)),image(std::move(im)),cells(n),texels(t) {
        Dataset elevation(dem.path),rgb(image.path);
        dem_pixels=std::min(GDALGetRasterXSize(elevation.handle),GDALGetRasterYSize(elevation.handle))-1;
        image_pixels=std::min(GDALGetRasterXSize(rgb.handle),GDALGetRasterYSize(rgb.handle));
        heights=readBand(GDALGetRasterBand(elevation.handle,1),dem,n,true);
        rgba.assign(4*t*t,255);
        if(GDALGetRasterCount(rgb.handle)<3) throw std::runtime_error("terrain imagery requires RGB bands");
        for(int c=0;c<3;++c) {
            auto band=GDALGetRasterBand(rgb.handle,c+1);
            if(GDALGetRasterDataType(band)!=GDT_Byte) throw std::runtime_error("terrain imagery requires 8-bit RGB");
            auto values=readBand(band,image,t,false);
            for(size_t i=0;i<values.size();++i) rgba[4*i+c]=uint8_t(std::clamp(std::lround(values[i]),0L,255L));
        }
    }
};
std::shared_ptr<const Block> block(const Source& dem,const Source& image,Key key,int n,int tex,size_t& hits) {
    RasterTile d(dem,key),im(image,key);
    static std::mutex mutex;
    static std::list<std::shared_ptr<const Block>> cache;
    auto findCached=[&]() -> std::shared_ptr<const Block> {
        for(auto it=cache.begin();it!=cache.end();++it) {
            const auto& value=**it;
            if(value.cells==n && value.texels==tex && value.dem.identity()==d.identity() && value.image.identity()==im.identity()) {
                auto result=*it;
                cache.splice(cache.begin(),cache,it);
                ++hits;
                return result;
            }
        }
        return {};
    };
    {
        std::lock_guard lock(mutex);
        if(auto result=findCached()) return result;
    }
    auto value=std::make_shared<Block>(d,im,n,tex);
    if(!d.stable()||!im.stable()) throw std::runtime_error("terrain raster changed during decoding");
    std::lock_guard lock(mutex);
    if(auto result=findCached()) return result;
    cache.push_front(value);
    if(cache.size()>64) cache.pop_back();
    return value;
}
#endif
// A palette entry owns the occupied half-open radial intervals and surface colour
// of a column. Tile payloads and the decoded cache are independent of the anchor.
struct VolumeTile {
    using Column=std::vector<std::array<int,2>>;
    struct Profile {
        Column runs;
        std::array<uint8_t,3> rgb;
    };
    int side,texture_side=0;
    double dem_spacing=0,image_spacing=0;
    std::vector<uint8_t> texture;
    std::vector<Profile> palette;
    std::vector<int> columns;

    VolumeTile(const RasterTile& file,const Key& key,double quantum) {
        if(file.versions[0].size>4*1024*1024) throw std::runtime_error("oversized volume tile: "+file.path);
        std::ifstream stream(file.path,std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(stream)),{});
        if(file.expected && digest(bytes)!=*file.expected) throw std::runtime_error("volume tile differs from published catalog: "+file.path);
        json tile=json::parse(bytes);
        int sourceLevel=key[0];
        for(int scale=file.divisor;scale>1;scale/=2) --sourceLevel;
        if(tile.at("tile")!=Key{sourceLevel,key[1]/file.divisor,key[2]/file.divisor})
            throw std::runtime_error("misaddressed volume tile: "+file.path);
        side=integer(tile.at("side"),1,256,"volume tile side");
        dem_spacing=tile.value("dem_spacing_m",0.);
        image_spacing=tile.value("imagery_spacing_m",0.);
        if(!std::isfinite(dem_spacing) || dem_spacing<0 || !std::isfinite(image_spacing) || image_spacing<0)
            throw std::invalid_argument("invalid native tile sampling metadata");
        if(tile.contains("texture_rgb") || tile.contains("texture_side")) {
            texture_side=integer(tile.at("texture_side"),1,256,"texture side");
            const auto& bytes=tile.at("texture_rgb").get_ref<const std::string&>();
            if(bytes.size()!=size_t(texture_side*texture_side*6)) throw std::invalid_argument("incorrect native RGB payload length");
            auto digit=[](char c) {
                if(c>='0' && c<='9') return c-'0';
                if(c>='a' && c<='f') return c-'a'+10;
                throw std::invalid_argument("invalid hex RGB payload");
            };
            for(size_t i=0;i<bytes.size();i+=2) texture.push_back(uint8_t(16*digit(bytes[i])+digit(bytes[i+1])));
        }
        const auto& profiles=tile.at("profiles");
        if(!profiles.is_array() || profiles.empty() || profiles.size()>65536) throw std::invalid_argument("invalid voxel profiles");
        for(const auto& entry:profiles) {
            Profile profile;
            const auto& rgb=entry.at("rgb");
            if(!rgb.is_array() || rgb.size()!=3) throw std::invalid_argument("invalid voxel RGB");
            for(int c=0;c<3;++c) profile.rgb[c]=uint8_t(integer(rgb[c],0,255,"colour"));
            const auto& runs=entry.at("runs");
            if(!runs.is_array() || runs.size()>32) throw std::invalid_argument("invalid voxel runs");
            for(const auto& run:runs) {
                if(!run.is_array() || run.size()!=2) throw std::invalid_argument("expected voxel [bottom,top) interval");
                const int lo=integer(run[0],-100000000,100000000,"bottom"),hi=integer(run[1],-100000000,100000000,"top");
                if(lo>=hi || (!profile.runs.empty() && lo<=profile.runs.back()[1]) ||
                   std::abs(lo*quantum)>100000 || std::abs(hi*quantum)>100000)
                    throw std::invalid_argument("noncanonical or out of range voxel runs");
                profile.runs.push_back({lo,hi});
            }
            palette.push_back(std::move(profile));
        }
        const auto& indices=tile.at("columns");
        if(!indices.is_array() || indices.size()!=size_t(side*side)) throw std::invalid_argument("wrong voxel column count");
        for(const auto& id:indices) columns.push_back(integer(id,0,int(palette.size())-1,"profile index"));
        if(!file.stable()) throw std::runtime_error("volume tile changed during loading: "+file.path);
    }
    static size_t offset(const RasterTile& file,int x,int y,int n,int side) {
        const auto coord=[&](int sub,int i) {
            return int(((int64_t(sub)*2*n+2*i+1)*side)/(int64_t(file.divisor)*2*n));
        };
        return coord(file.y,y)*side+coord(file.x,x);
    }
    const Profile& sample(const RasterTile& file,int x,int y,int n) const {
        return palette[columns[offset(file,x,y,n,side)]];
    }
    const uint8_t* color(const RasterTile& file,int x,int y,int n) const {
        if(texture.empty()) return sample(file,x,y,n).rgb.data();
        return texture.data()+3*offset(file,x,y,n,texture_side);
    }

};
std::shared_ptr<const VolumeTile> volumeTile(const RasterTile& file,const Key& key,double quantum,size_t& hits) {
    using CacheKey=std::tuple<std::string,FileVersion,Key,double,std::optional<uint64_t>>;
    const Key parent{key[0]-int(std::log2(file.divisor)),key[1]/file.divisor,key[2]/file.divisor};
    const CacheKey identity{file.path,file.versions[0],parent,quantum,file.expected};
    static std::mutex mutex;
    static std::list<std::pair<CacheKey,std::shared_ptr<const VolumeTile>>> cache;
    auto find=[&]() -> std::shared_ptr<const VolumeTile> {
        for(auto it=cache.begin();it!=cache.end();++it) if(it->first==identity) {
            auto value=it->second;cache.splice(cache.begin(),cache,it);++hits;return value;
        }
        return {};
    };
    { std::lock_guard lock(mutex); if(auto cached=find()) return cached; }
    auto tile=std::make_shared<VolumeTile>(file,key,quantum);
    std::lock_guard lock(mutex);
    if(auto cached=find()) return cached;
    cache.emplace_front(identity,tile);
    if(cache.size()>64) cache.pop_back();
    return tile;
}

#if VSGRAYSIM_EARTH_RASTER
// Use the two level-zero roots of the same catalog for an orbital presentation.
// These samples are independent of the regional physics tile window.
std::shared_ptr<const OrbitalSurface> readRasterGlobe(const Source& dem, const Source& rgb) {
    constexpr int side = 32, tex = 256;
    size_t reused = 0;
    auto result = std::make_shared<OrbitalSurface>();
    result->side = side;
    result->texture_side = tex;
    result->heights.resize(2 * side * side);
    result->rgba.resize(8 * tex * tex);
    json provenance = json::array();
    for (int x = 0; x < 2; ++x) {
        const auto root = block(dem, rgb, {0,x,0}, side, tex, reused);
        if (!root->dem.stable() || !root->image.stable())
            throw std::runtime_error("orbital raster roots changed while loading");
        provenance.push_back({root->dem.path, root->image.path,
                              root->dem.versions[0].key(), root->image.versions[0].key()});
        for (int row = 0; row < side; ++row)
            std::copy_n(root->heights.begin() + row * side, side, result->heights.begin() + (2 * row + x) * side);
        for (int row = 0; row < tex; ++row)
            std::copy_n(root->rgba.begin() + 4 * row * tex, 4 * tex, result->rgba.begin() + 4 * (2 * row + x) * tex);
    }
    result->dataset = digest(provenance.dump());
    return result;
}
#endif

std::shared_ptr<const OrbitalSurface> readOrbital(const Source& tiles,const json& catalog,uint64_t dataset,double quantum) {
    static std::mutex mutex;
    static std::weak_ptr<const OrbitalSurface> cached;
    std::lock_guard lock(mutex);
    std::array<RasterTile,2> files={RasterTile(tiles,{0,0,0},&catalog),RasterTile(tiles,{0,1,0},&catalog)};
    std::array<std::shared_ptr<const VolumeTile>,2> roots;
    size_t hits=0;
    for(int x=0;x<2;++x) roots[x]=volumeTile(files[x],{0,x,0},quantum,hits);
    if(!files[0].stable() || !files[1].stable()) throw std::runtime_error("orbital roots changed while loading");
    if(auto value=cached.lock(); value && value->dataset==dataset) return value;
    auto globe=std::make_shared<OrbitalSurface>();
    globe->dataset=dataset;
    globe->side=std::max(roots[0]->side,roots[1]->side);
    globe->texture_side=std::max({int(globe->side),roots[0]->texture_side,roots[1]->texture_side});
    const int n=globe->side,t=globe->texture_side;
    globe->heights.resize(2*n*n);
    globe->rgba.resize(2*t*t*4,255);
    for(int x=0;x<2;++x) {
        for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
            const auto& runs=roots[x]->sample(files[x],i,j,n).runs;
            globe->heights[(j*2+x)*n+i]=runs.empty()?std::numeric_limits<double>::quiet_NaN():runs.back()[1]*quantum;
        }
        for(int j=0;j<t;++j) for(int i=0;i<t;++i)
            std::copy_n(roots[x]->color(files[x],i,j,t),3,globe->rgba.begin()+4*((j*2+x)*t+i));
    }
    cached=globe;
    return globe;
}

}
void EarthDataService::bakeVolume(const std::string& input,const std::string& output,double lat,double lon,
                                  int minLevel,int maxLevel,int radius,double bottomMetres,bool globalRoots) {
#if !VSGRAYSIM_EARTH_RASTER
    throw std::logic_error("baking raster data requires the GDAL adapter");
#else
    if(minLevel<5 || maxLevel>20 || minLevel>maxLevel || radius<1 || radius>10 || !std::isfinite(bottomMetres))
        throw std::invalid_argument("invalid voxel bake range");
    const auto source=fs::absolute(input);
    const auto mapVersion=FileVersion::read(source.string());
    const auto destination=fs::absolute(output);
    if(fs::exists(destination)) throw std::invalid_argument("voxel output already exists");
    std::ifstream stream(source);json map;stream>>map;
    if(map.at("format")!="voxel-geodetic-v1" || map.at("profile")!="global-geodetic")
        throw std::invalid_argument("baking requires a geodetic DEM/imagery package");
    Source dem(map.at("dem"),source.string()),rgb(map.at("imagery"),source.string());
    const int cells=integer(map.at("cells_per_tile"),2,32,"cells"),tex=integer(map.at("texels_per_tile"),2,128,"texels");
    const double quantum=map.at("vertical_step_m").get<double>();
    if(!std::isfinite(quantum)||quantum<=0||quantum>10000||std::abs(bottomMetres)>100000 || std::abs(bottomMetres/quantum)>1e8)
        throw std::invalid_argument("invalid voxel bake depth or quantum");
    const int bottom=int(std::floor(bottomMetres/quantum));
    if(std::abs(bottom*quantum)>100000) throw std::invalid_argument("quantized bottom outside supported height domain");
    // Register raster drivers only for import. All generated paths are relative.
    GDALAllRegister();
    fs::create_directories(destination.parent_path());
    const auto temp=destination.string()+".building";
    if(!fs::create_directory(temp)) throw std::runtime_error("voxel output staging directory already exists: "+temp);
    const auto write=[](const fs::path& path,const json& data) {
        fs::create_directories(path.parent_path());
        std::ofstream out;out.exceptions(std::ios::failbit|std::ios::badbit);out.open(path);out<<data.dump()<<'\n';out.close();
    };
    try {
        std::map<std::string,std::array<FileVersion,3>> sourceVersions;
        const auto pin=[&](const RasterTile& tile) {
            if(sourceVersions.emplace(tile.path,tile.versions).first->second!=tile.versions)
                throw std::runtime_error("source changed during bake: "+tile.path);
        };
        const double declaredBottom=bottom*quantum;
        json catalog=json::object();
        auto bakeTile=[&](const Key& key,int cells,int tex) {
            size_t hits=0;
            auto data=block(dem,rgb,key,cells,tex,hits);
            pin(data->dem);pin(data->image);
            json profiles=json::array();std::vector<int> columns;std::map<int,int> ids;
            for(double height:data->heights) {
                if(!std::isfinite(height)||std::abs(height)>100000 || std::abs(height/quantum)>1e8)
                    throw std::runtime_error("unrepresentable DEM elevation");
                const int top=int(std::round(height/quantum));
                if(top<=bottom || std::abs(top*quantum)>100000) throw std::runtime_error("invalid solid interval; check bake bottom");
                auto [it,inserted]=ids.emplace(top,int(profiles.size()));
                if(inserted) profiles.push_back({{"runs",{{bottom,top}}},{"rgb",{128,128,128}}});
                columns.push_back(it->second);
            }
            constexpr char hex[]="0123456789abcdef";
            std::string texture;texture.reserve(6*tex*tex);
            for(size_t i=0;i<data->rgba.size();++i) if(i%4!=3) {
                const auto b=data->rgba[i];texture.push_back(hex[b>>4]);texture.push_back(hex[b&15]);
            }
            const double spanMetres=std::ldexp(111320.*180.,-key[0]);
            json tile={{"tile",key},{"side",cells},{"profiles",profiles},{"columns",columns},{"texture_side",tex},{"texture_rgb",texture},
                       {"dem_spacing_m",spanMetres*data->dem.divisor/data->dem_pixels},
                       {"imagery_spacing_m",spanMetres*data->image.divisor/data->image_pixels}};
            const auto name=tileName(key);
            if(globalRoots) catalog[name]=digest(tile.dump()+"\n");
            write(fs::path(temp)/"tiles"/(name+".json"),tile);
        };
        if(globalRoots) {
            for(int x=0;x<2;++x) bakeTile({0,x,0},128,256);
            for(int z=1;z<=3;++z)
                for(int y=0;y<(1<<z);++y)
                    for(int x=0;x<(2<<z);++x) bakeTile({z,x,y},32,128);
        }
        for(int level=minLevel;level<=maxLevel;++level) {
            const Key center=tileKey(lat,lon,level);
            const int ny=1<<level,nx=ny*2;
            for(int y=std::max(0,center[2]-radius);y<=std::min(ny-1,center[2]+radius);++y)
                for(int dx=-radius;dx<=radius;++dx) bakeTile({level,(center[1]+dx+nx)%nx,y},cells,tex);
        }
        if(globalRoots) map["orbital_catalog"]=std::move(catalog);
        map.erase("dem");map.erase("imagery");
        map["format"]="voxel-geodetic-volume-v1";
        map["tiles"]={{"uri","tiles/{z}/{x}/{y}.json"},{"max_level",maxLevel}};
        auto& settings=map.at("earth");settings.erase("tile");settings["backend"]="voxel";
        settings["min_lod"]=globalRoots ? 5 : minLevel;settings["max_lod"]=maxLevel;settings["initial_lod"]=maxLevel;
        map["solid_bottom_m"]=declaredBottom;
        write(fs::path(temp)/"earth.map.json",map);
        write(fs::path(temp)/"earth.scene.json",{{"geo_anchor",{{"lat",lat},{"lon",lon},{"alt",0}}},
            {"earth",{{"backend","voxel"},{"map","earth.map.json"}}},{"objects",json::array()},
            {"sensors",{{"sensor_list",json::array()}}}});
        for(const auto& [path,versions]:sourceVersions) {
            const std::array<FileVersion,3> current{FileVersion::read(path),FileVersion::read(path+".msk"),FileVersion::read(path+".aux.xml")};
            if(current!=versions) throw std::runtime_error("source changed before publication: "+path);
        }
        if(!(FileVersion::read(source.string())==mapVersion)) throw std::runtime_error("source map changed during bake");
        fs::rename(temp,destination);
    } catch(...) {fs::remove_all(temp);throw;}
#endif
}

std::array<int,3> EarthDataService::tileKey(double lat,double lon,int z) {
    if(!std::isfinite(lat)||std::abs(lat)>90||!std::isfinite(lon)||z<0||z>22)
        throw std::invalid_argument("invalid geographic terrain focus or LOD");
    const int ny=1<<z;
    double wrap=std::fmod(lon,360.); if(wrap< -180) wrap+=360.; if(wrap>=180) wrap-=360.;
    return {z,std::min(2*ny-1,int((wrap+180)/180*ny)),std::min(ny-1,int((90-lat)/180*ny))};
}
EarthSurfaceSnapshotPtr EarthDataService::selectTiles(const EarthSurfaceSnapshotPtr& current,
        double lat,double lon,int level) {
    if(!current || !current->resident_tiles) throw std::invalid_argument("current surface is not a global tile set");
    auto key=tileKey(lat,lon,level);
    if(current->declaration.at("tile")==key) return current;
    auto settings=current->declaration;
    settings["tile"]=key;
    auto next=loadMap(current->source,current->anchor,settings);
    if(current->orbital && (!next->orbital || next->orbital->dataset!=current->orbital->dataset))
        throw std::runtime_error("the published globe catalog changed; reload the scene");
    return next;
}
EarthSurfaceSnapshotPtr EarthDataService::observerTiles(const EarthSurfaceSnapshotPtr& published,double lat,double lon,int level) {
    if(!published || !published->orbital) throw std::invalid_argument("observer tiles require a published global volume");
    const auto key=tileKey(lat,lon,level);
    if(published->declaration.at("tile")==key) return published;
    auto settings=published->declaration;
    settings["tile"]=key;
    auto anchor=published->anchor;
    const double span=std::ldexp(180.,-level);
    anchor.lat_deg=90.-(key[2]+.5)*span;
    anchor.lon_deg=-180.+(key[1]+.5)*span;
    anchor.alt_m=0.;
    auto result=loadMap(published->source,anchor,settings);
    if(!result->orbital || result->orbital->dataset!=published->orbital->dataset)
        throw std::runtime_error("observer terrain belongs to a different globe generation");
    return result;
}
std::shared_ptr<TiledVoxelTerrain> TiledVoxelTerrain::load(const json& map,json& settings,
        const simcore::geo::GeoAnchor& anchor,const std::string& path) {
    if(map.at("profile")!="global-geodetic") throw std::invalid_argument("expected global-geodetic tile profile");
    auto result=std::make_shared<TiledVoxelTerrain>();
    result->strict=settings.value("strict",true);
    result->provenance_assumed=settings.value("provenance_assumed",false);
    auto datum=settings.value("vertical_datum",std::string());
    if(datum!="ellipsoidal" && datum!="EPSG:4979" && datum!="WGS84_ELLIPSOID" && !(datum=="unknown" && result->provenance_assumed))
        throw std::invalid_argument("voxel DEM requires ellipsoidal heights or an explicitly assumed reference datum");
    const int minL=integer(settings.at("min_lod"),5,20,"min_lod");
    const int maxL=integer(settings.at("max_lod"),minL,20,"max_lod");
    const int initial=integer(settings.at("initial_lod"),minL,maxL,"initial_lod");
    Key key=EarthDataService::tileKey(anchor.lat_deg,anchor.lon_deg,initial);
    if(settings.contains("tile")) {
        const auto& v=settings.at("tile");
        if(!v.is_array() || v.size()!=3) throw std::invalid_argument("expected terrain tile [z,x,y]");
        key[0]=integer(v[0],minL,maxL,"tile LOD");
        key[1]=integer(v[1],0,(2<<key[0])-1,"tile x"); key[2]=integer(v[2],0,(1<<key[0])-1,"tile y");
    }
    settings["tile"]=key;
    const int n=integer(map.at("cells_per_tile"),2,32,"cells_per_tile");
    const int peripheral=integer(map.value("peripheral_lod_delta",json(0)),0,1,"peripheral_lod_delta");
    if(peripheral && n%2) throw std::invalid_argument("two-level terrain requires an even cells_per_tile");
    const int tex=integer(map.at("texels_per_tile"),2,128,"texels_per_tile");
    const double dz=map.at("vertical_step_m").get<double>();
    if(!std::isfinite(dz)||dz<=0 || dz>10000) throw std::invalid_argument("invalid voxel vertical quantum");
    const int ny=1<<key[0],nx=2*ny,west=key[1]-1,north=std::max(0,key[2]-1);
    const int tilesX=3,tilesY=std::min(ny-1,key[2]+1)-north+1;
    const int cols=tilesX*n,rows=tilesY*n;
    const double span=180./ny;
    const bool volume=map.at("format")=="voxel-geodetic-volume-v1";
    std::optional<Source> dem,image,tiles;
    std::vector<RasterTile> volumeFiles;
    if(volume) {
        tiles.emplace(map.at("tiles"),path);
        if(map.contains("orbital_catalog")) {
            const auto& catalog=map.at("orbital_catalog");
            if(!catalog.is_object() || !catalog.contains("0/0/0") || !catalog.contains("0/1/0"))
                throw std::invalid_argument("incomplete orbital catalog");
            const json identity={catalog,dz,n,tex,peripheral,tiles->level,datum,result->provenance_assumed};
            result->orbital=readOrbital(*tiles,catalog,digest(identity.dump()),dz);
        }
        result->volume_.resize(cols*rows);
        result->volume_level_=key[0];result->west_tile_=west;result->north_tile_=north;
        result->cells_per_tile_=n;result->quantum_=dz;
    } else {
        dem.emplace(map.at("dem"),path);image.emplace(map.at("imagery"),path);
#if VSGRAYSIM_EARTH_RASTER
        if (settings.value("orbital_preview", false)) result->orbital = readRasterGlobe(*dem, *image);
#endif
    }
    std::vector<double> heights(cols*rows);
    auto stride=[&](int i,int j) {
        if(i<0 || j<0 || i>=cols || j>=rows) return 2;
        return peripheral && (i/n!=1 || j/n!=key[2]-north) ? 2 : 1;
    };
#if VSGRAYSIM_EARTH_RASTER
    std::vector<std::shared_ptr<const Block>> pinned;
#endif
    result->imagery_width=tilesX*tex; result->imagery_height=tilesY*tex;
    result->imagery_rgba.resize(size_t(result->imagery_width)*result->imagery_height*4);
    for(int z=0;z<tilesY;++z) for(int x=0;x<tilesX;++x) {
        const int step=stride(x*n,z*n),samples=n/step;
        const Key address{key[0],(west+x+nx)%nx,north+z};
        if(volume) {
            // Choose a profile intact at the cell centre. Interpolating run limits
            // between unrelated columns can fill a tunnel or invent a ceiling.
            RasterTile file(*tiles,address,result->orbital?&map.at("orbital_catalog"):nullptr);
            auto tile=volumeTile(file,address,dz,result->reused_tiles);
            volumeFiles.push_back(file);
            const double spacing=111320.*span*file.divisor/tile->side;
            result->dem_spacing_m=std::max(result->dem_spacing_m,std::max(spacing,tile->dem_spacing));
            result->imagery_spacing_m=std::max(result->imagery_spacing_m,std::max(tile->image_spacing,111320.*span*file.divisor/(tile->texture_side?tile->texture_side:tile->side)));
            for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
                const auto& profile=tile->sample(file,i/step,j/step,samples);
                result->volume_[(z*n+j)*cols+x*n+i]=std::shared_ptr<const Column>(tile,&profile.runs);
                if(i%step==0 && j%step==0 && !profile.runs.empty()) ++result->valid_samples;
            }
            for(int j=0;j<tex;++j) for(int i=0;i<tex;++i) {
                const auto color=tile->color(file,i,j,tex);
                auto dest=result->imagery_rgba.begin()+4*((z*tex+j)*result->imagery_width+x*tex+i);
                std::copy_n(color,3,dest);dest[3]=255;
            }
            continue;
        }
#if VSGRAYSIM_EARTH_RASTER
        auto elevation=*dem;
        if(step==2) elevation.level=std::min(elevation.level,key[0]-1);
        auto data=block(elevation,*image,address,samples,tex,result->reused_tiles);
        result->valid_samples+=samples*samples;
        pinned.push_back(data);
        result->dem_spacing_m=std::max(result->dem_spacing_m,111320.*span*data->dem.divisor/data->dem_pixels);
        result->imagery_spacing_m=std::max(result->imagery_spacing_m,111320.*span*data->image.divisor/data->image_pixels);
        for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
            double h=std::round(data->heights[(j/step)*samples+i/step]/dz)*dz;
            if(!std::isfinite(h)||std::abs(h)>100000.) throw std::runtime_error("invalid DEM height in metres");
            heights[(z*n+j)*cols+x*n+i]=h;
        }
        for(int j=0;j<tex;++j) {
            auto dest=result->imagery_rgba.begin()+4*((z*tex+j)*result->imagery_width+x*tex);
            std::copy_n(data->rgba.begin()+4*j*tex,4*tex,dest);
        }
#endif
    }
    result->resident_tiles=tilesX*tilesY;
    result->resolution_m=span/n*111320.;
    result->min_x=result->min_z=result->min_height=inf;
    result->max_x=result->max_z=result->max_height=-inf;
    const auto frame=simcore::geo::makeEnuToWorldMatrix(anchor);
    const auto inverse=vsg::inverse(frame);
    struct Corner {vsg::dvec3 base,up;};
    std::vector<Corner> corners((cols+1)*(rows+1));
    for(int j=0;j<=rows;++j) for(int i=0;i<=cols;++i) {
        double lat=90.-(north+double(j)/n)*span,lon=-180.+(west+double(i)/n)*span;
        auto normal=simcore::geo::geoUp(lat,lon);
        if(vsg::dot(normal,simcore::geo::geoUp(anchor.lat_deg,anchor.lon_deg))<.9)
            throw std::invalid_argument("tile focus exceeds the local physics frame; open a scene with a nearer anchor");
        auto b=inverse*vsg::dvec4(simcore::geo::geodeticToECEF(lat,lon,0),1.);
        auto u=inverse*vsg::dvec4(normal,0.);
        corners[j*(cols+1)+i]={{b.x,b.y,b.z},{u.x,u.y,u.z}};
    }
    auto vertex=[&](int id,double h) {
        const auto& corner=corners[id];
        // Round once to the positions VSG and Bullet actually consume.
        vsg::dvec3 p(vsg::vec3(corner.base+corner.up*h));
        if(volume && result->vertices_.size()>=2000000) throw std::runtime_error("volume mesh exceeds working-set vertex limit");
        auto index=uint32_t(result->vertices_.size());
        result->vertices_.push_back(p);
        result->imagery_uv.emplace_back(float(id%(cols+1))/cols,float(id/(cols+1))/rows);
        result->min_x=std::min(result->min_x,p.x); result->max_x=std::max(result->max_x,p.x);
        result->min_z=std::min(result->min_z,p.z); result->max_z=std::max(result->max_z,p.z);
        result->min_height=std::min(result->min_height,p.y); result->max_height=std::max(result->max_height,p.y);
        return index;
    };
    auto triangle=[&](uint32_t a,uint32_t b,uint32_t c,int step) {
        const auto& vs=result->vertices_;
        if(vsg::length(vsg::cross(vs[b]-vs[a],vs[c]-vs[a]))<1e-8) return;
        result->indices_.insert(result->indices_.end(),{a,b,c});
        result->triangle_stride_.push_back(step);
    };
    const int pitch=cols+1;
    for(int j=0;j<rows;++j) for(int i=0;i<cols;++i) {
        const int step=stride(i,j);
        if(i%step || j%step) continue;
        const int a=j*pitch+i,b=a+step,c=a+step*pitch,d=c+step;
        const std::array<int,4> ids{a,c,d,b};
        const std::array<int,4> adjacent{stride(i-1,j),stride(i,j+step),stride(i+step,j),stride(i,j-1)};
        auto cap=[&](double h,bool underside) {
            auto face=[&](uint32_t p,uint32_t q,uint32_t r) {
                triangle(p,underside?r:q,underside?q:r,step);
            };
            std::array<uint32_t,8> rim{};
            int count=0;
            for(int k=0;k<4;++k) {
                rim[count++]=vertex(ids[k],h);
                if(adjacent[k]<step) rim[count++]=vertex((ids[k]+ids[(k+1)%4])/2,h);
            }
            if(count==4) {
                face(rim[0],rim[1],rim[2]);
                face(rim[0],rim[2],rim[3]);
            } else {
                // The coarse face uses the fine neighbour's actual geodetic edge
                // nodes. A straight chord or a visual skirt leaves a collision seam.
                const auto center=vertex((a+d)/2,h);
                for(int k=0;k<count;++k) face(center,rim[k],rim[(k+1)%count]);
            }
        };
        if(volume) {
            const auto& runs=*result->volume_[j*cols+i];
            for(const auto& run:runs) { cap(run[0]*dz,true); cap(run[1]*dz,false); }
            for(int edge=0;edge<4;++edge) {
                const int ds=std::min(step,adjacent[edge]);
                for(int k=0;k<step;k+=ds) {
                    const int x=edge==0?i-1:edge==2?i+step:i+(edge==1?k:step-k-ds);
                    const int y=edge==1?j+step:edge==3?j-1:j+(edge==0?k:step-k-ds);
                    if(x<0 || x>=cols || y<0 || y>=rows) continue; // Across the residency boundary occupancy is unknown.
                    const auto& neighbour=*result->volume_[y*cols+x];
                    const int dir=(ids[(edge+1)%4]-ids[edge])/step;
                    const int u=ids[edge]+dir*k,v=u+dir*ds;
                    auto wall=[&](int lo,int hi) {
                        const auto p=vertex(u,lo*dz),q=vertex(v,lo*dz),r=vertex(v,hi*dz),s=vertex(u,hi*dz);
                        triangle(p,q,r,ds);triangle(p,r,s,ds);
                    };
                    for(const auto& run:runs) {
                        int bottom=run[0];
                        for(const auto& other:neighbour) {
                            if(other[1]<=bottom) continue;
                            if(other[0]>=run[1]) break;
                            if(other[0]>bottom) wall(bottom,other[0]);
                            bottom=std::max(bottom,other[1]);
                            if(bottom>=run[1]) break;
                        }
                        if(bottom<run[1]) wall(bottom,run[1]);
                    }
                }
            }
            continue;
        }
        const double h=heights[j*cols+i];
        cap(h,false);
        auto riser=[&](int u,int v,double other,int spacing) {
            if(other==h) return;
            const auto p=vertex(u,h),q=vertex(v,h),r=vertex(v,other),s=vertex(u,other);
            triangle(p,q,r,spacing); triangle(p,r,s,spacing);
        };
        if(i+step<cols) {
            int ds=std::min(step,adjacent[2]);
            for(int k=0;k<step;k+=ds) riser(b+k*pitch,b+(k+ds)*pitch,heights[(j+k)*cols+i+step],ds);
        }
        if(j+step<rows) {
            int ds=std::min(step,adjacent[1]);
            for(int k=0;k<step;k+=ds) riser(c+k+ds,c+k,heights[(j+step)*cols+i+k],ds);
        }
    }
    for(uint32_t idx=0;idx<result->indices_.size();idx+=3) {
        int x0=bins-1,x1=0,z0=bins-1,z1=0;
        for(int k=0;k<3;++k) {
            auto v=result->vertices_[result->indices_[idx+k]];
            auto x=result->bin(v.x,result->min_x,result->max_x),z=result->bin(v.z,result->min_z,result->max_z);
            x0=std::min(x0,x); x1=std::max(x1,x); z0=std::min(z0,z); z1=std::max(z1,z);
        }
        for(int z=z0;z<=z1;++z) for(int x=x0;x<=x1;++x) result->columns_[z*bins+x].push_back(idx);
    }
    for(const auto& file:volumeFiles) if(!file.stable()) throw std::runtime_error("volume tile changed before publication");
    if(result->indices_.empty()) throw std::runtime_error("empty terrain working set");
#if VSGRAYSIM_EARTH_RASTER
    for(const auto& t:pinned) if(!t->dem.stable() || !t->image.stable()) throw std::runtime_error("tile set changed before publication");
#endif
    return result;
}
int TiledVoxelTerrain::bin(double value,double low,double high) const {
    return std::clamp(int((value-low)/(high-low)*bins),0,bins-1);
}
uint64_t TiledVoxelTerrain::occupancySignature() const {
    uint64_t h=1469598103934665603ull;
    auto append=[&](const auto& value) {
        const auto bytes=reinterpret_cast<const unsigned char*>(&value);
        for(size_t i=0;i<sizeof(value);++i) {h^=bytes[i];h*=1099511628211ull;}
    };
    append(quantum_);append(cells_per_tile_);
    for(size_t index=0;index<volume_.size();++index) {
        const auto override=damage_->find(index);
        const auto& column=override==damage_->end()?*volume_[index]:override->second;
        append(uint8_t(column.size()));
        for(const auto& run:column) {append(run[0]);append(run[1]);}
    }
    return h;
}
std::optional<double> TiledVoxelTerrain::columnTopGeodetic(double lat,double lon) const {
    const auto* runs=columnAt(lat,lon);
    if(!runs || runs->empty()) return {};
    return runs->back()[1]*quantum_;
}
std::optional<bool> TiledVoxelTerrain::occupiedGeodetic(double lat,double lon,double height) const {
    const auto* runs=columnAt(lat,lon);
    if(!runs || !std::isfinite(height)) return {};
    return std::any_of(runs->begin(),runs->end(),[&](const Run& run){return height>=run[0]*quantum_ && height<run[1]*quantum_;});
}

SurfaceSample TiledVoxelTerrain::sampleLocal(double x,double z) const {
    return waterBedLocal(x,z,inf);
}
SurfaceSample TiledVoxelTerrain::waterBedLocal(double x,double z,double waterLevel) const {
    SurfaceSample sample;
    sample.revision=revision; sample.resolution_m=std::max(resolution_m,dem_spacing_m);
    if(!std::isfinite(x)||!std::isfinite(z)||x<min_x||x>max_x||z<min_z||z>max_z) return sample;
    if(indices_.empty())return sample;
    SurfaceSample above=sample;
    if(volume_.empty()) waterLevel=inf;
    for(auto idx:columns_[bin(z,min_z,max_z)*bins+bin(x,min_x,max_x)]) {
        auto a=vertices_[indices_[idx]],e=vertices_[indices_[idx+1]]-a,f=vertices_[indices_[idx+2]]-a;
        double det=e.x*f.z-e.z*f.x;
        if(std::abs(det)<1e-12) continue;
        double u=((x-a.x)*f.z-(z-a.z)*f.x)/det,v=(e.x*(z-a.z)-e.z*(x-a.x))/det;
        if(u< -1e-10 || v< -1e-10 || u+v>1+1e-10) continue;
        double height=a.y+u*e.y+v*f.y;
        auto& boundary=height<=waterLevel?sample:above;
        if(boundary.status!=SurfaceStatus::NoData && (height<=waterLevel?height<=boundary.height:height>=boundary.height)) continue;
        const int stride=triangle_stride_[idx/3];
        boundary.resolution_m=std::max(dem_spacing_m,resolution_m*stride);
        boundary.status=(provenance_assumed || stride>1 || dem_spacing_m>resolution_m)?SurfaceStatus::Coarse:SurfaceStatus::Ready;
        boundary.normal=vsg::normalize(vsg::cross(e,f)); boundary.height=height;
    }
    // The last crossing below the plane is an underside when the plane
    // intersects solid rock. In that case report its lid so the wet mask is dry.
    if(sample.status!=SurfaceStatus::NoData && sample.normal.y<0) return above;
    return sample;
}

std::optional<TiledVoxelTerrain::DamageWindow> TiledVoxelTerrain::extractDamageWindow(
        double lat,double lon,double halfSpanMetres,double bottomMetres) const {
    if(volume_.empty() || !std::isfinite(lat) || std::abs(lat)>89.9 || !std::isfinite(lon) ||
       !std::isfinite(halfSpanMetres) || halfSpanMetres<=0 || halfSpanMetres>20000 ||
       !std::isfinite(bottomMetres)||std::abs(bottomMetres)>1000000.)
        return std::nullopt;
    if(!columnAt(lat,lon))return std::nullopt;
    // Preserve the vertical strata where possible. Increase the isotropic
    // cell size only when the requested working volume exceeds its budget.
    double cell=std::max(quantum_,1.);int nx,nz,ny;
    for(;;) {
        nx=std::max(2,int(std::ceil(2*halfSpanMetres/cell)));nz=nx;
        const double layers=std::max(1.,std::ceil((std::max(max_height,min_height)+cell-bottomMetres)/cell));
        if(layers<=4'000'000./(double(nx)*nz)){ny=int(layers);break;}
        cell*=2.;
    }
    DamageWindow window;
    window.cell=cell;
    window.nx=nx; window.nz=nz;
    window.ny=ny;
    window.occupied.assign(size_t(nx)*window.ny*nz,false);
    window.originalRuns.assign(size_t(nx)*nz,{});
    window.sourceColumns.assign(size_t(nx)*nz,volume_.size());
    window.heightOffsets.assign(size_t(nx)*nz,0.);window.heightScales.assign(size_t(nx)*nz,1.);
    const auto focus=simcore::geo::ecefToLocal(anchor,simcore::geo::geodeticToECEF(lat,lon,0));
    window.min={focus.x-.5*nx*cell,bottomMetres,focus.z-.5*nz*cell};
    for(int j=0;j<nz;++j) for(int i=0;i<nx;++i) {
        // Sample and commit the same ENU cell centres; an independent
        // latitude/longitude interpolation would address different columns.
        const auto p=simcore::geo::localToECEF(anchor,
            {window.min.x+(i+.5)*cell,0.,window.min.z+(j+.5)*cell});
        const auto coordinate=simcore::geo::ecefToGeodetic(p.x,p.y,p.z);
        const double clat=coordinate.x,clon=coordinate.y;
        const Column* runs=columnAt(clat,clon);
        const size_t column=size_t(j)*nx+i;
        if(!runs) continue;
        const auto tile=EarthDataService::tileKey(clat,clon,volume_level_);
        const int tiles=1<<volume_level_,n=cells_per_tile_;
        const int tx=(tile[1]-west_tile_+2*tiles)%(2*tiles),ty=tile[2]-north_tile_;
        double wrap=std::fmod(clon+180.,360.);if(wrap<0)wrap+=360.;
        const double span=180./tiles;
        const int ci=std::clamp(int((wrap/span-tile[1])*n),0,n-1);
        const int cj=std::clamp(int(((90.-clat)/span-tile[2])*n),0,n-1);
        window.sourceColumns[column]=size_t((ty*n+cj)*3*n+tx*n+ci);
        window.heightOffsets[column]=coordinate.z;
        window.heightScales[column]=vsg::dot(simcore::geo::geoUp(clat,clon),simcore::geo::geoUp(anchor.lat_deg,anchor.lon_deg));
        for(const auto& run:*runs) {
            window.originalRuns[column].push_back({run[0]*quantum_,run[1]*quantum_});
            for(int y=0;y<window.ny;++y) {
                const double lo=coordinate.z+(window.min.y+y*cell)*window.heightScales[column];
                const double hi=lo+cell*window.heightScales[column];
                if(lo>=run[0]*quantum_ && hi<=run[1]*quantum_)
                    window.occupied[(size_t(j)*window.ny+y)*nx+i]=true;
            }
        }
    }
    return window;
}

std::size_t TiledVoxelTerrain::applyDamage(const DamageWindow& window,
        const std::vector<std::vector<std::array<double,2>>>& runs) {
    if(volume_.empty() || window.nx<=0 || window.nz<=0 ||
       !std::isfinite(window.cell)||window.cell<=0||!std::isfinite(vsg::length(window.min))||
       runs.size()!=size_t(window.nx)*window.nz) return 0;
    for(const auto& column:runs)for(const auto& run:column)
        if(!std::isfinite(run[0])||!std::isfinite(run[1])||
           std::abs(run[0])>100000||std::abs(run[1])>100000||run[0]>=run[1])
            throw std::invalid_argument("invalid damaged voxel run");
    if(!damage_.unique())damage_=std::make_shared<std::map<size_t,Column>>(*damage_);
    std::size_t changed=0;
    const double cell=window.cell;
    for(int j=0;j<window.nz;++j) for(int i=0;i<window.nx;++i) {
        const size_t column=size_t(j)*window.nx+i;
        if(window.originalRuns.size()==runs.size() && runs[column]==window.originalRuns[column])continue;
        // Locate the geodetic cell this window column maps to.
        const double dx=(double(i)+.5)/window.nx, dz=(double(j)+.5)/window.nz;
        const auto p=simcore::geo::localToECEF(anchor,
            {window.min.x+dx*window.nx*cell,0,window.min.z+dz*window.nz*cell});
        const auto geo=simcore::geo::ecefToGeodetic(p.x,p.y,p.z);
        // Find the volume column index by replaying columnAt's addressing.
        const auto tile=EarthDataService::tileKey(geo.x,geo.y,volume_level_);
        const int ny=1<<volume_level_,nxx=2*ny,n=cells_per_tile_;
        const int tx=(tile[1]-west_tile_+nxx)%nxx,ty=tile[2]-north_tile_;
        if(tx>=3 || ty<0 || ty>=int(volume_.size())/(3*n*n)) continue;
        double wrap=std::fmod(geo.y+180.,360.);if(wrap<0) wrap+=360.;
        const double span=180./ny;
        const int ci=std::clamp(int((wrap/span-tile[1])*n),0,n-1);
        const int cj=std::clamp(int(((90.-geo.x)/span-tile[2])*n),0,n-1);
        const size_t index=window.sourceColumns.size()==runs.size()?window.sourceColumns[column]:size_t((ty*n+cj)*3*n+tx*n+ci);
        if(index>=volume_.size()) continue;
        // Build the override column from the damaged runs (quantized).
        Column overrideRuns;
        for(const auto& run:runs[column]) {
            if(run[0]>=run[1]) continue;
            overrideRuns.push_back({int(std::ceil(run[0]/quantum_)),int(std::floor(run[1]/quantum_))});
            if(!overrideRuns.empty() && overrideRuns.back()[0]>=overrideRuns.back()[1])
                overrideRuns.pop_back();
        }
        // Canonicalize: sort and merge adjacent/overlapping runs.
        std::sort(overrideRuns.begin(),overrideRuns.end());
        Column merged;
        for(const auto& run:overrideRuns) {
            if(!merged.empty() && run[0]<=merged.back()[1])
                merged.back()[1]=std::max(merged.back()[1],run[1]);
            else merged.push_back(run);
        }
        const auto old=damage_->find(index);
        const Column* baked=old==damage_->end()?volume_[index].get():&old->second;
        bool same = merged.size()==baked->size() &&
            std::equal(merged.begin(),merged.end(),baked->begin());
        if(!same) ++changed;
        (*damage_)[index]=std::move(merged);
    }
    if(changed){rebuildVolumeMesh();++revision;}
    return changed;
}

void TiledVoxelTerrain::rebuildVolumeMesh() {
    invalidateRayIndex();
    const int n=cells_per_tile_,cols=3*n,rows=int(volume_.size())/cols,pitch=cols+1;
    const double span=180./(1<<volume_level_);
    const auto inverse=vsg::inverse(simcore::geo::makeEnuToWorldMatrix(anchor));
    struct Corner{vsg::dvec3 base,up;};std::vector<Corner> corners(size_t(pitch)*(rows+1));
    for(int j=0;j<=rows;++j)for(int i=0;i<=cols;++i) {
        const double lat=90.-(north_tile_+double(j)/n)*span,lon=-180.+(west_tile_+double(i)/n)*span;
        const auto base=inverse*vsg::dvec4(simcore::geo::geodeticToECEF(lat,lon,0),1.);
        const auto up=inverse*vsg::dvec4(simcore::geo::geoUp(lat,lon),0.);
        corners[j*pitch+i]={{base.x,base.y,base.z},{up.x,up.y,up.z}};
    }
    vertices_.clear();indices_.clear();imagery_uv.clear();triangle_stride_.clear();
    for(auto& bin:columns_)bin.clear();valid_samples=0;
    min_x=min_z=min_height=inf;max_x=max_z=max_height=-inf;
    auto profile=[&](int i,int j)->const Column& {
        const size_t index=size_t(j)*cols+i;const auto override=damage_->find(index);
        return override==damage_->end()?*volume_[index]:override->second;
    };
    auto vertex=[&](int id,int height) {
        if(vertices_.size()>=2'000'000)throw std::runtime_error("damaged volume mesh exceeds working-set vertex limit");
        const auto& c=corners[id];const vsg::dvec3 p(vsg::vec3(c.base+c.up*(height*quantum_)));
        const auto index=uint32_t(vertices_.size());vertices_.push_back(p);
        imagery_uv.emplace_back(float(id%pitch)/cols,float(id/pitch)/rows);
        min_x=std::min(min_x,p.x);max_x=std::max(max_x,p.x);min_z=std::min(min_z,p.z);max_z=std::max(max_z,p.z);
        min_height=std::min(min_height,p.y);max_height=std::max(max_height,p.y);return index;
    };
    auto quad=[&](std::array<uint32_t,4> v) {
        for(auto tri:std::array<std::array<uint32_t,3>,2>{{{v[0],v[1],v[2]},{v[0],v[2],v[3]}}}) {
            if(vsg::length(vsg::cross(vertices_[tri[1]]-vertices_[tri[0]],vertices_[tri[2]]-vertices_[tri[0]]))<1e-8)continue;
            indices_.insert(indices_.end(),tri.begin(),tri.end());triangle_stride_.push_back(1);
        }
    };
    for(int j=0;j<rows;++j)for(int i=0;i<cols;++i) {
        const auto& runs=profile(i,j);if(!runs.empty())++valid_samples;
        const int a=j*pitch+i;const std::array<int,4> rim{a,a+pitch,a+pitch+1,a+1};
        for(const auto& run:runs) {
            for(int side=0;side<2;++side) {
                std::array<uint32_t,4> face;for(int k=0;k<4;++k)face[k]=vertex(rim[k],run[side]);
                if(side==0)std::reverse(face.begin(),face.end());quad(face);
            }
            for(int edge=0;edge<4;++edge) {
                const int x=i+(edge==0?-1:edge==2?1:0),z=j+(edge==1?1:edge==3?-1:0);
                if(x<0||x>=cols||z<0||z>=rows)continue;
                const auto& other=profile(x,z);int lo=run[0];
                auto wall=[&](int from,int to) {
                    const int u=rim[edge],v=rim[(edge+1)%4];
                    quad({vertex(u,from),vertex(v,from),vertex(v,to),vertex(u,to)});
                };
                for(const auto& neighbour:other) {
                    if(neighbour[1]<=lo)continue;if(neighbour[0]>=run[1])break;
                    if(neighbour[0]>lo)wall(lo,neighbour[0]);lo=std::max(lo,neighbour[1]);if(lo>=run[1])break;
                }
                if(lo<run[1])wall(lo,run[1]);
            }
        }
    }
    if(vertices_.empty()){min_x=max_x=min_z=max_z=min_height=max_height=0.;return;}
    for(uint32_t index=0;index<indices_.size();index+=3) {
        int x0=bins-1,x1=0,z0=bins-1,z1=0;
        for(int k=0;k<3;++k) {
            const auto& p=vertices_[indices_[index+k]];const int x=bin(p.x,min_x,max_x),z=bin(p.z,min_z,max_z);
            x0=std::min(x0,x);x1=std::max(x1,x);z0=std::min(z0,z);z1=std::max(z1,z);
        }
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x)columns_[z*bins+x].push_back(index);
    }
}

const TiledVoxelTerrain::Column* TiledVoxelTerrain::columnAt(double lat,double lon) const {
    if(volume_.empty()) return nullptr;
    const auto tile=EarthDataService::tileKey(lat,lon,volume_level_);
    const int ny=1<<volume_level_,nx=2*ny,n=cells_per_tile_;
    const int x=(tile[1]-west_tile_+nx)%nx,y=tile[2]-north_tile_;
    if(x>=3 || y<0 || y>=int(volume_.size())/(3*n*n)) return nullptr;
    double wrap=std::fmod(lon+180.,360.);if(wrap<0) wrap+=360.;
    const double span=180./ny;
    const int i=std::clamp(int((wrap/span-tile[1])*n),0,n-1);
    const int j=std::clamp(int(((90.-lat)/span-tile[2])*n),0,n-1);
    const size_t index=size_t((y*n+j)*3*n+x*n+i);
    if(const auto it=damage_->find(index);it!=damage_->end()) return &it->second;
    return volume_[index].get();
}
}
