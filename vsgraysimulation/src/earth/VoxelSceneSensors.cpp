#include <vsgraysim/earth/VoxelSceneSensors.h>
#include <vsgraysim/earth/SurfaceMesh.h>
#include <vsgraysim/earth/VoxelEarthPatch.h>
#include "VoxelTerrain.h"
#include "VoxelSensorMaterials.h"
#include <vsg/core/Array2D.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
namespace vsgraysim::earth {
namespace {
constexpr size_t sensorImageBudget = 128ULL*1024*1024;
using TextureKey = std::pair<const void*, std::string>;

TextureKey textureKey(const VoxelSceneObject& object) {
    const auto* appearance = object.volume->appearance().get();
    const void* identity = !object.earth && appearance ? static_cast<const void*>(appearance) : &object.surface();
    return {identity, object.material.at("color_rgba").dump()};
}

struct PackedTexture {
    const EarthSurfaceSnapshot* surface;
    vsg::vec4 tint;
    uint32_t page=0, x=0, y=0;
};

void packSensorTextures(std::map<TextureKey,PackedTexture>& textures, uint32_t width, uint32_t height,
                        rockyenv::sensor::SharedSceneResources::TexArraySpec& output) {
    struct Shelf { uint32_t y, used, height; };
    struct Page { std::vector<Shelf> shelves; uint32_t used=0; };
    std::vector<Page> pages;
    std::vector<PackedTexture*> sorted;
    for(auto& [key,region]:textures)sorted.push_back(&region);
    std::stable_sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b){
        const auto extent=[](const auto* texture){return std::make_pair(texture->surface->imagery_height,texture->surface->imagery_width);};
        return extent(a)>extent(b);
    });
    // One replicated texel around each chart is sufficient for the existing
    // linear sampler, which uses a single mip level.
    for(auto* region:sorted) {
        const uint32_t w=region->surface->imagery_width+2,h=region->surface->imagery_height+2;
        const auto pageFor=[&](const Page& page){
            return page.used+h<=height || std::any_of(page.shelves.begin(),page.shelves.end(),
                [&](const Shelf& shelf){return shelf.height>=h && shelf.used+w<=width;});
        };
        const size_t index=std::distance(pages.begin(),std::find_if(pages.begin(),pages.end(),pageFor));
        if(index==pages.size()) {
            if((pages.size()+1)*size_t(width)*height*4>sensorImageBudget)
                throw std::runtime_error("packed voxel scene exceeds 128 MiB sensor imagery budget");
            pages.emplace_back();
        }
        auto& page=pages[index];
        auto shelf=std::find_if(page.shelves.begin(),page.shelves.end(),
            [&](const Shelf& candidate){return candidate.height>=h && candidate.used+w<=width;});
        if(shelf==page.shelves.end()) {
            page.shelves.push_back({page.used,0,h});
            page.used+=h;
            shelf=std::prev(page.shelves.end());
        }
        region->page=uint32_t(index);
        region->x=shelf->used+1;
        region->y=shelf->y+1;
        shelf->used+=w;
    }
    std::vector<vsg::ref_ptr<vsg::ubvec4Array2D>> images;
    for(size_t i=0;i<pages.size();++i) {
        auto page=vsg::ubvec4Array2D::create(width,height,vsg::ubvec4(255,255,255,255));
        page->properties.format=VK_FORMAT_R8G8B8A8_UNORM;
        images.push_back(page);
        output.layers.push_back(page);
        output.slotData.emplace_back(1,1,1,1);
    }
    std::array<double,256> linear{};
    for(size_t i=0;i<linear.size();++i) {
        const double c=i/255.;
        linear[i]=c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);
    }
    for(const auto& [key,region]:textures) {
        const auto& source=*region.surface;
        auto& image=*images[region.page];
        const int w=int(source.imagery_width),h=int(source.imagery_height);
        for(int y=-1;y<=h;++y)for(int x=-1;x<=w;++x) {
            const auto offset=4*(size_t(std::clamp(y,0,h-1))*w+std::clamp(x,0,w-1));
            vsg::ubvec4 pixel(0,0,0,255);
            for(int c=0;c<3;++c)
                pixel[c]=uint8_t(std::clamp(std::lround(linear[source.imagery_rgba[offset+c]]*region.tint[c]*255),0L,255L));
            image(int(region.x)+x,int(region.y)+y)=pixel;
        }
    }
}
}

VoxelSceneSensorData makeVoxelSceneSensorData(VoxelSceneSnapshotPtr snapshot) {
    if(!snapshot)throw std::invalid_argument("missing voxel scene snapshot");
    VoxelSceneSensorData result;result.snapshot=std::move(snapshot);
    result.textures.slotData.emplace_back(1,1,1,0);
    struct Texture {const EarthSurfaceSnapshot* surface;vsg::vec4 tint;};
    std::vector<Texture> textures;
    std::map<TextureKey,uint32_t> earthSlots;
    std::map<TextureKey,PackedTexture> packedTextures;
    uint32_t width=0,height=0,flatColors=0;
    for(const auto& [id,o]:result.snapshot->objects) {
        const auto& surface=o.surface();
        if(surface.imagery_rgba.empty()) {++flatColors;continue;}
        width=std::max(width,surface.imagery_width);height=std::max(height,surface.imagery_height);
        const auto& color=o.material.at("color_rgba");
        packedTextures.try_emplace(textureKey(o),PackedTexture{&surface,{color[0].get<float>(),color[1].get<float>(),color[2].get<float>(),0.f}});
    }
    const bool usePacking=size_t(width)*height*4*(packedTextures.size()+flatColors)>sensorImageBudget;
    if(usePacking)packSensorTextures(packedTextures,width+2,height+2,result.textures);
    for(const auto& [id,o]:result.snapshot->objects) {
        auto convert=[&](const nlohmann::json& m){
            rockyenv::sensor::RadarMaterial material;
            material.terrain=o.parentId.empty()&&(o.role=="terrain"||(o.role=="structure"&&o.body.motion=="static"));
            material.emissivity=m.at("emissivity").get<float>();
            material.rho_tv=m.at("rho_tv").get<float>();material.rho_swir=m.at("rho_swir").get<float>();
            material.eps_mwir=m.value("eps_mwir",material.emissivity);material.eps_lwir=m.value("eps_lwir",material.emissivity);
            if (const auto response = m.find("thermal_response"); response != m.end()) {
                auto field = [&](const char* key) { return response->at(key).get<float>(); };
                const float thermalModel = o.earth && o.parentId.empty() ? 2.f : 1.f;
                material.thermal_surface = {thermalModel,field("solar_gain_k"),field("inertia_h"),field("wet_cooling_k")};
                material.thermal_source = {field("source_gain_k"),field("source_start_s"),
                                           field("source_run_s"),field("source_tau_s")};
            }
            if(o.earth){const auto& parameters=o.earth->materials();const auto& radar=parameters.at("radar_x_params");
                material.dielectric_real=radar.at("dielectric_real").get<float>();material.dielectric_imag=radar.at("dielectric_imag").get<float>();
                material.surface_rough=radar.at("surface_rough").get<float>();material.sigma0_db=parameters.at("sar_c_params").at("sigma0_db").get<float>();}
            if(m.contains("radar")){const auto& r=m.at("radar");material.dielectric_real=r.at("dielectric_real");material.dielectric_imag=r.at("dielectric_imag");material.dielectric_model=r.at("dielectric_model");material.surface_rough=r.at("surface_rough");
                const double scale=r.at("rcs_scale");material.sigma0_db=r.at("sigma0_db").get<float>()+(scale>0?float(10*std::log10(scale)):0.f);
                if(scale==0){material.dielectric_real=1;material.dielectric_imag=0;material.dielectric_model=0;}}
            return material;
        };
        const auto material=convert(o.material);
        const auto& color=o.material.at("color_rgba");
        uint32_t colorSlot=uint32_t(result.textures.slotData.size());
        const vsg::vec4 tint(color[0].get<float>(),color[1].get<float>(),color[2].get<float>(),0.f);
        const auto& surface=o.surface();const bool textured=!surface.imagery_rgba.empty();
        const auto key=textureKey(o);auto previous=earthSlots.find(key);
        const PackedTexture* region=usePacking&&textured?&packedTextures.at(key):nullptr;
        if(region)colorSlot=region->page+1;
        else if(textured&&previous!=earthSlots.end())colorSlot=previous->second;
        else {
            result.textures.slotData.emplace_back(tint.r,tint.g,tint.b,textured?1.f:0.f);
            if(!usePacking)textures.push_back({textured?&surface:nullptr,tint});
            if(textured)earthSlots.emplace(key,colorSlot);
        }
        const auto materialId=uint32_t(result.materials.size());
        if(o.sensorMaterials)for(const auto& m:o.sensorMaterials->palette())result.materials.push_back(convert(m));
        else result.materials.push_back(material);
        if(surface.indices().empty())continue;
        auto draw=makeSurfaceDraw(surface,false);
        auto vertices=vsg::vec3Array::create();auto properties=vsg::vec2Array::create();auto ids=vsg::uintArray::create();
        auto uv=vsg::vec2Array::create();auto slots=vsg::uintArray::create();
        const bool smoothHeat = !surface.surface_normals.empty() &&
            std::any_of(result.materials.begin()+materialId,result.materials.end(),[](const auto& material) {
                return material.thermal_surface[0] > 1.5f;
            });
        std::vector<vsg::vec4> heatNormals;
        if(o.sensorMaterials){
            if(surface.triangleCells().size()*3!=surface.indices().size())throw std::invalid_argument("cell sensor materials require canonical triangle ownership");
            const auto dims=o.volume->dimensions();const auto origin=o.volume->gridOrigin();const auto cell=o.volume->resolution_m;
            std::vector<vsg::vec3> positions;std::vector<vsg::vec2> thermal,coords;std::vector<uint32_t> materials,layers;
            std::map<uint64_t,uint32_t> remap;auto indices=vsg::uintArray::create(surface.indices().size());size_t at=0;
            for(size_t triangle=0;triangle<surface.triangleCells().size();++triangle){const auto owner=surface.triangleCells()[triangle];
                const auto centre=origin+vsg::dvec3(owner%dims[0]+.5,owner/dims[0]%dims[1]+.5,owner/(size_t(dims[0])*dims[1])+.5)*cell;
                const auto palette=o.sensorMaterials->index(centre);const auto& m=o.sensorMaterials->palette()[palette];
                for(size_t j=0;j<3;++j){const auto original=surface.indices()[triangle*3+j];const uint64_t key=uint64_t(original)*256+palette;
                    auto [where,added]=remap.emplace(key,uint32_t(positions.size()));if(added){positions.push_back(vsg::vec3(surface.vertices()[original]));thermal.push_back({m.at("temperature_k").get<float>(),m.at("emissivity").get<float>()});materials.push_back(materialId+palette);coords.push_back(textured?surface.imagery_uv.at(original):vsg::vec2{});layers.push_back(colorSlot);
                        if (smoothHeat) heatNormals.emplace_back(surface.surface_normals.at(original),0.f);}
                    (*indices)[at++]=where->second;}}
            vertices=vsg::vec3Array::create(positions.size());std::copy(positions.begin(),positions.end(),vertices->begin());properties=vsg::vec2Array::create(thermal.size());std::copy(thermal.begin(),thermal.end(),properties->begin());ids=vsg::uintArray::create(materials.size());std::copy(materials.begin(),materials.end(),ids->begin());uv=vsg::vec2Array::create(coords.size());std::copy(coords.begin(),coords.end(),uv->begin());slots=vsg::uintArray::create(layers.size());std::copy(layers.begin(),layers.end(),slots->begin());
            draw->assignIndices(indices);draw->indexCount=uint32_t(indices->size());
        }else{
            const auto count=surface.vertices().size();vertices=draw->arrays.front()->data.cast<vsg::vec3Array>();
            properties=vsg::vec2Array::create(count,vsg::vec2(o.material.at("temperature_k").get<float>(),material.emissivity));ids=vsg::uintArray::create(count,materialId);
            uv=vsg::vec2Array::create(count,vsg::vec2{});if(textured)std::copy(surface.imagery_uv.begin(),surface.imagery_uv.end(),uv->begin());slots=vsg::uintArray::create(count,colorSlot);
            if(smoothHeat) for(const auto& normal:surface.surface_normals) heatNormals.emplace_back(normal,0.f);
        }
        if(region)for(auto& coord:*uv) {
            coord.x=(region->x+std::clamp(coord.x,0.f,1.f)*surface.imagery_width)/(width+2);
            coord.y=(region->y+std::clamp(coord.y,0.f,1.f)*surface.imagery_height)/(height+2);
        }
        if (!heatNormals.empty()) {
            auto shadingNormals = vsg::vec4Array::create(heatNormals.size());
            std::copy(heatNormals.begin(),heatNormals.end(),shadingNormals->begin());
            draw->assignArrays({vertices,properties,ids,uv,slots,shadingNormals});
        } else draw->assignArrays({vertices,properties,ids,uv,slots});
        result.instances.push_back({id,draw,o.pose(result.snapshot->tick,result.snapshot->fixedStep),true});
    }
    // The sensor texture pool consumes linear UNORM albedo. Display consumes
    // the same source bytes through an sRGB image. Decode BEFORE filtering;
    // treating sRGB bytes as reflectance would brighten measurement results.
    if(width&&height&&!usePacking) {
        auto linear=[](double n){n/=255.;return n<=.04045?n/12.92:std::pow((n+.055)/1.055,2.4);};
        for(const auto& texture:textures) {
            auto layer=vsg::ubvec4Array2D::create(width,height,vsg::ubvec4(255,255,255,255));layer->properties.format=VK_FORMAT_R8G8B8A8_UNORM;
            if(const auto* s=texture.surface)for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
                const double px=(x+.5)*s->imagery_width/width-.5,py=(y+.5)*s->imagery_height/height-.5;
                const int ix=int(std::floor(px)),iy=int(std::floor(py));const double u=px-ix,v=py-iy;vsg::ubvec4 pixel(0,0,0,255);
                for(int c=0;c<3;++c) {
                    double value=0;
                    for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
                        const auto a=std::clamp(ix+i,0,int(s->imagery_width)-1),b=std::clamp(iy+j,0,int(s->imagery_height)-1);
                        value+=linear(s->imagery_rgba[4*(size_t(b)*s->imagery_width+a)+c])*(i?u:1-u)*(j?v:1-v);
                    }
                    pixel[c]=uint8_t(std::clamp(std::lround(value*texture.tint[c]*255),0L,255L));
                }
                (*layer)(x,y)=pixel;
            }
            result.textures.layers.push_back(layer);
        }
    }
    return result;
}
}
