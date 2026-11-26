#include <vsgraysim/earth/VoxelEarthPatch.h>
#include <vsgraysim/earth/EarthDataService.h>
#include <vsgraysim/earth/VoxelScene.h>
#include "VoxelTerrain.h"
#include <vsgraysim/earth/SurfaceMesh.h>
#include <vsg/core/Array.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace vsgraysim::earth {
namespace {
using json=nlohmann::json;
int integer(const json& value,int low,int high,const char* name) {
    if(!value.is_number_integer()||value.get<double>()<low||value.get<double>()>high)
        throw std::invalid_argument(std::string("invalid Earth patch ")+name);
    return value.get<int>();
}
double number(const json& value,double low,double high,const char* name) {
    const double v=value.get<double>();
    if(!std::isfinite(v)||v<low||v>high)throw std::invalid_argument(std::string("invalid Earth patch ")+name);
    return v;
}
struct PatchMesh final : EarthSurfaceSnapshot {
    std::shared_ptr<const VoxelTerrain> occupancy;
    const VoxelEarthPatch* patch=nullptr;
    std::string mode;bool rigid=false;uint32_t ownerCell=0;
    std::vector<uint8_t> terrainSkin;
    struct VertexKey {
        std::array<int64_t,3> p; int face;
        bool operator==(const VertexKey& other)const{return p==other.p&&face==other.face;}
    };
    struct VertexHash {
        size_t operator()(const VertexKey& k)const {
            size_t h=unsigned(k.face);for(auto v:k.p)h^=std::hash<int64_t>{}(v)+0x9e3779b9+(h<<6)+(h>>2);return h;
        }
    };
    std::unordered_map<VertexKey,uint32_t,VertexHash> sharedVertices;
    EarthBackend backend()const override{return mode=="voxel"?EarthBackend::Voxel:EarthBackend::Rocky;}
    void triangle(vsg::dvec3 a,vsg::dvec3 b,vsg::dvec3 c,const vsg::dvec3& outward,bool smooth=false) {
        const auto normal=vsg::cross(b-a,c-a);
        if(vsg::length(normal)<1e-12)return;
        if(vsg::dot(normal,outward)<0)std::swap(b,c);
        if(indices_.size()+3>12'000'000)throw std::runtime_error("Earth patch exceeds 4M canonical triangles; reduce working set or HD subdivisions");
        // Share only vertices with the same surface role. A DEM cap must not
        // share normals with an excavation wall or its newly exposed floor.
        const int face=smooth?0:outward.y>.5?1:outward.y<-.5?2:outward.x>.5?3:outward.x<-.5?4:outward.z>.5?5:6;
        for(const auto& p:{a,b,c}) {
            const VertexKey key{{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)},face};
            auto [it,inserted]=sharedVertices.emplace(key,uint32_t(vertices_.size()));
            if(inserted) {
                if(vertices_.size()>=2'000'000)throw std::runtime_error("Earth patch exceeds 2M canonical surface vertices; reduce working set or HD subdivisions");
                vertices_.push_back(p);terrainSkin.push_back(smooth?1:0);
            }
            indices_.push_back(it->second);
        }
        triangle_cells_.push_back(ownerCell);
    }
    void polygon(const std::vector<vsg::dvec3>& polygon,const vsg::dvec3& outward,double flat=std::numeric_limits<double>::quiet_NaN()) {
        auto point=[&](vsg::dvec3 p){if(std::isfinite(flat))p.y=flat;return p;};
        for(size_t i=1;i+1<polygon.size();++i)triangle(point(polygon[0]),point(polygon[i]),point(polygon[i+1]),outward,!std::isfinite(flat)&&outward.y>.5);
    }
    void copy(const VoxelTerrain& v){vertices_=v.vertices();indices_=v.indices();triangle_cells_=v.triangleCells();}
    void finish(double x,double z,double spanX,double spanZ) {
        sharedVertices.clear();sharedVertices.rehash(0);
        min_x=x;min_z=z;max_x=x+spanX;max_z=z+spanZ;
        min_height=std::numeric_limits<double>::infinity();max_height=-min_height;
        for(const auto& p:vertices_){min_height=std::min(min_height,p.y);max_height=std::max(max_height,p.y);imagery_uv.emplace_back(float((p.x-x)/spanX),float((p.z-z)/spanZ));}
        if(vertices_.empty())min_height=max_height=occupancy->gridOrigin().y;
        auto hash=[&](const void* data,size_t length){const auto* bytes=static_cast<const uint8_t*>(data);for(size_t i=0;i<length;++i){revision^=bytes[i];revision*=1099511628211ULL;}};
        for(const auto& p:vertices_)for(double c:{p.x,p.y,p.z})hash(&c,sizeof(c));
        hash(mode.data(),mode.size());if(!revision)revision=1;
    }
    SurfaceSample sampleLocal(double x,double z)const override {
        SurfaceSample result;result.resolution_m=resolution_m;result.revision=revision;
        vsg::dvec3 hit,normal;
        if(intersectLocal({x,max_height+1,z},{0,-1,0},max_height-min_height+2,hit,normal)) {
            result.height=hit.y;result.normal=normal;result.status=provenance_assumed?SurfaceStatus::Coarse:SurfaceStatus::Ready;
        }
        return result;
    }
};
std::vector<vsg::dvec3> clip(const std::vector<vsg::dvec3>& input,double height,bool keepAbove) {
    std::vector<vsg::dvec3> result;if(input.empty())return result;
    auto inside=[&](const vsg::dvec3& p){return keepAbove?p.y>=height:p.y<=height;};
    auto previous=input.back();bool prev=inside(previous);
    for(const auto& p:input) {
        const bool next=inside(p);
        if(next!=prev)result.push_back(previous+(p-previous)*((height-previous.y)/(p.y-previous.y)));
        if(next)result.push_back(p);previous=p;prev=next;
    }
    return result;
}
}
void VoxelEarthPatch::validate()const {
    if(nx_<1||nz_<1||ny_<1||nx_>4096||nz_>4096||ny_>4'000'000||size_t(nx_)*nz_>4'000'000ULL/ny_||sub_<1||sub_>8||
       size_t(nx_*sub_+1)*(nz_*sub_+1)>1'050'625||heights_.size()!=size_t(nx_*sub_+1)*(nz_*sub_+1))
        throw std::invalid_argument("invalid/oversized Earth patch height field (4M cells, 1025² height samples)");
    for(double n:{x_,z_,bottom_,cell_})if(!std::isfinite(n)||std::abs(n)>1e6)throw std::invalid_argument("Earth patch outside local coordinate range");
    if(cell_<=1e-6||nx_*cell_>200000||nz_*cell_>200000)throw std::invalid_argument("invalid Earth patch cell/coverage");
    if(!seaReference_.empty()&&seaReference_.size()!=heights_.size())throw std::invalid_argument("invalid Earth sea mask");
    for(double h:seaReference_)if(!std::isfinite(h))throw std::invalid_argument("nonfinite Earth sea mask");
    for(double h:heights_)if(!std::isfinite(h)||h<=bottom_||h>bottom_+ny_*cell_+1e-7)throw std::invalid_argument("Earth patch height outside its declared volume");
    if(width_>4096||height_>4096||rgba_.size()!=size_t(width_)*height_*4||bool(width_)!=bool(height_))throw std::invalid_argument("invalid Earth patch imagery (maximum 4096²)");
    if(!materials_.is_object()||!provenance_.is_object())throw std::invalid_argument("invalid Earth patch material/provenance metadata");
    for(const auto& category:materials_.items()) {
        if(!category.value().is_object())throw std::invalid_argument("invalid Earth material category");
        for(const auto& field:category.value().items())if(!field.value().is_string()&&(!field.value().is_number()||!std::isfinite(field.value().get<double>())))throw std::invalid_argument("nonfinite Earth material parameter");
    }
    for(const auto* category:{"vis_params","lwir_params","radar_x_params","sar_c_params"})if(!materials_.contains(category))throw std::invalid_argument("missing Earth material category");
}
std::shared_ptr<const VoxelEarthPatch> VoxelEarthPatch::load(const json& source,const simcore::geo::GeoAnchor& anchor,const std::string& documentPath) {
    auto patch=std::shared_ptr<VoxelEarthPatch>(new VoxelEarthPatch);
    const auto style=source.value("voxel_surface",std::string("smooth"));if(style!="smooth"&&style!="blocks")throw std::invalid_argument("voxel_surface must be smooth or blocks");patch->smoothVoxel_=style=="smooth";
    patch->cell_=number(source.at("cell_size_m"),1e-5,1e5,"cell size");
    patch->bottom_=number(source.at("bottom_m"),-1e6,1e6,"bottom");
    patch->sub_=integer(source.value("hd_subdivisions",2),1,8,"HD subdivisions");
    const auto path=EarthDataService::resolveUri(source.at("map").get<std::string>(),documentPath);
    const auto surface=EarthDataService::loadMap(path,anchor,source.value("earth",json::object()));
    if(surface->backend()!=EarthBackend::Rocky)throw std::invalid_argument("Earth patch requires an elevation/imagery map or local heightmap; native volumes use source.type=volume");
    patch->x_=surface->min_x;patch->z_=surface->min_z;
    auto cells=[&](double span){const double n=span/patch->cell_;if(n<1||n>4096||std::abs(n-std::round(n))>1e-7)throw std::invalid_argument("Earth patch coverage must span an integer number of voxel cells");return int(std::round(n));};
    patch->nx_=cells(surface->max_x-surface->min_x);patch->nz_=cells(surface->max_z-surface->min_z);
    if(size_t(patch->nx_*patch->sub_+1)*(patch->nz_*patch->sub_+1)>1'050'625)throw std::invalid_argument("Earth patch exceeds 1025² HD samples");
    double maximum=-std::numeric_limits<double>::infinity();bool coarse=false;
    for(int z=0;z<=patch->nz_*patch->sub_;++z)for(int x=0;x<=patch->nx_*patch->sub_;++x) {
        const auto sample=surface->sampleLocal(patch->x_+x*patch->cell_/patch->sub_,patch->z_+z*patch->cell_/patch->sub_);
        if(sample.status==SurfaceStatus::NoData)throw std::runtime_error("Earth physics working set contains elevation NoData; select complete coverage");
        coarse=coarse||sample.status==SurfaceStatus::Coarse;
        patch->heights_.push_back(sample.height);maximum=std::max(maximum,sample.height);
    }
    if(source.contains("sea_surface_mask")){const auto& mask=source.at("sea_surface_mask");auto config=source.value("earth",json::object());config["imagery_size"]=32;const auto sea=EarthDataService::loadMap(EarthDataService::resolveUri(mask.at("map").get<std::string>(),documentPath),anchor,config);patch->seaMaskEpsilon_=number(mask.value("epsilon_m",.25),0,2,"sea mask epsilon");for(int z=0;z<=patch->nz_*patch->sub_;++z)for(int x=0;x<=patch->nx_*patch->sub_;++x){const auto h=sea->sampleLocal(patch->x_+x*patch->cell_/patch->sub_,patch->z_+z*patch->cell_/patch->sub_);if(h.status==SurfaceStatus::NoData)throw std::invalid_argument("sea datum mask has NoData");patch->seaReference_.push_back(h.height);}}
    const double layers=std::ceil((maximum-patch->bottom_)/patch->cell_);
    if(layers<1||layers>4'000'000)throw std::invalid_argument("Earth patch bottom/height exceeds volume budget");
    patch->ny_=int(layers);patch->width_=surface->imagery_width;patch->height_=surface->imagery_height;patch->rgba_=surface->imagery_rgba;
    patch->materials_=surface->material_config;
    patch->provenance_={{"earth",surface->declaration},{"surface_revision",surface->revision},{"resolution_m",surface->resolution_m},
        {"provenance_assumed",surface->provenance_assumed},{"source_coarse",coarse},{"dem_spacing_m",surface->dem_spacing_m},{"imagery_spacing_m",surface->imagery_spacing_m},
        {"geo_anchor",{{"lat",anchor.lat_deg},{"lon",anchor.lon_deg},{"alt",anchor.alt_m},{"heading_deg",anchor.heading_deg}}}};
    if(!patch->seaReference_.empty())patch->provenance_["sea_surface_mask"]={{"classification","DSM height minus EGM2008 geoid <= epsilon; inferred zero-height water, not a surveyed coastline or bathymetry"},{"epsilon_m",patch->seaMaskEpsilon_}};
    patch->validate();return patch;
}
json VoxelEarthPatch::encode()const {
    static const char* hex="0123456789abcdef";std::string pixels;pixels.reserve(rgba_.size()*2);
    for(const auto byte:rgba_){pixels+=hex[byte>>4];pixels+=hex[byte&15];}
    json value={{"format","voxel-earth-patch-v1"},{"origin_m",{x_,bottom_,z_}},{"cell_size_m",cell_},{"dimensions",{nx_,ny_,nz_}},
        {"voxel_surface",smoothVoxel_?"smooth":"blocks"},{"hd_subdivisions",sub_},{"heights_m",heights_},{"imagery_size",{width_,height_}},{"imagery_rgba_hex",pixels},
        {"provenance",provenance_},{"materials",materials_}};if(!seaReference_.empty()){value["sea_reference_m"]=seaReference_;value["sea_mask_epsilon_m"]=seaMaskEpsilon_;}return value;
}
std::shared_ptr<const VoxelEarthPatch> VoxelEarthPatch::decode(const json& value) {
    if(value.at("format")!="voxel-earth-patch-v1")throw std::invalid_argument("unsupported Earth patch checkpoint");
    auto result=std::shared_ptr<VoxelEarthPatch>(new VoxelEarthPatch);
    const auto& origin=value.at("origin_m");const auto& n=value.at("dimensions");const auto& size=value.at("imagery_size");
    if(!origin.is_array()||origin.size()!=3||!n.is_array()||n.size()!=3||!size.is_array()||size.size()!=2)throw std::invalid_argument("invalid Earth patch dimensions/origin");
    result->x_=number(origin[0],-1e6,1e6,"origin");result->bottom_=number(origin[1],-1e6,1e6,"bottom");result->z_=number(origin[2],-1e6,1e6,"origin");
    result->cell_=number(value.at("cell_size_m"),1e-5,1e5,"cell size");
    result->nx_=integer(n[0],1,4096,"columns X");result->ny_=integer(n[1],1,4'000'000,"layers");result->nz_=integer(n[2],1,4096,"columns Z");
    const auto style=value.value("voxel_surface",std::string("blocks"));if(style!="smooth"&&style!="blocks")throw std::invalid_argument("invalid Earth voxel surface style");result->smoothVoxel_=style=="smooth";
    result->sub_=integer(value.at("hd_subdivisions"),1,8,"HD subdivisions");
    const auto& heights=value.at("heights_m");if(!heights.is_array()||heights.size()>1'050'625)throw std::invalid_argument("oversized Earth height checkpoint");
    result->heights_=heights.get<std::vector<double>>();if(value.contains("sea_reference_m")){result->seaReference_=value.at("sea_reference_m").get<std::vector<double>>();result->seaMaskEpsilon_=number(value.value("sea_mask_epsilon_m",.25),0,2,"sea mask epsilon");}result->width_=integer(size[0],0,4096,"imagery width");result->height_=integer(size[1],0,4096,"imagery height");
    const auto& pixels=value.at("imagery_rgba_hex").get_ref<const std::string&>();
    if(pixels.size()!=size_t(result->width_)*result->height_*8)throw std::invalid_argument("truncated Earth imagery checkpoint");
    auto nibble=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("invalid Earth imagery hex");};
    result->rgba_.reserve(pixels.size()/2);for(size_t i=0;i<pixels.size();i+=2)result->rgba_.push_back(uint8_t(nibble(pixels[i])*16+nibble(pixels[i+1])));
    result->provenance_=value.at("provenance");result->materials_=value.at("materials");result->validate();return result;
}
std::shared_ptr<VoxelTerrain> VoxelEarthPatch::initialVolume()const {
    std::vector<bool> occupied(size_t(nx_)*ny_*nz_,false);const int pitch=nx_*sub_+1;
    for(int z=0;z<nz_;++z)for(int x=0;x<nx_;++x) {
        double land=-1e30;for(int j=0;j<=sub_;++j)for(int i=0;i<=sub_;++i){const auto k=size_t(z*sub_+j)*pitch+x*sub_+i;if(!seaReference_.empty())land=std::max(land,heights_[k]-seaReference_[k]);}if(!seaReference_.empty()&&land<=seaMaskEpsilon_)continue;
        double maximum=bottom_;
        for(int j=0;j<=sub_;++j)for(int i=0;i<=sub_;++i)maximum=std::max(maximum,heights_[size_t(z*sub_+j)*pitch+x*sub_+i]);
        for(int y=0;y<ny_;++y)occupied[(size_t(z)*ny_+y)*nx_+x]=bottom_+y*cell_<maximum-1e-10;
    }
    return VoxelTerrain::fromOccupancy({x_,bottom_,z_},cell_,nx_,ny_,nz_,occupied,false);
}
json VoxelEarthPatch::inferredThermalMaterials(const VoxelTerrain& volume, double baseK) const {
    if (!width_ || !height_ || volume.dimensions() != std::array<int,3>{nx_,ny_,nz_})
        throw std::invalid_argument("RGB thermal classification needs a complete Earth imagery/occupancy patch");
    // Presets are engineering estimates. VIS/SWIR and radar keep their
    // authored behaviour because these entries only override thermal fields.
    const auto material = [](const char* name, double temp, double mwir, double lwir,
                             double sun, double inertia, double wet) {
        return json{{"name",name},{"temperature_k",temp},{"eps_mwir",mwir},{"eps_lwir",lwir},
            {"thermal_response",{{"solar_gain_k",sun},{"inertia_h",inertia},{"wet_cooling_k",wet}}}};
    };
    auto palette = json::array({
        material("soil / mixed",baseK,.86,.92,12,3,5),
        material("tree cover",baseK-1.1,.94,.97,6,4,2.5),
        material("low vegetation",baseK-.5,.91,.96,8,2.8,4),
        material("built / bright mineral",baseK+.8,.85,.93,16,3.4,6),
        material("inferred open water",baseK,.95,.98,3,10,0)
    });
    auto classify = [](double r, double g, double b) {
        const double brightness = (r+g+b)/3.;
        if (b > 1.16*r && g > 1.06*r && b > .92*g && brightness > .10 && brightness < .62) return 4;
        if (g > 1.065*r && g > 1.10*b) return brightness < .34 ? 1 : 2;
        const double chroma = std::max({r,g,b})-std::min({r,g,b});
        if (brightness > .46 && chroma < .18) return 3;
        return 0;
    };

    // A fixed local grid and multiple taps keep classification stable across
    // cameras, terrain display modes and scene-package page boundaries.
    std::vector<uint8_t> classes(size_t(nx_)*nz_);
    for (int z=0;z<nz_;++z) for (int x=0;x<nx_;++x) {
        std::array<unsigned,5> votes{};
        for (const double dz : {.2,.5,.8}) for (const double dx : {.2,.5,.8}) {
            const auto px = std::min(width_-1,uint32_t((x+dx)*width_/nx_));
            const auto py = std::min(height_-1,uint32_t((z+dz)*height_/nz_));
            const auto p = (size_t(py)*width_+px)*4;
            if (rgba_[p+3]) ++votes[classify(rgba_[p]/255.,rgba_[p+1]/255.,rgba_[p+2]/255.)];
        }
        const auto dominant = std::max_element(votes.begin(),votes.end());
        if (*dominant >= 5) classes[size_t(z)*nx_+x] = uint8_t(dominant-votes.begin());
    }
    auto grid = std::vector<uint8_t>(volume.cellCount());
    const auto& occupied = volume.occupancy();
    const int pitch = nx_*sub_+1;
    for (int z=0;z<nz_;++z) for (int x=0;x<nx_;++x) {
        const auto zone = classes[size_t(z)*nx_+x];
        if (!zone) continue;
        // A sloping DSM cap can cross several vertical cells in one column.
        // Mark every cell it intersects, leaving buried cells as soil.
        double lowest = std::numeric_limits<double>::infinity();
        for (int j=0;j<=sub_;++j) for (int i=0;i<=sub_;++i)
            lowest = std::min(lowest,heights_[size_t(z*sub_+j)*pitch+x*sub_+i]);
        const int firstSurfaceCell = std::clamp(int(std::floor((lowest-bottom_-1e-9)/cell_)),0,ny_-1);
        for (int y=firstSurfaceCell;y<ny_;++y) {
            const auto cell = (size_t(z)*ny_+y)*nx_+x;
            if (occupied[cell]) grid[cell] = zone;
            else break;
        }
    }
    auto runs = json::array();
    for (size_t first=0;first<grid.size();) {
        size_t end=first+1;
        while (end<grid.size() && grid[end]==grid[first]) ++end;
        if (grid[first]) runs.push_back({first,end-first,grid[first]});
        first=end;
    }
    return {{"palette",std::move(palette)},{"cell_runs",std::move(runs)}};
}
EarthSurfaceSnapshotPtr VoxelEarthPatch::makeSurface(std::shared_ptr<const VoxelTerrain> volume,const std::string& mode,bool rigidFragment)const {
    if(mode!="hd"&&mode!="voxel")throw std::invalid_argument("Earth terrain mode must be hd or voxel");
    const auto n=volume->dimensions();const auto origin=volume->gridOrigin();
    std::array<int,3> offset{};const double bases[3]={x_,bottom_,z_};const int dims[3]={nx_,ny_,nz_};
    if(std::abs(volume->resolution_m-cell_)>cell_*1e-9)throw std::invalid_argument("Earth checkpoint occupancy cell size differs from height field");
    for(int a=0;a<3;++a) {
        const double cell=(origin[a]-bases[a])/cell_;offset[a]=int(std::round(cell));
        if(std::abs(cell-offset[a])>1e-7||offset[a]<0||offset[a]+n[a]>dims[a])throw std::invalid_argument("Earth checkpoint occupancy outside height field");
    }
    auto mesh=std::make_shared<PatchMesh>();mesh->occupancy=std::move(volume);mesh->patch=this;mesh->mode=mode;mesh->rigid=rigidFragment;
    mesh->revision=provenance_.value("surface_revision",uint64_t(1469598103934665603ULL));mesh->source=provenance_.at("earth").value("map",std::string());
    if(provenance_.contains("geo_anchor")) {
        const auto& a=provenance_.at("geo_anchor");mesh->anchor.lat_deg=a.at("lat").get<double>();mesh->anchor.lon_deg=a.at("lon").get<double>();mesh->anchor.alt_m=a.at("alt").get<double>();mesh->anchor.heading_deg=a.at("heading_deg").get<double>();
        simcore::geo::makeEnuToWorldMatrix(mesh->anchor);
    }
    mesh->dem_spacing_m=provenance_.value("dem_spacing_m",0.);mesh->imagery_spacing_m=provenance_.value("imagery_spacing_m",0.);
    mesh->resolution_m=(mode=="hd"||(smoothVoxel_&&!rigidFragment))?cell_/sub_:cell_;mesh->provenance_assumed=provenance_.value("provenance_assumed",false)||provenance_.value("source_coarse",false);
    mesh->imagery_width=width_;mesh->imagery_height=height_;mesh->material_config=materials_;mesh->declaration=provenance_;
    if(mode=="voxel"&&(!smoothVoxel_||rigidFragment))mesh->copy(*mesh->occupancy);
    else {
        const auto& occupied=mesh->occupancy->occupancy();const int pitch=nx_*sub_+1;
        auto solid=[&](int x,int y,int z){return x>=0&&x<n[0]&&y>=0&&y<n[1]&&z>=0&&z<n[2]&&occupied[(size_t(z)*n[1]+y)*n[0]+x];};
        auto point=[&](int x,int z){return vsg::dvec3(x_+x*cell_/sub_,heights_[size_t(z)*pitch+x],z_+z*cell_/sub_);};
        for(int z=0;z<n[2];++z)for(int x=0;x<n[0];++x) {
          const int gx=(x+offset[0])*sub_,gz=(z+offset[2])*sub_;
          double minimum=std::numeric_limits<double>::infinity();
          for(int j=0;j<=sub_;++j)for(int i=0;i<=sub_;++i)minimum=std::min(minimum,point(gx+i,gz+j).y);
          for(int y=0;y<n[1];++y)if(solid(x,y,z)) {
            mesh->ownerCell=uint32_t((size_t(z)*n[1]+y)*n[0]+x);
            const double low=origin.y+y*cell_,high=low+cell_;const int gx=(x+offset[0])*sub_,gz=(z+offset[2])*sub_;
            const bool above=solid(x,y+1,z),below=solid(x,y-1,z);
            // Entirely below the DEM, the cell's exposed cap is exactly flat.
            // Interior layers need no cap; keep side/hole generation below.
            if(high<=minimum) {
                const auto a=point(gx,gz),b=point(gx+sub_,gz),c=point(gx,gz+sub_),d=point(gx+sub_,gz+sub_);
                if(!below)mesh->polygon({a,c,d,b},{0,-1,0},low);
                if(!above)mesh->polygon({a,c,d,b},{0,1,0},high);
            } else for(int j=0;j<sub_;++j)for(int i=0;i<sub_;++i) {
                const auto a=point(gx+i,gz+j),b=point(gx+i+1,gz+j),c=point(gx+i,gz+j+1),d=point(gx+i+1,gz+j+1);
                for(const auto& tri:std::array<std::vector<vsg::dvec3>,2>{{{a,c,b},{b,c,d}}}) {
                    const double maxH=std::max({tri[0].y,tri[1].y,tri[2].y});if(maxH<=low+1e-10)continue;
                    auto polygon=clip(tri,low,true);
                    if(!below)mesh->polygon(polygon,{0,-1,0},low);
                    if(maxH<=high+1e-10)mesh->polygon(polygon,{0,1,0});
                    else {
                        mesh->polygon(clip(polygon,high,false),{0,1,0});
                        if(!above)mesh->polygon(clip(polygon,high,true),{0,1,0},high);
                    }
                }
            }
            auto side=[&](int ax,int az,int bx,int bz,vsg::dvec3 outward) {
                for(int i=0;i<sub_;++i) {
                    const auto a=point(gx+ax*sub_+(bx-ax)*i,gz+az*sub_+(bz-az)*i);
                    const auto b=point(gx+ax*sub_+(bx-ax)*(i+1),gz+az*sub_+(bz-az)*(i+1));
                    std::vector<double> cuts{0,1};
                    if(a.y!=b.y)for(double h:{low,high}) {const double t=(h-a.y)/(b.y-a.y);if(t>0&&t<1)cuts.push_back(t);}
                    std::sort(cuts.begin(),cuts.end());
                    for(size_t k=1;k<cuts.size();++k) {
                        auto p=a+(b-a)*cuts[k-1],q=a+(b-a)*cuts[k];if(std::max(p.y,q.y)<=low+1e-10)continue;
                        p.y=std::clamp(p.y,low,high);q.y=std::clamp(q.y,low,high);
                        const vsg::dvec3 p0(p.x,low,p.z),q0(q.x,low,q.z);
                        mesh->triangle(p0,q0,q,outward);mesh->triangle(p0,q,p,outward);
                    }
                }
            };
            if(!solid(x-1,y,z))side(0,0,0,1,{-1,0,0});
            if(!solid(x+1,y,z))side(1,0,1,1,{1,0,0});
            if(!solid(x,y,z-1))side(0,0,1,0,{0,0,-1});
            if(!solid(x,y,z+1))side(0,1,1,1,{0,0,1});
          }
        }
    }
    // Smooth only intact DEM caps. Excavation floors, cave ceilings and cut
    // walls keep their geometric normals and therefore remain visibly sharp.
    if(!mesh->terrainSkin.empty()) {
        const int pitch=nx_*sub_+1,rows=nz_*sub_+1;const double step=cell_/sub_;
        auto h=[&](int x,int z){return heights_[size_t(std::clamp(z,0,rows-1))*pitch+std::clamp(x,0,pitch-1)];};
        auto gradient=[&](int x,int z) {const int a=std::max(0,x-1),b=std::min(pitch-1,x+1),c=std::max(0,z-1),d=std::min(rows-1,z+1);return vsg::dvec3((h(b,z)-h(a,z))/((b-a)*step),0,(h(x,d)-h(x,c))/((d-c)*step));};
        auto draw=makeSurfaceDraw(*mesh,true);auto geometric=draw->arrays[1]->data.cast<vsg::vec3Array>();mesh->surface_normals.assign(geometric->begin(),geometric->end());
        for(size_t i=0;i<mesh->vertices().size();++i)if(mesh->terrainSkin[i]) {
            const auto& p=mesh->vertices()[i];const double x=std::clamp((p.x-x_)/step,0.,double(pitch-1)),z=std::clamp((p.z-z_)/step,0.,double(rows-1));
            const int ix=std::min(pitch-2,int(x)),iz=std::min(rows-2,int(z));const double u=x-ix,v=z-iz;
            const auto g=gradient(ix,iz)*((1-u)*(1-v))+gradient(ix+1,iz)*(u*(1-v))+gradient(ix,iz+1)*((1-u)*v)+gradient(ix+1,iz+1)*(u*v);
            const auto normal=vsg::vec3(vsg::normalize(vsg::dvec3(-g.x,1,-g.z)));
            if(vsg::dot(normal,mesh->surface_normals[i])>.7071067f)mesh->surface_normals[i]=normal;
        }
    }
    mesh->finish(x_,z_,nx_*cell_,nz_*cell_);
    if((rigidFragment||n[0]!=nx_||n[2]!=nz_)&&!rgba_.empty()&&!mesh->imagery_uv.empty()){
        // Keep the original pixel grid and linear sampling, including a
        // filtering gutter. Detached pieces must not each copy a 16MiB map.
        vsg::vec2 minimum(1,1),maximum(0,0);for(const auto& uv:mesh->imagery_uv)for(int a=0;a<2;++a){minimum[a]=std::min(minimum[a],uv[a]);maximum[a]=std::max(maximum[a],uv[a]);}
        const int x0=std::clamp(int(std::floor(double(minimum.x)*width_-.5))-1,0,int(width_)-1),z0=std::clamp(int(std::floor(double(minimum.y)*height_-.5))-1,0,int(height_)-1);
        const int x1=std::clamp(int(std::ceil(double(maximum.x)*width_-.5))+1,x0,int(width_)-1),z1=std::clamp(int(std::ceil(double(maximum.y)*height_-.5))+1,z0,int(height_)-1);
        mesh->imagery_width=uint32_t(x1-x0+1);mesh->imagery_height=uint32_t(z1-z0+1);mesh->imagery_rgba.resize(size_t(mesh->imagery_width)*mesh->imagery_height*4);
        for(int z=z0;z<=z1;++z)std::copy_n(rgba_.begin()+4*(size_t(z)*width_+x0),4*mesh->imagery_width,mesh->imagery_rgba.begin()+4*size_t(z-z0)*mesh->imagery_width);
        for(auto& uv:mesh->imagery_uv){uv.x=float((double(uv.x)*width_-x0)/mesh->imagery_width);uv.y=float((double(uv.y)*height_-z0)/mesh->imagery_height);}
    }else mesh->imagery_rgba=rgba_;
    // An authored surface can give uncovered voxel cuts their own soil
    // colour. Reserve a texel strip in the shared display/optical atlas so
    // road markings remain on the intact skin when a cell is removed.
    const auto declaration=provenance_.find("earth");
    if(declaration!=provenance_.end()&&declaration->contains("excavation_albedo_srgb")&&!mesh->terrainSkin.empty()&&
       mesh->imagery_width&&mesh->imagery_height<4094) {
        const auto rgb=declaration->at("excavation_albedo_srgb").get<std::array<int,3>>();
        for(auto c:rgb)if(c<0||c>255)throw std::invalid_argument("invalid excavation albedo byte");
        const auto width=mesh->imagery_width,height=mesh->imagery_height;
        mesh->imagery_height+=2;
        mesh->imagery_rgba.resize(size_t(width)*mesh->imagery_height*4);
        for(size_t i=size_t(width)*height;i<size_t(width)*mesh->imagery_height;++i) {
            for(int c=0;c<3;++c)mesh->imagery_rgba[i*4+c]=uint8_t(rgb[c]);
            mesh->imagery_rgba[i*4+3]=255;
        }
        for(size_t i=0;i<mesh->imagery_uv.size();++i) {
            auto& uv=mesh->imagery_uv[i];
            if(mesh->terrainSkin[i])uv.y=float(std::clamp(double(uv.y)*height,.5,double(height)-.5)/mesh->imagery_height);
            else uv.y=float((height+1.)/mesh->imagery_height);
        }
    }
    return mesh;
}
void refreshVoxelEarthObject(VoxelSceneObject& object) {
    if(!object.earth){object.geometry.reset();return;}
    if(object.terrainMode!="hd"&&object.terrainMode!="voxel")throw std::invalid_argument("invalid Earth terrain mode");
    if(object.terrainMode=="hd"&&object.body.motion!="static")throw std::invalid_argument("HD Earth terrain requires static motion; use voxel mode for moving fragments");
    auto current=std::dynamic_pointer_cast<const PatchMesh>(object.geometry);
    if(!current||current->occupancy!=object.volume||current->patch!=object.earth.get()||current->mode!=object.terrainMode||current->rigid!=(object.body.motion!="static"))
        object.geometry=object.earth->makeSurface(object.volume,object.terrainMode,object.body.motion!="static");
}
void validateVoxelEarthView(const VoxelSceneSnapshot& scene,const vsg::dvec3& position,const vsg::dvec3& forward,
    const vsg::dvec3& up,double fov,double aspect,double range) {
    std::set<const VoxelEarthPatch*> patches;
    for(const auto& [id,o]:scene.objects)if(o.earth&&o.earth->strictCoverage())patches.insert(o.earth.get());
    if(patches.empty())return;
    if(!std::isfinite(vsg::length(position))||!std::isfinite(vsg::length(forward))||!std::isfinite(vsg::length(up))||
       vsg::length(forward)<1e-12||vsg::length(vsg::cross(forward,up))<1e-12||!std::isfinite(fov)||fov<=0||fov>=179||!std::isfinite(aspect)||aspect<=0||!std::isfinite(range)||range<=0)
        throw std::invalid_argument("invalid Earth aperture/footprint");
    const auto f=vsg::normalize(forward),r=vsg::normalize(vsg::cross(f,up)),u=vsg::cross(r,f);
    const double tangent=std::tan(fov*3.14159265358979323846/360);
    bool downward=false,upward=false;
    for(int z:{-1,1})for(int x:{-1,1}){const auto d=f+r*(x*tangent)+u*(z*tangent/aspect);downward=downward||d.y<0;upward=upward||d.y>=0;}
    if(downward&&upward)throw std::out_of_range("strict Earth frustum crosses the horizon; select a bounded ground footprint or a sky-only view");
    for(const auto* patch:patches) {
        const auto box=patch->bounds();bool covered=position.y>=box.min.y;
        for(int z:{-1,1})for(int x:{-1,1}) {
            const auto d=vsg::normalize(f+r*(x*tangent)+u*(z*tangent/aspect));
            if(d.y>=0)continue;
            const double distance=(box.min.y-position.y)/d.y;if(distance<0){covered=false;continue;}
            const auto p=position+d*distance;
            covered=covered&&p.x>=box.min.x-1e-8&&p.x<=box.max.x+1e-8&&p.z>=box.min.z-1e-8&&p.z<=box.max.z+1e-8;
        }
        if(covered)return;
    }
    throw std::out_of_range("Earth measurement footprint exceeds the resident physics working set; load complete coverage before measuring");
}
}
