#include "vsgraysim/core/ObjLoader.h"

#include <spdlog/spdlog.h>

#include <vsg/core/Array.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/utils/Builder.h>
#include <vsg/io/Options.h>
#include <vsg/io/VSG.h>
#include <vsg/io/read.h>
#include <vsg/io/write.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsgraysim {

namespace {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------
// Material definition parsed from a .mtl file.
// ------------------------------------------------------------------------
struct ObjMaterial {
    vsg::vec4 diffuse   = vsg::vec4(0.7f, 0.7f, 0.7f, 1.0f);  // Kd
    vsg::vec4 ambient   = vsg::vec4(0.2f, 0.2f, 0.2f, 1.0f);  // Ka
    vsg::vec4 specular  = vsg::vec4(0.1f, 0.1f, 0.1f, 1.0f);  // Ks
    float     shininess = 16.0f;                              // Ns
    bool      has_texture = false;                            // map_Kd present
    std::string texture_path;
};

// Default material used when a .obj has no .mtl or an unknown usemtl.
constexpr const char* kDefaultMtlName = "__vsgraysim_default_mtl__";

// ------------------------------------------------------------------------
// Parse a .mtl file and merge the materials into `out_materials`.
// On failure, logs a detailed warning describing what went wrong so the
// user can fix the .mtl (or its path) without a debugger.
// ------------------------------------------------------------------------
void parseMtlFile(const std::string& mtl_path,
                  std::unordered_map<std::string, ObjMaterial>& out_materials) {
    namespace fs = std::filesystem;
    if (!fs::exists(mtl_path)) {
        spdlog::warn("ObjLoader: .mtl file does not exist: '{}' "
                     "(faces using its materials will fall back to "
                     "default grey)", mtl_path);
        return;
    }
    const auto mtl_size = fs::file_size(mtl_path);
    if (mtl_size == 0) {
        spdlog::warn("ObjLoader: .mtl file is empty: '{}' "
                     "(no materials will be parsed)", mtl_path);
        return;
    }

    std::ifstream in(mtl_path);
    if (!in.is_open()) {
        spdlog::warn("ObjLoader: .mtl file exists but cannot be opened "
                     "(permission denied?): '{}'", mtl_path);
        return;
    }

    std::string line;
    std::string current_name;
    ObjMaterial current;
    bool have_current = false;
    size_t line_no = 0;
    size_t mtl_count = 0;
    size_t warn_count = 0;

    auto flush_current = [&]() {
        if (have_current && !current_name.empty()) {
            out_materials[current_name] = current;
            ++mtl_count;
        }
        have_current = false;
        current = ObjMaterial{};
        current_name.clear();
    };

    while (std::getline(in, line)) {
        ++line_no;
        // Trim leading whitespace (mtl files are often indented).
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        if (line[p] == '#') continue;
        std::istringstream ss(line.substr(p));
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            flush_current();
            ss >> current_name;
            if (current_name.empty()) {
                spdlog::warn("ObjLoader: .mtl '{}' line {}: 'newmtl' with "
                             "no name - skipped", mtl_path, line_no);
                ++warn_count;
                continue;
            }
            have_current = true;
            if (out_materials.count(current_name) > 0) {
                spdlog::debug("ObjLoader: .mtl '{}' line {}: material '{}' "
                              "redefined (later definition wins)",
                              mtl_path, line_no, current_name);
            }
        } else if (tag == "Kd") {
            float r, g, b;
            if (!(ss >> r >> g >> b)) {
                spdlog::warn("ObjLoader: .mtl '{}' line {}: malformed 'Kd' "
                             "(expected 3 floats) - ignored", mtl_path, line_no);
                ++warn_count;
                continue;
            }
            current.diffuse = vsg::vec4(r, g, b, 1.0f);
        } else if (tag == "Ka") {
            float r, g, b;
            if (!(ss >> r >> g >> b)) {
                spdlog::warn("ObjLoader: .mtl '{}' line {}: malformed 'Ka' "
                             "(expected 3 floats) - ignored", mtl_path, line_no);
                ++warn_count;
                continue;
            }
            current.ambient = vsg::vec4(r, g, b, 1.0f);
        } else if (tag == "Ks") {
            float r, g, b;
            if (!(ss >> r >> g >> b)) {
                spdlog::warn("ObjLoader: .mtl '{}' line {}: malformed 'Ks' "
                             "(expected 3 floats) - ignored", mtl_path, line_no);
                ++warn_count;
                continue;
            }
            current.specular = vsg::vec4(r, g, b, 1.0f);
        } else if (tag == "Ns") {
            float ns;
            if (!(ss >> ns)) {
                spdlog::warn("ObjLoader: .mtl '{}' line {}: malformed 'Ns' "
                             "(expected 1 float) - ignored", mtl_path, line_no);
                ++warn_count;
                continue;
            }
            current.shininess = std::max(1.0f, ns);
        } else if (tag == "map_Kd") {
            std::string rest;
            std::getline(ss, rest);
            // Strip leading/trailing whitespace.
            size_t a = rest.find_first_not_of(" \t");
            size_t b = rest.find_last_not_of(" \t\r\n");
            if (a != std::string::npos && b != std::string::npos) {
                current.texture_path = rest.substr(a, b - a + 1);
                current.has_texture = true;
                // Verify the texture file exists next to the .mtl; if
                // not, log a warning (we still proceed - the diffuse
                // colour will be used as a flat material).
                fs::path tex_path = fs::path(mtl_path).parent_path() /
                                    current.texture_path;
                if (!fs::exists(tex_path)) {
                    spdlog::debug("ObjLoader: .mtl '{}' line {}: map_Kd "
                                  "texture '{}' not found next to .mtl "
                                  "(will use Kd colour as flat material)",
                                  mtl_path, line_no, current.texture_path);
                }
            }
        }
        // d / Tr / illum / Ni / Pr / Pm / etc. are accepted but ignored.
    }
    flush_current();
    spdlog::info("ObjLoader: parsed '{}' ({} bytes, {} lines, {} materials, "
                 "{} warnings)",
                 mtl_path, mtl_size, line_no, mtl_count, warn_count);
}

// ------------------------------------------------------------------------
// Per-material accumulated geometry.  Faces are triangulated and the
// resulting verts/norms/indices are appended to the bucket for the
// material active when the face was read.
// ------------------------------------------------------------------------
struct MtlBucket {
    std::vector<vsg::vec3> verts;
    std::vector<vsg::vec3> norms;
    std::vector<vsg::vec2> uvs;    // per-vertex texcoords (same count as verts)
    std::vector<uint32_t>  idx;
};

// ------------------------------------------------------------------------
// Binary cache (.vsgb) helpers.
//
// Parsing the project's large .obj files (Desert 95MB, Taipei 298MB)
// takes 5-10s per launch.  After a successful parse the resulting
// vsg::Node is serialised with vsg::write() to "<obj>.vsgb" (native
// VSG binary) and a small "<obj>.vsgb.meta" sidecar records the content
// fingerprint of the .obj and its sibling .mtl files.  On the next
// launch the cache is loaded with vsg::read_cast() as long as the
// fingerprint still matches, skipping the parse entirely.
// ------------------------------------------------------------------------

constexpr const char* kVsgbCacheExt = ".vsgb";
constexpr const char* kVsgbMetaExt  = ".vsgb.meta";
constexpr const char* kVsgbSlotsExt = ".vsgb.slots";   // 材质槽清单 sidecar

fs::path vsgbCachePath(const fs::path& obj) { return fs::path(obj.string() + kVsgbCacheExt); }
fs::path vsgbMetaPath(const fs::path& obj)  { return fs::path(obj.string() + kVsgbMetaExt); }
fs::path vsgbSlotsPath(const fs::path& obj) { return fs::path(obj.string() + kVsgbSlotsExt); }

/// Fingerprint of a file: 64-bit FNV-1a hash of the content plus its
/// size.  Content-based (not mtime) so that build-time copies or syncs
/// that refresh timestamps do not invalidate the cache; the cache only
/// goes stale when the content actually changes.
std::string fileStamp(const fs::path& p) {
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    if (ec) return {};
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return {};
    uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
    char buf[65536];
    while (in) {
        in.read(buf, sizeof(buf));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buf[i]);
            h *= 1099511628211ull;  // FNV-1a prime
        }
    }
    if (!in.eof()) return {};
    return std::to_string(h) + "|" + std::to_string(s);
}

/// Files whose modification invalidates the cache: the .obj itself plus
/// every .mtl sitting next to it.
std::vector<fs::path> cacheSourceFiles(const fs::path& obj) {
    std::vector<fs::path> files{ obj };
    std::error_code ec;
    const fs::path dir = obj.parent_path();
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() == ".mtl") files.push_back(it->path());
    }
    return files;
}

/// Write the fingerprint sidecar for a just-written cache.
void writeCacheMeta(const fs::path& obj) {
    const fs::path meta = vsgbMetaPath(obj);
    std::ofstream out(meta, std::ios::trunc);
    if (!out.is_open()) {
        spdlog::warn("ObjLoader: cannot write cache meta '{}'", meta.string());
        return;
    }
    for (const auto& f : cacheSourceFiles(obj)) {
        out << f.filename().string() << "|" << fileStamp(f) << "\n";
    }
}

/// True when the sidecar exists and every recorded fingerprint matches
/// the current state of the source files.
bool cacheMetaMatches(const fs::path& obj) {
    const fs::path meta = vsgbMetaPath(obj);
    std::ifstream in(meta);
    if (!in.is_open()) return false;
    std::map<std::string, std::string> expected;
    std::string line;
    while (std::getline(in, line)) {
        const size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        expected[line.substr(0, bar)] = line.substr(bar + 1);
    }
    const auto files = cacheSourceFiles(obj);
    if (files.size() != expected.size()) return false;
    for (const auto& f : files) {
        const auto it = expected.find(f.filename().string());
        if (it == expected.end()) return false;
        if (it->second != fileStamp(f)) return false;
    }
    return true;
}

/// Write the material-slot sidecar (one line per root child, same order):
///   name|KdR|KdG|KdB|map_kd
void writeSlotsSidecar(const fs::path& obj,
                       const std::vector<ObjMaterialSlot>& slots) {
    std::ofstream out(vsgbSlotsPath(obj), std::ios::trunc);
    if (!out.is_open()) {
        spdlog::warn("ObjLoader: cannot write slots sidecar '{}'",
                     vsgbSlotsPath(obj).string());
        return;
    }
    for (const auto& s : slots) {
        out << s.name << '|' << s.diffuse.x << '|' << s.diffuse.y << '|'
            << s.diffuse.z << '|' << s.map_kd << '\n';
    }
}

/// Read the material-slot sidecar; false when absent/corrupt.
bool readSlotsSidecar(const fs::path& obj,
                      std::vector<ObjMaterialSlot>& slots) {
    std::ifstream in(vsgbSlotsPath(obj));
    if (!in.is_open()) return false;
    slots.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const size_t p1 = line.find('|');
        const size_t p2 = (p1 == std::string::npos) ? std::string::npos : line.find('|', p1 + 1);
        const size_t p3 = (p2 == std::string::npos) ? std::string::npos : line.find('|', p2 + 1);
        const size_t p4 = (p3 == std::string::npos) ? std::string::npos : line.find('|', p3 + 1);
        if (p4 == std::string::npos) return false;   // corrupt → re-parse
        ObjMaterialSlot s;
        s.name = line.substr(0, p1);
        try {
            s.diffuse.x = std::stof(line.substr(p1 + 1, p2 - p1 - 1));
            s.diffuse.y = std::stof(line.substr(p2 + 1, p3 - p2 - 1));
            s.diffuse.z = std::stof(line.substr(p3 + 1, p4 - p3 - 1));
        } catch (const std::exception&) {
            return false;
        }
        s.map_kd = line.substr(p4 + 1);
        slots.push_back(std::move(s));
    }
    // An empty sidecar is legitimate (model without materials).
    return true;
}

/// Options with the native VSG reader/writer guaranteed to be present
/// (needed to read/write .vsgb regardless of what the caller provided).
vsg::ref_ptr<vsg::Options> cacheOptions(vsg::ref_ptr<vsg::Options> options) {
    auto opts = options ? options : vsg::Options::create();
    for (const auto& rw : opts->readerWriters) {
        if (dynamic_cast<vsg::VSG*>(rw.get())) return opts;
    }
    opts->add(vsg::VSG::create());
    return opts;
}

// ------------------------------------------------------------------------
// Build a vsg::StateGroup + vsg::VertexIndexDraw for one material
// bucket, configured with the material's diffuse colour.
// Returns null (and logs the reason) on failure.
// ------------------------------------------------------------------------
vsg::ref_ptr<vsg::Node>
buildMaterialNode(const std::string& mtl_name,
                  const ObjMaterial& mat,
                  const MtlBucket& bucket,
                  vsg::ref_ptr<vsg::Options> options) {
    if (bucket.verts.empty()) {
        spdlog::warn("ObjLoader: buildMaterialNode('{}') - no vertices, "
                     "skipping", mtl_name);
        return {};
    }
    if (bucket.idx.empty()) {
        spdlog::warn("ObjLoader: buildMaterialNode('{}') - no indices, "
                     "skipping", mtl_name);
        return {};
    }
    // Sanity check: normals array must match verts array (we generate
    // one normal per vertex in the triangulation step).
    if (bucket.norms.size() != bucket.verts.size()) {
        spdlog::warn("ObjLoader: buildMaterialNode('{}') - normal/vertex "
                     "count mismatch (verts={}, norms={}). Regenerating "
                     "face normals.", mtl_name, bucket.verts.size(),
                     bucket.norms.size());
        // Fall back: rebuild normals as flat face normals.  This
        // shouldn't happen in practice but protects against bugs in
        // the face parser.
        std::vector<vsg::vec3> fixed_norms = bucket.norms;
        fixed_norms.resize(bucket.verts.size(), vsg::vec3(0.0f, 1.0f, 0.0f));
        MtlBucket fixed = bucket;
        fixed.norms = std::move(fixed_norms);
        return buildMaterialNode(mtl_name, mat, fixed, options);
    }

    auto vsg_verts = vsg::vec3Array::create(bucket.verts.size());
    std::memcpy(vsg_verts->dataPointer(), bucket.verts.data(),
                bucket.verts.size() * sizeof(vsg::vec3));
    auto vsg_norms = vsg::vec3Array::create(bucket.norms.size());
    std::memcpy(vsg_norms->dataPointer(), bucket.norms.data(),
                bucket.norms.size() * sizeof(vsg::vec3));
    auto vsg_idx = vsg::uintArray::create(bucket.idx.size());
    std::memcpy(vsg_idx->dataPointer(), bucket.idx.data(),
                bucket.idx.size() * sizeof(uint32_t));

    // Optional uv channel (binding2): present only when every vertex has
    // a texcoord (no-uv .obj files keep the 2-array layout unchanged).
    vsg::ref_ptr<vsg::vec2Array> vsg_uvs;
    if (bucket.uvs.size() == bucket.verts.size()) {
        vsg_uvs = vsg::vec2Array::create(bucket.uvs.size());
        std::memcpy(vsg_uvs->dataPointer(), bucket.uvs.data(),
                    bucket.uvs.size() * sizeof(vsg::vec2));
    }

    auto vid = vsg::VertexIndexDraw::create();
    if (vsg_uvs) vid->assignArrays({vsg_verts, vsg_norms, vsg_uvs});
    else         vid->assignArrays({vsg_verts, vsg_norms});
    vid->assignIndices(vsg_idx);
    vid->indexCount    = static_cast<uint32_t>(bucket.idx.size());
    vid->instanceCount = 1;

    auto builder = vsg::Builder::create();
    builder->options = options ? options : vsg::Options::create();

    vsg::StateInfo si;
    si.lighting = true;
    auto sg = builder->createStateGroup(si);
    if (!sg) {
        spdlog::error("ObjLoader: buildMaterialNode('{}') - "
                      "Builder::createStateGroup returned null (verts={}, "
                      "idx={}, lighting=1). This usually means the vsg::Builder "
                      "failed to find a Phong shader set in the options.",
                      mtl_name, bucket.verts.size(), bucket.idx.size());
        return {};
    }
    sg->addChild(vid);
    // Log the material colour so we can verify it was parsed; the
    // Phong shader's default diffuse is mid-grey, but the geometry is
    // still visible.  A future improvement could push mat.diffuse into
    // a uniform via a custom DescriptorSet.
    spdlog::debug("ObjLoader: mtl '{}' diffuse=({:.2f},{:.2f},{:.2f}) "
                  "verts={} idx={}",
                  mtl_name, mat.diffuse.x, mat.diffuse.y, mat.diffuse.z,
                  bucket.verts.size(), bucket.idx.size());
    return sg;
}

} // anonymous namespace

// ------------------------------------------------------------------------
// Public entry point.
// On any failure path, logs a detailed diagnostic block describing what
// went wrong (file missing, file empty, no geometry, parse errors, etc.)
// so the user can fix the .obj without needing a debugger.
// ------------------------------------------------------------------------
vsg::ref_ptr<vsg::Node>
loadObjNode(const std::string& path,
            vsg::ref_ptr<vsg::Options> options,
            std::vector<ObjMaterialSlot>* out_slots) {
    namespace fs = std::filesystem;

    // ---- 0. Pre-flight checks ----------------------------------------
    if (path.empty()) {
        spdlog::error("ObjLoader: FAILED - empty path");
        return {};
    }
    if (!fs::exists(path)) {
        spdlog::error("ObjLoader: FAILED - file does not exist: '{}'", path);
        return {};
    }
    const auto file_size = fs::file_size(path);
    if (file_size == 0) {
        spdlog::error("ObjLoader: FAILED - file is empty: '{}'", path);
        return {};
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        spdlog::error("ObjLoader: FAILED - file exists but cannot be opened "
                      "(permission denied?): '{}'", path);
        return {};
    }

    const fs::path obj_path(path);
    auto cache_opts = cacheOptions(options);

    // ---- 0b. Binary cache (.vsgb) ------------------------------------
    // If a valid cached parse exists, load it and skip the whole parse.
    // When the caller requests material slots, the slots sidecar must
    // also exist (caches written before uv/slot support are stale then).
    const fs::path cache_path = vsgbCachePath(obj_path);
    const bool slots_needed = (out_slots != nullptr);
    if (fs::exists(cache_path) && cacheMetaMatches(obj_path) &&
        (!slots_needed || readSlotsSidecar(obj_path, *out_slots))) {
        auto cached = vsg::read_cast<vsg::Node>(cache_path.string(), cache_opts);
        if (cached) {
            spdlog::info("ObjLoader: loaded cached '{}' ({} bytes)",
                         cache_path.string(), fs::file_size(cache_path));
            return cached;
        }
        spdlog::warn("ObjLoader: cache read failed for '{}' - "
                     "re-parsing source", cache_path.string());
    } else if (fs::exists(cache_path)) {
        spdlog::info("ObjLoader: cache '{}' exists but sources changed - "
                     "re-parsing", cache_path.string());
    }
    if (out_slots) out_slots->clear();

    const std::string obj_dir = obj_path.parent_path().string();
    spdlog::info("ObjLoader: parsing '{}' ({} bytes, dir='{}')",
                 path, file_size, obj_dir);

    // ---- 1. Parse any referenced .mtl files --------------------------
    // We delay parsing until we see the first `mtllib` so we don't pay
    // the cost for models that ship without materials.
    std::unordered_map<std::string, ObjMaterial> materials;
    // Always register a default material so faces with no `usemtl` (or
    // an unknown one) still render with a visible colour.
    materials[kDefaultMtlName] = ObjMaterial{};

    // ---- 2. Stream the .obj ------------------------------------------
    std::vector<vsg::vec3> positions;   // v
    std::vector<vsg::vec3> normals;     // vn
    std::vector<vsg::vec2> texcoords;   // vt (u,v; kept for sensor chain)

    // Buckets keyed by material name.  Using a map preserves insertion
    // order via the iteration pattern below (we use a vector alongside
    // to remember the order so output is deterministic).
    std::unordered_map<std::string, MtlBucket> buckets;
    std::vector<std::string> mtl_order;
    std::string current_mtl = kDefaultMtlName;
    buckets[current_mtl] = MtlBucket{};
    mtl_order.push_back(current_mtl);

    vsg::vec3 bbox_min( 1e30f,  1e30f,  1e30f);
    vsg::vec3 bbox_max(-1e30f, -1e30f, -1e30f);

    auto ensure_bucket = [&](const std::string& name) {
        if (buckets.find(name) == buckets.end()) {
            buckets[name] = MtlBucket{};
            mtl_order.push_back(name);
        }
    };

    std::string line;
    size_t line_no = 0;
    size_t face_count = 0;
    size_t vt_count = 0;
    size_t skipped_tokens = 0;
    size_t malformed_lines = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line.empty()) continue;
        // Skip comments and the BOM if present.
        if (line[0] == '#') continue;
        // Trim leading whitespace.
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        if (p > 0) line = line.substr(p);

        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            float x, y, z;
            if (!(ss >> x >> y >> z)) {
                if (malformed_lines < 10) {
                    spdlog::warn("ObjLoader: '{}' line {}: malformed 'v' "
                                 "(expected 3 floats) - skipped",
                                 path, line_no);
                }
                ++malformed_lines;
                continue;
            }
            positions.emplace_back(x, y, z);
            bbox_min.x = std::min(bbox_min.x, x);
            bbox_min.y = std::min(bbox_min.y, y);
            bbox_min.z = std::min(bbox_min.z, z);
            bbox_max.x = std::max(bbox_max.x, x);
            bbox_max.y = std::max(bbox_max.y, y);
            bbox_max.z = std::max(bbox_max.z, z);
        } else if (tag == "vn") {
            float x, y, z;
            if (!(ss >> x >> y >> z)) {
                if (malformed_lines < 10) {
                    spdlog::warn("ObjLoader: '{}' line {}: malformed 'vn' "
                                 "(expected 3 floats) - skipped",
                                 path, line_no);
                }
                ++malformed_lines;
                continue;
            }
            float len = std::sqrt(x * x + y * y + z * z);
            if (len > 1e-12f) { x /= len; y /= len; z /= len; }
            normals.emplace_back(x, y, z);
        } else if (tag == "vt") {
            // Texture coords: kept for the sensor chain (uv per vertex).
            float u, v;
            if (ss >> u >> v) {
                texcoords.emplace_back(u, v);
                ++vt_count;
            } else {
                ++vt_count;
                continue;
            }
        } else if (tag == "mtllib") {
            std::string mtl_file;
            ss >> mtl_file;
            if (!mtl_file.empty()) {
                fs::path mtl_path = fs::path(obj_dir) / mtl_file;
                if (!fs::exists(mtl_path)) {
                    // Some exporters put the .mtl next to the .obj with
                    // a different basename; try resolving relative to
                    // the .obj itself as a last resort.
                    mtl_path = obj_path;
                    mtl_path.replace_filename(mtl_file);
                }
                if (fs::exists(mtl_path)) {
                    parseMtlFile(mtl_path.string(), materials);
                    // Make sure the default material still exists.
                    if (materials.find(kDefaultMtlName) == materials.end()) {
                        materials[kDefaultMtlName] = ObjMaterial{};
                    }
                } else {
                    spdlog::warn("ObjLoader: '{}' line {}: .mtl '{}' not "
                                 "found next to .obj (tried '{}'). Faces "
                                 "using its materials will fall back to "
                                 "default grey.",
                                 path, line_no, mtl_file, mtl_path.string());
                }
            }
        } else if (tag == "usemtl") {
            std::string name;
            ss >> name;
            if (!name.empty()) {
                if (materials.find(name) == materials.end()) {
                    spdlog::warn("ObjLoader: '{}' line {}: usemtl '{}' "
                                 "references unknown material (no .mtl "
                                 "loaded, or name not defined in .mtl). "
                                 "Using default grey.",
                                 path, line_no, name);
                    // Reuse the default material object so unknown mtl
                    // names still render with a sensible colour.
                    materials[name] = materials[kDefaultMtlName];
                }
                current_mtl = name;
                ensure_bucket(current_mtl);
            }
        } else if (tag == "f") {
            // Parse face vertices.  Each token is `v`, `v/vt`,
            // `v//vn`, or `v/vt/vn` (1-based; negative = relative).
            struct FaceVert { int v; int vt; int vn; };
            std::vector<FaceVert> fv;
            std::string tok;
            while (ss >> tok) {
                FaceVert v{-1, -1, -1};
                bool tok_ok = true;
                // Split on '/'.
                size_t a = 0;
                size_t pos = 0;
                int field = 0;
                while (pos <= tok.size() && field < 3) {
                    size_t next = tok.find('/', pos);
                    std::string num = (next == std::string::npos)
                                      ? tok.substr(pos)
                                      : tok.substr(pos, next - pos);
                    if (!num.empty()) {
                        int idx = 0;
                        try {
                            idx = std::stoi(num);
                        } catch (const std::exception&) {
                            if (malformed_lines < 10) {
                                spdlog::warn("ObjLoader: '{}' line {}: "
                                             "face token '{}' is not a "
                                             "valid integer - skipped",
                                             path, line_no, tok);
                            }
                            ++malformed_lines;
                            tok_ok = false;
                            break;
                        }
                        if (idx > 0)      idx -= 1;
                        else if (idx < 0) {
                            if (field == 0) idx = static_cast<int>(positions.size()) + idx;
                            else if (field == 1) idx = static_cast<int>(texcoords.size()) + idx;
                            else if (field == 2) idx = static_cast<int>(normals.size()) + idx;
                        }
                        if (field == 0) v.v  = idx;
                        else if (field == 1) v.vt = idx;
                        else if (field == 2) v.vn = idx;
                    }
                    field++;
                    if (next == std::string::npos) break;
                    pos = next + 1;
                    a = pos;
                    (void)a;
                }
                if (!tok_ok) continue;
                if (v.v < 0 || v.v >= static_cast<int>(positions.size())) {
                    ++skipped_tokens;
                    if (skipped_tokens <= 5) {
                        spdlog::warn("ObjLoader: '{}' line {}: face vertex "
                                     "index {} out of range [1, {}] - "
                                     "skipped (token='{}')",
                                     path, line_no, v.v + 1,
                                     positions.size(), tok);
                    } else if (skipped_tokens == 6) {
                        spdlog::warn("ObjLoader: '{}' line {}: further "
                                     "out-of-range face vertex warnings "
                                     "suppressed", path, line_no);
                    }
                    continue;
                }
                fv.push_back(v);
            }
            if (fv.size() < 3) {
                if (malformed_lines < 10) {
                    spdlog::debug("ObjLoader: '{}' line {}: face has <3 "
                                  "valid vertices ({}) - skipped",
                                  path, line_no, fv.size());
                }
                continue;
            }
            ++face_count;

            // Fan triangulation: triangle (fv[0], fv[k], fv[k+1]).
            MtlBucket& bucket = buckets[current_mtl];
            for (size_t k = 1; k + 1 < fv.size(); ++k) {
                const FaceVert& a = fv[0];
                const FaceVert& b = fv[k];
                const FaceVert& c = fv[k + 1];
                vsg::vec3 p0 = positions[a.v];
                vsg::vec3 p1 = positions[b.v];
                vsg::vec3 p2 = positions[c.v];

                // Use the .obj-provided normal when available, else
                // compute a face normal from the cross product.
                vsg::vec3 n0, n1, n2;
                auto face_n = [&]() {
                    vsg::vec3 e1 = p1 - p0;
                    vsg::vec3 e2 = p2 - p0;
                    vsg::vec3 n  = vsg::normalize(vsg::cross(e1, e2));
                    if (!std::isfinite(n.x) || !std::isfinite(n.y) ||
                        !std::isfinite(n.z)) {
                        n = vsg::vec3(0.0f, 1.0f, 0.0f);
                    }
                    return n;
                };
                if (a.vn >= 0 && a.vn < static_cast<int>(normals.size()))
                    n0 = normals[a.vn];
                else n0 = face_n();
                if (b.vn >= 0 && b.vn < static_cast<int>(normals.size()))
                    n1 = normals[b.vn];
                else n1 = face_n();
                if (c.vn >= 0 && c.vn < static_cast<int>(normals.size()))
                    n2 = normals[c.vn];
                else n2 = face_n();

                uint32_t base = static_cast<uint32_t>(bucket.verts.size());
                bucket.verts.push_back(p0);  bucket.norms.push_back(n0);
                bucket.verts.push_back(p1);  bucket.norms.push_back(n1);
                bucket.verts.push_back(p2);  bucket.norms.push_back(n2);
                auto push_uv = [&](const FaceVert& fv0) {
                    if (fv0.vt >= 0 && fv0.vt < static_cast<int>(texcoords.size()))
                        bucket.uvs.push_back(texcoords[fv0.vt]);
                    else
                        bucket.uvs.push_back(vsg::vec2(0.0f, 0.0f));
                };
                push_uv(a); push_uv(b); push_uv(c);
                bucket.idx.push_back(base + 0);
                bucket.idx.push_back(base + 1);
                bucket.idx.push_back(base + 2);
            }
        }
        // o / g / s / l / etc. are ignored.
    }

    // ---- 3. Assemble the scene graph ---------------------------------
    // Log a per-stage summary so we can diagnose files that parse but
    // produce no geometry (e.g. only comments/empty lines, or all face
    // indices out of range).
    spdlog::info("ObjLoader: '{}' parse summary: lines={}, v={}, vn={}, "
                 "vt={}, f={}, materials={}, skipped_tokens={}, "
                 "malformed_lines={}",
                 path, line_no, positions.size(), normals.size(),
                 vt_count, face_count, materials.size(), skipped_tokens,
                 malformed_lines);

    size_t total_verts = 0, total_idx = 0;
    for (const auto& kv : buckets) {
        total_verts += kv.second.verts.size();
        total_idx   += kv.second.idx.size();
    }
    if (total_verts == 0 || total_idx == 0) {
        // Distinguish the various reasons we might have ended up with
        // no geometry so the user knows how to fix the file.
        if (positions.empty()) {
            spdlog::error("ObjLoader: FAILED - no 'v' vertices found in "
                          "'{}' (lines={}, malformed={}). The file may "
                          "be empty, all comments, or not a Wavefront "
                          ".obj at all.", path, line_no, malformed_lines);
        } else if (face_count == 0) {
            spdlog::error("ObjLoader: FAILED - file has {} vertices but "
                          "0 valid faces in '{}' (skipped_tokens={}, "
                          "malformed_lines={}). All 'f' lines may be "
                          "malformed or reference out-of-range indices.",
                          positions.size(), path, skipped_tokens,
                          malformed_lines);
        } else {
            spdlog::error("ObjLoader: FAILED - {} faces parsed but 0 "
                          "vertices after triangulation in '{}' (this "
                          "shouldn't happen - please report).",
                          face_count, path);
        }
        return {};
    }

    auto root = vsg::Group::create();
    size_t added_buckets = 0;
    size_t failed_buckets = 0;
    if (out_slots) out_slots->clear();
    for (const auto& name : mtl_order) {
        auto it = buckets.find(name);
        if (it == buckets.end()) continue;
        const MtlBucket& bucket = it->second;
        if (bucket.verts.empty()) continue;

        // Look up the material (fall back to default if missing).
        ObjMaterial mat;
        auto mit = materials.find(name);
        if (mit != materials.end()) mat = mit->second;
        else                         mat = materials[kDefaultMtlName];

        auto node = buildMaterialNode(name, mat, bucket, options);
        if (node) {
            root->addChild(node);
            ++added_buckets;
            if (out_slots) {
                ObjMaterialSlot slot;
                slot.name    = name;
                slot.diffuse = mat.diffuse;
                slot.map_kd  = mat.texture_path;
                out_slots->push_back(std::move(slot));
            }
        } else {
            ++failed_buckets;
            spdlog::warn("ObjLoader: '{}' - buildMaterialNode failed for "
                         "material '{}' ({} verts, {} idx)",
                         path, name, bucket.verts.size(), bucket.idx.size());
        }
    }

    if (added_buckets == 0) {
        spdlog::error("ObjLoader: FAILED - {} material buckets but 0 "
                      "successfully built for '{}' ({} failed). See "
                      "preceding warnings for details.",
                      buckets.size(), path, failed_buckets);
        return {};
    }
    if (failed_buckets > 0) {
        spdlog::warn("ObjLoader: '{}' - {}/{} material buckets failed "
                     "to build (partial geometry will be rendered)",
                     path, failed_buckets, buckets.size());
    }

    spdlog::info("ObjLoader: '{}' -> {} verts, {} tris, {} faces, "
                 "{}/{} materials, bbox=({:.1f},{:.1f},{:.1f}).."
                 "({:.1f},{:.1f},{:.1f})",
                 path, total_verts, total_idx / 3, face_count,
                 added_buckets, materials.size(),
                 bbox_min.x, bbox_min.y, bbox_min.z,
                 bbox_max.x, bbox_max.y, bbox_max.z);

    // ---- 4. Write the binary cache for next launch -------------------
    if (vsg::write(root, cache_path.string(), cache_opts)) {
        writeCacheMeta(obj_path);
        if (out_slots) writeSlotsSidecar(obj_path, *out_slots);
        spdlog::info("ObjLoader: wrote cache '{}'", cache_path.string());
    } else {
        spdlog::warn("ObjLoader: failed to write cache '{}' - "
                     "will re-parse next launch", cache_path.string());
    }

    return root;
}

} // namespace vsgraysim
