#pragma once
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>
namespace vsgraysim::earth {
// Immutable source-surface atlas, keyed in the ORIGINAL voxel chart. Cropped
// fracture volumes keep this chart so a wing cannot inherit fuselage paint.
struct VoxelSurfaceAppearance {
    static constexpr int samples=8,gutter=1,tileSize=samples+2*gutter;
    vsg::dvec3 origin;
    std::array<int,3> dimensions{};
    double cellSize=1;
    uint32_t width=0,height=0;
    std::vector<uint8_t> rgba;
    std::map<uint64_t,uint32_t> faceTiles;
    struct Corner {vsg::dvec3 position;vsg::vec3 normal;};
    std::map<uint64_t,Corner> corners;
    std::vector<uint8_t> normal_rgba,mr_rgba,emissive_rgba;
    // Exact source triangles clipped to their owning occupied cell. Immutable
    // source-chart ownership survives cropped fracture volumes/checkpoints.
    struct SurfaceVertex {vsg::dvec3 position;vsg::vec3 normal;vsg::vec2 uv;};
    struct SurfacePatch {uint32_t cell;std::vector<SurfaceVertex> vertices;};
    std::vector<SurfacePatch> patches;
    std::vector<bool> originalOccupancy;
    bool detailed()const{return !patches.empty();}
    bool reconstructed()const{return detailed()||!corners.empty();}
    int64_t cellAt(const vsg::dvec3& p)const {
        int c[3];for(int a=0;a<3;++a){c[a]=int(std::floor((p[a]-origin[a])/cellSize));if(c[a]<0||c[a]>=dimensions[a])return -1;}
        return (int64_t(c[2])*dimensions[1]+c[1])*dimensions[0]+c[0];
    }
    uint64_t cornerKey(int x,int y,int z)const{return (uint64_t(z)*(dimensions[1]+1)+y)*(dimensions[0]+1)+x;}
    const Corner* corner(const vsg::dvec3& p)const {
        int at[3];for(int a=0;a<3;++a)at[a]=int(std::llround((p[a]-origin[a])/cellSize));
        if(at[0]<0||at[1]<0||at[2]<0||at[0]>dimensions[0]||at[1]>dimensions[1]||at[2]>dimensions[2])return nullptr;
        const auto found=corners.find(cornerKey(at[0],at[1],at[2]));return found==corners.end()?nullptr:&found->second;
    }
    uint64_t key(int x,int y,int z,int face)const {
        return ((uint64_t(z)*dimensions[1]+y)*dimensions[0]+x)*6+face;
    }
    std::array<vsg::vec2,4> uv(const vsg::dvec3& centre,int face)const {
        int xyz[3];for(int a=0;a<3;++a)xyz[a]=int(std::floor((centre[a]-origin[a])/cellSize));
        if(xyz[0]<0||xyz[1]<0||xyz[2]<0||xyz[0]>=dimensions[0]||xyz[1]>=dimensions[1]||xyz[2]>=dimensions[2])
            return {vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height)};
        const auto found=faceTiles.find(key(xyz[0],xyz[1],xyz[2],face));
        if(found==faceTiles.end())return {vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height),vsg::vec2(.5f/width,.5f/height)};
        const uint32_t columns=width/tileSize,tile=found->second;
        const float x=float(tile%columns*tileSize+gutter),y=float(tile/columns*tileSize+gutter);
        return {vsg::vec2(x/width,y/height),vsg::vec2((x+samples)/width,y/height),
                vsg::vec2((x+samples)/width,(y+samples)/height),vsg::vec2(x/width,(y+samples)/height)};
    }
};
}
