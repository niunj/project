#pragma once
/// @file PhysParamManager.h
/// @brief Layer 0: Multi-spectral physical attribute management.
///
/// Owns the CPU-side `GeoUnitPhysParams` array, indexed by `geo_unit_id`.
/// Responsible for:
///   1. Loading the JSON multi-spectral config files (section 2.1.2);
///   2. Validating parameters, supplying defaults;
///   3. Building the geometry-unit <-> object-id <-> band mapping table;
///   4. Providing partial-update APIs used by the dynamic node layer;
///   5. Handing off the buffer to the GPU cache uploader (Layer 2).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "vsgraysim/core/GeoUnitPhysParams.h"
#include "vsgraysim/core/SensorTypes.h"
#include <vsg/maths/vec3.h>
#include <nlohmann/json.hpp>

namespace vsgraysim {

/// One face-group / voxel-group binding extracted from the YAML/JSON
/// configuration (e.g. tank_engine, tank_body, tank_tire).
struct FaceGroup {
    std::string group_id;
    uint32_t    face_start;
    uint32_t    face_end;
    GeoUnitPhysParams template_params; ///< Parameters applied to each face
};

/// Per-object multi-spectral config.
struct ObjectPhysConfig {
    std::string object_id;
    std::string mesh_path;
    double      scale = 1.0;
    uint32_t    face_count;
    std::vector<FaceGroup> face_groups;

    // Dynamic parameter knobs
    uint32_t temp_update_rate_hz; ///< Temperature refresh rate
    float    rcs_modify_factor;   ///< RCS modification factor

    // ---- Rigid-body physics (optional; mass==0 => static) ----
    bool        physics_enabled      = false;
    vsg::dvec3  initial_position     {0.0, 0.0, 0.0};
    vsg::dvec3  initial_rotation_deg {0.0, 0.0, 0.0};
    vsg::dvec3  initial_velocity     {0.0, 0.0, 0.0};
    float       mass                 = 0.0f;
    float       restitution          = 0.1f;
    float       friction             = 0.5f;
    bool        use_convex_hull      = false;
};

/// Layer-0 manager: the single source of truth for all physical parameters
/// before they are uploaded to the GPU.
class PhysParamManager {
public:
    /// Load a single object config from JSON.
    /// Throws std::runtime_error on parse / validation failure.
    void loadObjectConfig(const std::string& json_path);

    /// Load a single object config from an in-memory JSON object (no file
    /// I/O).  Accepts the same schema as loadObjectConfig; `face_groups` is
    /// optional here — when missing a single default group covering
    /// [0, face_count-1] is generated.
    void loadObjectConfigFromJson(const nlohmann::json& j);

    /// Validate parameters (range checks, LUT references, etc.).
    /// Returns the number of corrected / defaulted parameters.
    std::size_t validateAll();

    /// Build the global geometry-unit array (`GeoUnitPhysParams`).
    /// One entry per face id, expanded from face_groups.
    void buildGeoUnitArray();

    /// Build the band-parameter mapping table (`BandParamMap`).
    void buildBandParamMap();

    /// Partial-update API: adjust the dynamic correction fields of every
    /// geometry unit that belongs to `object_id`.
    void updateDynamicParams(const std::string& object_id,
                             float rcs_modify_factor,
                             float temp_offset_K);

    /// Runtime mutation: per-geometry-unit parameter override.
    void overrideGeoUnit(uint32_t geo_unit_id,
                         const GeoUnitPhysParams& new_params);

    // --- Accessors -----------------------------------------------------
    const std::vector<GeoUnitPhysParams>& geoUnitArray() const { return geo_units_; }
    const std::vector<BandParamMap>&      bandParamMap() const { return band_param_map_; }
    const std::vector<ObjectPhysConfig>&  objectConfigs() const { return object_configs_; }

    /// Lookup a single object's configuration (nullptr if not found).
    const ObjectPhysConfig* findObject(const std::string& id) const;
    /// Global geometry-unit identity for an object's ORIGINAL local triangle index.
    const GeoUnitPhysParams* findFace(const std::string& object,uint32_t face) const;

    /// Total number of geometry units currently materialised.
    std::size_t geoUnitCount() const { return geo_units_.size(); }
    uint64_t revision() const { return revision_; }

private:
    uint64_t revision_=0;
    std::vector<ObjectPhysConfig>                          object_configs_;
    std::unordered_map<std::string, std::size_t>           object_index_;
    std::vector<GeoUnitPhysParams>                         geo_units_;
    std::vector<BandParamMap>                             band_param_map_;
    std::unordered_map<uint32_t, std::size_t>              geo_index_; ///< geo_unit_id -> array index
    std::unordered_map<std::string,std::unordered_map<uint32_t,std::size_t>> object_faces_;
};

using PhysParamManagerPtr = std::shared_ptr<PhysParamManager>;

} // namespace vsgraysim
