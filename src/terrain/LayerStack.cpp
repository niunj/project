#include "vsgraysim/terrain/LayerStack.h"
#include "vsgraysim/terrain/TileStore.h"
#include "vsgraysim/products/TerrainProduct.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace vsgraysim {

LayerStack::LayerStack(uint32_t tile_size, double resolution)
    : tile_size_(tile_size), resolution_(resolution) {}

std::string LayerStack::addLayer(const std::string& name, HeightLayer::Type type, BlendMode blend) {
    auto layer = std::make_unique<HeightLayer>();
    layer->name = name;
    layer->id = TerrainProduct::generateUUID();
    layer->type = type;
    layer->blend_mode = blend;
    layer->order = static_cast<int>(layers_.size());
    layer->tile_store = std::make_shared<MemoryTileStore>();

    std::string id = layer->id;
    layers_.push_back(std::move(layer));

    // Reorder
    std::sort(layers_.begin(), layers_.end(),
        [](const auto& a, const auto& b) { return a->order < b->order; });

    return id;
}

bool LayerStack::removeLayer(const std::string& id) {
    auto it = std::find_if(layers_.begin(), layers_.end(),
        [&id](const auto& l) { return l->id == id; });
    if (it != layers_.end()) {
        layers_.erase(it);
        // Reorder
        for (size_t i = 0; i < layers_.size(); ++i) {
            layers_[i]->order = static_cast<int>(i);
        }
        return true;
    }
    return false;
}

bool LayerStack::moveLayer(const std::string& id, int new_order) {
    auto it = std::find_if(layers_.begin(), layers_.end(),
        [&id](const auto& l) { return l->id == id; });
    if (it != layers_.end()) {
        int old_order = (*it)->order;
        (*it)->order = new_order;
        // Reorder
        std::sort(layers_.begin(), layers_.end(),
            [](const auto& a, const auto& b) { return a->order < b->order; });
        return true;
    }
    return false;
}

HeightLayer* LayerStack::getLayer(const std::string& id) {
    for (auto& l : layers_) if (l->id == id) return l.get();
    return nullptr;
}

const HeightLayer* LayerStack::getLayer(const std::string& id) const {
    for (const auto& l : layers_) if (l->id == id) return l.get();
    return nullptr;
}

std::vector<HeightLayer*> LayerStack::getLayers() {
    std::vector<HeightLayer*> result;
    for (auto& l : layers_) result.push_back(l.get());
    return result;
}

void LayerStack::setLayerVisible(const std::string& id, bool visible) {
    if (auto* l = getLayer(id)) l->visible = visible;
}

void LayerStack::setLayerLocked(const std::string& id, bool locked) {
    if (auto* l = getLayer(id)) l->locked = locked;
}

void LayerStack::setLayerOpacity(const std::string& id, float opacity) {
    if (auto* l = getLayer(id)) l->opacity = std::clamp(opacity, 0.0f, 1.0f);
}

void LayerStack::setLayerBlendMode(const std::string& id, BlendMode mode) {
    if (auto* l = getLayer(id)) l->blend_mode = mode;
}

double LayerStack::heightAt(double wx, double wz) const {
    double tile_world_size = resolution_ * tile_size_;

    TileKey key;
    key.morton = 0;
    key.lod = 0;
    key.tx = static_cast<uint32_t>(std::floor(wx / tile_world_size));
    key.ty = static_cast<uint32_t>(std::floor(wz / tile_world_size));

    auto composite = readCompositeTile(key);
    if (!composite) return 0.0;

    // Local coordinates within tile
    double local_x = wx - key.tx * tile_world_size;
    double local_z = wz - key.ty * tile_world_size;
    double fx = local_x / resolution_;
    double fz = local_z / resolution_;

    int ix = static_cast<int>(std::floor(fx));
    int iz = static_cast<int>(std::floor(fz));
    double u = fx - ix;
    double v = fz - iz;

    size_t stride = tile_size_ + 1;
    auto idx = [stride](int x, int z) { return size_t(z) * stride + x; };

    const auto& h = composite->height;
    if (h.empty()) return 0.0;

    ix = std::clamp(ix, 0, static_cast<int>(tile_size_) - 1);
    iz = std::clamp(iz, 0, static_cast<int>(tile_size_) - 1);
    int ix1 = std::min(ix + 1, static_cast<int>(tile_size_));
    int iz1 = std::min(iz + 1, static_cast<int>(tile_size_));

    double h00 = h[idx(ix, iz)];
    double h10 = h[idx(ix1, iz)];
    double h01 = h[idx(ix, iz1)];
    double h11 = h[idx(ix1, iz1)];

    double h0 = h00 * (1 - u) + h10 * u;
    double h1 = h01 * (1 - u) + h11 * u;
    return h0 * (1 - v) + h1 * v;
}

double LayerStack::heightAtLayer(const std::string& layer_id, double wx, double wz) const {
    if (auto* l = getLayer(layer_id)) {
        if (l->tile_store) {
            // return l->tile_store->heightAt(wx, wz);
            return 0.0;
        }
    }
    return 0.0;
}

std::optional<TileData> LayerStack::readCompositeTile(const TileKey& key) const {
    TileData result;
    result.key = key;
    bool first = true;

    for (const auto& layer : layers_) {
        if (!layer->visible || !layer->tile_store) continue;

        auto tile_opt = layer->tile_store->read(key);
        if (!tile_opt) continue;

        if (first) {
            result = *tile_opt;
            first = false;
        } else {
            blendTiles(result, *tile_opt, layer->blend_mode, layer->opacity, result);
        }
    }

    if (first) return std::nullopt;
    return result;
}

void LayerStack::writeLayerTile(const std::string& layer_id, const TileKey& key,
                                const TileData& data) {
    if (auto* l = getLayer(layer_id)) {
        if (l->tile_store) {
            l->tile_store->write(key, data);
        }
    }
}

std::optional<TileData> LayerStack::readLayerTile(const std::string& layer_id,
                                                   const TileKey& key) const {
    if (auto* l = getLayer(layer_id)) {
        if (l->tile_store) {
            return l->tile_store->read(key);
        }
    }
    return std::nullopt;
}

void LayerStack::generateProceduralLayer(const std::string& layer_id) {
    if (auto* l = getLayer(layer_id)) {
        if (l->type != HeightLayer::Type::Procedural) return;
        // Procedural generation would go here
        // For now, just mark as generated
    }
}

void LayerStack::regenerateAllProcedural() {
    for (auto& l : layers_) {
        if (l->type == HeightLayer::Type::Procedural) {
            generateProceduralLayer(l->id);
        }
    }
}

void LayerStack::setLayerMask(const std::string& layer_id,
                              std::shared_ptr<ITileStore> mask_store) {
    if (auto* l = getLayer(layer_id)) {
        l->mask_store = mask_store;
    }
}

nlohmann::json LayerStack::serialize() const {
    nlohmann::json j;
    j["tile_size"] = tile_size_;
    j["resolution"] = resolution_;
    j["layers"] = nlohmann::json::array();

    for (const auto& l : layers_) {
        nlohmann::json lj;
        lj["id"] = l->id;
        lj["name"] = l->name;
        lj["visible"] = l->visible;
        lj["locked"] = l->locked;
        lj["opacity"] = l->opacity;
        lj["blend_mode"] = static_cast<int>(l->blend_mode);
        lj["order"] = l->order;
        lj["type"] = static_cast<int>(l->type);
        if (l->type == HeightLayer::Type::Procedural) {
            lj["proc_params"] = {
                {"seed", l->proc_params.seed},
                {"frequency", l->proc_params.frequency},
                {"amplitude", l->proc_params.amplitude},
                {"octaves", l->proc_params.octaves},
                {"lacunarity", l->proc_params.lacunarity},
                {"gain", l->proc_params.gain},
                {"noise_type", l->proc_params.noise_type}
            };
        }
        j["layers"].push_back(lj);
    }
    return j;
}

bool LayerStack::deserialize(const nlohmann::json& j) {
    tile_size_ = j.value("tile_size", 64);
    resolution_ = j.value("resolution", 1.0);
    layers_.clear();

    for (const auto& lj : j.value("layers", nlohmann::json::array())) {
        auto layer = std::make_unique<HeightLayer>();
        layer->id = lj.value("id", TerrainProduct::generateUUID());
        layer->name = lj.value("name", "Layer");
        layer->visible = lj.value("visible", true);
        layer->locked = lj.value("locked", false);
        layer->opacity = lj.value("opacity", 1.0f);
        layer->blend_mode = static_cast<BlendMode>(lj.value("blend_mode", 0));
        layer->order = lj.value("order", 0);
        layer->type = static_cast<HeightLayer::Type>(lj.value("type", 0));
        layer->tile_store = std::make_shared<MemoryTileStore>();

        if (lj.contains("proc_params")) {
            auto& pp = lj["proc_params"];
            layer->proc_params.seed = pp.value("seed", 0);
            layer->proc_params.frequency = pp.value("frequency", 0.01);
            layer->proc_params.amplitude = pp.value("amplitude", 10.0);
            layer->proc_params.octaves = pp.value("octaves", 4);
            layer->proc_params.lacunarity = pp.value("lacunarity", 2.0);
            layer->proc_params.gain = pp.value("gain", 0.5);
            layer->proc_params.noise_type = pp.value("noise_type", "fbm");
        }

        layers_.push_back(std::move(layer));
    }

    std::sort(layers_.begin(), layers_.end(),
        [](const auto& a, const auto& b) { return a->order < b->order; });

    return true;
}

double LayerStack::blend(double base, double layer, BlendMode mode, float opacity) const {
    double l = layer * opacity;
    switch (mode) {
        case BlendMode::Replace: return l;
        case BlendMode::Add: return base + l;
        case BlendMode::Subtract: return base - l;
        case BlendMode::Multiply: return base * (1.0 + l);
        case BlendMode::Overlay:
            return base < 0.5 ? 2.0 * base * l : 1.0 - 2.0 * (1.0 - base) * (1.0 - l);
        case BlendMode::Min: return std::min(base, l);
        case BlendMode::Max: return std::max(base, l);
        case BlendMode::Mask: return base * (1.0 - opacity) + l * opacity;
    }
    return base;
}

double LayerStack::blendTiles(const TileData& base,
                              const TileData& layer,
                              BlendMode mode, float opacity,
                              TileData& output) const {
    // 逐像素混合高度场；输出与 base 尺寸一致。
    size_t count = base.height.size();
    if (layer.height.size() < count) count = layer.height.size();

    output = base;

    if (count == 0) return 0.0;
    if (output.height.size() < count) output.height.resize(count);

    for (size_t i = 0; i < count; ++i) {
        output.height[i] = static_cast<float>(blend(base.height[i], layer.height[i], mode, opacity));
    }

    // 法线 / splat：若只有层有数据则提升（简化：取 base；splat 与 normal 保持 base）
    return 0.0;
}

} // namespace vsgraysim