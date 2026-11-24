#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include <array>
#include <map>
#include <memory>

namespace vsgraysim::earth {
/// Mutable damage window extracted from a TiledVoxelTerrain: a local ENU
/// rectangle of columns with their occupied/removed run state. The host
/// damages this window through VoxelTerrainPhysics and commits it back;
/// the tiled terrain applies the runs as an override layer.
class TiledVoxelTerrain final : public EarthSurfaceSnapshot {
public:
    using Run=std::array<int,2>;
    using Column=std::vector<Run>;

    static std::shared_ptr<TiledVoxelTerrain> load(const nlohmann::json& map,
        nlohmann::json& settings, const simcore::geo::GeoAnchor&, const std::string& path);
    EarthBackend backend() const override { return EarthBackend::Voxel; }
    SurfaceSample sampleLocal(double x,double z) const override;
    SurfaceSample waterBedLocal(double x,double z,double waterLevel) const override;
    std::optional<double> columnTopGeodetic(double,double) const override;
    uint64_t occupancySignature() const;
    std::optional<bool> occupiedGeodetic(double latitude,double longitude,double height) const override;

    /// A local damage window: ENU-aligned occupancy grid extracted around a
    /// geodetic focus. `occupied` is indexed (z*ny + y)*nx + x.
    struct DamageWindow {
        vsg::dvec3 min{0,0,0}; double cell=1;
        int nx=0, ny=0, nz=0;
        std::vector<bool> occupied;
        /// Per column (nx*nz): original occupied [bottom,top) metre runs.
        std::vector<std::vector<std::array<double,2>>> originalRuns;
        std::vector<size_t> sourceColumns;
        std::vector<double> heightOffsets,heightScales;
    };
    /// Extract the damage window around a geodetic point. Returns nullopt
    /// if the point is outside terrain residency.
    std::optional<DamageWindow> extractDamageWindow(double lat,double lon,
        double halfSpanMetres,double bottomMetres) const;
    /// Commit damaged runs back: runs[i] replaces the occupied state of
    /// window column i. Runs are [bottom,top) metre intervals. Returns the
    /// number of columns changed.
    std::size_t applyDamage(const DamageWindow& window,
                            const std::vector<std::vector<std::array<double,2>>>& runs);
    /// Access the post-damage override layer (test/inspection use).
    const std::map<size_t,Column>& damageOverrides() const { return *damage_; }

private:
    void rebuildVolumeMesh();
    const Column* columnAt(double lat,double lon) const;
    std::vector<std::shared_ptr<const Column>> volume_;
    /// Post-damage override: key = column index in volume_, value = runs
    /// in quantum units. Present entries shadow the baked tile column.
    std::shared_ptr<std::map<size_t,Column>> damage_ =
        std::make_shared<std::map<size_t,Column>>();
    int volume_level_=0,west_tile_=0,north_tile_=0,cells_per_tile_=0;
    double quantum_=0;
    std::vector<uint8_t> triangle_stride_;
    static constexpr int bins = 32;
    std::array<std::vector<uint32_t>,bins*bins> columns_;
    int bin(double value,double low,double high) const;
};
}
