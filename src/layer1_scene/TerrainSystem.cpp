#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

#include <spdlog/spdlog.h>

#include <vsg/maths/transform.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/utils/Builder.h>

namespace vsgraysim {

namespace {

// ---------------------------------------------------------------------------
// Seeded value-noise / fBm / ridged-multifractal primitives.
// Everything is deterministic given (x, z, seed) so regeneration with the
// same seed reproduces identical terrain.
// ---------------------------------------------------------------------------
inline uint32_t hash2i(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = seed ^ (x * 0x27d4eb2du) ^ (y * 0x165667b1u);
    h ^= h >> 15; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

inline double valueNoise(double x, double z, uint32_t seed)
{
    int ix = static_cast<int>(std::floor(x));
    int iz = static_cast<int>(std::floor(z));
    double fx = x - std::floor(x);
    double fz = z - std::floor(z);
    const double sx = fx * fx * (3.0 - 2.0 * fx);
    const double sz = fz * fz * (3.0 - 2.0 * fz);

    const double n00 = (hash2i(ix,     iz,     seed) & 0xFFFFu) / 65535.0;
    const double n10 = (hash2i(ix + 1, iz,     seed) & 0xFFFFu) / 65535.0;
    const double n01 = (hash2i(ix,     iz + 1, seed) & 0xFFFFu) / 65535.0;
    const double n11 = (hash2i(ix + 1, iz + 1, seed) & 0xFFFFu) / 65535.0;

    return n00 * (1 - sx) * (1 - sz) + n10 * sx * (1 - sz) +
           n01 * (1 - sx) * sz + n11 * sx * sz;
}

/// Fractional Brownian motion: sum of value-noise octaves, normalised [0,1].
inline double fbm(double x, double z, int octaves, double lacunarity,
                  double gain, uint32_t seed)
{
    double amp = 1.0, freq = 1.0, sum = 0.0, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(x * freq, z * freq, seed + 101u * uint32_t(i));
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

/// Ridged multifractal (mountain ranges): sharp ridges, deep valleys.
inline double ridgedFbm(double x, double z, int octaves, double lacunarity,
                        double gain, uint32_t seed)
{
    double amp = 1.0, freq = 1.0, sum = 0.0, norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        double n = valueNoise(x * freq, z * freq, seed + 7u * uint32_t(i));
        double r = 1.0 - std::fabs(2.0 * n - 1.0);   // ridged: [-1,1] -> [0,1]
        r *= r;
        sum += amp * r;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

inline double smoothstep01(double edge0, double edge1, double x)
{
    double t = std::min(1.0, std::max(0.0, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0 - 2.0 * t);
}

inline double clampd(double v, double lo, double hi)
{
    return std::min(hi, std::max(lo, v));
}

constexpr uint32_t kTexSize = 256;

} // anonymous namespace

// ===========================================================================
// Construction / parameters
// ===========================================================================
TerrainSystem::TerrainSystem(uint32_t grid, double extent, double height_scale)
    : n_(grid), extent_(extent), height_scale_(height_scale)
{
    cell_size_ = extent_ / double(n_ - 1);
    heights_.assign(size_t(n_) * n_, 0.0);
    normals_.assign(size_t(n_) * n_, vsg::vec3(0.0f, 1.0f, 0.0f));
    buildNodes();
    generate(TerrainType::Plains);
}

void TerrainSystem::setGrid(uint32_t n)
{
    if (n == n_ || n < 8) return;
    n_ = n;
    cell_size_ = extent_ / double(n_ - 1);
    heights_.assign(size_t(n_) * n_, 0.0);
    normals_.assign(size_t(n_) * n_, vsg::vec3(0.0f, 1.0f, 0.0f));
    buildNodes();
    ++node_gen_;
}

void TerrainSystem::setExtent(double e)
{
    if (e <= 0.0) return;
    extent_ = e;
    cell_size_ = extent_ / double(n_ - 1);
}

void TerrainSystem::setHeightScale(double s)
{
    if (s <= 0.0) return;
    height_scale_ = s;
    if (water_level_ > height_scale_ * 2.0) water_level_ = height_scale_ * 0.1;
}

// ===========================================================================
// Mesh construction
// ===========================================================================
void TerrainSystem::buildNodes()
{
    const size_t nv = size_t(n_) * n_;
    const size_t nc = size_t(n_ - 1) * (n_ - 1);

    auto builder = vsg::Builder::create();
    builder->options = vsg::Options::create();

    // ---- terrain vertex/index arrays (DYNAMIC: CPU-edited every brush) ----
    t_pos_ = vsg::vec3Array::create(nv);
    t_pos_->properties.dataVariance = vsg::DYNAMIC_DATA;
    t_nrm_ = vsg::vec3Array::create(nv);
    t_nrm_->properties.dataVariance = vsg::DYNAMIC_DATA;
    t_uv_  = vsg::vec2Array::create(nv);
    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            (*t_uv_)[size_t(j) * n_ + i] = vsg::vec2(float(i) / (n_ - 1),
                                                     float(j) / (n_ - 1));
        }
    }
    t_idx_ = vsg::uintArray::create(nc * 6);
    {
        uint32_t* idx = reinterpret_cast<uint32_t*>(t_idx_->dataPointer());
        size_t k = 0;
        //  (i,j)      (i+1,j)
        //  (i,j+1)    (i+1,j+1)
        for (uint32_t j = 0; j < n_ - 1; ++j) {
            for (uint32_t i = 0; i < n_ - 1; ++i) {
                const uint32_t a = j * n_ + i;
                const uint32_t b = a + 1;
                const uint32_t c = a + n_;
                const uint32_t d = c + 1;
                idx[k++] = a; idx[k++] = c; idx[k++] = b;  // triangle 1
                idx[k++] = b; idx[k++] = c; idx[k++] = d;  // triangle 2
            }
        }
    }
    terrain_vid_ = vsg::VertexIndexDraw::create();
    terrain_vid_->assignArrays({t_pos_, t_nrm_, t_uv_});
    terrain_vid_->assignIndices(t_idx_);
    terrain_vid_->indexCount = static_cast<uint32_t>(nc * 6);
    terrain_vid_->instanceCount = 1;

    // ---- water arrays: fixed allocation, used subset varies with level ----
    w_pos_ = vsg::vec3Array::create(nv);
    w_pos_->properties.dataVariance = vsg::DYNAMIC_DATA;
    w_nrm_ = vsg::vec3Array::create(nv);
    w_nrm_->properties.dataVariance = vsg::DYNAMIC_DATA;
    w_uv_  = vsg::vec2Array::create(nv);
    for (uint32_t j = 0; j < n_; ++j)
        for (uint32_t i = 0; i < n_; ++i)
            (*w_uv_)[size_t(j) * n_ + i] = vsg::vec2(float(i) / (n_ - 1),
                                                     float(j) / (n_ - 1));
    w_idx_ = vsg::uintArray::create(nc * 6);
    w_idx_->properties.dataVariance = vsg::DYNAMIC_DATA;
    water_vid_ = vsg::VertexIndexDraw::create();
    water_vid_->assignArrays({w_pos_, w_nrm_, w_uv_});
    water_vid_->assignIndices(w_idx_);
    water_vid_->indexCount = 0;   // water hidden until a water type is set
    water_vid_->instanceCount = 1;

    // ---- procedural colour textures ----
    auto make_tex = []() -> vsg::ref_ptr<vsg::ubvec4Array2D> {
        auto tex = vsg::ubvec4Array2D::create(
            kTexSize, kTexSize,
            vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
        tex->properties.dataVariance = vsg::DYNAMIC_DATA;
        return tex;
    };
    terrain_tex_ = make_tex();
    water_tex_   = make_tex();

    // ---- state groups ------------------------------------------------------
    vsg::StateInfo si_terrain;
    // 使用光照+纹理管线: 地形颜色来自生成的 256x256 纹理, 且场景始终带
    // 环境光+雪效平行光 (ViewportKit::createDefaultLights)。若关灯 (unlit),
    // Builder 的纹理着色管线缺失顶点颜色输入会退化成黑色地形, 因此保持
    // lighting=true 与场景灯光一致 (与分析覆盖层同管)。
    si_terrain.lighting = true;
    si_terrain.two_sided = true;
    si_terrain.image = terrain_tex_;
    terrain_sg_ = builder->createStateGroup(si_terrain);
    terrain_sg_->addChild(terrain_vid_);

    vsg::StateInfo si_water;
    si_water.lighting = true;
    si_water.blending = true;
    si_water.two_sided = true;
    si_water.image = water_tex_;
    water_sg_ = builder->createStateGroup(si_water);
    water_sg_->addChild(water_vid_);

    if (!root_) root_ = vsg::Group::create();
    root_->children.clear();
    root_->addChild(terrain_sg_);
    root_->addChild(water_sg_);
    ++node_gen_;
}

// ===========================================================================
// Vertex / normal update
// ===========================================================================
void TerrainSystem::recomputeNormals()
{
    const double two_cell = 2.0 * cell_size_;
    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            const uint32_t im = (i > 0) ? i - 1 : 0;
            const uint32_t ip = (i + 1 < n_) ? i + 1 : n_ - 1;
            const uint32_t jm = (j > 0) ? j - 1 : 0;
            const uint32_t jp = (j + 1 < n_) ? j + 1 : n_ - 1;

            const double hx = heights_[size_t(j) * n_ + ip] -
                              heights_[size_t(j) * n_ + im];
            const double hz = heights_[size_t(jp) * n_ + i] -
                              heights_[size_t(jm) * n_ + i];
            vsg::vec3 n(float(-hx / two_cell), 1.0f, float(-hz / two_cell));
            const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 1e-12f) n /= len;
            normals_[size_t(j) * n_ + i] = n;
        }
    }
}

void TerrainSystem::updateVertices()
{
    if (!t_pos_ || !t_nrm_) return;
    const float* nrm = reinterpret_cast<const float*>(normals_.data());
    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            const size_t k = size_t(j) * n_ + i;
            (*t_pos_)[k] = vsg::vec3(float(i * cell_size_),
                                     float(heights_[k]),
                                     float(j * cell_size_));
            (*t_nrm_)[k] = normals_[k];
        }
    }
    (void)nrm;
    t_pos_->dirty();
    t_nrm_->dirty();
}

// ===========================================================================
// Water
// ===========================================================================
void TerrainSystem::refreshWaterMesh()
{
    if (!w_pos_ || !w_idx_ || !water_vid_) return;

    const double level = water_level_;
    std::vector<int32_t> vmap(size_t(n_) * n_, -1);

    // Pass 1: vertices below the water line.
    size_t vcount = 0;
    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            const size_t k = size_t(j) * n_ + i;
            if (heights_[k] < level) {
                vmap[k] = static_cast<int32_t>(vcount);
                (*w_pos_)[vcount] = vsg::vec3(float(i * cell_size_),
                                              float(level),
                                              float(j * cell_size_));
                (*w_uv_)[vcount] = (*t_uv_)[k];
                (*w_nrm_)[vcount] = vsg::vec3(0.0f, 1.0f, 0.0f);
                ++vcount;
            }
        }
    }

    // Pass 2: a triangle is emitted only when all 3 corners are submerged -
    // the water surface naturally follows the shoreline contour.
    size_t icount = 0;
    uint32_t* idx = reinterpret_cast<uint32_t*>(w_idx_->dataPointer());
    for (uint32_t j = 0; j < n_ - 1; ++j) {
        for (uint32_t i = 0; i < n_ - 1; ++i) {
            const int32_t a = vmap[size_t(j) * n_ + i];
            const int32_t b = vmap[size_t(j) * n_ + (i + 1)];
            const int32_t c = vmap[size_t(j + 1) * n_ + i];
            const int32_t d = vmap[size_t(j + 1) * n_ + (i + 1)];
            if (a >= 0 && c >= 0 && b >= 0) {
                idx[icount++] = uint32_t(a);
                idx[icount++] = uint32_t(c);
                idx[icount++] = uint32_t(b);
            }
            if (b >= 0 && c >= 0 && d >= 0) {
                idx[icount++] = uint32_t(b);
                idx[icount++] = uint32_t(c);
                idx[icount++] = uint32_t(d);
            }
        }
    }

    w_pos_->dirty();
    w_nrm_->dirty();
    w_uv_->dirty();
    w_idx_->dirty();
    water_vid_->indexCount = static_cast<uint32_t>(icount);
}

void TerrainSystem::carveFeatures(WaterType type)
{
    if (type == WaterType::None || type == WaterType::Ocean) return;

    const double e = extent_;

    // Distance function per water type; returns desired carve depth factor
    // in [0,1] (0 = untouched, 1 = carve fully).
    auto carve_factor = [&](double wx, double wz) -> double {
        switch (type) {
        case WaterType::Lake: {
            // Elliptical basin near the map centre.
            const double cx = 0.50 * e, cz = 0.56 * e;
            const double rx = 0.30 * e, rz = 0.22 * e;
            double d = std::sqrt(std::pow((wx - cx) / rx, 2) +
                                 std::pow((wz - cz) / rz, 2));
            return 1.0 - smoothstep01(0.55, 1.0, d);
        }
        case WaterType::Pond: {
            // Small pond in one corner.
            const double cx = 0.20 * e, cz = 0.74 * e;
            const double rx = 0.085 * e, rz = 0.06 * e;
            double d = std::sqrt(std::pow((wx - cx) / rx, 2) +
                                 std::pow((wz - cz) / rz, 2));
            return 1.0 - smoothstep01(0.5, 1.0, d);
        }
        case WaterType::River: {
            // Wandering river from one edge of the map to the opposite.
            const double width = 0.032 * e;
            const int nsamples = 96;
            double best = std::numeric_limits<double>::infinity();
            for (int s = 0; s <= nsamples; ++s) {
                const double t = double(s) / nsamples;
                const double cx = 0.03 * e + 0.94 * e * t;
                const double cz = e * (0.5 + 0.44 * std::sin(2.0 * M_PI *
                          (0.55 * t + 0.16 * std::sin(3.0 * M_PI * t))));
                const double dx = wx - cx, dz = wz - cz;
                best = std::min(best, std::sqrt(dx * dx + dz * dz));
            }
            return 1.0 - smoothstep01(0.45 * width, 1.05 * width, best);
        }
        default:
            return 0.0;
        }
    };

    // Carve the bed towards ~2 m below the water level.
    const double bed = water_level_ - 2.2;
    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            const size_t k = size_t(j) * n_ + i;
            const double wx = i * cell_size_;
            const double wz = j * cell_size_;
            const double f = carve_factor(wx, wz);
            if (f <= 0.0) continue;
            const double target = std::min(heights_[k], bed);
            heights_[k] = target * f + heights_[k] * (1.0 - f);
        }
    }
}

// ===========================================================================
// Procedural generation
// ===========================================================================
void TerrainSystem::generate(TerrainType type)
{
    terrain_type_ = type;
    const double freq_scale = 0.001 + 1.0 / extent_;

    for (uint32_t j = 0; j < n_; ++j) {
        for (uint32_t i = 0; i < n_; ++i) {
            const double wx = i * cell_size_;
            const double wz = j * cell_size_;
            const double x = wx * freq_scale;
            const double z = wz * freq_scale;
            const size_t k = size_t(j) * n_ + i;

            double h = 0.0;
            switch (type) {
            case TerrainType::Flat:
                h = 0.03 + 0.03 * fbm(x * 0.8, z * 0.8, 3, 2.0, 0.5, seed_);
                break;
            case TerrainType::Plains:
                h = 0.08 + 0.22 * fbm(x * 1.1, z * 1.1, 5, 2.1, 0.5, seed_);
                break;
            case TerrainType::Hills:
                h = 0.08 + 0.55 * fbm(x * 1.0, z * 1.0, 6, 2.0, 0.52, seed_);
                break;
            case TerrainType::Mountains: {
                double r = ridgedFbm(x * 0.85, z * 0.85, 6, 2.2, 0.5, seed_);
                double base = fbm(x * 1.4 + 13.7, z * 1.4 - 7.2, 4, 2.0, 0.5,
                                  seed_ + 7) * 0.35;
                h = 0.05 + 1.85 * r * r + base;
                break;
            }
            case TerrainType::Desert: {
                // Longitudinal dunes + fine ripple noise.
                const double dune = 0.5 + 0.5 * std::sin(wx * 0.009 + wz * 0.0065 +
                       1.7 * std::sin(wz * 0.0023 - wx * 0.0011));
                const double ripple = fbm(x * 6.0, z * 6.0, 3, 2.0, 0.5, seed_);
                h = 0.03 + 0.42 * dune + 0.06 * ripple;
                break;
            }
            case TerrainType::Canyon: {
                // High plateau cut by deep winding canyons.
                double c = fbm(x * 1.2, z * 1.2, 6, 2.0, 0.52, seed_);
                double detail = fbm(x * 3.5 + 5.0, z * 3.5 + 9.0, 3, 2.0, 0.5,
                                    seed_ + 3) * 0.08;
                h = 1.05 - 1.30 * c * c * c + 0.10 * detail + 0.04;
                break;
            }
            case TerrainType::Island: {
                // Radial falloff from the middle -> ocean shores.
                const double dx = (wx - 0.5 * extent_) / (0.46 * extent_);
                const double dz = (wz - 0.5 * extent_) / (0.46 * extent_);
                const double r = std::sqrt(dx * dx + dz * dz);
                const double mask = std::max(0.0, 1.0 - r) *
                                    std::max(0.0, 1.0 - r);
                const double base = fbm(x * 1.15, z * 1.15, 6, 2.1, 0.52, seed_);
                h = 0.02 + (0.25 + 1.5 * base) * mask;
                break;
            }
            case TerrainType::Gobi: {
                // Flat gravel desert with sparse yardang ridges.
                const double base = fbm(x * 1.6, z * 1.6, 4, 2.0, 0.5, seed_);
                const double yardang = ridgedFbm(x * 0.7 + 3.1, z * 0.7 - 2.4,
                                                 4, 2.1, 0.5, seed_ + 11);
                const double ridge = smoothstep01(0.60, 0.88, yardang);
                const double gravel = fbm(x * 8.0, z * 8.0, 2, 2.0, 0.5,
                                          seed_ + 23) * 0.05;
                h = 0.04 + 0.10 * base + 0.55 * ridge + gravel;
                break;
            }
            case TerrainType::Swamp: {
                // Near-flat wetland; the noise pockets become puddles once
                // the bundled water level sits in the lower third.
                const double base = fbm(x * 1.3, z * 1.3, 5, 2.0, 0.5, seed_);
                const double micro = fbm(x * 5.5 + 2.0, z * 5.5, 3, 2.0, 0.5,
                                         seed_ + 5);
                h = 0.06 + 0.10 * base + 0.05 * micro;
                break;
            }
            case TerrainType::Ocean: {
                // Deep seabed: abyssal plains + a mid-ocean ridge.
                const double abyss = fbm(x * 1.0, z * 1.0, 6, 2.1, 0.52, seed_);
                const double ridge = ridgedFbm(x * 0.6 + 8.8, z * 0.6 + 4.2,
                                               5, 2.2, 0.5, seed_ + 17);
                h = 0.02 + 0.22 * abyss + 0.30 * smoothstep01(0.55, 0.95, ridge);
                break;
            }
            }

            // Scale to world metres.
            heights_[k] = clampd(h * height_scale_, 0.0, height_scale_ * 1.5);
        }
    }

    // Re-centre the surface on y = 0: subtract the mean height so the
    // average terrain plane coincides with the world datum.  Without this
    // every generated biome sits fully above y = 0 and the reference grid
    // appears to be buried underneath the terrain.
    {
        double mean = 0.0;
        for (double h : heights_) mean += h;
        mean /= double(heights_.size());
        for (double& h : heights_) h -= mean;
    }

    computeWaterLevel_();
    if (type == TerrainType::Ocean) {
        // Composite biome: open sea covering most of the seabed.
        double lo = 0, hi = 0;
        heightRange(lo, hi);
        water_type_ = WaterType::Ocean;
        water_level_ = lo + 0.82 * (hi - lo);
    } else if (type == TerrainType::Swamp) {
        // Composite biome: shallow distributed puddles in the lows (no
        // big carving - the noise pockets hold the water).
        double lo = 0, hi = 0;
        heightRange(lo, hi);
        water_type_ = WaterType::Pond;
        water_level_ = lo + 0.32 * (hi - lo);
    } else {
        carveFeatures(water_type_);
    }
    // 重建节点/状态组并 ++node_gen_: 强制宿主下一帧 requestCompile,
    // 新建的描述符会把已填好的颜色纹理一并上传。否则 (setGrid 同尺寸早退
    // 时) 只有 CPU 端 dirty(), GPU 端纹理/几何不刷新 → 模板打开后地形发黑。
    buildNodes();
    recomputeNormals();
    updateVertices();
    refreshWaterMesh();
    regenerateColorTexture();
    spdlog::info("TerrainSystem: generated {} terrain ({}x{}, {} m, seed {})",
                 int(terrain_type_), n_, n_, int(extent_), seed_);
}

/// Pick a pleasing default water level relative to the generated heights.
void TerrainSystem::computeWaterLevel_()
{
    double lo = std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();
    for (double h : heights_) { lo = std::min(lo, h); hi = std::max(hi, h); }
    water_level_ = lo + 0.12 * (hi - lo);
}

void TerrainSystem::applyWater(WaterType type)
{
    water_type_ = type;
    if (type == WaterType::None) {
        refreshWaterMesh();   // empties the water draw
        return;
    }
    carveFeatures(type);
    buildNodes();   // 强制重编译 (同 generate(), 保证 GPU 纹理/几何刷新)
    recomputeNormals();
    updateVertices();
    refreshWaterMesh();
    regenerateColorTexture();
    spdlog::info("TerrainSystem: applied water '{}' @ {:.1f} m",
                 int(water_type_), water_level_);
}

void TerrainSystem::setWaterLevel(double level)
{
    water_level_ = clampd(level, -20.0, height_scale_ * 3.0);
    refreshWaterMesh();
}

// ===========================================================================
// Brush editing
// ===========================================================================
void TerrainSystem::applyBrush(double world_x, double world_z, BrushMode mode,
                               double radius, double strength,
                               double target_height)
{
    if (mode == BrushMode::None || radius <= 0.0 || strength <= 0.0) return;
    if (world_x < 0 || world_x > extent_ || world_z < 0 || world_z > extent_)
        return;

    const double r2 = radius * radius;
    const double dstr = strength * 0.5;              // metres per stroke
    const double cell = cell_size_;

    // Grid index bounds affected by the brush circle.
    const int i0 = std::max(0, int(std::floor((world_x - radius) / cell)));
    const int i1 = std::min(int(n_ - 1), int(std::ceil((world_x + radius) / cell)));
    const int j0 = std::max(0, int(std::floor((world_z - radius) / cell)));
    const int j1 = std::min(int(n_ - 1), int(std::ceil((world_z + radius) / cell)));

    const double hmin = -height_scale_ * 0.25;
    const double hmax = height_scale_ * 1.5;

    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const double gx = i * cell;
            const double gz = j * cell;
            const double dx = gx - world_x, dz = gz - world_z;
            const double dist = std::sqrt(dx * dx + dz * dz);
            if (dist > radius) continue;
            const double fall = 1.0 - smoothstep01(0.35, 1.0, dist / radius);

            const size_t k = size_t(j) * n_ + i;
            double& h = heights_[k];
            switch (mode) {
            case BrushMode::Raise:
                h += dstr * fall;
                break;
            case BrushMode::Lower:
                h -= dstr * fall;
                break;
            case BrushMode::Smooth: {
                // 3x3 box blur, excluding self.
                double sum = 0.0;
                int cnt = 0;
                for (int dj = -1; dj <= 1; ++dj) {
                    for (int di = -1; di <= 1; ++di) {
                        if (di == 0 && dj == 0) continue;
                        const int ii = i + di, jj = j + dj;
                        if (ii < 0 || ii >= int(n_) || jj < 0 || jj >= int(n_))
                            continue;
                        sum += heights_[size_t(jj) * n_ + ii];
                        ++cnt;
                    }
                }
                if (cnt > 0) {
                    const double avg = sum / cnt;
                    h += (avg - h) * (0.5 * fall) * (strength * 0.02 + 0.05);
                }
                break;
            }
            case BrushMode::Flatten:
                h += (target_height - h) * (0.35 * fall) * (strength * 0.02 + 0.1);
                break;
            case BrushMode::Noise: {
                // Seeded per-vertex jitter (stable across strokes).
                const double j =
                    (hash2i(uint32_t(i) + 3u * uint32_t(j), uint32_t(j),
                            seed_ + 901u) & 0xFFFFu) / 65535.0 - 0.5;
                h += j * 2.0 * dstr * fall;
                break;
            }
            case BrushMode::Plateau: {
                // Latch the stroke-centre height once, then pull towards it.
                if (!plateau_captured_) {
                    plateau_capture_h_ = sampleBilinear(world_x, world_z);
                    plateau_captured_ = true;
                }
                h += (plateau_capture_h_ - h) * (0.30 * fall) *
                     (strength * 0.02 + 0.1);
                break;
            }
            case BrushMode::Erode: {
                // Thermal erosion step: move material towards the lowest
                // of the 4 direct neighbours.
                double lo_h = h;
                static const int dx4[4] = {-1, 1, 0, 0};
                static const int dz4[4] = {0, 0, -1, 1};
                for (int nb = 0; nb < 4; ++nb) {
                    const int ii = i + dx4[nb], jj = j + dz4[nb];
                    if (ii < 0 || ii >= int(n_) || jj < 0 || jj >= int(n_))
                        continue;
                    lo_h = std::min(lo_h, heights_[size_t(jj) * n_ + ii]);
                }
                h += (lo_h - h) * (0.25 * fall) * (strength * 0.02 + 0.08);
                break;
            }
            case BrushMode::None:
                break;
            }
            h = clampd(h, hmin, hmax);
        }
    }
}

// ===========================================================================
// Height-field snapshots (host-side undo/redo)
// ===========================================================================
bool TerrainSystem::setHeightsData(const std::vector<double>& h)
{
    if (h.size() != heights_.size()) {
        spdlog::error("TerrainSystem: setHeightsData size mismatch ({} vs {})",
                      h.size(), heights_.size());
        return false;
    }
    heights_ = h;
    recomputeNormals();
    updateVertices();
    refreshWaterMesh();
    tex_dirty_ = true;
    return true;
}

// ===========================================================================
// Picking
// ===========================================================================
double TerrainSystem::sampleBilinear(double wx, double wz) const
{
    if (wx < 0 || wx > extent_ || wz < 0 || wz > extent_) return -1e9;
    const double gx = clampd(wx / cell_size_, 0.0, double(n_ - 1));
    const double gz = clampd(wz / cell_size_, 0.0, double(n_ - 1));
    const int ix = std::min(int(n_) - 2, int(std::floor(gx)));
    const int iz = std::min(int(n_) - 2, int(std::floor(gz)));
    const double fx = gx - ix, fz = gz - iz;

    const double h00 = heights_[size_t(iz) * n_ + ix];
    const double h10 = heights_[size_t(iz) * n_ + (ix + 1)];
    const double h01 = heights_[size_t(iz + 1) * n_ + ix];
    const double h11 = heights_[size_t(iz + 1) * n_ + (ix + 1)];
    return h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) +
           h01 * (1 - fx) * fz + h11 * fx * fz;
}

bool TerrainSystem::pick(const vsg::dvec3& origin, const vsg::dvec3& dir,
                         double& world_x, double& world_z, double max_dist) const
{
    const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-12) return false;
    const vsg::dvec3 d = dir / len;

    const double step = cell_size_ * 0.5;
    const double tmax = std::min(max_dist, 1.0e6);
    double t = 0.0;
    vsg::dvec3 p = origin;

    while (t < tmax) {
        if (p.x < 0 || p.x > extent_ || p.z < 0 || p.z > extent_) {
            // Already outside the terrain column: keep marching in case the
            // ray re-enters (e.g. over a hill crest).
            p += d * step;
            t += step;
            continue;
        }
        const double h = sampleBilinear(p.x, p.z);
        if (p.y <= h) {
            // Hit between [t-step, t]: bisection refinement.
            double t0 = std::max(0.0, t - step), t1 = t;
            for (int it = 0; it < 24; ++it) {
                const double tm = 0.5 * (t0 + t1);
                const vsg::dvec3 pm = origin + d * tm;
                if (pm.y <= sampleBilinear(pm.x, pm.z)) t1 = tm; else t0 = tm;
            }
            const vsg::dvec3 hit = origin + d * t1;
            world_x = hit.x;
            world_z = hit.z;
            return true;
        }
        p += d * step;
        t += step;
    }
    return false;
}

// ===========================================================================
// Queries
// ===========================================================================
double TerrainSystem::heightAtWorld(double wx, double wz) const
{
    const double h = sampleBilinear(wx, wz);
    return (h < -1e8) ? 0.0 : h;
}

vsg::vec3 TerrainSystem::normalAtWorld(double wx, double wz) const
{
    if (wx < 0 || wx > extent_ || wz < 0 || wz > extent_)
        return vsg::vec3(0.0f, 1.0f, 0.0f);
    const double gx = clampd(wx / cell_size_, 0.0, double(n_ - 1));
    const double gz = clampd(wz / cell_size_, 0.0, double(n_ - 1));
    const int ix = std::min(int(n_) - 2, int(std::floor(gx)));
    const int iz = std::min(int(n_) - 2, int(std::floor(gz)));
    const double fx = gx - ix, fz = gz - iz;
    const vsg::vec3& n00 = normals_[size_t(iz) * n_ + ix];
    const vsg::vec3& n10 = normals_[size_t(iz) * n_ + (ix + 1)];
    const vsg::vec3& n01 = normals_[size_t(iz + 1) * n_ + ix];
    const vsg::vec3& n11 = normals_[size_t(iz + 1) * n_ + (ix + 1)];
    vsg::vec3 n = n00 * float((1 - fx) * (1 - fz)) + n10 * float(fx * (1 - fz)) +
                  n01 * float((1 - fx) * fz) + n11 * float(fx * fz);
    const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l > 1e-12f) n /= l;
    return n;
}

void TerrainSystem::heightRange(double& lo, double& hi) const
{
    lo = std::numeric_limits<double>::max();
    hi = -std::numeric_limits<double>::max();
    for (double h : heights_) { lo = std::min(lo, h); hi = std::max(hi, h); }
    if (lo > hi) lo = hi = 0.0;
}

// ===========================================================================
// Procedural colour texture
// ===========================================================================
void TerrainSystem::regenerateColorTexture()
{
    if (!terrain_tex_) return;

    // Height range of the current surface: the colour ramp below keys off
    // the normalised height so even low-relief biomes (Plains) get a visible
    // colour gradient instead of a uniform green sheet.
    double hlo = 0.0, hhi = 0.0;
    heightRange(hlo, hhi);
    const double hspan = std::max(1e-6, hhi - hlo);

    // Per-biome base palette (grass / rock / sand).
    double grass[3] = {0.30, 0.44, 0.20};
    double rock[3]  = {0.47, 0.44, 0.40};
    double sand[3]  = {0.74, 0.69, 0.50};
    switch (terrain_type_) {
    case TerrainType::Desert:
        grass[0] = 0.80; grass[1] = 0.68; grass[2] = 0.44;
        rock[0]  = 0.66; rock[1]  = 0.52; rock[2]  = 0.34;
        sand[0]  = 0.87; sand[1]  = 0.78; sand[2]  = 0.58;
        break;
    case TerrainType::Gobi:
        grass[0] = 0.62; grass[1] = 0.53; grass[2] = 0.40;
        rock[0]  = 0.55; rock[1]  = 0.47; rock[2]  = 0.40;
        sand[0]  = 0.80; sand[1]  = 0.72; sand[2]  = 0.56;
        break;
    case TerrainType::Swamp:
        grass[0] = 0.21; grass[1] = 0.29; grass[2] = 0.17;
        rock[0]  = 0.33; rock[1]  = 0.31; rock[2]  = 0.24;
        sand[0]  = 0.42; sand[1]  = 0.36; sand[2]  = 0.26;   // mud
        break;
    case TerrainType::Ocean:
        grass[0] = 0.30; grass[1] = 0.38; grass[2] = 0.33;   // sediment
        rock[0]  = 0.38; rock[1]  = 0.40; rock[2]  = 0.42;
        sand[0]  = 0.62; sand[1]  = 0.60; sand[2]  = 0.47;
        break;
    default:
        break;
    }

    for (uint32_t v = 0; v < kTexSize; ++v) {
        const double wz = (v + 0.5) / kTexSize * extent_;
        for (uint32_t u = 0; u < kTexSize; ++u) {
            const double wx = (u + 0.5) / kTexSize * extent_;
            const double h = sampleBilinear(wx, wz);
            const double t = clampd(h / height_scale_, 0.0, 1.0);
            const double tnorm = clampd((h - hlo) / hspan, 0.0, 1.0);
            const double nrm_y = normalAtWorld(wx, wz).y;
            const double slope = clampd(1.0 - nrm_y, 0.0, 1.0);
            const double n1 = valueNoise(wx * 0.05, wz * 0.05, seed_ + 33);
            const double n2 = valueNoise(wx * 0.17 + 7.0, wz * 0.17, seed_ + 55);

            // Sand around / under water, grass on flats, rock on slopes,
            // snow on peaks.
            double r, g, b;
            const double sand_m = 1.0 - smoothstep01(water_level_ - 0.5,
                                                     water_level_ + 1.2, h);
            const double snow = smoothstep01(0.68, 0.92, t);
            const double rock_m = smoothstep01(0.12, 0.5, slope) * (1.0 - sand_m);

            // grass base: darker olive in the lows, sun-lit yellow-green
            // on the highs (normalised so flat biomes still show banding)
            const double lit = 0.66 + 0.64 * tnorm;
            r = grass[0] * lit + 0.05 * n2; g = grass[1] * lit + 0.06 * n2; b = grass[2] * lit + 0.03 * n2;
            // blend rock
            r += (rock[0] - r) * rock_m; g += (rock[1] - g) * rock_m; b += (rock[2] - b) * rock_m;
            // blend sand
            r += (sand[0] - r) * sand_m; g += (sand[1] - g) * sand_m; b += (sand[2] - b) * sand_m;
            // blend snow (not on ocean / swamp biomes)
            if (terrain_type_ != TerrainType::Ocean &&
                terrain_type_ != TerrainType::Swamp) {
                r += (0.94 - r) * snow; g += (0.96 - g) * snow; b += (1.00 - b) * snow;
            }
            // submerged sediment tint for ocean floors / swamp puddles
            if (h < water_level_) {
                const double sub = clampd((water_level_ - h) / 8.0, 0.0, 1.0) * 0.5;
                r += (0.24 - r) * sub; g += (0.31 - g) * sub; b += (0.30 - b) * sub;
            }

            // subtle per-texel tint variation
            const double tint = 0.93 + 0.14 * n1;
            terrain_tex_->set(u, v, vsg::ubvec4(
                static_cast<uint8_t>(clampd(r * tint, 0, 1) * 255.0),
                static_cast<uint8_t>(clampd(g * tint, 0, 1) * 255.0),
                static_cast<uint8_t>(clampd(b * tint, 0, 1) * 255.0), 255));

            // translucent water texture (same layout)
            const double wn = valueNoise(wx * 0.09, wz * 0.09, seed_ + 77);
            water_tex_->set(u, v, vsg::ubvec4(
                static_cast<uint8_t>(clampd(0.05 + 0.10 * wn, 0, 1) * 255.0),
                static_cast<uint8_t>(clampd(0.22 + 0.14 * wn, 0, 1) * 255.0),
                static_cast<uint8_t>(clampd(0.45 + 0.22 * wn, 0, 1) * 255.0),
                205));
        }
    }
    terrain_tex_->dirty();
    water_tex_->dirty();
}

void TerrainSystem::flushPending()
{
    if (!tex_dirty_) return;
    tex_dirty_ = false;
    regenerateColorTexture();
}

// ===========================================================================
// Persistence (custom binary .ter)
// ===========================================================================
bool TerrainSystem::saveHeightmap(const std::string& path) const
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        spdlog::error("TerrainSystem: cannot write '{}'", path);
        return false;
    }
    const char magic[8] = {'V', 'S', 'G', 'T', 'E', 'R', '1', '\n'};
    out.write(magic, 8);
    const uint32_t n = n_;
    const double extent = extent_, hscale = height_scale_, seed = seed_;
    const double wl = water_level_;
    const uint32_t wt = uint32_t(water_type_), tt = uint32_t(terrain_type_);
    out.write(reinterpret_cast<const char*>(&n), 4);
    out.write(reinterpret_cast<const char*>(&extent), 8);
    out.write(reinterpret_cast<const char*>(&hscale), 8);
    out.write(reinterpret_cast<const char*>(&seed), 8);
    out.write(reinterpret_cast<const char*>(&wl), 8);
    out.write(reinterpret_cast<const char*>(&wt), 4);
    out.write(reinterpret_cast<const char*>(&tt), 4);
    out.write(reinterpret_cast<const char*>(heights_.data()),
              sizeof(double) * heights_.size());
    if (!out) {
        spdlog::error("TerrainSystem: short write to '{}'", path);
        return false;
    }
    spdlog::info("TerrainSystem: saved '{}' ({}x{})", path, n_, n_);
    return true;
}

bool TerrainSystem::loadHeightmap(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        spdlog::error("TerrainSystem: cannot open '{}'", path);
        return false;
    }
    char magic[8];
    in.read(magic, 8);
    if (std::string(magic, 8) != std::string("VSGTER1\n")) {
        spdlog::error("TerrainSystem: '{}' is not a .ter heightmap", path);
        return false;
    }
    uint32_t n = 0;
    double extent = 0, hscale = 0, seed = 0, wl = 0;
    uint32_t wt = 0, tt = 0;
    in.read(reinterpret_cast<char*>(&n), 4);
    in.read(reinterpret_cast<char*>(&extent), 8);
    in.read(reinterpret_cast<char*>(&hscale), 8);
    in.read(reinterpret_cast<char*>(&seed), 8);
    in.read(reinterpret_cast<char*>(&wl), 8);
    in.read(reinterpret_cast<char*>(&wt), 4);
    in.read(reinterpret_cast<char*>(&tt), 4);
    if (n < 8 || n > 4096 || extent <= 0 || hscale <= 0) {
        spdlog::error("TerrainSystem: bad header in '{}'", path);
        return false;
    }

    std::vector<double> data(size_t(n) * n);
    in.read(reinterpret_cast<char*>(data.data()), sizeof(double) * data.size());
    if (!in) {
        spdlog::error("TerrainSystem: short height data in '{}'", path);
        return false;
    }

    n_ = n;
    extent_ = extent;
    height_scale_ = hscale;
    seed_ = uint32_t(seed);
    water_level_ = wl;
    water_type_ = WaterType(wt);
    terrain_type_ = TerrainType(tt);
    cell_size_ = extent_ / double(n_ - 1);
    heights_ = std::move(data);
    normals_.assign(size_t(n_) * n_, vsg::vec3(0.0f, 1.0f, 0.0f));
    buildNodes();          // rebuild arrays at the loaded resolution
    recomputeNormals();
    updateVertices();
    refreshWaterMesh();
    regenerateColorTexture();
    spdlog::info("TerrainSystem: loaded '{}' ({}x{})", path, n_, n_);
    return true;
}

} // namespace vsgraysim