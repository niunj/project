#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <vsgraysim/earth/VoxelScenePackage.h>
#include <zlib.h>
namespace vsgraysim::earth {
namespace {
using json = nlohmann::json;
namespace fs = std::filesystem;
constexpr uint64_t assetLimit = 256ull * 1024 * 1024, totalLimit = 512ull * 1024 * 1024;
std::vector<uint8_t> bytes(const fs::path &file, uint64_t limit) {
    if (!fs::is_regular_file(file) || fs::file_size(file) > limit)
        throw std::invalid_argument("missing or oversized scene package resource: " + file.string());
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> out(fs::file_size(file));
    in.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
    if (!in)
        throw std::runtime_error("scene package read failed");
    return out;
}
void atomicWrite(const fs::path &target, const std::vector<uint8_t> &data) {
    std::string pattern = target.string() + ".pending.XXXXXX";
    const int fd = mkstemp(pattern.data());
    if (fd < 0)
        throw std::runtime_error("cannot create scene package temporary file");
    bool closed = false;
    try {
        size_t done = 0;
        while (done < data.size()) {
            const auto count = ::write(fd, data.data() + done, data.size() - done);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("scene package write failed");
            done += size_t(count);
        }
        closed = true;
        if (::close(fd) != 0)
            throw std::runtime_error("scene package close failed");
        fs::rename(pattern, target);
    } catch (...) {
        if (!closed)
            ::close(fd);
        ::unlink(pattern.c_str());
        throw;
    }
}
bool digestName(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
fs::path assetPath(const fs::path &manifest, const json &asset) {
    auto relative = fs::path(asset.at("uri").get<std::string>());
    if (relative.is_absolute())
        throw std::invalid_argument("scene asset must be package-relative");
    for (const auto &part : relative)
        if (part == "..")
            throw std::invalid_argument("scene asset path escapes package");
    const auto root = fs::weakly_canonical(manifest.parent_path());
    const auto file = fs::weakly_canonical(root / relative);
    const auto tail = file.lexically_relative(root);
    if (tail.empty() || *tail.begin() == "..")
        throw std::invalid_argument("scene asset symlink escapes package");
    return file;
}
void validateManifest(const json &m) {
    if (m.at("format") != VoxelScenePackage::format || m.at("version") != 1 || m.at("codec") != "cbor-zlib" ||
        m.at("checkpoint").at("format") != "voxel-scene-checkpoint-v1" ||
        m.at("topic").at("format") != "earth-scene-topic-v1" || m.at("coordinates") != "east-up-south-metres")
        throw std::invalid_argument("unsupported scene package version/codec/coordinates");
    if (!m.at("assets").is_object() || m.at("assets").size() > 4096 || !m.at("objects").is_array() ||
        m.at("objects").size() > 256)
        throw std::invalid_argument("invalid scene package index");
    uint64_t total = 0;
    for (const auto &item : m.at("assets").items()) {
        if (!digestName(item.key()))
            throw std::invalid_argument("invalid scene asset digest");
        for (const auto *key : {"stored_bytes", "expanded_bytes"}) {
            const auto &n = item.value().at(key);
            if (!n.is_number_integer() || n.get<double>() < 1 || n.get<double>() > assetLimit)
                throw std::invalid_argument("invalid scene asset length");
        }
        const auto size = item.value().at("expanded_bytes").get<uint64_t>();
        if (size > totalLimit - total)
            throw std::invalid_argument("scene package expansion budget exceeded");
        total += size;
        item.value().at("uri").get<std::string>();
    }
    auto reference = [&](const json &value) {
        const auto id = value.get<std::string>();
        if (!m.at("assets").contains(id))
            throw std::invalid_argument("scene package references missing asset");
    };
    std::set<std::string> ids;
    for (const auto &object : m.at("objects")) {
        if (!ids.insert(object.at("id").get<std::string>()).second)
            throw std::invalid_argument("duplicate package object ID");
        reference(object.at("state"));
        reference(object.at("volume"));
        if (object.contains("sensor_materials"))
            reference(object.at("sensor_materials"));
    }
    for (const auto *key : {"earth_patches", "surface_appearances"}) {
        if (!m.at(key).is_object() || m.at(key).size() > 256)
            throw std::invalid_argument("invalid package shared table");
        for (const auto &item : m.at(key).items())
            reference(item.value());
    }
}

} // namespace
std::string VoxelScenePackage::sha256(const std::vector<uint8_t> &input) {
    // Optional system OpenSSL 3 one-shot ABI; no mandatory SDK dependency.
    // Always provide a caller-owned digest buffer (the API's static buffer is not thread-safe).
    using NativeHash = unsigned char *(*)(const unsigned char *, size_t, unsigned char *);
    static NativeHash native = []() {
        void *module = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
        return module ? reinterpret_cast<NativeHash>(dlsym(module, "SHA256")) : nullptr;
    }();
    if (native && !std::getenv("VSGRAYSIM_PORTABLE_SHA256")) {
        std::array<unsigned char, 32> digest{};
        if (native(input.empty() ? reinterpret_cast<const unsigned char *>("") : input.data(), input.size(),
                   digest.data())) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result(64, '0');
            for (size_t i = 0; i < digest.size(); ++i) {
                result[2 * i] = digits[digest[i] >> 4];
                result[2 * i + 1] = digits[digest[i] & 15];
            }
            return result;
        }
    }

    static constexpr uint32_t k[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotate = [](uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
    const size_t blocks = (input.size() + 9 + 63) / 64;
    const uint64_t bitSize = uint64_t(input.size()) * 8;
    for (size_t block = 0; block < blocks; ++block) {
        uint32_t w[64]{};
        for (size_t i = 0; i < 64; ++i) {
            const size_t at = block * 64 + i;
            uint8_t b = at < input.size() ? input[at] : at == input.size() ? 0x80 : 0;
            if (at >= blocks * 64 - 8)
                b = uint8_t(bitSize >> ((blocks * 64 - 1 - at) * 8));
            w[i / 4] |= uint32_t(b) << (24 - 8 * (i % 4));
        }
        for (int i = 16; i < 64; ++i) {
            const auto a = w[i - 15], b = w[i - 2];
            w[i] = w[i - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) + w[i - 7] +
                   (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], v = h[7];
        for (int i = 0; i < 64; ++i) {
            const auto t =
                v + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ ((~e) & g)) + k[i] + w[i];
            const auto u = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            v = g;
            g = f;
            f = e;
            e = d + t;
            d = c;
            c = b;
            b = a;
            a = t + u;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += v;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto value : h)
        out << std::setw(8) << value;
    return out.str();
}
bool VoxelScenePackage::extension(const std::string &name) {
    auto ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".vsimscene";
}
json VoxelScenePackage::inspect(const std::string &path) {
    const auto data = bytes(path, 8 * 1024 * 1024);
    auto manifest = json::parse(data);
    validateManifest(manifest);
    return manifest;
}
std::string VoxelScenePackage::resolveResource(const std::string &uri, const std::string &manifest) {
    if (uri.rfind("repo://", 0) == 0) {
        const auto relative = fs::path(uri.substr(7));
        if (relative.is_absolute())
            throw std::invalid_argument("invalid repository resource");
        for (const auto &part : relative)
            if (part == "..")
                throw std::invalid_argument("repository resource escapes root");
        std::vector<fs::path> roots{fs::current_path()};
        std::error_code error;
        auto root = fs::read_symlink("/proc/self/exe", error).parent_path();
        for (int i = 0; i < 8 && !root.empty(); ++i) {
            roots.push_back(root);
            root = root.parent_path();
        }
        for (const auto &base : roots)
            if (fs::is_regular_file(base / relative))
                return fs::weakly_canonical(base / relative).string();
        throw std::invalid_argument("missing repository scene resource: " + uri);
    }
    auto path = fs::path(uri);
    if (path.is_relative())
        path = fs::path(manifest).parent_path() / path;
    if (!fs::is_regular_file(path))
        throw std::invalid_argument("missing external scene resource: " + uri);
    return fs::weakly_canonical(path).string();
}
json VoxelScenePackage::checkpoint(const std::string &name) {
    const auto path = fs::absolute(name);
    const auto manifest = inspect(name);
    auto result = manifest.at("checkpoint");
    uint64_t total = 0;
    std::map<std::string, json> cache;
    std::map<std::string, size_t> references;
    for (const auto &entry : manifest.at("objects")) {
        ++references[entry.at("state")];
        ++references[entry.at("volume")];
        if (entry.contains("sensor_materials"))
            ++references[entry.at("sensor_materials")];
    }
    for (const auto *key : {"earth_patches", "surface_appearances"})
        for (const auto &entry : manifest.at(key).items())
            ++references[entry.value()];
    auto read = [&](const std::string &id) -> json {
        if (!digestName(id))
            throw std::invalid_argument("invalid scene asset digest");
        if (auto found = cache.find(id); found != cache.end())
            return found->second;
        const auto &entry = manifest.at("assets").at(id);
        const auto size = entry.at("expanded_bytes").get<uint64_t>();
        if (!size || size > assetLimit || size > totalLimit - total)
            throw std::invalid_argument("scene package expansion budget exceeded");
        total += size;
        const auto compressed = bytes(assetPath(path, entry), assetLimit);
        if (compressed.size() != entry.at("stored_bytes").get<uint64_t>())
            throw std::invalid_argument("scene asset length mismatch");
        std::vector<uint8_t> data(size);
        uLongf decoded = size;
        if (uncompress(data.data(), &decoded, compressed.data(), compressed.size()) != Z_OK ||
            decoded != size || sha256(data) != id)
            throw std::invalid_argument("corrupt scene package asset: " + id);
        auto value = json::from_cbor(data);
        if (references.at(id) > 1)
            cache.emplace(id, value);
        return value;
    };
    result["objects"] = json::array();
    std::set<std::string> ids;
    for (const auto &entry : manifest.at("objects")) {
        const auto id = entry.at("id").get<std::string>();
        if (!ids.insert(id).second)
            throw std::invalid_argument("duplicate scene package object");
        auto object = read(entry.at("state"));
        if (object.at("id") != id)
            throw std::invalid_argument("scene package object identity mismatch");
        object["volume"] = read(entry.at("volume"));
        if (entry.contains("sensor_materials"))
            object["sensor_materials"] = read(entry.at("sensor_materials"));
        result["objects"].push_back(std::move(object));
    }
    for (const auto *key : {"earth_patches", "surface_appearances"}) {
        if (!manifest.at(key).empty()) {
            result[key] = json::object();
            for (const auto &entry : manifest.at(key).items())
                result[key][entry.key()] = read(entry.value());
        }
    }
    return result;
}
json VoxelScenePackage::save(const std::string &name, const json &topic, const json &checkpoint) {
    if (checkpoint.at("format") != "voxel-scene-checkpoint-v1" ||
        topic.at("format") != "earth-scene-topic-v1")
        throw std::invalid_argument("scene package requires canonical checkpoint and Earth topic");
    const auto path = fs::absolute(name);
    fs::create_directories(path.parent_path());
    const auto folder = path.filename().string() + ".data";
    fs::create_directories(path.parent_path() / folder);
    json manifest = {{"format", format},
                     {"version", 1},
                     {"codec", "cbor-zlib"},
                     {"coordinates", "east-up-south-metres"},
                     {"units", {{"length", "m"}, {"temperature", "K"}, {"angle", "field-suffix-deg-or-rad"}}},
                     {"topic", topic},
                     {"checkpoint", checkpoint},
                     {"assets", json::object()},
                     {"objects", json::array()}};
    for (const auto *key : {"objects", "earth_patches", "surface_appearances"})
        manifest["checkpoint"].erase(key);
    manifest["topic"].erase("checkpoint");
    manifest["topic"]["scene"] = path.filename().string();
    if (topic.contains("earth_map")) {
        const auto original = fs::path(topic.at("earth_map").get<std::string>());
        const auto relative = original.lexically_relative(fs::current_path());
        if (!relative.empty() && *relative.begin() != "..")
            manifest["topic"]["earth_map"] = "repo://" + relative.generic_string();
    }
    uint64_t total = 0;
    auto store = [&](const json &value) {
        const auto data = json::to_cbor(value);
        if (data.empty() || data.size() > assetLimit)
            throw std::invalid_argument("scene package exceeds binary budget");
        const auto digest = sha256(data);
        if (manifest["assets"].contains(digest))
            return digest;
        if (data.size() > totalLimit - total)
            throw std::invalid_argument("scene package exceeds total binary budget");
        total += data.size();
        const auto uri = folder + "/" + digest + ".cborz";
        const auto asset = assetPath(path, {{"uri", uri}});
        std::vector<uint8_t> compressed;
        if (fs::exists(asset)) {
            json entry = {{"uri", uri}};
            compressed = bytes(assetPath(path, entry), assetLimit);
            std::vector<uint8_t> decoded(data.size());
            uLongf length = decoded.size();
            if (uncompress(decoded.data(), &length, compressed.data(), compressed.size()) != Z_OK ||
                length != data.size() || decoded != data)
                throw std::invalid_argument(
                    "existing content-addressed asset is corrupt; existing manifest retained");
        } else {
            uLongf length = compressBound(data.size());
            compressed.resize(length);
            if (compress2(compressed.data(), &length, data.data(), data.size(), Z_BEST_SPEED) != Z_OK)
                throw std::runtime_error("scene package compression failed");
            compressed.resize(length);
            atomicWrite(asset, compressed);
        }
        manifest["assets"][digest] = {
            {"uri", uri}, {"stored_bytes", compressed.size()}, {"expanded_bytes", data.size()}};
        return digest;
    };
    for (auto object : checkpoint.at("objects")) {
        json entry = {{"id", object.at("id")},
                      {"role", object.at("role")},
                      {"material", object.at("material")},
                      {"physics", object.at("physics")}};
        entry["grid"] = {{"origin_m", object.at("volume").at("origin_m")},
                         {"dimensions", object.at("volume").at("dimensions")},
                         {"cell_size_m", object.at("volume").at("cell_size_m")},
                         {"base_pose", object.at("base_pose")}};
        entry["volume"] = store(object.at("volume"));
        object.erase("volume");
        if (object.contains("sensor_materials")) {
            entry["sensor_materials"] = store(object.at("sensor_materials"));
            object.erase("sensor_materials");
        }
        entry["state"] = store(object);
        manifest["objects"].push_back(std::move(entry));
    }
    for (const auto *key : {"earth_patches", "surface_appearances"}) {
        manifest[key] = json::object();
        const auto table = checkpoint.value(key, json::object());
        for (const auto &entry : table.items())
            manifest[key][entry.key()] = store(entry.value());
    }
    manifest["residency"] = {{"mode", "bounded-authoritative-checkpoint"},
                             {"far_terrain", "existing-map-streaming"},
                             {"all_chunks_are_not_full_region_physics", true}};
    validateManifest(manifest);
    const auto text = manifest.dump(2);
    if (text.size() > 8 * 1024 * 1024)
        throw std::invalid_argument("scene package manifest too large");
    atomicWrite(path, std::vector<uint8_t>(text.begin(), text.end()));
    return manifest;
}
} // namespace vsgraysim::earth
