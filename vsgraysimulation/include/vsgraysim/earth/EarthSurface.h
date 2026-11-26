#pragma once
#include <vsgraysim/earth/OrbitalSurface.h>
#include <functional>
#include <optional>
#include <simcore/GeoFrame.h>
#include <nlohmann/json.hpp>
#include <vsg/maths/vec3.h>
#include <cstdint>
#include <array>
#include <vsg/maths/vec2.h>
#include <memory>
#include <string>
#include <vector>

namespace vsgraysim::earth {
enum class EarthBackend { Rocky, Voxel };
enum class SurfaceStatus { Ready, Coarse, NoData };
struct SurfaceSample {
    SurfaceStatus status = SurfaceStatus::NoData;
    double height = 0;
    vsg::dvec3 normal{0,1,0};
    double resolution_m = 0;
    uint64_t revision = 0;
};
/// A pinned, bounded local mesh derived from declared geospatial data. All
/// consumers use its same triangle diagonal and validity mask. A snapshot is
/// immutable after publication; a later load never mutates an old measurement.
class EarthSurfaceSnapshot {
public:
    virtual ~EarthSurfaceSnapshot() = default;
    virtual EarthBackend backend() const { return EarthBackend::Rocky; }
    simcore::geo::GeoAnchor anchor;
    uint64_t revision = 0;
    std::shared_ptr<const OrbitalSurface> orbital;
    std::string source;
    // Resolved declaration for saving, reopening and cross-process handoff.
    // Captured with this revision, including package defaults and scene overrides.
    nlohmann::json declaration;
    // North-first diffuse atlas and matching UVs, captured in the same generation.
    std::vector<uint8_t> imagery_rgba;
    std::vector<vsg::vec2> imagery_uv;
    std::vector<vsg::vec3> surface_normals;
    std::vector<uint8_t> normal_rgba,mr_rgba,emissive_rgba;
    bool hasMaterialBlend=false;
    double dem_spacing_m=0, imagery_spacing_m=0; // worst native north-south spacing in the active set
    uint32_t imagery_width=0, imagery_height=0;
    std::size_t resident_tiles=0, reused_tiles=0;
    bool has_map_layers = false;
    /// Effective physical parameters, with parser defaults and numeric types
    /// normalized. Display/editor metadata is deliberately excluded.
    nlohmann::json material_config=nlohmann::json::object();
    bool provenance_assumed = false;
    bool strict = true;
    double min_x=0,max_x=0,min_z=0,max_z=0,resolution_m=0;
    double min_height=0,max_height=0;
    uint32_t grid_size=0;
    std::size_t valid_samples=0;
    const std::vector<vsg::dvec3>& vertices() const { return vertices_; }
    const std::vector<uint32_t>& indices() const { return indices_; }
    /// Optional canonical triangle ownership in the backing occupied grid.
    const std::vector<uint32_t>& triangleCells()const{return triangle_cells_;}
    virtual SurfaceSample sampleLocal(double x,double z) const;
    /// Bed beneath the water plane, or the enclosing solid boundary when dry.
    virtual SurfaceSample waterBedLocal(double x,double z,double) const { return sampleLocal(x,z); }
    /// Source-cell occupancy in the pinned geodetic volume; nullopt outside coverage or on surface-only backends.
    virtual std::optional<bool> occupiedGeodetic(double,double,double) const { return std::nullopt; }
    virtual std::optional<double> columnTopGeodetic(double,double) const { return std::nullopt; }
    bool containsLocal(double x,double z) const { return sampleLocal(x,z).status!=SurfaceStatus::NoData; }
    bool intersectLocal(const vsg::dvec3& origin,const vsg::dvec3& direction,
                        double max_distance,vsg::dvec3& hit,vsg::dvec3& normal) const;
protected:
    friend class EarthDataService;
    void invalidateRayIndex() noexcept {++rayGeneration_;}
    std::vector<vsg::dvec3> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<uint32_t> triangle_cells_;
    std::vector<uint8_t> valid_;
private:
    // Lifetime and mutation tokens for derived meshes rebuilt before publish.
    // The query cache does not retain the source mesh or alter checkpoints.
    std::shared_ptr<const uint8_t> rayIdentity_=std::make_shared<const uint8_t>(0);
    uint64_t rayGeneration_=0;
};
using EarthSurfaceSnapshotPtr = std::shared_ptr<const EarthSurfaceSnapshot>;
/// Prepare an independent drawable on the worker; publish its handle only at commit.
using PrepareSurfaceView = std::function<std::function<void()>(EarthSurfaceSnapshotPtr)>;
} // namespace vsgraysim::earth
