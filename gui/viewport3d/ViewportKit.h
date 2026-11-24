#pragma once
/// @file ViewportKit.h
/// @brief 3D 视口共享组件库 (地形编辑器 / 场景设计器 / 仿真预览共用)。
///
/// 统一抽取三个窗口曾逐字重复实现的 VSG 视口原语:
///   - 默认灯光 (环境光 0.35 + 太阳平行光 1.2)
///   - 资产路径解析 (相对可执行文件目录或其父目录)
///   - 模型加载 (vsg 读取器优先, .obj 回退到 ObjLoader)
///   - LINE_LIST 线段几何 (Builder 管线 + 拓扑补丁)
///   - 传感器视锥线框顶点生成
///   - 实体变换矩阵 / 传感器类型配色
#ifndef VSGRAYSIM_GUI_VIEWPORTKIT_H
#define VSGRAYSIM_GUI_VIEWPORTKIT_H

#include <vsg/core/Array.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/vec3.h>

#include <QColor>

#include <string>
#include <vector>

namespace vsg {
class Group;
class StateGroup;
class Node;
class Options;
class Camera;
}

namespace vsgraysim { namespace gui {

namespace ViewportKit {

/// push-constant 布局定义 (各自定义 shader 共用, 16 字节)。
struct ShaderTimePush {
    float time_sec = 0.0f;   ///< 全局时间 (数据流/脉冲动画)
    float param1   = 0.0f;   ///< 自定义参数 (通断状态/扫描角度等)
    float param2   = 0.0f;   ///< 自定义参数
    float pad      = 0.0f;
};

/// 自定义 shader 节点的逐帧更新句柄。特性持有它以便在 onFrame 中
/// 刷新相机矩阵 (UBO) + 动画参数 (push-constant)。camera_ub 为空即非 shader 管线。
struct ShaderHandle {
    vsg::ref_ptr<vsg::floatArray> camera_ub;  ///< 相机 UBO (20 float: mat4 view_proj + vec4(cam_pos,time))
    vsg::ref_ptr<vsg::floatArray> push_pc;    ///< push-constant (4 float: time/param1/param2/pad)
    bool valid() const { return camera_ub && push_pc; }
};

/// 从 shaders/viewport/ 目录加载 GLSL 并构建自定义 GraphicsPipeline。
/// 返回的 StateGroup 已挂载 BindGraphicsPipeline + 相机 UBO 描述符集 +
/// push-constant 命令; 可直接 addChild(geometry)。失败时返回空 (调用方应回退到 makeBlendedMesh)。
/// out: 若提供, 填充逐帧更新句柄。
vsg::ref_ptr<vsg::StateGroup> createShaderPipeline(
    const std::string& vert_name,
    const std::string& frag_name,
    bool blending = true,
    bool depth_test = true,
    ShaderHandle* out = nullptr);

/// 逐帧刷新自定义 shader 节点的相机矩阵 + 动画参数。
/// view_proj = projection * view (世界→裁剪); 需在每帧 onFrame 中调用。
/// model = 内容根局部→世界矩阵: rocky 地球模式下为锚点 ENU→ECEF 矩阵,
/// 传统平面模式下传单位矩阵(默认)。自定义 shader 用 view_proj*model*pos
/// 完成局部几何到世界的定位 (vsg::MatrixTransform 不会作用于自定义管线)。
void updateShaderNode(const ShaderHandle& h,
                      const vsg::dmat4& view_proj,
                      const vsg::dvec3& cam_pos,
                      float time_sec, float param1 = 0.0f, float param2 = 0.0f,
                      const vsg::dmat4& model = vsg::dmat4(1.0));

/// 从 vsg::Camera 提取 view_proj (projection * view) 与相机世界位置。
/// cam 为空时返回单位矩阵 + 原点 (调用方仍可安全传给 updateShaderNode)。
void cameraMatrices(const vsg::Camera* cam,
                    vsg::dmat4& view_proj, vsg::dvec3& cam_pos);

/// 线段渲染样式 (同一套 LINE_LIST 管线, 不同场景不同样式)。
struct LineStyle {
    float width = 1.0f;        ///< 线宽
    bool  depth_bias = false;  ///< 负深度偏移 (避免与地形 z-fighting)
};

/// 默认灯光组: 环境光 (0.35) + 太阳平行光 (1.2), 方向 (-0.5,-1,-0.35)。
vsg::ref_ptr<vsg::Group> createDefaultLights();

/// 相对资产路径解析: 依次尝试原路径 / <exe 目录>/p / <exe 父目录>/p。
std::string resolveAssetPath(const std::string& p);

/// 模型加载: vsg::read_cast 优先; .obj 失败时回退 ObjLoader。
vsg::ref_ptr<vsg::Node> loadModelNode(const std::string& path,
                                      vsg::ref_ptr<vsg::Options> options = {});

/// 彩色线段节点 (LINE_LIST): `verts` 为成对端点, 每对一条线段。
/// ref_ptr 重载直接复用数组 (保留 DYNAMIC_DATA, 供逐帧原地改写)。
vsg::ref_ptr<vsg::StateGroup> makeLineSegments(
    vsg::ref_ptr<vsg::vec3Array> verts, const QColor& color,
    const LineStyle& style = {});
vsg::ref_ptr<vsg::StateGroup> makeLineSegments(
    const std::vector<vsg::vec3>& seg_pts, const QColor& color,
    const LineStyle& style = {});

/// 半透明填充网格 (TRIANGLE_LIST + 混合): `tri_pts` 为三角形顶点流
/// (每 3 个一三角), `uvs` 与顶点一一对应, uv.x 采样径向透明度渐变
/// (0=alpha_center → 1=alpha_edge)。用于探测范围/扇区/视锥侧壁等覆盖填充。
vsg::ref_ptr<vsg::StateGroup> makeBlendedMesh(
    const std::vector<vsg::vec3>& tri_pts,
    const std::vector<vsg::vec2>& uvs,
    const QColor& color, float alpha_center, float alpha_edge,
    const LineStyle& style = {});

/// XZ 平面圆形填充盘 (探测范围圈底填充, 贴地 + 径向渐隐)。
vsg::ref_ptr<vsg::StateGroup> makeFilledDisc(
    const vsg::vec3& center, double radius, const QColor& color,
    float alpha_center = 0.20f, float alpha_edge = 0.02f,
    int segs = 64);

/// XZ 平面扇形填充 (搜索扇区底填充, az/el 约定同 DetectionEffect)。
vsg::ref_ptr<vsg::StateGroup> makeFilledSector(
    const vsg::vec3& center, double radius, double az_center_deg,
    double fov_h_deg, const QColor& color,
    float alpha_center = 0.26f, float alpha_edge = 0.03f,
    int segs = 40);

/// 三维视锥四个侧壁的半透明填充 (apex → 远端矩形, 沿深度渐隐)。
vsg::ref_ptr<vsg::StateGroup> makeFrustumWalls(
    const vsg::dvec3& apex, const vsg::dvec3& fwd,
    const vsg::dvec3& right, const vsg::dvec3& up,
    double length, double fov_h_deg, double fov_v_deg,
    const QColor& color,
    float alpha_apex = 0.30f, float alpha_far = 0.05f);

/// 阵营光柱: 过 center 的两片正交竖直矩形 (X/Z 向), 自底向上渐隐。
/// 态势显示中标识平台归属/位置的经典表现 (远距离仍可见)。
vsg::ref_ptr<vsg::StateGroup> makeLightColumn(
    const vsg::vec3& center, double height, double half_width,
    const QColor& color, float alpha_bottom = 0.38f, float alpha_top = 0.0f);

/// 由观察方向推导视锥右/上基向量 (水平向上参考, 近竖直时换用 X 轴)。
void frustumBasis(const vsg::dvec3& dir, vsg::dvec3& right, vsg::dvec3& up);

/// 传感器视锥线框顶点 (成对端点): 4 条棱 + 远端矩形框
/// (+ 可选中间截面 / 视轴线)。`right`/`up` 为视锥右/上基向量。
std::vector<vsg::vec3> frustumSegments(const vsg::dvec3& origin,
                                       const vsg::dvec3& fwd,
                                       const vsg::dvec3& right,
                                       const vsg::dvec3& up,
                                       double fov_h_deg, double fov_v_deg,
                                       double length,
                                       bool mid_plane = false,
                                       bool axis_line = false);

/// 实体变换矩阵: translate * rotateY(yaw) * scale。
vsg::dmat4 makeEntityTransform(const vsg::dvec3& pos, double yaw_deg,
                               double scale);

/// 传感器类型 → 视锥线框颜色 (三软件统一配色)。
QColor sensorTypeColor(const std::string& type);

// ---------------------------------------------------------------------------
// 三维体积/管道/条带/光晕几何 (特效增强)
// ---------------------------------------------------------------------------

/// 三维半球或扇形半球网格 (探测体积)。
/// 方位范围 [az_min, az_max] 度, 俯仰范围 [el_min, el_max] 度。
/// occlusion: 每个角度方向的命中距离 (来自 OcclusionQueryService),
/// null 或空则完整半球。数组布局: [el_idx * az_segs + az_idx]。
/// uv.x = 沿径向进度 (0→1), uv.y = 方位角度分数 (0→1)。
/// out: 若提供, 填充逐帧更新句柄 (相机矩阵 + 扫描动画参数)。
vsg::ref_ptr<vsg::StateGroup> makeHemisphere(
    const vsg::dvec3& center, double max_radius,
    double az_min_deg, double az_max_deg,
    double el_min_deg, double el_max_deg,
    int az_segs, int el_segs,
    const QColor& color,
    const std::vector<float>* occlusion = nullptr,
    float alpha_inner = 0.25f, float alpha_edge = 0.02f,
    ShaderHandle* out = nullptr);

/// 通信/交战半透明管道: 沿两点连线生成圆柱体网格。
/// uv.x = 沿管道方向 (0=起点→1=终点), uv.y = 圆周角度 (0→1)。
/// radius = 管道半径 (m), segs = 沿长度分段, radial_segs = 圆周分段。
vsg::ref_ptr<vsg::StateGroup> makeCommTube(
    const vsg::vec3& from, const vsg::vec3& to,
    float radius, int segs, int radial_segs,
    const QColor& color, float alpha = 0.3f,
    ShaderHandle* out = nullptr);

/// 水平运动条带 (拖尾): 沿采样点序列铺设带状网格。
/// uv.x = 沿条带方向 (0=最旧→1=最新), uv.y = 宽度方向 (0→1)。
/// width = 条带宽度 (m), 点数 < 2 返回空。
vsg::ref_ptr<vsg::StateGroup> makeTrailRibbon(
    const std::vector<vsg::vec3>& points,
    float width, const QColor& color,
    ShaderHandle* out = nullptr);

/// 阵营光晕穹顶: 半球穹顶网格, 底部浓顶部淡。
/// uv.x = 方位角度 (0→1), uv.y = 高度分数 (0=底部→1=顶部)。
vsg::ref_ptr<vsg::StateGroup> makeFactionHalo(
    const vsg::vec3& center, double radius, double height,
    const QColor& color,
    ShaderHandle* out = nullptr);

} // namespace ViewportKit

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_VIEWPORTKIT_H
