#pragma once
/// @file PhysicsEngine.h
/// @brief Layer 0: Rigid-body physics bridge (Bullet3 + vsgBullet).
///
/// Wraps a vsgbDynamics::World (btDiscreteDynamicsWorld) and exposes a
/// small object-id-keyed API for creating rigid bodies from VSG meshes,
/// stepping the simulation, and querying pose/velocity - the data needed
/// by the ray tracer (dynamic geometry), the sensor scheduler (platform
/// mounting) and the signal processors (real Doppler).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <simcore/WorldState.h>

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/MatrixTransform.h>

#include <vsgbDynamics/World.h> // full definition required for ref_ptr member

#include "vsgraysim/core/RayPayload.h"   // Vec3 (射线求交几何数据)

namespace vsgraysim {

/// Configuration for one rigid body created from a VSG mesh.
struct RigidBodyDef {
    std::string object_id;            ///< matches ObjectPhysConfig::object_id
    vsg::dvec3  start_position{0.0, 0.0, 0.0};
    vsg::dvec3  start_rotation_deg{0.0, 0.0, 0.0}; ///< pitch, compass heading, roll (degrees)
    vsg::dvec3  initial_velocity{0.0, 0.0, 0.0};
    float       mass = 0.0f;          ///< 0 => static body (no gravity)
    float       restitution = 0.1f;
    float       friction = 0.5f;
    float       rolling_friction = 0.1f;
    bool        use_trimesh_shape = true; ///< static mesh: btBvhTriangleMeshShape
    /// Dynamic bodies use per-geode convex hulls (better performance than
    /// tri-meshes and gives real collision response).
    bool        use_convex_hull_shape = false;
    uint64_t earth_surface_revision=0;
};

class PhysicsEngine {
public:
    PhysicsEngine();
    uint64_t objectGeometryRevision() const { return object_geometry_revision_; }
    ~PhysicsEngine();

    // ---- World setup -------------------------------------------------
    void setGravity(const vsg::dvec3& g);
    /// Add a static infinite ground plane at y == plane_y.
    void addGroundPlane(double plane_y = 0.0);
    /// Consume the entire simulation interval, recording each physics substep.
    /// No Bullet maxSubSteps truncation and no interpolated render pose as truth.
    void step(double dt, std::optional<double> end_time = {});
    void configureTimeline(double max_step_seconds, double history_seconds = 10.0,
                           const std::string& epoch_utc = "2026-09-10T04:00:00Z");
    void setGeoAnchor(const simcore::geo::GeoAnchor& anchor);
    simcore::WorldSnapshotPtr snapshot() const;
    bool sampleBody(const std::string& id, double time, simcore::RigidState& out) const;
    bool bodyActive(std::size_t i) const { return i < bodies_.size() && bodies_[i]; }
    const std::string& bodyId(std::size_t i) const;
    const std::vector<double>& groundPlanes() const { return ground_planes_; }
    /// Stable body slot count, including tombstones. Check bodyActive(i) when iterating.
    std::size_t bodyCount() const { return transforms_.size(); }

    // ---- Rigid body creation ----------------------------------------
    /// Build a rigid body from a loaded mesh and return the visual
    /// transform node (a vsg::MatrixTransform synchronized from Bullet's
    /// authoritative body pose). The caller adds this node to the scene
    /// graph.  Returns nullptr on failure.
    vsg::ref_ptr<vsg::MatrixTransform>
    createRigidBodyFromMesh(vsg::Node* mesh, const RigidBodyDef& def, bool replaceStatic=false);

    struct PreparedMesh;
    static std::shared_ptr<PreparedMesh> prepareMesh(vsg::Node*,const RigidBodyDef&);
    vsg::ref_ptr<vsg::MatrixTransform> installMesh(PreparedMesh&,bool replaceStatic);

    /// Register an existing rigid body that was created outside this
    /// engine under the given object id,
    /// so bodyExists/transform/velocity queries can resolve it.  The body
    /// must already have been added to the dynamics world.  No-op if the
    /// id is already taken.
    void registerExternalBody(const std::string& object_id, void* body);
    void unregisterExternalBody(const std::string& object_id, void* body);
    /// Controlled platforms reuse scene bodies. New boxes are owned here too.
    void* nativeBody(const std::string& id) const;
    bool createBoxBody(const std::string& id, const vsg::dvec3& half_extents,
                       const simcore::RigidState& initial, double mass);
    void removeBody(const std::string& id);
    /// Kinematic motion is prescribed; dynamic motion is integrated by Bullet.
    void setKinematic(const std::string& id, bool enabled);
    void setKinematicTarget(const std::string& id, const simcore::RigidState& target);
    void setAngularVelocity(const std::string& id, const vsg::dvec3& rad_per_sec);
    vsg::dvec3 angularVelocity(const std::string& id) const;
    bool groundAt(double x, double z, double& height, vsg::dvec3& normal) const;
    /// Canonical coordinates remain double precision when Bullet is rebased.
    void shiftOrigin(const vsg::dvec3& origin);
    vsg::dvec3 physicsOrigin() const { return physics_origin_; }

    // ---- Query / control --------------------------------------------
    bool bodyExists(const std::string& object_id) const;
    vsg::dvec3 position(const std::string& object_id) const;
    vsg::dvec3 velocity(const std::string& object_id) const;
    vsg::dquat rotation(const std::string& object_id) const;
    vsg::dmat4 transform(const std::string& object_id) const;
    /// Returns the visual transform node (for debugging / GUI).
    vsg::ref_ptr<vsg::MatrixTransform> transformNode(const std::string& object_id) const;

    void setVelocity(const std::string& object_id, const vsg::dvec3& v);
    /// Accumulates a force held over the next full step interval, then cleared.
    void applyCentralForce(const std::string& object_id, const vsg::dvec3& f);

    /// Raycast against the physics world.  Returns true and the world-space
    /// hit point if a body blocks the segment from..to.
    bool raycast(const vsg::dvec3& from, const vsg::dvec3& to, vsg::dvec3& hit) const;

    /// Raycast 带命中法线 (世界系, 指向射线来源): 供光线追踪求交使用。
    /// 静态三角网格体内置 BVH 加速, 大场景下仍为对数级求交。
    bool raycast(const vsg::dvec3& from, const vsg::dvec3& to,
                 vsg::dvec3& hit, vsg::dvec3& normal) const;

    // ---- 射线求交几何 (供 RT 层自建 BVH, 避免每射线遍历物理世界) ----
    /// 刚体是否为静态 (创建时 mass == 0 且未被外部平台刚体接管)。
    bool bodyStatic(std::size_t i) const { return i < body_static_.size() && body_static_[i]; }
    /// 刚体的网格三角形 (建模局部系, 每 3 顶点一个三角形); 无网格时为空。
    const std::vector<Vec3>& bodyLocalTriangles(std::size_t i) const;
    /// 取走静态刚体三角形 (避免大场景网格双份驻留; 取后为空)。
    std::vector<Vec3> takeBodyTriangles(std::size_t i);
    /// 三角形世界安置矩阵: 静态=创建时固定安置矩阵, 动态=当前世界位姿。
    /// 世界顶点 = 矩阵 × 局部顶点。
    vsg::dmat4 bodyPlacement(std::size_t i) const;

    /// Access to the underlying vsgbDynamics world (advanced use only).
    vsg::ref_ptr<vsgbDynamics::World> world() const { return world_; }

private:
    std::size_t indexOf(const std::string& object_id) const;
    void synchronizeTransforms();
    simcore::RigidState stateAtIndex(std::size_t i, simcore::TimePoint time) const;
    void recordStates(simcore::TimePoint time, bool publish);
    uint64_t object_geometry_revision_=0; // Non-terrain collider membership and mesh content.
    struct Resources;
    std::unique_ptr<Resources> resources_;
    std::unordered_map<std::string, simcore::RigidStateHistory> histories_;
    simcore::WorldSnapshotPtr snapshot_;
    simcore::geo::GeoAnchor anchor_;
    simcore::TimePoint time_ = 0;
    simcore::TimePoint history_horizon_ = 10 * simcore::nanosecondsPerSecond;
    double max_step_ = 1.0 / 120.0;
    uint64_t revision_ = 0, surface_revision_=0;
    vsg::dvec3 physics_origin_{0,0,0};
    std::string epoch_utc_="2026-09-10T04:00:00Z";

    vsg::ref_ptr<vsgbDynamics::World>               world_;
    std::vector<vsg::ref_ptr<vsg::MatrixTransform>> transforms_; ///< visual nodes, owned
    /// Mesh-local COM per body; visual model pose = body pose * translate(-COM).
    std::vector<vsg::dvec3>                         coms_;
    std::vector<void*>                              bodies_;     ///< owned by Resources, or external
    /// 1 = externally owned body; the caller retains its lifetime responsibility.
    std::vector<uint8_t>                            external_;
    std::unordered_map<std::string, std::size_t>    index_;
    std::vector<std::string> body_ids_;
    std::vector<double> ground_planes_;
    // 射线求交几何: 建模局部系三角形 (静态刚体可被 RT 层取走),
    // 静态/安置矩阵缓存 (与 bodies_ 同长同序)。
    std::vector<std::vector<Vec3>>                  body_tris_;
    std::vector<uint8_t>                            body_static_;
    std::vector<vsg::dmat4>                         placers_;
};

using PhysicsEnginePtr = std::shared_ptr<PhysicsEngine>;

} // namespace vsgraysim
