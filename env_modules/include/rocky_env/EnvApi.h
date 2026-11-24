#pragma once
/// @file EnvApi.h
/// @brief rocky 环境渲染模块 (sky / cloud / ocean) 的对外接口规范。
///
/// 设计目标: 以 rocky 为地球基座, 在其上提供可独立复用的天空/云层/海洋渲染。
/// 三个模块都只依赖 VSG (VulkanSceneGraph) 与 rocky, 不依赖 Qt / 业务代码,
/// 编译为独立静态库 `vsgraysim_rocky_env`。宿主 (viewport3d 的 Feature) 通过
/// 本头文件的工厂创建模块, 每帧填充 EnvFrame 并调用 onFrame()。
///
/// 坐标约定: 云/海洋的几何工作在锚点 ENU 切平面 (局部米制, +Y 朝上, +X 东,
/// +Z 南), 由宿主通过 EnvFrame::model (ENU→世界/ECEF) 与 view_proj 完成定位;
/// 天空 (rocky::SkyNode) 是地心级节点, 直接工作在 ECEF。
#ifndef ROCKY_ENV_API_H
#define ROCKY_ENV_API_H

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Node.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace vsg {
class Device;
class Window;   // 前向声明 (vsg/app/Window.h; 头文件不暴露宿主窗口类型)
class Command;  // 前向声明 (vsg/commands/Command.h; FFT compute 命令返回类型)
}

namespace rockyenv {

/// 每帧由宿主填充并传给各模块的环境/渲染参数。
struct EnvFrame {
    // ---- 相机/坐标 ----
    vsg::dmat4 view_proj;          ///< projection * view (世界/ECEF → 裁剪)
    vsg::dmat4 model;              ///< ENU 局部 → 世界 (恒为锚点 ENU→ECEF)
    vsg::dvec3 cam_pos_world;      ///< 相机世界 (ECEF) 位置
    vsg::dvec3 cam_pos_enu;        ///< 相机在锚点 ENU 系内的位置 (云环/海面网格跟随用)
    double     time_sec = 0.0;     ///< 帧时间 (秒), 驱动云漂移/波浪相位

    // ---- 时间推进 (Phase 2: 运行时时间) ----
    int        year = 2026;        ///< UTC 年
    int        month = 9;          ///< UTC 月
    int        day = 26;           ///< UTC 日 (默认满月: 月相≈1, 月光链路可验证)
    double     hours_utc = 4.0;    ///< UTC 小时 (支持小数, 如 12.5 = 12:30)
    double     time_scale = 1.0;   ///< 仿真时间倍率 (1.0 = 实时, 2.0 = 2倍速)

    // ---- 天体/气象 (ENU 系, +Y 朝上) ----
    vsg::vec3  sun_dir_enu{0.3f, 0.8f, 0.2f};  ///< 太阳方向 (归一化)
    float      sun_el_deg = 55.0f;             ///< 太阳高度角 (度), 决定光照强度/色温
    vsg::vec3  wind_dir_enu{1.0f, 0.0f, 0.0f}; ///< 水平风方向 (归一化, xz)
    float      wind_speed = 6.0f;              ///< 风速 (m/s), 驱动云漂移/海浪谱
    float      cloud_cover = 0.5f;             ///< 云量 0..1
    float      visibility = 10000.0f;          ///< 能见度 (米), 驱动雾/大气消光
    float      precipitation = 0.0f;           ///< 降水强度 0..1 (0=晴, 1=暴雨)
    float      overcast = -1.f;                ///< <0 legacy configuration; 0..1 timeline blend
    /// 有效云底高度 (米, ENU +Y)。宿主从 ICloudQuery::cloudBase() 取真实值;
    /// 无云时保留默认 (海面云影投影的几何回退, 与旧硬编码一致)。
    float      cloud_base_m = 1200.0f;

    // ---- 天空大气光照 (rocky_sky 每帧由 ILightingProvider 下发; 无天空时为
    //      日间默认值, 保证云/海单独使用时外观与旧硬编码一致) ----
    vsg::vec3  sun_light{1.0f, 1.0f, 1.0f};      ///< 太阳直射色 (暮光变红变暗, 夜≈0)
    float      is_day = 1.0f;                   ///< 昼因子 1..0 (平滑过渡)
    vsg::vec3  moon_dir_enu{0.0f, -1.0f, 0.0f}; ///< 月亮方向 (归一化; 无星历时指向地心下方)
    float      moon_phase = -1.0f;              ///< 月相 0=朔 1=满, <0 = 无星历
    vsg::vec3  ambient_light{0.15f, 0.16f, 0.18f}; ///< 天光环境色 (云底/背光面照明)
    float      detailWeight = 1.f; // local representation opacity
    vsg::vec3  horizon_color{0.72f, 0.78f, 0.84f}; ///< 地平线天色 (距离雾/海天反射真值)
    float      detailRadius = 0.f; // radial overlap footprint (metres); zero disables feather
    vsg::vec3  zenith_color{0.20f, 0.42f, 0.72f};  ///< 天顶天色 (海面天空梯度)
    float      cloudEnvelope = 1.f; // synoptic coverage multiplier
    float      extinction_coeff = 0.0f;          ///< 消光系数 (供着色器雾/消光使用)
};

/// 环境模块统一接口。宿主以 std::unique_ptr<IEnvModule> 持有。
class IEnvModule {
public:
    virtual ~IEnvModule() = default;

    /// 模块名 (调试/日志)。
    virtual const char* name() const = 0;

    /// 挂到场景的根节点。天空模块挂世界根 (ECEF 级); 云/海洋模块挂锚点 ENU
    /// 内容根 (局部米制)。宿主负责 addChild。
    virtual vsg::Node* rootNode() = 0;

    /// 每帧更新 (相机/时间/气象)。宿主在渲染前调用。
    virtual void onFrame(const EnvFrame& f) = 0;

    /// 显隐开关。
    virtual void setEnabled(bool on) = 0;
    virtual bool enabled() const = 0;

    /// 挂载位置: true=世界根 (ECEF 地心级, 如 rocky 地球天空);
    /// false=作业区 ENU 内容根 (云/海)。宿主据此选择挂载点。
    virtual bool mountInWorldRoot() const { return false; }

    /// Bind before compilation to prepare device-local resources on a headless host.
    virtual void setDevice(vsg::Device*) {}

    /// 可选宿主钩子: 注入有效 Vulkan 窗口 (取 device/queue)。
    /// 仅 wave_mode=2 的海洋模块消费 (GPU FFT 波场需 window/device 构建
    /// compute 管线); 其余模块忽略。默认空实现, 向后兼容。
    /// 实现须在收到窗口时同步 build FFT (早于 viewer->compile), 以便
    /// set=1 采样描述符与渲染管线被编译; 不得延迟到 onFrame 才 build。
    virtual void setWindow(vsg::Window* w) { (void)w; }

    /// 可选宿主钩子: FFT compute 命令 (非渲染 RenderGraph, 须在任一 RenderGraph
    /// 之前挂入 CommandGraph —— 渲染 pass 内不允许 layout transition)。
    /// 返回 nullptr = 模块无 compute 负载。与 CommandGraph::addChild 兼容
    /// (Commands 派生自 Command, 宿主按 Command* 接收)。
    virtual vsg::Command* fftCommands() { return nullptr; }
};

/// 天体 (太阳) 方向信息 —— 星历单一真值源的输出。
struct EnvSunInfo {
    vsg::vec3 dirECEF{0.0f, 1.0f, 0.0f};   ///< 指向太阳的单位方向 (ECEF/世界)
    vsg::vec3 dirENU{0.0f, 1.0f, 0.0f};    ///< 指向太阳的单位方向 (锚点 ENU; x=东 y=上 z=南)
    float elevationDeg = 0.0f;             ///< 太阳高度角 (度, 水平=0 天顶=90, 夜为负)
    float azimuthDeg   = 0.0f;             ///< 太阳方位角 (度, 北=0 顺时针)
};

/// 可选能力接口: 模块若由星历驱动太阳 (如 rocky_sky 封装 rocky::SkyNode),
/// 实现本接口。宿主每帧用 EnvFrame::model (ENU→世界) 查询太阳, 作为云/海
/// shader 与平行光的**单一真值源**; 查询失败 (无 rocky/未挂载) 时宿主回退
/// 到手工 sun_el/sun_az 参数。用 dynamic_cast<IEnvModule*> 获取。
class ISunProvider {
public:
    virtual ~ISunProvider() = default;

    /// 同步当前 UTC 时刻 (与 EnvFrame 的 year/month/day/hours_utc 一致)。
    /// 宿主在**每帧查询太阳之前**调用 (通常在云/海 makeFrame 阶段), 保证
    /// 即使特性 onFrame 顺序不固定, 云/海拿到的太阳方向也是本帧时刻的;
    /// 实现内部做脏标记, 时刻未变化时不重复触发 SkyNode 更新。
    virtual void syncDateTime(int year, int month, int day, double hoursUtc) = 0;

    /// 计算已同步时刻、给定 ENU 锚点下的太阳方向。
    /// @param enuToEcef  EnvFrame::model (锚点 ENU→ECEF 刚体矩阵)。
    /// @param out        输出太阳方向/高度角/方位角。
    /// @return true 表示星历可用。
    virtual bool sunInfo(const vsg::dmat4& enuToEcef, EnvSunInfo& out) const = 0;
    virtual bool moonInfo(const vsg::dmat4&, EnvSunInfo&, float&) const { return false; }
    virtual bool solarEnabled() const { return true; }
    virtual bool lunarEnabled() const { return true; }
};

/// 大气/光照物理状态 —— 每帧由天空模块产出的**辐射度真值**。
/// 场景灯光 (平行光/环境光/月光)、云/海雾色与反射、后续红外/可见光传感器
/// 都消费本结构, 取代各自写死颜色/强度。颜色为 LDR 归一化辐照 (晴午直射 ≈1)。
struct AtmosphereState {
    // 太阳
    vsg::vec3 sun_dir_enu{0.0f, 1.0f, 0.0f};  ///< 指向太阳 (ENU, 归一化)
    float     sun_el_deg = 0.0f;              ///< 太阳视高度角 (度, 含蒙气差)
    vsg::vec3 sun_light{1.0f, 0.98f, 0.92f};  ///< 直射太阳灯光颜色 (随大气质量消光变红变暗; 夜≈0)
    float     sun_az_deg = 0.0f;

    // 天光/环境
    vsg::vec3 ambient_light{0.3f, 0.35f, 0.4f};  ///< 环境光 (天光积分近似; 夜≈0)
    float     moon_el_deg = -90.0f;              ///< 月亮高度角 (度)
    vsg::vec3 moon_dir_enu{0.0f, 1.0f, 0.0f};    ///< 指向月亮 (ENU, 归一化)
    float     moon_phase = 0.0f;                     ///< 月面照明比例 0..1 (0=朔 1=望)
    vsg::vec3 moon_light{0.0f, 0.0f, 0.0f};      ///< 月光平行光颜色 (夜/月出时非0)
    float     exposure = 1.0f;

    // 天空辐射色 (云/海距离雾与反射消费, 取代硬编码 0.72,0.78,0.84)
    vsg::vec3 zenith_color{0.20f, 0.42f, 0.72f};
    float     turbidity = 1.0f;
    vsg::vec3 horizon_color{0.72f, 0.78f, 0.84f};
    float     is_day = 1.0f;                     ///< 昼=1 夜=0 (太阳高度平滑过渡)
    
    // P1-2: 能见度/雾联动
    float     extinction_coeff = 0.0f;           ///< 消光系数 (每公里), 由能见度计算
    float     visibility = 10000.0f;             ///< 能见度 (米), 透传供着色器使用
    float     _pad5 = 0.0f;
    float     _pad6 = 0.0f;
};

/// 可选能力接口: 物理光照输出。模块每帧产出 AtmosphereState; 宿主/云/海
/// 用 dynamic_cast<IEnvModule*> 获取后读取 (灯光由模块内部自行驱动场景)。
class ILightingProvider {
public:
    virtual ~ILightingProvider() = default;

    /// 返回最近一帧的大气/光照状态 (onFrame 后有效)。
    /// @return true 表示状态有效。
    virtual bool getAtmosphereState(AtmosphereState& out) const = 0;
};

/// 可选能力接口: 大气路径查询 (仿真侧消费)。
/// 任意两点 (世界/ECEF) 之间的视线大气透过率与路径辐射,
/// 供红外/可见光传感器链路的大气衰减与背景辐射使用。颜色为 LDR 归一化值,
/// 晴午直射 ≈1。太阳方向/大气状态取最近一帧的星历真值。
class IAtmosphereQuery {
public:
    virtual ~IAtmosphereQuery() = default;

    /// 计算两点间视线路径的大气透过率与单次散射路径辐射。
    /// @param fromECEF  起点世界坐标 (ECEF)。
    /// @param toECEF    终点世界坐标。
    /// @param tau       输出: 谱透过率 (0..1, RGB)。
    /// @param pathRadiance 输出: 路径辐射 (RGB, LDR 归一化)。
    /// @return true 表示查询成功 (模块已 onFrame 且星历可用)。
    virtual bool transmittance(const vsg::dvec3& fromECEF, const vsg::dvec3& toECEF,
                               vsg::vec3& tau, vsg::vec3& pathRadiance) const = 0;

    /// 批次2 (谱大气): 波段化天空查询 —— 打通"天空谱 ↔ 传感器链"的波段契约。
    /// 返回给定波段 + 视线仰角的**晴空**真值 (云调制由消费侧叠加):
    ///   L_sky  波段天空辐亮度 (W/m²/sr, 方向相关: 散射波段随气团增亮;
    ///          热波段=大气柱发射, 天顶暗/地平趋近黑体)
    ///   sigma  波段消光系数 (1/km, 分子基线 + 气溶胶; 云消光由消费侧叠加)
    ///   esun   波段太阳直射辐照度 (W/m², TOA 普朗克积分 × 大气透过; 夜≈0)
    /// 纯 double 接口, 与渲染侧 (RGB LDR) 解耦; 真值与渲染同源 (星历/浊度/能见度)。
    /// @return false = 未实现或状态未就绪 (消费侧回退标量基线)。
    virtual bool bandSky(double lambda_min_m, double lambda_max_m, double view_el_deg,
                         double& L_sky, double& sigma, double& esun) const {
        (void)lambda_min_m; (void)lambda_max_m; (void)view_el_deg;
        (void)L_sky; (void)sigma; (void)esun;
        return false;
    }
};

/// 可选能力接口: 天空运行时控制 (调试面板 / 想定编辑器消费)。
/// 由 createSkyModule 返回的模块实现; 用 dynamic_cast<IEnvModule*> 获取。
/// 所有 setter 立即生效, 下一帧 onFrame 时反映到渲染与 AtmosphereState。
class ISkyControl {
public:
    virtual ~ISkyControl() = default;

    // ---- 渲染/物理参数 ----
    /// 全局曝光倍率 (天空穹顶 tone-mapping)。
    virtual void setExposure(float exposure) = 0;
    /// Mie 浊度倍数 (越大越雾/霾)。
    virtual void setTurbidity(float turbidity) = 0;
    /// 太阳平行光开关。
    virtual void setShowSun(bool on) = 0;
    /// 月光/月盘开关。
    virtual void setShowMoon(bool on) = 0;
    /// 大气层渲染开关 (Rocky 后端)。
    virtual void setShowAtmosphere(bool on) = 0;

    /// ---- 查询 (回显当前生效值) ----
    virtual float getExposure() const = 0;
    virtual float getTurbidity() const = 0;
    virtual bool  getShowSun() const = 0;
    virtual bool  getShowMoon() const = 0;
    virtual bool  getShowAtmosphere() const = 0;
};

/// 地形查询接口 (Phase 2: 海岸线/水深掩膜)。
/// 由宿主实现并注入, 供云/海模块查询地形高程、法线。
/// 模块不直接依赖业务地形对象, 保持独立性。
class ITerrainBridge {
public:
    virtual ~ITerrainBridge() = default;
    /// Changes when terrain coverage/data or its local frame changes.
    virtual uint64_t revision() const { return 0; }

    /// 查询锚点 ENU 坐标系下的地形高度 (米)。
    /// @param ex  ENU x 坐标 (米, 东)
    /// @param ez  ENU z 坐标 (米, 南)
    /// @return 地形高度 (米, 相对 ENU 原点); 不可用返回 NaN。
    virtual float heightENU(double ex, double ez) const = 0;
    virtual float waterBedENU(double ex,double ez,float /*seaLevel*/) const { return heightENU(ex,ez); }

    /// 查询锚点 ENU 坐标系下的地形法线 (归一化)。
    /// @param ex  ENU x 坐标 (米)
    /// @param ez  ENU z 坐标 (米)
    /// @return 地形法线 (ENU 坐标系); 不可用返回 (0,1,0)。
    virtual vsg::vec3 normalENU(double ex, double ez) const = 0;

    /// 判断给定 ENU 坐标是否为陆地 (地形高度 > 海平面)。
    /// @param ex        ENU x 坐标 (米)
    /// @param ez        ENU z 坐标 (米)
    /// @param seaLevel  海平面高度 (米)
    /// @return true 表示陆地。
    virtual bool isLand(double ex, double ez, float seaLevel) const = 0;

    /// 查询水深 (海平面 - 地形高度)。
    /// @param ex        ENU x 坐标 (米)
    /// @param ez        ENU z 坐标 (米)
    /// @param seaLevel  海平面高度 (米)
    /// @return 水深 (米); 陆地返回 0 或负值。
    virtual float waterDepth(double ex, double ez, float seaLevel) const = 0;
};

// ---------------------------------------------------------------------------
// Sky —— ECEF 天空与太阳、月亮照明。
// ---------------------------------------------------------------------------
struct SkyParams {
    /// Rocky 兼容后端接收宿主地图的 rocky::VSGContextImpl*。
    void*  rocky_context = nullptr;

    // ---- 时间 (UTC) ----
    int    year = 2026;
    int    month = 9;
    int    day = 26;              ///< 满月 (与宿主每帧注入一致; 月相≈1)
    double hours_utc = 4.0;   ///< UTC 小时 (台北 12:00 本地 ≈ UTC 4:00)
    double time_scale = 1.0;  ///< 仿真时间倍率 (1.0 = 实时)

    // ---- 渲染/物理 ----
    bool   show_atmosphere = false; ///< rocky 内置大气天空 (false=原生 sky_dome 穹顶
                                    ///  接管: 解析散射+日盘+程序化星空/银河, rocky
                                    ///  SkyNode 无星空)
    bool   show_sun = true;        ///< 太阳平行光/散射
    bool   show_moon = true;       ///< 月光平行光 (月盘渲染在后续版本)
    float  exposure = 1.0f;        ///< 穹顶曝光倍率
    float  turbidity = 1.0f;       ///< Mie 浊度倍数 (越大越雾/霾, 与能见度联动预留)
    
    // ---- P0: RockySky 增强 ----
    bool   use_bruneton_lut = false;  ///< 是否使用 Bruneton LUT (RockySky 后端可选)
    bool   overcast = false;
    bool voxel_earth = false; ///< Native ECEF dome and VSG lights; no Rocky context required.
};

/// 创建共享天空模块，后端依据构建选项及 voxel_earth 选择。
std::unique_ptr<IEnvModule> createSkyModule(const SkyParams& p = {});

// ---------------------------------------------------------------------------
// Cloud —— 多层语义化云 (v4): 每层类型/高度/覆盖/密度独立配置; 层云片 +
// 积云公告板环为承载, 类型化成形 (卷云薄纱 / 层积云 / 积云 / 积雨云砧状)。
// ---------------------------------------------------------------------------

/// 云层类型 (决定成形与光学)。
enum class CloudLayerType : int {
    Cirrus = 0,          ///< 卷云: 高空薄纱, 各向异性亮边
    Stratus = 1,         ///< 层云: 大片均匀水平层 (一期层云片)
    Stratocumulus = 2,   ///< 层积云: 层云 + 团块起伏
    Cumulus = 3,         ///< 积云: 蓬松团块 (默认)
    Cumulonimbus = 4,    ///< 积雨云: 塔状厚云 + 砧状顶 + 雨幡
};

/// 单层云配置 (v4)。类型默认值: 高度/覆盖/密度可查表, 此处给保守默认。
struct CloudLayerParams {
    CloudLayerType type = CloudLayerType::Cumulus;
    float altitude = 1200.0f;    ///< 层底高度 (m, ENU +Y)
    float thickness = 600.0f;    ///< 层厚 (m, 成形用)
    float coverage = 0.5f;       ///< 覆盖率 0..1 (EnvWeather/EnvFrame 可动态调制)
    float density  = 0.4f;       ///< 光学密度 0..1
};

struct CloudParams {
    // ---- 一期字段 (向后兼容: 单层模式; layers 为空时生效) ----
    float stratus_altitude = 420.0f;  ///< 层云高度 (m, ENU +Y)
    float stratus_size     = 6000.0f; ///< 层云片边长 (m)
    float cumulus_altitude = 520.0f;  ///< 积云底高 (m, ENU +Y)
    float cumulus_radius   = 8000.0f; ///< 积云以 ENU 锚点为中心的散布半径 (m)
                                      ///  (观察口全景机位 2500m 高度下, 云块需
                                      ///  散布至 4-8km 才能充满画面中带)
    int   cumulus_count    = 120;     ///< 积云块数量
    float cover            = 0.5f;    ///< 默认云量 0..1

    // ---- v4: 多层语义化 (空 = 单层兼容模式, 等价一期) ----
    std::vector<CloudLayerParams> layers;

    // ---- P3-4b: 体积云 (ray-march) ----
    /// 体积云开关: true 时 Cumulus/Cumulonimbus 层渲染为 3D ray-march
    /// 体 (自阴影 + 银边 + 团块结构); false 用公告板精灵 (旧行为)。
    /// 卷云/层云/层积云恒为水平层片 (非体积), 不受本开关影响。
    bool volume = false;
};

/// 可选能力接口: 云运行时控制 (想定编辑器 / 天气面板)。
/// 用 dynamic_cast<IEnvModule*> 获取; setter 下一帧生效。
class ICloudControl {
public:
    virtual ~ICloudControl() = default;

    /// 全局云量调制 0..1 (乘到各层 coverage; 一期单层模式即主云量)。
    virtual void setCoverage(float coverage) = 0;
    /// 设置多层配置 (整体替换; 每层类型/高度/覆盖/密度)。空 = 回到单层兼容模式。
    virtual void setLayers(const std::vector<CloudLayerParams>& layers) = 0;
    /// 调整某层覆盖率 (index 越界无效)。
    virtual void setLayerCoverage(int index, float coverage) = 0;
    /// 层开关 (index 越界无效)。
    virtual void setLayerEnabled(int index, bool on) = 0;

    virtual float getCoverage() const = 0;
    virtual std::vector<CloudLayerParams> getLayers() const = 0;
};

/// 可选能力接口: 云状态查询 (仿真侧消费)。
/// 云影/光照分析: shadowFactor 与渲染画面同源的云量图; precip 得到
/// 当前降水的云底高度 (雨幡位置)。
class ICloudQuery {
public:
    virtual ~ICloudQuery() = default;

    /// 云底/云顶高度 (多层取最低底/最高顶; 无云返回 false)。
    virtual bool cloudBase(float& out_base_m) const = 0;
    virtual bool cloudTop(float& out_top_m) const = 0;

    /// 云影因子 0..1 (0=全阴影 1=无云满光照): 与渲染云量图同源,
    /// 给定 ENU 水平位置 + 太阳方向, 层云沿太阳光线投影的遮挡估计。
    virtual float shadowFactor(double ex, double ez, double sun_el_rad) const = 0;

    /// 总云量 (各层覆盖加权; 与画面一致)。
    virtual float totalCoverage() const = 0;

    /// 指定 ENU 水平点正上方的云密度 alpha 0..1 (与渲染云量图同源的 cloudf2
    /// 采样)。供传感器天图烘焙做空间化云形调制: 视线仰角 el 的天区纹素映射
    /// 到云层底高度的水平交点 (ex,ez) 处采样。仅层云片类型参与 (公告板积云
    /// 不参与, 与 shadowFactor 一致)。缺省 0 = 无云实现时晴空。
    virtual float coverageAt(double ex, double ez) const {
        (void)ex; (void)ez;
        return 0.0f;
    }
};

std::unique_ptr<IEnvModule> createCloudModule(const CloudParams& p = {});

// ---------------------------------------------------------------------------
// Ocean —— 一期: ENU 相机跟随海面网格 + Gerstner 波 + 菲涅尔/天空反射近似。
// ---------------------------------------------------------------------------

/// 海洋边界 (锚点 ENU 系, 米)。海域被限制在一块显式区域内: 边界内海面网格
/// 跟随相机铺满, 越过边界按 fade_width 软渐隐到无 (不出现刀切硬边)。
/// 默认值极大 (100 km), 等价于"无界跟随", 保持既有行为; 需要限定海域
/// (如港口/近岸演习区) 时由参数变量指定。
struct OceanBounds {
    enum Shape : int { Disc = 0, Rectangle = 1 };

    int   shape      = Disc;        ///< 海域形状
    float center_x   = 0.0f;        ///< 海域中心 ENU x (米)
    float center_z   = 0.0f;        ///< 海域中心 ENU z (米)
    float radius     = 100000.0f;   ///< Disc 半径 (米)
    float half_x     = 100000.0f;   ///< Rectangle 半边长 x (米)
    float half_z     = 100000.0f;   ///< Rectangle 半边长 z (米)
    float fade_width = 400.0f;      ///< 边界软渐隐带宽 (米)
};

struct OceanParams {
    float sea_level   = 0.0f;     ///< 海平面 (ENU +Y, m)
    float size        = 3200.0f;  ///< 海面网格覆盖边长 (m, 以相机为中心)
    int   grid        = 128;      ///< 单边网格细分
    float wave_height = 1.3f;     ///< 有效波高 Hs (m); auto_sea_state=true 时忽略
    float wave_length = 0.0f;     ///< 谱峰波长 (m); 0=按 JONSWAP 谱峰自动 (v2 默认)
    float wind_speed  = 6.0f;     ///< 风速 (m/s)
    float sea_temp_k  = 291.0f;   ///< 海表温度 (K; 渲染不用, 传感器链海面辐射真值, 批次3)
    vsg::vec3 deep_color{0.015f, 0.11f, 0.21f};
    vsg::vec3 shallow_color{0.08f, 0.34f, 0.44f};
    OceanBounds bounds;           ///< 可配海洋边界
    ITerrainBridge* terrain = nullptr;  ///< 地形查询接口 (Phase 2: 海岸线掩膜)

    // ---- v2 (S1): 波场物理 (JONSWAP 谱 + 谐波真值源) ----
    float fetch_m  = 250000.0f;   ///< 风区长度 (m, JONSWAP)
    float duration = 10800.0f;    ///< 风时 (s, JONSWAP)
    int   harmonics = 8;          ///< Gerstner 谐波数 (4..16, 性能/细节权衡)
    float choppy    = 0.65f;      ///< Gerstner 水平收缩 Q (0=纯正弦, 1=最大尖峰)
    bool  auto_sea_state = true;  ///< true: 风速/风区/风时 → JONSWAP → 波高全自动;
                                  ///< false: 波高锁定 wave_height (手动想定)

    // ---- v3 (S2): 完整功能 (设计文档 §2.5) ----
    /// 波形模式: 0=GerstnerLegacy (4 写死谐波, 向后兼容),
    ///           1=GerstnerSpectral (S1 谱化谐波, 默认)。
    int   wave_mode = 1;
    /// 涌浪叠加 (设计 §2.5 swell): 独立长周期波列, 不随风场衰减。
    /// height>0 时生效; 从风浪主向偏转 swell_dir_deg, 长波长低频。
    float swell_height  = 0.0f;   ///< 涌浪有效波高 (m)
    float swell_length  = 180.0f; ///< 涌浪主波长 (m, 典型 150..300)
    float swell_dir_deg = 35.0f;  ///< 涌浪向相对风浪主向的偏角 (度, 左正右负)
    /// 光学反射档位 (设计 §5.8 降级链的探针路径):
    /// 0=天顶地平线梯度 (最省), 1=方向相关天空反射 (默认, 含 GGX 耀斑/SSS),
    /// 2=方向相关 + 全偏振菲涅尔 (Fs/Fp 精确式, 静水/内湖掠射更准)。
    int   reflection = 1;
    /// 球面曲率 (设计 §2.5 curvature): 海域半径 > 20km 时建议开,
    /// 远处海面下沉 + 地平线随观察高度抬升 (真海平面视觉)。
    bool  curvature = false;
    /// FFT 周期波场边长 (m, 有限且 >0；仅 wave_mode=2 使用)。
    /// 与显示覆盖 size 独立；512 网格最短可表示波长为 2*fft_patch_size/512。
    /// 调整此值会重建波谱；世界 ENU x/z 连续周期采样，不随相机改变相位。
    float fft_patch_size = 3200.0f;
};

/// 海况概况 (IOceanSampler 输出; 与渲染画面同源)。
struct SeaState {
    float significant_wave_height = 0.0f;  ///< Hs 有效波高 (m)
    float peak_period = 0.0f;              ///< Tp 谱峰周期 (s)
    int   beaufort = 0;                    ///< 蒲福风级 (0..12)
    float peak_direction_rad = 0.0f;       ///< 主波向 (ENU 数学角, x 正向=0)
};

/// 可选能力接口: 海洋仿真采样 (S1, CPU 与 GPU 渲染同参数同公式)。
/// 用途: 舰船浮力/洗消 (waveHeight/orbitalVelocity)、雷达海杂波
/// (seaState/foamCoverage)、红外海面发射率。用 dynamic_cast<IEnvModule*>
/// 获取; onFrame 后有效。坐标为锚点 ENU (米), 时间为帧 time_sec。
class IOceanSampler {
public:
    virtual ~IOceanSampler() = default;

    /// 海况概况 (与画面同源; 风速→JONSWAP→Hs/Tp/蒲福级)。
    virtual SeaState seaState() const = 0;

    /// ENU 坐标波面高度 (含 choppy 垂向位移, 相对海平面)。
    virtual float waveHeight(double ex, double ez, double t) const = 0;

    /// ENU 坐标波面法线 (归一化)。
    virtual vsg::vec3 waveNormal(double ex, double ez, double t) const = 0;

    /// 水质点轨道速度 (m/s): xz=水平 (波向传播), y=垂向 (相位差 90°)。
    virtual vsg::vec3 orbitalVelocity(double ex, double ez, double t) const = 0;

    /// 白沫覆盖率 0..1 (雅可比波陡 + 风成覆盖率, 与渲染同源)。
    virtual float foamCoverage(double ex, double ez, double t) const = 0;

    /// 海表温度 (K) — 传感器链海面辐射真值 (批次3; 缺省实现不破坏既有实现类)。
    virtual float seaTempK() const { return 291.0f; }

    /// 谐波表快照 (#4 传感器链 P0 逐像素波场消费; CPU WaveField 单一真值)。
    /// out 至少 16×vec4: x=波长L y=振幅A z=cosθ w=sinθ (风浪在前, 涌浪紧随)。
    /// 返回有效谐波数 (0=无波场实现); q 输出 Gerstner 水平收缩 Q。
    /// 缺省返回 0 → 传感器链海面细节自动禁用 (不破坏既有实现类)。
    virtual int waveHarmonics(float out[16][4], float& q) const {
        (void)out; (void)q;
        return 0;
    }
};

/// 可选能力接口: 海洋运行时控制 (想定编辑器 / 海况面板消费)。
/// 用 dynamic_cast<IEnvModule*> 获取; setter 下一帧生效, 无需重建模块。
class IOceanControl {
public:
    virtual ~IOceanControl() = default;

    virtual void setSeaLevel(float sea_level) = 0;
    /// 手动指定有效波高 (0 = 恢复 auto_sea_state 气象驱动)。
    virtual void setWaveHeight(float hs) = 0;
    /// 谱峰波长 (0 = 按 JONSWAP 谱峰自动)。
    virtual void setWaveLength(float peak_len) = 0;
    /// 风 (驱动海况演化与波向)。dir 取水平归一化向量。
    virtual void setWind(float speed, vsg::vec3 dir) = 0;
    virtual void setWaterColor(vsg::vec3 deep, vsg::vec3 shallow) = 0;
    virtual void setBounds(const OceanBounds& b) = 0;

    /// ---- v3 (S2) ----
    /// 涌浪叠加: height<=0 关闭。dir_deg 为涌浪向相对风浪主向的偏角 (度)。
    virtual void setSwell(float height, float length_m, float dir_deg) = 0;
    /// 反射档位: 0=梯度, 1=方向相关天空反射 (探针级)。
    virtual void setReflection(int level) = 0;
    /// 球面曲率开关 (>20km 海域建议开)。
    virtual void setCurvature(bool on) = 0;

    virtual float getSeaLevel() const = 0;
    virtual float getWaveHeight() const = 0;
    virtual float getWaveLength() const = 0;
    virtual float getWindSpeed() const = 0;
    virtual vsg::vec3 getWindDir() const = 0;
    virtual OceanBounds getBounds() const = 0;

    /// ---- v3 (S2) ----
    virtual float getSwellHeight() const = 0;
    virtual int   getReflection() const = 0;
    virtual bool  getCurvature() const = 0;
};

std::unique_ptr<IEnvModule> createOceanModule(const OceanParams& p = {});

// ---------------------------------------------------------------------------
// IEnvStack —— 环境"全家桶"聚合入口 (v2)。
//
// 动机: 三个模块 (sky/cloud/ocean) 各自创建/挂载/喂数据时, 宿主要重复写
// 挂载分流 (worldRoot vs contentRoot)、能力查询 (ISunProvider 转发给云海)、
// 时序 (sky 先算出光照真值再给云海)。本接口把这套约定固化到模块内, 宿主
// (rocky 地球工作流) 只需:
//   1. EnvironmentStackParams 决定要哪些模块 + rocky 上下文;
//   2. createEnvironmentStack() 一次创建;
//   3. 把 stack->worldRoot() / stack->contentRoot() 挂进场景;
//   4. 每帧 fillEnvFrame(...) 后调 stack->onFrame(frame) —— 内部自动:
//      sky 先 onFrame 并产出 AtmosphereState, 把太阳真值/天色写回 frame,
//      再喂给 cloud/ocean (即"单一真值源"由栈内保证, 不依赖宿主调用顺序)。
// ---------------------------------------------------------------------------

struct EnvironmentStackParams {
    // ---- 模块开关 (默认 sky+cloud+ocean 全开) ----
    bool enable_sky   = true;
    bool enable_cloud = true;
    bool enable_ocean = true;

    // ---- 子模块参数 (零默认值即可跑) ----
    SkyParams   sky;
    CloudParams cloud;
    OceanParams ocean;

    // ---- 能力选项 ----
    /// true: 栈内 sky 有 ISunProvider 时, 每帧把星历太阳真值覆写进
    /// EnvFrame::sun_dir_enu / sun_el_deg (云/海 shader 与宿主平行光同源)。
    /// 宿主手算太阳 (sun_el/sun_az 参数) 仅在无 sky 时生效。
    bool sky_drives_sun = true;
    /// true: 栈内 sky 有 ILightingProvider 时, 每帧把 AtmosphereState 的
    /// 直射/环境/天顶/地平线色与昼因子覆写进 EnvFrame 光照段, 云/海雾色
    /// 与反射与天空一致 (暮光/夜间自动跟随)。
    bool sky_drives_lighting = true;
};

class IEnvStack {
public:
    virtual ~IEnvStack() = default;

    /// 挂到世界根的节点 (ECEF 级, rocky 地球天空)。无此类模块时返回
    /// nullptr, 宿主可不挂。生命周期: 栈持有, 宿主只 addChild 不 delete。
    virtual vsg::Node* worldRoot() = 0;

    /// 挂到作业区 ENU 内容根的节点 (云/海)。
    virtual vsg::Node* contentRoot() = 0;

    /// 每帧驱动: 内部保证 sky→cloud→ocean 的真值传播顺序。
    /// @param f  宿主填充的帧参数 (相机矩阵/时间/气象/可选锚点)。
    virtual void onFrame(EnvFrame& f) = 0;

    virtual void setDevice(vsg::Device* device) {
        for (auto* module : modules()) if (module) module->setDevice(device);
    }

    /// 可选宿主钩子: 注入有效 Vulkan 窗口 (转发到各模块)。
    /// 消费方: wave_mode=2 的海洋模块 (GPU FFT 波场需 window/device 构建
    /// compute 管线 + set=1 采样渲染管线); 其余模块忽略。
    /// 须在 viewer->compile() 之前调用, 使 FFT 描述符/管线得以编译。
    /// 默认实现转发到所有模块的 IEnvModule::setWindow (各模块自行忽略)。
    virtual void setWindow(vsg::Window* w) {
        for (auto* m : modules()) {
            if (m) m->setWindow(w);
        }
    }

    /// 模块访问 (配置面板/调试用)。type 取值: "sky" / "cloud" / "ocean";
    /// 不存在返回 nullptr。
    virtual IEnvModule* module(const char* type) = 0;

    /// 能力转发: 在栈内模块上 dynamic_cast, 找到即返回 (sky 优先)。
    /// 用法: auto* sun = stack->query<ISunProvider>();
    template <typename T>
    T* query() {
        for (auto* m : modules()) {
            if (auto* t = dynamic_cast<T*>(m)) return t;
        }
        return nullptr;
    }

protected:
    /// query() 遍历用的模块列表 (sky 在前)。
    virtual std::vector<IEnvModule*> modules() = 0;
};

/// 创建环境聚合栈。参数零配置即可跑出 天空+云+海 全景。
std::unique_ptr<IEnvStack> createEnvironmentStack(const EnvironmentStackParams& p = {});

}  // namespace rockyenv

#endif  // ROCKY_ENV_API_H
