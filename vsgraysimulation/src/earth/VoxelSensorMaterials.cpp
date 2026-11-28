#include "VoxelSensorMaterials.h"
#include "VoxelTerrain.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
#include <stdexcept>
namespace vsgraysim::earth {
using json=nlohmann::json;
namespace {
double value(const json& j,const char* name,double fallback,double low,double high){double n=j.value(name,fallback);if(!std::isfinite(n)||n<low||n>high)throw std::invalid_argument(std::string("invalid voxel material ")+name);return n;}
vsg::dvec3 triple(const json& j){if(!j.is_array()||j.size()!=3)throw std::invalid_argument("spectral region requires coordinate triples");vsg::dvec3 p(j[0].get<double>(),j[1].get<double>(),j[2].get<double>());if(!std::isfinite(vsg::length(p)))throw std::invalid_argument("invalid spectral region coordinate");return p;}
}
json normalizeVoxelMaterial(const json& source){
    if(!source.is_object())throw std::invalid_argument("voxel material must be an object");
    const std::set<std::string> fields{"density_kg_m3","temperature_k","emissivity","rho_tv","rho_swir","color_rgba","eps_mwir","eps_lwir","radar","thermal_response"};for(const auto& item:source.items())if(!fields.count(item.key()))throw std::invalid_argument("unknown voxel material field: "+item.key());
    json out={{"density_kg_m3",value(source,"density_kg_m3",2600,.001,1e6)},{"temperature_k",value(source,"temperature_k",288.15,1,5000)},{"emissivity",value(source,"emissivity",.92,0,1)},{"rho_tv",value(source,"rho_tv",.25,0,1)},{"rho_swir",value(source,"rho_swir",.2,0,1)}};
    const auto color=source.value("color_rgba",json::array({.54,.49,.37,1}));if(!color.is_array()||color.size()!=4)throw std::invalid_argument("material color requires RGBA");for(const auto& x:color)if(!x.is_number()||!std::isfinite(x.get<double>())||x.get<double>()<0||x.get<double>()>1)throw std::invalid_argument("invalid material color");out["color_rgba"]=color;
    for(const char* key:{"eps_mwir","eps_lwir"})if(source.contains(key))out[key]=value(source,key,0,0,1);
    if (source.contains("thermal_response")) {
        const auto& response = source.at("thermal_response");
        if (!response.is_object()) throw std::invalid_argument("thermal_response must be an object");
        constexpr std::array<const char*, 7> keys = {"solar_gain_k","inertia_h","wet_cooling_k",
            "source_gain_k","source_start_s","source_run_s","source_tau_s"};
        for (const auto& field : response.items())
            if (std::find(keys.begin(),keys.end(),field.key()) == keys.end())
                throw std::invalid_argument("unknown thermal_response field: "+field.key());
        out["thermal_response"] = {
            {"solar_gain_k",value(response,"solar_gain_k",0,0,80)},
            {"inertia_h",value(response,"inertia_h",2,.05,48)},
            {"wet_cooling_k",value(response,"wet_cooling_k",0,0,25)},
            {"source_gain_k",value(response,"source_gain_k",0,0,400)},
            {"source_start_s",value(response,"source_start_s",0,0,1e7)},
            {"source_run_s",value(response,"source_run_s",0,0,1e7)},
            {"source_tau_s",value(response,"source_tau_s",60,.01,1e7)}
        };
    }
    if(source.contains("radar")){const auto& r=source.at("radar");if(!r.is_object())throw std::invalid_argument("radar material must be an object");for(const auto& p:r.items())if(p.key()!="dielectric_real"&&p.key()!="dielectric_imag"&&p.key()!="dielectric_model"&&p.key()!="sigma0_db"&&p.key()!="surface_rough"&&p.key()!="rcs_scale")throw std::invalid_argument("unknown radar material field: "+p.key());
        const auto model=r.value("dielectric_model",json(0));if(!model.is_number_integer()||model.get<int>()<0||model.get<int>()>3)throw std::invalid_argument("invalid radar dielectric model");
        out["radar"]={{"dielectric_real",value(r,"dielectric_real",6,1,1e8)},{"dielectric_imag",value(r,"dielectric_imag",0,0,1e8)},{"dielectric_model",model},{"sigma0_db",value(r,"sigma0_db",-20,-120,120)},{"surface_rough",value(r,"surface_rough",.5,0,1)},{"rcs_scale",value(r,"rcs_scale",1,0,1e8)}};
    }return out;
}
std::shared_ptr<const VoxelSensorMaterials> VoxelSensorMaterials::load(const json& input,const VoxelTerrain& volume,const json& base,bool checkpoint){
    if(!input.is_object())throw std::invalid_argument("sensor_materials must be an object");const std::set<std::string> fields{"palette","regions","cell_runs","origin_m","dimensions","cell_size_m"};for(const auto& e:input.items())if(!fields.count(e.key()))throw std::invalid_argument("unknown sensor material field: "+e.key());
    auto out=std::make_shared<VoxelSensorMaterials>();out->origin_=checkpoint?triple(input.at("origin_m")):volume.gridOrigin();out->dimensions_=checkpoint?input.at("dimensions").get<std::array<int,3>>():volume.dimensions();out->cell_=checkpoint?input.at("cell_size_m").get<double>():volume.resolution_m;
    size_t count=1;for(int n:out->dimensions_){if(n<1||n>4'000'000||count>4'000'000ULL/n)throw std::invalid_argument("sensor chart exceeds 4M cells");count*=n;}if(!std::isfinite(out->cell_)||out->cell_<=0||out->cell_!=volume.resolution_m)throw std::invalid_argument("sensor chart resolution mismatch");
    const auto& palette=input.at("palette");if(!palette.is_array()||palette.empty()||palette.size()>256)throw std::invalid_argument("sensor material palette must contain 1..256 entries");
    for(auto material:palette){if(!material.is_object()||(material.contains("density_kg_m3")||material.contains("color_rgba")))throw std::invalid_argument("sensor palette cannot change physical density or display color");if(material.contains("name")){const auto name=material.at("name").get<std::string>();if(name.empty()||name.size()>128)throw std::invalid_argument("invalid sensor material name");material.erase("name");}auto merged=base;merged.update(material);if(base.contains("radar")&&material.contains("radar")){auto r=base.at("radar");r.update(material.at("radar"));merged["radar"]=std::move(r);}out->palette_.push_back(normalizeVoxelMaterial(merged));}
    out->cells_.resize(count,0);auto id=[&](const json& n){if(!n.is_number_integer()||n.get<double>()<0||n.get<double>()>=palette.size())throw std::invalid_argument("sensor material index outside palette");return n.get<uint8_t>();};
    const auto regions=input.value("regions",json::array());if(!regions.is_array()||regions.size()>256)throw std::invalid_argument("too many spectral regions");for(const auto& region:regions){for(const auto& field:region.items())if(field.key()!="min_m"&&field.key()!="max_m"&&field.key()!="material")throw std::invalid_argument("unknown spectral region field");const auto low=triple(region.at("min_m")),high=triple(region.at("max_m"));int first[3],last[3];for(int a=0;a<3;++a){if(low[a]>=high[a])throw std::invalid_argument("empty spectral region");first[a]=int(std::clamp(std::ceil((low[a]-out->origin_[a])/out->cell_-.5),0.,double(out->dimensions_[a])));last[a]=int(std::clamp(std::ceil((high[a]-out->origin_[a])/out->cell_-.5),0.,double(out->dimensions_[a])));}const auto material=id(region.at("material"));for(int z=first[2];z<last[2];++z)for(int y=first[1];y<last[1];++y)std::fill(out->cells_.begin()+(size_t(z)*out->dimensions_[1]+y)*out->dimensions_[0]+first[0],out->cells_.begin()+(size_t(z)*out->dimensions_[1]+y)*out->dimensions_[0]+last[0],material);}
    const auto runs=input.value("cell_runs",json::array());if(!runs.is_array()||runs.size()>count)throw std::invalid_argument("invalid sensor cell runs");size_t end=0;for(const auto& run:runs){if(!run.is_array()||run.size()!=3||!run[0].is_number_integer()||!run[1].is_number_integer()||run[0].get<double>()<end||run[1].get<double>()<1||run[0].get<double>()>count||run[1].get<double>()>count)throw std::invalid_argument("invalid sensor cell run");const auto start=run[0].get<size_t>(),length=run[1].get<size_t>();if(length>count-start)throw std::invalid_argument("sensor cell run outside chart");std::fill_n(out->cells_.begin()+start,length,id(run[2]));end=start+length;}
    const auto min=volume.gridOrigin();const auto dims=volume.dimensions();
    for(int a=0;a<3;++a){const double offset=(min[a]-out->origin_[a])/out->cell_;if(offset<-.000001||std::abs(offset-std::round(offset))>1e-6||offset+dims[a]>out->dimensions_[a]+1e-6)throw std::invalid_argument("sensor chart does not contain cropped grid");}
    return out;
}
std::shared_ptr<const VoxelSensorMaterials> VoxelSensorMaterials::settled(const VoxelTerrain& volume)const{
    auto out=std::make_shared<VoxelSensorMaterials>(*this);const auto dimensions=volume.dimensions();const auto origin=volume.gridOrigin();
    for(int z=0;z<dimensions[2];++z)for(int x=0;x<dimensions[0];++x){int destination=0;for(int y=0;y<dimensions[1];++y)if(volume.occupancy()[(size_t(z)*dimensions[1]+y)*dimensions[0]+x]){
        const auto source=origin+vsg::dvec3(x+.5,y+.5,z+.5)*cell_;const auto target=origin+vsg::dvec3(x+.5,destination++ + .5,z+.5)*cell_;
        const auto chart=(target-origin_)/cell_;const auto key=(size_t(std::floor(chart.z))*dimensions_[1]+size_t(std::floor(chart.y)))*dimensions_[0]+size_t(std::floor(chart.x));out->cells_.at(key)=index(source);
    }}return out;
}
std::shared_ptr<const VoxelSensorMaterials> VoxelSensorMaterials::remapped(const VoxelTerrain& source,
    const VoxelTerrain& target,const std::vector<uint32_t>& destinations)const {
    auto out=std::make_shared<VoxelSensorMaterials>();
    out->origin_=target.gridOrigin();out->dimensions_=target.dimensions();out->cell_=cell_;
    out->palette_=palette_;out->cells_.resize(target.cellCount());
    const auto n=source.dimensions();
    for(size_t i=0;i<source.cellCount();++i)if(source.occupancy()[i]){
        const auto centre=source.gridOrigin()+vsg::dvec3(i%n[0]+.5,i/n[0]%n[1]+.5,i/(size_t(n[0])*n[1])+.5)*cell_;
        out->cells_.at(destinations.at(i))=index(centre);
    }
    return out;
}
std::shared_ptr<const VoxelSensorMaterials> VoxelSensorMaterials::paint(
    const json& region,const json& material,const json& radarFallback)const {
    const auto low=triple(region.at("min_m")),high=triple(region.at("max_m"));
    if(!material.is_object()||material.empty()||material.contains("density_kg_m3")||material.contains("color_rgba"))
        throw std::invalid_argument("spectral paint requires optical/thermal/radar properties only");

    std::array<int,3> first,last;
    for(size_t axis=0;axis<3;++axis) {
        if(low[axis]>=high[axis])throw std::invalid_argument("empty spectral paint region");
        first[axis]=int(std::clamp(std::ceil((low[axis]-origin_[axis])/cell_-.5),0.,double(dimensions_[axis])));
        last[axis]=int(std::clamp(std::ceil((high[axis]-origin_[axis])/cell_-.5),0.,double(dimensions_[axis])));
        if(first[axis]>=last[axis])throw std::invalid_argument("spectral paint misses voxel centres");
    }

    auto out=std::make_shared<VoxelSensorMaterials>(*this);
    std::array<int,256> replacement;
    replacement.fill(-1);
    const auto remap=[&](uint8_t index){
        auto merged=palette_.at(index);
        merged.update(material);
        if(material.contains("radar")) {
            auto radar=palette_[index].value("radar",radarFallback);
            if(!radar.is_null()) {
                radar.update(material["radar"]);
                merged["radar"]=std::move(radar);
            }
        }
        merged=normalizeVoxelMaterial(merged);
        auto match=std::find(out->palette_.begin(),out->palette_.end(),merged);
        if(match!=out->palette_.end())return int(match-out->palette_.begin());
        if(out->palette_.size()==replacement.size())throw std::invalid_argument("spectral palette exhausted (256 entries)");
        out->palette_.push_back(std::move(merged));
        return int(out->palette_.size()-1);
    };
    for(int z=first[2];z<last[2];++z)for(int y=first[1];y<last[1];++y) {
        const auto row=(size_t(z)*dimensions_[1]+y)*dimensions_[0];
        for(int x=first[0];x<last[0];++x) {
            const auto old=cells_[row+x];
            if(replacement[old]<0)replacement[old]=remap(old);
            out->cells_[row+x]=uint8_t(replacement[old]);
        }
    }
    return out;
}
uint8_t VoxelSensorMaterials::index(const vsg::dvec3& p)const{int at[3];for(int a=0;a<3;++a){const double n=std::floor((p[a]-origin_[a])/cell_);if(!std::isfinite(n)||n<0||n>=dimensions_[a])throw std::out_of_range("voxel material lookup outside original chart");at[a]=int(n);if(at[a]<0||at[a]>=dimensions_[a])throw std::out_of_range("voxel material lookup outside original chart");}return cells_[(size_t(at[2])*dimensions_[1]+at[1])*dimensions_[0]+at[0]];}
const json& VoxelSensorMaterials::at(const vsg::dvec3& local)const{return palette_.at(index(local));}
json VoxelSensorMaterials::encode()const{auto palette=json::array();for(auto p:palette_){p.erase("density_kg_m3");p.erase("color_rgba");palette.push_back(std::move(p));}auto runs=json::array();for(size_t i=0;i<cells_.size();){size_t end=i+1;while(end<cells_.size()&&cells_[end]==cells_[i])++end;if(cells_[i])runs.push_back({i,end-i,cells_[i]});i=end;}return {{"origin_m",{origin_.x,origin_.y,origin_.z}},{"dimensions",dimensions_},{"cell_size_m",cell_},{"palette",palette},{"cell_runs",runs}};}
}
