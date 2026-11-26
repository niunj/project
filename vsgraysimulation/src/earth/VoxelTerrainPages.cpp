#include "VoxelTerrainPages.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace vsgraysim::earth::terrain_pages {
namespace {
using json = nlohmann::json;

int checkedInteger(const json& value, int minimum, int maximum) {
    const auto n = value.get<double>();
    if (!value.is_number_integer() || n < minimum || n > maximum)
        throw std::invalid_argument("invalid terrain page dimension or offset");
    return value.get<int>();
}

size_t area(const Rect& rect) {
    return size_t(rect[2] - rect[0]) * (rect[3] - rect[1]);
}

template<class F> void rows(const Rect& rect, int pitch, F copy) {
    const size_t width = rect[2] - rect[0];
    for (int z = rect[1]; z < rect[3]; ++z)
        copy(size_t(z) * pitch + rect[0], size_t(z - rect[1]) * width, width);
}

json sliceSamples(const json& source, int pitch, const Rect& rect) {
    auto output = json::array();
    auto& values = output.get_ref<json::array_t&>();
    values.reserve(area(rect));
    rows(rect, pitch, [&](size_t global, size_t, size_t count) {
        values.insert(values.end(), source.begin() + global, source.begin() + global + count);
    });
    return output;
}

void restoreSamples(json::array_t& output, const json& source, int pitch, const Rect& rect) {
    rows(rect, pitch, [&](size_t global, size_t local, size_t count) {
        std::copy_n(source.begin() + local, count, output.begin() + global);
    });
}

bool validSamples(const json& samples, size_t count) {
    return samples.is_array() && samples.size() == count &&
        std::all_of(samples.begin(), samples.end(), [](const auto& v) {
            return v.is_number() && std::isfinite(v.template get<double>());
        });
}

size_t cellCount(const Layout& l) {
    return size_t(l.dimensions[0]) * l.dimensions[1] * l.dimensions[2];
}

void validateSharedSamples(const Layout& layout, const std::array<json, 4>& pages, const char* key) {
    for (const auto& [a, b] : std::array<std::array<int, 2>, 4>{{{0, 1}, {2, 3}, {0, 2}, {1, 3}}}) {
        const auto r = layout.heights(a);
        const auto s = layout.heights(b);
        const auto& p = pages[a].at(key);
        const auto& q = pages[b].at(key);
        for (int z = std::max(r[1], s[1]); z < std::min(r[3], s[3]); ++z)
            for (int x = std::max(r[0], s[0]); x < std::min(r[2], s[2]); ++x)
                if (p[size_t(z - r[1]) * (r[2] - r[0]) + x - r[0]] !=
                    q[size_t(z - s[1]) * (s[2] - s[0]) + x - s[0]])
                    throw std::invalid_argument("terrain pages disagree on their shared height samples");
    }
}
}

Layout::Layout(const json& value) {
    if (value.at("layout") != "quad-v1")
        throw std::invalid_argument("unsupported terrain page layout");
    const auto& dims = value.at("dimensions");
    const auto& position = value.at("origin_m");
    const auto& image = value.at("imagery_size");
    if (!dims.is_array() || dims.size() != 3 || !position.is_array() || position.size() != 3 ||
        !image.is_array() || image.size() != 2)
        throw std::invalid_argument("invalid terrain page layout axes");
    dimensions = {checkedInteger(dims[0], 2, 4096), checkedInteger(dims[1], 1, 4'000'000),
                  checkedInteger(dims[2], 2, 4096)};
    origin = position.get<std::array<double, 3>>();
    if (std::any_of(origin.begin(), origin.end(), [](double v) { return !std::isfinite(v) || std::abs(v) > 1e6; }))
        throw std::invalid_argument("invalid terrain page origin");
    cell = value.at("cell_size_m").get<double>();
    subdivisions = checkedInteger(value.at("hd_subdivisions"), 1, 8);
    imagery = {checkedInteger(image[0], 0, 4096), checkedInteger(image[1], 0, 4096)};
    sea = value.at("sea_reference").get<bool>();
    if (!std::isfinite(cell) || cell < 1e-5 || cell > 1e5 || cellCount(*this) > 4'000'000 ||
        size_t(dimensions[0] * subdivisions + 1) * (dimensions[2] * subdivisions + 1) > 1'050'625)
        throw std::invalid_argument("terrain pages exceed Earth patch limits");
}

Rect Layout::cells(int page) const {
    if (page < 0 || page >= 4) throw std::out_of_range("unknown terrain page");
    const int x = dimensions[0] / 2, z = dimensions[2] / 2;
    return {page % 2 ? x : 0, page / 2 ? z : 0, page % 2 ? dimensions[0] : x,
            page / 2 ? dimensions[2] : z};
}

Rect Layout::heights(int page) const {
    auto r = cells(page);
    for (auto& v : r) v *= subdivisions;
    // The sample on the last cell edge belongs to both adjacent pages.
    ++r[2];
    ++r[3];
    return r;
}

Rect Layout::pixels(int page) const {
    auto r = cells(page);
    for (int a = 0; a < 4; ++a) r[a] = r[a] * imagery[a % 2] / dimensions[(a % 2) * 2];
    return r;
}

std::array<double, 4> Layout::bounds(int page) const {
    const auto r = cells(page);
    return {origin[0] + r[0] * cell, origin[2] + r[1] * cell,
            origin[0] + r[2] * cell, origin[2] + r[3] * cell};
}

json index(const json& patch, const json& volume) {
    if (patch.at("format") != "voxel-earth-patch-v1" || volume.at("format") != "voxel-volume-rle-v1" ||
        patch.at("dimensions") != volume.at("dimensions") || patch.at("origin_m") != volume.at("origin_m") ||
        patch.at("cell_size_m") != volume.at("cell_size_m") || volume.contains("surface_appearance") ||
        volume.contains("surface_appearance_ref"))
        return nullptr;
    const auto& dims = patch.at("dimensions");
    if (dims[0].get<int>() < 2 || dims[2].get<int>() < 2) return nullptr;
    return {{"layout", "quad-v1"}, {"dimensions", dims}, {"origin_m", patch.at("origin_m")},
            {"cell_size_m", patch.at("cell_size_m")}, {"hd_subdivisions", patch.at("hd_subdivisions")},
            {"imagery_size", patch.at("imagery_size")}, {"sea_reference", patch.contains("sea_mask_epsilon_m")}};
}

std::array<json, 4> split(const Layout& layout, json& patch, json& volume) {
    const int nx = layout.dimensions[0], ny = layout.dimensions[1];
    const int pitch = nx * layout.subdivisions + 1;
    const size_t samples = size_t(pitch) * (layout.dimensions[2] * layout.subdivisions + 1);
    const auto& heights = patch.at("heights_m");
    const auto& pixels = patch.at("imagery_rgba_hex").get_ref<const std::string&>();
    if (!validSamples(heights, samples) || pixels.size() != size_t(layout.imagery[0]) * layout.imagery[1] * 8 ||
        (layout.sea && !validSamples(patch.at("sea_reference_m"), samples)))
        throw std::invalid_argument("invalid terrain source arrays for paging");
    std::vector<uint8_t> occupied(cellCount(layout));
    size_t end = 0;
    for (const auto& run : volume.at("occupied_runs")) {
        if (!run.is_array() || run.size() != 2) throw std::invalid_argument("invalid terrain occupancy run");
        const auto start = size_t(checkedInteger(run[0], 0, int(occupied.size())));
        const auto count = size_t(checkedInteger(run[1], 1, int(occupied.size())));
        if (start > occupied.size() - count || (end && start <= end))
            throw std::invalid_argument("invalid terrain occupancy range");
        std::fill_n(occupied.begin() + start, count, uint8_t(1));
        end = start + count;
    }
    std::array<json, 4> result;
    for (int page = 0; page < 4; ++page) {
        const auto c = layout.cells(page), h = layout.heights(page), p = layout.pixels(page);
        std::string image(area(p) * 8, '0');
        rows(p, layout.imagery[0], [&](size_t global, size_t local, size_t count) {
            std::copy_n(pixels.begin() + global * 8, count * 8, image.begin() + local * 8);
        });
        std::vector<uint8_t> bits((area(c) * ny + 7) / 8);
        size_t local = 0;
        for (int z = c[1]; z < c[3]; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = c[0]; x < c[2]; ++x, ++local)
                    bits[local / 8] |= occupied[(size_t(z) * ny + y) * nx + x] << (local % 8);
        auto& blob = result[page];
        blob = {{"height_m", sliceSamples(heights, pitch, h)}, {"rgba_hex", std::move(image)},
                {"occupancy_bits", json::binary(std::move(bits))}};
        if (layout.sea) blob["sea_reference_m"] = sliceSamples(patch.at("sea_reference_m"), pitch, h);
    }
    patch.erase("heights_m");
    patch.erase("imagery_rgba_hex");
    patch.erase("sea_reference_m");
    volume.erase("occupied_runs");
    return result;
}

void validatePage(const Layout& layout, int page, const json& blob) {
    if (!validSamples(blob.at("height_m"), area(layout.heights(page))) ||
        layout.sea != blob.contains("sea_reference_m") ||
        (layout.sea && !validSamples(blob.at("sea_reference_m"), area(layout.heights(page)))))
        throw std::invalid_argument("terrain page height data does not match its index");
    const auto& image = blob.at("rgba_hex").get_ref<const std::string&>();
    if (image.size() != area(layout.pixels(page)) * 8)
        throw std::invalid_argument("terrain page imagery size does not match its index");
    const auto& bits = blob.at("occupancy_bits").get_binary();
    const size_t count = area(layout.cells(page)) * layout.dimensions[1];
    if (bits.size() != (count + 7) / 8 || (count % 8 && bits.back() >> (count % 8)))
        throw std::invalid_argument("terrain page occupancy does not match its index");
}

void restore(const Layout& layout, const std::array<json, 4>& pages, json& patch, json& volume) {
    const int nx = layout.dimensions[0], ny = layout.dimensions[1];
    const int pitch = nx * layout.subdivisions + 1;
    const size_t samples = size_t(pitch) * (layout.dimensions[2] * layout.subdivisions + 1);
    json::array_t heights(samples), sea(layout.sea ? samples : 0);
    std::string image(size_t(layout.imagery[0]) * layout.imagery[1] * 8, '0');
    std::vector<uint8_t> occupancy(cellCount(layout));
    for (int page = 0; page < 4; ++page) {
        const auto c = layout.cells(page), h = layout.heights(page), p = layout.pixels(page);
        const auto& blob = pages[page];
        validatePage(layout, page, blob);
        restoreSamples(heights, blob.at("height_m"), pitch, h);
        if (layout.sea) restoreSamples(sea, blob.at("sea_reference_m"), pitch, h);
        const auto& localImage = blob.at("rgba_hex").get_ref<const std::string&>();
        rows(p, layout.imagery[0], [&](size_t global, size_t local, size_t count) {
            std::copy_n(localImage.begin() + local * 8, count * 8, image.begin() + global * 8);
        });
        const auto& bits = blob.at("occupancy_bits").get_binary();
        size_t local = 0;
        for (int z = c[1]; z < c[3]; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = c[0]; x < c[2]; ++x, ++local)
                    occupancy[(size_t(z) * ny + y) * nx + x] = (bits[local / 8] >> (local % 8)) & 1;
    }
    validateSharedSamples(layout, pages, "height_m");
    if (layout.sea) validateSharedSamples(layout, pages, "sea_reference_m");
    patch["heights_m"] = std::move(heights);
    patch["imagery_rgba_hex"] = std::move(image);
    if (layout.sea) patch["sea_reference_m"] = std::move(sea);
    auto runs = json::array();
    for (size_t start = 0; start < occupancy.size();) {
        if (!occupancy[start]) {
            ++start;
            continue;
        }
        size_t end = start + 1;
        while (end < occupancy.size() && occupancy[end]) ++end;
        runs.push_back({start, end - start});
        start = end;
    }
    volume["occupied_runs"] = std::move(runs);
}
}
