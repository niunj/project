/// Platform control acts on the scene's existing collider and model transform.
#include "PlatformPhysicsBridge.h"
#include <simcore/Orientation.h>
#include <btBulletDynamicsCommon.h>
#include <spdlog/spdlog.h>

namespace vsgraysim::platform {
struct PlatformPhysicsBridge::BodyRecord {
    std::string object_id;
    bool created = false;
    bool kinematic = false;
};

PlatformPhysicsBridge::PlatformPhysicsBridge(PhysicsEngine* physics) : physics_(physics) {}
PlatformPhysicsBridge::~PlatformPhysicsBridge() { removeAllBodies(); }

bool PlatformPhysicsBridge::createBody(uint32_t pid,const std::string& name,PlatformCategory category,
    const vsg::dvec3& pos,const vsg::dvec3& angles,const vsg::dvec3& half_extents) {
    if (!physics_ || records_.count(pid)) return false;
    auto record=std::make_unique<BodyRecord>();
    record->object_id=name;
    if (!physics_->bodyExists(name)) {
        simcore::RigidState initial;
        initial.position=pos; initial.orientation=simcore::headingRotation(angles.x,angles.y,angles.z);
        if (!physics_->createBoxBody(name,half_extents,initial,defaultMass(category))) return false;
        record->created=true;
    }
    // Mesh, collision shape, mass, visual node and object id remain a single body.
    auto* body=static_cast<btRigidBody*>(physics_->nativeBody(name));
    body->setActivationState(DISABLE_DEACTIVATION);
    body->setCcdMotionThreshold(.5f);
    body->setCcdSweptSphereRadius(.1f);
    records_.emplace(pid,std::move(record));
    return true;
}

void PlatformPhysicsBridge::removeBody(uint32_t pid) {
    auto it=records_.find(pid); if (it==records_.end()) return;
    const auto& record=*it->second;
    if (physics_ && physics_->bodyExists(record.object_id)) {
        if (record.created) physics_->removeBody(record.object_id);
        else {
            if (record.kinematic) physics_->setKinematic(record.object_id,false);
            physics_->setAngularVelocity(record.object_id,{});
            physics_->setVelocity(record.object_id,{});
        }
    }
    records_.erase(it);
}
void PlatformPhysicsBridge::removeAllBodies() {
    while (!records_.empty()) removeBody(records_.begin()->first);
}
void PlatformPhysicsBridge::setAuthority(uint32_t pid,bool kinematic) {
    auto it=records_.find(pid); if (it==records_.end()) return;
    if (it->second->kinematic!=kinematic) {
        physics_->setKinematic(it->second->object_id,kinematic);
        it->second->kinematic=kinematic;
    }
}
void PlatformPhysicsBridge::applyMotionCommand(uint32_t pid,const MotionState& desired,
    double end_time,double dt,bool kinematic) {
    auto it=records_.find(pid); if (it==records_.end()) return;
    const auto& id=it->second->object_id;
    setAuthority(pid,kinematic);
    if (kinematic) {
        simcore::RigidState target;
        target.time=simcore::fromSeconds(end_time);
        target.position=desired.position; target.orientation=desired.rotation;
        target.velocity=desired.velocity;
        physics_->setKinematicTarget(id,target);
    } else {
        const auto omega=simcore::angularVelocityBetween(physics_->rotation(id),desired.rotation,dt);
        physics_->setAngularVelocity(id,omega);
        physics_->setVelocity(id,desired.velocity);
    }
}
void PlatformPhysicsBridge::applyVelocity(uint32_t pid,const vsg::dvec3& velocity) {
    auto it=records_.find(pid); if (it!=records_.end()) physics_->setVelocity(it->second->object_id,velocity);
}
void PlatformPhysicsBridge::applyCentralForce(uint32_t pid,const vsg::dvec3& force) {
    auto it=records_.find(pid); if (it!=records_.end()) physics_->applyCentralForce(it->second->object_id,force);
}
bool PlatformPhysicsBridge::readMotionState(uint32_t pid,MotionState& state) const {
    auto it=records_.find(pid);
    if (it==records_.end() || !physics_->bodyExists(it->second->object_id)) return false;
    const auto& id=it->second->object_id;
    state.position=physics_->position(id); state.rotation=physics_->rotation(id); state.velocity=physics_->velocity(id);
    const auto angles=simcore::headingAngles(state.rotation);
    state.yaw=angles.x; state.pitch=angles.y; state.roll=angles.z;
    const auto omega=physics_->angularVelocity(id);
    // Rate components are world-frame for legacy equipment diagnostics.
    constexpr double deg=180/3.14159265358979323846;
    state.yaw_rate=-omega.y*deg; state.pitch_rate=omega.x*deg; state.roll_rate=-omega.z*deg;
    return true;
}
bool PlatformPhysicsBridge::bodyExists(uint32_t pid) const {
    auto it=records_.find(pid); return it!=records_.end() && physics_->bodyExists(it->second->object_id);
}
vsg::dvec3 PlatformPhysicsBridge::getPosition(uint32_t pid) const {
    auto it=records_.find(pid); return it==records_.end() ? vsg::dvec3{} : physics_->position(it->second->object_id);
}
vsg::dvec3 PlatformPhysicsBridge::getVelocity(uint32_t pid) const {
    auto it=records_.find(pid); return it==records_.end() ? vsg::dvec3{} : physics_->velocity(it->second->object_id);
}
vsg::dquat PlatformPhysicsBridge::getRotation(uint32_t pid) const {
    auto it=records_.find(pid); return it==records_.end() ? vsg::dquat{} : physics_->rotation(it->second->object_id);
}

// =============================================================================
// 默认参数
// =============================================================================
vsg::dvec3 PlatformPhysicsBridge::defaultExtents(PlatformCategory cat) {
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE: return {2.0, 1.5, 4.0};  // 坦克: 宽4m, 高1.5m, 长8m → half
        case PlatformCategory::AIRCRAFT:       return {8.0, 2.0, 8.0};  // 飞机
        case PlatformCategory::NAVAL_VESSEL:   return {5.0, 3.0, 25.0}; // 舰船
        case PlatformCategory::MISSILE:        return {0.3, 0.3, 3.0};  // 导弹
        default:                               return {1.0, 1.0, 1.0};
    }
}

float PlatformPhysicsBridge::defaultMass(PlatformCategory cat) {
    switch (cat) {
        case PlatformCategory::GROUND_VEHICLE: return 50000.0f;  // ~50吨
        case PlatformCategory::AIRCRAFT:       return 20000.0f;  // ~20吨
        case PlatformCategory::NAVAL_VESSEL:   return 5000000.0f; // ~5000吨
        case PlatformCategory::MISSILE:        return 500.0f;    // ~0.5吨
        default:                               return 10.0f;
    }
}

} // namespace vsgraysim::platform