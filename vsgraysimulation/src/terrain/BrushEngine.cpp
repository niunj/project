#include "vsgraysim/terrain/BrushEngine.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <spdlog/spdlog.h>

#include "vsgraysim/project/ICommand.h"

namespace vsgraysim {

BrushEngine::BrushEngine(LayerStack* layer_stack, TerrainProduct* terrain_product)
    : layer_stack_(layer_stack), terrain_product_(terrain_product) {
    // Generate preview ring geometry
    preview_.ring_vertices.resize(64);
    preview_.ring_indices.resize(64 * 2);
    for (int i = 0; i < 64; ++i) {
        double angle = 2.0 * M_PI * i / 64.0;
        preview_.ring_vertices[i] = {std::cos(angle), 0, std::sin(angle)};
        preview_.ring_indices[i * 2] = i;
        preview_.ring_indices[i * 2 + 1] = (i + 1) % 64;
    }
}

void BrushEngine::beginStroke(const vsg::dvec3& world_pos, double pressure) {
    current_stroke_.clear();
    current_stroke_.push_back({world_pos, pressure, 0.0});
    stroke_active_ = true;
    last_stroke_record_.reset();
}

void BrushEngine::addStrokePoint(const vsg::dvec3& world_pos, double pressure) {
    if (!stroke_active_) return;
    current_stroke_.push_back({world_pos, pressure, 0.0});
    applyAtPoint(world_pos, pressure);
}

void BrushEngine::endStroke() {
    if (!stroke_active_) return;
    stroke_active_ = false;
    // Record for undo would be handled by createStrokeCommand
}

void BrushEngine::applyDab(const vsg::dvec3& world_pos, double pressure) {
    beginStroke(world_pos, pressure);
    endStroke();
}

void BrushEngine::updatePreview(const vsg::dvec3& world_pos) {
    preview_.center = world_pos;
    preview_.radius = params_.radius;
    preview_.visible = true;
}

HeightLayer* BrushEngine::getTargetLayer() {
    if (!target_layer_id_.empty()) {
        return layer_stack_->getLayer(target_layer_id_);
    }
    // Default to first editable layer
    for (auto& l : layer_stack_->getLayers()) {
        if (l->type == HeightLayer::Type::Edit && !l->locked) {
            return l;
        }
    }
    return nullptr;
}

double BrushEngine::falloffWeight(double distance) const {
    double r = std::clamp(distance / params_.radius, 0.0, 1.0);
    double h = params_.hardness;

    switch (params_.falloff) {
        case FalloffType::Linear:
            return 1.0 - r;
        case FalloffType::Smooth: {
            double t = 1.0 - r;
            return t * t * (3.0 - 2.0 * t);  // Smoothstep
        }
        case FalloffType::Spherical:
            return std::sqrt(std::max(0.0, 1.0 - r * r));
        case FalloffType::Sharp:
            return (r < (1.0 - h)) ? 1.0 : (1.0 - r) / h;
        case FalloffType::Custom:
            if (!params_.custom_curve.empty()) {
                size_t idx = static_cast<size_t>(r * (params_.custom_curve.size() - 1));
                return params_.custom_curve[idx];
            }
            return 1.0 - r;
    }
    return 1.0 - r;
}

void BrushEngine::applyAtPoint(const vsg::dvec3& world_pos, double pressure) {
    HeightLayer* layer = getTargetLayer();
    if (!layer || !layer->tile_store) return;

    // Get tile key at world position
    TileKey key = terrain_product_->keyAt(world_pos.x, world_pos.z);

    // Read current tile
    auto tile_opt = layer->tile_store->read(key);
    TileData tile;
    if (tile_opt) {
        tile = *tile_opt;
    } else {
        // Create new tile
        tile.key = key;
        size_t stride = terrain_product_->tile_size + 1;
        tile.height.assign(stride * stride, 0.0f);
    }

    // Store before state for undo
    if (!last_stroke_record_) {
        last_stroke_record_ = StrokeRecord{};
        last_stroke_record_->layer_id = layer->id;
    }
    last_stroke_record_->before_tiles.emplace_back(key, tile);

    // Apply brush to tile
    double tile_world_size = terrain_product_->resolution * terrain_product_->tile_size;
    double local_x = world_pos.x - (terrain_product_->world_bounds.min.x + key.tx * tile_world_size);
    double local_z = world_pos.z - (terrain_product_->world_bounds.min.z + key.ty * tile_world_size);

    double fx = local_x / terrain_product_->resolution;
    double fz = local_z / terrain_product_->resolution;

    int center_ix = static_cast<int>(std::round(fx));
    int center_iz = static_cast<int>(std::round(fz));

    int radius_samples = static_cast<int>(std::ceil(params_.radius / terrain_product_->resolution));
    int stride = terrain_product_->tile_size + 1;

    double strength = params_.strength;
    if (params_.pressure_sensitive) {
        strength *= pressure * params_.pressure_strength_scale;
    }

    for (int dz = -radius_samples; dz <= radius_samples; ++dz) {
        for (int dx = -radius_samples; dx <= radius_samples; ++dx) {
            int ix = center_ix + dx;
            int iz = center_iz + dz;

            if (ix < 0 || ix >= stride || iz < 0 || iz >= stride) continue;

            double dist = std::sqrt(dx * dx + dz * dz) * terrain_product_->resolution;
            double weight = falloffWeight(dist);
            if (weight <= 0.0) continue;

            double delta = 0.0;
            switch (params_.mode) {
                case TerrainBrushMode::Raise:
                    delta = strength * weight;
                    break;
                case TerrainBrushMode::Lower:
                    delta = -strength * weight;
                    break;
                case TerrainBrushMode::Flatten:
                    delta = (params_.target_height - tile.height[iz * stride + ix]) * strength * weight;
                    break;
                case TerrainBrushMode::Set:
                    delta = (params_.target_height - tile.height[iz * stride + ix]) * weight;
                    break;
                case TerrainBrushMode::Smooth: {
                    // Average neighbors
                    double sum = 0.0;
                    int count = 0;
                    for (int nz = -1; nz <= 1; ++nz) {
                        for (int nx = -1; nx <= 1; ++nx) {
                            int nix = ix + nx;
                            int niz = iz + nz;
                            if (nix >= 0 && nix < stride && niz >= 0 && niz < stride) {
                                sum += tile.height[niz * stride + nix];
                                count++;
                            }
                        }
                    }
                    double avg = count > 0 ? sum / count : tile.height[iz * stride + ix];
                    delta = (avg - tile.height[iz * stride + ix]) * strength * weight;
                    break;
                }
                case TerrainBrushMode::Noise: {
                    // Add noise (simplified)
                    static std::mt19937 rng{std::random_device{}()};
                    static std::uniform_real_distribution<double> dist(-1, 1);
                    delta = dist(rng) * strength * weight;
                    break;
                }
                case TerrainBrushMode::Erase:
                    // Erase to base layer (would need base layer reference)
                    delta = -tile.height[iz * stride + ix] * strength * weight;
                    break;
            }

            double new_height = tile.height[iz * stride + ix] + delta;

            // Clamp
            if (params_.clamp_to_base) {
                // Would need base layer height here
            }
            new_height = std::clamp(new_height, params_.min_height, params_.max_height);

            tile.height[iz * stride + ix] = static_cast<float>(new_height);
        }
    }

    // Update min/max
    tile.min_height = *std::min_element(tile.height.begin(), tile.height.end());
    tile.max_height = *std::max_element(tile.height.begin(), tile.height.end());

    // Write back
    layer->tile_store->write(key, tile);

    // Record after state
    if (last_stroke_record_) {
        last_stroke_record_->after_tiles.emplace_back(key, tile);
    }
}

std::shared_ptr<ICommand> BrushEngine::createStrokeCommand() {
    if (!last_stroke_record_ || last_stroke_record_->before_tiles.empty()) return nullptr;

    auto record = *last_stroke_record_;
    last_stroke_record_.reset();

    return makeCommand(
        "BrushStroke_" + record.layer_id,
        "Brush stroke on layer " + record.layer_id,
        "TerrainSubsystem",
        "TerrainProduct",
        [this, record]() {
            // Redo: apply after state
            HeightLayer* layer = layer_stack_->getLayer(record.layer_id);
            if (layer && layer->tile_store) {
                for (const auto& [key, tile] : record.after_tiles) {
                    layer->tile_store->write(key, tile);
                }
            }
            return true;
        },
        [this, record]() {
            // Undo: restore before state
            HeightLayer* layer = layer_stack_->getLayer(record.layer_id);
            if (layer && layer->tile_store) {
                for (const auto& [key, tile] : record.before_tiles) {
                    layer->tile_store->write(key, tile);
                }
            }
            return true;
        }
    );
}

} // namespace vsgraysim
