#pragma once
/// @file SensorPipeline.h —— 多传感器仿真渲染: GPU RTT 级联编排 (设计文档 §9)。
///
/// 级联 (全部离屏 RGBA32F, GPU 直到出口):
///   rad(P0 蓝方辐射亮度) → vis(观察口) → optics(P2) → detector(P3 raw)
///   → electronics(P4 image) → display(左观察口/右传感器口, swapchain)
///
/// Timing is supplied by the host. env_viewer evaluates each selected exposure/
/// CPI boundary on an event clock; its scene truth is a static/analytic fixture.
#ifndef ROCKY_ENV_SENSOR_PIPELINE_H
#define ROCKY_ENV_SENSOR_PIPELINE_H

#include "rocky_env/SensorApi.h"
#include "rocky_env/SceneSource.h"   // RadarMaterial (build 参数)

#include <vsg/core/ref_ptr.h>
#include <vsg/core/Value.h>
#include <vsg/core/Array.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/vk/Framebuffer.h>

#include <memory>
#include <string>
#include <vector>

namespace vsg {
class Window;
class Device;
class Node;
class CommandGraph;
class RenderGraph;
}

namespace rockyenv {
namespace sensor {

class BandRT;   // B2/P1.1: 波段着色模块 (唯一指针成员, 头文件不暴露实现)
class GBufferPool;   // P1.1 全场景唯一求交 (上层创建, 引用传递)

/// 视图 UBO (与 shader SensorView 严格对齐, 112B；保留旧类型名):
///   vp   proj*view   cam near,far,tanHalfFov,aspect
///   misc x=噪声时变种子, yzw=太阳方向   eye 相机世界位置
struct SensorPushBlock {
    vsg::mat4 vp;
    vsg::vec4 cam;
    vsg::vec4 misc;
    vsg::vec4 eye;
};
using SensorPushValue = vsg::Value<SensorPushBlock>;

/// 程序化热场景 (地面 290K/坦克 305K/引擎舱 450K/尾焰 700K)。
/// 顶点: binding0 pos(vec3) + binding1 props(vec2: T_kelvin, emissivity)。
vsg::ref_ptr<vsg::Node> createThermalScene();

/// GPU 传感器链编排器。
class SensorPipeline {
public:
    SensorPipeline();    // BandRT 唯一指针成员 → 构造/析构出线 (cpp 内完整类型)
    ~SensorPipeline();
    struct BuildResult {
        bool ok = false;
        std::string error;
    };

    /// 构建离屏级联 + 窗口显示 pass。scene 由 createThermalScene 或
    /// buildThermalSceneGraph 提供 (观察口与传感器口共享同一几何 —— 设计文档
    /// §3 共享资产层)。
    /// P0 来源 (P1.1): band_shade compute 着色 (GBufferPool 唯一 TraceRays 的
    /// G-buffer), 构建失败自动回退光栅化 rad pass —— 下游级联零感知。
    /// allowRT=false 强制光栅化路径 (如 --no-rt 或无 RT 扩展窗口)。
    /// shared: P0 资源池 (可空; geometry/LUT/sampler 复用, 波段环境 UBO 独立)。
    /// gbuffer: P1.1 G-buffer 池 (allowRT 时必需)。
    BuildResult build(vsg::ref_ptr<vsg::Window> window,
                      const SensorProfile& profile,
                      const BandRadianceLut& lut,
                      const EnvAnswer& envAnswer,
                      vsg::ref_ptr<vsg::Node> scene,
                      const std::vector<RadarMaterial>* rtMaterials = nullptr,
                      bool allowRT = true,
                      class SharedSceneResources* shared = nullptr,
                      class GBufferPool* gbuffer = nullptr);
    /// Product execution only: no surface, swapchain, observer or display pass.
    BuildResult buildOffscreen(vsg::Device& device,
                      const SensorProfile& profile,const BandRadianceLut& lut,
                      const EnvAnswer& environment,vsg::ref_ptr<vsg::Node> scene,
                      class SharedSceneResources* shared,class GBufferPool* gbuffer);
    /// Explicit raster mode also supports headless GPU validation. The default
    /// production overload above retains the shared G-buffer execution path.
    BuildResult buildOffscreen(vsg::Device& device,
                      const SensorProfile& profile,const BandRadianceLut& lut,
                      const EnvAnswer& environment,vsg::ref_ptr<vsg::Node> scene,
                      class SharedSceneResources* shared,class GBufferPool* gbuffer,bool allowRT);
    /// Shared spectral sky input for RT and raster P0. TV uses four floats per
    /// texel in the existing low-to-high wavelength (B,G,R,luma) convention;
    /// raster fallback retains linear radiance and never samples display RGB.
    void updateSkyMap(const float* data,uint32_t width,uint32_t height);

    // ---- 每帧更新 (push constants 在每次 record 时重新读取) ----
    void setSensorView(const vsg::dmat4& proj, const vsg::dmat4& view, const vsg::dvec3& eye,
                       double nearZ, double farZ, double vfovDeg, double aspect);
    void setObserverView(const vsg::dmat4& proj, const vsg::dmat4& view, const vsg::dvec3& eye);
    /// #2/#3 观察口环境注入:
    ///   zenith_srgb     天顶 RGB (γ 已编码) → rgVis 动态 clear 色 (背景像素无片元,
    ///                   clear 色即天空; 每帧随 env 更新)
    ///   cloudBaseM/影强 → push cam.y/z (sensor_vis.frag cloudShadow)
    ///   esun (TV 波段)  → push cam.w (物理式照明的曝光锚)
    ///   skyValid        → push cam.x (RGB 天空图就绪; false 时 frag 走回退式)
    void setObserverEnv(const vsg::vec3& zenith_srgb, float cloudBaseM,
                        float shadowStrength, float esun, bool skyValid);
    /// #5 观察口体积云注入 (sensor_vis.frag binding 4 VolUbo, 与 band_shade.comp
    /// BandUbo 云段同约定): a=(云底 m, 影强, 云顶 m, 覆盖率) b=(密度, 风速,
    /// 风向x, 风向z) c=(时间 s, IR 云发射辐亮度, pad, pad)。数值去重。
    void setCloudVolume(const vsg::vec4& a, const vsg::vec4& b, const vsg::vec4& c);
    void setSunDir(const vsg::dvec3& dir) {
        if (observerPush_) {
            auto& m = observerPush_->value().misc;
            m = vsg::vec4(m.x, float(dir.x), float(dir.y), float(dir.z));
            observerPush_->dirty();
        }
        if (sensorPush_) {
            auto& m = sensorPush_->value().misc;
            m = vsg::vec4(m.x, float(dir.x), float(dir.y), float(dir.z));
            sensorPush_->dirty();
        }
    }
    void setFrameSeed(float seed) {
        if (sensorPush_) {sensorPush_->value().misc.x=seed;sensorPush_->dirty();}
        if (observerPush_) {observerPush_->value().misc.x=seed;observerPush_->dirty();}
    }
    /// B3 环境注入: 每帧更新 UBO env 段 [20..23] (σ/天空/直射, 消费方
    /// sensor_rad.frag) 并转发 BandRT::setEnv (RT rchit/rmiss 同参数)。
    void setEnv(const EnvAnswer& env);

    vsg::ref_ptr<vsg::CommandGraph> commandGraph;
    uint32_t sensorWidth() const { return sensorW_; }
    uint32_t sensorHeight() const { return sensorH_; }
    /// B2 调试/验收: RT 模块访问 (P0 回读 dumpP0); 无 RT 路径返回空。
    BandRT* rtModule() { return bandRT_.get(); }
    /// UBO (48 floats = 192B, 与 shader SensorUBO 对齐; 调试用)
    vsg::ref_ptr<vsg::floatArray> ubo;

    // ---- B5 分发时序: 产品级回读 (stages 发布的数据源) ----
    /// 指定级是否可读 (P0=RT/光栅 rad; P1 未产出; P2 optics; P3 det; P4 elec)。
    bool hasStage(ProductStage stage) const;
    /// Offscreen execution prefix ending at the requested product. Compile this
    /// graph and read that stage; downstream stages need not execute.
    vsg::ref_ptr<vsg::CommandGraph> offscreenStageGraph(ProductStage stage)const;
    /// 回读指定级产品 (RGBA32F 行优先, w*h*4 floats; 一次性传输命令,
    /// 自动布局往返 TRANSFER_SRC)。应在当帧 recordAndSubmit 之后调用。
    bool readbackStage(vsg::Window& window, ProductStage stage, std::vector<float>& out);
    bool readbackStage(vsg::Device& device, ProductStage stage, std::vector<float>& out);
    /// GPU 全链闭环: 指定级常驻显存 ImageView (载荷不回读, 显控经
    /// VkImage/VkDeviceMemory 句柄发布)。curLayout 返回当前图像布局
    /// (RT P0=GENERAL, 其余 SHADER_READ_ONLY_OPTIMAL)。无该级 → 空。
    vsg::ref_ptr<vsg::ImageView> stageImageView(ProductStage stage,
                                                VkImageLayout& curLayout) const;

private:
    BuildResult buildImpl(vsg::ref_ptr<vsg::Device>,vsg::ref_ptr<vsg::Window>,
                      const SensorProfile&,const BandRadianceLut&,const EnvAnswer&,
                      vsg::ref_ptr<vsg::Node>,const std::vector<RadarMaterial>*,bool,
                      SharedSceneResources*,GBufferPool*);
    uint32_t sensorW_ = 640, sensorH_ = 512;
    vsg::ref_ptr<SensorPushValue> sensorPush_;
    vsg::ref_ptr<SensorPushValue> observerPush_;
    std::unique_ptr<BandRT> bandRT_;   ///< B2: RT P0 (构建失败时为空 → 光栅回退)
    // 产品级颜色目标 (B5 回读; P1 未产出)
    vsg::ref_ptr<vsg::ImageView> radColor_;    ///< P0 光栅回退路径 (RT 时用 bandRT_->p0)
    bool p0FromRT_ = false;
    vsg::ref_ptr<vsg::ImageView> opticsColor_; ///< P2 焦平面
    vsg::ref_ptr<vsg::ImageView> detColor_;    ///< P3 full-well fraction (not calibrated electrons)
    vsg::ref_ptr<vsg::ImageView> elecColor_;   ///< P4 图像
    vsg::ref_ptr<vsg::RenderGraph> rgVis_;     ///< #2: 观察口 RG (动态 clear 色每帧更新)
    bool tvBand_ = false;
    vsg::ref_ptr<vsg::vec4Array2D> rasterSky_; ///< 256x128 linear spectral sky, shared by raster sensor/observer
    // #5: 观察口体积云 UBO (12 float = 3 vec4, DYNAMIC_DATA; setCloudVolume 数值去重)
    vsg::ref_ptr<vsg::floatArray> uboVolume_;
    vsg::vec4 volCacheA_ = vsg::vec4(-1.0f, -1.0f, -1.0f, -1.0f);
    vsg::vec4 volCacheB_ = vsg::vec4(-1.0f, -1.0f, -1.0f, -1.0f);
    vsg::vec4 volCacheC_ = vsg::vec4(-1.0f, -1.0f, -1.0f, -1.0f);
};

} // namespace sensor
} // namespace rockyenv

#endif // ROCKY_ENV_SENSOR_PIPELINE_H
