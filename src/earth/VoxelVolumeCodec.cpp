#include "VoxelTerrain.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <zlib.h>
namespace vsgraysim::earth {
std::size_t VoxelTerrain::occupiedCount() const {return occupied_count_;}
nlohmann::json VoxelTerrain::encodeVolume() const {
    auto runs=nlohmann::json::array();size_t start=0;
    while(start<occupied_.size()) {
        if(!occupied_[start]){++start;continue;}
        size_t end=start+1;while(end<occupied_.size()&&occupied_[end])++end;
        runs.push_back({start,end-start});start=end;
    }
    nlohmann::json result={{"format","voxel-volume-rle-v1"},{"dimensions",{columns_x_,layers_,columns_z_}},
        {"origin_m",{min_x,base_y_,min_z}},{"cell_size_m",resolution_m},{"occupied_runs",runs},
        {"revision",revision}};
    if(appearance_) {
        const auto& a=*appearance_;const char* digits="0123456789abcdef";bool compressedColour=false;std::vector<uint8_t> packedColour;
        if(a.rgba.size()>4*1024*1024){uLongf size=compressBound(a.rgba.size());packedColour.resize(size);if(compress2(packedColour.data(),&size,a.rgba.data(),a.rgba.size(),1)==Z_OK&&size*2<a.rgba.size()){packedColour.resize(size);compressedColour=true;}else packedColour.clear();}
        const auto& bytes=compressedColour?packedColour:a.rgba;std::string hex(bytes.size()*2,'0');for(size_t i=0;i<bytes.size();++i){hex[2*i]=digits[bytes[i]>>4];hex[2*i+1]=digits[bytes[i]&15];}
        auto faces=nlohmann::json::array();for(const auto& [key,tile]:a.faceTiles)faces.push_back({key,tile});
        auto corners=nlohmann::json::array();for(const auto& [key,c]:a.corners)corners.push_back({key,c.position.x,c.position.y,c.position.z,c.normal.x,c.normal.y,c.normal.z});
        auto pixelRuns=[](const std::vector<uint8_t>& pixels){auto runs=nlohmann::json::array();for(size_t i=0;i<pixels.size();) {size_t end=i+4;while(end<pixels.size()&&std::equal(pixels.begin()+i,pixels.begin()+i+4,pixels.begin()+end))end+=4;runs.push_back({(end-i)/4,pixels[i],pixels[i+1],pixels[i+2],pixels[i+3]});i=end;}return runs;};
        result["surface_appearance"]={{"origin_m",{a.origin.x,a.origin.y,a.origin.z}},{"dimensions",a.dimensions},{"cell_size_m",a.cellSize},
            {"width",a.width},{"height",a.height},{"face_tiles",faces}};
        result["surface_appearance"][compressedColour?"rgba_zlib_hex":"rgba_hex"]=std::move(hex);
        if(!corners.empty())result["surface_appearance"]["corners"]=corners;
        if(!a.mr_rgba.empty())result["surface_appearance"]["mr_runs"]=pixelRuns(a.mr_rgba);
        if(!a.normal_rgba.empty())result["surface_appearance"]["normal_runs"]=pixelRuns(a.normal_rgba);
        if(!a.emissive_rgba.empty())result["surface_appearance"]["emissive_runs"]=pixelRuns(a.emissive_rgba);
        if(a.detailed()){
            auto original=nlohmann::json::array();for(size_t i=0;i<a.originalOccupancy.size();){if(!a.originalOccupancy[i]){++i;continue;}size_t end=i+1;while(end<a.originalOccupancy.size()&&a.originalOccupancy[end])++end;original.push_back({i,end-i});i=end;}
            auto patches=nlohmann::json::array();for(const auto& p:a.patches){auto vertices=nlohmann::json::array();for(const auto& v:p.vertices)vertices.push_back({v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y});patches.push_back({p.cell,std::move(vertices)});}
            result["surface_appearance"]["source_occupied_runs"]=std::move(original);
            auto raw=nlohmann::json::to_cbor(patches);if(raw.size()>256ULL*1024*1024)throw std::runtime_error("source patch archive exceeds 256 MiB decoded geometry budget");uLongf length=compressBound(raw.size());std::vector<uint8_t> compressed(length);if(compress2(compressed.data(),&length,raw.data(),raw.size(),1)!=Z_OK)throw std::runtime_error("source patch compression failed");compressed.resize(length);std::string packed(length*2,'0');for(size_t i=0;i<length;++i){packed[2*i]=digits[compressed[i]>>4];packed[2*i+1]=digits[compressed[i]&15];}
            result["surface_appearance"]["source_patches_zlib_cbor"]={{"raw_bytes",raw.size()},{"hex",std::move(packed)}};
        }
    }
    return result;
}
std::shared_ptr<VoxelTerrain> VoxelTerrain::decodeVolume(const nlohmann::json& json) {
    if(json.at("format")!="voxel-volume-rle-v1")throw std::invalid_argument("unsupported voxel volume format");
    const auto& dims=json.at("dimensions");const auto& origin=json.at("origin_m");
    if(!dims.is_array()||dims.size()!=3||!origin.is_array()||origin.size()!=3)
        throw std::invalid_argument("invalid volume dimensions/origin");
    size_t volume=1;int n[3];vsg::dvec3 base;
    for(int a=0;a<3;++a) {
        if(!dims[a].is_number_integer()||dims[a].get<double>()<1||dims[a].get<double>()>4'000'000)
            throw std::invalid_argument("invalid volume dimension");
        n[a]=dims[a].get<int>();if(volume>4'000'000ULL/n[a])throw std::invalid_argument("volume exceeds 4M cells");
        volume*=n[a];base[a]=origin[a].get<double>();
    }
    const auto& runs=json.at("occupied_runs");if(!runs.is_array()||runs.size()>volume)
        throw std::invalid_argument("invalid volume runs");
    std::vector<bool> occupied(volume,false);size_t previousEnd=0;bool first=true;
    for(const auto& run:runs) {
        if(!run.is_array()||run.size()!=2||!run[0].is_number_integer()||!run[1].is_number_integer()||
           run[0].get<double>()<0||run[1].get<double>()<1||run[0].get<double>()>volume||run[1].get<double>()>volume)
            throw std::invalid_argument("invalid occupied run");
        const auto start=run[0].get<size_t>(),count=run[1].get<size_t>();
        if(start>volume-count||(!first&&start<=previousEnd))throw std::invalid_argument("noncanonical or overlapping volume runs");
        std::fill(occupied.begin()+start,occupied.begin()+start+count,true);previousEnd=start+count;first=false;
    }
    auto result=fromOccupancy(base,json.at("cell_size_m").get<double>(),n[0],n[1],n[2],occupied,false);
    if(json.contains("revision")) {
        const auto& r=json.at("revision");if(!r.is_number_integer()||r.get<double>()<0||r.get<double>()>1e12)
            throw std::invalid_argument("invalid volume revision");
        result->revision=r.get<uint64_t>();
    }
    if(json.contains("surface_appearance")) {
        const auto& encoded=json.at("surface_appearance");auto a=std::make_shared<VoxelSurfaceAppearance>();
        a->origin={encoded.at("origin_m").at(0).get<double>(),encoded.at("origin_m").at(1).get<double>(),encoded.at("origin_m").at(2).get<double>()};
        a->dimensions=encoded.at("dimensions").get<std::array<int,3>>();a->cellSize=encoded.at("cell_size_m").get<double>();
        a->width=encoded.at("width").get<uint32_t>();a->height=encoded.at("height").get<uint32_t>();
        if(!a->width||!a->height||uint64_t(a->width)*a->height>16'777'216||!std::isfinite(a->cellSize)||a->cellSize<=0||a->cellSize!=result->resolution_m)throw std::invalid_argument("invalid target appearance atlas");
        size_t cells=1;for(int axis=0;axis<3;++axis){if(!std::isfinite(a->origin[axis])||a->dimensions[axis]<1||a->dimensions[axis]>4'000'000||cells>4'000'000ULL/a->dimensions[axis])throw std::invalid_argument("invalid target appearance chart");cells*=a->dimensions[axis];}
        const bool compressedColour=encoded.contains("rgba_zlib_hex");if(compressedColour&&encoded.contains("rgba_hex"))throw std::invalid_argument("ambiguous colour atlas encoding");
        const auto& hex=encoded.at(compressedColour?"rgba_zlib_hex":"rgba_hex").get_ref<const std::string&>();const size_t byteCount=size_t(a->width)*a->height*4;
        if(hex.empty()||hex.size()%2||(compressedColour?hex.size()>byteCount*2:hex.size()!=byteCount*2))throw std::invalid_argument("invalid target atlas byte count");
        auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("invalid target atlas encoding");};
        std::vector<uint8_t> bytes(hex.size()/2);for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(digit(hex[2*i])*16+digit(hex[2*i+1]));
        if(compressedColour){a->rgba.resize(byteCount);uLongf length=byteCount;if(uncompress(a->rgba.data(),&length,bytes.data(),bytes.size())!=Z_OK||length!=byteCount)throw std::invalid_argument("invalid compressed colour atlas");}else a->rgba=std::move(bytes);
        const auto& faces=encoded.at("face_tiles");if(!faces.is_array()||faces.size()>2'000'000)throw std::invalid_argument("invalid target face map");
        for(const auto& pair:faces) {
            if(!pair.is_array()||pair.size()!=2||!pair[0].is_number_integer()||!pair[1].is_number_integer()||pair[0].get<double>()<0||pair[1].get<double>()<0)throw std::invalid_argument("invalid target face binding");
            const auto key=pair[0].get<uint64_t>();const auto tile=pair[1].get<uint32_t>();
            if(key>=cells*6||!tile||tile>=uint64_t(a->width/10)*(a->height/10)||!a->faceTiles.emplace(key,tile).second)throw std::invalid_argument("out of range target face binding");
        }
        if(encoded.contains("corners")) {
            const auto& list=encoded.at("corners");if(!list.is_array()||list.size()>2'000'000)throw std::invalid_argument("invalid surface corners");
            for(const auto& c:list) {
                if(!c.is_array()||c.size()!=7||!c[0].is_number_integer())throw std::invalid_argument("invalid surface corner");
                const auto key=c[0].get<uint64_t>();VoxelSurfaceAppearance::Corner corner{{c[1].get<double>(),c[2].get<double>(),c[3].get<double>()},{c[4].get<float>(),c[5].get<float>(),c[6].get<float>()}};
                const auto nx=uint64_t(a->dimensions[0]+1),ny=uint64_t(a->dimensions[1]+1);const auto grid=a->origin+vsg::dvec3(key%nx,key/nx%ny,key/(nx*ny))*a->cellSize;
                if(vsg::length(corner.position-grid)>std::sqrt(3.)*a->cellSize+1e-6)throw std::invalid_argument("surface corner exceeds source cell neighbourhood");
                if(key>=uint64_t(a->dimensions[0]+1)*(a->dimensions[1]+1)*(a->dimensions[2]+1)||!std::isfinite(vsg::length(corner.position))||!std::isfinite(vsg::length(corner.normal))||std::abs(vsg::length(corner.normal)-1)>1e-4||!a->corners.emplace(key,corner).second)throw std::invalid_argument("invalid reconstructed surface vertex");
            }
        }
        if(encoded.contains("source_patches")||encoded.contains("source_patches_zlib_cbor")){
            nlohmann::json unpacked;
            if(encoded.contains("source_patches_zlib_cbor")){const auto& archive=encoded.at("source_patches_zlib_cbor");const auto& size=archive.at("raw_bytes");if(!size.is_number_integer()||size.get<double>()<1||size.get<double>()>256ULL*1024*1024)throw std::invalid_argument("source geometry decompression budget exceeded");const auto hex=archive.at("hex").get<std::string>();if(hex.empty()||hex.size()%2||hex.size()>256ULL*1024*1024)throw std::invalid_argument("invalid compressed source geometry");std::vector<uint8_t> packed(hex.size()/2);for(size_t i=0;i<packed.size();++i)packed[i]=uint8_t(digit(hex[2*i])*16+digit(hex[2*i+1]));uLongf length=size.get<uLongf>();std::vector<uint8_t> raw(length);if(uncompress(raw.data(),&length,packed.data(),packed.size())!=Z_OK||length!=raw.size())throw std::invalid_argument("invalid compressed source geometry payload");unpacked=nlohmann::json::from_cbor(raw);}
            const auto& patches=encoded.contains("source_patches")?encoded.at("source_patches"):unpacked;if(!patches.is_array()||patches.empty()||patches.size()>1'300'000)throw std::invalid_argument("invalid source patch table");
            a->originalOccupancy.resize(cells,false);const auto& original=encoded.at("source_occupied_runs");size_t previous=0;bool first=true;
            if(!original.is_array()||original.size()>cells)throw std::invalid_argument("invalid original source occupancy");
            for(const auto& r:original){if(!r.is_array()||r.size()!=2||!r[0].is_number_unsigned()&&!r[0].is_number_integer()||!r[1].is_number_unsigned()&&!r[1].is_number_integer()||r[0].get<double>()<0||r[1].get<double>()<1||r[0].get<double>()>cells||r[1].get<double>()>cells)throw std::invalid_argument("invalid original source run");const size_t begin=r[0].get<size_t>(),count=r[1].get<size_t>();if(begin>cells-count||(!first&&begin<=previous))throw std::invalid_argument("overlapping original source runs");std::fill(a->originalOccupancy.begin()+begin,a->originalOccupancy.begin()+begin+count,true);previous=begin+count;first=false;}
            size_t vertices=0;uint32_t previousCell=0;for(const auto& p:patches){if(!p.is_array()||p.size()!=2||!p[0].is_number_integer()||p[0].get<double>()<0||p[0].get<double>()>=cells||!p[1].is_array()||p[1].size()<3||p[1].size()>9)throw std::invalid_argument("invalid source patch");VoxelSurfaceAppearance::SurfacePatch patch;patch.cell=p[0].get<uint32_t>();if(!a->originalOccupancy[patch.cell]||(!a->patches.empty()&&patch.cell<previousCell))throw std::invalid_argument("invalid source patch owner/order");previousCell=patch.cell;
                const auto low=a->origin+vsg::dvec3(patch.cell%a->dimensions[0],patch.cell/a->dimensions[0]%a->dimensions[1],patch.cell/(size_t(a->dimensions[0])*a->dimensions[1]))*a->cellSize;
                for(const auto& v:p[1]){if(!v.is_array()||v.size()!=8||++vertices>3'900'000)throw std::invalid_argument("invalid source patch vertex budget");for(const auto& x:v)if(!x.is_number()||!std::isfinite(x.get<double>()))throw std::invalid_argument("invalid source patch coordinate");VoxelSurfaceAppearance::SurfaceVertex q{{v[0].get<double>(),v[1].get<double>(),v[2].get<double>()},{v[3].get<float>(),v[4].get<float>(),v[5].get<float>()},{v[6].get<float>(),v[7].get<float>()}};for(int axis=0;axis<3;++axis)if(q.position[axis]<low[axis]-a->cellSize*1e-9||q.position[axis]>low[axis]+a->cellSize*(1+1e-9))throw std::invalid_argument("source patch escaped owning cell");if(std::abs(vsg::length(q.normal)-1)>1e-4||q.uv.x<0||q.uv.x>1||q.uv.y<0||q.uv.y>1)throw std::invalid_argument("invalid source normal/UV");patch.vertices.push_back(q);}
                a->patches.push_back(std::move(patch));
            }
        }
        auto decodePixels=[&](const char* name,std::vector<uint8_t>& pixels){if(!encoded.contains(name))return;const auto& runs=encoded.at(name);if(!runs.is_array()||runs.size()>a->rgba.size()/4)throw std::invalid_argument("invalid PBR runs");for(const auto& run:runs){if(!run.is_array()||run.size()!=5||!run[0].is_number_integer())throw std::invalid_argument("invalid PBR run");const auto count=run[0].get<uint64_t>();if(!count||count>(a->rgba.size()-pixels.size())/4)throw std::invalid_argument("PBR run outside atlas");uint8_t rgba[4];for(int c=0;c<4;++c){if(!run[c+1].is_number_integer()||run[c+1].get<double>()<0||run[c+1].get<double>()>255)throw std::invalid_argument("invalid PBR channel");rgba[c]=run[c+1].get<uint8_t>();}for(size_t i=0;i<count;++i)pixels.insert(pixels.end(),rgba,rgba+4);}if(pixels.size()!=a->rgba.size())throw std::invalid_argument("incomplete PBR atlas");};
        decodePixels("mr_runs",a->mr_rgba);decodePixels("normal_runs",a->normal_rgba);decodePixels("emissive_runs",a->emissive_rgba);
        result->setAppearance(a);
    }
    return result;
}
}
