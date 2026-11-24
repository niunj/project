#pragma once
/// @file SharedSceneResources.h —— P0 场景级资源池 (多传感器并行架构改造 P0)。
///
/// 解决 N 链共享同一场景时的 O(N) 资源膨胀: BandRT 与 RadarChain 原本各自
/// 抽取几何 + 上传 SSBO + 构建 BLAS/TLAS; 池化后全场景一份, O(N)→O(1)。
///
/// 三块内容:
///   P0.1 几何/BLAS/TLAS: 场景 VertexIndexDraw 契约 (binding0 pos / 1 props /
///        2 mat_idx) 一次性抽取为 SSBO BufferInfo (posFlat/索引/材质索引/材质表),
///        单 BLAS + 单实例 TLAS (恒等变换, 视角由 rgen push 下发, 静态场景无需
///        per-view TLAS)。BufferInfo 首次 compile 时分配上传, 两链描述符引用
///        同一 BufferInfo → 同一 VkBuffer (vsg 1.1.15 DescriptorBuffer::compile
///        对 buffer!=nullptr 的 BufferInfo 跳过重建, 已核实源码)。
///   P0.2 LUT/sampler: clamp+linear sampler 共享; 波段出射度 LUT 纹理按
///        (tMin/tMax/表内容) 缓存去重, 同波段多实例只建一份 device 纹理。
///   P0.3 环境 UBO: [σ/km, 天空辐亮度, 直射辐照度, night] 全链一份, setEnv
///        数值比较去重 (同帧多次调用仅一次 dirty) → 每帧一次上传。
///
/// 所有权与分层: env_modules 不依赖上层 —— 池由上层 (sensor_viewer / 仿真控制)
/// 创建并保证生命周期覆盖全部使用链, 以裸指针引用传递 (同 MeshLoader/
/// TerrainProvider 注入模式)。池未就绪 (build 失败/未调用) 时, 各链回退
/// 单实例抽取现行为 (迁移期双路径, 单实例零回归)。

#include <rocky_env/SensorApi.h>      // BandRadianceLut, EnvAnswer
#include <rocky_env/SceneSource.h>    // RadarMaterial
#include <rocky_env/DynamicScene.h>
#include <rocky_env/VoxelWeather.h>

#include <vsg/core/ref_ptr.h>
#include <vsg/core/Array.h>
#include <vsg/state/BufferInfo.h>
#include <vsg/state/Sampler.h>
#include <vsg/nodes/Node.h>
#include <vsg/raytracing/TopLevelAccelerationStructure.h>

#include <string>
#include <vector>

namespace vsg {
class ImageView;
class Window;
class Device;
class Data;
}

namespace rockyenv {
namespace sensor {

class SharedSceneResources {
public:
    std::shared_ptr<CloudSceneBinding> voxelCloud;
    struct Result {
        bool ok = false;
        std::string error;
    };

    /// TV 彩色批次: 贴图数组规格 (工具侧打包, 库侧只做 Vk 组装)。
    ///   layers    每层 RGBA8 数据 (尺寸一致, 通常 256×256; 层 i ↔ 槽 i+1;
    ///             加载失败的槽由工具侧填 Kd 色占位层, 保证 层=槽-1 恒成立)
    ///   slotData  每槽 (Kd.rgb, hasTex); 槽 0 = 无贴图缺省; hasTex=0 → shader
    ///             走 Kd 纯色分支 (不采样数组)
    struct TexArraySpec {
        std::vector<vsg::ref_ptr<vsg::Data>> layers;
        std::vector<vsg::vec4> slotData;
    };

    /// 一次性抽取场景几何 + 建立共享资源 (BLAS/TLAS 实际构建发生在
    /// viewer->compile(), 由描述符 compile 触发, 幂等)。
    ///   window     用于取 device (BLAS/TLAS 构造需要)
    ///   scene      热场景图 (单 VertexIndexDraw 契约, 与 BandRT/RadarChain 同)
    ///   materials  RT 材质表 (空 → 单一缺省地形材质, 同 RadarChain 现行为)
    ///   texSpec    TV 彩色贴图数组规格 (可空 = 无贴图资产, 旧行为)
    Result build(vsg::Window& window,
                 vsg::ref_ptr<vsg::Node> scene,
                 const std::vector<RadarMaterial>* materials,
                 const TexArraySpec* texSpec = nullptr);
    Result build(vsg::Device& device,
                 vsg::ref_ptr<vsg::Node> scene,
                 const std::vector<RadarMaterial>* materials,
                 const TexArraySpec* texSpec = nullptr);
    Result buildInstances(vsg::Device& device,const std::vector<SceneInstance>& instances,
                          const std::vector<RadarMaterial>& materials,const TexArraySpec* textures=nullptr);
    void setTransforms(const std::vector<vsg::dmat4>& transforms,const std::vector<bool>& visible);
    vsg::ref_ptr<vsg::VertexIndexDraw> sceneGeometry() const { return sceneGeometry_; }
    vsg::ref_ptr<vsg::Commands> sceneUpdates;
    vsg::ref_ptr<vsg::BufferInfo> bvhInfo,primitiveInfo,instanceInfo;
    uint32_t instanceCount=0;

    // ---- P0.1 几何抽取产物 (SSBO BufferInfo, 两链描述符共享) ----
    vsg::ref_ptr<vsg::BufferInfo> posFlatInfo;    ///< 平铺 pos (std430 vec3 16B stride 规避)
    vsg::ref_ptr<vsg::BufferInfo> indicesInfo;    ///< uint 索引 (ushort 运行时提升)
    vsg::ref_ptr<vsg::BufferInfo> matIdxInfo;     ///< 材质索引 (缺失 → 全 0)
    vsg::ref_ptr<vsg::BufferInfo> matDataInfo;    ///< 材质表 (每材质 2 vec4: 介电/σ0/粗糙/rcs_lut_id)
    /// 谱材质表 (批次1, 每材质 2 vec4): a=(ρ_tv,ρ_swir,ε_mwir,ε_lwir) b=(镜面强度,粗糙度,0,0)
    /// band_shade.comp binding 5 消费; 未指定字段按 Kirchhoff (ρ=1-ε) / 单值 ε 推导。
    vsg::ref_ptr<vsg::BufferInfo> spectralDataInfo;
    vsg::ref_ptr<vsg::BufferInfo> propsInfo;      ///< props SSBO (T_kelvin, emissivity) 直传
    // ---- TV 彩色批次: uv/贴图槽通道 (band_shade.comp bindings 10-12 消费) ----
    vsg::ref_ptr<vsg::BufferInfo> uvFlatInfo;     ///< 平铺 uv (vec2/顶点, 直传或零填充)
    vsg::ref_ptr<vsg::BufferInfo> slotInfo;       ///< 逐顶点贴图槽号 (uint/顶点)
    vsg::ref_ptr<vsg::BufferInfo> texSlotDataInfo;///< 槽表 (Kd.rgb, hasTex) — 槽0=缺省
    vsg::ref_ptr<vsg::ImageView> texArrayView;    ///< texture2DArray (层 = 槽-1; 空 = 无贴图)
    uint32_t texLayerCount = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t materialCount = 0;

    // ---- P0.1 加速结构 (单 BLAS + 单实例 TLAS, 恒等变换) ----
    vsg::ref_ptr<vsg::TopLevelAccelerationStructure> tlas;

    // ---- P0.2 sampler / LUT 缓存 ----
    vsg::ref_ptr<vsg::Sampler> sampler;           ///< clamp + linear (全链共享)
    /// 波段出射度 LUT 纹理: 同 (tMin/tMax/表内容) 只建一份 device 纹理。
    vsg::ref_ptr<vsg::ImageView> lutView(const BandRadianceLut& lut);

    // ---- P0.3 共享环境 UBO (每帧一次上传) ----
    /// [σ/km, sky, Esun_band, 0, sunDir.x, sunDir.y, sunDir.z, 0] (8 float;
    /// 批次1 扩容: 后 4 float 为太阳 ENU 单位指向, band_shade 太阳反射消费)
    vsg::ref_ptr<vsg::BufferInfo> envInfo;
    /// 每帧环境注入: 数值与上次相同则跳过 dirty (多链转发去重, 保证 1 次/帧)。
    void setEnv(const EnvAnswer& env);

    // ---- 状态 / 验收统计 ----
    bool ready() const { return ready_; }
    const std::string& error() const { return error_; }
    struct Stats {
        uint32_t geometryBuilds = 0;   ///< 池 build 次数 (验收: =1)
        uint64_t envSetCalls = 0;      ///< setEnv 转发次数 (含去重前)
        uint64_t envDirtyCount = 0;    ///< 实际触发上传次数 (验收: ≈帧数, 非帧数×链数)
        uint32_t lutTextures = 0;      ///< LUT 纹理构建数 (按波段去重后)
    };
    const Stats& stats() const { return stats_; }
    void printStats() const;

private:
    void installInstances(vsg::Device&,const std::vector<SceneInstance>&);
    vsg::ref_ptr<vsg::ubyteArray> instanceData_;
    vsg::ref_ptr<DynamicTlas> dynamicTlas_;
    vsg::ref_ptr<vsg::VertexIndexDraw> sceneGeometry_;
    bool ready_ = false;
    std::string error_;
    vsg::ref_ptr<vsg::floatArray> envData_;   ///< 共享 env UBO 数据 (DYNAMIC_DATA, 8 float)
    float lastEnv_[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    bool envInitialized_ = false;

    struct LutCacheEntry {
        float tMin = 0.0f, tMax = 0.0f;
        std::vector<float> table;
        vsg::ref_ptr<vsg::ImageView> view;
    };
    std::vector<LutCacheEntry> lutCache_;

    Stats stats_{};
};

} // namespace sensor
} // namespace rockyenv
