#include "vsgraysim/products/TerrainProduct.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <zlib.h>
#include <zip.h>

namespace vsgraysim {

// ---- TileKey helpers ----

TileKey TerrainProduct::keyAt(double wx, double wz) const {
    // Convert world coords to tile coordinates
    double tile_world_size = resolution * tile_size;
    int64_t tx = static_cast<int64_t>(std::floor((wx - world_bounds.min.x) / tile_world_size));
    int64_t ty = static_cast<int64_t>(std::floor((wz - world_bounds.min.z) / tile_world_size));

    TileKey key;
    key.lod = 0;
    key.tx = static_cast<uint32_t>(tx);
    key.ty = static_cast<uint32_t>(ty);

    // Morton encode (x, y) -> morton
    auto morton2d = [](uint32_t x, uint32_t y) {
        uint64_t m = 0;
        for (int i = 0; i < 32; ++i) {
            m |= (uint64_t)(x & (1u << i)) << i;
            m |= (uint64_t)(y & (1u << i)) << (i + 1);
        }
        return m;
    };
    key.morton = (static_cast<uint64_t>(key.lod) << 56) | (morton2d(key.tx, key.ty) << 8);
return key;
}
 
std::optional<TileData> TerrainProduct::getTile(const TileKey& key) const {
    if (!tile_store) return std::nullopt;
    return tile_store->read(key);
}

double TerrainProduct::heightAt(double wx, double wz) const {
    double h;
    if (tryHeightAt(wx, wz, h)) return h;
    return std::numeric_limits<double>::quiet_NaN();
}

bool TerrainProduct::tryHeightAt(double wx, double wz, double& out_height) const {
    if (!contains(wx, wz) || !tile_store) return false;

    TileKey key = keyAt(wx, wz);
    auto tile_opt = tile_store->read(key);
    if (!tile_opt) return false;

    const TileData& tile = *tile_opt;
    double tile_world_size = resolution * tile_size;

    // Local coordinates within tile
    double local_x = wx - (world_bounds.min.x + key.tx * tile_world_size);
    double local_z = wz - (world_bounds.min.z + key.ty * tile_world_size);

    // Normalize to [0, tile_size]
    double fx = local_x / resolution;
    double fz = local_z / resolution;

    int ix = static_cast<int>(std::floor(fx));
    int iz = static_cast<int>(std::floor(fz));
    double u = fx - ix;
    double v = fz - iz;

    // Clamp to tile boundaries
    ix = std::clamp(ix, 0, static_cast<int>(tile_size) - 1);
    iz = std::clamp(iz, 0, static_cast<int>(tile_size) - 1);
    int ix1 = std::min(ix + 1, static_cast<int>(tile_size));
    int iz1 = std::min(iz + 1, static_cast<int>(tile_size));

    size_t stride = tile_size + 1;
    auto idx = [stride](int x, int z) { return size_t(z) * stride + x; };

    double h00 = tile.height[idx(ix, iz)];
    double h10 = tile.height[idx(ix1, iz)];
    double h01 = tile.height[idx(ix, iz1)];
    double h11 = tile.height[idx(ix1, iz1)];

    // Bilinear interpolation
    double h0 = h00 * (1 - u) + h10 * u;
    double h1 = h01 * (1 - u) + h11 * u;
    out_height = h0 * (1 - v) + h1 * v;
    return true;
}

// ---- Factory methods ----

std::shared_ptr<TerrainProduct> TerrainProduct::create(const CRSInfo& crs,
                                                       const vsg::dbox& bounds,
                                                       double resolution,
                                                       uint32_t tile_size) {
    auto product = std::make_shared<TerrainProduct>();
    product->product_id = generateUUID();
    product->crs = crs;
    product->world_bounds = bounds;
    product->resolution = resolution;
    product->tile_size = tile_size;

    // Default biome rules
    product->biome_rules = {
        {"Water", 3, -1e9, 0.1, 0, 90, {0.0f, 0.2f, 0.6f, 1.0f}, "water"},
        {"Sand/Beach", 3, -0.5, 2.0, 0, 10, {0.8f, 0.7f, 0.5f, 1.0f}, "sand"},
        {"Grass", 0, 0.0, 1000.0, 0, 30, {0.2f, 0.6f, 0.2f, 1.0f}, "grass"},
        {"Rock", 1, 500.0, 1e9, 30, 90, {0.5f, 0.5f, 0.5f, 1.0f}, "rock"},
        {"Snow", 2, 3000.0, 1e9, 0, 90, {0.9f, 0.9f, 0.95f, 1.0f}, "snow"}
    };

    return product;
}

std::shared_ptr<TerrainProduct> TerrainProduct::load(const std::string& path) {
    // Implemented in ProjectSerializer
    return nullptr;
}

bool TerrainProduct::save(const std::string& path) const {
    // Implemented in ProjectSerializer
    return false;
}

// ---- Export stubs ----

bool TerrainProduct::exportHeightmap(const std::string&, HeightmapExportFormat) const {
    return false;
}

bool TerrainProduct::exportMesh(const std::string&, MeshExportFormat, int) const {
    return false;
}

bool TerrainProduct::exportVectorFeatures(const std::string&) const {
    return false;
}

// ---- UUID generation ----

std::string TerrainProduct::generateUUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    // Version 4 UUID
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << (a >> 32)
       << "-"
       << std::setw(4) << ((a >> 16) & 0xFFFF)
       << "-"
       << std::setw(4) << (a & 0xFFFF)
       << "-"
       << std::setw(4) << (b >> 48)
       << "-"
       << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
    return ss.str();
}

} // namespace vsgraysim