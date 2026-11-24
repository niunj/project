#pragma once
#include "EarthSurface.h"

namespace vsgraysim::earth {
class EarthDataService {
public:
    /// earth:{map,coverage:{min_x,max_x,min_z,max_z},grid_size,strict,
    /// vertical_datum}. Anchor always comes from scenario.geo_anchor.
    /// No earth/terrain_ref returns nullptr. Explicit but unusable data throws.
    static EarthSurfaceSnapshotPtr loadScenario(const nlohmann::json& scenario,
                                                const std::string& scenario_path);
    static EarthSurfaceSnapshotPtr loadMap(const std::string& map_path,
        const simcore::geo::GeoAnchor& anchor,const nlohmann::json& config=nlohmann::json::object());
    // Global-geodetic XYZ key (two roots). Latitude rows run north to south.
    static void bakeVolume(const std::string& input,const std::string& output,double latitude,double longitude,
                           int minLevel,int maxLevel,int radius,double bottomMetres,bool globalRoots=false);
    static std::array<int,3> tileKey(double lat,double lon,int level);
    static EarthSurfaceSnapshotPtr selectTiles(const EarthSurfaceSnapshotPtr& current,
        double latitude,double longitude,int level);
    /// Load a camera working set in its own tangent frame, from the published dataset.
    static EarthSurfaceSnapshotPtr observerTiles(const EarthSurfaceSnapshotPtr& published,
        double latitude,double longitude,int level);
    /// Returns an absolute reference. terrain_ref may also be .ter/.vsgterrain.
    static std::string resolveMapPath(const nlohmann::json& scenario,const std::string& scenario_path);
    static std::string resolveUri(const std::string& reference,const std::string& document_path);
};
} // namespace vsgraysim::earth
