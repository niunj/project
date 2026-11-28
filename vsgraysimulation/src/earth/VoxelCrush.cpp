#include "VoxelDynamics.h"
#include "VoxelTerrain.h"
#include "VoxelSensorMaterials.h"
#include <vsg/maths/transform.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim::earth {
namespace {
using Cell=std::array<int,3>;
using D=vsg::dvec3;
struct Grid {
    D origin;
    double cell;
    Cell size;
    size_t count()const{return size_t(size[0])*size[1]*size[2];}
    bool contains(const Cell& p)const{
        for(int a=0;a<3;++a)if(p[a]<0||p[a]>=size[a])return false;
        return true;
    }
    uint32_t id(const Cell& p)const{return (uint32_t(p[2])*size[1]+p[1])*size[0]+p[0];}
    Cell at(uint32_t i)const{return {int(i%size[0]),int(i/size[0]%size[1]),int(i/(size_t(size[0])*size[1]))};}
    D centre(const Cell& p)const{return origin+D(p[0]+.5,p[1]+.5,p[2]+.5)*cell;}
};
double smoothFalloff(double value) {
    const double t=std::clamp(value,0.,1.);
    return 1.-t*t*(3.-2.*t);
}

std::shared_ptr<const VoxelSurfaceAppearance> crushAppearance(const VoxelSurfaceAppearance& source,
    const Grid& from,const Grid& to,const std::vector<bool>& original,const std::vector<bool>& occupied,
    const std::vector<uint32_t>& destinations,size_t patchVertexBudget) {
    constexpr int tile=VoxelSurfaceAppearance::tileSize;
    const int clippedColumns=source.detailed()&&source.flatFaceCells.empty()
        ?int(std::ceil(std::sqrt(source.patches.size()+1.))):0;
    const int originalTile=clippedColumns?std::clamp(int(source.width)/clippedColumns,1,tile):tile;
    const uint32_t columns=std::max(1u,source.width/unsigned(originalTile));
    const bool retainPatches=source.detailed();
    const auto slotFromUv=[&](const vsg::vec2& uv){
        const auto px=std::min(source.width-1,uint32_t(uv.x*source.width));
        const auto py=std::min(source.height-1,uint32_t(uv.y*source.height));
        return (py/originalTile)*columns+std::min(columns-1,px/originalTile);
    };
    std::vector<bool> flat(to.count(),true);
    std::vector<int64_t> patchDestination;
    if(retainPatches)patchDestination.assign(source.originalOccupancy.size(),-1);
    std::map<uint64_t,uint32_t> faces;
    std::map<uint32_t,uint32_t> tiles{{0,0}};
    for(uint32_t i=0;i<destinations.size();++i){
        const auto dest=destinations[i];
        if(dest==std::numeric_limits<uint32_t>::max())continue;
        const auto old=source.cellAt(from.centre(from.at(i)));
        if(old<0)continue;
        const auto start=std::lower_bound(source.patches.begin(),source.patches.end(),old,
            [](const auto& p,int64_t o){return p.cell<o;});
        const auto q=to.at(dest);
        if(retainPatches&&vsg::length(to.centre(q)-from.centre(from.at(i)))<1.e-7*to.cell&&
            (source.flatFaceCells.empty()||!source.flatFaceCells[size_t(old)])){
            bool keep=true;
            for(int a=0;a<3;++a)for(int sign:{-1,1}){
                auto oldSide=from.at(i),newSide=q;
                oldSide[a]+=sign;newSide[a]+=sign;
                const bool was=from.contains(oldSide)&&original[from.id(oldSide)];
                const bool now=to.contains(newSide)&&occupied[to.id(newSide)];
                if(was!=now)keep=false;
            }
            if(keep){flat[dest]=false;patchDestination[size_t(old)]=dest;}
        }
        for(int face=0;face<6;++face){
            auto neighbour=q;neighbour[face/2]+=face%2?1:-1;
            if(to.contains(neighbour)&&occupied[to.id(neighbour)])continue;
            uint32_t slot=0;
            if(const auto match=source.faceTiles.find(uint64_t(old)*6+face);match!=source.faceTiles.end())
                slot=match->second;
            else if(start!=source.patches.end()&&start->cell==old){
                float best=-1.1f;
                for(auto patch=start;patch!=source.patches.end()&&patch->cell==old;++patch){
                    const auto& vertex=patch->vertices.front();
                    const float alignment=vertex.normal[face/2]*(face%2?1.f:-1.f);
                    if(alignment<=best)continue;
                    best=alignment;
                    slot=slotFromUv(vertex.uv);
                }
            }
            if(slot){
                tiles.try_emplace(slot,uint32_t(tiles.size()));
                faces.emplace(uint64_t(dest)*6+face,slot);
            }
        }
    }
    auto out=std::make_shared<VoxelSurfaceAppearance>();
    out->origin=to.origin;out->dimensions=to.size;out->cellSize=to.cell;
    if(retainPatches){
        size_t vertices=0;
        for(const auto& patch:source.patches)if(patchDestination[patch.cell]>=0){
            vertices+=patch.vertices.size();
            if(vertices>patchVertexBudget){out->patches.clear();break;}
            auto retained=patch;retained.cell=uint32_t(patchDestination[patch.cell]);
            out->patches.push_back(std::move(retained));
        }
        if(!out->patches.empty()){
            for(const auto& patch:out->patches){
                const auto slot=slotFromUv(patch.vertices.front().uv);
                tiles.try_emplace(slot,uint32_t(tiles.size()));
            }
            out->originalOccupancy=occupied;out->flatFaceCells=std::move(flat);
        }
    }
    const auto width=uint32_t(std::ceil(std::sqrt(double(tiles.size()))));
    out->width=width*tile;out->height=uint32_t((tiles.size()+width-1)/width)*tile;
    const size_t bytes=size_t(out->width)*out->height*4;
    if(bytes>64ULL*1024*1024)return {};
    const auto copy=[&](const auto& fromPixels,auto& toPixels){
        if(fromPixels.empty())return;
        toPixels.resize(bytes);
        for(const auto& [old,next]:tiles)for(int y=0;y<tile;++y)for(int x=0;x<tile;++x){
            const auto row=std::min(source.height-1,old/columns*originalTile+y*originalTile/tile);
            const auto col=std::min(source.width-1,old%columns*originalTile+x*originalTile/tile);
            const auto src=4*(size_t(row)*source.width+col);
            const auto dst=4*(size_t(next/width*tile+y)*out->width+next%width*tile+x);
            std::copy_n(fromPixels.begin()+src,4,toPixels.begin()+dst);
        }
    };
    copy(source.rgba,out->rgba);copy(source.normal_rgba,out->normal_rgba);
    copy(source.mr_rgba,out->mr_rgba);copy(source.emissive_rgba,out->emissive_rgba);
    for(auto& patch:out->patches){
        const auto old=slotFromUv(patch.vertices.front().uv);
        const auto next=tiles.at(old);
        for(auto& v:patch.vertices){
            const double x=double(v.uv.x)*source.width-(old%columns)*originalTile;
            const double y=double(v.uv.y)*source.height-(old/columns)*originalTile;
            v.uv={float((next%width*tile+x*tile/originalTile)/out->width),
                  float((next/width*tile+y*tile/originalTile)/out->height)};
        }
    }
    for(auto& [face,slot]:faces)slot=tiles.at(slot);
    out->faceTiles=std::move(faces);
    return out;
}
}

bool crushVoxelObject(VoxelSceneObject& object,uint64_t tick,double step,const VoxelImpact& impact,
                     size_t maximumCells,size_t maximumVertices) {
    const auto& volume=*object.volume;
    const double cell=volume.resolution_m,radius=object.body.crushRadius;
    const double force=std::clamp((impact.closingSpeed/object.body.crushYieldSpeed-1.)*.85,0.,1.);
    const double strain=force*object.body.crushMaxStrain;
    if(radius<cell||strain<.04)return false;
    const auto pose=object.pose(tick,step),inverse=vsg::inverse(pose);
    const auto point=inverse*impact.point;
    const auto q=inverse*vsg::dvec4(impact.normal,0.);
    const D normal=vsg::normalize(D{q.x,q.y,q.z});
    int axis=0;
    for(int a=1;a<3;++a)if(std::abs(normal[a])>std::abs(normal[axis]))axis=a;
    const int sign=normal[axis]>=0.?1:-1,u=(axis+1)%3,v=(axis+2)%3;
    const Grid from{volume.gridOrigin(),cell,volume.dimensions()};
    Grid to=from;
    int pad=std::clamp(int(std::min(8.,std::ceil(radius*.12/cell))),1,8);
    to.size[u]+=2*pad;to.size[v]+=2*pad;
    if(to.count()>std::min(maximumCells,size_t(4'000'000))){to=from;pad=0;}
    to.origin[u]-=pad*cell;to.origin[v]-=pad*cell;
    constexpr auto empty=std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> destinations(from.count(),empty);
    std::vector<bool> occupied(to.count(),false);
    std::vector<uint32_t> movable;
    for(uint32_t i=0;i<from.count();++i)if(volume.occupancy()[i]){
        auto x=from.at(i);const auto d=from.centre(x)-point;
        x[u]+=pad;x[v]+=pad;
        destinations[i]=to.id(x);occupied[destinations[i]]=true;
        if(std::hypot(d[u],d[v])<radius&&d[axis]*sign>.7*cell&&d[axis]*sign<radius*1.4)
            movable.push_back(i);
    }
    std::sort(movable.begin(),movable.end(),[&](uint32_t a,uint32_t b){
        const int x=from.at(a)[axis],y=from.at(b)[axis];
        return x==y?a<b:(x-y)*sign<0;
    });
    const double support=cell*.5*(std::abs(normal.x)+std::abs(normal.y)+std::abs(normal.z));
    uint32_t moved=0;
    for(uint32_t i:movable){
        const auto start=destinations[i];
        const auto old=to.at(start);
        const D d=to.centre(old)-point;
        const double depth=std::max(0.,d[axis]*sign-.5*cell);
        const double across=std::hypot(d[u],d[v]);
        const double strength=strain*smoothFalloff(across/radius)*smoothFalloff((depth/radius-.55)/.85);
        const int shift=int(std::min(double(to.size[axis]),std::round(depth*strength/cell)));
        if(!shift)continue;
        auto wanted=old;wanted[axis]-=sign*shift;
        const auto available=[&](const Cell& candidate){
            if(!to.contains(candidate)||occupied[to.id(candidate)])return false;
            if(vsg::dot(to.centre(candidate)-point,normal)<support-.20*cell)return false;
            for(int a=0;a<3;++a)for(int side:{-1,1}){
                auto adjacent=candidate;adjacent[a]+=side;
                if(to.contains(adjacent)&&occupied[to.id(adjacent)]&&to.id(adjacent)!=start)return true;
            }
            return false;
        };
        const int outwardU=d[u]>=0.?1:-1,outwardV=d[v]>=0.?1:-1;
        bool placed=false;
        for(int back=0;back<std::min(shift,3)&&!placed;++back){
            auto centre=wanted;centre[axis]+=sign*back;
            for(int r=0;r<=5&&!placed;++r){
                for(const auto& delta:std::array<std::array<int,2>,8>{{
                    {outwardU*r,0},{0,outwardV*r},{outwardU*r,outwardV*r},{-outwardU*r,0},
                    {0,-outwardV*r},{-outwardU*r,outwardV*r},{outwardU*r,-outwardV*r},
                    {-outwardU*r,-outwardV*r}}}){
                    auto x=centre;x[u]+=delta[0];x[v]+=delta[1];
                    if(!available(x))continue;
                    destinations[i]=to.id(x);occupied[destinations[i]]=true;occupied[start]=false;
                    ++moved;placed=true;break;
                }
            }
        }
    }
    if(!moved)return false;
    size_t faces=0;
    for(uint32_t i=0;i<occupied.size();++i)if(occupied[i])for(int a=0;a<3;++a)for(int side:{-1,1}){
        auto q=to.at(i);q[a]+=side;
        if(!to.contains(q)||!occupied[to.id(q)])++faces;
        if(faces>std::min(maximumVertices,size_t(2'000'000))/4)return false;
    }
    auto deformed=VoxelTerrain::fromOccupancy(to.origin,cell,to.size[0],to.size[1],to.size[2],occupied);
    if(const auto atlas=volume.appearance()){
        const auto remapped=crushAppearance(*atlas,from,to,volume.occupancy(),occupied,destinations,
                                            std::min(maximumVertices,size_t(4'000'000))-faces*4);
        if(!remapped)return false;
        deformed->setAppearance(remapped);
    }
    if(object.sensorMaterials)object.sensorMaterials=object.sensorMaterials->remapped(volume,*deformed,destinations);
    const auto oldMassCentre=object.localCentreOfMass();
    if(object.body.ballastMassKg>0.){
        const D offset=(object.body.ballastPosition-from.origin)/cell;
        const Cell at{int(std::floor(offset.x)),int(std::floor(offset.y)),int(std::floor(offset.z))};
        object.body.ballastPosition+=to.centre(to.at(destinations[from.id(at)]))-from.centre(at);
    }
    std::set<uint64_t> broken;
    for(const auto key:object.brokenBonds){
        const int a=int(key%3);
        auto adjacent=from.at(uint32_t(key/3));++adjacent[a];
        if(!from.contains(adjacent))continue;
        const auto first=destinations[key/3],second=destinations[from.id(adjacent)];
        if(first==empty||second==empty)continue;
        auto q=to.at(first);++q[a];
        if(to.contains(q)&&to.id(q)==second)broken.insert(uint64_t(first)*3+a);
    }
    object.brokenBonds=std::move(broken);
    object.volume=std::move(deformed);object.geometry.reset();object.collision.reset();
    const auto offset=pose*vsg::dvec4(oldMassCentre-object.localCentreOfMass(),0.);
    object.velocity+=vsg::cross(object.angularVelocity,D{offset.x,offset.y,offset.z});
    object.crushedAtTick=tick;
    return true;
}
}
