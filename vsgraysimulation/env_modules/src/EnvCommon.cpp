/// @file EnvCommon.cpp —— rocky_env 内部管线/UBO 工具实现。
#include "EnvCommon.h"

#include <vsg/state/ShaderStage.h>
#include "EarthShader.h"
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/DescriptorSetLayout.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/ViewportState.h>
#include <vsg/nodes/StateGroup.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

namespace rockyenv {

vsg::ref_ptr<vsg::StateGroup> buildEnvPipeline(const std::string& vert_name,
                                               const std::string& frag_name,
                                               bool blending, bool depth_test,
                                               vsg::ref_ptr<vsg::floatArray>& out_ub,
                                               const vsg::Descriptors* extra_set1,
                                               VkShaderStageFlags extra_set1_stage,
                                               vsg::ref_ptr<vsg::BindDescriptorSet>* out_bind_set1)
{
    auto vert_stage = vsgraysim::earth::earthShader(vert_name);
    auto frag_stage = vsgraysim::earth::earthShader(frag_name);

    // 分离数组布局: loc0 vec3 pos / loc1 vec3 normal / loc2 vec2 uv。
    // 模块侧均以 3 个独立数组 assignArrays({pos, normal, uv}) 传入, 而
    // VertexIndexDraw::record 把第 i 个数组绑定到 binding i —— 必须声明
    // 3 个 binding (各自 stride) 并把属性显式映射到对应 binding。旧实现
    // 只声明单 binding 交错 stride=44, 顶点抓取按 44 字节步进错读分离的
    // position 数组 → 几何乱位 (层云片"异形透明"、积云公告板不可见的根因)。
    auto vis = vsg::VertexInputState::create();
    vis->vertexAttributeDescriptions.resize(3);
    vis->vertexAttributeDescriptions[0].location = 0;
    vis->vertexAttributeDescriptions[0].binding = 0;
    vis->vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[0].offset = 0;
    vis->vertexAttributeDescriptions[1].location = 1;
    vis->vertexAttributeDescriptions[1].binding = 1;
    vis->vertexAttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[1].offset = 0;
    vis->vertexAttributeDescriptions[2].location = 2;
    vis->vertexAttributeDescriptions[2].binding = 2;
    vis->vertexAttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vis->vertexAttributeDescriptions[2].offset = 0;
    vis->vertexBindingDescriptions.resize(3);
    for (int b = 0; b < 3; ++b) {
        vis->vertexBindingDescriptions[b].binding = static_cast<uint32_t>(b);
        vis->vertexBindingDescriptions[b].stride =
            (b == 2) ? sizeof(vsg::vec2) : sizeof(vsg::vec3);
        vis->vertexBindingDescriptions[b].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    }

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE;

    auto bs = vsg::ColorBlendState::create();
    bs->attachments.resize(1);
    bs->attachments[0].blendEnable = blending ? VK_TRUE : VK_FALSE;
    if (blending) {
        bs->attachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        bs->attachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        bs->attachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        bs->attachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        bs->attachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        bs->attachments[0].alphaBlendOp = VK_BLEND_OP_ADD;
        bs->attachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                           VK_COLOR_COMPONENT_G_BIT |
                                           VK_COLOR_COMPONENT_B_BIT |
                                           VK_COLOR_COMPONENT_A_BIT;
    }

    auto ds = vsg::DepthStencilState::create();
    ds->depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    ds->depthWriteEnable = VK_FALSE;  // 云/海面半透明, 不写深度
    // 本 vsg 是反向 Z 约定 (perspective() 为 1→0 反向深度矩阵, 默认
    // DepthStencilState 即 GREATER, RenderGraph 默认清屏深度 0.0)。
    // 深度测试开启时必须用 GREATER_OR_EQUAL: 地形(写深度, GREATER)先画,
    // 半透明环境片元只有 d >= 地形深度才可见 (更近)。
    ds->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

    // 本 vsg 下管线不含 ViewportState 时 vkCreateGraphicsPipelines 视口为空,
    // 所有绘制被裁成零像素 (S0 根因, 与 rocky 地形管线同病同修)。
    // 静态 ViewportState: 尺寸 ROCKY_VIEWPORT=WxH (默认 1920x1080)。
    auto vs = vsg::ViewportState::create();
    {
        uint32_t vw = 1920, vh = 1080;
        if (const char* s = std::getenv("ROCKY_VIEWPORT"))
        {
            unsigned w = 0, h = 0;
            if (std::sscanf(s, "%ux%u", &w, &h) == 2 && w > 0 && h > 0)
                vw = w, vh = h;
        }
        vs->set(0, 0, vw, vh);
    }

    auto ub = vsg::floatArray::create(kEnvUboFloats);
    ub->properties.dataVariance = vsg::DYNAMIC_DATA;
    for (unsigned i = 0; i < kEnvUboFloats; ++i) (*ub)[i] = 0.0f;

    auto dsl = vsg::DescriptorSetLayout::create();
    dsl->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // set=1 可混合 LUT / FFT 纹理和海浪参数 UBO；布局必须与描述符实际
    // binding、类型和数组长度一致，不能把稀疏 binding 的 UBO 当成采样器。
    vsg::ref_ptr<vsg::DescriptorSetLayout> dsl1;
    if (extra_set1 && !extra_set1->empty()) {
        dsl1 = vsg::DescriptorSetLayout::create();
        for (const auto& descriptor : *extra_set1) {
            if (!descriptor) return {};
            dsl1->addBinding(descriptor->dstBinding, descriptor->descriptorType,
                             descriptor->getNumDescriptors(), extra_set1_stage);
        }
    }

    vsg::DescriptorSetLayouts layouts{dsl};
    if (dsl1) layouts.push_back(dsl1);
    auto layout = vsg::PipelineLayout::create(layouts, vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT,0,128}});

    vsg::GraphicsPipelineStates gp_states{vis, ias, rs, bs, ds, vs};
    auto gp = vsg::GraphicsPipeline::create(layout.get(), vsg::ShaderStages{vert_stage, frag_stage}, gp_states);

    auto bind_desc = vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, layout.get(), 0,
        vsg::Descriptors{vsg::DescriptorBuffer::create(ub, 0)});

    auto sg = vsg::StateGroup::create();
    sg->add(vsg::BindGraphicsPipeline::create(gp));
    sg->add(bind_desc);

    // P3 扩展: set=1 采样纹理组 (FFT 位移/法线泡沫)。out_bind_set1 回传供
    // 调用方在设备上下文就绪后替换为真实 ImageView (预编译期先占位)。
    if (dsl1 && extra_set1) {
        auto bind_desc1 = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, layout.get(), 1, *extra_set1);
        sg->add(bind_desc1);
        if (out_bind_set1) *out_bind_set1 = bind_desc1;
    }

    out_ub = ub;
    return sg;
}

void fillEnvUBO(vsg::floatArray& ub, const EnvFrame& f, const float misc[4],
                const float* extra, const vsg::vec3* deep, const vsg::vec3* shallow,
                const float* opt2)
{
    auto set_mat4 = [&](unsigned off, const vsg::dmat4& m) {
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r)
                ub[off + c * 4 + r] = static_cast<float>(m[c][r]);
    };
    set_mat4(0, f.view_proj);
    set_mat4(16, f.model);
    ub[32] = static_cast<float>(f.cam_pos_enu.x);
    ub[33] = static_cast<float>(f.cam_pos_enu.y);
    ub[34] = static_cast<float>(f.cam_pos_enu.z);
    ub[35] = static_cast<float>(f.time_sec);
    ub[36] = f.sun_dir_enu.x;
    ub[37] = f.sun_dir_enu.y;
    ub[38] = f.sun_dir_enu.z;
    ub[39] = f.sun_el_deg;
    ub[40] = f.wind_dir_enu.x;
    ub[41] = f.wind_dir_enu.z;   // 风取水平 xz 分量
    ub[42] = f.wind_speed;
    ub[43] = f.cloud_cover;
    ub[44] = misc[0];
    ub[45] = misc[1];
    ub[46] = misc[2];
    ub[47] = misc[3];
    if (extra) {
        for (unsigned i = 0; i < 8; ++i) ub[48 + i] = extra[i];
    }
    // offset 56..59: 大气/能见度 (所有模块共用; 此前能见度在 shader 内写死 10km)。
    // P1-2: 添加消光系数供着色器使用
    ub[56] = f.visibility;
    ub[57] = f.precipitation;  // 降水强度
    ub[58] = f.cloud_base_m;   // O2: 有效云底高度 (ocean.frag 云影投影几何用)
    ub[59] = f.detailWeight;             // 保留
    // offset 60..67: 海洋深浅水色 (此前 OceanParams 颜色为死参数)。
    if (deep) {
        ub[60] = deep->x; ub[61] = deep->y; ub[62] = deep->z; ub[63] = 0.0f;
    }
    if (shallow) {
        ub[64] = shallow->x; ub[65] = shallow->y; ub[66] = shallow->z; ub[67] = 0.0f;
    }
    // offset 68..83: 天空大气光照 (rocky_sky 的 AtmosphereState 真值; 云/海
    // shader 据此照明与上雾, 暮光/夜间与天空颜色严格一致)。
    ub[68] = f.sun_light.x; ub[69] = f.sun_light.y; ub[70] = f.sun_light.z;
    ub[71] = f.is_day;
    ub[72] = f.ambient_light.x; ub[73] = f.ambient_light.y; ub[74] = f.ambient_light.z;
    ub[75] = f.detailRadius;
    ub[76] = f.horizon_color.x; ub[77] = f.horizon_color.y; ub[78] = f.horizon_color.z;
    ub[79] = 0.0f;
    ub[80] = f.zenith_color.x; ub[81] = f.zenith_color.y; ub[82] = f.zenith_color.z;
    ub[83] = 0.0f;
    // offset 84..87: 月亮 (xyz=方向 ENU 归一化, w=月相 0朔..1望, <0=无星历)。
    // 海面月光反射带/云月面照明消费; SkyModule 的穹顶 UBO 亦用同区间 (onFrame
    // 里由 backend 真值覆写)。
    ub[84] = f.moon_dir_enu.x; ub[85] = f.moon_dir_enu.y;
    ub[86] = f.moon_dir_enu.z; ub[87] = f.moon_phase;
    // offset 160..163: oceanOpt2 (P3 GPU FFT 平铺参数; 云/非 FFT 海洋保持 0
    // → ocean.vert 走 Gerstner 谐波分支, 向后兼容)。
    if (opt2) {
        ub[160] = opt2[0]; ub[161] = opt2[1]; ub[162] = opt2[2]; ub[163] = opt2[3];
    }
    ub.dirty();
}

vsg::ref_ptr<vsg::StateGroup> buildSkyLUTPipeline(const std::string& vert_name,
                                                   const std::string& frag_name,
                                                   bool blending, bool depth_test,
                                                   vsg::ref_ptr<vsg::floatArray>& out_ub,
                                                   const vsg::Descriptors& lut_descriptors)
{
    if(lut_descriptors.size()!=5 || std::any_of(lut_descriptors.begin(),lut_descriptors.end(),[](const auto& d){return !d;})) {
        std::fprintf(stderr,"[rocky_env] sky LUT pipeline requires atmosphere, Moon and star textures\n");
        return {};
    }
    // Keep the same separate-array vertex layout and reverse-Z state as every
    // other environment pipeline. Only its set=1 fragment samplers differ.
    return buildEnvPipeline(vert_name,frag_name,blending,depth_test,out_ub,
                            &lut_descriptors,VK_SHADER_STAGE_FRAGMENT_BIT);
}

} // namespace rockyenv
