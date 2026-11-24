/// @file ViewportKit.cpp
/// @brief 3D 视口共享组件库实现 (见 ViewportKit.h)。
#include "ViewportKit.h"

#include <vsg/io/read.h>
#include <vsg/lighting/AmbientLight.h>
#include <vsg/lighting/DirectionalLight.h>
#include <vsg/maths/transform.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/utils/Builder.h>
#include <vsg/io/read.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/DescriptorBuffer.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/DescriptorSetLayout.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/PushConstants.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/app/Camera.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/app/RenderGraph.h>

#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "vsgraysim/core/ObjLoader.h"

namespace vsgraysim { namespace gui { namespace ViewportKit {

// ---------------------------------------------------------------------------
// Lights / paths / models
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::Group> createDefaultLights()
{
    auto grp = vsg::Group::create();
    auto ambient = vsg::AmbientLight::create();
    ambient->color = vsg::vec3(1.0f, 1.0f, 1.0f);
    ambient->intensity = 0.35f;
    auto sun = vsg::DirectionalLight::create();
    sun->color = vsg::vec3(1.0f, 1.0f, 1.0f);
    sun->intensity = 1.2f;
    sun->direction = vsg::dvec3(-0.5, -1.0, -0.35);
    grp->addChild(ambient);
    grp->addChild(sun);
    return grp;
}

std::string resolveAssetPath(const std::string& p)
{
    namespace fs = std::filesystem;
    if (p.empty() || fs::exists(p)) return p;
    const fs::path exe = QCoreApplication::applicationDirPath().toStdString();
    for (const fs::path& base : {exe, exe.parent_path()}) {
        const fs::path c = base / p;
        if (fs::exists(c)) return c.string();
    }
    return p;
}

vsg::ref_ptr<vsg::Node> loadModelNode(const std::string& path,
                                      vsg::ref_ptr<vsg::Options> options)
{
    namespace fs = std::filesystem;
    const fs::path p(path);
    if (path.empty() || !fs::exists(p) || fs::file_size(p) == 0) {
        spdlog::error("ViewportKit::loadModelNode: missing/empty '{}'", path);
        return {};
    }
    auto node = vsg::read_cast<vsg::Node>(path, options);
    if (node) return node;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".obj") {
        node = vsgraysim::loadObjNode(path, options);
        if (node) return node;
    }
    spdlog::error("ViewportKit::loadModelNode: failed '{}'", path);
    return {};
}

// ---------------------------------------------------------------------------
// Line segments (LINE_LIST via the proven Builder pipeline)
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeLineSegments(
    vsg::ref_ptr<vsg::vec3Array> verts, const QColor& color,
    const LineStyle& style)
{
    if (!verts || verts->size() < 2) return {};

    // 2x2 纯色纹理驱动线颜色 (走标准带纹理管线, 与笔刷光标环相同做法)。
    auto tex = vsg::ubvec4Array2D::create(
        2, 2, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    const uint8_t cr = uint8_t(color.red());
    const uint8_t cg = uint8_t(color.green());
    const uint8_t cb = uint8_t(color.blue());
    for (uint32_t ty = 0; ty < 2; ++ty)
        for (uint32_t tx = 0; tx < 2; ++tx)
            tex->set(tx, ty, vsg::ubvec4(cr, cg, cb, 255));

    vsg::StateInfo si;
    si.lighting = true;
    si.image = tex;
    auto builder = vsg::Builder::create();
    auto sg = builder->createStateGroup(si);

    // 把拓扑改成 LINE_LIST (Builder 默认三角形), 顶层与嵌套管线都要补丁,
    // 否则几何会渲染成盖住场景的黑色三角面。
    auto patchStates = [&style](const auto& cmds) {
        for (auto& sc : cmds) {
            if (auto ias = sc.template cast<vsg::InputAssemblyState>())
                ias->topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            if (auto rs = sc.template cast<vsg::RasterizationState>()) {
                rs->cullMode = VK_CULL_MODE_NONE;
                rs->lineWidth = style.width;
                if (style.depth_bias) {
                    // 向观察者偏移, 避免与地形/模型深度冲突。
                    rs->depthBiasEnable = VK_TRUE;
                    rs->depthBiasConstantFactor = -1.5f;
                    rs->depthBiasSlopeFactor = -1.5f;
                }
            }
        }
    };
    patchStates(sg->stateCommands);
    for (auto& sc : sg->stateCommands) {
        if (auto bgp = sc.cast<vsg::BindGraphicsPipeline>())
            patchStates(bgp->pipeline->pipelineStates);
    }

    // Arrays: pos + normal + uv (与笔刷光标环相同的顶点布局)。
    auto nrm = vsg::vec3Array::create(verts->size());
    auto uv  = vsg::vec2Array::create(verts->size());
    for (size_t i = 0; i < verts->size(); ++i) {
        (*nrm)[i] = vsg::vec3(0.0f, 1.0f, 0.0f);
        (*uv)[i]  = vsg::vec2(0.5f, 0.5f);
    }

    // Indices: 0,1, 2,3, ... (每对端点一条线段)。
    auto idx = vsg::uintArray::create(verts->size());
    for (size_t i = 0; i < verts->size(); ++i)
        (*idx)[i] = static_cast<uint32_t>(i);

    auto vid = vsg::VertexIndexDraw::create();
    vid->assignArrays({verts, nrm, uv});
    vid->assignIndices(idx);
    vid->indexCount = static_cast<uint32_t>(verts->size());
    vid->instanceCount = 1;

    sg->addChild(vid);
    return sg;
}

vsg::ref_ptr<vsg::StateGroup> makeLineSegments(
    const std::vector<vsg::vec3>& seg_pts, const QColor& color,
    const LineStyle& style)
{
    if (seg_pts.size() < 2 || seg_pts.size() % 2 != 0) return {};
    auto verts = vsg::vec3Array::create(seg_pts.size());
    for (size_t i = 0; i < seg_pts.size(); ++i)
        (*verts)[i] = seg_pts[i];
    return makeLineSegments(verts, color, style);
}

// ---------------------------------------------------------------------------
// Blended (translucent) meshes: TRIANGLE_LIST + alpha gradient texture
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeBlendedMesh(
    const std::vector<vsg::vec3>& tri_pts,
    const std::vector<vsg::vec2>& uvs,
    const QColor& color, float alpha_center, float alpha_edge,
    const LineStyle& style)
{
    if (tri_pts.size() < 3 || tri_pts.size() != uvs.size()) return {};

    // 64x1 渐变纹理: uv.x=0 处 alpha_center, uv.x=1 处 alpha_edge。
    constexpr uint32_t kGradSteps = 64;
    auto tex = vsg::ubvec4Array2D::create(
        kGradSteps, 1, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    for (uint32_t x = 0; x < kGradSteps; ++x) {
        const float t = float(x) / float(kGradSteps - 1);
        const float a = alpha_center + (alpha_edge - alpha_center) * t;
        tex->set(x, 0, vsg::ubvec4(uint8_t(color.red()), uint8_t(color.green()),
                                   uint8_t(color.blue()),
                                   uint8_t(std::clamp(a, 0.0f, 1.0f) * 255.0f)));
    }

    vsg::StateInfo si;
    si.lighting  = true;
    si.image     = tex;
    si.blending  = true;          // 半透明混合管线 (区别于线段的写法)
    auto builder = vsg::Builder::create();
    auto sg      = builder->createStateGroup(si);

    // 保持 TRIANGLE_LIST; 关闭背面剔除 (覆盖面正反视均可见), 可选深度偏移。
    auto patchStates = [&style](const auto& cmds) {
        for (auto& sc : cmds) {
            if (auto rs = sc.template cast<vsg::RasterizationState>()) {
                rs->cullMode = VK_CULL_MODE_NONE;
                if (style.depth_bias) {
                    rs->depthBiasEnable = VK_TRUE;
                    rs->depthBiasConstantFactor = -1.5f;
                    rs->depthBiasSlopeFactor = -1.5f;
                }
            }
        }
    };
    patchStates(sg->stateCommands);
    for (auto& sc : sg->stateCommands) {
        if (auto bgp = sc.cast<vsg::BindGraphicsPipeline>())
            patchStates(bgp->pipeline->pipelineStates);
    }

    auto verts = vsg::vec3Array::create(tri_pts.size());
    auto uv    = vsg::vec2Array::create(uvs.size());
    auto nrm   = vsg::vec3Array::create(tri_pts.size());
    for (size_t i = 0; i < tri_pts.size(); ++i) {
        (*verts)[i] = tri_pts[i];
        (*uv)[i]    = uvs[i];
        (*nrm)[i]   = vsg::vec3(0.0f, 1.0f, 0.0f);
    }
    auto idx = vsg::uintArray::create(tri_pts.size());
    for (size_t i = 0; i < tri_pts.size(); ++i)
        (*idx)[i] = static_cast<uint32_t>(i);

    auto vid = vsg::VertexIndexDraw::create();
    vid->assignArrays({verts, nrm, uv});
    vid->assignIndices(idx);
    vid->indexCount    = static_cast<uint32_t>(tri_pts.size());
    vid->instanceCount = 1;
    sg->addChild(vid);
    return sg;
}

vsg::ref_ptr<vsg::StateGroup> makeFilledDisc(
    const vsg::vec3& center, double radius, const QColor& color,
    float alpha_center, float alpha_edge, int segs)
{
    if (radius <= 0.0 || segs < 3) return {};
    std::vector<vsg::vec3> pts;
    std::vector<vsg::vec2> uvs;
    pts.reserve(size_t(segs) * 3);
    uvs.reserve(size_t(segs) * 3);
    for (int i = 0; i < segs; ++i) {
        const double a0 = 2.0 * M_PI * double(i) / double(segs);
        const double a1 = 2.0 * M_PI * double(i + 1) / double(segs);
        pts.push_back(center);                                    uvs.push_back({0.0f, 0.5f});
        pts.push_back(center + vsg::vec3(float(radius * std::cos(a0)), 0.0f,
                                         float(radius * std::sin(a0)))); uvs.push_back({1.0f, 0.5f});
        pts.push_back(center + vsg::vec3(float(radius * std::cos(a1)), 0.0f,
                                         float(radius * std::sin(a1)))); uvs.push_back({1.0f, 0.5f});
    }
    return makeBlendedMesh(pts, uvs, color, alpha_center, alpha_edge,
                           {/*width*/1.0f, /*depth_bias*/true});
}

vsg::ref_ptr<vsg::StateGroup> makeFilledSector(
    const vsg::vec3& center, double radius, double az_center_deg,
    double fov_h_deg, const QColor& color,
    float alpha_center, float alpha_edge, int segs)
{
    if (radius <= 0.0 || segs < 2) return {};
    const double az  = vsg::radians(az_center_deg);
    const double a0  = az - 0.5 * vsg::radians(fov_h_deg);
    const double a1  = az + 0.5 * vsg::radians(fov_h_deg);
    std::vector<vsg::vec3> pts;
    std::vector<vsg::vec2> uvs;
    pts.reserve(size_t(segs) * 3);
    uvs.reserve(size_t(segs) * 3);
    // 方位从局部北(-Z)顺时针，与测量孔径方向一致。
    auto dirAt = [&](double a) {
        return vsg::vec3(float(std::sin(a)), 0.0f, -float(std::cos(a)));
    };
    for (int i = 0; i < segs; ++i) {
        const double am = a0 + (a1 - a0) * double(i) / double(segs);
        const double an = a0 + (a1 - a0) * double(i + 1) / double(segs);
        pts.push_back(center); uvs.push_back({0.0f, 0.5f});
        pts.push_back(center + dirAt(am) * float(radius)); uvs.push_back({1.0f, 0.5f});
        pts.push_back(center + dirAt(an) * float(radius)); uvs.push_back({1.0f, 0.5f});
    }
    return makeBlendedMesh(pts, uvs, color, alpha_center, alpha_edge,
                           {/*width*/1.0f, /*depth_bias*/true});
}

vsg::ref_ptr<vsg::StateGroup> makeLightColumn(
    const vsg::vec3& center, double height, double half_width,
    const QColor& color, float alpha_bottom, float alpha_top)
{
    if (height <= 0.0 || half_width <= 0.0) return {};
    const float x = center.x, y = center.y, z = center.z;
    const float w = float(half_width), h = float(height);
    std::vector<vsg::vec3> pts;
    std::vector<vsg::vec2> uvs;
    auto quad = [&](const vsg::vec3& b0, const vsg::vec3& b1) {
        // 两三角 (底0 底1 顶1 / 底0 顶1 顶0), uv.x = 高度分数 (渐变)。
        auto push = [&](const vsg::vec3& p, float hf) {
            pts.push_back(p);
            uvs.push_back({hf, 0.5f});
        };
        const vsg::vec3 t0(b0.x, y + h, b0.z), t1(b1.x, y + h, b1.z);
        push(b0, 0.0f); push(b1, 0.0f); push(t1, 1.0f);
        push(b0, 0.0f); push(t1, 1.0f); push(t0, 1.0f);
    };
    quad({x - w, y, z}, {x + w, y, z});   // X 向面片
    quad({x, y, z - w}, {x, y, z + w});   // Z 向面片 (正交)
    return makeBlendedMesh(pts, uvs, color, alpha_bottom, alpha_top,
                           {/*width*/1.0f, /*depth_bias*/false});
}

vsg::ref_ptr<vsg::StateGroup> makeFrustumWalls(
    const vsg::dvec3& apex, const vsg::dvec3& fwd,
    const vsg::dvec3& right, const vsg::dvec3& up,
    double length, double fov_h_deg, double fov_v_deg,
    const QColor& color, float alpha_apex, float alpha_far)
{
    if (length <= 0.0) return {};
    const vsg::dvec3 fc = apex + fwd * length;                    // 远端中心
    const double hw = length * std::tan(vsg::radians(0.5 * fov_h_deg));
    const double hh = length * std::tan(vsg::radians(0.5 * fov_v_deg));
    const vsg::dvec3 c[4] = {
        fc - right * hw - up * hh, fc + right * hw - up * hh,
        fc + right * hw + up * hh, fc - right * hw + up * hh,
    };
    std::vector<vsg::vec3> pts;
    std::vector<vsg::vec2> uvs;
    auto quad = [&](const vsg::dvec3& f1, const vsg::dvec3& f2) { // 侧壁四边形
        auto push = [&](const vsg::dvec3& p, float d) {
            pts.push_back(vsg::vec3(float(p.x), float(p.y), float(p.z)));
            uvs.push_back({d, 0.5f});
        };
        push(apex, 0.0f); push(f1, 1.0f); push(f2, 1.0f);
    };
    quad(c[0], c[1]);   // 底
    quad(c[1], c[2]);   // 右
    quad(c[2], c[3]);   // 顶
    quad(c[3], c[0]);   // 左
    return makeBlendedMesh(pts, uvs, color, alpha_apex, alpha_far,
                           {/*width*/1.0f, /*depth_bias*/false});
}

// ---------------------------------------------------------------------------
// Sensor frustum wireframes
// ---------------------------------------------------------------------------
void frustumBasis(const vsg::dvec3& dir, vsg::dvec3& right, vsg::dvec3& up)
{
    const vsg::dvec3 f = vsg::normalize(dir);
    const vsg::dvec3 up_ref = (std::abs(f.y) > 0.95) ? vsg::dvec3(1, 0, 0)
                                                       : vsg::dvec3(0, 1, 0);
    right = vsg::normalize(vsg::cross(f, up_ref));
    up = vsg::cross(right, f);
}

std::vector<vsg::vec3> frustumSegments(const vsg::dvec3& origin,
                                       const vsg::dvec3& fwd_in,
                                       const vsg::dvec3& right_in,
                                       const vsg::dvec3& up_in,
                                       double fov_h_deg, double fov_v_deg,
                                       double length,
                                       bool mid_plane, bool axis_line)
{
    const vsg::dvec3 f = vsg::normalize(fwd_in);
    const vsg::dvec3 r = vsg::normalize(right_in);
    const vsg::dvec3 u = vsg::normalize(up_in);
    const double hh = std::tan(vsg::radians(fov_h_deg) * 0.5);
    const double hv = std::tan(vsg::radians(fov_v_deg) * 0.5);

    auto corner = [&](double d, double sx, double sy) {
        return origin + f * d + r * (sx * hh * d) + u * (sy * hv * d);
    };
    const vsg::dvec3 c1 = corner(length, +1.0, +1.0);
    const vsg::dvec3 c2 = corner(length, -1.0, +1.0);
    const vsg::dvec3 c3 = corner(length, -1.0, -1.0);
    const vsg::dvec3 c4 = corner(length, +1.0, -1.0);

    std::vector<vsg::vec3> pts;
    pts.reserve(34);
    auto seg = [&](const vsg::dvec3& a, const vsg::dvec3& b) {
        pts.push_back(vsg::vec3(a));
        pts.push_back(vsg::vec3(b));
    };
    seg(origin, c1); seg(origin, c2); seg(origin, c3); seg(origin, c4);
    if (mid_plane) {
        const vsg::dvec3 m1 = corner(length * 0.5, +1.0, +1.0);
        const vsg::dvec3 m2 = corner(length * 0.5, -1.0, +1.0);
        const vsg::dvec3 m3 = corner(length * 0.5, -1.0, -1.0);
        const vsg::dvec3 m4 = corner(length * 0.5, +1.0, -1.0);
        seg(m1, m2); seg(m2, m3); seg(m3, m4); seg(m4, m1);
    }
    seg(c1, c2); seg(c2, c3); seg(c3, c4); seg(c4, c1);   // 远端矩形
    if (axis_line) seg(origin, origin + f * length);        // 视轴
    return pts;
}

// ---------------------------------------------------------------------------
// Transforms / colours
// ---------------------------------------------------------------------------
vsg::dmat4 makeEntityTransform(const vsg::dvec3& pos, double yaw_deg,
                               double scale)
{
    vsg::dmat4 m(1.0);
    if (scale != 1.0) m = vsg::scale(scale, scale, scale);
    m = vsg::rotate(vsg::radians(-yaw_deg), vsg::dvec3(0.0, 1.0, 0.0)) * m;
    m = vsg::translate(pos.x, pos.y, pos.z) * m;
    return m;
}

QColor sensorTypeColor(const std::string& type)
{
    if (type == "ir")         return QColor(255, 140, 84);
    if (type == "laser")      return QColor(120, 255, 140);
    if (type == "sar")        return QColor(255, 220, 100);
    if (type == "vis")        return QColor(210, 214, 222);
    return QColor(127, 208, 255);   // radar 及未知类型
}

// ---------------------------------------------------------------------------
// Custom shader pipeline (from shaders/viewport/ GLSL files)
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> createShaderPipeline(
    const std::string& vert_name,
    const std::string& frag_name,
    bool blending,
    bool depth_test,
    ShaderHandle* out)
{
    // Load GLSL source files from shaders/viewport/
    const std::string dir = resolveAssetPath("shaders/viewport");
    const std::string vpath = dir + "/" + vert_name;
    const std::string fpath = dir + "/" + frag_name;

    auto vert_stage = vsg::ShaderStage::read(VK_SHADER_STAGE_VERTEX_BIT,
                                             "main", vpath);
    auto frag_stage = vsg::ShaderStage::read(VK_SHADER_STAGE_FRAGMENT_BIT,
                                             "main", fpath);
    if (!vert_stage || !frag_stage) {
        spdlog::warn("ViewportKit::createShaderPipeline: failed to load {}/{}",
                     vert_name, frag_name);
        return {};
    }

    // Compile GLSL → SPIR-V if shader compiler is available (VSG built with
    // glslang support). On failure fall back to null (caller uses blended mesh).
    auto compiler = vsg::ShaderCompiler::create();
    vsg::ShaderStages stages{vert_stage, frag_stage};
    if (!compiler || !compiler->supported() || !compiler->compile(stages)) {
        spdlog::warn("ViewportKit::createShaderPipeline: shader compile failed "
                     "for {}/{}", vert_name, frag_name);
        return {};
    }

    // Build vertex input state matching our standard layout:
    // location 0: vec3 pos, location 1: vec3 normal, location 2: vec2 uv
    auto vis = vsg::VertexInputState::create();
    vis->vertexAttributeDescriptions.resize(3);
    vis->vertexAttributeDescriptions[0].location = 0;
    vis->vertexAttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[0].offset = 0;
    vis->vertexAttributeDescriptions[1].location = 1;
    vis->vertexAttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vis->vertexAttributeDescriptions[1].offset = sizeof(vsg::vec3);
    vis->vertexAttributeDescriptions[2].location = 2;
    vis->vertexAttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    vis->vertexAttributeDescriptions[2].offset = 2 * sizeof(vsg::vec3);
    vis->vertexBindingDescriptions.resize(1);
    vis->vertexBindingDescriptions[0].binding = 0;
    vis->vertexBindingDescriptions[0].stride = 3 * sizeof(vsg::vec3) + sizeof(vsg::vec2);
    vis->vertexBindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    auto ias = vsg::InputAssemblyState::create();
    ias->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rs = vsg::RasterizationState::create();
    rs->cullMode = VK_CULL_MODE_NONE;  // 双面渲染

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
    ds->depthWriteEnable = VK_FALSE;  // 半透明物体不写深度
    ds->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // ---- 相机 UBO (set 0, binding 0): mat4 view_proj + vec4(cam_pos, time)
    //      + mat4 model(局部内容根→世界)。共 36 float。
    // 逐帧由 updateShaderNode 改写并标记脏, VSG 自动重新上传。旧 shader 若不
    // 声明 model 字段, 多出的尾部数据被 std140 安全忽略 (buffer 可大于声明)。
    auto camera_ub = vsg::floatArray::create(36);
    camera_ub->properties.dataVariance = vsg::DYNAMIC_DATA;

    auto dsl = vsg::DescriptorSetLayout::create();
    dsl->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    // ---- push-constant: time_sec / param1 / param2 / pad (16 字节) ----
    auto push_pc = vsg::floatArray::create(4);
    push_pc->properties.dataVariance = vsg::DYNAMIC_DATA;

    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = sizeof(ShaderTimePush);

    auto layout = vsg::PipelineLayout::create(
        vsg::DescriptorSetLayouts{dsl},
        vsg::PushConstantRanges{pc_range});

    vsg::GraphicsPipelineStates gp_states{vis, ias, rs, bs, ds};
    auto gp = vsg::GraphicsPipeline::create(layout.get(), stages, gp_states);

    auto bind_desc = vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, layout.get(), 0,
        vsg::Descriptors{vsg::DescriptorBuffer::create(camera_ub, 0)});
    auto push_cmd = vsg::PushConstants::create(
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, push_pc);

    auto sg = vsg::StateGroup::create();
    sg->add(vsg::BindGraphicsPipeline::create(gp));
    sg->add(bind_desc);
    sg->add(push_cmd);

    if (out) { out->camera_ub = camera_ub; out->push_pc = push_pc; }
    return sg;
}

void updateShaderNode(const ShaderHandle& h,
                      const vsg::dmat4& view_proj,
                      const vsg::dvec3& cam_pos,
                      float time_sec, float param1, float param2,
                      const vsg::dmat4& model)
{
    if (!h.valid()) return;
    // 相机 UBO: mat4 view_proj (16 float, 列主序, 与 GLSL std140 一致) +
    //           vec4(cam_pos.xyz, time_sec) + mat4 model(局部→世界)。
    auto& cam = *h.camera_ub;
    for (uint32_t c = 0; c < 4; ++c)
        for (uint32_t r = 0; r < 4; ++r)
            cam[c * 4 + r] = static_cast<float>(view_proj[c][r]);
    cam[16] = static_cast<float>(cam_pos.x);
    cam[17] = static_cast<float>(cam_pos.y);
    cam[18] = static_cast<float>(cam_pos.z);
    cam[19] = time_sec;
    for (uint32_t c = 0; c < 4; ++c)
        for (uint32_t r = 0; r < 4; ++r)
            cam[20 + c * 4 + r] = static_cast<float>(model[c][r]);
    cam.dirty();
    // push-constant: time / param1 / param2 / pad
    auto& pc = *h.push_pc;
    pc[0] = time_sec; pc[1] = param1; pc[2] = param2; pc[3] = 0.0f;
    pc.dirty();
}

void cameraMatrices(const vsg::Camera* cam,
                    vsg::dmat4& view_proj, vsg::dvec3& cam_pos)
{
    view_proj = vsg::dmat4(1.0);
    cam_pos = vsg::dvec3(0.0, 0.0, 0.0);
    if (!cam) return;
    vsg::dmat4 view(1.0), proj(1.0);
    if (cam->viewMatrix) view = cam->viewMatrix->transform();
    if (cam->projectionMatrix) proj = cam->projectionMatrix->transform();
    view_proj = proj * view;
    // 相机世界位置 = inverse(view) 的平移分量 (第 3 列)。
    const vsg::dmat4 inv = vsg::inverse(view);
    cam_pos = vsg::dvec3(inv[3][0], inv[3][1], inv[3][2]);
}

// ---------------------------------------------------------------------------
// makeHemisphere: 3D hemispherical volume mesh (detection range)
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeHemisphere(
    const vsg::dvec3& center, double max_radius,
    double az_min_deg, double az_max_deg,
    double el_min_deg, double el_max_deg,
    int az_segs, int el_segs,
    const QColor& color,
    const std::vector<float>* occlusion,
    float alpha_inner, float alpha_edge,
    ShaderHandle* out)
{
    if (max_radius <= 0.0 || az_segs < 2 || el_segs < 2) return {};

    // Build triangular mesh: for each (el, az) cell, two triangles
    // Surface radius per direction = occlusion distance or max_radius
    auto radiusAt = [&](int ei, int ai) -> double {
        if (occlusion && !occlusion->empty() &&
            ei < el_segs && ai < az_segs) {
            const size_t idx = static_cast<size_t>(ei) * az_segs + ai;
            if (idx < occlusion->size())
                return static_cast<double>((*occlusion)[idx]);
        }
        return max_radius;
    };

    auto dirAt = [&](double az_deg, double el_deg) -> vsg::dvec3 {
        const double azr = vsg::radians(az_deg);
        const double elr = vsg::radians(el_deg);
        const double ce = std::cos(elr), se = std::sin(elr);
        return {std::sin(azr) * ce, se, -std::cos(azr) * ce};
    };

    std::vector<vsg::vec3> verts;
    std::vector<vsg::vec2> uvs;
    verts.reserve(static_cast<size_t>(az_segs) * el_segs * 6);
    uvs.reserve(static_cast<size_t>(az_segs) * el_segs * 6);

    for (int ei = 0; ei < el_segs - 1; ++ei) {
        const double el0 = el_min_deg + (el_max_deg - el_min_deg) *
                          double(ei) / double(el_segs - 1);
        const double el1 = el_min_deg + (el_max_deg - el_min_deg) *
                          double(ei + 1) / double(el_segs - 1);
        for (int ai = 0; ai < az_segs - 1; ++ai) {
            const double az0 = az_min_deg + (az_max_deg - az_min_deg) *
                              double(ai) / double(az_segs - 1);
            const double az1 = az_min_deg + (az_max_deg - az_min_deg) *
                              double(ai + 1) / double(az_segs - 1);

            const double r00 = radiusAt(ei, ai);
            const double r01 = radiusAt(ei, ai + 1);
            const double r10 = radiusAt(ei + 1, ai);
            const double r11 = radiusAt(ei + 1, ai + 1);

            const vsg::dvec3 d00 = dirAt(az0, el0);
            const vsg::dvec3 d01 = dirAt(az1, el0);
            const vsg::dvec3 d10 = dirAt(az0, el1);
            const vsg::dvec3 d11 = dirAt(az1, el1);

            const vsg::vec3 p00(d00 * r00 + center);
            const vsg::vec3 p01(d01 * r01 + center);
            const vsg::vec3 p10(d10 * r10 + center);
            const vsg::vec3 p11(d11 * r11 + center);

            const float u0 = float(ai) / float(az_segs - 1);
            const float u1 = float(ai + 1) / float(az_segs - 1);
            const float v0 = float(ei) / float(el_segs - 1);
            const float v1 = float(ei + 1) / float(el_segs - 1);

            // Triangle 1: p00, p01, p11
            verts.push_back(p00); uvs.push_back({u0, v0});
            verts.push_back(p01); uvs.push_back({u1, v0});
            verts.push_back(p11); uvs.push_back({u1, v1});
            // Triangle 2: p00, p11, p10
            verts.push_back(p00); uvs.push_back({u0, v0});
            verts.push_back(p11); uvs.push_back({u1, v1});
            verts.push_back(p10); uvs.push_back({u0, v1});
        }
    }

    // Try custom shader pipeline, fall back to blended mesh
    auto sg = createShaderPipeline("detection_volume.vert",
                                    "detection_volume.frag", true, true, out);
    if (sg) {
        auto verts_arr = vsg::vec3Array::create(verts.size());
        auto nrm_arr = vsg::vec3Array::create(verts.size());
        auto uv_arr = vsg::vec2Array::create(uvs.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            (*verts_arr)[i] = verts[i];
            (*nrm_arr)[i] = vsg::vec3(0.0f, 1.0f, 0.0f);
            (*uv_arr)[i] = uvs[i];
        }
        auto idx = vsg::uintArray::create(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            (*idx)[i] = static_cast<uint32_t>(i);
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({verts_arr, nrm_arr, uv_arr});
        vid->assignIndices(idx);
        vid->indexCount = static_cast<uint32_t>(verts.size());
        vid->instanceCount = 1;
        sg->addChild(vid);
        return sg;
    }

    // Fallback: use makeBlendedMesh
    return makeBlendedMesh(verts, uvs, color, alpha_inner, alpha_edge,
                           {1.0f, false});
}

// ---------------------------------------------------------------------------
// makeCommTube: semi-transparent tube between two points
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeCommTube(
    const vsg::vec3& from, const vsg::vec3& to,
    float radius, int segs, int radial_segs,
    const QColor& color, float alpha, ShaderHandle* out)
{
    if (radius <= 0.0f || segs < 2 || radial_segs < 3) return {};

    const vsg::vec3 axis = to - from;
    const float len = vsg::length(axis);
    if (len < 1e-3f) return {};
    const vsg::vec3 dir = axis / len;

    // Build orthonormal basis (dir, right, up)
    vsg::vec3 ref(0.0f, 1.0f, 0.0f);
    if (std::abs(dir.y) > 0.9f) ref = vsg::vec3(1.0f, 0.0f, 0.0f);
    const vsg::vec3 right = vsg::normalize(vsg::cross(dir, ref));
    const vsg::vec3 up = vsg::cross(right, dir);

    std::vector<vsg::vec3> verts;
    std::vector<vsg::vec2> uvs;
    verts.reserve(static_cast<size_t>(segs) * radial_segs * 6);
    uvs.reserve(static_cast<size_t>(segs) * radial_segs * 6);

    for (int si = 0; si < segs - 1; ++si) {
        const float t0 = float(si) / float(segs - 1);
        const float t1 = float(si + 1) / float(segs - 1);
        const vsg::vec3 c0 = from + axis * t0;
        const vsg::vec3 c1 = from + axis * t1;

        for (int ri = 0; ri < radial_segs; ++ri) {
            const float a0 = 2.0f * 3.14159265f * float(ri) / float(radial_segs);
            const float a1 = 2.0f * 3.14159265f * float(ri + 1) / float(radial_segs);
            const vsg::vec3 off0 = right * (std::cos(a0) * radius) +
                                   up * (std::sin(a0) * radius);
            const vsg::vec3 off1 = right * (std::cos(a1) * radius) +
                                   up * (std::sin(a1) * radius);

            const vsg::vec3 p00 = c0 + off0;
            const vsg::vec3 p01 = c0 + off1;
            const vsg::vec3 p10 = c1 + off0;
            const vsg::vec3 p11 = c1 + off1;

            const float v0 = float(ri) / float(radial_segs);
            const float v1 = float(ri + 1) / float(radial_segs);

            // Triangle 1
            verts.push_back(p00); uvs.push_back({t0, v0});
            verts.push_back(p01); uvs.push_back({t0, v1});
            verts.push_back(p11); uvs.push_back({t1, v1});
            // Triangle 2
            verts.push_back(p00); uvs.push_back({t0, v0});
            verts.push_back(p11); uvs.push_back({t1, v1});
            verts.push_back(p10); uvs.push_back({t1, v0});
        }
    }

    // Try custom shader, fallback to blended mesh
    auto sg = createShaderPipeline("comm_tube.vert", "comm_tube.frag", true, true, out);
    if (sg) {
        auto verts_arr = vsg::vec3Array::create(verts.size());
        auto nrm_arr = vsg::vec3Array::create(verts.size());
        auto uv_arr = vsg::vec2Array::create(uvs.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            (*verts_arr)[i] = verts[i];
            (*nrm_arr)[i] = vsg::vec3(0.0f, 1.0f, 0.0f);
            (*uv_arr)[i] = uvs[i];
        }
        auto idx = vsg::uintArray::create(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            (*idx)[i] = static_cast<uint32_t>(i);
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({verts_arr, nrm_arr, uv_arr});
        vid->assignIndices(idx);
        vid->indexCount = static_cast<uint32_t>(verts.size());
        vid->instanceCount = 1;
        sg->addChild(vid);
        return sg;
    }

    // Fallback: blended mesh (no animation)
    return makeBlendedMesh(verts, uvs, color, alpha, alpha * 0.2f,
                           {1.0f, false});
}

// ---------------------------------------------------------------------------
// makeTrailRibbon: horizontal motion trail ribbon
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeTrailRibbon(
    const std::vector<vsg::vec3>& points,
    float width, const QColor& color, ShaderHandle* out)
{
    if (points.size() < 2 || width <= 0.0f) return {};

    const float hw = width * 0.5f;
    const size_t n = points.size();

    std::vector<vsg::vec3> verts;
    std::vector<vsg::vec2> uvs;
    verts.reserve(n * 2 * 3);  // worst case
    uvs.reserve(n * 2 * 3);

    for (size_t i = 0; i < n - 1; ++i) {
        const vsg::vec3& p0 = points[i];
        const vsg::vec3& p1 = points[i + 1];
        const vsg::vec3 d = p1 - p0;
        const float len = std::sqrt(d.x * d.x + d.z * d.z);
        if (len < 1e-3f) continue;
        // Perpendicular in XZ plane
        const float ux = -d.z / len;
        const float uz = d.x / len;

        const float t0 = float(i) / float(n - 1);
        const float t1 = float(i + 1) / float(n - 1);

        const vsg::vec3 l0(p0.x - ux * hw, p0.y, p0.z - uz * hw);
        const vsg::vec3 r0(p0.x + ux * hw, p0.y, p0.z + uz * hw);
        const vsg::vec3 l1(p1.x - ux * hw, p1.y, p1.z - uz * hw);
        const vsg::vec3 r1(p1.x + ux * hw, p1.y, p1.z + uz * hw);

        // Triangle 1: l0, r0, r1
        verts.push_back(l0); uvs.push_back({t0, 0.0f});
        verts.push_back(r0); uvs.push_back({t0, 1.0f});
        verts.push_back(r1); uvs.push_back({t1, 1.0f});
        // Triangle 2: l0, r1, l1
        verts.push_back(l0); uvs.push_back({t0, 0.0f});
        verts.push_back(r1); uvs.push_back({t1, 1.0f});
        verts.push_back(l1); uvs.push_back({t1, 0.0f});
    }

    // Try custom shader, fallback to blended mesh
    auto sg = createShaderPipeline("trail_ribbon.vert", "trail_ribbon.frag", true, true, out);
    if (sg) {
        auto verts_arr = vsg::vec3Array::create(verts.size());
        auto nrm_arr = vsg::vec3Array::create(verts.size());
        auto uv_arr = vsg::vec2Array::create(uvs.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            (*verts_arr)[i] = verts[i];
            (*nrm_arr)[i] = vsg::vec3(0.0f, 1.0f, 0.0f);
            (*uv_arr)[i] = uvs[i];
        }
        auto idx = vsg::uintArray::create(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            (*idx)[i] = static_cast<uint32_t>(i);
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({verts_arr, nrm_arr, uv_arr});
        vid->assignIndices(idx);
        vid->indexCount = static_cast<uint32_t>(verts.size());
        vid->instanceCount = 1;
        sg->addChild(vid);
        return sg;
    }

    // Fallback: blended mesh with alpha gradient
    return makeBlendedMesh(verts, uvs, color, 0.35f, 0.02f, {1.0f, true});
}

// ---------------------------------------------------------------------------
// makeFactionHalo: faction halo dome
// ---------------------------------------------------------------------------
vsg::ref_ptr<vsg::StateGroup> makeFactionHalo(
    const vsg::vec3& center, double radius, double height,
    const QColor& color, ShaderHandle* out)
{
    if (radius <= 0.0 || height <= 0.0) return {};

    constexpr int kSegs = 32;  // azimuth segments
    constexpr int kRings = 8;   // elevation rings

    std::vector<vsg::vec3> verts;
    std::vector<vsg::vec2> uvs;
    verts.reserve(static_cast<size_t>(kSegs) * kRings * 6);
    uvs.reserve(static_cast<size_t>(kSegs) * kRings * 6);

    for (int ri = 0; ri < kRings - 1; ++ri) {
        const float e0 = (float(ri) / float(kRings - 1)) * 3.14159265f * 0.5f;
        const float e1 = (float(ri + 1) / float(kRings - 1)) * 3.14159265f * 0.5f;
        const float r0 = float(radius) * std::cos(e0);
        const float r1 = float(radius) * std::cos(e1);
        const float h0 = float(height) * std::sin(e0);
        const float h1 = float(height) * std::sin(e1);
        const float v0 = float(ri) / float(kRings - 1);
        const float v1 = float(ri + 1) / float(kRings - 1);

        for (int si = 0; si < kSegs; ++si) {
            const float a0 = 2.0f * 3.14159265f * float(si) / float(kSegs);
            const float a1 = 2.0f * 3.14159265f * float(si + 1) / float(kSegs);
            const float u0 = float(si) / float(kSegs);
            const float u1 = float(si + 1) / float(kSegs);

            const vsg::vec3 p00(center.x + r0 * std::cos(a0), center.y + h0,
                                center.z + r0 * std::sin(a0));
            const vsg::vec3 p01(center.x + r0 * std::cos(a1), center.y + h0,
                                center.z + r0 * std::sin(a1));
            const vsg::vec3 p11(center.x + r1 * std::cos(a1), center.y + h1,
                                center.z + r1 * std::sin(a1));
            const vsg::vec3 p10(center.x + r1 * std::cos(a0), center.y + h1,
                                center.z + r1 * std::sin(a0));

            // Triangle 1
            verts.push_back(p00); uvs.push_back({u0, v0});
            verts.push_back(p01); uvs.push_back({u1, v0});
            verts.push_back(p11); uvs.push_back({u1, v1});
            // Triangle 2
            verts.push_back(p00); uvs.push_back({u0, v0});
            verts.push_back(p11); uvs.push_back({u1, v1});
            verts.push_back(p10); uvs.push_back({u0, v1});
        }
    }

    // Try custom shader, fallback to blended mesh
    auto sg = createShaderPipeline("faction_halo.vert", "faction_halo.frag", true, true, out);
    if (sg) {
        auto verts_arr = vsg::vec3Array::create(verts.size());
        auto nrm_arr = vsg::vec3Array::create(verts.size());
        auto uv_arr = vsg::vec2Array::create(uvs.size());
        for (size_t i = 0; i < verts.size(); ++i) {
            (*verts_arr)[i] = verts[i];
            (*nrm_arr)[i] = vsg::vec3(0.0f, 1.0f, 0.0f);
            (*uv_arr)[i] = uvs[i];
        }
        auto idx = vsg::uintArray::create(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            (*idx)[i] = static_cast<uint32_t>(i);
        auto vid = vsg::VertexIndexDraw::create();
        vid->assignArrays({verts_arr, nrm_arr, uv_arr});
        vid->assignIndices(idx);
        vid->indexCount = static_cast<uint32_t>(verts.size());
        vid->instanceCount = 1;
        sg->addChild(vid);
        return sg;
    }

    // Fallback: blended mesh (no pulse animation)
    return makeBlendedMesh(verts, uvs, color, 0.28f, 0.02f, {1.0f, false});
}

}}} // namespace vsgraysim::gui::ViewportKit
