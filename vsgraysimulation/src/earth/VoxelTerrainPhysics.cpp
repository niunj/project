#include "VoxelTerrainPhysics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vsgraysim::earth {

VoxelTerrainPhysics::VoxelTerrainPhysics(std::shared_ptr<const VoxelTerrain> source)
    : source_(std::move(source)) {
    if (!source_) throw std::invalid_argument("VoxelTerrainPhysics requires a source terrain");
    columns_x_ = source_->columns_x_;
    columns_z_ = source_->columns_z_;
    layers_ = source_->layers_;
    cell_size_ = source_->resolution_m;
    base_y_ = source_->base_y_;
    min_x_ = source_->min_x; max_x_ = source_->max_x;
    min_z_ = source_->min_z; max_z_ = source_->max_z;
    revision_=source_->revision;
    occupied_ = source_->occupied_;
    top_ = source_->top_;
}

std::optional<bool> VoxelTerrainPhysics::occupiedLocal(double x, double y, double z) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        x < min_x_ || x >= max_x_ || z < min_z_ || z >= max_z_ ||
        y < base_y_ || y >= base_y_ + layers_ * cell_size_) return std::nullopt;
    const int ix = int((x - min_x_) / cell_size_);
    const int iz = int((z - min_z_) / cell_size_);
    const int iy = int((y - base_y_) / cell_size_);
    return occupied_[index(ix, iy, iz)];
}

vsg::dvec3 VoxelTerrainPhysics::voxelCentre(int x, int y, int z) const {
    return {min_x_ + (x + .5) * cell_size_,
            base_y_ + (y + .5) * cell_size_,
            min_z_ + (z + .5) * cell_size_};
}

bool VoxelTerrainPhysics::rayVoxel(const vsg::dvec3& origin, const vsg::dvec3& ray,
                                   double max_dist, vsg::dvec3& hit, vsg::dvec3& normal) const {
    // Clip in world metres before converting to half-open voxel indices.
    const vsg::dvec3 minimum{min_x_,base_y_,min_z_};
    const int dimensions[3]={columns_x_,layers_,columns_z_};
    double entry=0.,exit=max_dist;int entryAxis=-1;
    for(int axis=0;axis<3;++axis) {
        const double lo=minimum[axis],hi=lo+dimensions[axis]*cell_size_;
        if(std::abs(ray[axis])<1e-14) {
            if(origin[axis]<lo || origin[axis]>=hi)return false;
            continue;
        }
        double near=(lo-origin[axis])/ray[axis],far=(hi-origin[axis])/ray[axis];
        if(near>far)std::swap(near,far);
        if(near>entry){entry=near;entryAxis=axis;}
        exit=std::min(exit,far);
        if(entry>exit)return false;
    }
    std::array<int,3> grid{},step{};vsg::dvec3 next,delta;
    for(int axis=0;axis<3;++axis) {
        const double coordinate=(origin[axis]+ray[axis]*entry-minimum[axis])/cell_size_;
        grid[axis]=int(std::floor(coordinate));
        if(ray[axis]<0 && std::abs(coordinate-std::round(coordinate))<1e-10)grid[axis]=int(std::round(coordinate))-1;
        grid[axis]=std::clamp(grid[axis],0,dimensions[axis]-1);
        step[axis]=ray[axis]>0?1:-1;
        if(std::abs(ray[axis])<1e-14)next[axis]=delta[axis]=std::numeric_limits<double>::infinity();
        else {
            next[axis]=(minimum[axis]+(grid[axis]+(step[axis]>0?1:0))*cell_size_-origin[axis])/ray[axis];
            delta[axis]=std::abs(cell_size_/ray[axis]);
        }
    }
    normal={0,0,0};if(entryAxis>=0)normal[entryAxis]=-step[entryAxis];
    double distance=entry;
    for(int iteration=0;iteration<columns_x_+layers_+columns_z_+3 && distance<=exit;++iteration) {
        if(occupied(grid[0],grid[1],grid[2])) {hit=origin+ray*distance;return true;}
        const double crossing=std::min({next.x,next.y,next.z});
        if(!std::isfinite(crossing)||crossing>exit)return false;
        normal={0,0,0};bool first=true;
        for(int axis=0;axis<3;++axis)if(std::abs(next[axis]-crossing)<1e-10*std::max(1.,std::abs(crossing))) {
            grid[axis]+=step[axis];next[axis]+=delta[axis];
            if(first){normal[axis]=-step[axis];first=false;}
            if(grid[axis]<0||grid[axis]>=dimensions[axis])return false;
        }
        distance=crossing;
    }
    return false;
}

bool VoxelTerrainPhysics::intersectLocal(const vsg::dvec3& origin, const vsg::dvec3& direction,
                                         double max_distance, vsg::dvec3& hit, vsg::dvec3& normal) const {
    if (!std::isfinite(vsg::length(origin)) || !std::isfinite(max_distance) || !std::isfinite(vsg::length(direction)) || vsg::length(direction) < 1e-12 || max_distance < 0) return false;
    if(source_->appearance()&&source_->appearance()->reconstructed())return source_->EarthSurfaceSnapshot::intersectLocal(origin,direction,max_distance,hit,normal);
    return rayVoxel(origin, direction / vsg::length(direction), max_distance, hit, normal);
}

bool VoxelTerrainPhysics::sweepSphere(const vsg::dvec3& origin, const vsg::dvec3& direction,
                                      double radius, double max_distance,
                                      vsg::dvec3& hit, vsg::dvec3& normal) const {
    const double length=vsg::length(direction);
    if(!std::isfinite(vsg::length(origin))||!std::isfinite(length)||length<1e-12||
       !std::isfinite(radius)||radius<0||!std::isfinite(max_distance)||max_distance<0)return false;
    if(radius==0)return intersectLocal(origin,direction,max_distance,hit,normal);
    const auto ray=direction/length,end=origin+ray*max_distance;
    const vsg::dvec3 minimum{min_x_,base_y_,min_z_};
    const int dimensions[3]={columns_x_,layers_,columns_z_};
    int first[3],last[3];
    for(int a=0;a<3;++a) {
        double lo=(std::min(origin[a],end[a])-radius-minimum[a])/cell_size_;
        double hi=(std::max(origin[a],end[a])+radius-minimum[a])/cell_size_;
        if(hi<0||lo>dimensions[a])return false;
        first[a]=int(std::clamp(std::floor(lo),0.,double(dimensions[a]-1)));
        last[a]=int(std::clamp(std::floor(hi),0.,double(dimensions[a]-1)));
    }
    double best=max_distance;bool found=false;
    for(int z=first[2];z<=last[2];++z)for(int y=first[1];y<=last[1];++y)for(int x=first[0];x<=last[0];++x) {
        if(!occupied(x,y,z))continue;
        const auto low=minimum+vsg::dvec3(x,y,z)*cell_size_;
        const auto high=low+vsg::dvec3(cell_size_,cell_size_,cell_size_);
        // Squared distance to a box is piecewise quadratic along the ray.
        // Split at box planes and solve for the first sphere contact exactly.
        std::vector<double> cuts{0.,best};
        for(int a=0;a<3;++a)if(std::abs(ray[a])>1e-14)
            for(double plane:{low[a],high[a]}) {
                double t=(plane-origin[a])/ray[a];if(t>0&&t<best)cuts.push_back(t);
            }
        std::sort(cuts.begin(),cuts.end());
        for(size_t segment=0;segment+1<cuts.size();++segment) {
            const double from=cuts[segment],to=cuts[segment+1],mid=.5*(from+to);
            double A=0,B=0,C=-radius*radius;
            for(int a=0;a<3;++a) {
                double at=origin[a]+ray[a]*mid,offset;
                if(at<low[a])offset=origin[a]-low[a];
                else if(at>high[a])offset=origin[a]-high[a];else continue;
                A+=ray[a]*ray[a];B+=offset*ray[a];C+=offset*offset;
            }
            double contact=from;
            if(A*from*from+2*B*from+C>1e-12) {
                const double discriminant=B*B-A*C;
                if(A<1e-20||discriminant<0)continue;
                contact=(-B-std::sqrt(std::max(0.,discriminant)))/A;
                if(contact<from-1e-10||contact>to+1e-10)continue;
            }
            if(contact<=best) {
                best=std::clamp(contact,from,to);found=true;
                const auto centre=origin+ray*best;
                for(int a=0;a<3;++a)hit[a]=std::clamp(centre[a],low[a],high[a]);
                const auto offset=centre-hit;
                normal=vsg::length(offset)>1e-12?vsg::normalize(offset):-ray;
            }
            break;
        }
    }
    return found;
}

void VoxelTerrainPhysics::setOccupied(int x, int y, int z, bool value) {
    if (x < 0 || x >= columns_x_ || y < 0 || y >= layers_ || z < 0 || z >= columns_z_) return;
    const int i = index(x, y, z);
    if (occupied_[i] == value) return;
    occupied_[i] = value;
    recomputeTop(x, z);
}

void VoxelTerrainPhysics::recomputeTop(int x, int z) {
    const int column = z * columns_x_ + x;
    double top = -std::numeric_limits<double>::infinity();
    for (int y = 0; y < layers_; ++y)
        if (occupied_[index(x, y, z)]) top = base_y_ + (y + 1) * cell_size_;
    top_[column] = top;
}

std::size_t VoxelTerrainPhysics::damage(const DamageEvent& event) {
    if (!std::isfinite(vsg::length(event.point)) || !std::isfinite(event.energy)||
        !std::isfinite(event.radius)||event.radius<0||(event.energy<=0&&event.radius==0)) return 0;
    // Crater radius from energy (simple blast scaling: r ∝ E^(1/3)).
    const double radius = event.radius > 0 ? event.radius
        : std::max(cell_size_ * 1.5, std::cbrt(std::max(event.energy, 1.0)) * .12);
    if (radius <= 0) return 0;
    const auto centre = event.point;
    std::array<int,3> gridMin{},gridMax{};
    const vsg::dvec3 minimum{min_x_,base_y_,min_z_};const int dimensions[3]={columns_x_,layers_,columns_z_};
    for(int axis=0;axis<3;++axis) {
        const double lo=std::floor((centre[axis]-radius-minimum[axis])/cell_size_);
        const double hi=std::floor((centre[axis]+radius-minimum[axis])/cell_size_);
        if(hi<0||lo>=dimensions[axis])return 0;
        gridMin[axis]=int(std::clamp(lo,0.,double(dimensions[axis]-1)));
        gridMax[axis]=int(std::clamp(hi,0.,double(dimensions[axis]-1)));
    }
    std::size_t removed = 0;
    for (int z = gridMin[2]; z <= gridMax[2]; ++z)
        for (int x = gridMin[0]; x <= gridMax[0]; ++x) {
            const auto before=removed;
            for (int y = gridMin[1]; y <= gridMax[1]; ++y) {
                if (!occupied(x, y, z)) continue;
                const auto c = voxelCentre(x, y, z);
                if (vsg::length(c - centre) <= radius) {
                    occupied_[index(x,y,z)]=false;
                    ++removed;
                }
            }
            if (removed!=before) recomputeTop(x,z);
        }
    destroyed_+=removed;
    if (removed) ++revision_;
    return removed;
}

std::size_t VoxelTerrainPhysics::collapse() {
    std::size_t moved = 0;
    // Discrete settling rule: unsupported cells settle to the next supported
    // layer. This does not approximate elastic structural mechanics.
    for (int z = 0; z < columns_z_; ++z)
        for (int x = 0; x < columns_x_; ++x) {
            for (int y = 0; y < layers_; ++y) {
                if (!occupied(x, y, z)) continue;
                if (y == 0 || occupied(x, y - 1, z)) continue;
                // Find the lowest supported layer below.
                int target = y;
                while (target > 0 && !occupied(x, target - 1, z)) --target;
                if (target < y) {
                    setOccupied(x, y, z, false);
                    setOccupied(x, target, z, true);
                    ++moved;
                }
            }
        }
    if (moved) ++revision_;
    return moved;
}

std::size_t VoxelTerrainPhysics::voxelCount() const {
    return std::count(occupied_.begin(), occupied_.end(), true);
}

std::vector<VoxelTerrainPhysics::Cluster>
VoxelTerrainPhysics::extractClusters(std::size_t minimumVoxels) const {
    std::vector<uint8_t> visited(occupied_.size(), 0);
    std::vector<Cluster> clusters;
    for (int z = 0; z < columns_z_; ++z)
        for (int y = 0; y < layers_; ++y)
            for (int x = 0; x < columns_x_; ++x) {
                const int start = index(x, y, z);
                if (!occupied_[start] || visited[start]) continue;
                // BFS flood fill over 6-connected neighbours.
                std::vector<std::array<int,3>> voxels;
                voxels.push_back({x, y, z});
                visited[start] = 1;
                for (std::size_t i = 0; i < voxels.size(); ++i) {
                    const auto [cx, cy, cz] = voxels[i];
                    for (const auto& d : std::array<std::array<int,3>,6>{{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}}}) {
                        const int nx = cx+d[0], ny = cy+d[1], nz = cz+d[2];
                        if (nx < 0 || nx >= columns_x_ || ny < 0 || ny >= layers_ ||
                            nz < 0 || nz >= columns_z_) continue;
                        const int ni = index(nx, ny, nz);
                        if (occupied_[ni] && !visited[ni]) {
                            visited[ni] = 1;
                            voxels.push_back({nx, ny, nz});
                        }
                    }
                }
                if (voxels.size() < minimumVoxels) continue;
                Cluster cluster;
                cluster.voxels = std::move(voxels);
                cluster.count = cluster.voxels.size();
                vsg::dvec3 sum{}, min{1e30, 1e30, 1e30}, max{-1e30, -1e30, -1e30};
                for (const auto& v : cluster.voxels) {
                    const auto c = voxelCentre(v[0], v[1], v[2]);
                    sum += c;
                    for (int a = 0; a < 3; ++a) { min[a] = std::min(min[a], c[a]); max[a] = std::max(max[a], c[a]); }
                }
                cluster.centre = sum / double(cluster.count);
                cluster.halfExtents = (max - min) * .5 + vsg::dvec3(cell_size_, cell_size_, cell_size_) * .5;
                clusters.push_back(std::move(cluster));
            }
    std::sort(clusters.begin(), clusters.end(),
              [](const Cluster& a, const Cluster& b) { return a.count > b.count; });
    return clusters;
}

std::shared_ptr<VoxelTerrain> VoxelTerrainPhysics::commit() const {
    auto result = std::make_shared<VoxelTerrain>(*source_);
    result->occupied_ = occupied_;
    result->top_ = top_;
    result->revision = revision_;
    result->valid_samples=0;
    result->min_height=std::numeric_limits<double>::infinity();
    result->max_height=-result->min_height;
    int lowest=layers_;
    for(int z=0;z<columns_z_;++z)for(int x=0;x<columns_x_;++x) {
        const auto top=top_[z*columns_x_+x];
        if(!std::isfinite(top))continue;
        ++result->valid_samples;
        result->max_height=std::max(result->max_height,top);
        for(int y=0;y<lowest;++y)if(occupied(x,y,z)){lowest=y;break;}
    }
    if(result->valid_samples)result->min_height=base_y_+lowest*cell_size_;
    else result->min_height=result->max_height=base_y_;
    result->rebuildMesh();
    return result;
}

} // namespace vsgraysim::earth
