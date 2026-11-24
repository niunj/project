#pragma once
/// @file GBufferPool.h —— P1.1: 全场景唯一求交 (G-buffer 一次 TraceRays)。
///
/// 解决"同帧重复求交"瓶颈: P1 前成像 P0 (band_rt RT) + 雷达散射体提取
/// (radar_scatter RT) 各自完整求交 (多波段再乘 N)。P1.1 收敛为:
///   全场景唯一 RT 管线 + 唯一 TraceRays → 波段无关 G-buffer (RGBA32F×3):
///     G0 = (T_kelvin, emissivity, range_m, hit)   —— band_shade.comp 消费
///     G1 = (pos.xyz ENU, matId)                   —— radar_scatter.comp 消费
///     G2 = (normal.xyz, 0)                        —— 批次1: 太阳反射/GGX 耀斑
///   hit=0 (天空): G0=(0,0,1e6,0), G1=0, G2=0。
///
/// One view/resolution per pool. GpuSensorRuntime owns one pool per sensor;
/// only identical view/time samples may share a pool. Geometry/BLAS/TLAS remain
/// scene-owned. Hardware RT uses TraceRays, other devices use compute BVH.
///
/// 资源依赖: 几何 SSBO/BLAS/TLAS 复用 SharedSceneResources 池 (P0);
/// 池未就绪时本类自抽取 (迁移期回退, 与 P0 前单实例路径同构)。

#include <rocky_env/SceneSource.h>    // RadarMaterial

#include <vsg/core/ref_ptr.h>
#include <vsg/core/Value.h>
#include <vsg/maths/mat4.h>
#include <vsg/core/Array.h>           // floatArray (批次1 相机位置 UBO)

#include <string>

namespace vsg {
class Window;
class Device;
class Node;
class ImageView;
class Commands;
class BufferInfo;
template<typename T> class Value;
}

namespace rockyenv {
namespace sensor {

class SharedSceneResources;

/// G-buffer rgen push 块 (与 BandRTPush 同布局: 逆视/逆投影矩阵)。
struct GBufferPush {
    vsg::mat4 viewInv;
    vsg::mat4 projInv;
};

/// P1.1 G-buffer 池: 全场景唯一 RT 求交。
class GBufferPool {
public:
    struct Result {
        bool ok = false;
        std::string error;
    };

    /// 建 RT 管线 + G-buffer 图 + 唯一 TraceRays 命令链 (viewer->compile() 触发)。
    ///   width/height  G-buffer 分辨率 (上层保证与成像 profile / 雷达栅格一致;
    ///                 消费方 build 时校验, 不一致报错)
    ///   shared        P0 资源池 (几何/BLAS/TLAS/材质表复用; 未就绪 → 自抽取回退)
    Result build(vsg::Window& window,
                 vsg::ref_ptr<vsg::Node> scene,
                 const std::vector<RadarMaterial>* materials,
                 SharedSceneResources* shared,
                 uint32_t width, uint32_t height);
    Result build(vsg::Device& device,
                 vsg::ref_ptr<vsg::Node> scene,
                 const std::vector<RadarMaterial>* materials,
                 SharedSceneResources* shared,
                 uint32_t width, uint32_t height);

    /// 每帧活跃视角 (唯一消费者视角: 雷达模式 = 雷达视角, 成像模式 = 成像视角)。
    void setSensorView(const vsg::dmat4& proj, const vsg::dmat4& view);

    // ---- G-buffer 输出 (STORAGE, GENERAL 布局全程) ----
    vsg::ref_ptr<vsg::ImageView> g0;   ///< (T_kelvin, emissivity, range_m, hit)
    vsg::ref_ptr<vsg::ImageView> g1;   ///< (pos.xyz ENU, matId)
    vsg::ref_ptr<vsg::ImageView> g2;   ///< (normal.xyz ENU, 0) 批次1: 面法线
    vsg::ref_ptr<vsg::ImageView> g3;   ///< (uv.x, uv.y, texSlot, 0) TV 彩色批次
    /// 命令链: bindRT → [G UNDEFINED→GENERAL] → TraceRays → [RT写→shader读]。
    /// 上层挂于 graphics commandGraph (暖身后、band_shade 前)。
    vsg::ref_ptr<vsg::Commands> commands;

    /// σ 材质表 SSBO (RadarChain scatter.comp 消费; 与池同源, 回退路径自建)。
    vsg::ref_ptr<vsg::BufferInfo> matDataInfo;
    /// 谱材质表 SSBO (批次1: band_shade 太阳反射/GGX 消费; 每材质 2 vec4,
    /// 与 σ 材质表同序同数; 与池同源, 回退路径自建)。
    vsg::ref_ptr<vsg::BufferInfo> spectralDataInfo;
    /// 相机 ENU 位置 UBO (批次1: band_shade 镜面 V 向; setSensorView 每帧更新,
    /// 4 float [eye.xyz, 0]; 与活跃视角同源 —— 雷达模式=雷达视点, 成像=成像视点)。
    vsg::ref_ptr<vsg::BufferInfo> camPosInfo;

    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    bool ready() const { return ready_; }
    const std::string& error() const { return error_; }

private:
    Result buildCompute(vsg::Device&,SharedSceneResources*,uint32_t,uint32_t);
    uint32_t w_ = 0, h_ = 0;
    bool ready_ = false;
    std::string error_;
    SharedSceneResources* shared_ = nullptr;   ///< P0 池 (非拥有)
    vsg::ref_ptr<vsg::Value<GBufferPush>> push_;
    vsg::ref_ptr<vsg::floatArray> camPosData_; ///< camPosInfo 载体 (DYNAMIC_DATA)
};

} // namespace sensor
} // namespace rockyenv
