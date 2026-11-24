///
/// rocky_env 统一验证程序 (env_viewer) —— 环境基座 + 多传感器成像链。
///
/// 由原 env_viewer (三模块环境查看器) 与原 sensor_viewer (多传感器成像链
/// 测试程序) 合并而成, 单一可执行同时覆盖两类验证:
///
/// 1) 纯环境验证 (默认): rocky 地球基座 (MapNode, ECEF) + sky/cloud/ocean
///    三模块 (IEnvStack 单一真值源) + 键盘交互。--mode/--map/--ocean 等
///    控制基座内容。画面即普通可见光渲染。
///    传感链路未启用时单 view 全窗口 (地球+天空+云+海)。
///
/// 2) 传感器验证 (--scene 或 --band 任一给出): 双 view 布局 ——
///    左观察口 = rocky 地球 + 环境 + 场景几何 (真实 vsg View, 挂 rocky 定制
///    ViewDependentStateEx —— 裸 VDS 缺 renderParamsBuf 会导致地形全黑);
///    右传感器视口 = SensorPipeline 合成画面 (rad→optics→detector→
///    electronics RTT 级联 + TV/红外波段 + 雷达回波链)。
///    场景 geo_anchor 存在时星历锚点取场景值, 否则缺省台北。
///
/// 用法 (环境基座):
///   ./env_viewer [--mode all|sky|cloud|ocean] [--frames N] [--shot out.ppm]
///                [--map rocky.map.json | --no-map] [--ocean]
///                [--time H | --hours-utc H | --hours H] [--sun-el D] [--sun-az D]
///                [--wind MPS] [--wind-az D] [--cover 0..1] [--hs M]
///                [--time-scale X] [--obs-eye x,y,z] [--obs-center x,y,z]
///                [--fft-patch M] (periodic ocean field size, independent of display size)
///                [--width W] [--height H]
///                [--cam 1..4]  (1=同框全景 2=掠海 3=仰天 4=俯视地形)
///                [--fov DEG] [--near M] [--far M] [--alt M]  (相机参数覆盖)
///                [--icd vulkan_icd.json] [--list-icd] [--debug-layer]
///
/// 用法 (传感器链, 与环境参数可任意组合):
///   ./env_viewer --band mwir|lwir|swir|tv            # CLI 单实例 (程序化热场景)
///   ./env_viewer --scene xxx.scenario.json [--sensor <id|name|index>]
///                [--sen-fov DEG] [--netd-mk 50] [--tint-ms 8]
///                [--cloud 0..1] [--rain 0..1] [--vis M]
///                [--cloud-type 0..4|cirrus|stratus|stratocumulus|cumulus|cumulonimbus]
///                [--cloud-base M] [--cloud-thick M] [--cloud-density 0..1]
///                [--sen-eye x,y,z] [--sen-center x,y,z] [--sen-res WxH]
///                [--publish] [--stages csv] [--power-off-after s] [--unlink-after s]
///                [--metrics] [--radar-cpu-stages] [--dump-p0 path]
///                [--thermal-state <名>] [--no-rt]
///                [--rf Hz] [--bw Hz] [--cpi N] [--plat-vel x,y,z] [--dump-rd path]
///                [--track-after s]
///
/// rocky 地球基座: 默认创建 MapNode (ECEF 世界根直挂) —— --map 指定 rocky
/// 地图 JSON (.map.json, 图层代码方式加载); 未指定或加载失败时回落内置离线
/// TMS 全球底图+高程。--no-map 关闭地球。海洋默认不创建; --mode ocean 或
/// --ocean 显式开启。注意 --mode ocean 单独全黑是已知遗留 (sky 关闭无光照)。
///
/// 交互 (键位合并, 冲突键按上下文分配):
///   S / C / O   切换 sky / cloud / ocean 单独显示 (再按一次返回全部)
///   A           全部显示
///   1 / 2 / 3   相机预设: 纯环境 = 同框全景/掠海/仰天; 传感器 = 全景/近观/传感器视线
///   4           纯环境专用: 俯视地形
///   T / t       时间基准 +1h / -1h (驱动星历, 观察昼夜/暮光; 纯环境)
///   [ / ]       风速 -1 / +1 m/s (海况随之变化; 纯环境)
///   - / =       云量 -0.1 / +0.1 (纯环境)
///   P           打印当前各模块查询读数 (星历/云底云顶/海况)
///   ESC         退出
///
/// 说明: 环境侧每个模块的 Control/Query 接口在此程序中都被真实调用; 传感器
/// 侧 SceneSource/SensorRegistry/SensorScheduler/SensorPipeline/RadarChain/
/// ProductDispatch 全链验证。环境基座统一走 IEnvStack (栈内保证 sky→cloud→
/// ocean 真值传播顺序), EnvPhysicsService.bind(IEnvStack*) 直接可用。

#include <rocky_env/SwapchainReadback.h>
#include <rocky_env/EnvApi.h>
#include <rocky_env/RuntimePaths.h>
#include <limits>

#include <rocky_env/SensorApi.h>
#include <rocky_env/SceneSource.h>
#include <rocky_env/SensorInstance.h>
#include <rocky_env/SensorScheduler.h>
#include <rocky_env/GimbalModel.h>
#include <rocky_env/SensorPipeline.h>
#include <rocky_env/BandRT.h>
#include <rocky_env/GBufferPool.h>            // P1.1: 全场景唯一求交 G-buffer
#include <rocky_env/RadarChain.h>
#include <rocky_env/SharedSceneResources.h>   // P0: 场景级资源池 (几何/BLAS/TLAS/LUT/env)
#include <rocky_env/RadarStages.h>
#include <rocky_env/StateRingBuffer.h>
#include <rocky_env/EnvPhysicsService.h>
#include <rocky_env/RockyGraphicsFeatures.h>
#include <product_dispatch/ProductDispatch.h>
#include <vsgraysim/core/ObjLoader.h>
#include <vsgraysim/broker/SimDataBroker.h>
#include <vsgraysim/broker/EquipmentInfo.h>
#include <vsgraysim/layer1_scene/TerrainSystem.h>   // P3: .vsgterrain 高度场 → 三角形汤

#include <rocky/vsg/VSGContext.h>
#include <rocky/vsg/MapNode.h>
#include <rocky/TMSImageLayer.h>
#include <rocky/TMSElevationLayer.h>
#include <rocky/Profile.h>
#include <simcore/GeoFrame.h>

using simcore::geo::GeoAnchor;
using simcore::geo::makeEnuToWorldMatrix;
using simcore::geo::geoUp;

#include <vsg/all.h>
#include <vsgXchange/all.h>
#include <vsg/commands/CopyImageToBuffer.h>
#include <vsg/commands/PipelineBarrier.h>

// ImGui 半透明切换按钮 (方案A: 双 SensorPipeline 常驻, 切换激活那个)
#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <limits.h>

#include <nlohmann/json.hpp>

#include <vulkan/vulkan.h>

// X11 抓屏 (验证渲染画面; 无 X11 开发头时降级为不可用)。
#if defined(__has_include)
#  if __has_include(<X11/Xlib.h>)
#    define ENV_VIEWER_HAVE_X11 1
#    include <X11/Xlib.h>
#    include <X11/Xutil.h>
#  endif
#endif
#ifndef ENV_VIEWER_HAVE_X11
#  define ENV_VIEWER_HAVE_X11 0
#endif

// VSG 的 Xcb_Window 要求 traits->nativeWindow 恰好是 xcb_window_t 类型、
// traits->systemConnection 恰好是 xcb_connection_t* 类型 (any_cast 精确匹配,
// 类型不符会抛 std::bad_any_cast)。宿主 VsgQtWidget 亦如此。
#if defined(__has_include)
#  if __has_include(<xcb/xcb.h>)
#    define ENV_VIEWER_HAVE_XCB 1
#    include <xcb/xcb.h>
#  endif
#endif
#ifndef ENV_VIEWER_HAVE_XCB
#  define ENV_VIEWER_HAVE_XCB 0
#endif

using namespace rockyenv;
using namespace rockyenv::sensor;
using rockyenv::EnvironmentStackParams;
using rockyenv::CloudLayerParams;
using rockyenv::CloudLayerType;
using rockyenv::EnvFrame;
using rockyenv::createEnvironmentStack;

namespace {

// ---------------------------------------------------------------------------
// 命令行参数 (环境基座 + 传感器链 并集)
// ---------------------------------------------------------------------------
struct Args {
    // ---- 环境基座 ----
    std::string mode = "all";          ///< all | sky | cloud | ocean
    std::string map_json;              ///< --map: rocky 地图 JSON (空=内置 TMS 底图)
    bool  no_map = false;              ///< --no-map: 不创建 rocky 地球底座
    bool  ocean = false;               ///< --ocean: 显式创建海洋模块 (默认关)
    float sun_el = 45.0f;              ///< 平面模式太阳高度角 (deg; 无 sky 时生效)
    float sun_az = 160.0f;             ///< 太阳方位角 (deg)
    float wind_az = 80.0f;             ///< 风向 (deg)
    float hs = 0.0f;                   ///< 有效波高 (0=自动)
    int   wave_mode = -1;              ///< --wave-mode: 波形模式 0=legacy 1=谱谐波 2=GPU FFT (P3, -1=场景/默认)
    float fft_patch = 3200.f;
    bool has_fft_patch = false;
    int   cam = 1;                     ///< 启动相机预设 (纯环境; 1=同框全景 2=掠海 3=仰天 4=俯视)
    double fov = 0.0;                  ///< --fov: 覆盖观察相机垂直 fov (0=预设值)
    double near_z = 0.0;               ///< --near: 覆盖近平面 (0=预设值)
    double far_z = 0.0;                ///< --far: 覆盖远平面 (0=预设值)
    double alt = 0.0;                  ///< --alt: 覆盖相机 ENU 高度 (0=预设值)
    bool  list_icd = false;            ///< 只打印 ICD/设备信息后退出

    // ---- 共用 ----
    int   frames = 0;                  ///< >0: 渲染 N 帧后退出 (自动化验证)
    std::string shot;                  ///< 非空: 截图保存路径
    int   width = 1600;
    int   height = 900;
    double hours_utc = 4.0;            ///< UTC 小时 (台北正午 ≈ 4)
    double day_minutes = 4.0;          ///< --day-minutes: 24h 昼夜循环的真实分钟数 (0=固定时刻)
    double time_scale = 1.0;
    float wind = 10.0f;                ///< 风速 (m/s)
    float cover = 0.5f;                ///< 云量
    bool  cloud_volume = false;        ///< --cloud-volume: 积云/积雨云渲染为 3D ray-march 体积云 (P3-4b)
    std::string icd_;                  ///< 显式指定 Vulkan ICD json 路径
    bool  debug_layer = false;         ///< 启用 Vulkan 校验层

    // ---- 传感器链 (原 sensor_viewer) ----
    std::string band = "mwir";         ///< --band (显式给出才进入传感器模式)
    bool   has_band = false;           ///< CLI 显式指定 (Param 优先级: CLI > 场景 JSON > 默认)
    double netd_mk = 50.0;
    double tint_ms = 8.0;
    double sen_fov = 20.0;             ///< --sen-fov: 传感器视场角 (env --fov 为观察相机, 已改名避歧义)
    bool   has_netd = false;
    bool   has_tint = false;
    bool   has_sen_fov = false;
    std::string sensor;                ///< --sensor <id|name|index>; 空 = 首个成像实例
    std::string scene;                 ///< 非空: 场景 JSON (objects/thermal 驱动几何)
    std::string anchor;                ///< --anchor "lat,lon[,alt_m]": 覆盖星历锚点
                                       ///  (缺省取场景 geo_anchor / 台北; 环境测试
                                       ///  可搬到海域使海洋板越过陆地 discard)
    std::string dump_p0;               ///< 非空: P0 回读 raw 输出路径 (B2 验收)
    double rain = 0.0;                 ///< --rain 0..1 降水强度 (1=暴雨; 雨消光/路径辐射/太阳衰减)
    double vis = 10000.0;              ///< --vis m 晴空气溶胶能见度 (bandSky σ 与灰天谱形权值 κ 的单一真值)
    double cloud_base = 1200.0;        ///< --cloud-base m 云底高度 (云温由高度按直减率派生)
    double cloud_thick = 600.0;        ///< --cloud-thick m 层厚
    double cloud_density = 0.4;        ///< --cloud-density 0..1 光学密度
    int    cloud_type = 3;             ///< --cloud-type: 0 卷云/1 层云/2 层积云/3 积云(默认,公告板颗粒感)/4 积雨云
    bool   no_rt = false;              ///< 强制 P0 光栅化路径 (B2 回归对比)
    // ---- B5 分发时序 ----
    bool   publish = false;            ///< --publish: 启用 ProductFrame→传输帧分发
    std::string stages_csv;            ///< --stages "raw,image" (CLI > 场景 stages > 缺省 image)
    double power_off_after = -1.0;     ///< --power-off-after <s>: 自测断电停帧 (<0 关)
    double unlink_after = -1.0;        ///< --unlink-after <s>: 自测链接门控 (<0 关)
    // ---- B6 雷达回波链 ----
    double rf_hz = -1.0;               ///< --rf <Hz>: 载频覆盖 (验收: 多普勒质心平移)
    double bw_hz = -1.0;               ///< --bw <Hz>: 带宽覆盖 (验收: 距离分辨率)
    int    cpi = 0;                    ///< --cpi <N>: CPI 脉冲数覆盖 (验收: 多普勒分辨率)
    std::string plat_vel = "40,0,0";   ///< --plat-vel "x,y,z" m/s: 雷达平台合成运动
    std::string dump_rd;               ///< --dump-rd <path>: RD 图 PPM 输出 (首个 CPI)
    // ---- B7 扫描机构 ----
    double track_after = -1.0;         ///< --track-after <s>: SEARCH→TRACK 迁移时刻 (<0 关)
    // ---- GPU 全链闭环 (P2/P3/P4) ----
    bool   metrics = false;            ///< --metrics: 显式回读 + 指标统计 (默认零回读)
    bool   radar_cpu_stages = false;   ///< --radar-cpu-stages: 雷达段 GPU/CPU 交叉对比
    std::string thermal_state;         ///< --thermal-state <名>: 工况倍率 (CLI > 场景 JSON > base)
    // ---- A7/C2 机位覆盖 (优先级最高, 验证/构图用) ----
    std::string sen_eye;               ///< --sen-eye "x,y,z" ENU 米: 传感器/观察口位置
    std::string sen_center;            ///< --sen-center "x,y,z" ENU 米: 瞄准点
    std::string obs_eye,obs_center;    ///< Explicit left observer pose; sensor installation is unchanged.
    // ---- 分辨率覆盖 (场景 JSON 缺省 640x512; 测试/图版用高分辨率) ----
    int    sen_res_w = 0;              ///< --sen-res "WxH": 传感器分辨率覆盖 (0 = 用 JSON/缺省)
    int    sen_res_h = 0;

    /// 传感器链是否启用 (--scene 或 --band 任一显式给出)。
    bool sensorRequested() const { return !scene.empty() || has_band; }

    // ---- 方案D: 单 SensorPipeline 重建切换 (TV ↔ MWIR), ImGui 半透明按钮 ----
    bool   dual_band = false;            ///< --dual-band: 启用 ImGui TV/MWIR 切换按钮
    int    switch_at = -1;               ///< --switch-at <帧>: 第 N 帧自动切到另一 band (无头验证用)
};

// ---------------------------------------------------------------------------
// D 方案 (单 Pipeline 重建切换): 共享资源 (sharedScene/gbufferPool) 仅建一次,
// 切 band 时释放并重建单个 SensorPipeline (LUT/天空图/着色管线按新 band 重建),
// 相机/环境/观察口保持不变。bands[0] 即当前激活 (单元素 vector 保持原索引语义,
// activeIdx 恒为 0)。
// ---------------------------------------------------------------------------
struct BandResources {
    BandType          type = BandType::MWIR;
    SensorProfile     profile;
    BandRadianceLut   lut;
    SensorPipeline    pipe;
    std::vector<float> skyBake;   // 谱天空图 CPU 烘焙结果 (TV=4ch, IR=1ch)
    EnvAnswer         env;        // 该 band 的环境应答 (sigma/sky/sun 按 band 不同)
    // #5 IR 云发射辐亮度 LUT (band4.w = ε·M(T_c)/π; TV 不用)
    BandRadianceLut   cloudEmitLut;
    bool              cloudEmitReady = false;
    bool              ok = false;
};

Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&](double def) -> double {
            return (i + 1 < argc) ? std::atof(argv[++i]) : def;
        };
        // ---- 共用 / 环境基座 ----
        if (k == "--mode" && i + 1 < argc)        a.mode = argv[++i];
        else if (k == "--frames")                 a.frames = static_cast<int>(next(a.frames));
        else if (k == "--shot" && i + 1 < argc)   a.shot = argv[++i];
        else if (k == "--map" && i + 1 < argc)    a.map_json = argv[++i];
        else if (k == "--no-map")                 a.no_map = true;
        else if (k == "--ocean")                  a.ocean = true;
        else if (k == "--hours-utc")              a.hours_utc = next(a.hours_utc);
        else if (k == "--time")                   a.hours_utc = next(a.hours_utc);
        else if (k == "--hours")                  a.hours_utc = next(a.hours_utc);
        else if (k == "--day-minutes")            a.day_minutes = std::max(0.0, next(a.day_minutes));
        else if (k == "--sun-el")                 a.sun_el = static_cast<float>(next(a.sun_el));
        else if (k == "--sun-az")                 a.sun_az = static_cast<float>(next(a.sun_az));
        else if (k == "--wind")                   a.wind = static_cast<float>(next(a.wind));
        else if (k == "--wind-az")                a.wind_az = static_cast<float>(next(a.wind_az));
        else if (k == "--cover")                  a.cover = static_cast<float>(next(a.cover));
        else if (k == "--hs")                     a.hs = static_cast<float>(next(a.hs));
        else if (k == "--wave-mode")              a.wave_mode = std::clamp(static_cast<int>(next(a.wave_mode)), 0, 2);
        else if (k == "--fft-patch")              {a.fft_patch=float(next(a.fft_patch));a.has_fft_patch=true;}
        else if (k == "--time-scale")             a.time_scale = next(a.time_scale);
        else if (k == "--width")                  a.width = static_cast<int>(next(a.width));
        else if (k == "--height")                 a.height = static_cast<int>(next(a.height));
        else if (k == "--cam")                    a.cam = static_cast<int>(next(a.cam));
        else if (k == "--icd" && i + 1 < argc)    a.icd_ = argv[++i];
        else if (k == "--list-icd")               a.list_icd = true;
        else if (k == "--fov")                    a.fov = next(a.fov);
        else if (k == "--near")                   a.near_z = next(a.near_z);
        else if (k == "--far")                    a.far_z = next(a.far_z);
        else if (k == "--alt")                    a.alt = next(a.alt);
        else if (k == "--debug-layer")            a.debug_layer = true;
        // ---- 传感器链 ----
        else if (k == "--band" && i + 1 < argc)   { a.band = argv[++i]; a.has_band = true; }
        else if (k == "--netd-mk")                { a.netd_mk = next(a.netd_mk); a.has_netd = true; }
        else if (k == "--tint-ms")                { a.tint_ms = next(a.tint_ms); a.has_tint = true; }
        else if (k == "--sen-fov")                { a.sen_fov = next(a.sen_fov); a.has_sen_fov = true; }
        else if (k == "--sensor" && i + 1 < argc) a.sensor = argv[++i];
        else if (k == "--scene" && i + 1 < argc)  a.scene = argv[++i];
        else if (k == "--anchor" && i + 1 < argc) a.anchor = argv[++i];
        else if (k == "--dump-p0" && i + 1 < argc) a.dump_p0 = argv[++i];
        else if (k == "--cloud")                  a.cover = static_cast<float>(next(a.cover));
        else if (k == "--cloud-base")             a.cloud_base = next(a.cloud_base);
        else if (k == "--cloud-thick")            a.cloud_thick = next(a.cloud_thick);
        else if (k == "--cloud-density")          a.cloud_density = next(a.cloud_density);
        else if (k == "--cloud-volume")           a.cloud_volume = true;
        else if (k == "--cloud-type" && i + 1 < argc) {   // 名称或数字 0-4
            const std::string t = argv[++i];
            a.cloud_type = (t == "cirrus") ? 0 : (t == "stratus") ? 1 :
                           (t == "stratocumulus") ? 2 : (t == "cumulus") ? 3 :
                           (t == "cumulonimbus") ? 4 : std::atoi(t.c_str());
        }
        else if (k == "--rain")                   a.rain = next(a.rain);
        else if (k == "--vis")                    a.vis = std::max(100.0, next(a.vis));
        else if (k == "--no-rt")                  a.no_rt = true;
        else if (k == "--publish")                a.publish = true;
        else if (k == "--stages" && i + 1 < argc) a.stages_csv = argv[++i];
        else if (k == "--power-off-after")        a.power_off_after = next(a.power_off_after);
        else if (k == "--unlink-after")           a.unlink_after = next(a.unlink_after);
        else if (k == "--rf")                     a.rf_hz = next(a.rf_hz);
        else if (k == "--bw")                     a.bw_hz = next(a.bw_hz);
        else if (k == "--cpi")                    a.cpi = static_cast<int>(next(a.cpi));
        else if (k == "--plat-vel" && i + 1 < argc) a.plat_vel = argv[++i];
        else if (k == "--dump-rd" && i + 1 < argc) a.dump_rd = argv[++i];
        else if (k == "--track-after")            a.track_after = next(a.track_after);
        else if (k == "--metrics")                a.metrics = true;
        else if (k == "--radar-cpu-stages")       a.radar_cpu_stages = true;
        else if (k == "--thermal-state" && i + 1 < argc) a.thermal_state = argv[++i];
        else if (k == "--sen-eye" && i + 1 < argc)    a.sen_eye = argv[++i];
        else if (k == "--sen-center" && i + 1 < argc) a.sen_center = argv[++i];
        else if (k == "--obs-eye") a.obs_eye = i+1<argc ? argv[++i] : "<missing>";
        else if (k == "--obs-center") a.obs_center = i+1<argc ? argv[++i] : "<missing>";
        else if (k == "--sen-res" && i + 1 < argc) {   // "WxH" 传感器分辨率覆盖
            const std::string v = argv[++i];
            const auto x = v.find('x');
            if (x != std::string::npos) {
                a.sen_res_w = std::atoi(v.substr(0, x).c_str());
                a.sen_res_h = std::atoi(v.substr(x + 1).c_str());
            }
        }
        else if (k == "--dual-band")                a.dual_band = true;
        else if (k == "--switch-at")                a.switch_at = static_cast<int>(next(-1.0));
        else {
            std::fprintf(stderr, "未知参数: %s\n", k.c_str());
            std::exit(2);
        }
    }
    return a;
}

constexpr double kC = 2.99792458e8;   // 光速 (m/s, B6 载频/波长换算)
constexpr double kPi = 3.14159265358979323846;   // B7 扫描角换算

/// ENU 点/方向 → ECEF (model 变换后取 xyz; vsg 无 dvec4→dvec3 隐式转换)。
inline vsg::dvec3 enuXform(const vsg::dmat4& m, const vsg::dvec3& p, double w = 1.0)
{
    const vsg::dvec4 r = m * vsg::dvec4(p, w);
    return vsg::dvec3(r.x, r.y, r.z);
}

// ---------------------------------------------------------------------------
// 模块可见性 (键位 → IEnvStack 模块开关)
// ---------------------------------------------------------------------------
enum class Mode { All, Sky, Cloud, Ocean };

void applyMode(IEnvStack* stack, Mode m)
{
    if (!stack) return;
    if (auto* s = stack->module("sky"))   s->setEnabled(m == Mode::All || m == Mode::Sky);
    if (auto* s = stack->module("cloud")) s->setEnabled(m == Mode::All || m == Mode::Cloud);
    if (auto* s = stack->module("ocean")) s->setEnabled(m == Mode::All || m == Mode::Ocean);
}

// ---------------------------------------------------------------------------
// 打印当前进程实际可用的 Vulkan 物理设备 (验证走的是独显还是软件光栅化)。
// ---------------------------------------------------------------------------
void printVulkanDevices()
{
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "env_viewer";
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;
    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) {
        std::printf("[vulkan] 无法创建实例 (loader 不可用?)\n");
        return;
    }
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    if (n) vkEnumeratePhysicalDevices(inst, &n, devs.data());
    std::printf("[vulkan] 物理设备 %u 个:\n", n);
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(devs[i], &p);
        const char* type = "other";
        switch (p.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   type = "独显"; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: type = "核显"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            type = "CPU 软光栅"; break;
            default: break;
        }
        std::printf("   [%u] %s  (%s)  api %u.%u.%u  vendor 0x%04x\n", i, p.deviceName, type,
                    VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
                    VK_VERSION_PATCH(p.apiVersion), p.vendorID);
    }
    vkDestroyInstance(inst, nullptr);
}

// ---------------------------------------------------------------------------
// Vulkan 帧回读截图命令 (不依赖 X 窗口映射状态)。
//
// 坑: XGetImage 依赖窗口被 WM 映射 (map_state==IsViewable)。无 WM/映射滞后的
// 环境里抓屏读到的是未定义内容 (常见上半纯白)。本命令在 RenderGraph 后追加
// vkCmdCopyImageToBuffer: record 时取当前帧 swapchain image, PRESENT→TRANSFER_SRC
// barrier + 拷贝 + 还原 barrier, 主循环 recordAndSubmit 后 waitIdle 再 map 读取。
// ---------------------------------------------------------------------------
using rockyenv::SwapchainReadback;

/// 从回读 buffer 写 PPM (BGRA→RGB, 顶部行序)。
bool writePPMFromReadback(vsg::Buffer* buffer, const VkExtent2D& ext, const std::string& path)
{
    if (!buffer || ext.width == 0 || ext.height == 0) return false;
    auto* mem = buffer->getDeviceMemory(0);
    if (!mem) return false;
    void* data = nullptr;
    const VkDeviceSize size = VkDeviceSize(ext.width) * ext.height * 4;
    if (mem->map(0, size, 0, &data) != VK_SUCCESS || !data) return false;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fprintf(f, "P6\n%u %u\n255\n", ext.width, ext.height);
        const auto* src = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> line(size_t(ext.width) * 3);
        for (uint32_t y = 0; y < ext.height; ++y) {
            for (uint32_t x = 0; x < ext.width; ++x) {
                const uint8_t* p = src + (size_t(y) * ext.width + x) * 4;
                line[size_t(x) * 3 + 0] = p[2];   // R (BGRA→RGB)
                line[size_t(x) * 3 + 1] = p[1];   // G
                line[size_t(x) * 3 + 2] = p[0];   // B
            }
            std::fwrite(line.data(), 1, line.size(), f);
        }
        std::fclose(f);
    }
    mem->unmap();
    return f != nullptr;
}

/// 按窗口 ID 抓取**本程序窗口自身**的图像并保存为 PPM (回退路径)。
/// 返回 true 表示成功; mean_lum/nonuniform 用于判定是否纯色/空白。
bool captureWindowToPPM(Display* dpy, Window win, const std::string& path,
                        float& mean_lum, float& nonuniform, int& out_w, int& out_h)
{
#if ENV_VIEWER_HAVE_X11
    if (!dpy || !win) return false;
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(dpy, win, &wa)) return false;
    const int w = wa.width, h = wa.height;
    if (w <= 0 || h <= 0) return false;
    XImage* img = XGetImage(dpy, win, 0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h),
                            AllPlanes, ZPixmap);
    if (!img) return false;

    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
    double sum = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned long px = XGetPixel(img, x, y);
            const int r = static_cast<int>((px >> 16) & 0xFF);
            const int g = static_cast<int>((px >> 8) & 0xFF);
            const int b = static_cast<int>(px & 0xFF);
            const int l = (r * 299 + g * 587 + b * 114) / 1000;
            gray[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(l);
            sum += l;
        }
    }
    const double total = static_cast<double>(w) * h;
    mean_lum = static_cast<float>(sum / total);
    double var = 0.0;
    for (auto v : gray) { const double d = v - mean_lum; var += d * d; }
    nonuniform = static_cast<float>(std::sqrt(var / gray.size()));

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { XDestroyImage(img); return false; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned long px = XGetPixel(img, x, y);
            const unsigned char rgb[3] = {
                static_cast<unsigned char>((px >> 16) & 0xFF),
                static_cast<unsigned char>((px >> 8) & 0xFF),
                static_cast<unsigned char>(px & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    XDestroyImage(img);
    out_w = w; out_h = h;
    return true;
#else
    (void)dpy; (void)win; (void)path; (void)mean_lum; (void)nonuniform; (void)out_w; (void)out_h;
    return false;
#endif
}

/// 等待窗口真正被映射 (map_state == IsViewable)。
///
/// 坑: VSG 的 Xcb_Window 构造里虽然调用了 xcb_map_window, 但 X 服务器的映射是
/// 异步的, 且窗口管理器 (mutter 等) 还要做重定位/加装饰。构造刚返回时窗口常常仍是
/// Unmapped (map_state=0), 此时 XGetImage 只会读到全黑 —— 表现为"渲染帧数在推进、
/// 无任何报错、但截图纯黑", 极易被误判成渲染/场景缺陷。
/// 这里在抓图前轮询等待窗口变为 IsViewable。
bool waitForWindowMapped(xcb_connection_t* conn, xcb_window_t win, int timeout_ms)
{
#if ENV_VIEWER_HAVE_XCB
    if (!conn || !win) return false;
    const int step_ms = 20;
    for (int waited = 0; waited <= timeout_ms; waited += step_ms) {
        xcb_get_window_attributes_cookie_t ck = xcb_get_window_attributes(conn, win);
        xcb_get_window_attributes_reply_t* rp =
            xcb_get_window_attributes_reply(conn, ck, nullptr);
        if (rp) {
            const uint8_t st = rp->map_state;
            free(rp);
            if (st == XCB_MAP_STATE_VIEWABLE) return true;
        }
        xcb_flush(conn);
        usleep(step_ms * 1000);
    }
    return false;
#else
    (void)conn; (void)win; (void)timeout_ms;
    return false;
#endif
}

/// 抓取 X11 根窗口图像并保存为 PPM (仅用于对照, 会混入桌面)。
bool captureRootToPPM(const std::string& path, float& mean_lum, float& nonuniform)
{
#if ENV_VIEWER_HAVE_X11
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    const Window root = DefaultRootWindow(dpy);
    XWindowAttributes wa{};
    if (!XGetWindowAttributes(dpy, root, &wa)) { XCloseDisplay(dpy); return false; }
    XImage* img = XGetImage(dpy, root, 0, 0,
                            static_cast<unsigned>(wa.width),
                            static_cast<unsigned>(wa.height),
                            AllPlanes, ZPixmap);
    if (!img) { XCloseDisplay(dpy); return false; }

    const int w = img->width, h = img->height;
    double sum = 0.0;
    std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned long px = XGetPixel(img, x, y);
            const int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            const int l = (r * 299 + g * 587 + b * 114) / 1000;
            gray[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(l);
            sum += l;
        }
    }
    mean_lum = static_cast<float>(sum / (static_cast<double>(w) * h));
    double var = 0.0;
    for (auto v : gray) { const double d = v - mean_lum; var += d * d; }
    nonuniform = static_cast<float>(std::sqrt(var / gray.size()));

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { XDestroyImage(img); XCloseDisplay(dpy); return false; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned long px = XGetPixel(img, x, y);
            const unsigned char rgb[3] = {
                static_cast<unsigned char>((px >> 16) & 0xFF),
                static_cast<unsigned char>((px >> 8) & 0xFF),
                static_cast<unsigned char>(px & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    XDestroyImage(img);
    XCloseDisplay(dpy);
    return true;
#else
    (void)path; (void)mean_lum; (void)nonuniform;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// TV 彩色 (A7): map_Kd 贴图加载 + box 重采样 → 统一尺寸 RGBA8 数组层
// ---------------------------------------------------------------------------
constexpr uint32_t kTexW = 256;       // 贴图数组统一宽 (JLG 613 层显存预算 157MB)
constexpr uint32_t kTexH = 256;

/// 加载贴图并 box 重采样至 kTexW×kTexH RGBA8。仅支持 8bit 4 通道未压缩格式
/// (vsgXchange stbi jpg/png 产物 ubvec4Array2D); 压缩 DDS/其他格式 → nullptr
/// (调用方落 Kd 占位层, hasTex=0 → shader 走 Kd 纯色分支)。
vsg::ref_ptr<vsg::Data> loadTextureLayer(const std::string& absPath) {
    auto options = vsg::Options::create();
    options->readerWriters.push_back(vsgXchange::all::create());
    auto data = vsg::read_cast<vsg::Data>(absPath, options);
    if (!data) return {};
    auto src = data->cast<vsg::ubvec4Array2D>();
    if (!src || src->width() == 0 || src->height() == 0) return {};
    const uint32_t sw = src->width(), sh = src->height();
    auto dst = vsg::ubvec4Array2D::create(kTexW, kTexH);
    dst->properties.format = VK_FORMAT_R8G8B8A8_UNORM;
    for (uint32_t ty = 0; ty < kTexH; ++ty) {
        const uint32_t sy0 = ty * sh / kTexH;
        const uint32_t sy1 = std::max(sy0 + 1, (ty + 1) * sh / kTexH);
        for (uint32_t tx = 0; tx < kTexW; ++tx) {
            const uint32_t sx0 = tx * sw / kTexW;
            const uint32_t sx1 = std::max(sx0 + 1, (tx + 1) * sw / kTexW);
            uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (uint32_t sy = sy0; sy < sy1; ++sy)
                for (uint32_t sx = sx0; sx < sx1; ++sx) {
                    const vsg::ubvec4& p = (*src)(sx, sy);
                    r += p.r; g += p.g; b += p.b; a += p.a; ++n;
                }
            (*dst)(tx, ty) = vsg::ubvec4(uint8_t(r / n), uint8_t(g / n),
                                         uint8_t(b / n), uint8_t(a / n));
        }
    }
    return dst;
}

/// Kd 纯色占位层 (贴图加载失败; SharedSceneResources「层=槽-1」契约要求层层存在)
vsg::ref_ptr<vsg::Data> makeKdLayer(const vsg::vec4& kd) {
    auto dst = vsg::ubvec4Array2D::create(kTexW, kTexH);
    dst->properties.format = VK_FORMAT_R8G8B8A8_UNORM;
    const vsg::ubvec4 c(
        uint8_t(std::clamp(kd.r, 0.0f, 1.0f) * 255.0f + 0.5f),
        uint8_t(std::clamp(kd.g, 0.0f, 1.0f) * 255.0f + 0.5f),
        uint8_t(std::clamp(kd.b, 0.0f, 1.0f) * 255.0f + 0.5f), 255);
    for (auto& px : *dst) px = c;
    return dst;
}

/// B1: 贴图平均亮度 (0..1, RGB 均值; 加载失败/非 8bit RGBA = -1)。亚采样步长 4。
/// 与 loadTextureLayer 同读路径 (vsgXchange), 库本体零贴图解码依赖原则不变。
float textureMeanLuminance(const std::string& absPath) {
    auto options = vsg::Options::create();
    options->readerWriters.push_back(vsgXchange::all::create());
    auto data = vsg::read_cast<vsg::Data>(absPath, options);
    if (!data) return -1.0f;
    auto src = data->cast<vsg::ubvec4Array2D>();
    if (!src || src->width() == 0 || src->height() == 0) return -1.0f;
    const size_t n = size_t(src->width()) * src->height();
    double sum = 0.0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i += 4) {
        const vsg::ubvec4& p = (*src)[i];
        sum += (p.r + p.g + p.b) / (3.0 * 255.0);
        ++cnt;
    }
    return cnt ? float(sum / double(cnt)) : -1.0f;
}

// ---------------------------------------------------------------------------
// 传感器注册表打印 (B1 多实例注册可见性)
// ---------------------------------------------------------------------------
void printRegistry(const std::vector<SensorInstance>& reg) {
    std::printf("[registry] 传感器实例 %zu 个 (场景驱动注册):\n", reg.size());
    for (const auto& inst : reg) {
        if (!inst.desc) continue;
        const SensorDesc& d = *inst.desc;
        std::string pos = "platform=未绑定 ";
        if (!d.platform.empty())
            pos = "platform=" + d.platform + (d.faction.empty() ? " " : "(" + d.faction + ") ");
        char line[512];
        if (d.frame_kind == "RADAR") {
            std::snprintf(line, sizeof(line),
                          "  #%d %-12s %-5s band=%-4s λ%.3gm %spos=(%.1f,%.1f,%.1f)  prf=%.0fHz  "
                          "beam=%.1fmrad  scan=%s%s",
                          d.sensor_id, d.name.c_str(), d.frame_kind.c_str(), d.band.c_str(),
                          d.wavelength, pos.c_str(), inst.position_enu.x, inst.position_enu.y,
                          inst.position_enu.z, d.pulse_rate, d.beam_width * 1000.0,
                          d.scan.mode.c_str(), "  [仅注册]");
        } else {
            std::snprintf(line, sizeof(line),
                          "  #%d %-12s %-5s λ%.2f-%.2fum %spos=(%.1f,%.1f,%.1f)  %ux%u fov%.0f°  "
                          "rate=%.1fHz  netd=%.0fmK  scan=%s",
                          d.sensor_id, d.name.c_str(), d.frame_kind.c_str(),
                          inst.profile.band.lambda_min * 1e6, inst.profile.band.lambda_max * 1e6,
                          pos.c_str(), inst.position_enu.x, inst.position_enu.y,
                          inst.position_enu.z, inst.profile.width, inst.profile.height,
                          inst.profile.fov_deg, d.rate_hz, d.netd_mk, d.scan.mode.c_str());
        }
        std::printf("%s\n", line);
    }
}

// ---------------------------------------------------------------------------
// P3: terrain_ref 解析 + .vsgterrain 地形注入 (TerrainProvider 契约)
// ---------------------------------------------------------------------------
/// 与 SceneSource::resolveMeshPath 同规则解析引用 (否则相对/绝对路径不等):
/// 原样(CWD) / 场景文件目录 / 其上级 / 上两级, 命中即返回弱规范化路径。
std::string resolveSceneRef(const std::string& scenario_path, const std::string& ref) {
    namespace fs = std::filesystem;
    if (ref.empty()) return ref;
    const fs::path p(ref);
    std::error_code ec;
    if (p.is_absolute()) return ref;
    const fs::path dir = fs::path(scenario_path).parent_path();
    const fs::path cands[] = {fs::path(ref), dir / ref, dir / ".." / ref,
                              dir / ".." / ".." / ref};
    for (const auto& c : cands) {
        if (fs::exists(c, ec)) {
            auto canon = fs::weakly_canonical(c, ec);
            return ec ? c.string() : canon.string();
        }
    }
    return ref;
}

/// .vsgterrain → TerrainMeshData (TerrainSystem 单一真值源: 高度场 → 三角形汤,
/// world=(i*cell, h, j*cell) Y-up ENU, cell=extent/(grid-1))。
/// .ter 高度图相对 .vsgterrain 目录解析 (保存时即最终高度, 含水面雕刻/笔刷);
/// 无 .ter → 程序化 generate+applyWater 兜底。温度/发射率置 0 →
/// buildThermalSceneGraph 回退场景 ground 缺省 (290K/ε0.92)。
bool loadTerrainProject(const std::string& vsgterrain_path, TerrainMeshData& out) {
    std::ifstream in(vsgterrain_path);
    if (!in) return false;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    const uint32_t grid = uint32_t(std::max(2, j.value("grid", 256)));
    const double extent = j.value("extent", 1000.0);
    auto terr = std::make_unique<vsgraysim::TerrainSystem>(
        grid, extent, j.value("height_scale", 60.0));
    terr->setSeed(uint32_t(j.value("seed", 20260810u)));
    bool haveHm = false;
    if (j.contains("heightmap") && j.at("heightmap").is_string()) {
        namespace fs = std::filesystem;
        const fs::path dir = fs::path(vsgterrain_path).parent_path();
        const std::string hm = j.at("heightmap").get<std::string>();
        std::error_code ec;
        for (const auto& c : {fs::path(hm), dir / hm}) {
            if (!fs::exists(c, ec)) continue;
            haveHm = terr->loadHeightmap(c.string());
            if (haveHm) break;
        }
    }
    if (!haveHm) {   // 无高度图: 程序化生成 + 水面雕刻 (与场景设计器同序)
        const int tt = std::clamp(j.value("terrain_type", 0), 0, 9);
        const int wt = std::clamp(j.value("water_type", 0), 0, 4);
        terr->generate(static_cast<vsgraysim::TerrainType>(tt));
        terr->applyWater(static_cast<vsgraysim::WaterType>(wt));
    }
    const uint32_t n = terr->grid();
    const double cell = terr->cellSize();
    const auto& h = terr->heightsData();
    if (n < 2 || h.size() != size_t(n) * n) return false;
    out.vertices.clear();
    out.vertices.reserve(size_t(n - 1) * (n - 1) * 6);
    for (uint32_t jz = 0; jz + 1 < n; ++jz) {
        for (uint32_t ix = 0; ix + 1 < n; ++ix) {
            const size_t k00 = size_t(jz) * n + ix;
            const size_t k10 = k00 + 1;
            const size_t k01 = size_t(jz + 1) * n + ix;
            const size_t k11 = k01 + 1;
            const float x0 = float(ix * cell), x1 = float((ix + 1) * cell);
            const float z0 = float(jz * cell), z1 = float((jz + 1) * cell);
            out.vertices.emplace_back(x0, float(h[k00]), z0);
            out.vertices.emplace_back(x0, float(h[k01]), z1);
            out.vertices.emplace_back(x1, float(h[k11]), z1);
            out.vertices.emplace_back(x0, float(h[k00]), z0);
            out.vertices.emplace_back(x1, float(h[k11]), z1);
            out.vertices.emplace_back(x1, float(h[k10]), z0);
        }
    }
    out.temp_k = 0.0f;      // 0 → SceneSource 回退场景 ground 缺省温度/发射率
    out.emissivity = 0.0f;
    return !out.vertices.empty();
}

// ---------------------------------------------------------------------------
// 全模块查询读数打印 (验证 Control/Query 接口契约; IEnvStack 能力转发)
// ---------------------------------------------------------------------------
void dumpReadouts(IEnvStack* stack, const vsg::dmat4& model)
{
    std::printf("\n===== rocky_env 模块状态 =====\n");

    // ---- sky ----
    IEnvModule* sky = stack ? stack->module("sky") : nullptr;
    if (sky) {
        std::printf("[sky]\n");
        if (auto* sun = stack->query<ISunProvider>()) {
            EnvSunInfo si;
            if (sun->sunInfo(model, si))
                std::printf("  星历太阳: 高度 %.1f°  方位 %.1f°\n",
                            si.elevationDeg, si.azimuthDeg);
        }
        if (auto* light = stack->query<ILightingProvider>()) {
            AtmosphereState st;
            if (light->getAtmosphereState(st))
                std::printf("  光照: 昼=%.2f 直射=(%.2f,%.2f,%.2f) 天光=(%.2f,%.2f,%.2f)\n",
                            st.is_day,
                            st.sun_light.x, st.sun_light.y, st.sun_light.z,
                            st.ambient_light.x, st.ambient_light.y, st.ambient_light.z);
        }
        if (auto* sky_ctl = stack->query<ISkyControl>())
            std::printf("  控制: 曝光 %.2f  浊度 %.2f  太阳 %d 月亮 %d 大气 %d\n",
                        sky_ctl->getExposure(), sky_ctl->getTurbidity(),
                        sky_ctl->getShowSun(), sky_ctl->getShowMoon(),
                        sky_ctl->getShowAtmosphere());
        // 大气透过率自检: 验证 LUT 数据是否物理合理 (天空全黑时用于区分
        // "LUT 数据错误" 与 "GPU 纹理上传/绑定错误")。
        if (auto* atmo = stack->query<IAtmosphereQuery>()) {
            // 契约: transmittance 坐标恒为 ECEF 米制; 查询点为锚点 ENU, 先变换。
            const vsg::dvec3 from = enuXform(model, vsg::dvec3(0.0, 0.0, 0.0));
            const vsg::dvec3 toZenith = enuXform(model, vsg::dvec3(0.0, 10000.0, 0.0));
            vsg::vec3 tau(0.0f, 0.0f, 0.0f), rad(0.0f, 0.0f, 0.0f);
            if (atmo->transmittance(from, toZenith, tau, rad))
                std::printf("  大气: 天顶方向透过率=(%.4f,%.4f,%.4f) 路径辐射=(%.4f,%.4f,%.4f)\n",
                            tau.x, tau.y, tau.z, rad.x, rad.y, rad.z);
            else
                std::printf("  大气: transmittance 查询失败\n");
            const vsg::dvec3 toSun = enuXform(model, vsg::dvec3(5000.0, 7000.0, 5000.0));
            vsg::vec3 tau2(0.0f, 0.0f, 0.0f), rad2(0.0f, 0.0f, 0.0f);
            if (atmo->transmittance(from, toSun, tau2, rad2))
                std::printf("  大气: 太阳方向透过率=(%.4f,%.4f,%.4f) 路径辐射=(%.4f,%.4f,%.4f)\n",
                            tau2.x, tau2.y, tau2.z, rad2.x, rad2.y, rad2.z);
        }
    } else {
        std::printf("[sky] 未加载\n");
    }

    // ---- cloud ----
    IEnvModule* cloud = stack ? stack->module("cloud") : nullptr;
    if (cloud) {
        std::printf("[cloud]\n");
        if (auto* cloud_q = stack->query<ICloudQuery>()) {
            float base = 0.0f, top = 0.0f;
            const bool hb = cloud_q->cloudBase(base);
            const bool ht = cloud_q->cloudTop(top);
            std::printf("  云底 %s", hb ? (std::to_string(base) + " m").c_str() : "—");
            std::printf("  云顶 %s", ht ? (std::to_string(top) + " m").c_str() : "—");
            std::printf("  总云量 %.3f\n", cloud_q->totalCoverage());
            std::printf("  云影因子(原点) 太阳 80°: %.3f | 60°: %.3f | 30°: %.3f\n",
                        cloud_q->shadowFactor(0.0, 0.0, 80.0 * M_PI / 180.0),
                        cloud_q->shadowFactor(0.0, 0.0, 60.0 * M_PI / 180.0),
                        cloud_q->shadowFactor(0.0, 0.0, 30.0 * M_PI / 180.0));
            // 云影空间分布统计 (验证云影随位置变化, 非恒定值)。
            {
                const double el = 60.0 * M_PI / 180.0;
                float smin = 1e9f, smax = -1e9f; double ssum = 0.0;
                const int N = 300;
                for (int i = 0; i < N; ++i) {
                    const double ex = -1500.0 + i * 10.0;
                    const double ez = std::sin(i * 0.53) * 800.0;
                    const float sf = cloud_q->shadowFactor(ex, ez, el);
                    smin = std::min(smin, sf); smax = std::max(smax, sf);
                    ssum += sf;
                }
                std::printf("  云影分布 (%d 点, 太阳60°): [%.3f, %.3f]  均值 %.3f\n",
                            N, smin, smax, ssum / N);
            }
        }
        if (auto* cloud_ctl = stack->query<ICloudControl>()) {
            const auto layers = cloud_ctl->getLayers();
            std::printf("  控制: 全局云量 %.2f  层数 %zu\n",
                        cloud_ctl->getCoverage(), layers.size());
            for (size_t i = 0; i < layers.size(); ++i)
                std::printf("    层%zu 类型 %d 高度 %.0f m 覆盖 %.2f 密度 %.2f\n",
                            i, static_cast<int>(layers[i].type), layers[i].altitude,
                            layers[i].coverage, layers[i].density);
        }
    } else {
        std::printf("[cloud] 未加载\n");
    }

    // ---- ocean ----
    IEnvModule* ocean = stack ? stack->module("ocean") : nullptr;
    if (ocean) {
        std::printf("[ocean]\n");
        if (auto* ocean_q = stack->query<IOceanSampler>()) {
            const SeaState ss = ocean_q->seaState();
            std::printf("  海况: Hs %.3f m  Tp %.2f s  蒲福 %d 级  主波向 %.1f°\n",
                        ss.significant_wave_height, ss.peak_period, ss.beaufort,
                        ss.peak_direction_rad * 180.0 / M_PI);
            // 多点采样 (原点相位恒为 0, 需非原点/非零时刻才有代表性):
            // 统计一波面的高度起伏与法线倾斜, 验证波场确实在动。
            const double t = 1.7;
            float hmin = 1e9f, hmax = -1e9f, tilt_max = 0.0f, foam_max = 0.0f;
            double vmax = 0.0;
            for (int i = 0; i < 400; ++i) {
                const double ex = -300.0 + i * 1.5;
                const double ez = 120.0 + std::sin(i * 0.37) * 200.0;
                const float h = ocean_q->waveHeight(ex, ez, t);
                hmin = std::min(hmin, h); hmax = std::max(hmax, h);
                const vsg::vec3 n = ocean_q->waveNormal(ex, ez, t);
                tilt_max = std::max(tilt_max, std::sqrt(n.x * n.x + n.z * n.z));
                foam_max = std::max(foam_max, ocean_q->foamCoverage(ex, ez, t));
                vmax = std::max(vmax, static_cast<double>(
                    vsg::length(ocean_q->orbitalVelocity(ex, ez, t))));
            }
            std::printf("  波面起伏 (400 点, t=1.7s): y ∈ [%.3f, %.3f] m  峰谷差 %.3f m\n",
                        hmin, hmax, hmax - hmin);
            std::printf("  最大法线倾斜 %.4f  最大白沫 %.4f  最大轨道速度 %.3f m/s\n",
                        tilt_max, foam_max, vmax);
            std::printf("  (Hs=%.2f → 峰谷差应约 2×Hs=%.2f m)\n",
                        ss.significant_wave_height, 2.0 * ss.significant_wave_height);
        }
        if (auto* ocean_ctl = stack->query<IOceanControl>())
            std::printf("  控制: 风速 %.2f m/s  Hs %.3f m  反射 %d  曲率 %d  涌浪 Hs %.3f\n",
                        ocean_ctl->getWindSpeed(), ocean_ctl->getWaveHeight(),
                        ocean_ctl->getReflection(), ocean_ctl->getCurvature(),
                        ocean_ctl->getSwellHeight());
    } else {
        std::printf("[ocean] 未加载\n");
    }
    std::printf("==============================\n\n");
}

// ---------------------------------------------------------------------------
// 相机预设
// ---------------------------------------------------------------------------
/// C1: 纯环境相机预设 (ENU 机位 → ECEF 世界)。此前默认机位 eye(0,55,-260)→
/// center(0,30,400) 为俯视 -2°, 垂直 FOV 55° 上缘仅 +25°, 而层云 420m@660m
/// 需 29° 仰角 —— 云永远在画面外。预设 1 改为微仰角机位 (上缘 ~+32°)。
struct CamPreset { vsg::dvec3 eye; vsg::dvec3 center; const char* desc; };
const CamPreset kCamPresets[] = {
    // ENU 分量 = makeEnuToWorldMatrix 的列序: (x=东, y=上, z=南=-北)!
    // (历史坑: 本表曾按 (East,North,Up) 书写 —— "高度"被写进 z(南北) 分量,
    //  y(铅垂) 反而是负值 → 相机在椭球面以下 1200m, 画面只剩朝天视野。
    //  y 必须为正才在地面上。)
    { vsg::dvec3(0.0, 2500.0, -1200.0), vsg::dvec3(0.0, 3000.0, -11200.0), "同框全景 (天空/云/地面)" },
    { vsg::dvec3(0.0, 8.0, 0.0),        vsg::dvec3(0.0, 12.0, -600.0),     "掠海 (海面主导)" },
    { vsg::dvec3(0.0, 20.0, -200.0),    vsg::dvec3(0.0, 900.0, -140.0),    "仰天 (观星/云底)" },
    { vsg::dvec3(0.0, 3000.0, 0.0),     vsg::dvec3(0.0, 0.0, -50.0),       "俯视 (地形纹理检查)" },
};

/// 观察口机位 (传感器模式; 程序化场景缺省, --scene 按场景包围盒自适应覆盖)。
/// 机位为 ENU 局部坐标, applyPreset 时经 enu_to_ecef 变换到 ECEF 世界
/// (rocky 地球基座的世界根)。
struct Rig {
    CamPreset presets[3] = {
        {{0.0, 2500.0, -1200.0}, {-2874.0, 1905.0, 4963.0}}, // 俯角 -5° 朝南偏西 205°:
                                                           // 眼高 2500m (视距 178km)，
                                                           // 海平线 ~42% 高; 宜兰海岸
                                                           // (104km) + 中央山脉剪影入画
                                                           // —— 地形昼夜光照可验 (内置
                                                           // DEM 仅 z5, 彭佳屿拉平不可见)
        {{22.0, 6.0, 5.0},    {0.0, 1.5, -40.0}},   // 近观目标
        {{0.0, 2.5, 30.0},    {0.0, 1.2, -40.0}},   // 近似传感器视线
    };
    vsg::ref_ptr<vsg::LookAt> lookAt;
    vsg::ref_ptr<vsg::Perspective> persp;
    vsg::dmat4 enu_to_ecef;    ///< ENU 机位 → ECEF 世界 (rocky 地球基座)
    vsg::dvec3 up_ecef{0.0, 1.0, 0.0};
    int preset = 0;
    double vfov = 55.0, aspect = 1.0, nearZ = 0.5, farZ = 5000.0;

    void applyPreset(int p) {
        preset = std::clamp(p, 0, 2);
        lookAt->eye = enuXform(enu_to_ecef, presets[preset].eye);
        lookAt->center = enuXform(enu_to_ecef, presets[preset].center);
        lookAt->up = up_ecef;
    }
    vsg::dmat4 view() const { return lookAt->transform(); }
    vsg::dmat4 proj() const { return persp->transform(); }
};

// ---------------------------------------------------------------------------
// 观察口场景几何预览管线 (传感器模式)。
//
// 热场景几何 (VertexIndexDraw, binding0=pos(vec3)) 在传感器管线内由
// sensor_rad/sensor_vis 着色; 观察口 (rocky ECEF 世界) 需要一条独立的最小
// 管线渲染同一几何 —— 内联 GLSL 源 (运行时 shaderc 编译, 与 env 模块同路径),
// 顶点仅取 binding0, UBO 传 CPU 双精度组装的 mvp (proj*view*ENU→ECEF)。
// 深度: 反向 Z (GREATER + 写深度), 与 rocky 地形正确互遮。
// ---------------------------------------------------------------------------
struct ScenePreview {
    vsg::ref_ptr<vsg::StateGroup> sg;
    vsg::ref_ptr<vsg::Value<vsg::mat4>> mvp;   // 每帧 CPU 侧组装后写入 (record 时读取)
};

ScenePreview createScenePreview(const VkExtent2D& viewport)
{
    ScenePreview out;
    const std::string vs = R"(#version 450
layout(set=0,binding=0) uniform ObserverView { mat4 mvp; } pc;
layout(location=0) in vec3 pos;
layout(location=0) out vec3 vPos;
void main() {
    vPos = pos;
    gl_Position = pc.mvp * vec4(pos, 1.0);
}
)";
    const std::string fs = R"(#version 450
layout(location=0) in vec3 vPos;
layout(location=0) out vec4 outColor;
void main() {
    // 高度调制双色 (地面深绿 / 物体灰褐), 预览用途
    float t = clamp(vPos.y * 0.04, 0.0, 1.0);
    vec3 base = mix(vec3(0.16, 0.22, 0.13), vec3(0.45, 0.42, 0.38), t);
    outColor = vec4(base, 1.0);
}
)";
    auto vert = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vs);
    auto frag = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fs);
    auto compiler = vsg::ShaderCompiler::create();
    vsg::ShaderStages stages{vert, frag};
    if (!compiler || !compiler->supported() || !compiler->compile(stages)) {
        std::fprintf(stderr, "[preview] 预览 shader 编译失败 (观察口无场景几何)\n");
        return out;
    }

    // 顶点输入: 仅 binding0 pos (热场景 binding0=pos(vec3); 其余绑定本管线不消费)
    auto vis = vsg::VertexInputState::create();
    vis->vertexAttributeDescriptions.resize(1);
    vis->vertexAttributeDescriptions[0].location = 0;
    vis->vertexAttributeDescriptions[0].binding = 0;
    vis->vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[0].offset = 0;
    vis->vertexBindingDescriptions.resize(1);
    vis->vertexBindingDescriptions[0].binding = 0;
    vis->vertexBindingDescriptions[0].stride = sizeof(vsg::vec3);
    vis->vertexBindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE;

    auto ms = vsg::MultisampleState::create();

    // 反向 Z: 本 vsg 约定 (清屏深度 0.0 + GREATER), 与 rocky 地形一致互遮。
    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = VK_TRUE;
    ds->depthWriteEnable = VK_TRUE;
    ds->depthCompareOp = VK_COMPARE_OP_GREATER;

    auto bs = vsg::ColorBlendState::create();
    bs->attachments.resize(1);
    bs->attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                        VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT |
                                        VK_COLOR_COMPONENT_A_BIT;

    // 静态 ViewportState (本 vsg 无逐帧 vkCmdSetViewport, 管线必须显式挂)。
    auto vp = vsg::ViewportState::create(viewport);

    auto viewLayout=vsg::DescriptorSetLayout::create();
    viewLayout->addBinding(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT);
    auto plc = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{viewLayout},vsg::PushConstantRanges{});
    auto gp = vsg::GraphicsPipeline::create(plc, stages, vsg::GraphicsPipelineStates{
        vis, ias, rs, ms, ds, bs, vp});
    auto bind = vsg::BindGraphicsPipeline::create(gp);

    out.mvp = vsg::Value<vsg::mat4>::create();
    out.mvp->properties.dataVariance=vsg::DYNAMIC_DATA;

    out.sg = vsg::StateGroup::create();
    out.sg->add(bind);
    out.sg->add(vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS,plc,0,
        vsg::Descriptors{vsg::DescriptorBuffer::create(out.mvp,0)}));
    return out;
}

// ---------------------------------------------------------------------------
// 键盘事件处理器 (切模块 / 相机预设 / 调时间 / 调海况 / 打印读数 / 退出)
// ---------------------------------------------------------------------------
struct ViewerState {
    bool sensorMode = false;            ///< 传感器链启用 (键位上下文)
    IEnvStack* stack = nullptr;
    Args* args = nullptr;
    Mode mode = Mode::All;
    double hours_utc = 4.0;
    bool dump_requested = false;
    vsg::dmat4 enu_to_ecef;             ///< 锚点 ENU→ECEF (相机预设换算/读数查询)
    vsg::dvec3 up_ecef{0.0, 1.0, 0.0};
    // 纯环境相机 (kCamPresets)
    vsg::ref_ptr<vsg::LookAt> envLookAt;   ///< 相机预设直接改写 eye/center
    int cam_preset = 1;
    // 传感器模式观察口相机 (Rig)
    Rig* rig = nullptr;
    bool quit_requested = false;
};

/// C1: 纯环境相机预设应用 (ENU 机位 → ECEF 世界, up 取锚点铅垂)。
void applyEnvCamPreset(ViewerState* st, int idx)
{
    if (!st || !st->envLookAt) return;
    const int n = static_cast<int>(sizeof(kCamPresets) / sizeof(kCamPresets[0]));
    idx = std::clamp(idx, 0, n - 1);
    st->envLookAt->eye = enuXform(st->enu_to_ecef, kCamPresets[idx].eye);
    st->envLookAt->center = enuXform(st->enu_to_ecef, kCamPresets[idx].center);
    st->envLookAt->up = enuXform(st->enu_to_ecef, vsg::dvec3(0.0, 1.0, 0.0), 0.0);
    st->cam_preset = idx + 1;
    std::printf("[cam] 预设 %d (%s): eye(%.0f,%.0f,%.0f) center(%.0f,%.0f,%.0f)\n",
                idx + 1, kCamPresets[idx].desc,
                kCamPresets[idx].eye.x, kCamPresets[idx].eye.y, kCamPresets[idx].eye.z,
                kCamPresets[idx].center.x, kCamPresets[idx].center.y, kCamPresets[idx].center.z);
    std::fflush(stdout);
}

class LabKeyHandler : public vsg::Inherit<vsg::Visitor, LabKeyHandler> {
public:
    explicit LabKeyHandler(ViewerState* st) : st_(st) {}

    void apply(vsg::KeyPressEvent& e) override
    {
        if (!st_) return;
        const uint32_t code = static_cast<uint32_t>(e.keyBase);
        switch (code) {
            case vsg::KEY_S:
                st_->mode = (st_->mode == Mode::Sky) ? Mode::All : Mode::Sky;
                applyMode(st_->stack, st_->mode);
                std::printf("[mode] sky %s\n", st_->mode == Mode::Sky ? "单独" : "全部");
                break;
            case vsg::KEY_C:
                st_->mode = (st_->mode == Mode::Cloud) ? Mode::All : Mode::Cloud;
                applyMode(st_->stack, st_->mode);
                std::printf("[mode] cloud %s\n", st_->mode == Mode::Cloud ? "单独" : "全部");
                break;
            case vsg::KEY_O:
                st_->mode = (st_->mode == Mode::Ocean) ? Mode::All : Mode::Ocean;
                applyMode(st_->stack, st_->mode);
                std::printf("[mode] ocean %s\n", st_->mode == Mode::Ocean ? "单独" : "全部");
                break;
            case vsg::KEY_A:
                st_->mode = Mode::All;
                applyMode(st_->stack, st_->mode);
                std::printf("[mode] 全部\n");
                break;
            case vsg::KEY_1:
            case vsg::KEY_2:
            case vsg::KEY_3:
                if (st_->sensorMode && st_->rig) {
                    st_->rig->applyPreset(int(code - vsg::KEY_1));
                    std::printf("[camera] 观察口预设 %d\n", st_->rig->preset + 1);
                } else {
                    applyEnvCamPreset(st_, int(code - vsg::KEY_1));
                }
                break;
            case vsg::KEY_4:
                if (!st_->sensorMode) applyEnvCamPreset(st_, 3);
                break;
            case vsg::KEY_T:
                st_->hours_utc = std::fmod(st_->hours_utc + 1.0 + 24.0, 24.0);
                std::printf("[time] UTC %.1f h\n", st_->hours_utc);
                break;
            case vsg::KEY_t:
                st_->hours_utc = std::fmod(st_->hours_utc - 1.0 + 24.0, 24.0);
                std::printf("[time] UTC %.1f h\n", st_->hours_utc);
                break;
            case vsg::KEY_Leftbracket:
                st_->args->wind = std::max(0.0f, st_->args->wind - 1.0f);
                // 海洋下一帧经 EnvFrame 风自动跟随 (auto_sea_state; 栈喂数)
                std::printf("[wind] %.1f m/s\n", st_->args->wind);
                break;
            case vsg::KEY_Rightbracket:
                st_->args->wind += 1.0f;
                std::printf("[wind] %.1f m/s\n", st_->args->wind);
                break;
            case vsg::KEY_Minus:
                st_->args->cover = std::max(0.0f, st_->args->cover - 0.1f);
                if (auto* cloud_ctl = st_->stack ? st_->stack->query<ICloudControl>() : nullptr)
                    cloud_ctl->setCoverage(st_->args->cover);
                std::printf("[cloud] 云量 %.2f\n", st_->args->cover);
                break;
            case vsg::KEY_Equals:
                st_->args->cover = std::min(1.0f, st_->args->cover + 0.1f);
                if (auto* cloud_ctl = st_->stack ? st_->stack->query<ICloudControl>() : nullptr)
                    cloud_ctl->setCoverage(st_->args->cover);
                std::printf("[cloud] 云量 %.2f\n", st_->args->cover);
                break;
            case vsg::KEY_P:
                st_->dump_requested = true;
                break;
            case vsg::KEY_Escape:
                st_->quit_requested = true;
                break;
            default:
                break;
        }
    }

    void apply(vsg::KeyReleaseEvent&) override {}
    void apply(vsg::CloseWindowEvent&) override { if (st_) st_->quit_requested = true; }

private:
    ViewerState* st_ = nullptr;
};

}  // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const Args args = parseArgs(argc, argv);
    auto observerCoordinate=[](const std::string& text,vsg::dvec3& value) {
        if(text.empty())return true;
        std::istringstream stream(text);char first=0,second=0;
        if(!(stream>>value.x>>first>>value.y>>second>>value.z)||first!=','||second!=',')return false;
        stream>>std::ws;
        return stream.eof()&&std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);
    };
    vsg::dvec3 observerEye,observerCenter;
    if(!observerCoordinate(args.obs_eye,observerEye)||!observerCoordinate(args.obs_center,observerCenter)||
       (args.has_fft_patch&&(!std::isfinite(args.fft_patch)||args.fft_patch<=0))) {
        std::fprintf(stderr,"Invalid --obs-eye/--obs-center (finite x,y,z required) or --fft-patch (positive meters required)\n");return 2;
    }
    // 固定双视口形态 (用户要求唯一测试形态): 左=可见光观察口 (rocky 地球),
    // 右=红外/TV 传感器成像。不再有纯环境单视口模式。
    const bool sensorMode = true;

    // ---- 优先 NVIDIA 独显 ICD ----
    //
    // 重要: Vulkan loader 认的环境变量是 VK_ICD_FILENAMES (旧名) / VK_DRIVER_FILES
    // (1.3.207+ 新名), **不是** VK_ICD_FILE_LIST —— 后者会被 loader 完全忽略, 于是
    // 进程会退回到默认多 ICD 枚举顺序, 命中 lavapipe (llvmpipe) 软件光栅化, 表现出
    // "窗口能开、帧能跑、但管线特性缺失/画面异常" 的症状。
    // 这里两个变量都设, 并支持 --icd 显式指定; 若用户已显式设置则不覆盖。
    {
        const char* candidates[] = {
            "/usr/share/vulkan/icd.d/nvidia_icd.json",
            "/etc/vulkan/icd.d/nvidia_icd.json",
            nullptr
        };
        const bool already = (std::getenv("VK_ICD_FILENAMES") != nullptr) ||
                             (std::getenv("VK_DRIVER_FILES") != nullptr);
        if (!already) {
            const char* chosen = args.icd_.empty() ? nullptr : args.icd_.c_str();
            if (!chosen) {
                for (auto* p = candidates; *p; ++p) {
                    if (::access(*p, R_OK) == 0) { chosen = *p; break; }
                }
            }
            if (chosen) {
                setenv("VK_ICD_FILENAMES", chosen, 0);
                setenv("VK_DRIVER_FILES", chosen, 0);
                std::printf("[icd] 使用 ICD: %s\n", chosen);
            } else {
                std::printf("[icd] 未找到 nvidia ICD, 使用 loader 默认枚举\n");
            }
        } else {
            const char* f = std::getenv("VK_ICD_FILENAMES");
            const char* d = std::getenv("VK_DRIVER_FILES");
            std::printf("[icd] 沿用外部设置: VK_ICD_FILENAMES=%s VK_DRIVER_FILES=%s\n",
                        f ? f : "(unset)", d ? d : "(unset)");
        }
    }

    // ---- 工作目录: 不再 chdir ----
    // 旧逻辑向上寻找含 shaders/env 的目录并 chdir, 会命中 build/shaders/env
    // (构建期拷贝) —— 此后 resolveEnvShader 的 CWD 相对候选全部落在**过期
    // 构建副本**上, 造成 "改 shader 不生效 / 探针失踪" 的调试假象。
    // shader 解析统一由 resolveEnvShader 的候选序完成 (源码树优先)。

    std::printf("=== rocky_env 统一验证程序 (%s模式) ===\n",
                sensorMode ? "环境+传感器" : "纯环境");
    if (sensorMode)
        std::printf("波段: %s  场景: %s\n",
                    args.band.c_str(), args.scene.empty() ? "(程序化热场景)" : args.scene.c_str());
    std::printf("模式: %s  (Rocky ECEF 地球基座)  时间 UTC %.1f h  风 %.1f m/s  云量 %.2f\n",
                args.mode.c_str(), args.hours_utc, args.wind, args.cover);

    printVulkanDevices();
    if (args.list_icd) return 0;

    // ---- B6: VK_EXT_shader_atomic_float 探测 (radar_echo 回波原子浮点累加依赖)。
    // 设备扩展须在设备创建时注入 → 在窗口创建前用临时 VkInstance 探测物理设备。
    bool atomicFloatOK = false;
    if (sensorMode && !args.no_rt) {
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.pApplicationName = "env_viewer_probe";
        ai.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &ai;
        VkInstance vi;
        if (vkCreateInstance(&ci, nullptr, &vi) == VK_SUCCESS) {
            uint32_t nd = 0;
            if (vkEnumeratePhysicalDevices(vi, &nd, nullptr) == VK_SUCCESS && nd > 0) {
                std::vector<VkPhysicalDevice> devs(nd);
                if (vkEnumeratePhysicalDevices(vi, &nd, devs.data()) == VK_SUCCESS) {
                    for (VkPhysicalDevice pd : devs) {
                        uint32_t ne = 0;
                        if (vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, nullptr) != VK_SUCCESS ||
                            ne == 0)
                            continue;
                        std::vector<VkExtensionProperties> exts(ne);
                        if (vkEnumerateDeviceExtensionProperties(pd, nullptr, &ne, exts.data()) != VK_SUCCESS)
                            continue;
                        bool hasExt = false;
                        for (const auto& e : exts)
                            if (std::strcmp(e.extensionName,
                                            VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME) == 0)
                                hasExt = true;
                        if (!hasExt) continue;
                        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT af{
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT};
                        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                        f2.pNext = &af;
                        vkGetPhysicalDeviceFeatures2(pd, &f2);
                        if (af.shaderBufferFloat32AtomicAdd == VK_TRUE) {
                            atomicFloatOK = true;
                            break;
                        }
                    }
                }
            }
            vkDestroyInstance(vi, nullptr);
        }
        std::printf("[radar] VK_EXT_shader_atomic_float: %s\n",
                    atomicFloatOK ? "支持 (回波原子累加可用)" : "不支持 (回波链不可用)");
    }

    // ROCKY_VIEWPORT: rocky 地形管线与 env 模块管线的静态 ViewportState 尺寸
    // (本 vsg 无逐帧 vkCmdSetViewport, 管线必须显式挂静态 viewport, 见
    //  TerrainState.cpp / EnvCommon.cpp)。与窗口实际尺寸保持一致。
    // 传感器模式: 唯一使用 rocky/env 管线的 view 是左半观察口 → 视口取半宽。
    const uint32_t obsW = sensorMode ? uint32_t(args.width / 2) : uint32_t(args.width);
    const VkExtent2D obsExtent{obsW, uint32_t(args.height)};
    {
        std::string vp = std::to_string(obsW) + "x" + std::to_string(args.height);
        setenv("ROCKY_VIEWPORT", vp.c_str(), 0);  // 不覆盖外部显式设置
    }

    // ---- VSG 窗口 / 查看器 ----
    auto viewer = vsg::Viewer::create();
    // B2: RT 设备扩展/特性 (BandRT 需 VK_KHR_ray_tracing_pipeline 等), 仅传感器
    // 模式申请。窗口创建失败 (驱动无 RT) → 回退基础 traits, P0 走光栅化路径。
    auto makeTraits = [&](bool with_rt) -> vsg::ref_ptr<vsg::WindowTraits> {
        auto t = vsg::WindowTraits::create(uint32_t(args.width), uint32_t(args.height),
                                           "rocky_env viewer");
        t->debugLayer = args.debug_layer;
        t->apiDumpLayer = false;
        t->vulkanVersion=VK_API_VERSION_1_1;
        t->queueFlags=VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT;
        if (with_rt) {
            t->queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            t->vulkanVersion = VK_API_VERSION_1_1;
            t->deviceExtensionNames = {
                VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
                VK_KHR_SPIRV_1_4_EXTENSION_NAME,
                VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME};
            auto features = t->deviceFeatures = vsg::DeviceFeatures::create();
            features->get();
            features->get<VkPhysicalDeviceBufferDeviceAddressFeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES>()
                .bufferDeviceAddress = VK_TRUE;
            features->get<VkPhysicalDeviceRayTracingPipelineFeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR>()
                .rayTracingPipeline = VK_TRUE;
            features->get<VkPhysicalDeviceAccelerationStructureFeaturesKHR,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR>()
                .accelerationStructure = VK_TRUE;
            // B6: 回波合成原子浮点 (探测支持时注入; 不支持时回波链在选中处拦截)
            if (atomicFloatOK) {
                t->deviceExtensionNames.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
                features->get<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT,
                              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT>()
                    .shaderBufferFloat32AtomicAdd = VK_TRUE;
            }
        }
        return t;
    };
    if (sensorMode && args.no_rt) {
        std::fprintf(stderr, "[rt] --no-rt: 强制光栅化 P0 路径\n");
    }
    vsg::ref_ptr<vsg::Window> window;
    if (sensorMode) {
        try {
            window = vsg::Window::create(makeTraits(!args.no_rt));
        } catch (const vsg::Exception& e) {
            std::fprintf(stderr, "[rt] RT 窗口创建异常 (%s) → 回退基础窗口\n", e.message.c_str());
        }
        if (!window) {
            std::fprintf(stderr, "[rt] RT 特性窗口创建失败 → 回退基础窗口 (P0 光栅化)\n");
            try {
                window = vsg::Window::create(makeTraits(false));
            } catch (const vsg::Exception& e) {
                std::fprintf(stderr, "无法创建 VSG 窗口: %s\n", e.message.c_str());
                return 1;
            }
        }
    } else {
        window = vsg::Window::create(makeTraits(false));
    }
    if (!window) {
        std::fprintf(stderr, "错误: 无法创建 VSG 窗口\n");
        return 1;
    }
    bool readbackSupported=false;
    try {
        auto traits=window->traits();
        auto physical=window->getOrCreatePhysicalDevice();
        rockyenv::configureRockyGraphicsFeatures(*physical,traits->deviceExtensionNames,traits->deviceFeatures);
        VkSurfaceCapabilitiesKHR capabilities{};
        if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical->vk(),*window->getOrCreateSurface(),&capabilities)==VK_SUCCESS)
            readbackSupported=(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;
        if(readbackSupported)traits->swapchainPreferences.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        else std::fprintf(stderr,"[shot] Surface does not support swapchain transfer-source usage; use X11 capture\n");
    }catch(const std::exception& error){std::fprintf(stderr,"Rocky graphics unavailable: %s\n",error.what());return 1;}
    // addWindow 延后到 MapNode 创建之后 (rocky_simple 顺序): MapNode::create
    // 依赖 VSGContextImpl::device(), 窗口未入 viewer 时该指针为空。

    // 从 VSG 回写的 traits 里取出真实的 X11 窗口 ID。
    unsigned long owned_win = 0;
#if ENV_VIEWER_HAVE_XCB
    if (window->traits() && window->traits()->nativeWindow.has_value()) {
        try {
            owned_win = static_cast<unsigned long>(
                std::any_cast<xcb_window_t>(window->traits()->nativeWindow));
        } catch (const std::bad_any_cast&) {
            owned_win = 0;
        }
    }
#endif
    if (owned_win)
        std::printf("[window] 实际 X11 窗口 id=0x%lx  visible=%d\n",
                    owned_win, window->visible() ? 1 : 0);
    else
        std::printf("[window] 未能取回 X11 窗口 id, 截图将回退根窗口\n");
    std::fflush(stdout);

    // 用 VSG 自己的 xcb 连接校验窗口确实存在 (排除"窗口未真正创建"的误判)。
#if ENV_VIEWER_HAVE_XCB
    if (owned_win && window->traits() && window->traits()->systemConnection.has_value()) {
        try {
            auto* conn = std::any_cast<xcb_connection_t*>(window->traits()->systemConnection);
            if (conn) {
                xcb_get_geometry_cookie_t ck = xcb_get_geometry(conn, static_cast<xcb_window_t>(owned_win));
                xcb_get_geometry_reply_t* rp = xcb_get_geometry_reply(conn, ck, nullptr);
                if (rp) {
                    std::printf("[window] xcb 几何校验: %ux%u (窗口在 X 服务器上真实存在)\n",
                                rp->width, rp->height);
                    free(rp);
                } else {
                    std::printf("[window] xcb 几何校验失败: 窗口不在 X 服务器上!\n");
                }
                xcb_get_window_attributes_cookie_t ac =
                    xcb_get_window_attributes(conn, static_cast<xcb_window_t>(owned_win));
                xcb_get_window_attributes_reply_t* ar =
                    xcb_get_window_attributes_reply(conn, ac, nullptr);
                if (ar) {
                    std::printf("[window] map_state=%u (2=IsViewable)\n", ar->map_state);
                    free(ar);
                }
            }
        } catch (const std::bad_any_cast&) {}
    }
#endif
    std::fflush(stdout);

    // ---- rocky 上下文 (与 gui RockyMapFeature 同配方; SkyNode 必需) ----
    // 数据路径兜底: PROJ 缺 proj.db 会使 SRS::ECEF 无效 → SkyNode 不创建
    // 太阳光 (setDateTime 段错误); ROCKY_FILE_PATH 供 rocky 定位自带 shader。
    rockyenv::runtime::configureDefaults();
    {
        std::printf("[rocky] 创建 VSGContext (绑定本 viewer)...\n");
        std::fflush(stdout);
    }
    vsg::ref_ptr<vsg::Viewer> viewer_ref(viewer);
    auto rocky_singleton = rocky::VSGContextFactory::create(viewer_ref);   // 按值, 持有至 main 结束
    rocky::VSGContextImpl* rocky_ctx = rocky_singleton.get();
    if (!rocky_ctx) {
        std::fprintf(stderr, "错误: rocky VSGContext 创建失败\n");
        return 1;
    }

    // ---- rocky 地球底座 (MapNode; 与 gui RockyMapFeature 同配方) ----
    // 注意: 此处不预创建 CompileManager。瓦片分页编译全部发生在帧循环的
    // mapNode->update() 中 (viewer->compile() 之后), 编译段会以窗口级
    // Context (带 renderPass) 创建 CompileManager; 若用 device 级 Context
    // 提前编译瓦片管线, vkCreateGraphicsPipelines 会拿到空 renderPass,
    // 管线在 GPU 侧无效 → 地形零光栅化 (S0 根因)。
    vsg::ref_ptr<rocky::MapNode> mapNode;
    if (!args.no_map) {
        mapNode = rocky::MapNode::create(rocky_ctx);
        if (!mapNode) {
            std::fprintf(stderr, "警告: rocky MapNode 创建失败, 继续无地球运行\n");
        } else {
            // 开启地形光照 (rocky 默认 lighting=false = unlit 影像直出, 24h
            // 昼夜循环下瓦片恒原色不随太阳变化)。开启后 sky_->sun/ambient
            // (vsg::DirectionalLight, SkyModule::applyLights 每帧按星历驱动)
            // 才会作用于瓦片 —— 夜间地形变黑、晨昏染色、白天有方向光影。
            mapNode->terrainSettings().lighting = true;
            // --map 地图 JSON 优先 (TMSImage/TMSElevation, 代码方式加载)。
            if (!args.map_json.empty()) {
                std::ifstream f(args.map_json);
                std::stringstream ss;
                if (f) ss << f.rdbuf();
                auto j = nlohmann::json::parse(ss.str(), nullptr, false);
                if (j.is_object() && j.contains("layers")) {
                    for (const auto& l : j["layers"]) {
                        if (!l.is_object()) continue;
                        const std::string type = l.value("type", std::string());
                        const std::string uri  = l.value("uri",  std::string());
                        if (uri.empty()) continue;
                        const std::string profile =
                            l.value("profile", std::string("global-geodetic"));
                        auto mkTms = [&](const std::string& base) {
                            if (base.find("{z}") != std::string::npos) return base;
                            return base + "/{z}/{x}/{-y}.tif";
                        };
                        if (type == "TMSImage") {
                            auto layer = rocky::TMSImageLayer::create();
                            layer->uri = mkTms(uri);
                            layer->profile = rocky::Profile(profile);
                            layer->name = l.value("name", std::string());
                            layer->maxLevel = l.value("maxLevel", 3);
                            mapNode->map->add(layer);
                        } else if (type == "TMSElevation") {
                            auto layer = rocky::TMSElevationLayer::create();
                            layer->uri = mkTms(uri);
                            layer->profile = rocky::Profile(profile);
                            layer->name = l.value("name", std::string());
                            layer->maxLevel = l.value("maxLevel", 5);
                            mapNode->map->add(layer);
                        }
                    }
                    std::printf("[rocky] map '%s' loaded (%zu layers)\n",
                                args.map_json.c_str(), j["layers"].size());
                } else {
                    std::fprintf(stderr, "警告: map JSON 格式无效 '%s'\n",
                                 args.map_json.c_str());
                }
            }
            // 图层为空 → 内置离线 TMS 全球底图+高程兜底。
            if (mapNode->map->layers().empty()) {
                auto img = rocky::TMSImageLayer::create();
                img->uri = std::string("/home/mte/EarthData/World.earth.tms_repo/worldTexture_tiff")
                         + "/{z}/{x}/{-y}.tif";
                img->profile = rocky::Profile("global-geodetic");
                img->name = "Earth Imagery";
                img->maxLevel = 3;
                mapNode->map->add(img);
                auto dem = rocky::TMSElevationLayer::create();
                dem->uri = std::string("/home/mte/EarthData/World.earth.tms_repo/worldHeight")
                         + "/{z}/{x}/{-y}.tif";
                dem->profile = rocky::Profile("global-geodetic");
                dem->name = "Earth Elevation";
                dem->maxLevel = 5;
                mapNode->map->add(dem);
                std::printf("[rocky] 内置 TMS 底图+高程已挂载\n");
            }
        }
    }

    // ---- 创建模块 ----
    // addWindow 在 MapNode 创建之后执行 (rocky_simple 顺序): MapNode::create
    // 过程中 VSGContextImpl 依赖 device, 需窗口先入 viewer 才能取到。
    viewer->addWindow(window);

    // ---- 锚点 ENU→ECEF ----
    // Rocky 单基座: 世界根 = ECEF (sky 的 SkyNode 直挂), 云/海挂锚点 ENU 内容根
    // (世界根下的 ENU 变换节点, 与 gui contentRoot 同构)。
    // 场景 geo_anchor 存在时星历锚点取场景值, 否则缺省 (GeoFrame GeoAnchor)。
    GeoAnchor anchor;

    // 场景数据源 (--scene JSON 驱动 / 程序化回退); SceneData 生命周期提升
    // (传感器实例 desc 指针引用其 sensor_list)。
    std::unique_ptr<JsonSceneDataSource> source;
    const SceneData* sceneData = nullptr;

    // ---- 场景 JSON 解析 (纯 CPU; 几何构建在环境栈创建之后 —— 批次3
    //      OceanProvider 需要 IOceanSampler 波面真值)。锚点先于环境栈确定。 ----
    if (sensorMode && !args.scene.empty()) {
        source = std::make_unique<JsonSceneDataSource>(args.scene);
        auto loaded = source->load();
        if (!loaded.ok)
            std::fprintf(stderr, "场景加载失败: %s → 回退程序化热场景\n", loaded.error.c_str());
        else
            sceneData = &source->data();
    }
    if (sceneData && sceneData->anchor.valid) {
        anchor.lat_deg = sceneData->anchor.lat_deg;
        anchor.lon_deg = sceneData->anchor.lon_deg;
        anchor.alt_m = sceneData->anchor.alt_m;
    }
    // --anchor "lat,lon[,alt]" CLI 覆盖 (最高优先级): 环境测试可把锚点搬到
    // 海域 —— ocean.frag 按逐顶点地形高度 discard 陆地, 陆地锚点下海洋板
    // 全部被丢弃 (掠海视角无海面)。
    if (!args.anchor.empty()) {
        double la, lo, al = 0.0;
        int n = std::sscanf(args.anchor.c_str(), "%lf,%lf,%lf", &la, &lo, &al);
        if (n >= 2) {
            anchor.lat_deg = la;
            anchor.lon_deg = lo;
            if (n >= 3) anchor.alt_m = al;
            std::fprintf(stderr, "[env] --anchor 覆盖: (%.4f, %.4f, %.1fm)\n", la, lo, al);
        } else {
            std::fprintf(stderr, "[env] --anchor 解析失败 '%s' (期望 lat,lon[,alt])\n",
                         args.anchor.c_str());
        }
    }
    const double anchorLat = anchor.lat_deg, anchorLon = anchor.lon_deg, anchorAlt = anchor.alt_m;
    const vsg::dmat4 enu_to_ecef = makeEnuToWorldMatrix(anchor);
    const vsg::dvec3 up_ecef = geoUp(anchor.lat_deg, anchor.lon_deg);

    // ---- Vulkan 帧回读截图资源 (两分支共用; 回读命令挂到对应 CommandGraph 尾部,
    //      renderpass 之后 → 捕获最终合成画面, 不依赖窗口映射状态)。 ----
    bool wantReadback = false;
    vsg::ref_ptr<vsg::Buffer> readbackBuffer;
    vsg::ref_ptr<SwapchainReadback> readback;
    if(readbackSupported) {
        const VkDeviceSize rbSize = VkDeviceSize(args.width) * args.height * 4;
        readbackBuffer = vsg::createBufferAndMemory(
            window->getOrCreateDevice(), rbSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        readback = SwapchainReadback::create();
        readback->window = window;
        readback->dstBuffer = readbackBuffer;
        readback->request = &wantReadback;
    }
    // 截图保存 (回读优先; 失败回退 X11 按窗口 ID 抓取)。
    auto saveShot = [&](const std::string& path) {
        wantReadback = false;
        if (readbackBuffer) {
            vkDeviceWaitIdle(*window->getOrCreateDevice());   // vsg::Device 隐式转 VkDevice
            if (writePPMFromReadback(readbackBuffer.get(), window->extent2D(), path)) {
                std::printf("[shot] %s (Vulkan 回读 %ux%u)\n", path.c_str(),
                            window->extent2D().width, window->extent2D().height);
                return;
            }
        }
#if ENV_VIEWER_HAVE_XCB
        if (owned_win && window->traits() && window->traits()->systemConnection.has_value()) {
            try {
                auto* conn = std::any_cast<xcb_connection_t*>(window->traits()->systemConnection);
                if (conn && !waitForWindowMapped(conn, static_cast<xcb_window_t>(owned_win), 5000))
                    std::printf("[shot] 警告: 等待窗口映射超时 (map_state 仍非 IsViewable)\n");
            } catch (const std::bad_any_cast&) {}
        }
#endif
#if ENV_VIEWER_HAVE_X11
        if (Display* dpy = XOpenDisplay(nullptr)) {
            float mean_lum = 0.0f, nonuniform = 0.0f;
            int cw = 0, ch = 0;
            if (captureWindowToPPM(dpy, Window(owned_win ? owned_win
                                                         : DefaultRootWindow(dpy)),
                                   path, mean_lum, nonuniform, cw, ch))
                std::printf("[shot] %s (X11 %dx%d)  mean_lum=%.1f  nonuniform=%.1f\n",
                            path.c_str(), cw, ch, mean_lum, nonuniform);
            else
                std::printf("[shot] 截图失败\n");
            XCloseDisplay(dpy);
        } else
#endif
        {
            float mean_lum = 0.0f, nonuniform = 0.0f;
            if (captureRootToPPM(path, mean_lum, nonuniform))
                std::printf("[shot] %s (根窗口回退, 可能含桌面)  平均亮度 %.1f  标准差 %.1f\n",
                            path.c_str(), mean_lum, nonuniform);
            else
                std::printf("[shot] 截图失败 (回读与 X11 均不可用)\n");
        }
    };

    // ---- B3: 环境栈 (sky/cloud/ocean 单一真值源) + EnvPhysicsService 契约 ----
    // 环境基座统一走 IEnvStack (合并项): 栈内保证 sky→cloud→ocean 真值传播
    // 顺序 (星历太阳覆写 + AtmosphereState 写回), 宿主不再手工同步。
    // 四模块默认全显 (地球/云/天空/海洋, 用户要求): 场景 JSON 显式关海才关。
    const bool oceanOn = !sceneData || sceneData->ocean.enabled ||
                         args.ocean || (args.mode == "ocean");
    EnvironmentStackParams stackParams;
    stackParams.enable_sky = true;
    stackParams.enable_cloud = true;
    stackParams.enable_ocean = oceanOn;
    if (oceanOn) {
        if (sensorMode && sceneData) {
            // 传感器模式: 海温/海平面取场景 JSON (RT 海面辐射真值)
            stackParams.ocean.sea_temp_k = sceneData->ocean.sea_temp_k;
            stackParams.ocean.sea_level = sceneData->ocean.sea_level;
            stackParams.ocean.fft_patch_size = sceneData->ocean.fft_patch_size;
        } else {
            // 环境模式: 原三模块查看器参数 (大覆盖 + 细网格 + 谱谐波)。
            // size 60km: 掠海仰视机位 (20m 高) 下海洋板须覆盖到地平线
            // (~16km 几何地平线之外留余量), 否则板外 rocky 地球面以黑/绿
            // 碎面露出 (板边弧线伪影)。
            stackParams.ocean.size = 60000.0f;
            stackParams.ocean.grid = 192;
            stackParams.ocean.wind_speed = args.wind;
            stackParams.ocean.auto_sea_state = true;
            stackParams.ocean.wave_height = args.hs;   // 0 = auto_sea_state 气象驱动
            stackParams.ocean.harmonics = 16;
            stackParams.ocean.reflection = 1;
            // P3: --wave-mode 覆盖波形模式 (0=legacy 1=谱谐波 2=GPU FFT)。
            // FFT 网格细分 grid 建议与位移纹理分辨率相当 (512 谱 → 网格 256)。
            if (args.wave_mode >= 0) {
                stackParams.ocean.wave_mode = args.wave_mode;
                if (args.wave_mode == 2) stackParams.ocean.grid = 256;
            }
        }
    }
    if(args.has_fft_patch)stackParams.ocean.fft_patch_size=args.fft_patch;
    // rocky 基座: sky 硬依赖 VSGContext (Rocky 单后端, ECEF 世界)
    stackParams.sky.rocky_context = rocky_singleton.get();
    stackParams.sky.hours_utc = args.hours_utc;
    stackParams.sky.time_scale = args.time_scale;
    if (sensorMode) {
        // 传感器模式: 单层云片 (CLI 全参数), 全局调制置 1 → totalCoverage
        // 严格等于层 coverage (否则缺省 0.5 再乘一层)
        CloudLayerParams cloudLayer;
        cloudLayer.type = static_cast<CloudLayerType>(std::clamp(args.cloud_type, 0, 4));
        cloudLayer.altitude = float(std::max(args.cloud_base, 50.0));
        cloudLayer.thickness = float(std::max(args.cloud_thick, 50.0));
        cloudLayer.coverage = float(std::clamp(args.cover, 0.0f, 1.0f));
        cloudLayer.density = float(std::clamp(args.cloud_density, 0.05, 1.0));
        stackParams.cloud.layers.push_back(cloudLayer);
        stackParams.cloud.cover = 1.0f;
    } else {
        // 环境模式: 原三模块查看器三层云 (卷云/层积云/积云)
        CloudLayerParams cirrus;
        cirrus.type = CloudLayerType::Cirrus;
        cirrus.altitude = 6500.0f; cirrus.thickness = 900.0f;
        cirrus.coverage = 0.35f;   cirrus.density = 0.25f;
        CloudLayerParams stratoc;
        stratoc.type = CloudLayerType::Stratocumulus;
        stratoc.altitude = 1800.0f; stratoc.thickness = 500.0f;
        stratoc.coverage = 0.5f;    stratoc.density = 0.5f;
        CloudLayerParams cumulus;
        cumulus.type = CloudLayerType::Cumulus;
        cumulus.altitude = 900.0f; cumulus.thickness = 700.0f;
        cumulus.coverage = 0.45f;  cumulus.density = 0.6f;
        stackParams.cloud.layers = {cirrus, stratoc, cumulus};
        stackParams.cloud.cover = args.cover;
    }
    // P3-4b: --cloud-volume 使积云/积雨云走 3D ray-march 体积渲染。
    stackParams.cloud.volume = args.cloud_volume;
    auto envStack = createEnvironmentStack(stackParams);
    if (!envStack) {
        std::fprintf(stderr, "错误: 环境栈创建失败\n");
        return 1;
    }
    // P3: 注入 Vulkan 窗口 (转发到各模块; wave_mode=2 的海洋据此同步 build
    // GPU FFT 波场 + set=1 采样渲染管线)。须在 viewer->compile() 之前,
    // 使 FFT 描述符/管线被编译。窗口已创建 (两分支共用)。
    envStack->setWindow(window.get());
    EnvPhysicsService envService;
    envService.bind(envStack.get());

    // =======================================================================
    // 传感器链分支 (原 sensor_viewer 全流程; 观察口 = rocky 地球双视口布局)
    // =======================================================================
    if (sensorMode) {
        const double cloudCover = std::clamp(args.cover, 0.0f, 1.0f);

        // ---- 场景数据源状态 (--scene JSON 驱动 / 程序化回退) ----
        vsg::ref_ptr<vsg::Node> scene;
        bool jsonScene = false;         // --scene 成功 → 场景自适应机位
        ThermalScene thermalScene;      // B2: 场景几何 + RT 雷达材质表 (生命周期贯穿)
        // A7 TV 彩色: map_Kd token → 绝对路径注册表 (meshLoader 逐网格登记)
        std::map<std::string, std::string> texAbsPaths;
        vsg::dvec3 sceneCenter(0.0, 1.5, -20.0);
        double sceneR = 60.0;
        double viewFar = 5000.0;
        vsg::dvec3 senEye(0.0, 2.0, 60.0), senCenter(0.0, 1.2, -40.0);

        // ---- B1: 场景构建前预热环境帧 (栈 onFrame → AtmosphereState → is_day
        //      真值), 贴图亮度升温幅度按昼夜取值 —— 单一真值源原则。 ----
        {
            EnvFrame fp;
            fp.model = enu_to_ecef;                      // 锚点 ENU→ECEF (星历真值)
            fp.cam_pos_world = enuXform(enu_to_ecef, senEye);
            fp.cam_pos_enu = senEye;
            fp.year = 2026; fp.month = 9; fp.day = 26;   // 满月 (月相≈1, 月光链路可验证)
            fp.hours_utc = args.hours_utc;
            fp.cloud_cover = float(cloudCover);
            fp.wind_speed = float(args.wind);
            fp.precipitation = float(args.rain);
            fp.visibility = float(args.vis);
            envStack->onFrame(fp);
            envService.update(0.0, args.hours_utc, -1.0, args.wind, args.rain);
        }
        // 昼: 暗面吸热升温上限 18K; 夜: 收敛 2K
        const float solarHeatK = (envService.isDay() > 0.5) ? 18.0f : 2.0f;
        std::printf("[thermal] 贴图亮度升温 solarHeatK=%.1fK (is_day=%.2f)\n",
                    solarHeatK, envService.isDay());

        // B1: 贴图亮度缓存 (map_kd → 平均亮度; 懒计算, 跨网格共享)
        std::map<std::string, float> texLumCache;
        auto textureLuminance = [&texAbsPaths, &texLumCache](const std::string& mapKd) -> float {
            auto it = texLumCache.find(mapKd);
            if (it != texLumCache.end()) return it->second;
            float lum = -1.0f;
            auto pit = texAbsPaths.find(mapKd);
            if (!mapKd.empty() && pit != texAbsPaths.end())
                lum = textureMeanLuminance(pit->second);
            texLumCache.emplace(mapKd, lum);
            return lum;
        };

        // ---- 场景几何构建 (--scene JSON 驱动) ----
        if (sceneData) {
            const SceneData& sd = *sceneData;
            // MeshLoader 注入: vsg 内建 (.vsgb 预编译) → vsgXchange (非 OBJ 格式)
            // → 自研 loadObjNode (本环境 vsgXchange 无 assimp, .obj 恒失败)
            // TV 彩色: loadObjNode 传 out_slots (材质桶表), 三角形汤同步抽 uv/槽号
            auto meshLoader = [&texAbsPaths](const std::string& p, MeshLoadResult& out) -> bool {
                auto node = vsg::read_cast<vsg::Node>(p);
                if (!node) {
                    auto options = vsg::Options::create();
                    options->readerWriters.push_back(vsgXchange::all::create());
                    node = vsg::read_cast<vsg::Node>(p, options);
                }
                if (!node) {
                    auto options = vsg::Options::create();
                    std::vector<vsgraysim::ObjMaterialSlot> slots;
                    node = vsgraysim::loadObjNode(p, options, &slots);
                    if (node) {
                        // A7: map_Kd token → 绝对路径登记 (相对 mtl 目录; 首现生效)
                        const std::string dir =
                            p.substr(0, p.find_last_of("/\\") + 1);
                        for (const auto& s : slots) {
                            out.slots.push_back({s.map_kd, s.diffuse});
                            if (!s.map_kd.empty() && !texAbsPaths.count(s.map_kd)) {
                                const std::string abs = dir + s.map_kd;
                                if (std::filesystem::exists(abs))
                                    texAbsPaths[s.map_kd] = abs;
                            }
                        }
                    }
                }
                if (!node) return false;
                return flattenMeshToTriangles(node, out.vertices, &out.uvs, &out.tri_slots);
            };
            // 地形注入 (TerrainProvider): terrain_ref 为 .vsgterrain →
            // TerrainSystem 高度场三角形汤进 RT/光栅; 未命中 → SceneSource 地面兜底链。
            const std::string terrainAbs = resolveSceneRef(args.scene, sd.terrain_ref);
            auto terrainProvider = [terrainAbs](TerrainMeshData& out) -> bool {
                if (terrainAbs.find(".vsgterrain") == std::string::npos) return false;
                if (!loadTerrainProject(terrainAbs, out)) {
                    std::fprintf(stderr, "[terrain] .vsgterrain 加载失败 → 地面平面兜底\n");
                    return false;
                }
                std::printf("[terrain] .vsgterrain 注入: %s (%zu 三角, 温度回退 ground 缺省)\n",
                            terrainAbs.c_str(), out.vertices.size() / 3);
                return true;
            };
            // 批次3: 海面注入 (OceanProvider 契约) — IOceanSampler 波面真值 →
            // RT 三角形汤。波面相位冻结在 t=0 (场景级 BLAS 一次构建)。
            auto oceanProvider = [&](OceanMeshData& out) -> bool {
                if (!oceanOn || !envStack) return false;
                auto* sampler = envStack->query<rockyenv::IOceanSampler>();
                if (!sampler) return false;
                const float seaTemp = sd.ocean.sea_temp_k;
                const float seaLevel = sd.ocean.sea_level;
                // extent 缺省: 随场景对象分布自适应 (与 SceneSource 地面公式一致 ×2)
                double maxR = 0.0;
                for (const auto& o : sd.objects)
                    maxR = std::max(maxR, std::sqrt(o.position_enu.x * o.position_enu.x +
                                                    o.position_enu.z * o.position_enu.z));
                const float extent = (sd.ocean.extent > 0.0f)
                                         ? sd.ocean.extent
                                         : float(std::clamp(maxR * 1.5 + 100.0, 500.0, 20000.0)) * 2.0f;
                constexpr int kN = 128;                     // 128×128 → 32k 三角
                const double cell = double(extent) / kN;
                const double x0 = -double(extent) * 0.5, z0 = -double(extent) * 0.5;
                constexpr double t0 = 0.0;
                std::vector<float> hgt(size_t(kN + 1) * (kN + 1));
                std::vector<float> foam(size_t(kN + 1) * (kN + 1));
                for (int j = 0; j <= kN; ++j) {
                    for (int i = 0; i <= kN; ++i) {
                        const size_t k = size_t(j) * (kN + 1) + i;
                        hgt[k] = sampler->waveHeight(x0 + i * cell, z0 + j * cell, t0);
                        foam[k] = sampler->foamCoverage(x0 + i * cell, z0 + j * cell, t0);
                    }
                }
                out.vertices.reserve(size_t(kN) * kN * 6);
                out.vertex_temp_k.reserve(out.vertices.capacity());
                out.vertex_emissivity.reserve(out.vertices.capacity());
                out.face_flags.reserve(size_t(kN) * kN);
                out.sea_temp_k = seaTemp;
                // 水面材质: 低反照率 + 强镜面; Debye 介电模型 (载频物理式)
                out.water_material.dielectric_real = 80.0f;
                out.water_material.dielectric_imag = 20.0f;
                out.water_material.dielectric_model = 1;
                out.water_material.sigma0_db = -25.0f;
                out.water_material.surface_rough = 0.4f;
                out.water_material.emissivity = 0.98f;
                out.water_material.rho_tv = 0.02f;
                out.water_material.rho_swir = 0.02f;
                out.water_material.eps_mwir = 0.99f;
                out.water_material.eps_lwir = 0.98f;
                out.water_material.spec_strength = 1.0f;
                out.water_material.spec_roughness = 0.18f;
                // 白沫材质: 高反照率弱镜面 (发射率>0 触发独立材质槽)
                out.foam_material.emissivity = 0.96f;
                out.foam_material.rho_tv = 0.22f;
                out.foam_material.rho_swir = 0.22f;
                out.foam_material.spec_strength = 0.1f;
                out.foam_material.spec_roughness = 0.8f;
                auto emitVert = [&](size_t k, float x, float z) {
                    out.vertices.emplace_back(x, seaLevel + hgt[k], z);
                    out.vertex_temp_k.push_back(seaTemp);
                    out.vertex_emissivity.push_back(foam[k] > 0.3f ? 0.96f : 0.98f);
                };
                size_t foamTris = 0;
                for (int j = 0; j < kN; ++j) {
                    for (int i = 0; i < kN; ++i) {
                        const size_t k00 = size_t(j) * (kN + 1) + i;
                        const size_t k10 = k00 + 1;
                        const size_t k01 = k00 + (kN + 1);
                        const size_t k11 = k01 + 1;
                        const float xf0 = float(x0 + i * cell), xf1 = float(x0 + (i + 1) * cell);
                        const float zf0 = float(z0 + j * cell), zf1 = float(z0 + (j + 1) * cell);
                        emitVert(k00, xf0, zf0);
                        emitVert(k01, xf0, zf1);
                        emitVert(k11, xf1, zf1);
                        emitVert(k00, xf0, zf0);
                        emitVert(k11, xf1, zf1);
                        emitVert(k10, xf1, zf0);
                        const bool foamy = (foam[k00] + foam[k10] + foam[k01] + foam[k11]) * 0.25f > 0.3f;
                        out.face_flags.push_back(foamy ? 1 : 0);
                        if (foamy) ++foamTris;
                    }
                }
                std::printf("[ocean] 波面注入: %zu 三角 (extent %.0fm, 海温 %.1fK, "
                            "白沫面 %zu, 相位冻结 t0)\n",
                            out.vertices.size() / 3, extent, seaTemp, foamTris);
                return !out.vertices.empty();
            };
            // P4 工况: CLI > 场景 JSON 顶层 thermal_state > base (空 = 不乘)
            const std::string thermalState = !args.thermal_state.empty()
                ? args.thermal_state : sd.default_thermal_state;
            if (!thermalState.empty())
                std::printf("[thermal] 工况 '%s' 生效 (来源 %s)\n", thermalState.c_str(),
                            args.thermal_state.empty() ? "场景 JSON" : "CLI");
            auto thermal = buildThermalSceneGraph(sd, meshLoader, terrainProvider,
                                                  thermalState, oceanProvider,
                                                  textureLuminance, solarHeatK);
            if (!thermal.node || !thermal.has_bounds) {
                std::fprintf(stderr, "场景几何构建为空 → 回退程序化热场景\n");
            } else {
                thermalScene = std::move(thermal);
                scene = thermalScene.node;
                jsonScene = true;
                sceneCenter = (thermalScene.bound_min + thermalScene.bound_max) * 0.5;
                sceneR = std::max(vsg::length(thermalScene.bound_max - thermalScene.bound_min) * 0.5, 30.0);
                viewFar = std::max(5000.0, sceneR * 6.0);

                // 传感器瞄准首个非地形对象 (场景驱动的主目标)
                if (const SceneObjectDesc* tgt = sd.primaryTarget(sd.terrain_ref)) {
                    senCenter = tgt->position_enu + vsg::dvec3(0.0, 1.5, 0.0);
                    senEye = senCenter + vsg::dvec3(30.0, 4.0, 40.0);
                }

                std::printf("场景 JSON 驱动: %s — %zu 对象 (mesh %u, geo %u), %u 三角\n",
                            sd.scene_name.c_str(), sd.objects.size(), sd.mesh_count,
                            sd.geo_count, thermalScene.triangle_count);
                std::printf("  ENU 包围盒 (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)  地面材质 %.1fK ε%.2f\n",
                            thermalScene.bound_min.x, thermalScene.bound_min.y, thermalScene.bound_min.z,
                            thermalScene.bound_max.x, thermalScene.bound_max.y, thermalScene.bound_max.z,
                            sd.ground.temp_k, sd.ground.emissivity);
                if (oceanOn)
                    std::printf("  海面: 启用 (海温 %.1fK, 海平面 %.1fm)\n",
                                sd.ocean.sea_temp_k, sd.ocean.sea_level);
            }
        }

        // ---- 传感器实例注册表 (B1: 场景驱动多实例 + CLI 单实例回退) ----
        // A7/C2: 机位 CLI 覆盖 (最高优先级; 在 cliDesc 构建前应用一次, 实例选择后
        // 幂等重应用以压过场景驱动机位)
        {
            auto parseXYZ = [](const std::string& s, vsg::dvec3& v) {
                double x, y, z;
                if (!s.empty() && std::sscanf(s.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) {
                    v = vsg::dvec3(x, y, z);
                    return true;
                }
                return false;
            };
            parseXYZ(args.sen_eye, senEye);
            parseXYZ(args.sen_center, senCenter);
        }
        SensorDesc cliDesc;                 // 生命周期: 实例 desc 指针引用
        std::vector<SensorInstance> registry;
        const bool sceneDriven = (sceneData && !sceneData->sensors.empty());
        if (sceneDriven) {
            registry = SensorRegistry::createFromScene(*sceneData);
        } else {
            // 场景无 sensor_list / 程序化场景: CLI 参数构造单实例 (兼容原行为)
            cliDesc.sensor_id = 1;
            cliDesc.name = "sensor_" + args.band;
            cliDesc.type = (args.band == "tv") ? "vis" : "ir";
            cliDesc.band = args.band;
            for (auto& c : cliDesc.band)
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            cliDesc.frame_kind = (args.band == "tv") ? "VIS" : cliDesc.band;
            cliDesc.fov_deg = args.sen_fov;
            cliDesc.netd_mk = args.netd_mk;
            cliDesc.integration_ms = args.tint_ms;
            cliDesc.aperture_offset = senEye;
            cliDesc.direction = senCenter - senEye;   // createFromScene 内单位化
            SensorInstance i0;
            i0.desc = &cliDesc;
            i0.profile = SensorRegistry::profileFromDesc(cliDesc);
            i0.position_enu = cliDesc.aperture_offset;
            i0.renderable = true;
            registry.push_back(i0);
        }

        const SensorInstance* sel = SensorRegistry::select(registry, args.sensor);
        if (!sel) {
            std::fprintf(stderr, "未找到传感器实例: '%s'\n", args.sensor.c_str());
            return 2;
        }
        // B6: RADAR 选中 → 回波链模式; 窗口渲染回退首个成像实例 (观察口保留成像画面)。
        const bool radarMode = !sel->renderable;
        const SensorInstance* gfxSel = sel;
        if (radarMode) {
            if (!atomicFloatOK) {
                std::fprintf(stderr, "[radar] 设备不支持 VK_EXT_shader_atomic_float → 回波链不可用\n");
                return 2;
            }
            gfxSel = nullptr;
            for (const auto& i : registry)
                if (i.renderable) { gfxSel = &i; break; }
            if (!gfxSel) {
                std::fprintf(stderr, "场景无成像类传感器实例 (观察口无渲染对象)\n");
                return 2;
            }
            std::printf("[radar] 回波链模式: #%d %s; 观察口回退 #%d %s\n",
                        sel->desc->sensor_id, sel->desc->name.c_str(),
                        gfxSel->desc->sensor_id, gfxSel->desc->name.c_str());
        }

        // B6 CLI 覆盖 (Param > 场景 JSON): 直接改选中实例底层 desc, 保证调度器节拍/
        // 回波链参数/发布元数据三者一致; CPI 归一到 2 的幂 (慢时间 FFT) 且 ≤256。
        {
            SensorDesc& md = *const_cast<SensorDesc*>(sel->desc);   // 测试程序: CLI 覆盖落底层
            if (radarMode) {
                if (args.rf_hz > 0.0) md.wavelength = kC / args.rf_hz;
                if (md.wavelength <= 0.0) md.wavelength = kC / 10e9;
                uint32_t cpi = (args.cpi > 0) ? uint32_t(args.cpi) : md.cpi_pulses;
                uint32_t p2 = 1;
                while (p2 * 2 <= cpi && p2 * 2 <= 256) p2 *= 2;
                md.cpi_pulses = p2;
            }
        }
        printRegistry(registry);
        std::printf("[registry] 选中: #%d %s (%s)\n", sel->desc->sensor_id,
                    sel->desc->name.c_str(), sel->desc->frame_kind.c_str());

        // ---- B4: 每实例帧调度器 + 物理真值环形缓冲 (统一 3D 时序) ----
        std::vector<SensorScheduler> scheds(registry.size());
        std::vector<StateRingBuffer> states(registry.size(), StateRingBuffer(1024, 0.0));
        std::printf("[sched] 每实例帧调度器 (B4 统一 3D 时序):\n");
        for (size_t i = 0; i < registry.size(); ++i) {
            const SensorDesc& d = *registry[i].desc;
            if (!scheds[i].configure(d)) {
                std::printf("  #%d %-12s 节拍无法推导 (rate=0 且积分无效)\n",
                            d.sensor_id, d.name.c_str());
                continue;
            }
            if (scheds[i].coherent()) {
                const double prf = (d.pulse_rate > 0.0) ? d.pulse_rate : d.rate_hz;
                std::printf("  #%d %-12s 相干  PRF=%.0fHz × CPI=%u 脉冲 → 帧率 %.2fHz (period %.2fms)\n",
                            d.sensor_id, d.name.c_str(), prf, d.cpi_pulses,
                            1.0 / scheds[i].period(), scheds[i].period() * 1000.0);
            } else {
                std::printf("  #%d %-12s 成像  rate=%.2fHz (period %.2fms)\n",
                            d.sensor_id, d.name.c_str(), 1.0 / scheds[i].period(),
                            scheds[i].period() * 1000.0);
            }
        }
        // StateRingBuffer 自检: 合成匀速运动验证插值与端点外推。
        {
            StateRingBuffer rb(16);
            for (int k = 0; k <= 10; ++k)
                rb.push({k * 0.1, vsg::dvec3(k * 0.1, 0.0, 0.0), vsg::dvec3(1.0, 0.0, 0.0)});
            StateSample s;
            rb.sample(0.55, s);   // 区间插值: 0.5/0.6 相邻样本中点
            const bool ok1 = std::abs(s.pos.x - 0.55) < 1e-9;
            const double x1 = s.pos.x;
            rb.sample(1.2, s);    // 显式允许的 1s 期限内匀速外推
            const bool ok2 = std::abs(s.pos.x - 1.2) < 1e-9;
            const double x2 = s.pos.x;
            std::printf("[state] 环形缓冲自检: 插值 t=0.55→x=%.4f (%s)  外推 t=1.2→x=%.4f (%s)\n",
                        x1, ok1 ? "OK" : "FAIL", x2, ok2 ? "OK" : "FAIL");
        }
        gimbalSelfCheck();   // B7: 扫描机构纯函数自检 (8 模式/相干性/slew)

        // 场景驱动实例: 观察口机位 = 渲染实例 (成像/回退) 安装位姿 + 初始 LOS
        if (sceneDriven) {
            senEye = gfxSel->position_enu;
            senCenter = senEye + gfxSel->los_dir * 300.0;
        }
        // A7/C2: 机位 CLI 覆盖 (最高优先级; 验证/构图用)
        {
            auto parseXYZ = [](const std::string& s, vsg::dvec3& v) {
                double x, y, z;
                if (!s.empty() && std::sscanf(s.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) {
                    v = vsg::dvec3(x, y, z);
                    return true;
                }
                return false;
            };
            if (parseXYZ(args.sen_eye, senEye))
                std::printf("[camera] senEye CLI 覆盖: (%.1f, %.1f, %.1f)\n",
                            senEye.x, senEye.y, senEye.z);
            if (parseXYZ(args.sen_center, senCenter))
                std::printf("[camera] senCenter CLI 覆盖: (%.1f, %.1f, %.1f)\n",
                            senCenter.x, senCenter.y, senCenter.z);
        }
        // B6 雷达相机 (回波链视角): 雷达实例安装位 + 视线 (正式指向由 B7 扫描机构接管)
        vsg::dvec3 radarEye, radarCenter;
        if (radarMode) {
            radarEye = sel->position_enu;
            const SceneObjectDesc* tgt = sceneData ? sceneData->primaryTarget(sceneData->terrain_ref)
                                                   : nullptr;
            if (tgt)
                radarCenter = tgt->position_enu + vsg::dvec3(0.0, 1.5, 0.0);
            else
                radarCenter = radarEye + sel->los_dir * 300.0;
        }

        // ---- 传感器参数: 实例 profile + CLI 显式覆盖 (Param > 场景 JSON > 默认) ----
        // B6: profile 服务于窗口渲染 (GPU 成像链) → 取渲染实例; 雷达回波链参数
        // 独立走 RadarChainParams (sel desc)。
        // 方案A: 先解析基础 profile (分辨率/FOV/NETD/积分; 波段独立), 再按 band 列表
        // 逐套派生 (profile.band = make(bt) → LUT)。dual_band=false 时退化为单套。
        SensorProfile baseProfile = gfxSel->profile;
        const BandType baseBandType = baseProfile.band.type;
        // --sen-res "WxH": 传感器分辨率覆盖 (覆盖 G-buffer/P0/级联全链分辨率)
        if (args.sen_res_w > 0 && args.sen_res_h > 0) {
            baseProfile.width = args.sen_res_w;
            baseProfile.height = args.sen_res_h;
            std::printf("[sen-res] 分辨率覆盖: %dx%d\n", baseProfile.width, baseProfile.height);
        }
        if (args.has_sen_fov) baseProfile.fov_deg = args.sen_fov;
        if (args.has_netd) baseProfile.detector.netd_mk = args.netd_mk;
        if (args.has_tint) baseProfile.detector.integration_ms = args.tint_ms;

        // 确定要构建的 band 列表 (dual_band → TV+MWIR; 否则按 CLI/场景单套)
        std::vector<BandType> bandList;
        if (args.dual_band) {
            bandList = {BandType::TV, BandType::MWIR};
        } else {
            BandType bt = baseBandType;
            if (args.has_band) {
                if (args.band == "tv")          bt = BandType::TV;
                else if (args.band == "swir")   bt = BandType::SWIR;
                else if (args.band == "lwir")   bt = BandType::LWIR;
                else if (args.band == "mwir")   bt = BandType::MWIR;
                else {
                    std::fprintf(stderr, "未知波段: %s (mwir|lwir|swir|tv)\n", args.band.c_str());
                    return 2;
                }
            }
            bandList = {bt};
        }

        // D 方案: bands 容器仅含当前激活的单个 BandResources (bands[0]),
        // activeIdx 恒为 0 (兼容旧索引语义)。切 band 时 deviceWaitIdle → 释放重建。
        std::vector<std::unique_ptr<BandResources>> bands;
        int activeIdx = 0;   // 恒 0 (bands[0] 即激活), ImGui 回调切 band 时重建
        auto buildActiveBand = [&](BandType bt) -> bool {
            auto br = std::make_unique<BandResources>();
            br->type = bt;
            br->profile = baseProfile;
            br->profile.band = BandSpec::make(bt);
            // Exposure is a configured sensor property, not a hidden per-band
            // display adjustment. Keep scheduler, detector and metadata equal.
            if (!br->lut.build(br->profile.band)) {
                std::fprintf(stderr, "LUT 构建失败 (%s)\n", br->profile.band.name());
                return false;
            }
            bands.clear();
            bands.push_back(std::move(br));
            return true;
        };
        if (!buildActiveBand(bandList[0])) return 1;

        // ---- 环境契约 (envStack/envService 已于场景构建前创建) ----
        // 方案A: 参数化 BandSpec, 支持 TV/MWIR 双 band 独立生成 EnvRequest
        auto makeEnvRequestFor = [&](const BandSpec& band, double fovDeg, double simTime) {
            EnvRequest req;
            req.band = band;
            req.fov_h_deg = fovDeg;
            const vsg::dvec3 los = vsg::normalize(senCenter - senEye);
            req.los_az_deg = std::atan2(double(los.x), double(los.z)) * 180.0 / M_PI;
            req.los_el_deg = std::asin(std::clamp(double(los.y), -1.0, 1.0)) * 180.0 / M_PI;
            req.lat = anchorLat;
            req.lon = anchorLon;
            req.alt = anchorAlt;
            req.sim_time = simTime;
            // 0-24h 昼夜循环 (与帧循环 ef.hours_utc 同一公式)
            req.hours_utc = (args.day_minutes > 0.0)
                ? std::fmod(args.hours_utc + simTime * 24.0 / (args.day_minutes * 60.0), 24.0)
                : args.hours_utc;
            return req;
        };
        // 单 band 兼容路径: makeEnvRequest 绑定当前激活 band
        auto makeEnvRequest = [&](double simTime) {
            const auto& bp = bands[size_t(activeIdx)]->profile;
            return makeEnvRequestFor(bp.band, bp.fov_deg, simTime);
        };
        // 批次2: 谱天空图烘焙 (CPU bandSky 纯晴空真值, 256×128 latlong)
        // 方案A: 参数化 BandType + 输出 vector, 支持 TV/MWIR 双 band 独立烘焙
        constexpr uint32_t kSkyW = 256, kSkyH = 128;
        auto bakeSkyMap = [&](BandType bandType, const BandSpec& bandSpec,
                              std::vector<float>& out) {
            // #1: TV 波段烘焙 RGB (λ 三等分子带 bandSky 真值), 其它波段为标量 R32F
            const bool isTvCh = bandType == BandType::TV;
            const uint32_t ch = isTvCh ? 4u : 1u;
            out.assign(size_t(kSkyW) * kSkyH * ch, 0.0f);
            auto* aq = envStack->query<rockyenv::IAtmosphereQuery>();
            if (!aq) return;
            auto* cq = envStack->query<rockyenv::ICloudQuery>();
            float cloudBaseM = 0.0f;
            const bool hasCloud = cq && cq->cloudBase(cloudBaseM);
            const double sigRain = envService.rainExtinction(bandSpec);   // Np/km
            const double lamLo = bandSpec.lambda_min;
            const double d3 = (bandSpec.lambda_max - lamLo) / 3.0;
            // #1 谱形补全: 分子 Rayleigh λ^-4 + 气溶胶中性底座 κ, 行内归一
            double wSky[3] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
            double kappa = 0.0, tauA0 = 0.0;
            if (isTvCh) {
                const double visM = envService.visibilityM();
                kappa = 8.6 * aerosolSigma(visM, 0.55) / aerosolSigma(10000.0, 0.55);
                tauA0 = aerosolSigma(visM, 0.55) * 1.2;
                if (std::getenv("SKYMAP_DEBUG"))
                    std::printf("[skymap] vis=%.0fm kappa=%.2f tauA0=%.3f\n",
                                visM, kappa, tauA0);
            }
            for (uint32_t r = 0; r < kSkyH; ++r) {
                const double el = (double(r) + 0.5) / double(kSkyH) * 90.0;
                if (isTvCh) {
                    const double sinElG = std::max(std::sin(el * M_PI / 180.0), 1.0 / 38.0);
                    const double amGeo = 1.0 / sinElG;
                    double raw[3];
                    for (int i = 0; i < 3; ++i) {
                        const double lamUm = (lamLo + (double(i) + 0.5) * d3) * 1e6;
                        const double tauRay = 0.098 * std::pow(0.55 / lamUm, 4.0);
                        const double gR = (1.0 - std::exp(-amGeo * tauRay))
                                        / (1.0 - std::exp(-tauRay));
                        const double gA = (1.0 - std::exp(-amGeo * tauA0))
                                        / (1.0 - std::exp(-tauA0));
                        raw[i] = std::pow(lamUm, -4.0) * gR + kappa * gA;
                    }
                    const double s3 = raw[0] + raw[1] + raw[2];
                    for (int i = 0; i < 3; ++i) wSky[i] = raw[i] / s3;
                }
                double lC[3] = {0.0, 0.0, 0.0}, sB = 0.0, eC[3] = {0.0, 0.0, 0.0};
                bool ok;
                if (isTvCh) {
                    ok = aq->bandSky(lamLo, lamLo + d3, el, lC[0], sB, eC[0])
                      && aq->bandSky(lamLo + d3, lamLo + 2.0 * d3, el, lC[1], sB, eC[1])
                      && aq->bandSky(lamLo + 2.0 * d3, lamLo + 3.0 * d3, el, lC[2], sB, eC[2]);
                } else {
                    ok = aq->bandSky(lamLo, lamLo + 3.0 * d3, el, lC[0], sB, eC[0]);
                }
                if (!ok) continue;
                double veil[3];
                for (int i = 0; i < 3; ++i) veil[i] = 0.5 * lC[i];
                const double sinEl = std::max(std::sin(el * M_PI / 180.0), 0.02);
                const double dSlantKm = std::min(double(cloudBaseM) / sinEl / 1000.0, 30.0);
                const double tauR = std::exp(-sigRain * dSlantKm);
                if (std::getenv("SKYMAP_DEBUG") && r % 16 == 0)
                    std::printf("[skymap] el=%5.1f° l=%8.3f/%8.3f/%8.3f s=%9.3f "
                                "tauR=%.3f sigRain=%.3f\n",
                                el, lC[0], lC[1], lC[2], sB, tauR, sigRain);
                for (uint32_t c = 0; c < kSkyW; ++c) {
                    float* px = out.data() + (size_t(r) * kSkyW + c) * ch;
                    for (int i = 0; i < 3; ++i) {
                        double li = lC[i];
                        if (isTvCh) li *= 3.0 * wSky[i];
                        px[i] = float(li * tauR + veil[i] * (1.0 - tauR));
                    }
                    if (isTvCh) px[3] = px[0] + px[1] + px[2];
                }
            }
            if (const char* dp = std::getenv("SKYMAP_DUMP")) {
                const bool isTvDump = bandType == BandType::TV;
                const uint32_t dch = isTvDump ? 4u : 1u;
                float mx = 0.0f;
                for (uint32_t i = 0; i < kSkyW * kSkyH; ++i)
                    mx = std::max(mx, out[i * dch + (dch - 1)]);
                FILE* df = std::fopen(dp, "wb");
                if (df) {
                    std::fprintf(df, "P6\n%u %u\n255\n", kSkyW, kSkyH);
                    for (uint32_t i = 0; i < kSkyW * kSkyH; ++i) {
                        const uint8_t g = uint8_t(255.0f * out[i * dch + (dch - 1)] / std::max(mx, 1e-6f));
                        const uint8_t rgb[3] = {g, g, g};
                        std::fwrite(rgb, 1, 3, df);
                    }
                    std::fclose(df);
                    std::printf("[skymap] dump %s  max=%.3f\n", dp, mx);
                }
            }
        };

        // 初始帧: 栈 onFrame 产出 AtmosphereState → 服务缓存 → 波段应答
        EnvFrame f0;
        f0.model = enu_to_ecef;
        f0.cam_pos_world = enuXform(enu_to_ecef, senEye);
        f0.cam_pos_enu = senEye;
        f0.year = 2026; f0.month = 9; f0.day = 26;
        f0.hours_utc = args.hours_utc;
        f0.cloud_cover = float(cloudCover);
        f0.wind_speed = float(args.wind);
        f0.precipitation = float(args.rain);
        f0.visibility = float(args.vis);
        envStack->onFrame(f0);
        envService.update(0.0, args.hours_utc, -1.0, args.wind, args.rain);
        // D 方案: 仅激活 band 生成初始环境应答
        bands[0]->env = envService.answer(makeEnvRequestFor(bands[0]->profile.band,
                                                            bands[0]->profile.fov_deg, 0.0));
        const EnvAnswer& env = bands[0]->env;

        std::printf("波段: %s (%.2f-%.2f um)  NETD %.0f mK  积分 %.1f ms  FOV %.1f deg\n",
                    bands[0]->profile.band.name(), bands[0]->profile.band.lambda_min * 1e6,
                    bands[0]->profile.band.lambda_max * 1e6, bands[0]->profile.detector.netd_mk,
                    bands[0]->profile.detector.integration_ms, bands[0]->profile.fov_deg);
        std::printf("[env] UTC %.1fh 云量 %.2f 风 %.1fm/s 雨 %.2f 锚点(%.3f,%.3f) → σ=%.3f/km  "
                    "天空=%.4f  直射=%.1f  vis=%.0fm  昼=%.2f  天气=%d\n",
                    args.hours_utc, envService.cloudCoverage(), args.wind, args.rain,
                    anchorLat, anchorLon,
                    env.sigma_per_km, env.sky_radiance, env.sun_irradiance,
                    env.visibility_m, env.is_day, env.weather_type);
        if (oceanOn)
            std::printf("[env] 海洋真值: 海温 %.1fK  白沫 %.3f  蒲福 %d (IOceanSampler 同源)\n",
                        env.sea_surface_temp_k, env.foam_fraction, env.beaufort);

        // ---- GPU 级联 ----
        if (!scene) scene = createThermalScene();
        // 方案A: SensorPipeline 不可移动/拷贝, 各 band 实例存于 bands[i]->pipe,
        // 构建完成后统一 commandGraph + SwitchGroup 接入。
        const std::vector<RadarMaterial>* rtMaterials =
            jsonScene ? &thermalScene.materials : nullptr;

        // ---- P0 场景级资源池: 几何/BLAS/TLAS/LUT/sampler/env UBO 全链一份 ----
        auto sharedScene = std::make_unique<SharedSceneResources>();
        {
            // A7 TV 彩色: 贴图槽表 → TexArraySpec (统一 kTexW×kTexH; 失败槽 Kd 占位)
            SharedSceneResources::TexArraySpec texSpec;
            bool haveTexSpec = jsonScene && thermalScene.tex_slots.size() > 1;
            uint32_t okCnt = 0, phCnt = 0;
            if (haveTexSpec) {
                texSpec.slotData.push_back(vsg::vec4(0.5f, 0.5f, 0.5f, 0.0f));  // 槽0 无贴图
                for (size_t s = 1; s < thermalScene.tex_slots.size(); ++s) {
                    const auto& slot = thermalScene.tex_slots[s];
                    vsg::ref_ptr<vsg::Data> layer;
                    bool ok = false;
                    auto it = texAbsPaths.find(slot.map_kd);
                    if (!slot.map_kd.empty() && it != texAbsPaths.end()) {
                        layer = loadTextureLayer(it->second);
                        ok = layer != nullptr;
                    }
                    if (!layer) layer = makeKdLayer(slot.kd);
                    else ++okCnt;
                    texSpec.layers.push_back(layer);
                    texSpec.slotData.push_back(vsg::vec4(slot.kd.r, slot.kd.g, slot.kd.b,
                                                         ok ? 1.0f : 0.0f));
                }
                phCnt = uint32_t(thermalScene.tex_slots.size() - 1) - okCnt;
                std::printf("[SharedScene] TV 贴图槽表: %zu 槽 (加载 %u, Kd 占位 %u)\n",
                            thermalScene.tex_slots.size() - 1, okCnt, phCnt);
            }
            auto ps = sharedScene->build(*window, scene, rtMaterials,
                                         haveTexSpec ? &texSpec : nullptr);
            if (!ps.ok) {
                std::fprintf(stderr, "[SharedScene] 池构建失败 (%s) → 各链单实例抽取回退\n",
                             ps.error.c_str());
            }
        }

        // ---- P1.1 雷达模式分辨率对齐 (成像级联消费雷达视角 G-buffer) ----
        if (radarMode) {
            for (auto& br : bands) {
                br->profile.width = sel->desc->res_w;
                br->profile.height = sel->desc->res_h;
            }
        }

        // ---- P1.1 G-buffer 池: 全场景唯一 TraceRays (band_shade/radar_scatter 双消费) ----
        auto gbufferPool = std::make_unique<GBufferPool>();
        {
            auto gb = gbufferPool->build(*window, scene, rtMaterials, sharedScene.get(),
                                         bands[0]->profile.width, bands[0]->profile.height);
            if (!gb.ok)
                std::fprintf(stderr, "[GBuffer] 池构建失败 (%s) → 下游链各自回退\n",
                             gb.error.c_str());
        }

        // D 方案 (单 Pipeline 重建切换): 共享资源 (sharedScene/gbufferPool) 仅建一次,
        // 切 band 时释放并重建单个 SensorPipeline。切换函数由 GUI 调用 (需 deviceWaitIdle
        // 后重建并再 compile)。bands[0] 即当前激活; 切换时 bandList[bandIdx] 为目标。
        auto buildPipeline = [&]() -> bool {
            auto& br = bands[0];
            auto built = br->pipe.build(window, br->profile, br->lut, br->env, scene,
                                        rtMaterials, !args.no_rt,
                                        sharedScene.get(), gbufferPool.get());
            if (!built.ok) {
                std::fprintf(stderr, "SensorPipeline 构建失败 (%s): %s\n",
                             br->profile.band.name(), built.error.c_str());
                return false;
            }
            br->ok = true;
            br->cloudEmitReady = br->cloudEmitLut.build(br->profile.band);
            if (br->cloudEmitReady)
                bakeSkyMap(br->type, br->profile.band, br->skyBake);
            std::printf("GPU 传感器链构建成功 [%s] (传感器 %ux%u, 级联 rad→optics→detector→electronics)\n",
                        br->profile.band.name(), br->pipe.sensorWidth(), br->pipe.sensorHeight());
            if (br->pipe.rtModule())
                std::printf("[rt] %s P0 渲染路径: G-buffer 唯一求交 + band_shade compute 着色\n",
                            br->profile.band.name());
            else
                std::printf("[rt] %s P0 渲染路径: 光栅化回退 (无遮挡/无阴影, 画家序; --no-rt 或驱动无 RT)\n",
                            br->profile.band.name());
            return true;
        };
        if (!buildPipeline()) return 1;

        // D 方案: bandsSG 为 bands 的挂载点 (ref_ptr 占位, commandGraph 组装段赋值;
        // switchBand 闭包捕获引用, 编译期已声明即可, 运行期使用时已赋值)。
        vsg::ref_ptr<vsg::Group> bandsSG;
        // GUI 切换入口 (D 方案): 用户点 ImGui 按钮 → deviceWaitIdle → 释放重建 → 再 compile。
        auto switchBand = [&](int bandIdx) {
            if (bandIdx < 0 || bandIdx >= int(bandList.size())) return;
            if (bands[0]->type == bandList[size_t(bandIdx)]) return;   // 同 band, 无操作
            std::printf("[switch] 重建传感器链: %s → %s\n",
                        bands[0]->profile.band.name(), BandSpec::make(bandList[size_t(bandIdx)]).name());
            viewer->deviceWaitIdle();
            bandsSG->children.clear();
            if (!buildActiveBand(bandList[size_t(bandIdx)]) || !buildPipeline()) {
                std::fprintf(stderr, "[switch] 重建失败, 退出\n");
                std::exit(1);
            }
            bandsSG->addChild(bands[0]->pipe.commandGraph);
            auto cr = viewer->compile();
            if (!cr)
                std::fprintf(stderr, "[switch] 重编译失败 (result=%d): %s\n", cr.result, cr.message.c_str());
            std::printf("[switch] 切换完成: 当前 = %s (位置姿态不变)\n", bands[0]->profile.band.name());
        };

        // ---- B6 雷达回波链 (radarMode): 散射体 RT + 回波合成 compute ----
        vsg::ref_ptr<vsg::Perspective> radarPersp;
        vsg::ref_ptr<vsg::LookAt> radarLookAt;
        std::unique_ptr<RadarChain> radarChain;
        vsg::dvec3 platVelV(0.0, 0.0, 0.0);
        // B7 扫描机构: 雷达 (CPI 中心摆动) 与成像 (t_exp 单指向) 各一 Gimbal
        GimbalModel gimbalRadar, gimbalIm;
        const double radarBaseDist = radarMode ? vsg::length(radarCenter - radarEye) : 0.0;
        const vsg::dvec3 radarBaseDir = radarMode ? vsg::normalize(radarCenter - radarEye)
                                                  : vsg::dvec3(0.0, 0.0, 1.0);
        const vsg::dvec3 imBaseDir = vsg::normalize(senCenter - senEye);
        const double imBaseDist = vsg::length(senCenter - senEye);
        if (radarMode) {
            {
                double vx, vy, vz;
                if (std::sscanf(args.plat_vel.c_str(), "%lf,%lf,%lf", &vx, &vy, &vz) == 3)
                    platVelV = vsg::dvec3(vx, vy, vz);
            }
            const SensorDesc& rd = *sel->desc;   // CLI 覆盖已同步回 desc (λ/CPI)
            gimbalRadar.configure(rd.scan, rd.beam_width);
            RadarChainParams rcp;
            rcp.width = rd.res_w;
            rcp.height = rd.res_h;
            rcp.lambda = rd.wavelength;
            rcp.transmit_power_w = rd.power;
            rcp.prf = (rd.pulse_rate > 0.0) ? rd.pulse_rate : rd.rate_hz;
            rcp.cpi_pulses = rd.cpi_pulses;
            rcp.pulse_width_s = 1e-6;
            rcp.bandwidth_hz = (args.bw_hz > 0.0) ? args.bw_hz : 30e6;
            rcp.range_min_m = 0.0;
            rcp.range_max_m = (rd.range_m > 0.0) ? rd.range_m : 8000.0;
            rcp.fov_h_deg = (rd.fov_deg > 0.0) ? rd.fov_deg : ((rd.fov_h > 0.0) ? rd.fov_h : 60.0);
            rcp.fov_v_deg = (rd.fov_v > 0.0) ? rd.fov_v : rcp.fov_h_deg * 0.5;
            // n_fast 由带宽推导 (快时间复采样率 = 带宽); P2.2 扩容至 4096
            {
                const double tFast = 2.0 * (rcp.range_max_m - rcp.range_min_m) / kC;
                uint32_t nRaw = uint32_t(std::ceil(tFast * rcp.bandwidth_hz));
                uint32_t nf = 512;
                while (nf < nRaw) nf <<= 1;   // FFT radix-2
                if (nf > 512 && nf <= 4096) {
                    std::printf("[radar] [P2.2] n_fast=%u (>512) 启用行变换分段扩容, "
                                "距离门 %.0fm 全量保留 (分辨率 c/2B=%.1fm 不变)\n",
                                nf, rcp.range_max_m - rcp.range_min_m,
                                kC / (2.0 * rcp.bandwidth_hz));
                } else if (nf > 4096) {
                    const double gate = 4096.0 * kC / (2.0 * rcp.bandwidth_hz);
                    std::printf("[radar] [告警] n_fast=%u 超出 GPU 扩容上限 4096 → "
                                "距离门 %.0fm 钳制至 %.0fm (距离分辨率 c/2B=%.1fm 不变)\n",
                                nf, rcp.range_max_m - rcp.range_min_m, gate,
                                kC / (2.0 * rcp.bandwidth_hz));
                    rcp.range_max_m = rcp.range_min_m + gate;
                    nf = 4096;
                }
                rcp.n_fast = nf;
            }
            // B7 相干性约束: CPI 内波束扫过角度 ≤ beam_width, 超限自动缩短 CPI
            const uint32_t rawCpi = rcp.cpi_pulses;
            const uint32_t clampedCpi = gimbalRadar.clampCpiPulses(rawCpi, rcp.prf);
            if (clampedCpi < 2) {
                std::fprintf(stderr, "[gimbal] scan rate cannot support a coherent CPI of at least 2 pulses\n");
                return 2;
            }
            if (clampedCpi != rawCpi) {
                rcp.cpi_pulses = clampedCpi;
                const_cast<SensorDesc&>(rd).cpi_pulses = clampedCpi;
                scheds[size_t(sel - registry.data())].configure(rd);
                std::printf("[gimbal] [相干性] scan=%.1f°/s × T_cpi=%.1fms > 波束 %.2f° → "
                            "CPI 自动缩短 %u→%u\n",
                            std::abs(rd.scan.az_rate),
                            1000.0 * double(rawCpi) / rcp.prf, rd.beam_width * 180.0 / kPi,
                            rawCpi, clampedCpi);
            }

            radarChain = std::make_unique<RadarChain>();
            auto rb = radarChain->build(*window, gbufferPool.get(), rcp, rtMaterials,
                                        sharedScene.get());
            if (!rb.ok) {
                std::fprintf(stderr, "雷达回波链构建失败: %s\n", rb.error.c_str());
                return 1;
            }

            radarPersp = vsg::Perspective::create(
                rcp.fov_v_deg, double(rcp.width) / double(rcp.height), 0.5,
                std::max(2000.0, sceneR * 4.0));
            radarLookAt = vsg::LookAt::create(radarEye, radarCenter, vsg::dvec3(0.0, 1.0, 0.0));
            std::printf("[radar] 回波链: λ=%.3gcm (f_c=%.3gGHz) PRF=%.0fHz CPI=%u (帧率 %.2fHz) "
                        "BW=%.1fMHz 距离门 0..%.0fm 平台速度 (%.1f,%.1f,%.1f)m/s\n",
                        rcp.lambda * 100.0, kC / rcp.lambda / 1e9, rcp.prf, rcp.cpi_pulses,
                        rcp.prf / double(rcp.cpi_pulses), rcp.bandwidth_hz / 1e6, rcp.range_max_m,
                        platVelV.x, platVelV.y, platVelV.z);
        }
        // B7: 成像实例扫描配置 (观察口渲染实例; t_exp 单指向消费)
        gimbalIm.configure(gfxSel->desc->scan, gfxSel->desc->beam_width);
        if (radarMode || gimbalRadar.spec().mode != "fixed" || gimbalIm.spec().mode != "fixed") {
            std::printf("[gimbal] 扫描配置: 雷达=%s(%.1f°/s)  成像=%s(%.1f°/s)  状态机=SEARCH\n",
                        gimbalRadar.spec().mode.c_str(), gimbalRadar.spec().az_rate,
                        gimbalIm.spec().mode.c_str(), gimbalIm.spec().az_rate);
        }
        // B7: 本帧波束指向 (绝对 ENU az/el, °) —— 帧头元数据
        double beamAzDeg = 0.0, beamElDeg = 0.0;

        // ---- B5 分发时序: 设备注册 + 订阅 + 分发器 (ProductFrame→传输帧) ----
        const uint32_t selId = uint32_t(sel->desc->sensor_id);
        const ptrdiff_t selIdx = sel - registry.data();
        SensorDesc pubDesc = *sel->desc;
        if (radarMode) {
            pubDesc.cpi_pulses = radarChain->params().cpi_pulses;
        } else {
            pubDesc.lambda_min = bands[size_t(activeIdx)]->profile.band.lambda_min;
            pubDesc.lambda_max = bands[size_t(activeIdx)]->profile.band.lambda_max;
            pubDesc.frame_kind = (pubDesc.frame_kind == "RADAR") ? pubDesc.frame_kind
                                                                 : bands[size_t(activeIdx)]->profile.band.name();
            pubDesc.fov_deg = bands[size_t(activeIdx)]->profile.fov_deg;
            pubDesc.netd_mk = bands[size_t(activeIdx)]->profile.detector.netd_mk;
            pubDesc.integration_ms = bands[size_t(activeIdx)]->profile.detector.integration_ms;
            if (!scheds[size_t(selIdx)].configure(pubDesc)) {
                std::fprintf(stderr, "[sched] invalid effective imaging exposure/rate\n");
                return 2;
            }
        }

        struct PubState {
            bool enabled = false;
            std::unique_ptr<product_dispatch::ProductDispatcher> disp;
            bool power_off_sent = false, unlink_sent = false;
            bool power_off_verified = false, unlink_verified = false;
            double power_off_at = 0.0, unlink_at = 0.0;
            uint64_t published_at_off = 0;
            int last_stats_frame = -60;
            bool first_publish_logged = false;
        } pub;

        if (args.publish) {
            vsgraysim::SimDataBroker& broker = vsgraysim::SimDataBroker::instance();
            broker.serverOnline(true);
            // 场景驱动设备注册 (equipment_id = sensor_id; 阵营/平台名随场景)
            for (const auto& inst : registry) {
                vsgraysim::EquipmentInfo eq;
                eq.equipment_id = uint32_t(inst.desc->sensor_id);
                eq.name = inst.desc->name;
                const std::string& k = inst.desc->frame_kind;
                eq.type = (k == "VIS") ? vsgraysim::SensorType::VIS
                        : (k == "RADAR") ? (inst.desc->type == "sar" ? vsgraysim::SensorType::SAR
                                                                     : vsgraysim::SensorType::RADAR)
                                         : vsgraysim::SensorType::IR;
                eq.platform_name = inst.desc->platform;
                eq.range_max = float(inst.desc->range_m);
                eq.role = (inst.desc->role == "guidance") ? 1 : 0;
                eq.faction = (inst.desc->faction == "red") ? 2
                           : (inst.desc->faction == "blue") ? 1 : 0;
                eq.platform_kind = inst.desc->type;
                vsgraysim::EquipmentRegistry::instance().registerEquipment(eq);
            }
            if (!broker.subscribe(selId)) {
                std::fprintf(stderr, "[dispatch] 订阅装备 %u 失败\n", selId);
                return 1;
            }
            pub.enabled = true;
            pub.disp = std::make_unique<product_dispatch::ProductDispatcher>();
            pub.disp->configure(pubDesc);
            if (!args.stages_csv.empty())
                pub.disp->setStagesFromCsv(args.stages_csv);   // CLI > 场景 stages > 缺省
            if (radarMode) {
                for (auto stage : pub.disp->stages()) {
                    if (stage == ProductStage::Raw ||
                        (pub.disp->kind() == product_dispatch::SinkKind::Sar && stage == ProductStage::Image)) {
                        std::fprintf(stderr, "[dispatch] SAR/ISAR focusing is not implemented; request echo/range_comp/rd_map\n");
                        return 2;
                    }
                }
            }
            pub.disp->onIr   = [&](const vsgraysim::IrFrame& f)   { return vsgraysim::SimDataBroker::instance().publishIr(f); };
            pub.disp->onVis  = [&](const vsgraysim::VisFrame& f)  { return vsgraysim::SimDataBroker::instance().publishVis(f); };
            pub.disp->onRadar= [&](const vsgraysim::RadarFrame& f){ return vsgraysim::SimDataBroker::instance().publishRadar(f); };
            pub.disp->onSar  = [&](const vsgraysim::SarFrame& f)  { return vsgraysim::SimDataBroker::instance().publishSar(f); };
            std::printf("[dispatch] B5 分发时序启用: 装备 #%u %s → %s  发布级:",
                        selId, pubDesc.name.c_str(),
                        (pub.disp->kind() == product_dispatch::SinkKind::Ir ? "IrFrame"
                        : pub.disp->kind() == product_dispatch::SinkKind::Vis ? "VisFrame"
                        : pub.disp->kind() == product_dispatch::SinkKind::Sar ? "SarFrame"
                                                                              : "RadarFrame"));
            for (auto s : pub.disp->stages())
                std::printf(" P%u(%s)", unsigned(s), productStageName(s));
            std::printf("\n");
        }

        // 传感器相机纵横比/垂直 FOV (publishStep 帧头元数据与 senPersp 共用)
        const double senAspect = double(bands[size_t(activeIdx)]->profile.width) /
                                 double(bands[size_t(activeIdx)]->profile.height);
        const double senVfov = 2.0 * std::atan(std::tan(
            bands[size_t(activeIdx)]->profile.fov_deg * M_PI / 180.0 / 2.0) / senAspect) * 180.0 / M_PI;

        // ---- B5: 单帧发布步 (present 后调用) ----
        auto publishStep = [&](double simTime, int frameNo,
                               const std::vector<SchedulerEvent>& due) {
            if (!pub.enabled) return;
            vsgraysim::SimDataBroker& broker = vsgraysim::SimDataBroker::instance();

            // 1) eq/control: 取走显控控制命令 (供电门控)
            vsgraysim::ControlCommand cmd;
            while (broker.pollControl(&cmd)) {
                if (cmd.equipment_id == selId) {
                    const bool on = (cmd.type == vsgraysim::ControlCommand::Type::PowerOn);
                    pub.disp->setPowered(on);
                    std::printf("[dispatch] 控制命令 seq=%u: %s → 传感器 #%u %s\n",
                                unsigned(cmd.seq), on ? "PowerOn" : "PowerOff", selId,
                                on ? "恢复帧生成" : "停帧");
                }
            }
            // 2) 自测触发器 (模拟显控端操作)
            if (args.power_off_after >= 0 && !pub.power_off_sent && simTime >= args.power_off_after) {
                vsgraysim::ControlCommand off;
                off.type = vsgraysim::ControlCommand::Type::PowerOff;
                off.equipment_id = selId;
                off.seq = 1;
                broker.sendControl(off);
                pub.power_off_sent = true;
                pub.power_off_at = simTime;
                pub.published_at_off = broker.channelStats(selId).published_frames;
                std::printf("[dispatch] 自测: t=%.2fs 发送 PowerOff (equipment %u)\n", simTime, selId);
            }
            if (args.unlink_after >= 0 && !pub.unlink_sent && simTime >= args.unlink_after) {
                broker.unsubscribe(selId);
                pub.unlink_sent = true;
                pub.unlink_at = simTime;
                std::printf("[dispatch] 自测: t=%.2fs 取消订阅 (equipment %u)\n", simTime, selId);
            }

            // 3) 链接门控刷新 (显控订阅状态每帧回读)
            pub.disp->setLinked(broker.isSubscribed(selId));

            // 4) 调度事件 → ProductFrame → 门控分发 (雷达模式走 iq 路径)
            // Host publication requires actual pixels. GPU-only delivery needs
            // a resource handle and synchronization contract, not a flag.
            if (!radarMode) {
                for (const auto& ev : due) {
                for (auto stg : pub.disp->stages()) {
                    auto& apipe = bands[size_t(activeIdx)]->pipe;
                    const auto& aprof = bands[size_t(activeIdx)]->profile;
                    if (!apipe.hasStage(stg)) continue;
                    ProductFrame pf;
                    pf.type = sensorFrameTypeFromBand(aprof.band.type);
                    pf.stage = stg;
                    pf.frame_id = ev.frame_id;
                    pf.sim_time = ev.t;
                    pf.timestamp_ns = uint64_t(simcore::fromSeconds(ev.t));
                    pf.sensor_id = selId;
                    pf.width = aprof.width;
                    pf.height = aprof.height;
                    pf.integration_s = aprof.detector.integration_ms * 1e-3;
                    pf.fov_h_deg = aprof.fov_deg;
                    pf.fov_v_deg = senVfov;
                    pf.beam_az_deg = beamAzDeg;
                    pf.beam_el_deg = beamElDeg;
                    std::vector<float> rgba;
                    if (!apipe.readbackStage(*window, stg, rgba)) continue;
                    const size_t npx = size_t(pf.width) * pf.height;
                    pf.pixels.resize(npx);
                    for (size_t p = 0; p < npx; ++p) pf.pixels[p] = rgba[p * 4];
                    if (pub.disp->dispatch(pf) && !pub.first_publish_logged) {
                        pub.first_publish_logged = true;
                        std::printf("[dispatch] 首帧发布 P%u(%s): frame_id=%u sim_time=%.4f "
                                    "%ux%u %s → SimDataBroker\n",
                                    unsigned(stg), productStageName(stg),
                                    unsigned(ev.frame_id), ev.t, pf.width, pf.height,
                                    "CPU载荷回读");
                    }
                }
                }
            }

            // 5) 周期统计 + 显控侧取数验证
            if (frameNo - pub.last_stats_frame >= 60) {
                pub.last_stats_frame = frameNo;
                const auto& st = pub.disp->stats();
                const auto cs = broker.channelStats(selId);
                std::printf("[dispatch] 帧%d 发布=%llu 丢弃(链接=%llu 供电=%llu 级=%llu) "
                            "通道: published=%llu powered=%d\n",
                            frameNo, (unsigned long long)st.published,
                            (unsigned long long)st.dropped_unlinked,
                            (unsigned long long)st.dropped_unpowered,
                            (unsigned long long)st.dropped_stage,
                            (unsigned long long)cs.published_frames, int(cs.powered));
                vsgraysim::IrFrame ir;
                if (pub.disp->kind() == product_dispatch::SinkKind::Ir &&
                    broker.latestIr(selId, &ir) && ir.frame_id > 0) {
                    std::printf("[dispatch] 显控取数 latestIr: frame_id=%llu sim_time=%.4f "
                                "%ux%u stage=%u λ=%.1f-%.1fμm (订阅到 P%d)\n",
                                (unsigned long long)ir.frame_id, ir.sim_time,
                                ir.width, ir.height, unsigned(ir.stage),
                                ir.wavelength_min, ir.wavelength_max, int(ir.stage));
                }
                vsgraysim::RadarFrame rf;
                if (pub.disp->kind() == product_dispatch::SinkKind::Radar &&
                    broker.latestRadar(selId, &rf) && rf.frame_id > 0) {
                    std::printf("[dispatch] 显控取数 latestRadar: frame_id=%llu sim_time=%.4f "
                                "%ux%u (bin) stage=%u f_c=%.3gGHz BW=%.1fMHz CPI=%u (订阅到 P%d)\n",
                                (unsigned long long)rf.frame_id, rf.sim_time,
                                rf.range_bins, rf.doppler_bins, unsigned(rf.stage),
                                rf.carrier_hz / 1e9, rf.bandwidth / 1e6, rf.coherent_pulses,
                                int(rf.stage));
                }
            }

            // 6) 自测验证: 断电停帧 / 链接门控
            const auto& st = pub.disp->stats();
            if (pub.power_off_sent && !pub.power_off_verified && simTime >= pub.power_off_at + 0.5) {
                const auto cs = broker.channelStats(selId);
                const bool frozen = cs.published_frames == pub.published_at_off && st.dropped_unpowered > 0;
                pub.power_off_verified = true;
                std::printf("[dispatch] 验收·断电停帧: %s (通道发布 %llu→%llu 冻结, "
                            "供电门控丢弃=%llu, 通道 powered=%d)\n",
                            frozen ? "OK" : "FAIL",
                            (unsigned long long)pub.published_at_off,
                            (unsigned long long)cs.published_frames,
                            (unsigned long long)st.dropped_unpowered, int(cs.powered));
            }
            if (pub.unlink_sent && !pub.unlink_verified && simTime >= pub.unlink_at + 0.5) {
                pub.unlink_verified = true;
                std::printf("[dispatch] 验收·链接门控: %s (未订阅丢弃=%llu, linked=%d)\n",
                            st.dropped_unlinked > 0 ? "OK" : "FAIL",
                            (unsigned long long)st.dropped_unlinked, int(pub.disp->linked()));
            }
        };

        // ---- B6: CPI 处理步 (present 后; 段处理全显存, 默认零回读) ----
        bool rd_dumped = false;
        bool zero_rb_logged = false;
        auto radarProcessCpi = [&](const SchedulerEvent& ev) {
            // B6 诊断: 首个 CPI 读散射体统计
            if (ev.frame_id == 0) {
                const auto ss = radarChain->scatterStats(*window);
                std::printf("[radar] 散射体统计: %u/%u 命中  σmax=%.4g σmean=%.4g  首命中 ENU(%.1f,%.1f,%.1f)\n",
                            ss.hits, ss.total, ss.sigma_max, ss.sigma_mean,
                            ss.pos_first.x, ss.pos_first.y, ss.pos_first.z);
            }
            const RadarChainParams& rcp = radarChain->params();
            RadarEchoMeta meta;
            meta.lambda = rcp.lambda;
            meta.prf = rcp.prf;
            meta.pulse_width_s = rcp.pulse_width_s;
            meta.bandwidth_hz = rcp.bandwidth_hz;
            meta.range_min_m = rcp.range_min_m;
            meta.range_max_m = rcp.range_max_m;
            meta.cpi_pulses = rcp.cpi_pulses;
            meta.n_fast = rcp.n_fast;
            meta.sample_rate_hz = rcp.sample_rate_hz;

            std::vector<uint32_t> want{uint32_t(radar::Stage::RdMap), uint32_t(radar::Stage::Product)};
            if (pub.enabled) {
                want.clear();
                for (auto s : pub.disp->stages()) want.push_back(uint32_t(s));
            }
            auto mkRadarFrame = [&](ProductStage stage) {
                ProductFrame pf;
                pf.type = SensorFrameType::RADAR;
                pf.frame_id = ev.frame_id;
                pf.sim_time = ev.t;
                pf.timestamp_ns = uint64_t(simcore::fromSeconds(ev.t));
                pf.sensor_id = selId;
                pf.width = rcp.n_fast;
                pf.height = rcp.cpi_pulses;
                pf.range_min_m = rcp.range_min_m;
                pf.range_max_m = rcp.range_max_m;
                pf.pulse_width_s = rcp.pulse_width_s;
                pf.bandwidth_hz = rcp.bandwidth_hz;
                pf.sample_rate_hz = rcp.sample_rate_hz;
                pf.integration_s = double(rcp.cpi_pulses) / rcp.prf;
                pf.stage = stage;
                pf.beam_az_deg = beamAzDeg;
                pf.beam_el_deg = beamElDeg;
                return pf;
            };
            auto inWant = [&want](uint32_t s) {
                return std::find(want.begin(), want.end(), s) != want.end();
            };

            RadarMetrics gpuM;
            bool haveGpuM = false;
            const bool wantGpuMetrics = args.metrics || args.radar_cpu_stages || pub.enabled;
            const bool wantDump = !args.dump_rd.empty() && !rd_dumped;
            if (wantGpuMetrics || wantDump) {
                std::vector<std::complex<float>> rd;
                if (radarChain->readbackRd(*window, rd)) {
                    ProductFrame grd = mkRadarFrame(ProductStage::Focal);   // RdMap=2
                    grd.iq = std::move(rd);
                    gpuM = rdMetrics(grd);
                    haveGpuM = true;
                    std::printf("[radar] CPI#%u t=%.4f [GPU] 峰值(r-bin=%u,f-bin=%u) 占用bin=%u "
                                "多普勒质心=%.2f  Pmax=%.3g Pmean=%.3g\n",
                                unsigned(ev.frame_id), ev.t, gpuM.peak_range_bin,
                                gpuM.peak_doppler_bin, gpuM.occupied_bins,
                                gpuM.doppler_centroid_bins, gpuM.max_power, gpuM.mean_power);
                    {
                        double bucketMs = -1.0;
                        const double echoMs = radarChain->echoKernelMs(window->getOrCreateDevice(), &bucketMs);
                        if (echoMs >= 0.0) {
                            std::printf("[radar]   echo kernel GPU 耗时 %.3f ms (P2 时间戳, 分桶四 pass %.3f ms)\n",
                                        echoMs, bucketMs);
                        }
                    }
                    if (pub.enabled) {
                        if (inWant(uint32_t(radar::Stage::Echo))) {
                            std::vector<std::complex<float>> echo;
                            if (radarChain->readbackEcho(*window, echo)) {
                                ProductFrame out = mkRadarFrame(ProductStage::Scene);
                                out.iq = std::move(echo);
                                pub.disp->dispatch(out);
                            }
                        }
                        if (inWant(uint32_t(radar::Stage::RangeComp))) {
                            std::vector<std::complex<float>> range;
                            if (radarChain->readbackRange(*window, range)) {
                                ProductFrame out = mkRadarFrame(ProductStage::Pupil);
                                out.iq = std::move(range);
                                pub.disp->dispatch(out);
                            }
                        }
                        if (inWant(uint32_t(radar::Stage::RdMap))) pub.disp->dispatch(grd);
                        if (inWant(uint32_t(radar::Stage::Product))) {
                            std::vector<float> prod;
                            if (radarChain->readbackProduct(*window, prod)) {
                                ProductFrame out = mkRadarFrame(ProductStage::Image);
                                out.pixels = std::move(prod);
                                pub.disp->dispatch(out);
                            }
                        }
                    }
                }
                if (wantDump) {
                    rd_dumped = true;
                    std::vector<float> prod;
                    bool ok = false;
                    if (radarChain->readbackProduct(*window, prod)) {
                        ProductFrame pf = mkRadarFrame(ProductStage::Image);
                        pf.pixels = std::move(prod);
                        ok = writeMagnitudePPM(pf, args.dump_rd.c_str());
                    } else if (haveGpuM) {
                        ProductFrame pf = mkRadarFrame(ProductStage::Focal);
                        ok = writeMagnitudePPM(pf, args.dump_rd.c_str());
                    }
                    std::printf("[radar] RD 图输出: %s (%s)\n", args.dump_rd.c_str(),
                                ok ? "OK" : "失败");
                }
            }
            if (args.radar_cpu_stages) {
                // ---- CPU 段交叉对比: 回波读回 → CPU RadarStages → 同一指标 ----
                std::vector<std::complex<float>> echo;
                if (radarChain->readbackEcho(*window, echo)) {
                    const auto frames = runRadarChain(echo, meta, ev.frame_id, ev.t, selId, want);
                    for (const auto& f : frames) {
                        if (f.stage != ProductStage::Focal) continue;
                        const auto m = rdMetrics(f);
                        std::printf("[radar] CPI#%u t=%.4f [CPU] 峰值(r-bin=%u,f-bin=%u) 占用bin=%u "
                                    "多普勒质心=%.2f  Pmax=%.3g Pmean=%.3g\n",
                                    unsigned(ev.frame_id), ev.t, m.peak_range_bin,
                                    m.peak_doppler_bin, m.occupied_bins,
                                    m.doppler_centroid_bins, m.max_power, m.mean_power);
                        if (haveGpuM) {
                            const bool same = m.peak_range_bin == gpuM.peak_range_bin &&
                                              m.peak_doppler_bin == gpuM.peak_doppler_bin;
                            std::printf("[radar] CPI#%u GPU/CPU 交叉对比: 峰值bin %s "
                                        "(GPU r=%u,f=%u vs CPU r=%u,f=%u)\n",
                                        unsigned(ev.frame_id), same ? "一致" : "偏差",
                                        gpuM.peak_range_bin, gpuM.peak_doppler_bin,
                                        m.peak_range_bin, m.peak_doppler_bin);
                        }
                    }
                }
            }
            if (!wantGpuMetrics && !wantDump) {
                if (!zero_rb_logged) {
                    zero_rb_logged = true;
                    std::printf("[radar] GPU-local processing; no transport without --publish "
                                "(--metrics reads statistics, --radar-cpu-stages compares CPU)\n");
                }
            }
        };

        // ---- 观察口场景几何预览管线 (合并新增): 热场景 binding0 进左观察口 ----
        ScenePreview preview = createScenePreview(obsExtent);

        // ---- 相机 ----
        // 观察口机位 = 传感器安装位姿视角 (原 sensor_viewer 行为): Rig 预设为
        // ENU 机位, applyPreset 经 enu_to_ecef 变换到 ECEF 世界 (rocky 基座)。
        Rig rig;
        rig.enu_to_ecef = enu_to_ecef;
        rig.up_ecef = up_ecef;
        if (jsonScene) {
            // 场景自适应机位 (仅 --scene 成功时覆盖): 全景 / 主目标近观 / 传感器视线
            rig.presets[0] = {sceneCenter + vsg::dvec3(0.9 * sceneR, 0.5 * sceneR, 1.3 * sceneR),
                              sceneCenter};
            rig.presets[1] = {senCenter + vsg::dvec3(12.0, 4.0, 14.0), senCenter};
            rig.presets[2] = {sceneCenter + vsg::dvec3(0.0, 0.12 * sceneR, 1.8 * sceneR),
                              sceneCenter};
        }
        if(!args.obs_eye.empty())rig.presets[0].eye=observerEye;
        if(!args.obs_center.empty())rig.presets[0].center=observerCenter;
        const auto observerDirection=rig.presets[0].center-rig.presets[0].eye;
        if(vsg::length(observerDirection)<1e-6 || vsg::length(vsg::cross(vsg::normalize(observerDirection),vsg::dvec3(0,1,0)))<1e-6) {
            std::fprintf(stderr,"Invalid observer pose: eye and center must differ, and direction must not be parallel to ENU up\n");return 2;
        }
        rig.lookAt = vsg::LookAt::create(vsg::dvec3(0.0, 0.0, 0.0),
                                         vsg::dvec3(0.0, 0.0, -1.0), up_ecef);
        // far 1e7: 保地球视野 (观察口是 rocky ECEF 地球基座, 瓦片延伸至地平)
        rig.persp = vsg::Perspective::create(55.0, double(obsW) / double(args.height),
                                             0.5, 1e7);
        rig.applyPreset(0);

        // 传感器相机 (ENU 局部系; 传感器管线世界 = ENU): 瞄准主目标, 水平 FOV → 垂直 FOV
        auto senLookAt = vsg::LookAt::create(senEye, senCenter, vsg::dvec3(0.0, 1.0, 0.0));
        const double senFar = std::max(2000.0, sceneR * 4.0);
        auto senPersp = vsg::Perspective::create(senVfov, senAspect, 0.5, senFar);

        // ---- 场景根 (合并): rocky 地球 (ECEF) + 环境栈 + 锚点 ENU 内容根 ----
        auto root = vsg::Group::create();
        if (mapNode) root->addChild(mapNode);
        if (auto* wr = envStack->worldRoot()) root->addChild(vsg::ref_ptr<vsg::Node>(wr));
        auto content_root = vsg::MatrixTransform::create();
        content_root->matrix = enu_to_ecef;
        if (auto* cr = envStack->contentRoot()) content_root->addChild(vsg::ref_ptr<vsg::Node>(cr));
        // 观察口场景几何预览: 热场景节点同一份挂入预览管线 (管线只消费 binding0
        // 位置; mvp 每帧 CPU 组装 proj*view*ENU→ECEF, 不依赖图遍历模型矩阵)
        if (preview.sg && scene) preview.sg->addChild(scene);
        if (preview.sg) content_root->addChild(preview.sg);
        root->addChild(content_root);

        // ---- 场景结构自检 (排查"清屏色但无几何"类问题) ----
        {
            struct Counter : public vsg::Visitor {
                int groups = 0, state_groups = 0, draws = 0, switches = 0, transforms = 0;
                void apply(vsg::Group& g) override { ++groups; g.traverse(*this); }
                void apply(vsg::StateGroup& g) override { ++state_groups; g.traverse(*this); }
                void apply(vsg::Switch& s) override { ++switches; s.traverse(*this); }
                void apply(vsg::MatrixTransform& t) override { ++transforms; t.traverse(*this); }
                void apply(vsg::VertexIndexDraw& d) override {
                    ++draws;
                    std::printf("     [draw] indexCount=%u instanceCount=%u arrays=%zu\n",
                                d.indexCount, d.instanceCount, d.arrays.size());
                }
                void apply(vsg::Node&) override {}
            } counter;
            std::printf("[scene] 遍历场景结构:\n");
            root->accept(counter);
            std::printf("[scene] Group=%d StateGroup=%d Switch=%d MatrixTransform=%d VertexIndexDraw=%d\n",
                        counter.groups, counter.state_groups, counter.switches,
                        counter.transforms, counter.draws);
            if (counter.draws == 0)
                std::printf("[scene] 警告: 场景中没有任何绘制节点, 画面必然只有清屏色!\n");
        }

        // ---- 左观察口 view (rocky 地球 + env + 场景几何; 半宽视口) ----
        auto obsCamera = vsg::Camera::create(rig.persp, rig.lookAt,
                                             vsg::ViewportState::create(obsExtent));
        auto obsView = vsg::View::create(obsCamera);
        obsView->addChild(root);

        // rocky ViewDependentStateEx 修复 (合并保留项): 裸 VDS 缺 renderParamsBuf
        // 会导致地形全黑 —— 观察 view 必须挂 Ex 版并注册进 rocky 共享渲染数据。
        if (rocky_ctx) {
            auto vds = ROCKY_NAMESPACE::ViewDependentStateEx::create(obsView, window->getOrCreateDevice());
            obsView->viewDependentState = vds;
            rocky_ctx->sharedRenderData->viewDependentState[obsView->viewID] = vds;
            obsView->viewDependentState->maxShadowDistance = 15000.0;
        }

        // ---- 观察口 RenderGraph: 追加在 SensorPipeline 的 rgDisp 之后覆盖左半 ----
        // rgDisp 全窗口合成 (左=vis 右=elec); rgObs 以半宽 renderArea + 反向 Z 清屏
        // (深度清 0.0) 覆盖左半 → 最终布局: 左=rocky 观察口, 右=传感器合成画面。
        auto rgObs = vsg::RenderGraph::create(window, obsView);
        rgObs->renderArea.offset = VkOffset2D{0, 0};
        rgObs->renderArea.extent = obsExtent;
        rgObs->setClearValues(VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}},
                              VkClearDepthStencilValue{0.0f, 0});
        // P3: 海洋 GPU FFT compute 命令 (wave_mode=2): 须在任一 RenderGraph
        // 之前执行 (渲染 pass 内不允许 layout transition), 每帧演化位移/法线
        // 泡沫纹理供 ocean_fft.vert 顶点采样。插到根 commandGraph children 最前。
        auto rootCG = vsg::CommandGraph::create(window);
        if (auto* oc = envStack->module("ocean"))
            if (auto* fftCmd = oc->fftCommands())
                rootCG->addChild(vsg::ref_ptr<vsg::Command>(fftCmd));
        rootCG->addChild(rgObs);
        // 多 view 管线索引泄漏修复: RenderGraph 内遍历 View 会把 CommandBuffer::viewID
        // 置为该 view 的 ID (观察口 obsView=1) 并残留在 CommandBuffer 上; 下一帧传感器
        // 侧离屏通道没有 View 复位, 而 BindGraphicsPipeline::record 无边界检查地用
        // commandBuffer->viewID 索引 _implementation (传感器管线只编译了 viewID 0 槽位)
        // → 越界读出无效 VkPipeline → 驱动崩溃。rgObs 之后立即复位 viewID=0。
        struct ResetViewID : vsg::Inherit<vsg::Command, ResetViewID>
        {
            void record(vsg::CommandBuffer& cb) const override { cb.viewID = 0; }
        };
        rootCG->addChild(ResetViewID::create());

        // D 方案 (单 Pipeline 重建切换): 共享资源 (sharedScene/gbufferPool) 仅建一次,
        // 切 band 时释放并重建单个 SensorPipeline (LUT/天空图/着色管线按新 band 重建),
        // 相机/环境/观察口保持不变。GUI 切换 = deviceWaitIdle → 释放 → 重建 → 再 compile。
        bandsSG = vsg::Group::create();
        rootCG->addChild(bandsSG);
        if (!bands.empty())
            bandsSG->addChild(bands[size_t(activeIdx)]->pipe.commandGraph);
        // 帧回读截图命令 (末尾 → 捕获"左观察口+右传感器"最终合成)
        if(readback)rootCG->addChild(readback);

        viewer->assignRecordAndSubmitTaskAndPresentation({rootCG});

        // ---- P1.2 异步 compute queue: 雷达链独立 RecordAndSubmitTask + 队列间
        // 二值 semaphore 环形依赖 (深度 2)。task 须在 compile() 前入队。 ----
        vsg::ref_ptr<vsg::RecordAndSubmitTask> taskGfx, taskRadar;
        vsg::ref_ptr<vsg::Semaphore> semIm[2], semRd[2];   // S_im / S_rd 环 (深度 2)
        bool p12Wired = false;
        if (!viewer->recordAndSubmitTasks.empty())
            taskGfx = viewer->recordAndSubmitTasks.front();
        if (radarChain && radarChain->commandGraph && taskGfx) {
            auto device = taskGfx->device;
            const int family = device->getPhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
            uint32_t queueIdx = 0;
            const auto& qfps = device->getPhysicalDevice()->getQueueFamilyProperties();
            if (family >= 0 && size_t(family) < qfps.size() && qfps[size_t(family)].queueCount > 1)
                queueIdx = 1;   // 同 family 第 2 个 queue: 异步提交 (同族免 ownership transfer)
            else
                std::printf("[P1.2] 无独立 compute queue (family=%d) → 回退同队列双 CB\n", family);
            taskRadar = vsg::RecordAndSubmitTask::create(device, 3);
            taskRadar->commandGraphs = {radarChain->commandGraph};
            taskRadar->queue = device->getQueue(uint32_t(family), queueIdx);
            // 手动 task 须自行挂接动态数据上传队列 (vsg 仅对内置 task 自动设置)
            taskRadar->transferTask->transferQueue = taskRadar->queue;
            for (auto& s : semIm)
                s = vsg::Semaphore::create(device, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            for (auto& s : semRd)
                s = vsg::Semaphore::create(device, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            viewer->recordAndSubmitTasks.push_back(taskRadar);
            p12Wired = true;
            std::printf("[P1.2] 雷达链独立 task: queue family=%d index=%u, "
                        "二值 semaphore 环深度 2 (成像/雷达 record/submit 解耦)\n",
                        family, queueIdx);
        }

        // ---- 键盘 / 关闭事件 ----
        auto st = new ViewerState;
        st->sensorMode = true;
        st->stack = envStack.get();
        st->args = const_cast<Args*>(&args);
        st->hours_utc = args.hours_utc;
        st->enu_to_ecef = enu_to_ecef;
        st->up_ecef = up_ecef;
        st->rig = &rig;
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));
        viewer->addEventHandler(LabKeyHandler::create(st));

        // ---- ImGui 半透明 TV/MWIR 切换按钮 (方案A) ----
        // vsgImGui 标准集成: SendEventsToImGui 把 Qt/X11 事件转 ImGui, RenderImGui 作为
        // 独立 CommandGraph 叠加渲染 (透明背景 → 半透明悬浮按钮)。仅 dual_band 启用。
        vsg::ref_ptr<vsgImGui::RenderImGui> gui;
        if (args.dual_band) {
            viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());
            gui = vsgImGui::RenderImGui::create(window);
            // 跳过 imgui.ini 加载/保存: 项目目录里旧版 imgui.ini 的 TableSettings
            // 与当前 ImGui 不兼容, LoadIniSettingsFromDisk 在 TableSettingsHandler_ApplyAll
            // 段崩溃 (gdb 栈顶)。置空 IniFilename 彻底绕过。
            ImGui::GetIO().IniFilename = nullptr;
            // 中文字体 (NotoSansCJKSC) —— 用户工作区 fonts/
            namespace fs = std::filesystem;
            for (const auto& fp : {fs::path("fonts/NotoSansCJKSC.ttf"),
                                   fs::path(VSGRAYSIM_ENV_VIEWER_FONT)}) {
                std::error_code ec;
                if (fs::exists(fp, ec) && ImGui::GetIO().Fonts->AddFontFromFileTTF(
                        fp.string().c_str(), 18.0f, nullptr,
                        ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon())) {
                    std::printf("[imgui] 中文字体加载: %s\n", fp.string().c_str());
                    break;
                }
            }
            gui->add([&]() -> bool {
                const char* names[2] = {"可见光 TV", "红外 MWIR"};
                auto& io = ImGui::GetIO();
                // 半透明悬浮窗: 无标题栏/背景/缩放, 右上角固定
                ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 200.0f, 20.0f),
                                        ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.40f);
                ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoMove;
                bool open = true;
                if (ImGui::Begin("传感器切换", &open, flags)) {
                    ImGui::Text("传感器波段");
                    ImGui::Separator();
                    for (int i = 0; i < 2 && i < int(bandList.size()); ++i) {
                        const bool sel = (bands[0]->type == bandList[size_t(i)]);
                        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        if (ImGui::Button(names[i], ImVec2(160, 30)))
                            switchBand(i);   // D 方案: 点按钮 → 重建传感器链 (位置姿态不变)
                        if (sel) ImGui::PopStyleColor();
                        if (i == 0 && bandList.size() > 1) ImGui::SameLine();
                    }
                }
                ImGui::End();
                return open;
            });
            // 独立 CommandGraph → 独立提交 (在根 CG 之后, 叠加显示)
            auto guiCG = vsg::CommandGraph::create(window);
            guiCG->addChild(gui);
            viewer->assignRecordAndSubmitTaskAndPresentation({rootCG, guiCG});
            std::printf("[imgui] 半透明切换按钮已启用 (TV/MWIR)\n");
        }

        // ---- 编译 ----
        try {
            auto cr = viewer->compile();
            if (!cr) {
                std::fprintf(stderr, "编译失败 (result=%d message='%s')\n",
                             cr.result, cr.message.c_str());
                return 1;
            }
            std::printf("场景编译成功  swapchain extent=%ux%u  观察口 %ux%u + 传感器 %ux%u\n",
                        window->extent2D().width, window->extent2D().height,
                        obsW, args.height, bands[0]->pipe.sensorWidth(), bands[0]->pipe.sensorHeight());
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "编译异常: %s\n", ex.what());
            return 1;
        }

        // Render at each selected measurement deadline as well as the 60 Hz
        // heartbeat. One GPU result belongs to one event; render throughput
        // changes wall-clock speed, never the exposure/CPI simulation time.
        // #5 IR 云发射辐亮度 LUT (band4.w = ε·M(T_c)/π; 云体近黑体发射), per-band
        for (auto& br : bands) {
            br->cloudEmitReady = br->cloudEmitLut.build(br->profile.band);
            if (br->cloudEmitReady)
                bakeSkyMap(br->type, br->profile.band, br->skyBake);
        }
        const int target_frames = args.frames > 0 ? args.frames : -1;
        const int shot_frame = (target_frames > 0) ? std::min(20, target_frames - 1) : 20;
        simcore::EventClock simulationClock(simcore::PeriodicTimeline(60));
        int frame = 0;
        while (viewer->advanceToNextFrame() && !st->quit_requested) {
            viewer->handleEvents();

            // rocky 瓦片分页/动态数据单驱动 (合并新增: 观察口地球; 勿手动
            // mapNode->update()/rocky_ctx->update() —— 手动调用会以错误 Context
            // 重复编译瓦片管线 → 地形零光栅化, 原版 S0 根因)
            viewer->update();

            if (st->dump_requested) {
                dumpReadouts(envStack.get(), enu_to_ecef);
                st->dump_requested = false;
            }

            // 方案D: --switch-at 无头验证 (第 N 帧自动切到另一 band)
            if (args.switch_at >= 0 && frame == args.switch_at) {
                const int nxt = (bands[0]->type == bandList[0]) ? 1 : 0;
                switchBand(nxt);
            }

            // 三时钟域: 观察口 ENU 等效 (传感器管线世界 = ENU 局部; 机位预设
            // ENU 值即 ECEF 机位经 enu_to_ecef 正变换的源)。
            const auto& obsPreset = rig.presets[rig.preset];
            auto obsLookAtEnu = vsg::LookAt::create(obsPreset.eye, obsPreset.center,
                                                    vsg::dvec3(0.0, 1.0, 0.0));
            // 方案A: 激活 band 引用 (GUI 切换瞬时生效; 相机/环境参数两套共用)。
            auto& ap = bands[size_t(activeIdx)]->pipe;
            const auto& ab = bands[size_t(activeIdx)]->profile;
            ap.setObserverView(rig.proj(), obsLookAtEnu->transform(), obsPreset.eye);
            ap.setFrameSeed(float(frame));

            // 观察口预览 mvp (CPU 双精度组装 proj*view*ENU→ECEF)
            preview.mvp->value() = vsg::mat4(rig.proj() * rig.view() * enu_to_ecef);
            preview.mvp->dirty();

            // B3 环境契约: 栈 onFrame (单一真值源) → 服务缓存 → 波段应答 → GPU 注入。
            EnvFrame ef;
            // 环境内容 (云/海/月) 渲染于左观察口 (obsView), EnvFrame 必须携带
            // 观察口相机: 旧实现填传感器相机 → env UBO 的 view_proj/相机位置
            // 全部错位, 积云公告板/月盘按传感器窄视锥投影 → 观察口内不可见,
            // 海洋透视错乱。传感器管线自带 setObserverView/setSensorView,
            // 不消费 EnvFrame 相机 → 改观察口相机对两侧均安全。
            ef.view_proj = rig.proj() * rig.view();
            ef.model = enu_to_ecef;   // 锚点 ENU→ECEF (星历真值; 场景几何保持 ENU 局部)
            ef.cam_pos_world = enuXform(enu_to_ecef, obsPreset.eye);
            ef.cam_pos_enu = obsPreset.eye;
            ef.time_sec = simcore::toSeconds(simulationClock.now());
            ef.year = 2026; ef.month = 9; ef.day = 26;   // 满月 (月相≈1, 月光链路可验证)
            // 0-24h 昼夜循环: day_minutes 真实分钟走完一天 (0 = 固定时刻)。
            const double daySec = args.day_minutes > 0.0 ? args.day_minutes * 60.0 : 0.0;
            const double loopH = daySec > 0.0
                ? std::fmod(args.hours_utc + ef.time_sec * 24.0 / daySec, 24.0)
                : args.hours_utc;
            ef.hours_utc = loopH;
            ef.cloud_cover = float(cloudCover);
            ef.wind_speed = float(args.wind);
            ef.precipitation = float(args.rain);
            ef.visibility = float(args.vis);
            envStack->onFrame(ef);
            envService.update(ef.time_sec, loopH, -1.0, args.wind, args.rain);
            ap.setSunDir(vsg::dvec3(envService.sunDirEnu()));
            ap.setEnv(envService.answerAtmosphere(makeEnvRequest(ef.time_sec)));
            // 批次2/#1: 谱天空图注入 (数值去重, env 静止时零上传; RT 路径专用)。
            // 方案A: 仅激活 band 的谱天空图每帧按需上传。
            ap.updateSkyMap(bands[size_t(activeIdx)]->skyBake.data(),kSkyW,kSkyH);
            // #2 观察口环境: 天顶 RGB (γ 编码 → rgVis 动态 clear) + 体积云 + TV esun
            {
                auto* cq = envStack->query<rockyenv::ICloudQuery>();
                float cloudBaseM = 0.0f, cloudTopM = 0.0f;
                const bool hasCloud = cq && cq->cloudBase(cloudBaseM) && cq->cloudTop(cloudTopM);
                constexpr float kShadowStrength = 0.85f;
                const bool isTvBand = ab.band.type == BandType::TV;
                vsg::dvec3 zenLin(0.35, 0.52, 0.82);   // 兜底晴空蓝
                bool zenOk = false;
                const auto& skyB = bands[size_t(activeIdx)]->skyBake;
                if (isTvBand && skyB.size() >= size_t(kSkyW) * kSkyH * 4) {
                    const float* top = skyB.data() + size_t(kSkyW) * (kSkyH - 1) * 4;
                    double sr = 0.0, sg = 0.0, sb = 0.0;
                    for (uint32_t c = 0; c < kSkyW; ++c) {
                        sr += top[c * 4 + 2]; sg += top[c * 4 + 1]; sb += top[c * 4 + 0];
                    }
                    zenLin = vsg::dvec3(sr / kSkyW, sg / kSkyW, sb / kSkyW);
                    zenOk = true;
                }
                if (!zenOk) {
                    if (auto* aq = envStack->query<rockyenv::IAtmosphereQuery>()) {
                        const double lo = 0.38e-6, dz = (0.78e-6 - lo) / 3.0;
                        double b = 0.0, g = 0.0, rr = 0.0, s = 0.0, e = 0.0;
                        if (aq->bandSky(lo, lo + dz, 88.0, b, s, e)
                            && aq->bandSky(lo + dz, lo + 2.0 * dz, 88.0, g, s, e)
                            && aq->bandSky(lo + 2.0 * dz, lo + 3.0 * dz, 88.0, rr, s, e)) {
                            zenLin = vsg::dvec3(rr, g, b);   // 低λ→蓝 中λ→绿 高λ→红
                            zenOk = true;
                        }
                    }
                }
                // 曝光: 白天晴空天顶 TV 辐亮度 ~4 W/m²/sr → 归一到 ~1.0, 再 γ 编码
                auto gam = [](double v) {
                    return float(std::pow(std::clamp(v / 4.0, 0.0, 1.0), 1.0 / 2.2));
                };
                const vsg::vec3 zenSrgb(gam(zenLin.x), gam(zenLin.y), gam(zenLin.z));
                // TV esun (观察口物理式照明曝光锚): TV 波段应答直射项, 与 RT TV 链同源
                EnvRequest tvReq = makeEnvRequest(ef.time_sec);
                tvReq.band = BandSpec::make(BandType::TV);
                const float esunTv = float(envService.answerAtmosphere(tvReq).sun_irradiance);
                // #5 体积云参数 (RT + 观察口同约定)
                float cov = hasCloud ? cq->totalCoverage() : 0.0f;
                float dens = 0.4f;
                float ctype = float(std::clamp(args.cloud_type, 0, 4));
                if (hasCloud) {
                    if (auto* cc = envStack->query<rockyenv::ICloudControl>()) {
                        float best = -1.0f;
                        int bestType = -1;
                        for (const auto& L : cc->getLayers()) {
                            const bool sheet = L.type == CloudLayerType::Stratus ||
                                               L.type == CloudLayerType::Cirrus ||
                                               L.type == CloudLayerType::Stratocumulus;
                            if (sheet && L.coverage > best) {
                                best = L.coverage; dens = L.density; bestType = int(L.type);
                            }
                        }
                        if (best < 0.0f) {   // 无 sheet 层: 取 coverage 最大层
                            for (const auto& L : cc->getLayers())
                                if (L.coverage > best) { best = L.coverage; bestType = int(L.type); }
                        }
                        if (bestType >= 0) ctype = float(bestType);
                    }
                }
                float cloudEmitL = 0.0f;
                if (hasCloud && !isTvBand && bands[size_t(activeIdx)]->cloudEmitReady)
                    cloudEmitL = 0.96f * bands[size_t(activeIdx)]->cloudEmitLut.exitance(std::max(
                        210.0f, 288.15f - 6.5e-3f * (0.5f * (cloudBaseM + cloudTopM))))
                        / float(M_PI);
                if (auto* rt = ap.rtModule())
                    rt->setCloudVolume(hasCloud ? cloudBaseM : 0.0f, hasCloud ? cloudTopM : 0.0f,
                                       std::clamp(cov, 0.0f, 1.0f), std::clamp(dens, 0.0f, 1.0f),
                                       float(ef.wind_dir_enu.x), float(ef.wind_dir_enu.z),
                                       float(ef.wind_speed), float(ef.time_sec),
                                       cloudEmitL, kShadowStrength, ctype);
                // #4 海面逐像素波场: CPU WaveField 谐波表快照 → BandUbo oceanWave 段
                if (auto* rt = ap.rtModule())
                    if (auto* os = envStack->query<rockyenv::IOceanSampler>()) {
                        float wh[16][4] = {};
                        float q = 0.0f;
                        const int n = os->waveHarmonics(wh, q);
                        static bool waveDbg = false;
                        if (!waveDbg) {
                            waveDbg = true;
                            std::printf("[wave] n=%d q=%.2f", n, q);
                            for (int i = 0; i < n && i < 3; ++i)
                                std::printf("  L%d=%.1f A%d=%.3f d=(%.2f,%.2f)", i, wh[i][0], i, wh[i][1], wh[i][2], wh[i][3]);
                            std::printf("\n");
                        }
                        rt->setOceanWave(&wh[0][0], n, q);
                    }
                // #5 观察口同约定体积云 (sensor_vis.frag VolUbo, march 与 RT 链同源)
                ap.setCloudVolume(
                    vsg::vec4(hasCloud ? cloudBaseM : 0.0f, kShadowStrength,
                              hasCloud ? cloudTopM : 0.0f, std::clamp(cov, 0.0f, 1.0f)),
                    vsg::vec4(std::clamp(dens, 0.0f, 1.0f), float(ef.wind_speed),
                              float(ef.wind_dir_enu.x), float(ef.wind_dir_enu.z)),
                    vsg::vec4(float(ef.time_sec), cloudEmitL, ctype, 0.0f));
                ap.setObserverEnv(zenSrgb, hasCloud ? cloudBaseM : 0.0f, kShadowStrength,
                                  esunTv, isTvBand);
            }

            // This viewer has analytic static/constant-velocity fixture truth.
            // Keep instance positions distinct; production truth will come from
            // physics snapshots rather than copying the selected camera to all.
            for (size_t i = 0; i < states.size(); ++i) {
                StateSample truth;
                truth.t = ef.time_sec;
                truth.pos = registry[i].position_enu;
                if (ptrdiff_t(i) == selIdx) {
                    truth.pos = radarMode ? radarEye + platVelV * ef.time_sec : senEye;
                    truth.vel = radarMode ? platVelV : vsg::dvec3(0.0, 0.0, 0.0);
                }
                if (!states[i].push(truth)) {
                    std::fprintf(stderr, "[state] invalid truth for sensor #%d\n", registry[i].desc->sensor_id);
                    viewer->deviceWaitIdle();
                    return 2;
                }
            }
            std::vector<SchedulerEvent> due;   // B5: 选中实例的到期事件 (分发用)
            for (size_t i = 0; i < registry.size(); ++i) {
                if (!scheds[i].valid()) continue;
                do {
                for (const auto& ev : scheds[i].update(ef.time_sec)) {
                    if (ptrdiff_t(i) == selIdx) due.push_back(ev);
                    if (ev.frame_id >= 3) continue;   // 每实例前 3 事件 (仅打印节流)
                    const SensorDesc& d = *registry[i].desc;
                    if (ev.kind == SchedulerEvent::Kind::CpiBoundary)
                        std::printf("[sched] #%d %-12s CPI#%u t=[%.4f..%.4f] pulses=%u\n",
                                    d.sensor_id, d.name.c_str(), unsigned(ev.frame_id),
                                    ev.t_start, ev.t, ev.pulses);
                    else
                        std::printf("[sched] #%d %-12s 成像 t_exp=%.4f\n",
                                    d.sensor_id, d.name.c_str(), ev.t);
                }
                } while (scheds[i].backlogged());
            }
            if (due.size() > 1) {
                std::fprintf(stderr, "[sched] multiple measurements require separate GPU evaluations\n");
                viewer->deviceWaitIdle();
                return 2;
            }
            if(!due.empty()) ap.setFrameSeed(float(due.front().frame_id));

            // B7 状态机迁移 (模拟制导逻辑 Param 消息): --track-after <s>
            // SEARCH→TRACK, slaved 指向主目标。
            if (args.track_after >= 0.0 && ef.time_sec >= args.track_after) {
                const bool radarPending = radarMode && gimbalRadar.state() != GimbalModel::State::Track;
                const bool imPending = !radarMode && gimbalIm.state() != GimbalModel::State::Track;
                if (radarPending || imPending) {
                    const vsg::dvec3 eye = radarMode ? radarEye : senEye;
                    const vsg::dvec3 baseDir = radarMode ? radarBaseDir : imBaseDir;
                    double az0 = 0.0, el0 = 0.0;
                    GimbalModel::dirToAzEl(baseDir, az0, el0);
                    double tgtAz = az0, tgtEl = el0;
                    if (sceneData && sceneData->primaryTarget(sceneData->terrain_ref)) {
                        const vsg::dvec3 to = sceneData->primaryTarget(sceneData->terrain_ref)
                                                  ->position_enu - eye;
                        GimbalModel::dirToAzEl(vsg::normalize(to), tgtAz, tgtEl);
                    }
                    GimbalModel& g = radarMode ? gimbalRadar : gimbalIm;
                    g.setTargetAzEl(tgtAz - az0, tgtEl - el0);   // 机体系相对标称偏转
                    g.setState(GimbalModel::State::Track);
                    std::printf("[gimbal] 状态迁移 SEARCH→TRACK (t=%.2fs, 目标 az=%.1f° el=%.1f°, slew 限幅 90°/s)\n",
                                ef.time_sec, tgtAz, tgtEl);
                }
            }

            // B7 扫描指向: 成像类曝光时刻单指向 (t_exp 事件 → LOS 更新)
            if (!radarMode) {
                for (const auto& ev : due) {
                    if (ev.kind != SchedulerEvent::Kind::Image) continue;
                    const GimbalLos gl = gimbalIm.los(ev.t);
                    double az0 = 0.0, el0 = 0.0;
                    GimbalModel::dirToAzEl(imBaseDir, az0, el0);
                    const vsg::dvec3 dir = GimbalModel::azElToDir(az0 + gl.az_deg, el0 + gl.el_deg);
                    beamAzDeg = az0 + gl.az_deg;
                    beamElDeg = el0 + gl.el_deg;
                    // vsg::LookAt 无 set(eye,center,up) 方法, 直接赋值 public 成员
                    senLookAt->eye = senEye;
                    senLookAt->center = senEye + dir * imBaseDist;
                    senLookAt->up = vsg::dvec3(0.0, 1.0, 0.0);
                    break;   // selected event has its own exact render time
                }
            }
            // Apply the current event's gimbal before recording every pipeline,
            // including the raster fallback (not just the RT G-buffer camera).
            ap.setSensorView(senPersp->transform(), senLookAt->transform(), senEye,
                             0.5, senFar, senVfov, senAspect);

            // B6 雷达链帧驱动: 传感器视角 + 本帧 CPI 脉冲天线位姿
            if (radarMode) {
                // P1.1: G-buffer 活跃视角 = 雷达基座视角 (CPI 分支内再覆盖为中心视角)
                gbufferPool->setSensorView(radarPersp->transform(), radarLookAt->transform());
                if (!due.empty()) {
                    const SchedulerEvent& ev = due.back();   // CPI 结束事件 (帧时统)
                    const RadarChainParams& rcp = radarChain->params();
                    const double t0 = ev.t_start;
                    double az0 = 0.0, el0 = 0.0;
                    GimbalModel::dirToAzEl(radarBaseDir, az0, el0);
                    std::vector<vsg::dvec3> ant(size_t(rcp.cpi_pulses));
                    std::vector<vsg::dvec3> beamDirs(size_t(rcp.cpi_pulses));
                    for (uint32_t n = 0; n < rcp.cpi_pulses; ++n) {
                        StateSample s;
                        const double t_n = t0 + double(n) / rcp.prf;
                        if (!states[size_t(selIdx)].sample(t_n, s)) {
                            std::fprintf(stderr, "[state] unavailable pulse truth t=%.9f (CPI=%llu)\n",
                                         t_n, static_cast<unsigned long long>(ev.frame_id));
                            viewer->deviceWaitIdle();
                            return 2;
                        }
                        ant[n] = s.pos;
                        const GimbalLos gl = gimbalRadar.los(t_n);   // 逐脉冲扫描偏转
                        beamDirs[n] = GimbalModel::azElToDir(az0 + gl.az_deg, el0 + gl.el_deg);
                    }
                    radarChain->setPulses(ant, true, beamDirs, float(rcp.beam_width_rad));
                    const double tMid = t0 + double(rcp.cpi_pulses) / (2.0 * rcp.prf);
                    const GimbalLos glMid = gimbalRadar.los(tMid);
                    StateSample middle;
                    if (!states[size_t(selIdx)].sample(tMid, middle)) {
                        std::fprintf(stderr, "[state] unavailable CPI midpoint truth\n");
                        viewer->deviceWaitIdle();
                        return 2;
                    }
                    const vsg::dvec3 dir = GimbalModel::azElToDir(az0 + glMid.az_deg, el0 + glMid.el_deg);
                    beamAzDeg = az0 + glMid.az_deg;
                    beamElDeg = el0 + glMid.el_deg;
                    radarLookAt->eye = middle.pos;
                    radarLookAt->center = middle.pos + dir * radarBaseDist;
                    radarLookAt->up = vsg::dvec3(0.0, 1.0, 0.0);
                    gbufferPool->setSensorView(radarPersp->transform(), radarLookAt->transform());
                } else {
                    radarChain->setPulses({}, false);
                }
            } else if (gbufferPool->ready()) {
                // P1.1: 成像模式活跃视角 = 成像相机 (含 B7 扫描 t_exp 指向)
                gbufferPool->setSensorView(senPersp->transform(), senLookAt->transform());
            }

            // P1.2: 每帧刷新队列间 semaphore 环形配对 (二值, 深度 2)
            if (p12Wired) {
                taskGfx->waitSemaphores.clear();
                if (frame >= 1) taskGfx->waitSemaphores.push_back(semRd[size_t(frame - 1) % 2]);
                taskGfx->signalSemaphores = {semIm[size_t(frame) % 2]};
                taskRadar->waitSemaphores = {semIm[size_t(frame) % 2]};
                taskRadar->signalSemaphores = {semRd[size_t(frame) % 2]};
            }

            // 截图请求: 回读命令已挂根 commandGraph 尾部, 本帧提交内执行
            const bool shotNow = (!args.shot.empty() && frame == shot_frame);
            if (shotNow && readbackBuffer) wantReadback = true;

            viewer->recordAndSubmit();
            viewer->present();

            // B5 分发时序: present 后执行 (GPU 产品回读需在提交/呈现之后)
            publishStep(ef.time_sec, frame, due);
            // B6: CPI 边界事件 → 回读取回 + 段处理 (+ 分发/指标/dump)
            if (radarMode && !due.empty())
                radarProcessCpi(due.back());

            if (shotNow) saveShot(args.shot);

            // B2 验收: P0 回读 (最后一帧, 需 RT 路径)
            if (!args.dump_p0.empty() && target_frames > 0 && frame + 1 >= target_frames) {
                if (auto* rt = ap.rtModule())
                    rt->dumpP0(*window, args.dump_p0.c_str());
                else
                    std::printf("[dump-p0] 无 RT 模块 (光栅路径), 跳过\n");
            }

            ++frame;
            if (target_frames > 0 && frame >= target_frames) break;
            const auto deadline = scheds[size_t(selIdx)].valid()
                ? scheds[size_t(selIdx)].nextDeadline()
                : std::numeric_limits<simcore::TimePoint>::max();
            simulationClock.advance(deadline);
        }

        std::printf("env_viewer 传感器链退出 (共 %d 帧)\n", frame);
        // P0 验收统计: geometryBuilds=1 / envDirtyCount≈帧数 / lutTextures 去重
        sharedScene->printStats();
        viewer->deviceWaitIdle();
        return 0;
    }
}
