#include "vsgraysim/terrain/TerrainSubsystem.h"

#include "vsgraysim/project/ProjectSerializer.h"

#include <vsg/utils/Builder.h>
#include <vsg/state/DepthStencilState.h>

#include <spdlog/spdlog.h>

namespace vsgraysim {

TerrainSubsystem::TerrainSubsystem() = default;

TerrainSubsystem::~TerrainSubsystem() {
    shutdown();
}

bool TerrainSubsystem::initialize(const nlohmann::json& config) {
    if (config.contains("chunk")) {
        spdlog::error("TerrainSubsystem: independent chunk rendering is retired; use the shared Rocky earth base");
        return false;
    }
    // Parse config
    config_.crs = config.value("crs", "EPSG:4978");
    if (config.contains("bounds")) {
        auto b = config["bounds"];
        config_.bounds.min = {b["min"][0], b["min"][1], b["min"][2]};
        config_.bounds.max = {b["max"][0], b["max"][1], b["max"][2]};
    }
    config_.resolution = config.value("resolution", 1.0);
    config_.tile_size = config.value("tile_size", 64);
    config_.tile_store_path = config.value("tile_store_path", "");
    config_.tile_store_zip = config.value("tile_store_zip", false);

    config_.create_default_layers = config.value("create_default_layers", true);

    // Create tile store
    if (!config_.tile_store_path.empty()) {
        if (config_.tile_store_zip) {
            tile_store_ = TileStoreFactory::createZipStore(config_.tile_store_path);
        } else {
            tile_store_ = TileStoreFactory::createFileStore(config_.tile_store_path);
        }
    } else {
        tile_store_ = TileStoreFactory::createMemoryStore();
    }

    // Create terrain product
    CRSInfo crs_info;
    crs_info.epsg_code = config_.crs;
    crs_info.is_projected = true;

    product_ = TerrainProduct::create(crs_info, config_.bounds, config_.resolution, config_.tile_size);
    product_->tile_store = tile_store_;

    // Create layer stack
    layer_stack_ = std::make_unique<LayerStack>(config_.tile_size, config_.resolution);

    // Create brush engine
    brush_engine_ = std::make_unique<BrushEngine>(layer_stack_.get(), product_.get());

    // Create default layers
    if (config_.create_default_layers) {
        createDefaultLayers();
    }

    initialized_ = true;
    spdlog::info("TerrainSubsystem initialized: {}x{} tiles, resolution={}m",
                 config_.tile_size, config_.tile_size, config_.resolution);

    return true;
}

void TerrainSubsystem::shutdown() {
    brush_engine_.reset();
    layer_stack_.reset();
    product_.reset();
    tile_store_.reset();
    initialized_ = false;
}

void TerrainSubsystem::createDefaultLayers() {
    // Base layer (procedural)
    std::string base_id = layer_stack_->addLayer("Base", HeightLayer::Type::Base, BlendMode::Replace);
    layer_stack_->getLayer(base_id)->locked = true;

    // Procedural detail layer
    std::string proc_id = layer_stack_->addLayer("Procedural", HeightLayer::Type::Procedural, BlendMode::Add);
    auto* proc_layer = layer_stack_->getLayer(proc_id);
    proc_layer->proc_params.seed = 12345;
    proc_layer->proc_params.frequency = 0.005;
    proc_layer->proc_params.amplitude = 50.0;
    proc_layer->proc_params.octaves = 6;
    proc_layer->proc_params.noise_type = "fbm";

    // Edit layer (user edits)
    std::string edit_id = layer_stack_->addLayer("Edits", HeightLayer::Type::Edit, BlendMode::Replace);
    layer_stack_->getLayer(edit_id)->blend_mode = BlendMode::Replace;

    // Mask layer
    layer_stack_->addLayer("Masks", HeightLayer::Type::Mask, BlendMode::Mask);

    // Set target layer to edit layer
    brush_engine_->setTargetLayer(edit_id);
}

nlohmann::json TerrainSubsystem::diagnostics() const {
    nlohmann::json j;
    j["initialized"] = initialized_;
    j["dirty"] = dirty_;
    j["product_id"] = product_ ? product_->product_id : "none";
    j["layers"] = layer_stack_ ? static_cast<int>(layer_stack_->getLayers().size()) : 0;
    j["tile_store"] = tile_store_ ? tile_store_->describe() : "none";

    j["render_backend"] = "rocky_external";

    return j;
}

// ---- IEditorSubsystem ----

std::shared_ptr<void> TerrainSubsystem::produceProduct() const {
    // Update product from layer stack before saving
    // In a full implementation, we'd composite all layers into product tiles
    return std::static_pointer_cast<void>(std::const_pointer_cast<TerrainProduct>(product_));
}

void TerrainSubsystem::consumeProduct(std::shared_ptr<const void> product) {
    auto terrain = std::static_pointer_cast<const TerrainProduct>(product);
    if (terrain) {
        product_ = std::const_pointer_cast<TerrainProduct>(terrain);
        // Rebuild layer stack from product if needed
        dirty_ = false;
    }
}

bool TerrainSubsystem::executeCommand(std::shared_ptr<void> command) {
    auto cmd = std::static_pointer_cast<ICommand>(command);
    if (cmd && cmd->productType() == "TerrainProduct") {
        bool result = cmd->execute();
        if (result) dirty_ = true;
        return result;
    }
    return false;
}

// ---- ISimulationSubsystem ----

void TerrainSubsystem::onPreTick(const TimeTick& tick) {
    // Terrain is mostly read-only during simulation
    // Update chunk manager for camera position if needed
}

void TerrainSubsystem::onTick(const TimeTick& tick) {
    // Physical terrain is an immutable EarthSurfaceSnapshot; no GPU work here.
}

void TerrainSubsystem::onPostTick(const TimeTick& tick) {
    // Nothing to do
}

void* TerrainSubsystem::getMutableProduct(const std::string& product_type) {
    if (product_type == "TerrainProduct") return product_.get();
    return nullptr;
}

const void* TerrainSubsystem::getConstProduct(const std::string& product_type) const {
    if (product_type == "TerrainProduct") return product_.get();
    return nullptr;
}

// ---- Terrain-specific API ----

void TerrainSubsystem::regenerateProcedural() {
    layer_stack_->regenerateAllProcedural();
    dirty_ = true;
}

bool TerrainSubsystem::save(const std::string& path) const {
    ProjectSerializer serializer;
    return serializer.saveTerrain(product_, path);
}

bool TerrainSubsystem::load(const std::string& path) {
    ProjectSerializer serializer;
    auto loaded = serializer.loadTerrain(path);
    if (loaded) {
        product_ = loaded;
        dirty_ = false;
        return true;
    }
    return false;
}

bool TerrainSubsystem::exportHeightmap(const std::string& path,
                                    HeightmapExportFormat fmt) const {
    return product_ ? product_->exportHeightmap(path, fmt) : false;
}

bool TerrainSubsystem::exportMesh(const std::string& path,
                               MeshExportFormat fmt, int max_lod) const {
    return product_ ? product_->exportMesh(path, fmt, max_lod) : false;
}

vsg::ref_ptr<vsg::Group> TerrainSubsystem::getRootNode() const {
    return {};
}

} // namespace vsgraysim
