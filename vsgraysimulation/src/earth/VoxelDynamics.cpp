#include "VoxelDynamics.h"
#include "VoxelTerrain.h"
#include "VoxelTerrainCollision.h"
#include "VoxelBuoyancy.h"
#include <vsgraysim/earth/VoxelOcean.h>
#include <btBulletDynamicsCommon.h>
#include <BulletCollision/Gimpact/btGImpactShape.h>
#include <BulletCollision/Gimpact/btGImpactCollisionAlgorithm.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <simcore/Orientation.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace vsgraysim::earth {
namespace {
btVector3 bt(const vsg::dvec3& p){return {btScalar(p.x),btScalar(p.y),btScalar(p.z)};}
vsg::dvec3 vec(const btVector3& p){return {p.x(),p.y(),p.z()};}
btTransform bt(const vsg::dmat4& m) {
    vsg::dvec3 p,s;vsg::dquat q;
    if(!vsg::decompose(m,p,q,s))throw std::invalid_argument("invalid rigid voxel transform");
    q=simcore::normalized(q);return btTransform(btQuaternion(btScalar(q.x),btScalar(q.y),btScalar(q.z),btScalar(q.w)),bt(p));
}
vsg::dmat4 matrix(const btTransform& t) {
    const auto q=t.getRotation();return vsg::translate(vec(t.getOrigin()))*vsg::rotate(simcore::normalized(vsg::dquat(q.x(),q.y(),q.z(),q.w())));
}
struct Shape {
    std::shared_ptr<const std::vector<HydroBox>> hydro;
    std::shared_ptr<const std::vector<HydroNode>> hydroTree;
    std::shared_ptr<const std::vector<HydroBox>> pressureColumns;
    std::shared_ptr<const std::vector<HydroNode>> pressureTree;
    double hydroSpan=0;
    std::weak_ptr<const EarthSurfaceSnapshot> source;
    std::vector<std::unique_ptr<btBoxShape>> boxes;
    std::vector<std::unique_ptr<btConvexHullShape>> hulls;bool reconstructed=false;
    struct CellHull {vsg::dvec3 low,high;std::vector<vsg::dvec4> planes;};std::vector<CellHull> hullCells;
    btTransform gridToBody;
    std::unique_ptr<btCompoundShape> compound;
    std::unique_ptr<btTriangleMesh> triangles;
    std::unique_ptr<btBvhTriangleMeshShape> terrain;
    std::unique_ptr<btGImpactMeshShape> contactMesh;
    btTransform bodyToModel; // principal axes at the actual occupied-volume COM
    btVector3 inertiaPerDensity;
    btCollisionShape* collision()const{return contactMesh?static_cast<btCollisionShape*>(contactMesh.get()):terrain?static_cast<btCollisionShape*>(terrain.get()):compound.get();}
};
std::shared_ptr<Shape> makeTerrainShape(const VoxelSceneObject& object) {
    auto result=std::make_shared<Shape>();result->source=object.geometry;
    result->triangles=std::make_unique<btTriangleMesh>();
    const auto& mesh=object.surface();const auto origin=object.volume->gridOrigin();
    const auto& v=mesh.vertices();const auto& indices=mesh.indices();
    if(indices.empty())throw std::invalid_argument("empty HD Earth collider");
    for(size_t i=0;i<indices.size();i+=3)
        result->triangles->addTriangle(bt(v[indices[i]]-origin),bt(v[indices[i+1]]-origin),bt(v[indices[i+2]]-origin),false);
    result->terrain=std::make_unique<btBvhTriangleMeshShape>(result->triangles.get(),true);result->terrain->setMargin(0);
    result->bodyToModel.setIdentity();result->bodyToModel.setOrigin(bt(origin));result->inertiaPerDensity={0,0,0};return result;
}
// Recenter the same collision geometry around the combined material/ballast
// mass tensor. No shape, margin, occupied cell or contact precision changes.
void addBallast(vsg::dvec3& centre,double tensor[3][3],const VoxelTerrain& volume,double equivalentVolume,const vsg::dvec3& position){
    if(equivalentVolume<=0)return;const double cell=volume.resolution_m,V=volume.occupiedCount()*cell*cell*cell;const auto point=position-volume.gridOrigin();const auto combined=(centre*V+point*equivalentVolume)/(V+equivalentVolume);const auto a=centre-combined,b=point-combined;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)tensor[i][j]+=V*(i==j?vsg::dot(a,a)-a[i]*a[i]:-a[i]*a[j])+equivalentVolume*(i==j?vsg::dot(b,b)-b[i]*b[i]:-b[i]*b[j]);centre=combined;
}
// Projected voxel corner quads can be non-planar or locally concave.
// A convex hull of eight such corners changes the canonical visible surface.
// GImpact consumes exactly the published triangulation, while mass/inertia
// remain the occupied voxel material, not an AABB or triangle-shell estimate.
std::shared_ptr<Shape> makeReconstructedShape(const std::shared_ptr<const VoxelTerrain>& volume,double ballast,const vsg::dvec3& ballastPosition){
    auto shape=std::make_shared<Shape>();shape->source=volume;shape->reconstructed=true;const auto size=volume->dimensions();const double cell=volume->resolution_m,mass=cell*cell*cell;auto centre=volume->centreOfMass()-volume->gridOrigin();double tensor[3][3]{};const auto& occupied=volume->occupancy();
    for(int z=0;z<size[2];++z)for(int y=0;y<size[1];++y)for(int x=0;x<size[0];++x)if(occupied[(size_t(z)*size[1]+y)*size[0]+x]){const auto r=vsg::dvec3(x+.5,y+.5,z+.5)*cell-centre;for(int a=0;a<3;++a)for(int b=0;b<3;++b)tensor[a][b]+=mass*(a==b?(cell*cell/6.+vsg::dot(r,r)-r[a]*r[a]):-r[a]*r[b]);}
    addBallast(centre,tensor,*volume,ballast,ballastPosition);
    btMatrix3x3 inertia{btScalar(tensor[0][0]),btScalar(tensor[0][1]),btScalar(tensor[0][2]),btScalar(tensor[1][0]),btScalar(tensor[1][1]),btScalar(tensor[1][2]),btScalar(tensor[2][0]),btScalar(tensor[2][1]),btScalar(tensor[2][2])};btTransform principal;principal.setIdentity();principal.setOrigin(bt(centre));inertia.diagonalize(principal.getBasis(),btScalar(.00001),32);shape->inertiaPerDensity={inertia[0][0],inertia[1][1],inertia[2][2]};shape->gridToBody=principal.inverse();btTransform origin;origin.setIdentity();origin.setOrigin(bt(volume->gridOrigin()));shape->bodyToModel=origin*principal;
    shape->triangles=std::make_unique<btTriangleMesh>();const auto& points=volume->vertices();const auto& indices=volume->indices();size_t triangles=0;
    for(size_t i=0;i+2<indices.size();i+=3){const auto a=points[indices[i]],b=points[indices[i+1]],c=points[indices[i+2]];if(vsg::length(vsg::cross(b-a,c-a))<1e-12)continue;shape->triangles->addTriangle(shape->gridToBody*bt(a-volume->gridOrigin()),shape->gridToBody*bt(b-volume->gridOrigin()),shape->gridToBody*bt(c-volume->gridOrigin()),false);++triangles;}
    if(!triangles)throw std::invalid_argument("reconstructed voxel surface has no finite contact triangles");shape->contactMesh=std::make_unique<btGImpactMeshShape>(shape->triangles.get());shape->contactMesh->setMargin(btScalar(.00025));shape->contactMesh->updateBound();return shape;
}
std::shared_ptr<Shape> makeShape(const std::shared_ptr<const VoxelTerrain>& volume,double ballast,const vsg::dvec3& ballastPosition) {
    if(volume->appearance()&&volume->appearance()->reconstructed()&&!std::getenv("VSGRAYSIM_REFERENCE_CONVEX_SKIN"))return makeReconstructedShape(volume,ballast,ballastPosition);
    auto shape=std::make_shared<Shape>();shape->source=volume;shape->compound=std::make_unique<btCompoundShape>();
    shape->compound->setMargin(0);
    const auto n=volume->dimensions();const auto& occupied=volume->occupancy();std::vector<bool> used(occupied.size(),false);
    auto at=[&](int x,int y,int z){return (size_t(z)*n[1]+y)*n[0]+x;};
    const auto appearance=volume->appearance();shape->reconstructed=appearance&&!appearance->corners.empty();
    auto deformed=[&](int x,int y,int z){if(!shape->reconstructed)return false;for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto point=volume->gridOrigin()+vsg::dvec3(x+dx,y+dy,z+dz)*volume->resolution_m;if(const auto* c=appearance->corner(point);c&&vsg::length(c->position-point)>1e-9)return true;}return false;};
    std::vector<bool> skin(occupied.size(),false);if(shape->reconstructed)for(int z=0;z<n[2];++z)for(int y=0;y<n[1];++y)for(int x=0;x<n[0];++x)if(occupied[at(x,y,z)])skin[at(x,y,z)]=deformed(x,y,z);
    auto available=[&](int x,int y,int z){return occupied[at(x,y,z)]&&!used[at(x,y,z)]&&!skin[at(x,y,z)];};
    const double cell=volume->resolution_m;
    struct MassBox {vsg::dvec3 centre,size;double mass;};std::vector<MassBox> massBoxes;
    // Interior cells use disjoint cuboids; boundary cells use the same eight
    // reconstructed corners as the skin. Never hull the entire concave target.
    for(int z=0;z<n[2];++z)for(int y=0;y<n[1];++y)for(int x=0;x<n[0];++x)if(occupied[at(x,y,z)]&&!used[at(x,y,z)]) {
        int endX=x+1,endY=y+1,endZ=z+1;
        if(!skin[at(x,y,z)]) {
        while(endX<n[0]&&available(endX,y,z))++endX;
        while(endY<n[1]) {bool full=true;for(int i=x;i<endX;++i)if(!available(i,endY,z)){full=false;break;}if(!full)break;++endY;}
        while(endZ<n[2]) {bool full=true;for(int j=y;j<endY&&full;++j)for(int i=x;i<endX;++i)if(!available(i,j,endZ)){full=false;break;}if(!full)break;++endZ;}
        }
        if(skin[at(x,y,z)]) {
            // Grow along each axis independently. A flat span can merge even
            // when the chord or nose is curved. Every occupied grid point must
            // fit the same trilinear corner field to 0.25 mm; never cross a hole.
            auto point=[&](int i,int j,int k){auto p=volume->gridOrigin()+vsg::dvec3(i,j,k)*cell;if(const auto* source=appearance->corner(p))p=source->position;return p;};
            auto acceptable=[&](const std::array<int,3>& end) {
                for(int k=z;k<end[2];++k)for(int j=y;j<end[1];++j)for(int i=x;i<end[0];++i)if(!occupied[at(i,j,k)]||used[at(i,j,k)])return false;
                std::array<vsg::dvec3,8> corner;
                for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)corner[(dz*2+dy)*2+dx]=point(dx?end[0]:x,dy?end[1]:y,dz?end[2]:z);
                for(int k=z;k<=end[2];++k)for(int j=y;j<=end[1];++j)for(int i=x;i<=end[0];++i) {
                    const double u=double(i-x)/(end[0]-x),v=double(j-y)/(end[1]-y),w=double(k-z)/(end[2]-z);vsg::dvec3 interpolated{};
                    for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)interpolated+=corner[(dz*2+dy)*2+dx]*((dx?u:1-u)*(dy?v:1-v)*(dz?w:1-w));
                    if(vsg::length(interpolated-point(i,j,k))>.00025)return false;
                }
                return true;
            };
            std::array<int,3> end{x+1,y+1,z+1},start{x,y,z},axes{0,1,2};std::sort(axes.begin(),axes.end(),[&](int a,int b){return n[a]>n[b];});
            for(int axis:axes)while(end[axis]<n[axis]&&end[axis]-start[axis]<16){auto candidate=end;++candidate[axis];if(!acceptable(candidate))break;end=candidate;}
            endX=end[0];endY=end[1];endZ=end[2];
        }
        if(shape->boxes.size()+shape->hulls.size()>=65536)throw std::runtime_error("voxel body exceeds 65536 exact collision cuboids");
        if(skin[at(x,y,z)]) {
            auto hull=std::make_unique<btConvexHullShape>();std::vector<vsg::dvec3> points;
            const auto centre=vsg::dvec3(x+endX,y+endY,z+endZ)*(cell*.5);
            for(int dz=0;dz<2;++dz)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
                auto point=volume->gridOrigin()+vsg::dvec3(dx?endX:x,dy?endY:y,dz?endZ:z)*cell;
                if(const auto* source=appearance->corner(point))point=source->position;
                const auto p=point-volume->gridOrigin();hull->addPoint(bt(p-centre),false);points.push_back(vec(bt(p)));
            }
            Shape::CellHull cell;cell.low=cell.high=points.front();
            for(const auto& p:points)for(int a=0;a<3;++a){cell.low[a]=std::min(cell.low[a],p[a]);cell.high[a]=std::max(cell.high[a],p[a]);}
            for(size_t a=0;a<points.size();++a)for(size_t b=a+1;b<points.size();++b)for(size_t c=b+1;c<points.size();++c) {
                auto n=vsg::cross(points[b]-points[a],points[c]-points[a]);if(vsg::length(n)<1e-12)continue;n=vsg::normalize(n);double d=vsg::dot(n,points[a]),lo=0,hi=0;
                for(const auto& p:points){const auto side=vsg::dot(n,p)-d;lo=std::min(lo,side);hi=std::max(hi,side);}
                if(lo< -1e-7&&hi>1e-7)continue;
                if(hi>1e-7){n=-n;d=-d;}
                bool duplicate=false;for(const auto& plane:cell.planes)if(vsg::length(vsg::dvec3(plane.x,plane.y,plane.z)-n)<1e-5&&std::abs(plane.w-d)<1e-6){duplicate=true;break;}
                if(!duplicate)cell.planes.emplace_back(n,d);
                if(lo>=-1e-7&&hi<=1e-7)cell.planes.emplace_back(-n,-d);
            }
            shape->hullCells.push_back(std::move(cell));
            // Bullet computes compound mass centres from child transforms,
            // not from off-centre vertices inside a hull. Recenter the support
            // points and compensate in the child transform: geometry is the
            // same, while mass remains at the occupied-cell centre.
            hull->setMargin(0);hull->recalcLocalAabb();btTransform local;local.setIdentity();local.setOrigin(bt(centre));
            shape->compound->addChildShape(local,hull.get());shape->hulls.push_back(std::move(hull));
        } else {
            const auto half=vsg::dvec3(endX-x,endY-y,endZ-z)*(cell*.5);
            auto box=std::make_unique<btBoxShape>(bt(half));box->setMargin(btScalar(std::min(cell*.01,.002)));
            btTransform local;local.setIdentity();local.setOrigin(bt(vsg::dvec3(x+endX,y+endY,z+endZ)*(cell*.5)));
            shape->compound->addChildShape(local,box.get());shape->boxes.push_back(std::move(box));
        }
        massBoxes.push_back({vsg::dvec3(x+endX,y+endY,z+endZ)*(cell*.5),vsg::dvec3(endX-x,endY-y,endZ-z)*cell,
                            (endX-x)*double(endY-y)*(endZ-z)*cell*cell*cell});
        for(int k=z;k<endZ;++k)for(int j=y;j<endY;++j)for(int i=x;i<endX;++i)used[at(i,j,k)]=true;
    }
    if(shape->boxes.empty()&&shape->hulls.empty())throw std::invalid_argument("empty voxel collider");
    btTransform principal;principal.setIdentity();
    // Durable mass is occupied-cell volume, including a cell's intrinsic
    // inertia. Reconstructed, sometimes almost planar hulls are contact skins;
    // their AABBs must not redefine the inertia of the voxel material.
    auto centre=volume->centreOfMass()-volume->gridOrigin();double tensor[3][3]{};
    for(const auto& box:massBoxes) {
        const auto r=box.centre-centre;
        for(int a=0;a<3;++a)for(int b=0;b<3;++b)
            tensor[a][b]+=box.mass*(a==b?((box.size[(a+1)%3]*box.size[(a+1)%3]+box.size[(a+2)%3]*box.size[(a+2)%3])/12+vsg::dot(r,r)-r[a]*r[a]):-r[a]*r[b]);
    }
    addBallast(centre,tensor,*volume,ballast,ballastPosition);
    btMatrix3x3 inertia{btScalar(tensor[0][0]),btScalar(tensor[0][1]),btScalar(tensor[0][2]),
                        btScalar(tensor[1][0]),btScalar(tensor[1][1]),btScalar(tensor[1][2]),
                        btScalar(tensor[2][0]),btScalar(tensor[2][1]),btScalar(tensor[2][2])};
    principal.setOrigin(bt(centre));inertia.diagonalize(principal.getBasis(),btScalar(.00001),32);
    shape->inertiaPerDensity={inertia[0][0],inertia[1][1],inertia[2][2]};
    const auto inverse=principal.inverse();shape->gridToBody=inverse;
    for(int i=0;i<shape->compound->getNumChildShapes();++i)shape->compound->updateChildTransform(i,inverse*shape->compound->getChildTransform(i),false);
    shape->compound->recalculateLocalAabb();
    btTransform origin;origin.setIdentity();origin.setOrigin(bt(volume->gridOrigin()));shape->bodyToModel=origin*principal;
    return shape;
}
struct Body {
    std::string id;
    uint32_t ordinal=0;
    std::shared_ptr<Shape> shape;
    std::unique_ptr<btDefaultMotionState> motion;
    std::unique_ptr<btRigidBody> rigid;
};
// Dense curved skins can generate thousands of redundant cell/ground
// manifolds. Keep real support contacts spanning each normal cluster before
// building solver constraints. No contact is invented across empty volume.
class ContactWorld final:public btDiscreteDynamicsWorld {
    std::vector<unsigned char> contactMasks_,selected_;
    std::vector<btPersistentManifold*> solverManifolds_;
public:
    using btDiscreteDynamicsWorld::btDiscreteDynamicsWorld;
    bool trace=false;double collisionMs=0,reductionMs=0,solverMs=0;
    size_t rawContacts=0,keptContacts=0,maxManifolds=0;
    void performDiscreteCollisionDetection()override {
        const auto start=std::chrono::steady_clock::now();btDiscreteDynamicsWorld::performDiscreteCollisionDetection();
        if(trace)collisionMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    void solveConstraints(btContactSolverInfo& info)override {
        const auto start=std::chrono::steady_clock::now();
        if(trace){maxManifolds=std::max(maxManifolds,size_t(getDispatcher()->getNumManifolds()));for(int i=0;i<getDispatcher()->getNumManifolds();++i)rawContacts+=getDispatcher()->getManifoldByIndexInternal(i)->getNumContacts();}
        struct Contact {int manifold,index;btVector3 p;double depth;};
        using Key=std::tuple<uint32_t,uint32_t,int,int,int,int>;std::map<Key,std::vector<Contact>> groups;
        const int count=getDispatcher()->getNumManifolds();contactMasks_.assign(count,0);selected_.assign(count,0);
        for(int i=0;i<getDispatcher()->getNumManifolds();++i) {
            auto* manifold=getDispatcher()->getManifoldByIndexInternal(i);
            if(!manifold->getNumContacts())continue;
            const auto* a=static_cast<const Body*>(manifold->getBody0()->getUserPointer());const auto* b=static_cast<const Body*>(manifold->getBody1()->getUserPointer());
            if(!a||!b||(!a->shape->reconstructed&&!b->shape->reconstructed))continue;
            selected_[i]=1;
            for(int j=0;j<manifold->getNumContacts();++j) {
                const auto& point=manifold->getContactPoint(j);auto n=point.m_normalWorldOnB;if(a->ordinal>b->ordinal)n=-n;
                int axis=0;for(int k=1;k<3;++k)if(btFabs(n[k])>btFabs(n[axis]))axis=k;
                // Keep distinct terrain/support normals in separate patches;
                // a dominant-axis bin alone can merge a slope and its cut wall.
                groups[{std::min(a->ordinal,b->ordinal),std::max(a->ordinal,b->ordinal),axis*2+(n[axis]>0),int(std::lround(n.x()*8)),int(std::lround(n.y()*8)),int(std::lround(n.z()*8))}].push_back({i,j,(point.getPositionWorldOnA()+point.getPositionWorldOnB())*.5,-double(point.getDistance())});
            }
        }
        for(auto& [key,points]:groups) {
            // A support tie must not depend on allocation order or on how
            // many empty child manifolds the broad phase happened to create.
            std::sort(points.begin(),points.end(),[](const Contact& a,const Contact& b){return std::tie(a.p.x(),a.p.y(),a.p.z(),a.depth)<std::tie(b.p.x(),b.p.y(),b.p.z(),b.depth);});
            auto add=[&](size_t i){contactMasks_[points[i].manifold]|=static_cast<unsigned char>(1u<<points[i].index);};
            if(points.size()<=16){for(size_t i=0;i<points.size();++i)add(i);continue;}
            size_t deepest=0;for(size_t i=1;i<points.size();++i)if(points[i].depth>points[deepest].depth)deepest=i;add(deepest);
            for(int z:{-1,1})for(int y:{-1,1})for(int x:{-1,1}) {
                const btVector3 direction(x,y,z);size_t best=0;
                for(size_t i=1;i<points.size();++i)if(points[i].p.dot(direction)>points[best].p.dot(direction))best=i;add(best);
            }
        }
        for(int m=0;m<count;++m)if(selected_[m]){auto* manifold=getDispatcher()->getManifoldByIndexInternal(m);for(int i=manifold->getNumContacts()-1;i>=0;--i)if(!(contactMasks_[m]&(1u<<i)))manifold->removeContactPoint(i);}
        const auto solve=std::chrono::steady_clock::now();
        if(trace){reductionMs+=std::chrono::duration<double,std::milli>(solve-start).count();for(int i=0;i<getDispatcher()->getNumManifolds();++i)keptContacts+=getDispatcher()->getManifoldByIndexInternal(i)->getNumContacts();}
        if(getNumConstraints())btDiscreteDynamicsWorld::solveConstraints(info);
        else {
            // This voxel world has contact constraints only. Feed exactly the
            // retained supports to the same Bullet sequential-impulse solver,
            // without empty manifolds or an allocation-dependent island sort.
            solverManifolds_.clear();
            for(int i=0;i<count;++i){auto* m=getDispatcher()->getManifoldByIndexInternal(i);if(m->getNumContacts()&&getDispatcher()->needsResponse(m->getBody0(),m->getBody1()))solverManifolds_.push_back(m);}
            auto key=[](const btPersistentManifold* m){const auto* a=static_cast<const Body*>(m->getBody0()->getUserPointer());const auto* b=static_cast<const Body*>(m->getBody1()->getUserPointer());const auto& c=m->getContactPoint(0);return std::make_tuple(a->ordinal,b->ordinal,c.m_index0,c.m_index1,c.m_localPointA.x(),c.m_localPointA.y(),c.m_localPointA.z());};
            std::sort(solverManifolds_.begin(),solverManifolds_.end(),[&](const auto* a,const auto* b){return key(a)<key(b);});
            // Bullet also integrates external force impulses in solveGroup;
            // free-flight bodies must participate even with zero contacts.
            auto& bodies=getCollisionObjectArray();m_constraintSolver->prepareSolve(bodies.size(),int(solverManifolds_.size()));
            m_constraintSolver->solveGroup(bodies.size()?&bodies[0]:nullptr,bodies.size(),solverManifolds_.data(),int(solverManifolds_.size()),nullptr,0,info,getDebugDrawer(),getDispatcher());
            m_constraintSolver->allSolved(info,getDebugDrawer());
        }
        if(trace)solverMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solve).count();
    }
};
struct World {
    btDefaultCollisionConfiguration configuration;
    detail::TerrainCompoundAlgorithm::Create terrainCompound{false},swappedTerrainCompound{true};
    btCollisionDispatcher dispatcher{&configuration};
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    ContactWorld world{&dispatcher,&broadphase,&solver,&configuration};
    std::vector<Body> bodies;
    vsg::dvec3 origin{};
    explicit World(bool indexedTerrain){
        btGImpactCollisionAlgorithm::registerAlgorithm(&dispatcher);
        if(indexedTerrain){dispatcher.registerCollisionCreateFunc(COMPOUND_SHAPE_PROXYTYPE,TRIANGLE_MESH_SHAPE_PROXYTYPE,&terrainCompound);dispatcher.registerCollisionCreateFunc(TRIANGLE_MESH_SHAPE_PROXYTYPE,COMPOUND_SHAPE_PROXYTYPE,&swappedTerrainCompound);}
        world.getSolverInfo().m_numIterations=20;world.getSolverInfo().m_solverMode&=~SOLVER_USE_WARMSTARTING;world.getSolverInfo().m_splitImpulse=true;world.getSolverInfo().m_splitImpulsePenetrationThreshold=0;
    }
    ~World(){for(auto& b:bodies)world.removeRigidBody(b.rigid.get());}
    btTransform placement(const VoxelSceneObject& object,uint64_t tick,double step)const {
        auto m=object.pose(tick,step);m[3][0]-=origin.x;m[3][1]-=origin.y;m[3][2]-=origin.z;return bt(m);
    }
    void contacts(VoxelSceneSnapshot& candidate,double maximumDistance=.001) {
        candidate.contacts.clear();
        for(int i=0;i<dispatcher.getNumManifolds();++i) {
            const auto* manifold=dispatcher.getManifoldByIndexInternal(i);
            const auto* a=static_cast<const Body*>(manifold->getBody0()->getUserPointer());
            const auto* b=static_cast<const Body*>(manifold->getBody1()->getUserPointer());
            if(!a||!b)continue;
            for(int p=0;p<manifold->getNumContacts();++p) {
                const auto& c=manifold->getContactPoint(p);if(c.getDistance()>maximumDistance)continue;
                if(candidate.contacts.size()>=65536)throw std::runtime_error("voxel contact budget exceeded");
                candidate.contacts.push_back({a->id,b->id,vec(c.getPositionWorldOnA())+origin,
                    vec(c.getPositionWorldOnB())+origin,vec(c.m_normalWorldOnB),double(c.getAppliedImpulse()),std::max(0.,-double(c.getDistance()))});
            }
        }
    }
};
}
struct VoxelDynamics::Impl {
    bool indexedTerrain=true;
    using ShapeKey=std::tuple<const EarthSurfaceSnapshot*,double,double,double,double>;
    std::map<ShapeKey,std::shared_ptr<Shape>> shapes;
    std::shared_ptr<Shape> shape(const VoxelSceneObject& o) {
        const bool hd=o.earth&&o.body.motion=="static";
        const std::shared_ptr<const EarthSurfaceSnapshot> source=hd?o.geometry:o.volume;
        const double ballast=o.body.ballastMassKg/o.material.at("density_kg_m3").get<double>();const auto p=o.body.ballastPosition;const ShapeKey key{source.get(),ballast,p.x,p.y,p.z};
        auto found=shapes.find(key);if(found!=shapes.end()&&found->second->source.lock()==source)return found->second;
        for(auto it=shapes.begin();it!=shapes.end();)if(it->second->source.expired())it=shapes.erase(it);else ++it;
        auto result=hd?makeTerrainShape(o):makeShape(o.volume,ballast,p);shapes[key]=result;return result;
    }
    std::unique_ptr<World> build(const VoxelSceneSnapshot& state) {
        auto result=std::make_unique<World>(indexedTerrain);result->world.setGravity(bt(state.gravity));
        for(const auto& [id,o]:state.objects)if(o.body.motion=="dynamic"&&o.volume->occupiedCount()) {
            const auto p=o.pose(state.tick,state.fixedStep)*o.localCentreOfMass();
            result->origin={std::round(p.x/1024)*1024,std::round(p.y/1024)*1024,std::round(p.z/1024)*1024};break;
        }
        result->bodies.reserve(state.objects.size());size_t boxes=0;
        for(const auto& [id,o]:state.objects) {
            if(!o.volume->occupiedCount()||o.surface().indices().empty())continue;
            Body body;body.id=id;body.ordinal=uint32_t(result->bodies.size());body.shape=shape(o);boxes+=body.shape->boxes.size()+body.shape->hulls.size();
            if(boxes>131072)throw std::runtime_error("voxel scene exceeds 131072 exact collision cuboids");
            const auto placement=result->placement(o,state.tick,state.fixedStep);
            if(vsg::length(vec(placement.getOrigin()))>1e6)throw std::runtime_error("voxel collision world exceeds local precision range; partition the scene");
            const auto transform=placement*body.shape->bodyToModel;
            body.motion=std::make_unique<btDefaultMotionState>(transform);
            const double density=o.material.at("density_kg_m3").get<double>();const bool dynamic=o.body.motion=="dynamic";
            btRigidBody::btRigidBodyConstructionInfo ci(btScalar(dynamic?o.massKg():0),body.motion.get(),body.shape->collision(),dynamic?body.shape->inertiaPerDensity*btScalar(density):btVector3(0,0,0));
            ci.m_friction=btScalar(o.body.friction);ci.m_restitution=btScalar(o.body.restitution);
            ci.m_linearDamping=btScalar(o.body.linearDamping);ci.m_angularDamping=btScalar(o.body.angularDamping);
            body.rigid=std::make_unique<btRigidBody>(ci);
            const auto offset=vec(placement.getBasis()*body.shape->bodyToModel.getOrigin());
            body.rigid->setLinearVelocity(bt(o.velocity+vsg::cross(o.angularVelocity,offset)));
            body.rigid->setAngularVelocity(bt(o.angularVelocity));body.rigid->setActivationState(DISABLE_DEACTIVATION);
            if(o.body.motion=="prescribed")body.rigid->setCollisionFlags(body.rigid->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
            result->bodies.push_back(std::move(body));auto& stored=result->bodies.back();
            stored.rigid->setUserPointer(&stored);result->world.addRigidBody(stored.rigid.get());
        }
        return result;
    }
};
VoxelDynamics::VoxelDynamics(bool indexedTerrain):impl_(std::make_unique<Impl>()){impl_->indexedTerrain=indexedTerrain;}
VoxelDynamics::~VoxelDynamics()=default;
void VoxelDynamics::step(const VoxelSceneSnapshot& previous,VoxelSceneSnapshot& next) {
    buoyancyMs=waterDragMs=waterMs=0;
    for(auto& [id,o]:next.objects)if(o.body.motion=="dynamic"&&!o.volume->occupiedCount()) {
        o.basePose=previous.objects.at(id).pose(previous.tick,previous.fixedStep);o.motionOriginTick=next.tick;o.velocity=vsg::dvec3{};o.angularVelocity=vsg::dvec3{};
    }
    const bool dynamic=std::any_of(previous.objects.begin(),previous.objects.end(),[](const auto& entry){return entry.second.body.motion=="dynamic"&&entry.second.volume->occupiedCount();});
    if(!dynamic){next.contacts.clear();VoxelOcean::advanceWakes(next,&previous);return;}
    auto staged=impl_->build(previous);
    staged->world.trace=std::getenv("VSGRAYSIM_VOXEL_CONTACT_TRACE")!=nullptr;
    double minimumCell=1e30,speed=0;
    for(const auto& body:staged->bodies) {
        const auto& o=previous.objects.at(body.id);minimumCell=std::min(minimumCell,o.volume->resolution_m);
        btVector3 low,high;btTransform identity;identity.setIdentity();body.shape->collision()->getAabb(identity,low,high);
        const double radius=std::max(double(low.length()),double(high.length()));
        speed=std::max(speed,double(body.rigid->getLinearVelocity().length())+double(body.rigid->getAngularVelocity().length())*radius);
    }
    speed+=vsg::length(previous.gravity)*previous.fixedStep;
    const double required=std::ceil(previous.fixedStep/std::min(1./120.,minimumCell*.25/std::max(speed,.001)));
    if(staged->world.trace)std::fprintf(stderr,"CONTACT begin tick=%llu speed=%.8g cell=%.8g steps=%.0f\n",(unsigned long long)next.tick,speed,minimumCell,required);
    if(required>256)throw std::runtime_error("voxel motion exceeds the 256 collision substep budget; reduce fixed_step_seconds");
    const int steps=std::max(1,int(required));const double dt=previous.fixedStep/steps;
    for(int s=0;s<steps;++s) {
        for(auto& body:staged->bodies) {
            const auto& o=previous.objects.at(body.id);
            if(previous.ocean&&o.body.motion=="dynamic") {
                // Exact occupied cuboids are cached independently of the
                // wave spectrum; waterline quadrature refines them on demand.
                const double span=2.;
                if(!body.shape->hydro||body.shape->hydroSpan!=span){body.shape->hydro=makeHydroBoxes(*o.volume,span);body.shape->hydroSpan=span;body.shape->hydroTree=makeHydroTree(*body.shape->hydro);body.shape->pressureColumns=makeHydroBoxes(*o.volume,span,true);body.shape->pressureTree=makeHydroTree(*body.shape->pressureColumns);}
                auto pose=matrix(body.rigid->getWorldTransform()*body.shape->bodyToModel.inverse());for(int a=0;a<3;++a)pose[3][a]+=staged->origin[a];
                const auto timing=applyVoxelWater(*body.rigid,*body.shape->hydro,pose,staged->origin,*previous.ocean,previous.timeSeconds()+s*dt,dt,previous.gravity,(std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO")?nullptr:body.shape->hydroTree.get()),std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO")?nullptr:body.shape->pressureColumns.get(),std::getenv("VSGRAYSIM_OCEAN_REFERENCE_HYDRO")?nullptr:body.shape->pressureTree.get());buoyancyMs+=timing.pressureMs;waterDragMs+=timing.dragMs;waterMs+=timing.totalMs;
            }
            if(o.body.motion!="prescribed")continue;
            const auto lead=(s+1)*dt;
            auto placement=o.rotor?drivenVoxelRotorPose(previous,o,lead):
                o.poseTrack?voxelTrackPose(previous,o,previous.timeSeconds()+lead):o.pose(previous.tick,previous.fixedStep);
            const auto delta=o.rotor||o.poseTrack?vsg::dvec3{}:o.velocity*lead;
            for(int a=0;a<3;++a)placement[3][a]+=delta[a]-staged->origin[a];
            const auto transform=bt(placement)*body.shape->bodyToModel;
            body.motion->setWorldTransform(transform);body.rigid->setWorldTransform(transform);
        }
        staged->world.stepSimulation(btScalar(dt),0,btScalar(dt));
    }
    for(const auto& body:staged->bodies) {
        auto& o=next.objects.at(body.id);if(o.body.motion!="dynamic")continue;
        auto model=matrix(body.rigid->getWorldTransform()*body.shape->bodyToModel.inverse());
        for(int a=0;a<3;++a)model[3][a]+=staged->origin[a];
        o.basePose=model;o.motionOriginTick=next.tick;o.angularVelocity=vec(body.rigid->getAngularVelocity());
        const auto offset=vec(body.rigid->getWorldTransform().getBasis()*body.shape->bodyToModel.inverse().getOrigin());
        o.velocity=vec(body.rigid->getLinearVelocity())+vsg::cross(o.angularVelocity,offset);
    }
    VoxelOcean::advanceWakes(next,&previous);
    staged->contacts(next);
    if(staged->world.trace) {
        double linear=0,angular=0,energy=0,depth=0;
        for(const auto& b:staged->bodies)if(b.rigid->getInvMass()>0){linear=std::max(linear,double(b.rigid->getLinearVelocity().length()));angular=std::max(angular,double(b.rigid->getAngularVelocity().length()));energy+=.5*double(b.rigid->getLinearVelocity().length2()/b.rigid->getInvMass());const auto w=b.rigid->getWorldTransform().getBasis().transpose()*b.rigid->getAngularVelocity();for(int a=0;a<3;++a)if(b.rigid->getInvInertiaDiagLocal()[a]>0)energy+=.5*double(w[a]*w[a]/b.rigid->getInvInertiaDiagLocal()[a]);}
        for(const auto& c:next.contacts)depth=std::max(depth,c.penetrationM);
        std::fprintf(stderr,"CONTACT end tick=%llu collision_ms=%.4f reduction_ms=%.4f solver_ms=%.4f manifolds=%zu raw=%zu kept=%zu linear=%.8g angular=%.8g energy=%.8g depth=%.8g\n",(unsigned long long)next.tick,staged->world.collisionMs,staged->world.reductionMs,staged->world.solverMs,staged->world.maxManifolds,staged->world.rawContacts,staged->world.keptContacts,linear,angular,energy,depth);
    }
}
void VoxelDynamics::detect(VoxelSceneSnapshot& next,double maximumContactDistance) {
    auto staged=impl_->build(next);staged->world.performDiscreteCollisionDetection();staged->contacts(next,maximumContactDistance);
}
void VoxelDynamics::impulse(VoxelSceneObject& object,uint64_t tick,double step,const vsg::dvec3& point,const vsg::dvec3& impulse) {
    if(object.body.motion!="dynamic")throw std::invalid_argument("impulse requires a dynamic voxel body");
    if(!object.volume->occupiedCount())return;
    auto s=impl_->shape(object);const auto placement=bt(object.pose(tick,step));const auto transform=placement*s->bodyToModel;
    const auto offset=vec(placement.getBasis()*s->bodyToModel.getOrigin());
    btRigidBody::btRigidBodyConstructionInfo ci(btScalar(object.massKg()),nullptr,s->collision(),s->inertiaPerDensity*btScalar(object.material.at("density_kg_m3").get<double>()));
    btRigidBody body(ci);body.setWorldTransform(transform);body.updateInertiaTensor();
    body.setLinearVelocity(bt(object.velocity+vsg::cross(object.angularVelocity,offset)));body.setAngularVelocity(bt(object.angularVelocity));
    // Subtract in the canonical double-precision model chart. Subtracting two
    // large float world positions gives a centre impulse a spurious lever,
    // particularly for thin, light fragments high above the chart origin.
    const auto model=object.pose(tick,step);
    const auto lever=model*vsg::dvec4(vsg::inverse(model)*point-object.localCentreOfMass(),0.);
    body.applyImpulse(bt(impulse),bt({lever.x,lever.y,lever.z}));
    object.angularVelocity=vec(body.getAngularVelocity());object.velocity=vec(body.getLinearVelocity())-vsg::cross(object.angularVelocity,offset);
}
std::optional<VoxelSceneHit> VoxelDynamics::raycast(const VoxelSceneSnapshot& snapshot,const vsg::dvec3& origin,const vsg::dvec3& direction,double distance) {
    auto staged=impl_->build(snapshot);const auto from=bt(origin-staged->origin),to=bt(origin+direction*distance-staged->origin);
    struct TerrainRay : btCollisionWorld::ClosestRayResultCallback {
        using ClosestRayResultCallback::ClosestRayResultCallback;
        bool needsCollision(btBroadphaseProxy* proxy)const override {
            if(!ClosestRayResultCallback::needsCollision(proxy))return false;
            const auto* object=static_cast<const btCollisionObject*>(proxy->m_clientObject);
            const auto* body=static_cast<const Body*>(object->getUserPointer());
            return body && body->shape->terrain;
        }
    } ray(from,to);
    ray.m_flags=btTriangleRaycastCallback::kF_UseGjkConvexCastRaytest|btTriangleRaycastCallback::kF_KeepUnflippedNormal;
    staged->world.rayTest(from,to,ray);
    std::optional<VoxelSceneHit> result;
    if(ray.hasHit()) {
        const auto* body=static_cast<const Body*>(ray.m_collisionObject->getUserPointer());
        result=VoxelSceneHit{body->id,double(ray.m_closestHitFraction)*distance,vec(ray.m_hitPointWorld)+staged->origin,vec(ray.m_hitNormalWorld)};
    }
    // GImpact uses these same triangles for contacts. Query their original
    // double-precision vertices and pose so that float recentering and Bullet's
    // triangle edge tolerance do not turn a grazing miss into a false hit.
    for (const auto& body : staged->bodies) {
        if (!body.shape->contactMesh) continue;
        const auto& object = snapshot.objects.at(body.id);
        const auto pose = object.pose(snapshot.tick, snapshot.fixedStep);
        const auto inverse = vsg::inverse(pose);
        const auto d = inverse * vsg::dvec4(direction, 0.);
        vsg::dvec3 localPoint, localNormal;
        if (!object.surface().intersectLocal(inverse * origin, {d.x, d.y, d.z}, distance,
                                             localPoint, localNormal)) continue;
        const auto point = pose * localPoint;
        const auto along = vsg::dot(point - origin, direction);
        if (result && along >= result->distance) continue;
        const auto normal = pose * vsg::dvec4(localNormal, 0.);
        result = VoxelSceneHit{body.id, along, point, {normal.x, normal.y, normal.z}};
    }
    // Use the supporting planes of the same Bullet cell hulls. A GJK ray cast
    // can miss thin boundary cells because its cast tolerance is too coarse.
    for(const auto& body:staged->bodies)if(body.shape->reconstructed) {
        const auto transform=matrix(body.rigid->getWorldTransform()*body.shape->gridToBody),inverse=vsg::inverse(transform);
        const auto p=inverse*(origin-staged->origin);const auto d4=inverse*vsg::dvec4(direction,0.);const vsg::dvec3 d(d4.x,d4.y,d4.z);
        for(const auto& cell:body.shape->hullCells) {
            double entry=0,exit=result?result->distance:distance;bool hit=true;vsg::dvec3 normal{};
            for(int a=0;a<3;++a) {
                if(std::abs(d[a])<1e-14){if(p[a]<cell.low[a]-1e-7||p[a]>cell.high[a]+1e-7){hit=false;break;}continue;}
                double near=(cell.low[a]-p[a])/d[a],far=(cell.high[a]-p[a])/d[a];if(near>far)std::swap(near,far);entry=std::max(entry,near);exit=std::min(exit,far);if(entry>exit+1e-7){hit=false;break;}
            }
            if(!hit)continue;
            for(const auto& plane:cell.planes) {
                const vsg::dvec3 n(plane.x,plane.y,plane.z);const double speed=vsg::dot(n,d),signedDistance=plane.w-vsg::dot(n,p);
                if(std::abs(speed)<1e-14){if(signedDistance< -1e-7){hit=false;break;}continue;}
                const double crossing=signedDistance/speed;
                if(speed<0){if(crossing>=entry-1e-7){entry=std::max(entry,crossing);normal=n;}}else exit=std::min(exit,crossing);
                if(entry>exit+1e-7){hit=false;break;}
            }
            if(!hit||entry<0||entry>distance||(result&&entry>=result->distance))continue;
            const auto n=transform*vsg::dvec4(normal,0.);result=VoxelSceneHit{body.id,entry,origin+direction*entry,{n.x,n.y,n.z}};
        }
    }
    // Raycast the actual Bullet cuboids analytically. GJK convex casts can skip
    // a thin cuboid at sub-metre resolutions; slab tests have no cast tolerance.
    for(const auto& body:staged->bodies)if(body.shape->compound) {
        for(int i=0;i<body.shape->compound->getNumChildShapes();++i) {
            const auto* box=dynamic_cast<const btBoxShape*>(body.shape->compound->getChildShape(i));if(!box)continue;
            const auto transform=body.rigid->getWorldTransform()*body.shape->compound->getChildTransform(i);
            const auto inverse=vsg::inverse(matrix(transform));
            const auto p=inverse*(origin-staged->origin);const auto d4=inverse*vsg::dvec4(direction,0.);
            const vsg::dvec3 d(d4.x,d4.y,d4.z);
            const auto half=vec(box->getHalfExtentsWithMargin());
            double entry=0,exit=result?result->distance:distance;int axis=-1;double sign=0;
            bool hit=true;
            for(int a=0;a<3;++a) {
                if(std::abs(d[a])<1e-14){if(p[a]<-half[a]||p[a]>half[a]){hit=false;break;}continue;}
                double near=(-half[a]-p[a])/d[a],far=(half[a]-p[a])/d[a];if(near>far)std::swap(near,far);
                if(near>entry){entry=near;axis=a;sign=d[a]>0?-1.:1.;}exit=std::min(exit,far);
                if(entry>exit){hit=false;break;}
            }
            if(!hit||entry<0||entry>distance||(result&&entry>=result->distance))continue;
            vsg::dvec3 normal{};if(axis>=0)normal[axis]=sign;
            const auto n=matrix(transform)*vsg::dvec4(normal,0.);
            result=VoxelSceneHit{body.id,entry,origin+direction*entry,{n.x,n.y,n.z}};
        }
    }
    return result;
}
}
