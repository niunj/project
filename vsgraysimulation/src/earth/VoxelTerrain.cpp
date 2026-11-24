#include "VoxelTerrain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vsgraysim::earth {
namespace {
using Cell = std::array<int, 3>;

Cell integerTriple(const nlohmann::json& value, int minimum, const Cell& maximum) {
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("voxel coordinate requires three integers");
    Cell result{};
    for (int axis = 0; axis < 3; ++axis) {
        if (!value[axis].is_number_integer() || value[axis].get<double>() < minimum ||
            value[axis].get<double>() > maximum[axis])
            throw std::invalid_argument("voxel coordinate outside supported dimensions");
        result[axis] = value[axis].get<int>();
    }
    return result;
}
}

std::shared_ptr<VoxelTerrain> VoxelTerrain::load(const nlohmann::json& package,
                                                 const nlohmann::json& config) {
    const auto dims = integerTriple(package.at("dimensions"), 1, {256, 256, 256});
    const auto& cells = package.at("occupied");
    const auto& origin = package.at("origin_m");
    const double size = package.at("cell_size_m").get<double>();
    const size_t volume = size_t(dims[0]) * dims[1] * dims[2];
    if (!origin.is_array() || origin.size() != 3 || !std::isfinite(size) || size <= 0 ||
        !cells.is_array() || cells.empty() || cells.size() > 32768 || volume > 1048576)
        throw std::invalid_argument("invalid or oversized voxel terrain");
    if (config.contains("coverage") || config.contains("grid_size"))
        throw std::invalid_argument("voxel coverage and resolution are defined by the volume");

    vsg::dvec3 base;
    for (int axis = 0; axis < 3; ++axis) {
        base[axis] = origin.at(axis).get<double>();
        // Rendering and collision use local float coordinates. Reject cells
        // that collapse when their corners are converted to that representation.
        float previous = static_cast<float>(base[axis]);
        for (int i = 1; i <= dims[axis]; ++i) {
            float next = static_cast<float>(base[axis] + i * size);
            if (!std::isfinite(previous) || !std::isfinite(next) || next <= previous)
                throw std::invalid_argument("voxel terrain exceeds local coordinate precision");
            previous = next;
        }
    }
    auto index = [&](int x, int y, int z) { return (z * dims[1] + y) * dims[0] + x; };
    std::vector<bool> occupied(volume);
    for (const auto& item : cells) {
        const auto cell = integerTriple(item, 0, {dims[0]-1, dims[1]-1, dims[2]-1});
        const auto i = index(cell[0], cell[1], cell[2]);
        if (occupied[i]) throw std::invalid_argument("duplicate occupied voxel");
        occupied[i] = true;
    }

    auto surface = std::make_shared<VoxelTerrain>();
    surface->strict = config.value("strict", true);
    surface->provenance_assumed = config.value("provenance_assumed", false);
    surface->resolution_m = size;
    surface->columns_x_ = dims[0];
    surface->columns_z_ = dims[2];
    surface->min_x = base.x;
    surface->min_z = base.z;
    surface->max_x = base.x + size * dims[0];
    surface->max_z = base.z + size * dims[2];
    surface->min_height = std::numeric_limits<double>::infinity();
    surface->max_height = -surface->min_height;
    surface->top_.assign(dims[0] * dims[2], -surface->min_height);

    // A fixed traversal and winding yield identical surfaces for reordered
    // occupancy lists. Emit each exterior quad only once, including cave roofs.
    for (int z = 0; z < dims[2]; ++z) for (int y = 0; y < dims[1]; ++y)
        for (int x = 0; x < dims[0]; ++x) {
            if (!occupied[index(x,y,z)]) continue;
            Cell cell{x,y,z};
            auto& top = surface->top_[z * dims[0] + x];
            if (!std::isfinite(top)) ++surface->valid_samples;
            top = base.y + (y + 1) * size;
            surface->min_height = std::min(surface->min_height, base.y + y * size);
            surface->max_height = std::max(surface->max_height, top);
            for (int axis = 0; axis < 3; ++axis) for (int side : {-1, 1}) {
                Cell adjacent = cell;
                adjacent[axis] += side;
                if (adjacent[axis] >= 0 && adjacent[axis] < dims[axis] &&
                    occupied[index(adjacent[0], adjacent[1], adjacent[2])]) continue;

                Cell corner = cell;
                if (side > 0) ++corner[axis];
                int u = (axis + 1) % 3, v = (axis + 2) % 3;
                if (side < 0) std::swap(u,v);
                const uint32_t first = static_cast<uint32_t>(surface->vertices_.size());
                for (const auto& offset : std::array<std::array<int,2>,4>{{{0,0}, {1,0}, {1,1}, {0,1}}}) {
                    Cell grid = corner;
                    grid[u] += offset[0];
                    grid[v] += offset[1];
                    surface->vertices_.push_back(base + vsg::dvec3(grid[0],grid[1],grid[2]) * size);
                }
                surface->indices_.insert(surface->indices_.end(),
                    {first,first+1,first+2,first,first+2,first+3});
            }
        }
    if (surface->strict && surface->valid_samples != surface->top_.size())
        throw std::invalid_argument("strict voxel terrain has uncovered ground columns");
    surface->base_y_=base.y;surface->layers_=dims[1];surface->occupied_=std::move(occupied);
    surface->rebuildMesh();
    return surface;
}

std::shared_ptr<VoxelTerrain> VoxelTerrain::fromOccupancy(
    const vsg::dvec3& min, double cell, int nx, int ny, int nz,
    const std::vector<bool>& occupied, bool strict) {
    if (nx < 1 || ny < 1 || nz < 1 || cell <= 0 || !std::isfinite(cell) ||
        size_t(nx) * ny > 4'000'000ULL / nz || occupied.size() != size_t(nx) * ny * nz)
        throw std::invalid_argument("invalid occupancy grid for voxel terrain");
    const int dimensions[3]={nx,ny,nz};
    for(int axis=0;axis<3;++axis) {
        float previous=float(min[axis]);
        for(int i=1;i<=dimensions[axis];++i) {
            const float next=float(min[axis]+i*cell);
            if(!std::isfinite(previous)||!std::isfinite(next)||next<=previous)
                throw std::invalid_argument("voxel grid exceeds local coordinate precision");
            previous=next;
        }
    }
    auto surface = std::make_shared<VoxelTerrain>();
    surface->strict = strict;
    surface->resolution_m = cell;
    surface->columns_x_ = nx;
    surface->columns_z_ = nz;
    surface->min_x = min.x;
    surface->min_z = min.z;
    surface->max_x = min.x + cell * nx;
    surface->max_z = min.z + cell * nz;
    surface->min_height = std::numeric_limits<double>::infinity();
    surface->max_height = -surface->min_height;
    surface->top_.assign(size_t(nx) * nz, std::numeric_limits<double>::quiet_NaN());
    auto at = [&](int x, int y, int z) { return (size_t(z) * ny + y) * nx + x; };
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        if (!occupied[at(x, y, z)]) continue;
        auto& top = surface->top_[size_t(z) * nx + x];
        if (!std::isfinite(top)) ++surface->valid_samples;
        top = min.y + (y + 1) * cell;
        surface->min_height = std::min(surface->min_height, min.y + y * cell);
        surface->max_height = std::max(surface->max_height, top);
    }
    if(!surface->valid_samples)surface->min_height=surface->max_height=min.y;
    surface->base_y_ = min.y;
    surface->layers_ = ny;
    surface->occupied_ = occupied;
    surface->rebuildMesh();
    return surface;
}

SurfaceSample VoxelTerrain::sampleLocal(double x, double z) const {
    return waterBedLocal(x,z,std::numeric_limits<double>::infinity());
}
SurfaceSample VoxelTerrain::waterBedLocal(double x,double z,double level) const {
    SurfaceSample sample;
    sample.revision = revision;
    sample.resolution_m = resolution_m;
    if (!std::isfinite(x) || !std::isfinite(z) || x < min_x || x > max_x ||
        z < min_z || z > max_z) return sample;
    // Exposed voxel faces are closed polygons. At an exact shared edge a
    // vertical ray touches both columns, so the highest incident face wins.
    auto incident = [&](double coordinate, double origin, int count) {
        const int nearest = std::clamp(int(std::round((coordinate-origin)/resolution_m)),0,count);
        if (coordinate == origin + nearest * resolution_m)
            return std::pair{std::max(0, nearest-1), std::min(nearest,count-1)};
        const int cell = std::clamp(int((coordinate-origin)/resolution_m),0,count-1);
        return std::pair{cell,cell};
    };
    const auto [x0,x1] = incident(x,min_x,columns_x_);
    const auto [z0,z1] = incident(z,min_z,columns_z_);
    double height = -std::numeric_limits<double>::infinity();
    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix) {
            const double top=top_[iz*columns_x_+ix];
            if(top<=level) {height=std::max(height,top);continue;}
            bool connected=false;
            for(int y=0;y<layers_;++y) {
                if(!occupied_[(iz*layers_+y)*columns_x_+ix]) {connected=false;continue;}
                connected=connected || base_y_+y*resolution_m<=level;
                if(connected) height=std::max(height,base_y_+(y+1)*resolution_m);
            }
        }
    if (std::isfinite(height)) {
        sample.height = height;
        sample.status = provenance_assumed ? SurfaceStatus::Coarse : SurfaceStatus::Ready;
    }
    return sample;
}
void VoxelTerrain::rebuildMesh() {
    invalidateRayIndex();
    occupied_count_=0;vsg::dvec3 centreSum{};
    imagery_uv.clear();surface_normals.clear();
    if(appearance_){imagery_rgba=appearance_->rgba;imagery_width=appearance_->width;imagery_height=appearance_->height;normal_rgba=appearance_->normal_rgba;mr_rgba=appearance_->mr_rgba;}
    vertices_.clear();
    indices_.clear();triangle_cells_.clear();
    const double size = resolution_m;
    const vsg::dvec3 base(min_x, base_y_, min_z);
    for (int z = 0; z < columns_z_; ++z) for (int y = 0; y < layers_; ++y)
        for (int x = 0; x < columns_x_; ++x) {
            const auto at=[&](int ix,int iy,int iz){
                return (iz*layers_+iy)*columns_x_+ix;
            };
            if (!occupied_[at(x,y,z)]) continue;
            ++occupied_count_;centreSum+=vsg::dvec3(x+.5,y+.5,z+.5);
            for (int axis = 0; axis < 3; ++axis) for (int side : {-1, 1}) {
                std::array<int,3> adjacent{x, y, z};
                adjacent[axis] += side;
                if (adjacent[0] >= 0 && adjacent[0] < columns_x_ &&
                    adjacent[1] >= 0 && adjacent[1] < layers_ &&
                    adjacent[2] >= 0 && adjacent[2] < columns_z_ &&
                    occupied_[at(adjacent[0], adjacent[1], adjacent[2])]) continue;
                // Source patches already describe the original exterior. Only
                // newly exposed voxel cuts receive neutral cell-face geometry.
                if(appearance_&&appearance_->detailed()){
                    const auto neighbour=base+vsg::dvec3(adjacent[0]+.5,adjacent[1]+.5,adjacent[2]+.5)*size;
                    const auto original=appearance_->cellAt(neighbour);
                    if(original<0||!appearance_->originalOccupancy[size_t(original)])continue;
                }
                std::array<int,3> corner{x, y, z};
                if (side > 0) ++corner[axis];
                int u = (axis + 1) % 3, v = (axis + 2) % 3;
                if (side < 0) std::swap(u, v);
                if(vertices_.size()+4>2'000'000)throw std::runtime_error("voxel mesh exceeds 2M vertex budget");
                const uint32_t first = static_cast<uint32_t>(vertices_.size());
                for (const auto& offset : std::array<std::array<int,2>,4>{{{0,0}, {1,0}, {1,1}, {0,1}}}) {
                    std::array<int,3> grid = corner;
                    grid[u] += offset[0];
                    grid[v] += offset[1];
                    auto point=base+vsg::dvec3(grid[0],grid[1],grid[2])*size;
                    vsg::vec3 normal{};normal[axis]=float(side);
                    if(appearance_&&!appearance_->corners.empty()) {
                        if(const auto* source=appearance_->corner(point)){point=source->position;
                            const auto face=appearance_->faceTiles.find(appearance_->key(int(std::floor((base.x+(x+.5)*size-appearance_->origin.x)/size)),int(std::floor((base.y+(y+.5)*size-appearance_->origin.y)/size)),int(std::floor((base.z+(z+.5)*size-appearance_->origin.z)/size)),axis*2+(side>0)));
                            if(face!=appearance_->faceTiles.end())normal=source->normal;
                        }
                    }
                    vertices_.push_back(point);if(appearance_&&appearance_->reconstructed())surface_normals.push_back(normal);
                }
                if(appearance_) {
                    const auto uv=appearance_->uv(base+vsg::dvec3(x+.5,y+.5,z+.5)*size,axis*2+(side>0));
                    imagery_uv.insert(imagery_uv.end(),uv.begin(),uv.end());
                }
                indices_.insert(indices_.end(),
                    {first, first+1, first+2, first, first+2, first+3});
                triangle_cells_.insert(triangle_cells_.end(),2,uint32_t(at(x,y,z)));
            }
        }
    if(appearance_&&appearance_->detailed())for(const auto& patch:appearance_->patches){
        const auto& chart=*appearance_;const auto key=patch.cell;
        const auto centre=chart.origin+vsg::dvec3(key%chart.dimensions[0]+.5,key/chart.dimensions[0]%chart.dimensions[1]+.5,key/(size_t(chart.dimensions[0])*chart.dimensions[1])+.5)*size;
        const auto cell=(centre-base)/size;const int x=int(std::floor(cell.x)),y=int(std::floor(cell.y)),z=int(std::floor(cell.z));
        if(x<0||y<0||z<0||x>=columns_x_||y>=layers_||z>=columns_z_||!occupied_[(size_t(z)*layers_+y)*columns_x_+x])continue;
        if(vertices_.size()+patch.vertices.size()>4'000'000)throw std::runtime_error("voxel source/cut mesh exceeds 4M detail vertex budget");
        const uint32_t first=uint32_t(vertices_.size());for(const auto& p:patch.vertices){vertices_.push_back(p.position);surface_normals.push_back(p.normal);imagery_uv.push_back(p.uv);}
        for(uint32_t j=1;j+1<patch.vertices.size();++j){indices_.insert(indices_.end(),{first,first+j,first+j+1});triangle_cells_.push_back(uint32_t((size_t(z)*layers_+y)*columns_x_+x));}
    }
    centre_of_mass_=occupied_count_?base+centreSum*(size/double(occupied_count_)):base;
}

std::optional<bool> VoxelTerrain::occupiedGeodetic(double lat,double lon,double height) const {
    if(!std::isfinite(lat)||std::abs(lat)>90 || !std::isfinite(lon)||!std::isfinite(height)) return std::nullopt;
    const auto p=simcore::geo::ecefToLocal(anchor,simcore::geo::geodeticToECEF(lat,lon,height));
    if(p.x<min_x || p.x>=max_x || p.z<min_z || p.z>=max_z) return std::nullopt;
    if(p.y<base_y_ || p.y>=base_y_+layers_*resolution_m) return false;
    const int x=int((p.x-min_x)/resolution_m),z=int((p.z-min_z)/resolution_m),y=int((p.y-base_y_)/resolution_m);
    return occupied_[(z*layers_+y)*columns_x_+x];
}

}
