#include "VoxelDynamics.h"
#include "VoxelTerrain.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vsg/maths/transform.h>
#include <stdexcept>
namespace vsgraysim::earth {
namespace {
using json=nlohmann::json;
double field(const json& j,const char* key,double fallback,double low,double high) {
    const double value=j.value(key,fallback);
    if(!std::isfinite(value)||value<low||value>high)throw std::invalid_argument(std::string("invalid physics ")+key);
    return value;
}
struct Grid {
    const VoxelTerrain& volume;
    std::array<int,3> n;
    explicit Grid(const VoxelTerrain& v):volume(v),n(v.dimensions()){}
    uint32_t at(int x,int y,int z)const{return (uint32_t(z)*n[1]+y)*n[0]+x;}
    std::array<int,3> xyz(uint32_t i)const{return {int(i%n[0]),int(i/n[0]%n[1]),int(i/(n[0]*n[1]))};}
    int neighbour(uint32_t index,int axis,int sign)const {
        auto p=xyz(index);p[axis]+=sign;
        if(p[axis]<0||p[axis]>=n[axis])return -1;
        return int(at(p[0],p[1],p[2]));
    }
    vsg::dvec3 centre(uint32_t i)const {const auto p=xyz(i);return volume.gridOrigin()+vsg::dvec3(p[0]+.5,p[1]+.5,p[2]+.5)*volume.resolution_m;}
    bool solid(uint32_t i)const{return i<volume.cellCount()&&volume.occupancy()[i];}
};
bool containsBallast(const VoxelSceneObject& o){if(o.body.ballastMassKg<=0)return false;const auto p=(o.body.ballastPosition-o.volume->gridOrigin())/o.volume->resolution_m;const auto n=o.volume->dimensions();int i[3];for(int a=0;a<3;++a){i[a]=int(std::floor(p[a]));if(i[a]<0||i[a]>=n[a])return false;}return o.volume->occupancy()[(size_t(i[2])*n[1]+i[1])*n[0]+i[0]];}

}
void validateVoxelBody(VoxelSceneObject& object,const json& source,bool checkpoint) {
    const auto value=source.value("physics",json::object());
    if(!value.is_object())throw std::invalid_argument("physics must be an object");
    const std::set<std::string> allowed{"motion","fracture","support","fracture_energy_j_m2","friction","restitution","linear_damping","angular_damping","ballast","crush"};
    for(auto it=value.begin();it!=value.end();++it)if(!allowed.count(it.key()))throw std::invalid_argument("unknown physics field: "+it.key());
    auto& b=object.body;b.motion=value.value("motion",std::string("prescribed"));
    if(b.motion!="prescribed"&&b.motion!="static"&&b.motion!="dynamic")throw std::invalid_argument("invalid voxel motion mode");
    b.fracture=value.value("fracture",false);
    b.fractureEnergyPerArea=field(value,"fracture_energy_j_m2",1000,1e-6,1e12);
    b.friction=field(value,"friction",.6,0,2);b.restitution=field(value,"restitution",.1,0,1);
    b.linearDamping=field(value,"linear_damping",.02,0,1);b.angularDamping=field(value,"angular_damping",.02,0,1);
    b.crushRadius=0.;
    if(value.contains("crush")){
        const auto& crush=value.at("crush");
        if(!crush.is_object())throw std::invalid_argument("physics.crush must be an object");
        for(const auto& entry:crush.items())
            if(entry.key()!="radius_m"&&entry.key()!="yield_speed_mps"&&entry.key()!="max_strain")
                throw std::invalid_argument("unknown impact deformation option: "+entry.key());
        if(object.role!="target"||object.earth)throw std::invalid_argument("impact deformation requires a solid target");
        b.crushRadius=field(crush,"radius_m",2.,.05,50.);
        b.crushYieldSpeed=field(crush,"yield_speed_mps",4.,.1,100.);
        b.crushMaxStrain=field(crush,"max_strain",.65,.05,.85);
    }
    if(value.contains("ballast")){const auto& q=value.at("ballast");if(!q.is_object())throw std::invalid_argument("ballast must be an object");for(const auto& item:q.items())if(item.key()!="mass_kg"&&item.key()!="position_m")throw std::invalid_argument("unknown ballast field");b.ballastMassKg=field(q,"mass_kg",0,0,1e12);const auto& p=q.at("position_m");if(!p.is_array()||p.size()!=3)throw std::invalid_argument("ballast position needs three values");for(int a=0;a<3;++a){b.ballastPosition[a]=p[a].get<double>();if(!std::isfinite(b.ballastPosition[a])||std::abs(b.ballastPosition[a])>1e8)throw std::invalid_argument("invalid ballast position");}if(b.ballastMassKg>0&&!containsBallast(object))throw std::invalid_argument("ballast must be inside an occupied voxel");}
    Grid grid(*object.volume);
    const auto declaredSupport=value.value("support",std::string("none"));
    if(declaredSupport!="none"&&declaredSupport!="base"&&declaredSupport!="all")throw std::invalid_argument("invalid voxel support policy");
    b.anchorAll=declaredSupport=="all";
    if(checkpoint) {
        const auto anchors=source.value("anchor_cells",json::array()),bonds=source.value("broken_bonds",json::array());
        if(!anchors.is_array()||anchors.size()>object.volume->cellCount()||!bonds.is_array()||bonds.size()>object.volume->cellCount()*3)throw std::invalid_argument("invalid voxel bond/anchor arrays");
        for(const auto& a:anchors) {
            if(!a.is_number_unsigned()&&!a.is_number_integer())throw std::invalid_argument("invalid anchor index");
            if(a.get<double>()<0||a.get<double>()>=object.volume->cellCount())throw std::invalid_argument("anchor out of range");
            const auto index=a.get<uint32_t>();
            if(!grid.solid(index)||!object.anchors.insert(index).second)throw std::invalid_argument("duplicate or empty anchor cell");
        }
        for(const auto& b:bonds) {
            if(!b.is_number_integer()||b.get<double>()<0||b.get<double>()>=object.volume->cellCount()*3)throw std::invalid_argument("invalid bond index");
            const auto key=b.get<uint64_t>();const int next=grid.neighbour(uint32_t(key/3),int(key%3),1);
            if(next<0||!grid.solid(uint32_t(key/3))||!grid.solid(uint32_t(next))||!object.brokenBonds.insert(key).second)throw std::invalid_argument("invalid/duplicate broken face bond");
        }
        object.parentId=source.value("parent_id",std::string());
    } else {
        const auto support=value.value("support",std::string("none"));
        if(support!="none"&&support!="base"&&support!="all")throw std::invalid_argument("invalid voxel support policy");
        if(support=="all"||(support=="base"&&grid.n[1]==1))b.anchorAll=true;
        if(support!="none"&&!b.anchorAll)for(uint32_t i=0;i<object.volume->cellCount();++i)if(grid.solid(i)&&(support=="all"||(support=="base"&&grid.xyz(i)[1]==0)))object.anchors.insert(i);
    }
    if((!object.anchors.empty()||b.anchorAll)&&b.motion!="static")throw std::invalid_argument("anchored voxels require static motion");
    if(b.motion=="static"&&(vsg::length(object.velocity)>0||vsg::length(object.angularVelocity)>0))throw std::invalid_argument("static voxel body cannot have velocity");
    if(b.motion=="prescribed"&&vsg::length(object.angularVelocity)>0&&
       !object.rotor&&!object.poseTrack&&
       !(checkpoint&&(source.contains("rotor_drive")||source.contains("pose_track"))))
        throw std::invalid_argument("angular motion requires a rotor drive, pose track or dynamic mode");
}
json encodeVoxelBody(const VoxelSceneObject& o) {
    const auto& b=o.body;
    json encoded={{"motion",b.motion},{"fracture",b.fracture},{"support",b.anchorAll?"all":"none"},{"fracture_energy_j_m2",b.fractureEnergyPerArea},
        {"friction",b.friction},{"restitution",b.restitution},{"linear_damping",b.linearDamping},{"angular_damping",b.angularDamping}};
    if(b.ballastMassKg>0)encoded["ballast"]={{"mass_kg",b.ballastMassKg},{"position_m",{b.ballastPosition.x,b.ballastPosition.y,b.ballastPosition.z}}};
    if(b.crushRadius>0.)encoded["crush"]={{"radius_m",b.crushRadius},
        {"yield_speed_mps",b.crushYieldSpeed},{"max_strain",b.crushMaxStrain}};
    return encoded;
}
vsg::dvec3 VoxelSceneObject::localCentreOfMass()const {const double ballast=body.ballastMassKg;if(ballast<=0)return volume->centreOfMass();const double mass=massKg();return (volume->centreOfMass()*(mass-ballast)+body.ballastPosition*ballast)/mass;}

void pruneVoxelBodyTopology(VoxelSceneObject& original) {
    if(original.body.ballastMassKg>0&&!containsBallast(original))original.body.ballastMassKg=0;
    Grid grid(*original.volume);
    for(auto it=original.anchors.begin();it!=original.anchors.end();)if(!grid.solid(*it))it=original.anchors.erase(it);else ++it;
    for(auto it=original.brokenBonds.begin();it!=original.brokenBonds.end();) {
        const auto key=*it;const int next=grid.neighbour(uint32_t(key/3),int(key%3),1);
        if(next<0||!grid.solid(uint32_t(key/3))||!grid.solid(uint32_t(next)))it=original.brokenBonds.erase(it);else ++it;
    }
}
VoxelFractureResult fractureVoxelObject(VoxelSceneSnapshot& scene,const std::string& id,const json* event) {
    auto& original=scene.objects.at(id);VoxelFractureResult result;
    pruneVoxelBodyTopology(original);Grid grid(*original.volume);
    if(!original.body.fracture) {if(event)throw std::invalid_argument("enable physics.fracture before breaking face bonds");return result;}
    if(event) {
        auto vector=[](const json& j){return vsg::dvec3(j[0].get<double>(),j[1].get<double>(),j[2].get<double>());};
        auto point=vector(event->at("point_m")),normal=vector(event->at("normal"));
        if(event->value("space",std::string("object"))=="scene") {
            const auto inverse=vsg::inverse(original.pose(scene.tick,scene.fixedStep));point=inverse*point;
            const auto direction=inverse*vsg::dvec4(normal,0);normal=vsg::dvec3(direction.x,direction.y,direction.z);
        }
        normal=vsg::normalize(normal);const double radius=event->at("radius_m").get<double>();
        const double cost=original.body.fractureEnergyPerArea*std::pow(original.volume->resolution_m,2);
        std::vector<std::pair<double,uint64_t>> candidates;
        for(uint32_t i=0;i<original.volume->cellCount();++i)if(grid.solid(i))for(int axis=0;axis<3;++axis) {
            const int next=grid.neighbour(i,axis,1);const uint64_t bond=uint64_t(i)*3+axis;
            if(next<0||!grid.solid(uint32_t(next))||original.brokenBonds.count(bond))continue;
            const auto a=grid.centre(i)-point,b=grid.centre(uint32_t(next))-point;
            const double da=vsg::dot(a,normal),db=vsg::dot(b,normal);
            if((da<0)==(db<0)||da==db)continue;
            const auto crossing=a+(b-a)*(-da/(db-da));const double d2=vsg::dot(crossing,crossing);
            if(d2<=radius*radius)candidates.emplace_back(d2,bond);
        }
        std::sort(candidates.begin(),candidates.end());
        const double energy=event->at("energy_j").get<double>();
        for(const auto& candidate:candidates) {
            if(double(result.brokenBonds+1)*cost>energy*(1+1e-12))break;
            original.brokenBonds.insert(candidate.second);++result.brokenBonds;
        }
        result.spentEnergy=double(result.brokenBonds)*cost;
        if(!result.brokenBonds)return result;
    }
    std::vector<int> labels(original.volume->cellCount(),-1);std::vector<std::vector<uint32_t>> pieces;
    for(uint32_t i=0;i<original.volume->cellCount();++i)if(grid.solid(i)&&labels[i]<0) {
        const int label=int(pieces.size());pieces.push_back({i});labels[i]=label;
        for(size_t head=0;head<pieces.back().size();++head) {
            const auto current=pieces.back()[head];
            for(int axis=0;axis<3;++axis)for(int sign:{-1,1}) {
                const int next=grid.neighbour(current,axis,sign);if(next<0||!grid.solid(uint32_t(next))||labels[next]>=0)continue;
                const uint64_t bond=uint64_t(sign>0?current:uint32_t(next))*3+axis;
                if(original.brokenBonds.count(bond))continue;
                labels[next]=label;pieces.back().push_back(uint32_t(next));
            }
        }
    }
    if(pieces.empty())return result;
    if(scene.objects.size()+pieces.size()-1>256)throw std::runtime_error("fracture exceeds the 256 body budget; no fragments were discarded");
    auto anchored=[&](const auto& cells){return original.body.anchorAll||std::any_of(cells.begin(),cells.end(),[&](auto i){return original.anchors.count(i);});};
    std::stable_sort(pieces.begin(),pieces.end(),[&](const auto& a,const auto& b){if(anchored(a)!=anchored(b))return anchored(a);return a.size()>b.size();});
    const auto parent=original;const auto placement=original.pose(scene.tick,scene.fixedStep);
    for(size_t p=0;p<pieces.size();++p) {
        auto piece=parent;piece.basePose=placement;piece.motionOriginTick=scene.tick;
        const bool fixed=parent.body.anchorAll||std::any_of(pieces[p].begin(),pieces[p].end(),[&](auto i){return parent.anchors.count(i);});
        piece.body.motion=fixed?"static":"dynamic";
        if(piece.earth&&!fixed)piece.terrainMode="voxel";
        if(pieces.size()>1) {
            std::array<int,3> low=grid.n,high{0,0,0};
            for(const auto i:pieces[p]){const auto at=grid.xyz(i);for(int a=0;a<3;++a){low[a]=std::min(low[a],at[a]);high[a]=std::max(high[a],at[a]);}}
            const std::array<int,3> n{high[0]-low[0]+1,high[1]-low[1]+1,high[2]-low[2]+1};
            auto remap=[&](uint32_t i){const auto xyz=grid.xyz(i);return (uint32_t(xyz[2]-low[2])*n[1]+xyz[1]-low[1])*n[0]+xyz[0]-low[0];};
            std::vector<bool> occupied(size_t(n[0])*n[1]*n[2],false);piece.anchors.clear();piece.brokenBonds.clear();
            for(const auto i:pieces[p]){occupied[remap(i)]=true;if(parent.anchors.count(i))piece.anchors.insert(remap(i));}
            for(const auto key:parent.brokenBonds)if(labels[key/3]==labels[pieces[p][0]]) {
                const int next=grid.neighbour(uint32_t(key/3),int(key%3),1);
                if(next>=0&&labels[next]==labels[pieces[p][0]])piece.brokenBonds.insert(uint64_t(remap(uint32_t(key/3)))*3+key%3);
            }
            piece.volume=VoxelTerrain::fromOccupancy(grid.volume.gridOrigin()+vsg::dvec3(low[0],low[1],low[2])*grid.volume.resolution_m,
                grid.volume.resolution_m,n[0],n[1],n[2],occupied,false);
            if(parent.volume->appearance())std::const_pointer_cast<VoxelTerrain>(piece.volume)->setAppearance(parent.volume->appearance());
            piece.collision.reset();
        }
        if(piece.body.ballastMassKg>0&&!containsBallast(piece))piece.body.ballastMassKg=0;
        if(p) {
            do{if(scene.nextFragment>=1'000'000'000'000ULL)throw std::overflow_error("fragment sequence exhausted");piece.id="fragment-"+std::to_string(scene.nextFragment++);}while(scene.objects.count(piece.id));
            piece.parentId=parent.id;result.fragments.push_back(piece.id);scene.objects.emplace(piece.id,std::move(piece));
        } else original=std::move(piece);
    }
    return result;
}
}
