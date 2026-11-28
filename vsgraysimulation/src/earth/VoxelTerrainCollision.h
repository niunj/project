#pragma once
#include <BulletCollision/CollisionDispatch/btCompoundCollisionAlgorithm.h>
#include <BulletCollision/CollisionDispatch/btCollisionObjectWrapper.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>
#include <BulletCollision/CollisionShapes/btConcaveShape.h>
#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <algorithm>
#include <vector>
#include <new>

namespace vsgraysim::earth::detail {
// Broad candidate rejection only. Every admitted child still uses Bullet's
// original convex/concave narrow phase, original mesh and original margins.
// No global Bullet callback or persistent simulation state is introduced.
class TerrainCompoundAlgorithm final : public btCompoundCollisionAlgorithm {
    std::vector<unsigned char> candidates_;
    std::vector<int> active_;
    std::vector<int> childOrder_;
    btManifoldArray manifolds_;
    bool fallback_=false;

    struct Triangles final : btTriangleCallback {
        const btDbvtNode* root;
        const btCompoundShape& compound;
        btTransform meshToCompound,compoundToMesh,meshInverse,compoundWorld;
        btMatrix3x3 absoluteBasis;
        btScalar padding,aabbPadding;
        btDbvtVolume referenceBounds;
        btVector3 meshWorldLow,meshWorldHigh;
        std::vector<unsigned char>& candidates;
        std::vector<const btDbvtNode*> stack;
        Triangles(const btDbvtNode* node,const btCompoundShape& shape,const btConcaveShape& mesh,const btTransform& meshWorld,const btTransform& world,btScalar pad,btScalar aabbPad,std::vector<unsigned char>& selected)
            :root(node),compound(shape),meshToCompound((meshWorld.inverse()*world).inverse()),compoundToMesh(meshWorld.inverse()*world),meshInverse(meshWorld.inverse()),compoundWorld(world),absoluteBasis(compoundToMesh.getBasis().absolute()),padding(pad),aabbPadding(aabbPad),candidates(selected){
            stack.reserve(128);btVector3 low,high;mesh.getAabb(world.inverse()*meshWorld,low,high);referenceBounds=btDbvtVolume::FromMM(low,high);mesh.getAabb(meshWorld,meshWorldLow,meshWorldHigh);
        }
        void processTriangle(btVector3* triangle,int,int)override {
            const btVector3 a=meshToCompound*triangle[0],b=meshToCompound*triangle[1],c=meshToCompound*triangle[2];
            btVector3 low=a,high=a;low.setMin(b);low.setMin(c);high.setMax(b);high.setMax(c);
            const btVector3 extra(padding,padding,padding);low-=extra;high+=extra;
            const auto bounds=btDbvtVolume::FromMM(low,high);
            btVector3 meshLow=triangle[0],meshHigh=triangle[0];meshLow.setMin(triangle[1]);meshLow.setMin(triangle[2]);meshHigh.setMax(triangle[1]);meshHigh.setMax(triangle[2]);
            const btVector3 meshExtra(aabbPadding,aabbPadding,aabbPadding);meshLow-=meshExtra;meshHigh+=meshExtra;
            const btVector3 n=(b-a).cross(c-a),absN=n.absolute();
            const btScalar plane=n.dot(a),tolerance=padding*(absN.x()+absN.y()+absN.z());
            stack.clear();if(root)stack.push_back(root);
            while(!stack.empty()) {
                const auto* node=stack.back();stack.pop_back();
                if(!Intersect(node->volume,bounds)||!Intersect(node->volume,referenceBounds))continue;
                // Mirror the original convex/concave triangle-AABB eligibility
                // in mesh coordinates. Do not inflate discovery by the much
                // larger persistent-contact breaking threshold.
                const auto centre=compoundToMesh*node->volume.Center(),extent=absoluteBasis*node->volume.Extents();
                if(!TestAabbAgainstAabb2(centre-extent,centre+extent,meshLow,meshHigh))continue;
                // An entire child-tree node on either side of the triangle
                // plane cannot contribute even a breaking-threshold contact.
                if(btFabs(n.dot(node->volume.Center())-plane)>absN.dot(node->volume.Extents())+tolerance)continue;
                if(node->isleaf()) {
                    const int index=node->dataAsInt;if(candidates[index])continue;
                    const auto world=compoundWorld*compound.getChildTransform(index);
                    btVector3 childLow,childHigh;
                    // Also retain the generic compound algorithm's original
                    // world-space child/mesh AABB gate, including its rounding.
                    compound.getChildShape(index)->getAabb(world,childLow,childHigh);
                    if(!TestAabbAgainstAabb2(childLow,childHigh,meshWorldLow,meshWorldHigh))continue;
                    compound.getChildShape(index)->getAabb(meshInverse*world,childLow,childHigh);
                    if(TestAabbAgainstAabb2(childLow,childHigh,meshLow,meshHigh))candidates[index]=1;
                }
                else {stack.push_back(node->childs[0]);stack.push_back(node->childs[1]);}
            }
        }
    };
public:
    BT_DECLARE_ALIGNED_ALLOCATOR();
    TerrainCompoundAlgorithm(const btCollisionAlgorithmConstructionInfo& ci,const btCollisionObjectWrapper* a,const btCollisionObjectWrapper* b,bool swapped)
        :btCompoundCollisionAlgorithm(ci,a,b,swapped){}
    void processCollision(const btCollisionObjectWrapper* a,const btCollisionObjectWrapper* b,const btDispatcherInfo& info,btManifoldResult* result)override {
        const auto* compoundWrap=m_isSwapped?b:a;const auto* meshWrap=m_isSwapped?a:b;
        const auto* compound=static_cast<const btCompoundShape*>(compoundWrap->getCollisionShape());
        const auto* tree=compound->getDynamicAabbTree();
        const int meshType=meshWrap->getCollisionShape()->getShapeType();
        if(fallback_||!tree||(meshType!=TRIANGLE_MESH_SHAPE_PROXYTYPE&&meshType!=FAST_CONCAVE_MESH_PROXYTYPE)||compound->getUpdateRevision()!=m_compoundShapeRevision||result->m_closestPointDistanceThreshold>0) {
            // Generic Bullet supports mutable/nested/fallback configurations.
            fallback_=true;btCompoundCollisionAlgorithm::processCollision(a,b,info,result);return;
        }
        const auto* mesh=static_cast<const btConcaveShape*>(meshWrap->getCollisionShape());
        if(childOrder_.empty()) {
            std::vector<const btDbvtNode*> stack;if(tree->m_root)stack.push_back(tree->m_root);
            while(!stack.empty()){const auto* node=stack.back();stack.pop_back();if(node->isleaf())childOrder_.push_back(node->dataAsInt);else{stack.push_back(node->childs[0]);stack.push_back(node->childs[1]);}}
        }
        candidates_.assign(compound->getNumChildShapes(),0);
        const btTransform compoundToMesh=meshWrap->getWorldTransform().inverse()*compoundWrap->getWorldTransform();
        btVector3 low,high;compound->getAabb(compoundToMesh,low,high);
        // Include both the dispatcher manifold threshold and floating-point
        // roundoff. Padding expands candidate search, never collision geometry.
        const btScalar breaking=btMax(gContactBreakingThreshold,btMin(compound->getContactBreakingThreshold(gContactBreakingThreshold),mesh->getContactBreakingThreshold(gContactBreakingThreshold)));
        const auto absoluteLow=low.absolute(),absoluteHigh=high.absolute();
        const btScalar scale=btMax(absoluteLow[absoluteLow.maxAxis()],absoluteHigh[absoluteHigh.maxAxis()]);
        const btScalar aabbPadding=mesh->getMargin()+btScalar(64)*SIMD_EPSILON*(btScalar(1)+scale);
        const btScalar padding=breaking+aabbPadding;
        const btVector3 extra(aabbPadding,aabbPadding,aabbPadding);low-=extra;high+=extra;
        Triangles triangles(tree->m_root,*compound,*mesh,meshWrap->getWorldTransform(),compoundWrap->getWorldTransform(),padding,aabbPadding,candidates_);
        mesh->processAllTriangles(&triangles,low,high);

        // Refresh surviving contacts even when no current triangle candidate
        // exists. This preserves Bullet's original manifold lifetime rules.
        for(int index:active_) {
            manifolds_.resize(0);m_childCollisionAlgorithms[index]->getAllContactManifolds(manifolds_);
            for(int i=0;i<manifolds_.size();++i)if(auto* manifold=manifolds_[i];manifold->getNumContacts()) {
                result->setPersistentManifold(manifold);result->refreshContactPoints();result->setPersistentManifold(nullptr);
            }
        }
        // Retain Bullet's child-tree order rather than triangle enumeration
        // order. Candidate discovery never controls support/solver ordering.
        for(int index:childOrder_)if(candidates_[index]) {
            const auto& local=compound->getChildTransform(index);const btTransform transform=compoundWrap->getWorldTransform()*local;
            btTransform preTransform=local;if(compoundWrap->m_preTransform)preTransform=preTransform*(*compoundWrap->m_preTransform);
            btCollisionObjectWrapper child(compoundWrap,compound->getChildShape(index),compoundWrap->getCollisionObject(),transform,preTransform,-1,index);
            auto*& algorithm=m_childCollisionAlgorithms[index];
            if(!algorithm){algorithm=m_dispatcher->findAlgorithm(&child,meshWrap,m_sharedManifold,BT_CONTACT_POINT_ALGORITHMS);active_.push_back(index);}
            const bool first=result->getBody0Internal()==compoundWrap->getCollisionObject();
            const auto* original=first?result->getBody0Wrap():result->getBody1Wrap();
            if(first){result->setBody0Wrap(&child);result->setShapeIdentifiersA(-1,index);}
            else {result->setBody1Wrap(&child);result->setShapeIdentifiersB(-1,index);}
            algorithm->processCollision(&child,meshWrap,info,result);
            if(first)result->setBody0Wrap(original);else result->setBody1Wrap(original);
        }
        // Release only empty, no-longer-candidate algorithms. Nonempty support
        // manifolds survive until the standard refresh removes their contacts.
        active_.erase(std::remove_if(active_.begin(),active_.end(),[&](int index){
            if(candidates_[index])return false;
            manifolds_.resize(0);m_childCollisionAlgorithms[index]->getAllContactManifolds(manifolds_);
            for(int i=0;i<manifolds_.size();++i)if(manifolds_[i]->getNumContacts())return false;
            m_childCollisionAlgorithms[index]->~btCollisionAlgorithm();m_dispatcher->freeCollisionAlgorithm(m_childCollisionAlgorithms[index]);m_childCollisionAlgorithms[index]=nullptr;return true;
        }),active_.end());
    }
    struct Create final : btCollisionAlgorithmCreateFunc {
        explicit Create(bool swapped){m_swapped=swapped;}
        btCollisionAlgorithm* CreateCollisionAlgorithm(btCollisionAlgorithmConstructionInfo& ci,const btCollisionObjectWrapper* a,const btCollisionObjectWrapper* b)override {
            // Bullet's default pool is sized for its built-in algorithms;
            // this larger adapter must use aligned fallback allocation.
            // The dispatcher already frees non-pool pointers with btAlignedFree.
            void* memory=btAlignedAlloc(sizeof(TerrainCompoundAlgorithm),16);
            if(!memory)throw std::bad_alloc();
            return new(memory) TerrainCompoundAlgorithm(ci,a,b,m_swapped);
        }
    };
};
}
