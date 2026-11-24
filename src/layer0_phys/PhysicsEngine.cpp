#include "vsgraysim/layer0_phys/PhysicsEngine.h"

#include <spdlog/spdlog.h>

#include <btBulletDynamicsCommon.h>
#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/nodes/Transform.h>
#include <vsg/maths/transform.h>

#include <vsgbCollision/CollisionShapes.h>
#include <vsgbDynamics/CreationRecord.h>
#include <vsgbDynamics/RigidBody.h>
#include <vsgbDynamics/World.h>

#include <cmath>
#include <atomic>
#include <simcore/Orientation.h>
#include <simcore/UtcEpoch.h>
#include <stdexcept>

namespace vsgraysim {

namespace {

vsg::dmat4 eulerToMat4(const vsg::dvec3& deg) {
    return vsg::rotate(simcore::headingRotation(deg.y,deg.x,deg.z));
}

btVector3 toBt(const vsg::dvec3& v) { return btVector3(btScalar(v.x), btScalar(v.y), btScalar(v.z)); }
vsg::dvec3 fromBt(const btVector3& v) { return {v.x(), v.y(), v.z()}; }
btTransform toBt(const simcore::RigidState& state, const vsg::dvec3& com,const vsg::dvec3& origin={}) {
    const auto q = simcore::normalized(state.orientation);
    return btTransform(btQuaternion(q.x,q.y,q.z,q.w), toBt(state.point(com)-origin));
}

// Local helper: walks a VSG subtree, accumulates the world-space (relative
// to the subtree root) bounding box of all VertexIndexDraw position data
// and collects the raw vertices.  Used instead of vsg::ComputeBounds:
// ComputeBounds relies on ArrayState which mis-parses the array layouts
// produced by vsgXchange's gltf reader (reads past the end of constant
// arrays -> garbage bounds).
class MeshGeometryCollector : public vsg::Visitor {
public:
    using vsg::Visitor::apply;

    void apply(vsg::Transform& t) override {
        vsg::dmat4 prev = stack_.empty() ? vsg::dmat4() : stack_.back();
        stack_.push_back(t.transform(prev));
        t.traverse(*this);
        stack_.pop_back();
    }
    void apply(vsg::Node& n) override { n.traverse(*this); }
    void apply(vsg::VertexIndexDraw& vid) override {
        if (vid.arrays.empty()) return;
        auto* pos = dynamic_cast<vsg::vec3Array*>(vid.arrays[0]->data.get());
        if (!pos) return;
        const vsg::dmat4 m = stack_.empty() ? vsg::dmat4() : stack_.back();
        for (auto& v : *pos) {
            const vsg::dvec3 w = m * vsg::dvec3(v.x, v.y, v.z);
            bounds.min.x = std::min(bounds.min.x, w.x);
            bounds.min.y = std::min(bounds.min.y, w.y);
            bounds.min.z = std::min(bounds.min.z, w.z);
            bounds.max.x = std::max(bounds.max.x, w.x);
            bounds.max.y = std::max(bounds.max.y, w.y);
            bounds.max.z = std::max(bounds.max.z, w.z);
        }
        auto add = [&](uint32_t index) {
            if (index >= pos->size()) throw std::invalid_argument("collision mesh index exceeds vertex count");
            const auto& v = (*pos)[index];
            vertices.push_back(m * vsg::dvec3(v.x,v.y,v.z));
        };
        if (vid.indices && vid.indices->data) {
            const auto* u16 = dynamic_cast<const vsg::ushortArray*>(vid.indices->data.get());
            const auto* u32 = dynamic_cast<const vsg::uintArray*>(vid.indices->data.get());
            const auto* u8 = dynamic_cast<const vsg::ubyteArray*>(vid.indices->data.get());
            const auto n = u16 ? u16->size() : u32 ? u32->size() : u8 ? u8->size() : 0;
            const auto end = uint64_t(vid.firstIndex) + vid.indexCount;
            if (end > n) throw std::invalid_argument("collision mesh draw exceeds index buffer");
            for (uint64_t i=vid.firstIndex; i<end; ++i) {
                const int64_t index = int64_t(u16 ? (*u16)[i] : u32 ? (*u32)[i] : (*u8)[i]) + vid.vertexOffset;
                if (index < 0 || uint64_t(index) >= pos->size()) throw std::invalid_argument("invalid collision mesh index");
                add(uint32_t(index));
            }
        } else for (uint32_t i=0; i<pos->size(); ++i) add(i);
    }

    vsg::dbox bounds;                 ///< empty until at least one mesh seen
    std::vector<vsg::dvec3> vertices; ///< world-space vertices (relative to subtree root)
private:
    std::vector<vsg::dmat4> stack_;
};

} // anonymous namespace

struct PhysicsEngine::Resources {
    struct Body {
        std::unique_ptr<btTriangleMesh> mesh;
        std::unique_ptr<btCollisionShape> shape;
        std::unique_ptr<btMotionState> motion;
        std::unique_ptr<btRigidBody> body;
    };
    std::unique_ptr<btDefaultCollisionConfiguration> config;
    std::unique_ptr<btCollisionDispatcher> dispatcher;
    std::unique_ptr<btDbvtBroadphase> broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
    std::vector<std::unique_ptr<Body>> owned;
    std::map<std::size_t, std::pair<simcore::RigidState, simcore::RigidState>> targets;
    std::map<std::size_t, btScalar> dynamic_masses;
};

PhysicsEngine::PhysicsEngine() : resources_(std::make_unique<Resources>()), world_(vsgbDynamics::World::create()) {
    // vsgBullet's default AxisSweep world is bounded to +/- 1 km and 1000
    // proxies. Earth scenes need an unbounded broadphase with explicit ownership.
    auto* previous = world_->getDynamicsWorld();
    auto* dispatcher = previous->getDispatcher();
    auto* config = static_cast<btCollisionDispatcher*>(dispatcher)->getCollisionConfiguration();
    auto* broadphase = previous->getBroadphase();
    auto* solver = previous->getConstraintSolver();
    delete previous; delete solver; delete broadphase; delete dispatcher; delete config;
    resources_->config = std::make_unique<btDefaultCollisionConfiguration>();
    resources_->dispatcher = std::make_unique<btCollisionDispatcher>(resources_->config.get());
    resources_->broadphase = std::make_unique<btDbvtBroadphase>();
    resources_->solver = std::make_unique<btSequentialImpulseConstraintSolver>();
    world_->setDynamicsWorld(new btDiscreteDynamicsWorld(resources_->dispatcher.get(),
        resources_->broadphase.get(), resources_->solver.get(), resources_->config.get()));
    world_->getDynamicsWorld()->setGravity(btVector3(0, btScalar(-9.81), 0));
    recordStates(0, true);
}

PhysicsEngine::~PhysicsEngine() {
    for (const auto& owned : resources_->owned)
        if (owned && owned->body) world_->getDynamicsWorld()->removeRigidBody(owned->body.get());
    resources_->owned.clear();
    world_ = nullptr;
}

void PhysicsEngine::setGravity(const vsg::dvec3& g) {
    world_->getDynamicsWorld()->setGravity(toBt(g));
}

void PhysicsEngine::addGroundPlane(double plane_y) {
    if (!std::isfinite(plane_y)) throw std::invalid_argument("invalid ground height");
    auto owned = std::make_unique<Resources::Body>();
    owned->shape = std::make_unique<btStaticPlaneShape>(btVector3(0,1,0), btScalar(plane_y));
    btTransform groundTransform; groundTransform.setIdentity(); groundTransform.setOrigin(toBt(-physics_origin_));
    owned->motion = std::make_unique<btDefaultMotionState>(groundTransform);
    btRigidBody::btRigidBodyConstructionInfo ci(0, owned->motion.get(), owned->shape.get());
    owned->body = std::make_unique<btRigidBody>(ci);
    owned->body->setFriction(.9f);
    world_->getDynamicsWorld()->addRigidBody(owned->body.get());
    resources_->owned.push_back(std::move(owned));
    ground_planes_.push_back(plane_y);
    spdlog::info("Layer0: PhysicsEngine - ground plane added at y={:.2f}", plane_y);
}

void PhysicsEngine::configureTimeline(double max_step_seconds, double history_seconds,const std::string& epoch_utc) {
    simcore::parseUtcEpoch(epoch_utc);
    if (!std::isfinite(max_step_seconds) || max_step_seconds <= 0 || max_step_seconds > 1 || time_ != 0)
        throw std::invalid_argument("configure physics timestep before advancing the world");
    const auto horizon = simcore::fromSeconds(history_seconds);
    if (!horizon) throw std::invalid_argument("truth history must be positive");
    max_step_ = max_step_seconds; history_horizon_ = horizon; epoch_utc_=epoch_utc; histories_.clear();
    recordStates(0, true);
}

void PhysicsEngine::setGeoAnchor(const simcore::geo::GeoAnchor& anchor) {
    simcore::geo::makeEnuToWorldMatrix(anchor); // validate before accepting
    if (time_ != 0) throw std::logic_error("cannot change the coordinate origin of a running physics world");
    anchor_ = anchor; recordStates(0, true);
}

void PhysicsEngine::step(double dt, std::optional<double> end_time) {
    if (!std::isfinite(dt) || dt <= 0 || dt / max_step_ > 4096)
        throw std::invalid_argument("invalid physics interval or excessive catch-up");
    const auto end = end_time ? simcore::fromSeconds(*end_time) : time_ + simcore::fromSeconds(dt);
    if (end <= time_ || std::abs(simcore::toSeconds(end-time_) - dt) > 2e-9)
        throw std::invalid_argument("physics interval disagrees with simulation time");
    const auto begin = time_;
    for (std::size_t i=0;i<bodies_.size();++i) {
        if (!bodyActive(i) || static_cast<btRigidBody*>(bodies_[i])->isStaticObject()) continue;
        const auto pos=stateAtIndex(i,time_).position;
        if (vsg::length(pos-physics_origin_)>1000)
            shiftOrigin({std::floor(pos.x/1000)*1000,std::floor(pos.y/1000)*1000,std::floor(pos.z/1000)*1000});
        break;
    }
    const uint32_t steps = std::max(1u, uint32_t(std::ceil(dt/max_step_ - 1e-7)));
    struct AppliedLoad { btRigidBody* body; btVector3 force,torque; };
    std::vector<AppliedLoad> loads;
    auto* dynamics=world_->getDynamicsWorld();
    for (int i=0;i<dynamics->getNumCollisionObjects();++i)
        if (auto* body=btRigidBody::upcast(dynamics->getCollisionObjectArray()[i]); body && !body->isStaticOrKinematicObject())
            loads.push_back({body,body->getTotalForce(),body->getTotalTorque()});
    recordStates(begin, false);
    for (uint32_t s = 1; s <= steps; ++s) {
        const auto next = begin + (end-begin)*s/steps;
        // Bullet clears external loads after each explicit stepSimulation call.
        // A host force command applies over this entire interval, not just its first substep.
        if (s>1) for (const auto& load:loads) {
            load.body->applyCentralForce(load.force); load.body->applyTorque(load.torque);
        }
        for (const auto& [i, pair] : resources_->targets) {
            if (!bodyActive(i)) continue;
            const double u = std::clamp(double(next-pair.first.time)/double(pair.second.time-pair.first.time), 0.0, 1.0);
            auto target = pair.second;
            target.position = pair.first.position*(1-u) + pair.second.position*u;
            target.orientation = simcore::interpolateRotation(pair.first.orientation, pair.second.orientation, u);
            auto* body = static_cast<btRigidBody*>(bodies_[i]);
            body->getMotionState()->setWorldTransform(toBt(target, coms_[i],physics_origin_));
            body->setWorldTransform(toBt(target, coms_[i],physics_origin_));
        }
        // Explicit substeps consume all elapsed time; Bullet's internal
        // accumulator and render interpolation are deliberately not used.
        world_->getDynamicsWorld()->stepSimulation(btScalar(simcore::toSeconds(next-time_)), 0);
        time_ = next;
        synchronizeTransforms();
        recordStates(time_, s == steps);
    }
    for (auto it = resources_->targets.begin(); it != resources_->targets.end(); ) {
        if (it->second.second.time <= end) it = resources_->targets.erase(it); else ++it;
    }
}

void PhysicsEngine::synchronizeTransforms() {
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (!bodies_[i]) continue;
        auto* body = static_cast<btRigidBody*>(bodies_[i]);
        const auto& wt = body->getWorldTransform();
        const btMatrix3x3& b = wt.getBasis();
        vsg::dmat4 m;
        m[0] = vsg::dvec4(b[0][0], b[1][0], b[2][0], 0.0);
        m[1] = vsg::dvec4(b[0][1], b[1][1], b[2][1], 0.0);
        m[2] = vsg::dvec4(b[0][2], b[1][2], b[2][2], 0.0);
        m[3] = vsg::dvec4(physics_origin_+fromBt(wt.getOrigin()), 1.0);
        transforms_[i]->matrix = m * vsg::translate(-coms_[i]);
    }
}

void PhysicsEngine::registerExternalBody(const std::string& object_id, void* body) {
    if (!body) return;
    auto it = index_.find(object_id);
    if (it != index_.end()) {
        if (bodies_[it->second] != body)
            throw std::logic_error("a scene body already owns this object id; reuse it instead of creating a second collider");
        return;
    }
    transforms_.push_back(vsg::MatrixTransform::create());
    coms_.push_back({0.0, 0.0, 0.0});   // 外部刚体 com 在原点 (btDefaultMotionState)
    bodies_.push_back(body);
    external_.push_back(1);
    index_[object_id] = transforms_.size() - 1;
    body_ids_.push_back(object_id);
    body_tris_.emplace_back();          // 外部刚体无网格数据 (碰撞用包围盒)
    body_static_.push_back(0);
    placers_.push_back(vsg::dmat4());
    ++object_geometry_revision_;
    synchronizeTransforms(); recordStates(time_, true);
    spdlog::info("Layer0: PhysicsEngine - external body '{}' registered ({} total)",
                 object_id, transforms_.size());
}

struct PhysicsEngine::PreparedMesh {
    RigidBodyDef definition;
    std::unique_ptr<Resources::Body> owned;
    vsg::dvec3 center;
    vsg::ref_ptr<vsg::MatrixTransform> transform;
    std::vector<Vec3> triangles;
};

vsg::ref_ptr<vsg::MatrixTransform>
PhysicsEngine::createRigidBodyFromMesh(vsg::Node* mesh,const RigidBodyDef& def,bool replaceStatic) {
    auto prepared=prepareMesh(mesh,def);
    return prepared ? installMesh(*prepared,replaceStatic) : nullptr;
}

std::shared_ptr<PhysicsEngine::PreparedMesh>
PhysicsEngine::prepareMesh(vsg::Node* mesh,const RigidBodyDef& def) {
    if (!mesh) {
        spdlog::warn("Layer0: PhysicsEngine - null mesh for '{}'", def.object_id);
        return {};
    }
    if (!simcore::finite(def.start_position) || !simcore::finite(def.start_rotation_deg) ||
        !simcore::finite(def.initial_velocity) || !std::isfinite(def.mass) || def.mass < 0)
        throw std::invalid_argument("invalid rigid body initial state");
    // A single model-to-world node is shared by visuals, queries and RT.
    // The body's COM offset is applied exactly once, even under rotation.
    auto mt = vsg::MatrixTransform::create();
    mt->matrix = vsg::translate(def.start_position) * eulerToMat4(def.start_rotation_deg);
    mt->addChild(vsg::ref_ptr<vsg::Node>(mesh));

    // Gather the mesh geometry ourselves (see MeshGeometryCollector).
    MeshGeometryCollector collector;
    mesh->accept(collector);
    if (collector.vertices.empty()) {
        spdlog::warn("Layer0: PhysicsEngine - no VertexIndexDraw position data "
                     "found in mesh for '{}', skipping body", def.object_id);
        return {};
    }

    // Bounds-center approximation of COM in model coordinates. The rotated
    // offset is applied to the Bullet body, never to the canonical model pose.
    const vsg::dvec3 mesh_center = vsg::dvec3(collector.bounds.max + collector.bounds.min) * 0.5;

    // Collision shape from the raw vertex data, centered on the body origin.
    const vsg::dvec3 half = vsg::dvec3(collector.bounds.max - collector.bounds.min) * 0.5;
    auto owned = std::make_unique<Resources::Body>();
    btCollisionShape* shape = nullptr;
    if (def.mass == 0.0f && def.use_trimesh_shape) {
        // Static triangle-mesh (BVH) for raycasts / resting bodies.
        owned->mesh = std::make_unique<btTriangleMesh>();
        auto* tri_mesh = owned->mesh.get();
        for (std::size_t i = 0; i + 2 < collector.vertices.size(); i += 3) {
            btVector3 a = toBt(collector.vertices[i] - mesh_center);
            btVector3 b = toBt(collector.vertices[i + 1] - mesh_center);
            btVector3 c = toBt(collector.vertices[i + 2] - mesh_center);
            tri_mesh->addTriangle(a, b, c);
        }
        if (tri_mesh->getNumTriangles() == 0) {
            spdlog::warn("Layer0: PhysicsEngine - mesh for '{}' has no triangles",
                         def.object_id);
            return {};
        }
        shape = new btBvhTriangleMeshShape(tri_mesh, true, true);
    } else if (def.use_convex_hull_shape && collector.vertices.size() >= 4) {
        auto* hull = new btConvexHullShape();
        for (const auto& v : collector.vertices)
            hull->addPoint(toBt(v - mesh_center));
        shape = hull;
    } else {
        shape = new btBoxShape(btVector3(btScalar(half.x), btScalar(half.y), btScalar(half.z)));
    }

    owned->shape.reset(shape);
    std::vector<Vec3> tris;
    tris.reserve(collector.vertices.size());
    for (const auto& v : collector.vertices)
        tris.push_back({float(v.x),float(v.y),float(v.z)});
    return std::make_shared<PreparedMesh>(PreparedMesh{def,std::move(owned),mesh_center,mt,std::move(tris)});
}

vsg::ref_ptr<vsg::MatrixTransform> PhysicsEngine::installMesh(PreparedMesh& prepared,bool replaceStatic) {
    const auto& def=prepared.definition;
    auto& owned=prepared.owned;
    if(!owned || owned->body) throw std::logic_error("mesh installation is single use");
    auto mt=prepared.transform;
    const auto mesh_center=prepared.center;
    auto& tris=prepared.triangles;
    auto* shape=owned->shape.get();
    const auto slot=indexOf(def.object_id);
    if (replaceStatic && (!bodyStatic(slot) || external_.at(slot) || def.mass!=0))
        throw std::invalid_argument("only owned static geometry can be replaced");
    if (!replaceStatic && index_.count(def.object_id)) {
        spdlog::warn("Layer0: PhysicsEngine - duplicate body '{}', skipping", def.object_id);
        return {};
    }

    const auto model = mt->matrix;
    btMatrix3x3 basis(model[0][0],model[1][0],model[2][0], model[0][1],model[1][1],model[2][1],
                      model[0][2],model[1][2],model[2][2]);
    const auto center = model * vsg::dvec4(mesh_center, 1);
    const vsg::dvec3 centerWorld(center.x,center.y,center.z);
    if (def.mass>0 && index_.empty() && vsg::length(def.start_position)>1000) shiftOrigin(def.start_position);
    btTransform start(basis, toBt(centerWorld-physics_origin_));
    owned->motion = std::make_unique<btDefaultMotionState>(start);
    btVector3 inertia(0,0,0);
    if (def.mass > 0) shape->calculateLocalInertia(def.mass, inertia);
    btRigidBody::btRigidBodyConstructionInfo info(def.mass, owned->motion.get(), shape, inertia);
    owned->body = std::make_unique<btRigidBody>(info);
    auto* body = owned->body.get();
    body->setFriction(def.friction); body->setRestitution(def.restitution);
    body->setRollingFriction(def.rolling_friction);
    body->setLinearVelocity(toBt(def.initial_velocity));
    if (def.mass == 0.0f) {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
    }
    if (replaceStatic) {
        auto previous=std::find_if(resources_->owned.begin(),resources_->owned.end(),
            [&](const auto& resource){return resource->body.get()==bodies_[slot];});
        if(previous==resources_->owned.end()) throw std::logic_error("static body resource missing");
        world_->getDynamicsWorld()->removeRigidBody(previous->get()->body.get());
        world_->getDynamicsWorld()->addRigidBody(body);
        previous->swap(owned); // Outgoing BVH lives until the staged handle is retired.
        bodies_[slot]=body;
        coms_[slot]=mesh_center;
        transforms_[slot]->children.swap(mt->children);
        mt=transforms_[slot];
        body_tris_[slot].swap(tris);
        placers_[slot]=model;
    } else {
        world_->getDynamicsWorld()->addRigidBody(body);
        resources_->owned.push_back(std::move(owned));
        transforms_.push_back(mt);
        coms_.push_back(mesh_center);
        bodies_.push_back(body);
        external_.push_back(0);
        index_[def.object_id]=transforms_.size()-1;
        body_ids_.push_back(def.object_id);
        body_tris_.push_back(std::move(tris));
        body_static_.push_back(def.mass==0);
        placers_.push_back(model);
    }
    if(def.object_id!="__earth_surface") ++object_geometry_revision_;
    if(def.earth_surface_revision) surface_revision_=def.earth_surface_revision;
    synchronizeTransforms(); recordStates(time_, true);

    spdlog::info("Layer0: PhysicsEngine - rigid body '{}' (mass={}, pos=({:.1f},{:.1f},{:.1f}), vel=({:.1f},{:.1f},{:.1f}))",
                 def.object_id, def.mass,
                 def.start_position.x, def.start_position.y, def.start_position.z,
                 def.initial_velocity.x, def.initial_velocity.y, def.initial_velocity.z);
    return mt;
}

std::size_t PhysicsEngine::indexOf(const std::string& object_id) const {
    auto it = index_.find(object_id);
    return it == index_.end() ? transforms_.size() : it->second;
}

void* PhysicsEngine::nativeBody(const std::string& id) const {
    const auto i=indexOf(id); return i<bodies_.size() ? bodies_[i] : nullptr;
}

simcore::RigidState PhysicsEngine::stateAtIndex(std::size_t i, simcore::TimePoint time) const {
    const auto* body=static_cast<btRigidBody*>(bodies_[i]);
    const auto& transform=body->getWorldTransform();
    const auto q=transform.getRotation();
    simcore::RigidState state;
    state.time=time;
    state.orientation=simcore::normalized({q.x(),q.y(),q.z(),q.w()});
    const auto offset=transform.getBasis()*toBt(coms_[i]);
    state.position=physics_origin_+fromBt(transform.getOrigin()-offset);
    state.angular_velocity=fromBt(body->getAngularVelocity());
    state.velocity=fromBt(body->getLinearVelocity()-body->getAngularVelocity().cross(offset));
    return state;
}

void PhysicsEngine::recordStates(simcore::TimePoint time, bool publish) {
    std::shared_ptr<simcore::WorldSnapshot> frame;
    if (publish) {
        frame=std::make_shared<simcore::WorldSnapshot>();
        frame->time=time; frame->revision=++revision_; frame->anchor=anchor_;
        frame->epoch_utc=epoch_utc_; frame->surface_revision=surface_revision_;
    }
    for (const auto& [id,i]:index_) {
        if (!bodyActive(i)) continue;
        const auto state=stateAtIndex(i,time);
        auto it=histories_.try_emplace(id,history_horizon_).first;
        if (!it->second.push(state)) throw std::runtime_error("invalid physics truth sample");
        if (frame) frame->bodies.emplace(id,state);
    }
    if (frame) std::atomic_store(&snapshot_,simcore::WorldSnapshotPtr(std::move(frame)));
}

simcore::WorldSnapshotPtr PhysicsEngine::snapshot() const { return std::atomic_load(&snapshot_); }

bool PhysicsEngine::sampleBody(const std::string& id,double time,simcore::RigidState& out) const {
    const auto it=histories_.find(id);
    if (it==histories_.end()) return false;
    return it->second.sample(simcore::fromSeconds(time),out);
}

bool PhysicsEngine::createBoxBody(const std::string& id,const vsg::dvec3& half,
                                  const simcore::RigidState& initial,double mass) {
    if (bodyExists(id)) return false;
    if (id.empty() || !simcore::finite(half) || half.x<=0 || half.y<=0 || half.z<=0 ||
        !simcore::finite(initial.position) || !simcore::finite(initial.velocity) || !std::isfinite(mass) || mass<0)
        throw std::invalid_argument("invalid platform body");
    auto owned=std::make_unique<Resources::Body>();
    owned->shape=std::make_unique<btBoxShape>(toBt(half));
    if (mass>0 && index_.empty() && vsg::length(initial.position)>1000) shiftOrigin(initial.position);
    owned->motion=std::make_unique<btDefaultMotionState>(toBt(initial,{},physics_origin_));
    btVector3 inertia(0,0,0);
    if (mass>0) owned->shape->calculateLocalInertia(btScalar(mass),inertia);
    btRigidBody::btRigidBodyConstructionInfo info(btScalar(mass),owned->motion.get(),owned->shape.get(),inertia);
    owned->body=std::make_unique<btRigidBody>(info);
    auto* body=owned->body.get();
    body->setLinearVelocity(toBt(initial.velocity));
    body->setAngularVelocity(toBt(initial.angular_velocity));
    world_->getDynamicsWorld()->addRigidBody(body);
    registerExternalBody(id,body);
    external_.back()=0;
    // An engine-created box is real scene geometry, available to all query backends.
    const vsg::dvec3 corners[8]={{-half.x,-half.y,-half.z},{half.x,-half.y,-half.z},
        {half.x,half.y,-half.z},{-half.x,half.y,-half.z},{-half.x,-half.y,half.z},
        {half.x,-half.y,half.z},{half.x,half.y,half.z},{-half.x,half.y,half.z}};
    const int triangles[36]={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
    auto& geometry=body_tris_.back();
    for (int vertex:triangles) geometry.push_back({float(corners[vertex].x),float(corners[vertex].y),float(corners[vertex].z)});
    body_static_.back()=mass==0;
    placers_.back()=initial.matrix();
    resources_->owned.push_back(std::move(owned));
    return true;
}

void PhysicsEngine::removeBody(const std::string& id) {
    const auto i=indexOf(id); if (!bodyActive(i)) return;
    auto* body=static_cast<btRigidBody*>(bodies_[i]);
    world_->getDynamicsWorld()->removeRigidBody(body);
    index_.erase(id); histories_.erase(id); resources_->targets.erase(i); resources_->dynamic_masses.erase(i);
    bodies_[i]=nullptr; body_tris_[i].clear(); body_static_[i]=0;
    for (auto it=resources_->owned.begin();it!=resources_->owned.end();++it)
        if ((*it)->body.get()==body) { resources_->owned.erase(it); break; }
    ++object_geometry_revision_;
    recordStates(time_,true);
}

void PhysicsEngine::unregisterExternalBody(const std::string& id,void* body) {
    if (nativeBody(id)==body) removeBody(id);
}

void PhysicsEngine::setKinematic(const std::string& id,bool enabled) {
    const auto i=indexOf(id); if (!bodyActive(i)) throw std::invalid_argument("unknown controlled body");
    auto* body=static_cast<btRigidBody*>(bodies_[i]);
    world_->getDynamicsWorld()->removeRigidBody(body);
    if (enabled) {
        resources_->dynamic_masses.try_emplace(i,body->getInvMass()>0 ? 1/body->getInvMass() : btScalar(0));
        body->setMassProps(0,btVector3(0,0,0));
        body->setCollisionFlags((body->getCollisionFlags() & ~btCollisionObject::CF_STATIC_OBJECT) | btCollisionObject::CF_KINEMATIC_OBJECT);
    } else {
        resources_->targets.erase(i);
        body->setCollisionFlags(body->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
        auto it=resources_->dynamic_masses.find(i);
        if (it!=resources_->dynamic_masses.end()) {
            btVector3 inertia(0,0,0); body->getCollisionShape()->calculateLocalInertia(it->second,inertia);
            body->setMassProps(it->second,inertia); resources_->dynamic_masses.erase(it);
        }
    }
    body->updateInertiaTensor(); body->setActivationState(DISABLE_DEACTIVATION);
    body_static_[i]=body->isStaticObject();
    world_->getDynamicsWorld()->addRigidBody(body);
}

void PhysicsEngine::setKinematicTarget(const std::string& id,const simcore::RigidState& target) {
    const auto i=indexOf(id);
    if (!bodyActive(i) || !static_cast<btRigidBody*>(bodies_[i])->isKinematicObject() || target.time<=time_ ||
        !simcore::finite(target.position)) throw std::invalid_argument("invalid kinematic target");
    simcore::normalized(target.orientation);
    resources_->targets[i]={stateAtIndex(i,time_),target};
}

vsg::dvec3 PhysicsEngine::angularVelocity(const std::string& id) const {
    const auto* body=static_cast<btRigidBody*>(nativeBody(id));
    return body ? fromBt(body->getAngularVelocity()) : vsg::dvec3{};
}

void PhysicsEngine::setAngularVelocity(const std::string& id,const vsg::dvec3& omega) {
    if (!simcore::finite(omega)) throw std::invalid_argument("invalid angular velocity");
    if (auto* body=static_cast<btRigidBody*>(nativeBody(id))) { body->setAngularVelocity(toBt(omega)); body->activate(); }
}

bool PhysicsEngine::groundAt(double x,double z,double& height,vsg::dvec3& normal) const {
    struct StaticRay : btCollisionWorld::ClosestRayResultCallback {
        using ClosestRayResultCallback::ClosestRayResultCallback;
        bool needsCollision(btBroadphaseProxy* proxy) const override {
            auto* object=static_cast<btCollisionObject*>(proxy->m_clientObject);
            return object->isStaticObject() && ClosestRayResultCallback::needsCollision(proxy);
        }
    } ray(toBt(vsg::dvec3(x,20000,z)-physics_origin_),toBt(vsg::dvec3(x,-12000,z)-physics_origin_));
    if (!std::isfinite(x) || !std::isfinite(z)) return false;
    world_->getDynamicsWorld()->rayTest(ray.m_rayFromWorld,ray.m_rayToWorld,ray);
    if (!ray.hasHit()) return false;
    height=physics_origin_.y+ray.m_hitPointWorld.y();
    normal=fromBt(ray.m_hitNormalWorld);
    const auto length=vsg::length(normal);
    if (length<1e-12) return false;
    normal=normal/length;
    return true;
}

bool PhysicsEngine::bodyExists(const std::string& object_id) const {
    return bodyActive(indexOf(object_id));
}
const std::string& PhysicsEngine::bodyId(std::size_t i) const {
    static const std::string empty;
    return i<body_ids_.size() ? body_ids_[i] : empty;
}

vsg::dmat4 PhysicsEngine::transform(const std::string& object_id) const {
    const std::size_t i = indexOf(object_id);
    if (i >= transforms_.size()) return vsg::dmat4();
    return transforms_[i]->matrix;
}

vsg::ref_ptr<vsg::MatrixTransform> PhysicsEngine::transformNode(const std::string& object_id) const {
    const std::size_t i = indexOf(object_id);
    if (i >= transforms_.size()) return {};
    return transforms_[i];
}

vsg::dvec3 PhysicsEngine::position(const std::string& object_id) const {
    const std::size_t i = indexOf(object_id);
    if (i >= transforms_.size()) return {};
    const auto& m = transforms_[i]->matrix;
    // synchronizeTransforms has already removed the rotated COM offset.
    return vsg::dvec3(m[3][0], m[3][1], m[3][2]);
}

vsg::dquat PhysicsEngine::rotation(const std::string& object_id) const {
    const std::size_t i = indexOf(object_id);
    if (i >= transforms_.size()) return {};
    // Convert the 3x3 rotation block of the (y-up) transform matrix to a
    // quaternion (standard Shepperd / scalar-scaled algorithm).
    const auto& m = transforms_[i]->matrix;
    const double m00 = m[0][0], m01 = m[1][0], m02 = m[2][0];
    const double m10 = m[0][1], m11 = m[1][1], m12 = m[2][1];
    const double m20 = m[0][2], m21 = m[1][2], m22 = m[2][2];
    const double trace = m00 + m11 + m22;
    double q[4];
    if (trace > 0.0) {
        double s = std::sqrt(trace + 1.0) * 2.0;
        q[0] = (m21 - m12) / s;
        q[1] = (m02 - m20) / s;
        q[2] = (m10 - m01) / s;
        q[3] = 0.25 * s;
    } else if (m00 > m11 && m00 > m22) {
        double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q[0] = 0.25 * s;
        q[1] = (m01 + m10) / s;
        q[2] = (m02 + m20) / s;
        q[3] = (m21 - m12) / s;
    } else if (m11 > m22) {
        double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q[0] = (m01 + m10) / s;
        q[1] = 0.25 * s;
        q[2] = (m12 + m21) / s;
        q[3] = (m02 - m20) / s;
    } else {
        double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q[0] = (m02 + m20) / s;
        q[1] = (m12 + m21) / s;
        q[2] = 0.25 * s;
        q[3] = (m10 - m01) / s;
    }
    const double len = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (len > 1e-12)
        return vsg::dquat(q[0]/len, q[1]/len, q[2]/len, q[3]/len);
    return {};
}

vsg::dvec3 PhysicsEngine::velocity(const std::string& object_id) const {
    const std::size_t i = indexOf(object_id);
    if (i >= bodies_.size()) return {};
    const auto* body = static_cast<btRigidBody*>(bodies_[i]);
    const auto offset = body->getWorldTransform().getBasis() * toBt(coms_[i]);
    return fromBt(body->getLinearVelocity() - body->getAngularVelocity().cross(offset));
}

void PhysicsEngine::setVelocity(const std::string& object_id, const vsg::dvec3& v) {
    const std::size_t i = indexOf(object_id);
    if (i >= bodies_.size()) return;
    if (!simcore::finite(v)) throw std::invalid_argument("invalid body velocity");
    auto* body = static_cast<btRigidBody*>(bodies_[i]);
    body->setLinearVelocity(toBt(v) + body->getAngularVelocity().cross(body->getWorldTransform().getBasis()*toBt(coms_[i])));
    body->activate();
}

void PhysicsEngine::applyCentralForce(const std::string& object_id, const vsg::dvec3& f) {
    const std::size_t i = indexOf(object_id);
    if (i >= bodies_.size()) return;
    if (!simcore::finite(f)) throw std::invalid_argument("invalid body force");
    auto* body=static_cast<btRigidBody*>(bodies_[i]);
    body->applyCentralForce(toBt(f)); body->activate();
}

bool PhysicsEngine::raycast(const vsg::dvec3& from, const vsg::dvec3& to, vsg::dvec3& hit) const {
    vsg::dvec3 normal;
    return raycast(from, to, hit, normal);
}

bool PhysicsEngine::raycast(const vsg::dvec3& from, const vsg::dvec3& to,
                            vsg::dvec3& hit, vsg::dvec3& normal) const {
    btCollisionWorld::ClosestRayResultCallback cb(toBt(from-physics_origin_), toBt(to-physics_origin_));
    world_->getDynamicsWorld()->rayTest(cb.m_rayFromWorld, cb.m_rayToWorld, cb);
    if (!cb.hasHit()) return false;
    const btVector3& p = cb.m_hitPointWorld;
    hit = physics_origin_+vsg::dvec3(p.x(), p.y(), p.z());
    // m_hitNormalWorld 已指向射线来源 (Bullet 约定); 退化时取射线反向。
    const btVector3& n = cb.m_hitNormalWorld;
    const double nlen = std::sqrt(n.x() * n.x() + n.y() * n.y() + n.z() * n.z());
    if (nlen > 1e-9)
        normal = vsg::dvec3(n.x(), n.y(), n.z()) / nlen;
    else
        normal = vsg::normalize(to - from) * -1.0;
    return true;
}

void PhysicsEngine::shiftOrigin(const vsg::dvec3& origin) {
    if (!simcore::finite(origin)) throw std::invalid_argument("invalid physics origin");
    const auto previous=physics_origin_;
    auto* world=world_->getDynamicsWorld();
    for (int i=0;i<world->getNumCollisionObjects();++i) {
        auto* object=world->getCollisionObjectArray()[i];
        auto transform=object->getWorldTransform();
        transform.setOrigin(toBt(fromBt(transform.getOrigin())+previous-origin));
        object->setWorldTransform(transform); object->setInterpolationWorldTransform(transform);
        if (auto* body=btRigidBody::upcast(object))
            if (body->getMotionState()) body->getMotionState()->setWorldTransform(transform);
        world->updateSingleAabb(object);
    }
    physics_origin_=origin;
    synchronizeTransforms();
}

const std::vector<Vec3>& PhysicsEngine::bodyLocalTriangles(std::size_t i) const {
    static const std::vector<Vec3> kEmpty;
    return i < body_tris_.size() ? body_tris_[i] : kEmpty;
}

std::vector<Vec3> PhysicsEngine::takeBodyTriangles(std::size_t i) {
    if (i >= body_tris_.size()) return {};
    return std::move(body_tris_[i]);
}

vsg::dmat4 PhysicsEngine::bodyPlacement(std::size_t i) const {
    if (i >= transforms_.size()) return vsg::dmat4();
    // 静态且未被平台接管: 创建时的安置矩阵固定不变;
    // 其余 (动态/被接管): MotionState 逐帧写入的当前世界位姿。
    if (i < body_static_.size() && body_static_[i] && !external_[i])
        return placers_[i];
    return transforms_[i]->matrix;
}

} // namespace vsgraysim
