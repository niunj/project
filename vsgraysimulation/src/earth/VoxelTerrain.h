#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include "VoxelSurfaceAppearance.h"

namespace vsgraysim::earth {
class VoxelTerrainPhysics;
class VoxelTerrain final : public EarthSurfaceSnapshot {
public:
    static std::shared_ptr<VoxelTerrain> load(const nlohmann::json& package,
                                              const nlohmann::json& config);
    /// Direct factory for programmatically built occupancy grids (voxelized
    /// models, tiled-terrain damage windows). Avoids a JSON round-trip for
    /// large grids. `occupied` is indexed (z*ny + y)*nx + x.
    static std::shared_ptr<VoxelTerrain> fromOccupancy(
        const vsg::dvec3& min, double cell, int nx, int ny, int nz,
        const std::vector<bool>& occupied, bool strict=false);
    /// Compact, self-contained RLE checkpoint; unlike legacy packages, this
    /// codec supports empty volumes and the programmatic 4M-cell budget.
    nlohmann::json encodeVolume() const;
    static std::shared_ptr<VoxelTerrain> decodeVolume(const nlohmann::json&);
    std::size_t occupiedCount() const;
    vsg::dvec3 centreOfMass()const{return centre_of_mass_;}
    std::size_t cellCount() const { return occupied_.size(); }
    std::array<int,3> dimensions() const { return {columns_x_,layers_,columns_z_}; }
    vsg::dvec3 gridOrigin() const { return {min_x,base_y_,min_z}; }
    const std::vector<bool>& occupancy() const { return occupied_; }
    EarthBackend backend() const override { return EarthBackend::Voxel; }
    SurfaceSample sampleLocal(double x, double z) const override;

    SurfaceSample waterBedLocal(double x,double z,double waterLevel) const override;
    std::optional<bool> occupiedGeodetic(double latitude,double longitude,double height) const override;
    /// Rebuild the render mesh (vertices_/indices_) from current occupied_/top_.
    /// Called by VoxelTerrainPhysics::commit() after damage/collapse changes state.
    void rebuildMesh();
    std::shared_ptr<const VoxelSurfaceAppearance> appearance()const{return appearance_;}
    void setAppearance(std::shared_ptr<const VoxelSurfaceAppearance> value){appearance_=std::move(value);rebuildMesh();}
private:
    friend class VoxelTerrainPhysics;
    std::shared_ptr<const VoxelSurfaceAppearance> appearance_;
    std::size_t occupied_count_=0;
    vsg::dvec3 centre_of_mass_{};
    double base_y_=0;
    int layers_=0;
    std::vector<bool> occupied_;
    int columns_x_ = 0;
    int columns_z_ = 0;
    std::vector<double> top_;
};
}
