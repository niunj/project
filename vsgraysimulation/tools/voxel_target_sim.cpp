// voxel_target_sim.cpp
//
// End-to-end demonstration for the voxel target pipeline:
//
//   obj / osgb / vsgb / ive  ->  voxelize  ->  bind physics
//     ->  collision ray cast  ->  blast damage (explosion)
//     ->  structural collapse  ->  rigid-body debris  ->  PNG renders
//
// Every state is rendered with a CPU voxel ray caster (Amanatides-Woo
// DDA per pixel) so the demo does not depend on a window, Vulkan swapchain
// or GUI.  PNG output uses a small zlib-based writer so the tool only needs
// zlib, which is already part of the build.
//
// Usage:
//   voxel_target_sim <model.(obj|osgb|vsgb|ive)> <output_dir>
//                    [--voxels N] [--blast-radius R] [--blast-energy E]
//                    [--seed S]

#include "VoxelTerrainPhysics.h"
#include "VoxelTerrainDebris.h"
#include "VoxelModelVoxelizer.h"
#include "VoxelTerrain.h"

#include <vsgraysim/layer0_phys/PhysicsEngine.h>

using namespace vsgraysim::earth;
using vsgraysim::PhysicsEngine;

#include <vsg/maths/vec3.h>
#include <vsg/maths/mat4.h>
#include <vsg/io/Logger.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

using vsg::dvec3;

// ---------------------------------------------------------------------------
// Minimal PNG writer (8-bit RGB, no interlace). Only needs zlib.
// ---------------------------------------------------------------------------
class PngWriter {
public:
    PngWriter(std::string path, int width, int height)
        : path_(std::move(path)), width_(width), height_(height),
          pixels_(size_t(width) * height * 3, 0) {}

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        const size_t i = (size_t(y) * width_ + x) * 3;
        pixels_[i + 0] = r;
        pixels_[i + 1] = g;
        pixels_[i + 2] = b;
    }

    bool save() const {
        std::ofstream out(path_, std::ios::binary);
        if (!out) return false;
        static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        out.write(reinterpret_cast<const char*>(signature), 8);

        const uint32_t w = uint32_t(width_), h = uint32_t(height_);
        writeChunk(out, "IHDR", [&]{
            std::array<uint8_t,13> data{};
            data[0]=uint8_t(w>>24); data[1]=uint8_t(w>>16); data[2]=uint8_t(w>>8);  data[3]=uint8_t(w);
            data[4]=uint8_t(h>>24); data[5]=uint8_t(h>>16); data[6]=uint8_t(h>>8);  data[7]=uint8_t(h);
            data[8]=8;  // bit depth
            data[9]=2;  // color type: truecolour RGB
            // compression / filter / interlace = 0
            return data;
        }());

        // Raw image stream with per-scanline filter byte 0.
        std::vector<uint8_t> raw(size_t(height_) * (1 + size_t(width_) * 3));
        for (int y = 0; y < height_; ++y) {
            uint8_t* row = raw.data() + size_t(y) * (1 + size_t(width_) * 3);
            row[0] = 0;  // filter type None
            const uint8_t* src = pixels_.data() + size_t(y) * width_ * 3;
            std::memcpy(row + 1, src, size_t(width_) * 3);
        }
        uLongf compressedSize = compressBound(uLong(raw.size()));
        std::vector<uint8_t> compressed(compressedSize);
        if (compress2(compressed.data(), &compressedSize, raw.data(), uLong(raw.size()),
                      Z_BEST_SPEED) != Z_OK) return false;
        compressed.resize(compressedSize);
        writeChunk(out, "IDAT", compressed);
        writeChunk(out, "IEND", std::vector<uint8_t>{});
        return out.good();
    }

private:
    static uint32_t crc32_of(const std::string& type, const std::vector<uint8_t>& data) {
        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(type.data()), uInt(type.size()));
        if (!data.empty()) crc = crc32(crc, data.data(), uInt(data.size()));
        return uint32_t(crc);
    }
    template <typename Container>
    static void writeChunk(std::ofstream& out, const std::string& type, const Container& data) {
        const uint32_t length = uint32_t(data.size());
        std::array<uint8_t,4> lenBytes{uint8_t(length>>24), uint8_t(length>>16),
                                       uint8_t(length>>8), uint8_t(length)};
        out.write(reinterpret_cast<const char*>(lenBytes.data()), 4);
        out.write(type.data(), 4);
        if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        const uint32_t crc = crc32_of(type, {data.begin(), data.end()});
        std::array<uint8_t,4> crcBytes{uint8_t(crc>>24), uint8_t(crc>>16),
                                       uint8_t(crc>>8), uint8_t(crc)};
        out.write(reinterpret_cast<const char*>(crcBytes.data()), 4);
    }

    std::string path_;
    int width_, height_;
    std::vector<uint8_t> pixels_;
};

// ---------------------------------------------------------------------------
// CPU voxel ray caster. Renders the occupancy grid with a simple Lambert
// shading model: three directional lights (key / fill / rim) + ambient.
// ---------------------------------------------------------------------------
struct Camera {
    dvec3 eye{0,0,0};
    dvec3 right{1,0,0};
    dvec3 up{0,1,0};
    dvec3 forward{0,0,-1};
    double orthoHalfWidth = 1.0;   // world units covered by half the image width
    double orthoHalfHeight = 1.0;
};

struct RenderResult {
    int width = 0, height = 0;
    long long primaryRays = 0;
    long long hitRays = 0;
};

bool voxelAt(const VoxelTerrainPhysics& physics, const dvec3& p) {
    const auto occupied = physics.occupiedLocal(p.x, p.y, p.z);
    return occupied.has_value() && *occupied;
}

// Amanatides-Woo DDA. Returns true and fills hit/normal when an occupied
// voxel is hit.  The ray may start outside the grid.
bool rayVoxel(const VoxelTerrainPhysics& physics, const dvec3& origin, const dvec3& dir,
              double maxDist, dvec3& hit, dvec3& normal) {
    const vsg::dbox bounds = physics.bounds();
    const double cell = physics.cellSize();
    const int cx = physics.columnsX(), cz = physics.columnsZ(), layers = physics.layers();
    const dvec3 lo{bounds.min.x, bounds.min.y, bounds.min.z};
    const dvec3 hi{bounds.max.x, bounds.max.y, bounds.max.z};

    auto gridOf = [&](const dvec3& p) {
        return std::array<int,3>{int(std::floor((p.x - lo.x) / cell)),
                                 int(std::floor((p.y - lo.y) / cell)),
                                 int(std::floor((p.z - lo.z) / cell))};
    };
    auto inGrid = [&](const std::array<int,3>& g) {
        return g[0] >= 0 && g[0] < cx && g[1] >= 0 && g[1] < layers && g[2] >= 0 && g[2] < cz;
    };

    std::array<int,3> grid = gridOf(origin);
    double t = 0.0;
    if (!inGrid(grid)) {
        dvec3 tNear, tFar;
        for (int a = 0; a < 3; ++a) {
            const double d = dir[a];
            if (std::abs(d) < 1e-12) {
                tNear[a] = -std::numeric_limits<double>::infinity();
                tFar[a]  =  std::numeric_limits<double>::infinity();
                continue;
            }
            const double t1 = (lo[a] - origin[a]) / d;
            const double t2 = (hi[a] - origin[a]) / d;
            tNear[a] = std::min(t1, t2);
            tFar[a]  = std::max(t1, t2);
        }
        t = std::max({tNear.x, tNear.y, tNear.z});
        const double tExit = std::min({tFar.x, tFar.y, tFar.z});
        if (t >= tExit || tExit < 0 || t >= maxDist) return false;
        t = std::max(t, 0.0);
        grid = gridOf(origin + dir * (t + 1e-6));
        if (!inGrid(grid)) return false;
    }

    const std::array<int,3> step{dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1};
    dvec3 tMax, tDelta;
    for (int a = 0; a < 3; ++a) {
        const double d = dir[a];
        const double cellOrigin = lo[a] + grid[a] * cell;
        if (std::abs(d) < 1e-12) {
            tMax[a] = std::numeric_limits<double>::infinity();
            tDelta[a] = std::numeric_limits<double>::infinity();
        } else {
            tMax[a] = (d > 0 ? (cellOrigin + cell - origin[a]) : (origin[a] - cellOrigin)) / std::abs(d);
            tDelta[a] = cell / std::abs(d);
        }
    }
    for (int iter = 0; iter < 4096 && t < maxDist; ++iter) {
        if (inGrid(grid) &&
            physics.occupiedLocal(lo.x + (grid[0] + .5) * cell,
                                  lo.y + (grid[1] + .5) * cell,
                                  lo.z + (grid[2] + .5) * cell).value_or(false)) {
            hit = origin + dir * t;
            if (tMax.x < tMax.y && tMax.x < tMax.z) normal = {double(-step[0]), 0, 0};
            else if (tMax.y < tMax.z)                normal = {0, double(-step[1]), 0};
            else                                     normal = {0, 0, double(-step[2])};
            return true;
        }
        if (tMax.x < tMax.y && tMax.x < tMax.z)      { grid[0] += step[0]; t = tMax.x; tMax.x += tDelta.x; }
        else if (tMax.y < tMax.z)                    { grid[1] += step[1]; t = tMax.y; tMax.y += tDelta.y; }
        else                                         { grid[2] += step[2]; t = tMax.z; tMax.z += tDelta.z; }
        if (!inGrid(grid)) return false;
    }
    return false;
}

dvec3 shade(const dvec3& hit, const dvec3& normal, const dvec3& eye) {
    // Base material: concrete grey. Slight checker on (x,z) so voxelisation
    // pattern is visible in the renders.
    const int checker = (int(std::floor(hit.x / 0.5)) + int(std::floor(hit.z / 0.5))) & 1;
    dvec3 albedo = checker ? dvec3{0.62, 0.60, 0.57} : dvec3{0.55, 0.53, 0.50};

    // Directional key light (upper-front-left).
    const dvec3 keyDir = vsg::normalize(dvec3{-0.45, 0.85, 0.30});
    const dvec3 fillDir = vsg::normalize(dvec3{0.60, 0.35, -0.70});
    const dvec3 rimDir  = vsg::normalize(dvec3{0.20, 0.20, -0.95});
    const dvec3 viewDir = vsg::normalize(eye - hit);

    const double nDotKey  = std::max(0.0, vsg::dot(normal, keyDir));
    const double nDotFill = std::max(0.0, vsg::dot(normal, fillDir));
    const double nDotRim  = std::max(0.0, vsg::dot(normal, rimDir));

    // Specular highlight from the key light (Blinn-Phong).
    const dvec3 halfV = vsg::normalize(keyDir + viewDir);
    const double spec = std::pow(std::max(0.0, vsg::dot(normal, halfV)), 32.0);

    const dvec3 ambient{0.14, 0.15, 0.17};
    const dvec3 color = ambient
        + albedo * nDotKey  * dvec3{1.00, 0.96, 0.88} * 1.05
        + albedo * nDotFill * dvec3{0.55, 0.65, 0.80} * 0.45
        + albedo * nDotRim  * dvec3{0.80, 0.85, 1.00} * 0.30
        + dvec3{1.0, 0.97, 0.90} * spec * 0.30;
    return dvec3{
        std::clamp(color.x, 0.0, 1.0) * 255.0 + 0.5,
        std::clamp(color.y, 0.0, 1.0) * 255.0 + 0.5,
        std::clamp(color.z, 0.0, 1.0) * 255.0 + 0.5};
}

// Place the camera on an orbit around `target`. Elevation is in degrees
// above the horizon; azimuth is measured from +X towards +Z.
Camera orbitCamera(const dvec3& target, double radius,
                   double azimuthDeg, double elevationDeg, double fovScale) {
    const double az = azimuthDeg * M_PI / 180.0;
    const double el = elevationDeg * M_PI / 180.0;
    Camera cam;
    cam.eye = target + dvec3{radius * std::cos(el) * std::cos(az),
                             radius * std::sin(el),
                             radius * std::cos(el) * std::sin(az)};
    cam.forward = vsg::normalize(target - cam.eye);
    // Right = normalize(forward x worldUp). Up = right x forward.
    const dvec3 worldUp{0, 1, 0};
    cam.right = vsg::normalize(vsg::cross(cam.forward, worldUp));
    cam.up = vsg::cross(cam.right, cam.forward);
    const double aspect = 4.0 / 3.0;
    cam.orthoHalfWidth = radius * fovScale;
    cam.orthoHalfHeight = cam.orthoHalfWidth / aspect;
    return cam;
}

RenderResult renderView(const VoxelTerrainPhysics& physics, const Camera& cam,
                        const std::string& path, int width = 800, int height = 600,
                        const std::vector<std::pair<vsg::dmat4,vsg::dvec3>>* debrisBoxes = nullptr) {
    PngWriter png(path, width, height);
    const vsg::dbox bounds = physics.bounds();
    const dvec3 lo{bounds.min.x, bounds.min.y, bounds.min.z};
    const dvec3 hi{bounds.max.x, bounds.max.y, bounds.max.z};
    const dvec3 centre = (lo + hi) * 0.5;
    // Push the ray origin far back along -forward so we definitely start outside.
    const double startBack = vsg::length(hi - lo) * 0.5 + 1.0;
    const double maxDist = vsg::length(hi - lo) * 1.5 + 2.0;

    // Background: vertical gradient sky.
    for (int y = 0; y < height; ++y) {
        const double fy = double(y) / double(height - 1);
        const dvec3 top{0.36, 0.52, 0.72};
        const dvec3 bottom{0.72, 0.78, 0.84};
        const dvec3 c = bottom + (top - bottom) * (1.0 - fy);
        for (int x = 0; x < width; ++x)
            png.set(x, y, uint8_t(c.x*255+.5), uint8_t(c.y*255+.5), uint8_t(c.z*255+.5));
    }

    RenderResult result;
    result.width = width; result.height = height;
    for (int y = 0; y < height; ++y) {
        // Screen Y goes top->bottom; camera up is +up.
        const double sy = (1.0 - 2.0 * (double(y) + 0.5) / height) * cam.orthoHalfHeight;
        for (int x = 0; x < width; ++x) {
            const double sx = (2.0 * (double(x) + 0.5) / width - 1.0) * cam.orthoHalfWidth;
            const dvec3 planePoint = centre + cam.right * sx + cam.up * sy;
            const dvec3 origin = planePoint - cam.forward * startBack;
            dvec3 hit, normal;
            ++result.primaryRays;
            if (rayVoxel(physics, origin, cam.forward, maxDist, hit, normal)) {
                ++result.hitRays;
                const dvec3 col = shade(hit, normal, cam.eye);
                png.set(x, y, uint8_t(col.x), uint8_t(col.y), uint8_t(col.z));
            }
        }
    }
    // Overlay rigid-body debris as shaded orange wireframe boxes.
    if (debrisBoxes && !debrisBoxes->empty()) {
        auto project = [&](const dvec3& p) -> std::pair<int,int> {
            const dvec3 rel = p - cam.eye;
            const double depth = vsg::dot(rel, cam.forward);
            if (depth < 1e-6) return {-1, -1};
            const double sx = vsg::dot(rel, cam.right);
            const double sy = vsg::dot(rel, cam.up);
            const double scale = double(width) / (2.0 * cam.orthoHalfWidth);
            return {int((0.5 + sx / (2.0 * cam.orthoHalfWidth)) * width),
                    int((0.5 - sy / (2.0 * cam.orthoHalfHeight)) * height)};
        };
        for (const auto& [transform, halfExtents] : *debrisBoxes) {
            const dvec3 corners[8] = {
                { halfExtents.x,-halfExtents.y, halfExtents.z},{ halfExtents.x,-halfExtents.y,-halfExtents.z},
                {-halfExtents.x,-halfExtents.y,-halfExtents.z},{-halfExtents.x,-halfExtents.y, halfExtents.z},
                { halfExtents.x, halfExtents.y, halfExtents.z},{ halfExtents.x, halfExtents.y,-halfExtents.z},
                {-halfExtents.x, halfExtents.y,-halfExtents.z},{-halfExtents.x, halfExtents.y, halfExtents.z}};
            dvec3 world[8];
            for (int k = 0; k < 8; ++k) {
                const auto w = transform * vsg::dvec4(corners[k], 1.0);
                world[k] = {w.x, w.y, w.z};
            }
            static const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                                             {0,4},{1,5},{2,6},{3,7}};
            for (auto& edge : edges) {
                auto a = project(world[edge[0]]);
                auto b = project(world[edge[1]]);
                if (a.first < 0 || b.first < 0) continue;
                int x0 = a.first, y0 = a.second, x1 = b.first, y1 = b.second;
                const int dx = std::abs(x1-x0), sxStep = x0<x1?1:-1;
                const int dy = -std::abs(y1-y0), syStep = y0<y1?1:-1;
                int err = dx + dy;
                while (true) {
                    if (x0 >= 0 && x0 < width && y0 >= 0 && y0 < height)
                        png.set(x0, y0, 235, 130, 35);
                    if (x0 == x1 && y0 == y1) break;
                    const int e2 = 2*err;
                    if (e2 >= dy) { err += dy; x0 += sxStep; }
                    if (e2 <= dx) { err += dx; y0 += syStep; }
                }
            }
        }
    }
    if (!png.save()) throw std::runtime_error("failed to write PNG: " + path);
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// Driver: walks the target through four states and renders each one.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <model.(obj|osgb|vsgb|ive)> <output_dir>"
                  << " [--voxels N] [--blast-radius R] [--blast-energy E]\n";
        return 2;
    }
    const std::string modelPath = argv[1];
    const std::string outDirArg = argv[2];
    int maxDimension = 64;
    double blastRadius = 0.0;   // 0 -> derive from energy
    double blastEnergy = 900.0; // Joules (≈ 1 kg TNT in kJ / 4.184)
    for (int i = 3; i < argc; ++i) {
        const std::string flag = argv[i];
        auto need = [&](const char* name) -> double {
            if (i + 1 >= argc) { std::cerr << "missing value for " << name << "\n"; std::exit(2); }
            return std::stod(argv[++i]);
        };
        if (flag == "--voxels") maxDimension = int(need("--voxels"));
        else if (flag == "--blast-radius") blastRadius = need("--blast-radius");
        else if (flag == "--blast-energy") blastEnergy = need("--blast-energy");
        else { std::cerr << "unknown flag: " << flag << "\n"; return 2; }
    }

    vsg::Logger::instance()->level = vsg::Logger::LOGGER_WARN;
    fs::create_directories(outDirArg);
    const fs::path outDir = fs::absolute(outDirArg);
    auto log = [&](const std::string& m) { std::cout << "[voxel_target_sim] " << m << "\n"; };

    try {
        log("loading " + modelPath);
        VoxelModelConfig voxelConfig;
        voxelConfig.maxDimension = maxDimension;
        const auto model = voxelizeFile(modelPath, voxelConfig);
        if (model.occupiedCount == 0) throw std::runtime_error("voxelization produced no cells");
        log("voxelized " + std::to_string(model.nx) + "x" + std::to_string(model.ny) + "x"
            + std::to_string(model.nz) + ", occupied=" + std::to_string(model.occupiedCount)
            + ", cell=" + std::to_string(model.cellSize) + " m");

        VoxelTerrainPhysics physics(model.terrain);
        if (physics.voxelCount() != model.occupiedCount)
            throw std::runtime_error("physics state did not adopt the voxel model");
        log("physics bound: " + std::to_string(physics.voxelCount()) + " voxels");

        const vsg::dbox bounds = physics.bounds();
        const dvec3 centre{(bounds.min.x+bounds.max.x)*.5,
                           (bounds.min.y+bounds.max.y)*.5,
                           (bounds.min.z+bounds.max.z)*.5};
        const double radius = vsg::length(bounds.max - bounds.min) * 0.5;

        auto makeViews = [&](double scale) {
            struct V { std::string name; Camera cam; };
            return std::vector<V>{
                {"iso",   orbitCamera(centre, radius*scale, 45., 32., 1.6)},
                {"front", orbitCamera(centre, radius*scale,  0., 10., 1.6)},
                {"top",   orbitCamera(centre, radius*scale, 45., 88., 1.6)},
            };
        };
        auto renderAll = [&](const std::string& label,
                             const std::vector<std::pair<vsg::dmat4,vsg::dvec3>>* debrisBoxes = nullptr) {
            for (const auto& v : makeViews(1.55)) {
                const auto file = (outDir / (label+"-"+v.name+".png")).string();
                const auto s = renderView(physics, v.cam, file, 800, 600, debrisBoxes);
                log("  "+label+"/"+v.name+" hits="+std::to_string(s.hitRays)
                    +"/"+std::to_string(s.primaryRays));
            }
        };

        // State A: intact
        renderAll("01-intact");

        // Collision: downward ray from above must hit the roof
        const dvec3 rayOrigin{centre.x, bounds.max.y + radius, centre.z};
        dvec3 hitPoint, hitNormal;
        if (!physics.intersectLocal(rayOrigin, {0,-1,0}, radius*4., hitPoint, hitNormal))
            throw std::runtime_error("collision ray missed the voxel target");
        log("collision hit at y=" + std::to_string(hitPoint.y));

        // State B: explosion damage. Two blasts: a large cratering charge on
        // the roof and a breaching charge on a side wall, so the damage is
        // clearly visible from iso/front/top viewpoints.
        VoxelTerrainPhysics::DamageEvent roofBlast;
        roofBlast.point = hitPoint;
        roofBlast.velocity = {0, -220., 0};
        roofBlast.energy = blastEnergy;
        roofBlast.radius = blastRadius > 0 ? blastRadius : radius * 0.42;
        const std::size_t destroyed = physics.damage(roofBlast);
        VoxelTerrainPhysics::DamageEvent wallBlast;
        wallBlast.point = dvec3{bounds.min.x, centre.y, centre.z};
        wallBlast.velocity = {1, 0, 0};
        wallBlast.energy = blastEnergy;
        wallBlast.radius = roofBlast.radius;
        const std::size_t destroyed2 = physics.damage(wallBlast);
        log("blasts destroyed " + std::to_string(destroyed + destroyed2)
            + " voxels (roof=" + std::to_string(destroyed) + " wall=" + std::to_string(destroyed2) + ")");
        if (!destroyed && !destroyed2) throw std::runtime_error("blasts removed no voxels");
        renderAll("02-damaged");

        // State C: structural collapse
        const std::size_t moved = physics.collapse();
        log("collapse moved " + std::to_string(moved) + " voxels");
        auto committed = physics.commit();
        log("committed rev=" + std::to_string(committed->revision)
            + " tris=" + std::to_string(committed->indices().size()/3));
        renderAll("03-collapsed");

        // State D: rigid-body debris via Bullet
        const auto clusters = physics.extractClusters(8);
        log("clusters=" + std::to_string(clusters.size()) + " (largest="
            + std::to_string(clusters.empty()?0:clusters.front().count) + ")");
        PhysicsEngine bullet;
        bullet.setGravity({0,-9.81,0});
        bullet.addGroundPlane(0.);
        VoxelTerrainDebris debris(bullet, physics);
        const std::size_t spawned = debris.spawn(clusters, 8);
        log("debris spawned=" + std::to_string(spawned));
        std::vector<std::pair<vsg::dmat4,vsg::dvec3>> debrisBoxes;
        if (spawned) {
            const auto t0 = debris.transforms();
            for (int i=0;i<12;++i) debris.step(1./30.);
            const auto t1 = debris.transforms();
            if (!t1.empty()) log("debris sample y "
                + std::to_string(t0[0][3][1])+" -> "+std::to_string(t1[0][3][1]));
            // Frame the debris boxes: transform + half extents per fragment.
            // VoxelTerrainDebris does not expose per-fragment extents on the
            // live API, so reuse the cluster half extents of the spawned set.
            for (std::size_t k = 0; k < t1.size() && k < clusters.size(); ++k)
                debrisBoxes.push_back({t1[k], clusters[k].halfExtents});
        }
        renderAll("04-debris", &debrisBoxes);

        log("final voxels=" + std::to_string(physics.voxelCount())
            + " destroyed=" + std::to_string(physics.destroyedCount()));
        log("output: " + outDir.string());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[voxel_target_sim] FAIL: " << e.what() << "\n";
        return 1;
    }
}
