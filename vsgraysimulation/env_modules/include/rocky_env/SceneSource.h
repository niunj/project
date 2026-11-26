#pragma once
#include <array>
/// @file SceneSource.h —— 多传感器仿真渲染: 场景数据源抽象 (设计文档 §3/§14)。
///
/// 职责 (设计文档 §14 gap#1 场景数据源升级):
///   把场景 JSON (workspace/scenes/*.scenario.json) 解析为场景对象描述
///   (几何 + ENU 变换 + 热属性面组), 并构建 VSG 热场景图 —— 观察口与传感器
///   共享同一几何 (共享资产层 "数据源唯一" 原则)。
///
/// 顶点格式与 createThermalScene() 完全一致:
///   binding0 pos (vec3, ENU 米, Y-up) + binding1 props (vec2: T_kelvin, emissivity)
///   → sensor_rad.vert/frag 零改动即可消费场景驱动几何。
///   binding2 mat_idx (uint32, B2): RT 材质表索引 (radar_x_params) —— 光栅管线
///   不声明该 binding (缓冲绑而不读, 合法), BandRT rchit/B6 消费。
///
/// 首步范围约定 (精确实装项见设计文档 §14.3):
///   - WGS84→ENU: geo_anchor 锚点 + objects[].platform.position_geo
///     (纯数学 GeoFrame 变换, 不依赖 rocky 运行时);
///   - 热属性: face_groups[].lwir_params{base_temp[K], emissivity} +
///     thermal.parts[]{temp_k, face_group} 覆盖;
///   - 网格: 由调用方注入 MeshLoader (vsgXchange assimp), 库本体零 vsgXchange 依赖;
///     面组温度按 face_range/face_count 比例映射 (assimp 不保证 OBJ 面序,
///     精确面组烘焙属场景设计侧职责);
///   - 地面: 三级链 —— TerrainProvider (.vsgterrain) > rocky 地表高程网格
///     (GeoElevationQuery 桥) > 常高平面兜底; 温度取 terrain 对象面组;
///   - 旋转: 首步不应用 (rotation_deg 预留)。
#ifndef ROCKY_ENV_SCENE_SOURCE_H
#define ROCKY_ENV_SCENE_SOURCE_H

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vsg {
class Node;
}

namespace rockyenv {
namespace sensor {

// ===========================================================================
// 场景描述结构 (场景 JSON → 传感器渲染投影)
// ===========================================================================

/// WGS84 锚点 (场景 geo_anchor 同名字段)。
/// 公共头不引入 GeoFrame.h (gui 侧共享头), ENU 换算在 SceneSource.cpp 内完成。
struct GeoAnchorSpec {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double alt_m = 0.0;
    double heading_deg = 0.0;
    bool valid = false;
};

/// 雷达 X 波段材质 (objects[].face_groups[].radar_x_params 同名映射;
/// B2 进 RT 材质表随场景下发, B6 回波链散射体材质消费)。
/// 谱批次扩展 (光谱缺口补齐 2026-09): 谱属性字段追加在尾部 —— 雷达 SSBO
/// 打包为 3 vec4/材质 (批次4 含介电模型), 追加字段不破坏既有索引语义。
struct RadarMaterial {
    int rcs_lut_id = -1;        ///< RCS 查找表编号 (无雷达参数 = -1)
    float dielectric_real = 0.0f;   ///< 复介电常数实部 (锚点值; 模型 1/3 为参考点值)
    float dielectric_imag = 0.0f;   ///< 复介电常数虚部
    float sigma0_db = -20.0f;   ///< 面元后向散射系数 (dB, 地形类面组)
    float surface_rough = 0.5f; ///< 表面粗糙度 [0,1]
    // ---- 批次4: 介电模型 (雷达多频段材质) ----
    /// 0=固定 (JSON 值原样) / 1=海水 Debye 弛豫 (εr(f) 物理式, 锚点值忽略) /
    /// 2=金属 (|Γ|²=1, 反相) / 3=陆地频变锚点 (f_ref=10GHz 幂律缩放)
    int dielectric_model = 0;
    // ---- 谱材质 (批次1): 0/负 = 未指定 → 谱 SSBO 打包时推导 ----
    float emissivity = 0.0f;    ///< 单值发射率镜像 (由 ThermalGroup/Ground 同步;
                                ///< Kirchhoff 推导 ρ_band=1-ε 的基准)
    float rho_tv = -1.0f;       ///< TV 反照率; negative means unspecified, zero is physical
    float rho_swir = -1.0f;
    float eps_mwir = -1.0f;
    float eps_lwir = -1.0f;
    float spec_strength = 0.0f; ///< 镜面强度 (0=朗伯; 水面 1.0 → 太阳耀斑)
    float spec_roughness = 0.5f;///< GGX 粗糙度 (spec_strength>0 时生效)
    bool terrain = false;      ///< Scene ground, independent of scattering material overrides
    // [enabled, solar response K, time constant hours, wet cooling K]
    std::array<float,4> thermal_surface{};
    // [steady temperature rise K, start s, run time s, time constant s]
    std::array<float,4> thermal_source{};
};

/// 热面组 (objects[].face_groups[].lwir_params + radar_x_params 同名映射)。
struct ThermalGroup {
    std::string group_id;
    uint32_t face_begin = 0;    ///< face_range 起点 (面索引, 0 基)
    uint32_t face_end = 0;      ///< face_range 终点 (闭区间)
    float temp_k = 300.0f;      ///< base_temp / base_temp_k (K); part 命中时为 part 值
    float emissivity = 0.90f;   ///< lwir_params.emissivity
    RadarMaterial radar;        ///< radar_x_params (缺省 rcs_lut_id=-1)
    bool part_override = false; ///< P4: 被 thermal.parts[] 按名覆盖 (part_override 标记)
};

/// 场景对象描述 (objects[] 元素的传感器渲染投影)。
struct SceneObjectDesc {
    std::string object_id;
    std::string faction;                 ///< red|blue|neutral (显控阵营色数据源)
    std::string type;                    ///< missile|uav|tank|... (程序化盒体尺寸推断)
    std::string mesh_path;               ///< 已解析绝对路径 (空 = 程序化几何)
    uint32_t face_count = 0;             ///< 声称面数 (face_range 归一化基准)
    vsg::dvec3 position_enu{0.0, 0.0, 0.0};  ///< ENU 米 (X=East, Y=Up, Z=-North)
    vsg::dvec3 rotation_deg{0.0, 0.0, 0.0};  ///< pitch/heading/roll degrees
    double scale = 1.0;
    bool from_geo = false;               ///< 位置来自 WGS84 position_geo 换算
    std::vector<ThermalGroup> thermal_groups;
    /// P4 工况温度倍率表 (objects[].thermal.states, JSON 对象 {"名": 系数})。
    /// --thermal-state 命中 → 该对象全部温度 (面组+兜底) × 系数。
    std::map<std::string, float> thermal_states;
    float fallback_temp_k = 300.0f;      ///< 无面组命中时的对象温度 (thermal.base_temp_k)
    float fallback_emissivity = 0.90f;
    RadarMaterial fallback_radar;        ///< 无面组命中时的雷达材质兜底

    /// 地形对象判定: 新 schema 以 type=="terrain" 为准; 兼容旧场景
    /// (terrain_ref=OBJ 时代) 的 mesh_path == terrain_ref 匹配。
    bool isTerrain(const std::string& terrain_ref) const {
        return type == "terrain" ||
               (!terrain_ref.empty() && mesh_path == terrain_ref);
    }
};

/// 地面材质描述 (传感器地面语义: 温度/发射率/雷达散射; 取 terrain 对象首面组)。
/// 几何由地形链提供: TerrainProvider (.vsgterrain) > rocky 地表高程网格
/// (GeoElevationQuery 桥) > 常高平面兜底 (范围随对象分布自适应)。
struct GroundDesc {
    double height = 0.0;         ///< 平面兜底高度 (m, ENU)
    float temp_k = 290.0f;       ///< terrain 对象面组温度, 缺省 290K
    float emissivity = 0.92f;
    RadarMaterial radar;         ///< terrain 面组 radar_x_params (sigma0_db/surface_rough)
};

/// 扫描机构配置 (sensors.sensor_list[].scan, §7.2 指向时序;
/// B1 只解析存档, GimbalModel 消费见 B7)。
struct ScanSpec {
    std::string mode = "fixed";          ///< fixed|raster|sector|circular|rosette|conical|spiral|slaved
    double az_center = 0.0;              ///< 扫描中心方位 (°, 机体系)
    double el_center = 0.0;              ///< 扫描中心俯仰 (°)
    double az_extent = 0.0;              ///< 方位扫描范围 (°)
    double el_extent = 0.0;              ///< 俯仰扫描范围 (°)
    double az_rate = 0.0;                ///< 方位扫描速率 (°/s; circular 为 rpm)
    double el_rate = 0.0;                ///< 俯仰扫描速率 (°/s)
    std::string target_ref;              ///< slaved 指向目标 (objects[].object_id)
};

/// 传感器描述 (sensors.sensor_list[] 元素的渲染投影, §14.3)。
/// 新 schema (frame/profile/stages/scan) 与旧字段 (type/band/tx_params/
/// aperture/signal_params) 双兼容: 新字段缺省时按 A1 同规则推导。
struct SensorDesc {
    int sensor_id = 0;
    std::string name;                    ///< 显式名; 缺省 "sensor_<id>"
    std::string type;                    ///< 原始类型: radar|sar|ir|vis|laser
    std::string band;                    ///< 原始波段: X / MWIR / ...
    std::string role;                    ///< search | guidance
    std::string platform;                ///< platform_object → objects[].object_id
    std::string faction;                 ///< 随平台归属解析 (未绑定则空)
    // ---- frame (§14.3) ----
    std::string frame_kind;              ///< VIS|SWIR|MWIR|LWIR|RADAR (空=未映射, 如 laser)
    double lambda_min = 0.0;             ///< (m); 0 = 按 kind 推导
    double lambda_max = 0.0;
    uint32_t res_w = 640, res_h = 512;
    double fov_deg = 0.0;                ///< 水平视场; 0 = 回退 aperture.fov_horizontal
    double rate_hz = 0.0;                ///< 成像帧率 / 雷达 PRF; 0 = 按 kind 推导
    // ---- profile.optics / detector / electronics (SensorApi.h 同名映射) ----
    double f_number = 4.0, transmission = 0.85, psf_sigma_px = 0.9, vignetting = 0.15;
    double pixel_pitch_um = 15.0, qe = 0.7, integration_ms = 8.0;
    double netd_mk = 50.0, well_ke = 20000.0, read_noise_e = 80.0;
    uint32_t bits = 14;
    double agc_low_pct = 0.002, agc_high_pct = 0.998, gamma = 1.0;
    // ---- aperture (指向初值, 机体系; §7.2 scan 的起始中心) ----
    vsg::dvec3 aperture_offset{0.0, 0.0, 0.0}; ///< 平台机体系安装偏移 (m)
    vsg::dvec3 direction{0.0, 0.0, -1.0};      ///< 初始指向 (机体系)
    double fov_h = 0.0, fov_v = 0.0;           ///< 旧 aperture FOV (°)
    double az_deg = 0.0, el_deg = 0.0;         ///< 角度形式初值 (°)
    double range_m = 8000.0;
    // ---- tx_params / 雷达参数 (B6 回波链) ----
    double power = 1000.0;
    double beam_width = 0.05;            ///< (rad)
    double wavelength = 0.03;            ///< (m)
    double pulse_rate = 1000.0;          ///< PRF (Hz)
    double duty_cycle = 0.05;
    uint32_t cpi_pulses = 128;           ///< 一帧 CPI 脉冲数 (B4 相干帧节拍)
    // ---- 发布级 (B5 分发时序) ----
    std::vector<std::string> stages;     ///< scene/pupil/focal/raw/image 或雷达五段
    ScanSpec scan;                       ///< §7.2 扫描机构
};

/// Parse the same sensor contract used by the viewer, without loading geometry.
std::vector<SensorDesc> parseSensorDescriptions(const std::string& scenario_json);

/// 海面场景描述 (顶层 ocean 段; 批次3 海面进 RT + 海洋真值)。
struct OceanDesc {
    bool enabled = false;
    float sea_temp_k = 291.0f;   ///< 海表温度 (K; EnvAnswer.sea_surface_temp_k 同源)
    float sea_level = 0.0f;      ///< 海平面 ENU 高度 (m)
    float extent = 0.0f;         ///< 海面网格边长 (m); 0 = 随场景对象分布自适应
    float fft_patch_size = 3200.0f; ///< 周期波场边长 (m, 有限正数；与 extent 独立)
};

/// 已解析场景 (SceneDataSource 输出)。
struct SceneData {
    GeoAnchorSpec anchor;        ///< ENU 锚点 (WGS84); 无 geo_anchor 时 valid=false
    /// 场景地形引用 (原始值): 地形工程 (.ter/.vsgterrain) | rocky 地形包
    /// (.map.json) | 空 = 仅 rocky 粗底图。OBJ 地表模型走 objects[] (type=terrain)。
    std::string terrain_ref;
    std::string scene_name;
    std::vector<SceneObjectDesc> objects;
    std::vector<SensorDesc> sensors;  ///< 场景驱动传感器注册表 (B1)
    GroundDesc ground;
    OceanDesc ocean;             ///< 批次3: 海面场景声明 (enabled=false = 无海洋)
    /// P4: 顶层缺省工况名 (JSON "thermal_state"; CLI --thermal-state 优先于此)
    std::string default_thermal_state;
    /// 统计 (日志/验证)
    uint32_t mesh_count = 0;         ///< 声明网格的对象数
    uint32_t geo_count = 0;          ///< 位置来自 position_geo 换算的对象数
    /// 首个非地形对象 (传感器瞄准目标; 无则 nullptr)
    const SceneObjectDesc* primaryTarget(const std::string& terrain_ref) const;
    /// 按 object_id 查找对象 (传感器平台绑定; 无则 nullptr)
    const SceneObjectDesc* findObject(const std::string& object_id) const;
};

/// 场景数据源抽象 (设计文档 §3 SceneGeometry "数据源唯一")。
/// 实现一: JsonSceneDataSource (scenario.json 静态驱动);
/// 未来: 仿真运行态源 (StateRingBuffer 实时位姿/温度场)。
class SceneDataSource {
public:
    virtual ~SceneDataSource() = default;
    struct Result {
        bool ok = false;
        std::string error;
    };
    /// 解析场景 (纯 CPU, 无 GPU/窗口依赖)
    virtual Result load() = 0;
    virtual const SceneData& data() const = 0;
};

/// JSON 场景数据源 (workspace/scenes/*.scenario.json, §14.3 schema)。
/// mesh_path 按 依次解析: 原样(CWD) / 场景文件目录 / 其上级 / 上两级。
class JsonSceneDataSource : public SceneDataSource {
public:
    explicit JsonSceneDataSource(std::string path);
    Result load() override;
    const SceneData& data() const override { return data_; }
    SceneData& data_mut() { return data_; }   ///< 宿主覆盖 (工况/锚点等) 用可变访问

private:
    std::string path_;
    SceneData data_;
};

// ===========================================================================
// 几何构建 (描述 → VSG 图)
// ===========================================================================

/// 材质槽信息 (TV 彩色批次: map_Kd 贴图 + Kd 漫反射色)。
struct MeshSlotInfo {
    std::string map_kd;    ///< map_Kd 路径 token (空 = 无贴图, 用 Kd 纯色)
    vsg::vec4 kd = vsg::vec4(0.7f, 0.7f, 0.7f, 1.0f);   ///< Kd 漫反射色
};

/// 网格加载结果: 三角形汤 + 可选 uv/贴图槽通道 (TV 彩色批次)。
/// uvs 与 vertices 同序 (逐三角形 3 顶点各一 uv); tri_slots 逐三角形给出
/// 材质槽号 (索引 slots, 与 objloader 材质桶 VID 序一致)。
struct MeshLoadResult {
    std::vector<vsg::vec3> vertices;   ///< 三角形汤 (每三角形 3 顶点, 网格局部系)
    std::vector<vsg::vec2> uvs;        ///< 逐顶点 uv (与 vertices 等长; 空 = 无 uv)
    std::vector<uint32_t> tri_slots;   ///< 逐三角形贴图槽号 (空 = 全 0 无贴图)
    std::vector<MeshSlotInfo> slots;   ///< 材质槽表 (VID 序; 与 tri_slots 配套)
};

/// 网格加载回调: 绝对路径 → MeshLoadResult。
/// 由调用方注入 (sensor_viewer 用 vsg 内建/vsgXchange/loadObjNode 三级链),
/// 库本体不依赖 vsgXchange。uv/tri_slots 为空 = 网格无贴图资产 (旧行为)。
using MeshLoader = std::function<bool(const std::string& abs_path,
                                      MeshLoadResult& result)>;

/// P3 地形网格数据 (TerrainProvider 输出; ENU Y-up, 三角形汤)。
/// temp_k/emissivity ≤ 0 → 回退 data.ground 缺省。
struct TerrainMeshData {
    std::vector<vsg::vec3> vertices;   ///< 三角形汤 (ENU 米, 已含高程)
    float temp_k = 0.0f;               ///< 地形温度 (K; 0 = 用场景 ground 缺省)
    float emissivity = 0.0f;           ///< 地形发射率 (0 = 用场景 ground 缺省)
    // ---- 批次3: 逐顶点热属性 (空/≤0 → 回退整网格 temp_k/emissivity) ----
    std::vector<float> vertex_temp_k;      ///< 逐顶点温度 (K, 与 vertices 等长)
    std::vector<float> vertex_emissivity;  ///< 逐顶点发射率 (与 vertices 等长)
};

/// P3 地形注入回调: 由调用方提供真实地形三角形 (如 .vsgterrain →
/// TerrainSystem 高度场 → 三角形汤)。返回 false/空 → 平面兜底。
using TerrainProvider = std::function<bool(TerrainMeshData&)>;

/// B1 贴图亮度回调: map_Kd 路径 → 平均亮度 (0..1); <0 = 未知 (该贴图不调制)。
/// 由宿主注入 (库本体零 vsgXchange/贴图解码依赖, 与 MeshLoader 同原则)。
using TextureLuminance = std::function<float(const std::string& map_kd)>;

/// 批次3 海面网格数据 (OceanProvider 输出; ENU Y-up, 三角形汤, 相位冻结)。
struct OceanMeshData {
    std::vector<vsg::vec3> vertices;       ///< 波面三角形汤 (ENU 米, 含波高)
    std::vector<float> vertex_temp_k;      ///< 逐顶点温度 (空/≤0 → sea_temp_k)
    std::vector<float> vertex_emissivity;  ///< 逐顶点发射率 (空/≤0 → 0.98 水面缺省)
    std::vector<uint8_t> face_flags;       ///< 逐三角形: 0=水面 1=白沫 (空 = 全水面)
    float sea_temp_k = 291.0f;             ///< 海表温度 (K)
    RadarMaterial water_material;          ///< 水面材质 (谱反照率/镜面 → 太阳耀斑)
    RadarMaterial foam_material;           ///< 白沫材质 (发射率/反照率全 0 = 复用水面)
};

/// 批次3 海面注入回调: IOceanSampler 波面真值 → RT 三角形汤 (传感器链
/// 海面背景)。返回 false/空 → 场景无海面 (既有行为)。
using OceanProvider = std::function<bool(OceanMeshData&)>;

/// rocky 地表高程查询桥 (WGS84 度/米): (lat_deg, lon_deg) → 椭球高 elev_m。
/// 宿主注入 (gui 侧 RockyMapFeature::IGeoTerrainQuery::groundElevation 的
/// lambda 包装; env_modules 不依赖 gui 头)。未注入/不可用 → 地面回退常高平面。
using GeoElevationQuery = std::function<bool(double lat_deg, double lon_deg,
                                             double& elev_m)>;

/// 从 VSG 场景图抽取三角形汤 (应用 MatrixTransform 链; 逐三角形 3 顶点)。
/// 供 MeshLoader 实现复用 (vsg 内建/vsgXchange/loadObjNode 产物 → 三角形汤)。
/// out_uv: 与 out_tri_vertices 同序的逐顶点 uv (VID 数组中的 vec2Array, 可空);
/// out_tri_slot: 逐三角形 VID 槽号 (objloader 材质桶序, 可空)。
bool flattenMeshToTriangles(vsg::ref_ptr<vsg::Node> node,
                            std::vector<vsg::vec3>& out_tri_vertices,
                            std::vector<vsg::vec2>* out_uv = nullptr,
                            std::vector<uint32_t>* out_tri_slot = nullptr);

/// 构建结果: 场景图 + ENU 包围盒 (相机自适应) + RT 材质表 (B2)。
struct ThermalScene {
    vsg::ref_ptr<vsg::Node> node;
    vsg::dvec3 bound_min{0.0, 0.0, 0.0};
    vsg::dvec3 bound_max{0.0, 0.0, 0.0};
    bool has_bounds = false;
    uint32_t triangle_count = 0;
    /// RT 材质表 (索引 = 顶点 binding2 材质索引): [0] = 地形, 其后按
    /// (对象, 面组) 去重分配; BandRT 下发 SSBO, rchit/B6 按 matIdx 取用。
    std::vector<RadarMaterial> materials;
    /// 贴图槽表 (TV 彩色批次: 顶点 binding4 槽号索引本表)。[0] = 无贴图缺省,
    /// 其后按 map_kd 路径跨网格去重; 空 = 场景无贴图资产。
    /// map_kd 路径由调用方 (A3 SharedSceneResources) 加载进 texture2DArray。
    std::vector<MeshSlotInfo> tex_slots;
};

/// 构建 VSG 热场景图 (观察口与传感器口共享, 画家序: 地面→海面→对象)。
///   mesh 对象: loader 成功 → 逐三角形面组温度映射; 失败 → 程序化盒体回退;
///   程序化对象: 按 type 尺寸盒体 (首面组温度, 其余面组仅网格有意义);
///   地面: terrainProvider 命中 → 真实地形汤; 否则 elevationQuery + geo_anchor
///   有效 → rocky 地表高程网格 (锚点 ENU 平面 + 逐顶点真实高程);
///   均不可用 → 常高平面兜底 (范围随对象分布自适应, 无真实高程/遮挡);
///   海面 (批次3): oceanProvider 命中 → 波面三角形汤 (白沫分面组, 相位冻结);
///   工况 (P4): thermalState 非空且对象 thermal_states 命中 → 温度 × 倍率
///   (CLI > 场景 JSON 顶层 > base, 调用方解析优先级后传入)。
/// B1 贴图亮度升温: textureLuminance 有效且 solarHeatK>0 时, 有贴图的面按
///   T_baked = T + (1-亮度)·solarHeatK 烘焙 (暗面白天吸热更热; 同一温度
///   面组内按贴图内容分出温度层次, 红外细节来源)。无贴图面不调制。
/// 索引 uint32 (支持大网格; createThermalScene 的 ushort 仅小场景适用)。
ThermalScene buildThermalSceneGraph(const SceneData& data, MeshLoader loader = {},
                                    TerrainProvider terrainProvider = {},
                                    const std::string& thermalState = {},
                                    OceanProvider oceanProvider = {},
                                    TextureLuminance textureLuminance = {},
                                    float solarHeatK = 0.0f,
                                    GeoElevationQuery elevationQuery = {});

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_SCENE_SOURCE_H
