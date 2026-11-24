/// @file TerrainLayerFeature.cpp
/// @brief 地形层特性实现 (见 TerrainLayerFeature.h)。
#include "TerrainLayerFeature.h"

#include "FeatureRegistry.h"
#include "ViewportContext.h"

#include "vsgraysim/layer1_scene/TerrainSystem.h"

#include <vsg/maths/transform.h>
#include <vsg/nodes/MatrixTransform.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace vsgraysim { namespace viewport3d {

// ---------------------------------------------------------------------------
// ITerrainQuery 实现: 世界坐标 (居中后) → 地形局部坐标转换在本类内部完成。
// ---------------------------------------------------------------------------
class TerrainLayerFeature::Query : public ITerrainQuery {
public:
    explicit Query(TerrainLayerFeature* owner) : owner_(owner) {}

    bool hasTerrain() const override
    {
        return owner_ && owner_->terrain_ && owner_->loaded_;
    }
    double heightAtWorld(double x, double z) const override
    {
        auto* t = owner_ ? owner_->terrain_.get() : nullptr;
        if (!t) return 0.0;
        return t->heightAtWorld(x + owner_->offset_, z + owner_->offset_);
    }
    double cellSize() const override
    {
        return (owner_ && owner_->terrain_) ? owner_->terrain_->cellSize() : 0.0;
    }
    double extent() const override
    {
        return (owner_ && owner_->terrain_) ? owner_->terrain_->extent() : 0.0;
    }
    uint32_t grid() const override
    {
        return (owner_ && owner_->terrain_) ? owner_->terrain_->grid() : 0;
    }
    uint64_t nodeGeneration() const override
    {
        return (owner_ && owner_->terrain_) ? owner_->terrain_->nodeGeneration() : 0;
    }
    uint64_t heightsVersion() const override
    {
        return owner_ ? owner_->edit_version_ : 0;
    }

private:
    TerrainLayerFeature* owner_;
};

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
TerrainLayerFeature::~TerrainLayerFeature() = default;

void TerrainLayerFeature::onAttach(ViewportContext& ctx)
{
    ctx_ = &ctx;
    terrain_mt_ = vsg::MatrixTransform::create();

    query_ = std::make_shared<Query>(this);
    ctx.setTerrainQuery(query_);

    if (!auto_generate_) {
        // 场景设计/仿真预览: 初始无地形, 等 loadHeightmapFile 或配置生成。
        spdlog::info("TerrainLayerFeature: attached idle (awaiting heightmap)");
        return;
    }

    if (!terrain_) terrain_ = std::make_unique<TerrainSystem>();
    applyParams();

    terrain_mt_->matrix = vsg::translate(-offset_, 0.0, -offset_);
    terrain_mt_->addChild(terrain_->node());
    // geo 模式挂到锚点 ENU 内容根 (精细编辑区落到地球表面); 否则挂世界根。
    if (auto root = ctx.contentRoot()) root->addChild(terrain_mt_);
    loaded_ = true;

    gen_seen_ = terrain_->nodeGeneration();
    ctx.requestCompile();
    spdlog::info("TerrainLayerFeature: terrain attached ({}x{}, {} m, offset {})",
                 terrain_->grid(), terrain_->grid(), terrain_->extent(), offset_);
}

void TerrainLayerFeature::onDetach(ViewportContext& ctx)
{
    ctx.setTerrainQuery(nullptr);
    query_.reset();
    if (loaded_ && terrain_mt_) ctx.removeFromWorld(terrain_mt_);
    loaded_ = false;
    terrain_mt_ = {};
    terrain_.reset();
    ctx_ = nullptr;
}

void TerrainLayerFeature::onFrame(ViewportContext& ctx, const FrameTick& tick)
{
    (void)tick;
    if (!terrain_) return;
    terrain_->flushPending();
    // 网格分辨率等改变 → 新顶点数组 → 需要增量编译。
    if (gen_seen_ != terrain_->nodeGeneration()) {
        gen_seen_ = terrain_->nodeGeneration();
        ctx.requestCompile();
    }
}

void TerrainLayerFeature::configure(const nlohmann::json& params)
{
    if (!params.is_object()) return;
    if (params.contains("auto_generate"))
        auto_generate_ = params.value("auto_generate", true);
    if (params.contains("centered"))
        centered_ = params.value("centered", true);
    params_.update(params);
    if (attached() && terrain_) {
        applyParams();
        if (terrain_mt_)
            terrain_mt_->matrix = vsg::translate(-offset_, 0.0, -offset_);
    }
}

// ---------------------------------------------------------------------------
// 参数应用
// ---------------------------------------------------------------------------
void TerrainLayerFeature::applyParams()
{
    if (!terrain_) return;
    if (params_.contains("grid"))
        terrain_->setGrid(params_.value("grid", uint32_t(256)));
    if (params_.contains("extent"))
        terrain_->setExtent(params_.value("extent", 1000.0));
    if (params_.contains("height_scale"))
        terrain_->setHeightScale(params_.value("height_scale", 60.0));
    if (params_.contains("seed"))
        terrain_->setSeed(params_.value("seed", uint32_t(0)));

    const auto tt = TerrainType(params_.value("template", int(TerrainType::Plains)));
    terrain_->generate(tt);
    const auto wt = WaterType(params_.value("water", int(WaterType::Lake)));
    terrain_->applyWater(wt);
    if (params_.contains("water_level"))
        terrain_->setWaterLevel(params_.value("water_level", terrain_->waterLevel()));

    recenter();
    ++edit_version_;   // 高度场已重建
}

void TerrainLayerFeature::recenter()
{
    // 地形居中: 局部 [0, extent] → 世界 [-extent/2, +extent/2]。
    // 场景设计/仿真预览 (centered=false) 保持局部坐标, 与实体位置同系。
    offset_ = (centered_ && terrain_) ? terrain_->extent() * 0.5 : 0.0;
    if (terrain_mt_)
        terrain_mt_->matrix = vsg::translate(-offset_, 0.0, -offset_);
}

// ---------------------------------------------------------------------------
// 高度图装载 (场景模板地形)
// ---------------------------------------------------------------------------
bool TerrainLayerFeature::loadHeightmapFile(const std::string& ter_path)
{
    auto t = std::make_unique<TerrainSystem>();
    if (!t->loadHeightmap(ter_path)) {
        spdlog::warn("TerrainLayerFeature: heightmap load failed '{}'", ter_path);
        return false;
    }
    terrain_ = std::move(t);
    if (!terrain_mt_) terrain_mt_ = vsg::MatrixTransform::create();
    terrain_mt_->children.clear();
    terrain_mt_->addChild(terrain_->node());
    recenter();

    if (ctx_) {
        if (auto root = ctx_->contentRoot()) {
            auto& kids = root->children;
            if (std::find(kids.begin(), kids.end(),
                          vsg::ref_ptr<vsg::Node>(terrain_mt_)) == kids.end())
                root->addChild(terrain_mt_);
        }
        ctx_->requestCompile();
    }
    loaded_ = true;
    gen_seen_ = terrain_->nodeGeneration();
    ++edit_version_;
    spdlog::info("TerrainLayerFeature: heightmap loaded '{}' ({}x{}, offset {})",
                 ter_path, terrain_->grid(), terrain_->grid(), offset_);
    return true;
}

void TerrainLayerFeature::clearTerrain()
{
    if (!loaded_) { terrain_.reset(); return; }
    if (ctx_) ctx_->removeFromWorld(terrain_mt_);
    if (terrain_mt_) terrain_mt_->children.clear();
    terrain_.reset();
    loaded_ = false;
    ++edit_version_;
}

VSGVP_REGISTER_FEATURE(TerrainLayerFeature);

}} // namespace vsgraysim::viewport3d
