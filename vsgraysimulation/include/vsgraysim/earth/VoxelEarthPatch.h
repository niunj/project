#pragma once
#include "EarthSurface.h"
#include <array>
#include <vsg/maths/box.h>

namespace vsgraysim::earth {
class VoxelTerrain;
struct VoxelSceneObject;
struct VoxelSceneSnapshot;
/// A pinned georeferenced height field and imagery atlas for a bounded physics
/// working set. The occupancy is the durable damage state; both display modes
/// are derived from that state, never by reopening an undamaged source raster.
class VoxelEarthPatch {
public:
    struct Sculpt {std::shared_ptr<const VoxelEarthPatch> patch;std::shared_ptr<VoxelTerrain> volume;};
    Sculpt sculpt(std::shared_ptr<const VoxelTerrain>,const nlohmann::json& brush)const;
    static std::shared_ptr<const VoxelEarthPatch> load(const nlohmann::json& source,
        const simcore::geo::GeoAnchor&,const std::string& documentPath);
    static std::shared_ptr<const VoxelEarthPatch> decode(const nlohmann::json&);
    nlohmann::json encode() const;
    std::shared_ptr<VoxelTerrain> initialVolume() const;
    nlohmann::json inferredThermalMaterials(const VoxelTerrain&, double baseK) const;
    EarthSurfaceSnapshotPtr makeSurface(std::shared_ptr<const VoxelTerrain>,const std::string& mode,bool rigidFragment=false) const;
    const nlohmann::json& provenance() const {return provenance_;}
    const nlohmann::json& materials() const {return materials_;}
    size_t residentBytes()const{return (heights_.size()+seaReference_.size())*sizeof(double)+rgba_.size();}
    std::array<double, 2> pageSplitXZ() const {return {x_+(nx_/2)*cell_,z_+(nz_/2)*cell_};}
    double cellSize() const {return cell_;}
    vsg::dbox bounds()const{return {{x_,bottom_,z_},{x_+nx_*cell_,bottom_+ny_*cell_,z_+nz_*cell_}};}
    bool strictCoverage()const{return provenance_.at("earth").value("strict",true);}
private:
    double x_=0,z_=0,bottom_=0,cell_=1;
    int nx_=0,nz_=0,ny_=0,sub_=1;
    bool smoothVoxel_=true;
    uint32_t width_=0,height_=0;
    std::vector<double> seaReference_;double seaMaskEpsilon_=0; // optional DSM zero-water mask, never bathymetry
    std::vector<double> heights_; // north-first, (nx*sub+1)*(nz*sub+1)
    std::vector<uint8_t> rgba_;
    nlohmann::json provenance_,materials_;
    void validate() const;
};
/// Refresh the canonical drawable/collider after damage or a mode change.
/// Detached terrain uses voxel rigid bodies; HD surfaces are static colliders.
void refreshVoxelEarthObject(VoxelSceneObject&);
/// Conservative full-frustum coverage at the explicit volume bottom. A strict
/// Earth measurement must fit one resident working set; unknown ground beyond
/// that window must not be returned as a fabricated sky/background product.
void validateVoxelEarthView(const VoxelSceneSnapshot&,const vsg::dvec3& position,
    const vsg::dvec3& forward,const vsg::dvec3& up,double horizontalFovDeg,double aspect,double range);
}
