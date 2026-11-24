#include "TiledVoxelTerrainPhysics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vsgraysim::earth {

TiledVoxelTerrainPhysics::TiledVoxelTerrainPhysics(
    std::shared_ptr<const TiledVoxelTerrain> terrain)
    : terrain_(terrain?std::make_shared<TiledVoxelTerrain>(*terrain):nullptr) {
    if (!terrain_) throw std::invalid_argument("TiledVoxelTerrainPhysics requires a terrain");
    if (terrain_->backend() != EarthBackend::Voxel)
        throw std::invalid_argument("TiledVoxelTerrainPhysics requires the voxel backend");
}

bool TiledVoxelTerrainPhysics::focus(double lat, double lon, double halfSpanMetres,
                                      std::optional<double> bottomMetres) {
    if (!std::isfinite(lat) || std::abs(lat) > 90 || !std::isfinite(lon) ||
        !std::isfinite(halfSpanMetres) || halfSpanMetres <= 0 || halfSpanMetres > 20000)
        return false;
    const double bottom = bottomMetres.value_or(
        terrain_->min_height - 0.25 * (terrain_->max_height - terrain_->min_height + 1.));
    auto window = terrain_->extractDamageWindow(lat, lon, halfSpanMetres, bottom);
    if (!window) return false;
    window_ = std::move(*window);
    // Rebuild the single-grid snapshot from the window occupancy.
    snapshot_ = VoxelTerrain::fromOccupancy(
        window_.min, window_.cell, window_.nx, window_.ny, window_.nz,
        window_.occupied, /*strict=*/false);
    grid_.emplace(snapshot_);
    focused_ = true;
    return true;
}

std::vector<std::vector<std::array<double,2>>>
TiledVoxelTerrainPhysics::currentRuns() const {
    std::vector<std::vector<std::array<double,2>>> runs(size_t(window_.nx) * window_.nz);
    if (!grid_) return runs;
    const double cell = window_.cell;
    for (int j = 0; j < window_.nz; ++j)
        for (int i = 0; i < window_.nx; ++i) {
            auto& column = runs[size_t(j) * window_.nx + i];
            bool unchanged=true;
            for(int y=0;y<window_.ny;++y)
                if(grid_->occupiedLocal(window_.min.x+(i+.5)*cell,window_.min.y+(y+.5)*cell,
                                        window_.min.z+(j+.5)*cell).value_or(false)!=
                   window_.occupied[(size_t(j)*window_.ny+y)*window_.nx+i]) {unchanged=false;break;}
            if(unchanged){column=window_.originalRuns[size_t(j)*window_.nx+i];continue;}
            // Walk the column bottom-up; contiguous occupied cells merge.
            int start = -1;
            for (int y = 0; y < window_.ny; ++y) {
                const auto occupied = grid_->occupiedLocal(
                    window_.min.x + (i + .5) * cell,
                    window_.min.y + (y + .5) * cell,
                    window_.min.z + (j + .5) * cell);
                if (occupied.value_or(false)) {
                    if (start < 0) start = y;
                } else if (start >= 0) {
                    column.push_back({window_.min.y + start * cell,
                                      window_.min.y + y * cell});
                    start = -1;
                }
            }
            if (start >= 0)
                column.push_back({window_.min.y + start * cell,
                                  window_.min.y + double(window_.ny) * cell});
            const auto index=size_t(j)*window_.nx+i;
            const double offset=window_.heightOffsets[index],scale=window_.heightScales[index];
            for(auto& run:column)for(auto& height:run)height=offset+scale*height;
            const double bottom=offset+scale*window_.min.y;
            const double top=offset+scale*(window_.min.y+window_.ny*cell);
            // The finite damage window cannot remove geology outside its
            // vertical coverage, even when the lowest sampled cell is empty.
            for(const auto& run:window_.originalRuns[index]) {
                if(run[0]<bottom)column.push_back({run[0],std::min(bottom,run[1])});
                if(run[1]>top)column.push_back({std::max(top,run[0]),run[1]});
            }
        }
    return runs;
}

std::size_t TiledVoxelTerrainPhysics::commit() {
    if (!focused_ || !grid_) return 0;
    auto next=std::make_shared<TiledVoxelTerrain>(*terrain_);
    const auto changed=next->applyDamage(window_,currentRuns());
    if(changed)terrain_=std::move(next);
    return changed;
}

} // namespace vsgraysim::earth
