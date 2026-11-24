#pragma once
/// @file EnvCommon.h
/// @brief rocky_env 模块内部共享工具: VSG 自定义管线构建 + 环境 UBO 填充。
/// 仅供 env_modules/src 内部使用, 不对外暴露。
#ifndef ROCKY_ENV_COMMON_H
#define ROCKY_ENV_COMMON_H

#include <rocky_env/EnvApi.h>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/state/Descriptor.h>

#include <string>

namespace rockyenv {
/// Compose the ECEF transforms in double precision before uploading a local
/// effect's float UBO. Shader-space positions and all weather queries remain ENU.
inline EnvFrame localRenderFrame(const EnvFrame& input) {
    auto output=input;
    output.view_proj=input.view_proj*input.model;
    output.model=vsg::dmat4(1.0);
    return output;
}

/// 环境 UBO 布局 (std140, 84 float), 与 shaders/env/*.vert 一致:
///   mat4 view_proj; mat4 model; vec4 camPosTime; vec4 sun; vec4 wind; vec4 misc;  // 0..47
///   vec4 ext0; vec4 ext1;   // 模块自定义扩展 (海洋边界等), offset 48..55
///   vec4 atm;               // x=能见度(米) (所有模块共用), offset 56..59
///   vec4 deepCol;           // 海洋深水色 rgb, offset 60..63 (海洋专用)
///   vec4 shallowCol;        // 海洋浅水色 rgb, offset 64..67 (海洋专用)
///   vec4 sunLight;          // rgb=太阳直射色, w=昼因子, offset 68..71 (天空下发)
///   vec4 ambient;           // rgb=天光环境色, offset 72..75
///   vec4 skyHorizon;        // rgb=地平线天色, offset 76..79
///   vec4 skyZenith;         // rgb=天顶天色, offset 80..83
///   vec4 moon;              // xyz=月亮方向ENU, w=月相, offset 84..87 (仅 sky_dome)
///   vec4 astro;             // x=本地恒星时(rad) y=纬度(rad), offset 88..91
///   vec4 oceanWave[16];     // S1/S2 海洋谐波表: x=L y=A z=cosθ w=sinθ,
///                            // offset 92..155 (16 谐波; 风浪在前, 涌浪紧随)
///   vec4 oceanOpt;          // S2 海洋选项, offset 156..159:
///                            //   x=swellH y=reflection z=curvature w=waveCount
///   vec4 oceanOpt2;         // P3 GPU FFT 海洋平铺, offset 160..163:
///                            //   x=FFT平铺边长(m) y=流向x z=流向z w=FFT开关(1=vertex采样)
constexpr unsigned kEnvUboFloats = 164;
constexpr unsigned kEnvUboBaseFloats = 48;  // 前 48 float 为所有模块共用
constexpr unsigned kOceanWaveUboOffset = 92;  // 海洋谐波段起始
constexpr unsigned kOceanWaveMax = 16;        // 谐波槽数
constexpr unsigned kOceanOpt2UboOffset = 160; // P3 FFT 平铺参数段起始

/// 逐帧填充环境 UBO。misc 为 4 个自定义参数 (云/海洋各自解释)。
/// extra 为可选的 8 个扩展 float (写入 offset 48..55, 两个 vec4), 不需要传 nullptr。
/// deep/shallow 为海洋水色 (写入 offset 60..67), 云模块传 nullptr。
/// opt2 为 P3 GPU FFT 海洋平铺参数 (写入 offset 160..163: x=平铺边长 y=流向x
/// z=流向z w=FFT开关), 云/非 FFT 海洋传 nullptr (段保持 0 → shader 走谐波分支)。
void fillEnvUBO(vsg::floatArray& ub, const EnvFrame& f, const float misc[4],
                const float* extra = nullptr,
                const vsg::vec3* deep = nullptr,
                const vsg::vec3* shallow = nullptr,
                const float* opt2 = nullptr);

/// 从 shaders/env/ 加载 GLSL 并构建自定义 GraphicsPipeline。
/// 顶点输入: loc0 vec3 pos, loc1 vec3 normal, loc2 vec2 uv。
/// 返回 StateGroup (含 BindGraphicsPipeline + 环境 UBO 描述符集), 可直接
/// addChild(VertexIndexDraw)。失败返回空。out_ub 返回动态 UBO 数组供逐帧填充。
///
/// P3 扩展 (ocean GPU FFT): 传入 extra_set1_descriptors 时, 管线追加
/// set=1 采样纹理描述符组 (结合采样器, 顶点+片元阶段), 供 ocean.vert
/// vertex texture fetch 位移/法线泡沫场。extra_set1_stage 决定着色阶段;
/// extra_set1 非空时 out_bind_set1 返回对应 BindDescriptorSet (供后续替换
/// 真实 ImageView 描述符, 见 OceanModule)。
vsg::ref_ptr<vsg::StateGroup> buildEnvPipeline(
    const std::string& vert_name,
    const std::string& frag_name,
    bool blending, bool depth_test,
    vsg::ref_ptr<vsg::floatArray>& out_ub,
    const vsg::Descriptors* extra_set1 = nullptr,
    VkShaderStageFlags extra_set1_stage = VK_SHADER_STAGE_VERTEX_BIT |
                                          VK_SHADER_STAGE_FRAGMENT_BIT,
    vsg::ref_ptr<vsg::BindDescriptorSet>* out_bind_set1 = nullptr);

/// 带 LUT 描述符集的天空管线构建 (set=0: UBO, set=1: LUT textures)。
/// lut_descriptors 为 set=1 的四个采样器描述 (transmittance/scattering/irradiance/月面);
/// 缺失任意纹理时返回空，不创建会静默查零的占位管线。
/// 返回 StateGroup (含 BindGraphicsPipeline + 两个描述符集), out_ub 返回 UBO。
vsg::ref_ptr<vsg::StateGroup> buildSkyLUTPipeline(const std::string& vert_name,
                                                   const std::string& frag_name,
                                                   bool blending, bool depth_test,
                                                   vsg::ref_ptr<vsg::floatArray>& out_ub,
                                                   const vsg::Descriptors& lut_descriptors = {});

/// 解析 shaders/env/<name> 的绝对路径 (相对可执行文件 / 其父目录)。
std::string resolveEnvShader(const std::string& name);

}  // namespace rockyenv

#endif  // ROCKY_ENV_COMMON_H
