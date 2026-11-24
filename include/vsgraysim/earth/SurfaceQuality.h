#pragma once
#include <vsgraysim/earth/EarthSurface.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsgraysim::earth {
/// Read-only diagnostics for the pinned mesh and its matching image. These are
/// source/display statistics, never a replacement for terrain or sensor data.
struct SurfaceQuality {
    uint64_t revision=0;
    std::size_t validTriangles=0, upwardTriangles=0, imagePixels=0;
    double heightMin=0, heightMax=0, slopeMaxDegrees=0;
    double edgeMinMetres=0, edgeMeanMetres=0, edgeMaxMetres=0;
    double demSpacingMetres=0, imagerySpacingMetres=0;
    std::array<unsigned,3> rgbMin{0,0,0},rgbMax{0,0,0};
    double imageLumaStdDev=0;
    bool imageryValid=false, sourceCoarse=false, provenanceAssumed=false;
    nlohmann::json json() const {
        auto finite=[](double value)->nlohmann::json{return std::isfinite(value)?nlohmann::json(value):nlohmann::json(nullptr);};
        return {{"surface_revision",revision},{"height_reference","local_tangent_up_metres"},{"valid_triangles",validTriangles},{"upward_triangles",upwardTriangles},
            {"top_height_min_m",upwardTriangles?finite(heightMin):nlohmann::json(nullptr)},
            {"top_height_max_m",upwardTriangles?finite(heightMax):nlohmann::json(nullptr)},
            {"top_height_range_m",upwardTriangles?finite(heightMax-heightMin):nlohmann::json(nullptr)},
            {"top_slope_max_deg",upwardTriangles?finite(slopeMaxDegrees):nlohmann::json(nullptr)},
            {"triangle_edge_min_m",validTriangles?finite(edgeMinMetres):nlohmann::json(nullptr)},
            {"triangle_edge_mean_m",validTriangles?finite(edgeMeanMetres):nlohmann::json(nullptr)},
            {"triangle_edge_max_m",validTriangles?finite(edgeMaxMetres):nlohmann::json(nullptr)},
            {"dem_spacing_m",finite(demSpacingMetres)},{"imagery_spacing_m",finite(imagerySpacingMetres)},
            {"imagery_valid",imageryValid},{"imagery_pixels",imagePixels},{"imagery_rgb_min",rgbMin},{"imagery_rgb_max",rgbMax},
            {"imagery_srgb_luma_stddev",finite(imageLumaStdDev)},{"source_coarse",sourceCoarse},{"provenance_assumed",provenanceAssumed}};
    }
};
inline SurfaceQuality surfaceQuality(const EarthSurfaceSnapshot& surface) {
    SurfaceQuality q;q.revision=surface.revision;q.demSpacingMetres=surface.dem_spacing_m;q.imagerySpacingMetres=surface.imagery_spacing_m;q.provenanceAssumed=surface.provenance_assumed;
    q.heightMin=q.edgeMinMetres=std::numeric_limits<double>::infinity();q.heightMax=-q.heightMin;
    const auto& vertices=surface.vertices();const auto& indices=surface.indices();
    const auto finite=[](const vsg::dvec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    for(std::size_t i=0;i+2<indices.size();i+=3) {
        const auto ia=indices[i],ib=indices[i+1],ic=indices[i+2];if(ia>=vertices.size()||ib>=vertices.size()||ic>=vertices.size())continue;
        const auto& a=vertices[ia];const auto& b=vertices[ib];const auto& c=vertices[ic];if(!finite(a)||!finite(b)||!finite(c))continue;
        const auto area=vsg::cross(b-a,c-a);const double norm=vsg::length(area);if(!std::isfinite(norm)||norm<1e-12)continue;
        ++q.validTriangles;
        for(const double length:{vsg::length(b-a),vsg::length(c-b),vsg::length(a-c)}) {q.edgeMinMetres=std::min(q.edgeMinMetres,length);q.edgeMaxMetres=std::max(q.edgeMaxMetres,length);q.edgeMeanMetres+=length;}
        // Upward-facing terrain excludes stored geology bottoms and vertical
        // voxel walls from the reported surface relief and slope.
        if(area.y>1e-8*norm) {
            ++q.upwardTriangles;q.heightMin=std::min({q.heightMin,a.y,b.y,c.y});q.heightMax=std::max({q.heightMax,a.y,b.y,c.y});
            q.slopeMaxDegrees=std::max(q.slopeMaxDegrees,std::acos(std::clamp(area.y/norm,0.,1.))*180./3.14159265358979323846);
        }
    }
    if(q.validTriangles)q.edgeMeanMetres/=3.*q.validTriangles;else q.edgeMinMetres=0;
    if(!q.upwardTriangles)q.heightMin=q.heightMax=0;
    const auto pixels=uint64_t(surface.imagery_width)*surface.imagery_height;
    q.imageryValid=pixels>0&&pixels<=std::numeric_limits<std::size_t>::max()/4&&surface.imagery_rgba.size()==pixels*4;
    if(q.imageryValid) {
        q.rgbMin={255,255,255};double mean=0,sum=0;
        for(std::size_t i=0;i<surface.imagery_rgba.size();i+=4) {
            if(surface.imagery_rgba[i+3]==0)continue;
            for(int c=0;c<3;++c){q.rgbMin[c]=std::min(q.rgbMin[c],unsigned(surface.imagery_rgba[i+c]));q.rgbMax[c]=std::max(q.rgbMax[c],unsigned(surface.imagery_rgba[i+c]));}
            const double luma=(.2126*surface.imagery_rgba[i]+.7152*surface.imagery_rgba[i+1]+.0722*surface.imagery_rgba[i+2])/255.;
            const double delta=luma-mean;mean+=delta/double(++q.imagePixels);sum+=delta*(luma-mean);
        }
        if(q.imagePixels)q.imageLumaStdDev=std::sqrt(std::max(0.,sum/q.imagePixels));else{q.imageryValid=false;q.rgbMin={0,0,0};}
    }
    // A fine output mesh does not imply fine source data. The public resolution
    // can already include the native DEM spacing, so also compare its edge size.
    q.sourceCoarse=(surface.resolution_m>0&&q.demSpacingMetres>surface.resolution_m*1.01)||
        (q.validTriangles&&q.demSpacingMetres>q.edgeMinMetres*1.01)||
        (q.validTriangles&&q.imagerySpacingMetres>q.edgeMinMetres*1.01);
    return q;
}
} // namespace vsgraysim::earth
