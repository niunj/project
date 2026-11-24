#pragma once
/// @file SceneBuilder.h
/// @brief Layer 1: Builds the VSG scene graph from Layer-0 configuration.
///
/// Loads GLTF/OSGB models through vsgXchange, instantiates
/// `MultiSpectralGeometryNode` (or `DynamicMultiSpectralNode` for dynamic
/// objects), binds face-ranges to geo_unit IDs, attaches sensor receiver
/// apertures, and constructs the Vulkan RT acceleration structures
/// (TLAS/BLAS) via the Layer-2 RT pipeline.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include "vsgraysim/layer0_phys/PhysParamManager.h"
#include "vsgraysim/layer0_phys/PhysicsEngine.h"
#include "vsgraysim/layer1_scene/MultiSpectralGeometryNode.h"
#include "vsgraysim/layer1_scene/DynamicMultiSpectralNode.h"

namespace vsg { class Node; class Options; }

namespace vsgraysim {

struct SensorAperture {
    uint32_t    sensor_id;
    vsg::dvec3  position;        ///< World-space position (m)
    vsg::dvec3  direction;       ///< Boresight direction (unit)
    double      half_angle;     ///< Receive FoV half-angle (rad)
    double      aperture_area;   ///< Aperture area (m^2)
    vsg::ref_ptr<vsg::Node> debug_geom; ///< Optional debug viz
};

class SceneBuilder {
public:
    SceneBuilder(PhysParamManagerPtr phys,
                 PhysicsEnginePtr physics = {},
                 vsg::ref_ptr<vsg::Options> options = {});

    /// Load all object configs already present in the PhysParamManager,
    /// build their corresponding MultiSpectralGeometryNodes, attach them
    /// to the root group, and return the root.
    vsg::ref_ptr<vsg::Group> build();

    /// Register a sensor receiver aperture (added to scene for RT intersection).
    void addSensorAperture(const SensorAperture& a);

    /// Accessor for the built MS nodes (empty until `build()` is called).
    const std::vector<vsg::ref_ptr<MultiSpectralGeometryNode>>& msNodes() const { return ms_nodes_; }
    const std::vector<vsg::ref_ptr<DynamicMultiSpectralNode>>&   dynNodes() const { return dyn_nodes_; }
    const std::vector<SensorAperture>&                            apertures() const { return apertures_; }
    /// Mutable aperture access (physics/sensor-platform sync updates
    /// positions in place each frame).
    std::vector<SensorAperture>&                                 apertures() { return apertures_; }
    /// Rigid-body transform nodes produced during build(), keyed by object id.
    const std::unordered_map<std::string, vsg::ref_ptr<vsg::MatrixTransform>>&
    physicsTransforms() const { return physics_transforms_; }

private:
    PhysParamManagerPtr                                        phys_;
    PhysicsEnginePtr                                           physics_;
    vsg::ref_ptr<vsg::Options>                                 options_;
    vsg::ref_ptr<vsg::Group>                                   root_;
    std::vector<vsg::ref_ptr<MultiSpectralGeometryNode>>       ms_nodes_;
    std::vector<vsg::ref_ptr<DynamicMultiSpectralNode>>        dyn_nodes_;
    std::vector<SensorAperture>                                apertures_;
    std::unordered_map<std::string, vsg::ref_ptr<vsg::MatrixTransform>>
                                                               physics_transforms_;
};

using SceneBuilderPtr = std::shared_ptr<SceneBuilder>;

} // namespace vsgraysim
