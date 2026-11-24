#pragma once
#include <vsg/maths/mat4.h>
#include <vector>
#include <memory>
class btRigidBody;
namespace vsgraysim::earth {
class VoxelTerrain;class VoxelOcean;
struct SubmergedBox {double fraction=0;vsg::dvec3 centre{};};
// Exact volume and first moment of an oriented box clipped by a plane.
SubmergedBox clipBuoyancyBox(const vsg::dvec3& size,const vsg::dmat4& pose,const vsg::dvec3& normal,double plane,bool referencePath=false,bool divideReference=false);
struct HydroBox {
    vsg::dvec3 low,size;
    struct Face {vsg::dvec3 centre,normal;double area=0;};
    std::vector<Face> faces;
};
std::shared_ptr<const std::vector<HydroBox>> makeHydroBoxes(const VoxelTerrain&,double maximumSpan,bool verticalColumns=false);
struct HydroNode {vsg::dvec3 low,size,moment;double volume=0;int left=-1,right=-1,box=-1;size_t count=0;};
std::shared_ptr<const std::vector<HydroNode>> makeHydroTree(const std::vector<HydroBox>&);
struct WaterTiming {double pressureMs=0,dragMs=0,totalMs=0;};
WaterTiming applyVoxelWater(btRigidBody&,const std::vector<HydroBox>&,const vsg::dmat4& modelToWorld,
                     const vsg::dvec3& physicsOrigin,const VoxelOcean&,double seconds,double dt,const vsg::dvec3& gravity,const std::vector<HydroNode>* tree=nullptr,
                     const std::vector<HydroBox>* pressureBoxes=nullptr,const std::vector<HydroNode>* pressureTree=nullptr);
}
