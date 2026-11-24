#pragma once
/// @file SensorFovFeature.h
/// @brief 传感器视锥特性: 世界空间视锥组管理 + 局部空间视锥节点构建
///        (迁移自 SceneDesignerWindow::makeFovConeNode 与
///        SimulationControlWindow::buildPreviewScene 的视锥部分)。
#ifndef VSGRAYSIM_VIEWPORT3D_SENSORFOVFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_SENSORFOVFEATURE_H

#include "IViewportFeature.h"
#include "SceneEntities.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/core/Array.h>
#include <vsg/maths/vec3.h>

#include <string>
#include <vector>

namespace vsg { class Group; class Node; }

namespace vsgraysim { namespace viewport3d {

/// 传感器 FoV 视锥线框。
/// 两种挂载模式:
///   - 局部空间: makeLocalConeNode() 产出节点, 宿主挂实体 mt 下
///     (含中间截面 + 视轴, 随实体变换)。
///   - 世界空间: setWorldFovs() 全量替换本特性管理的视锥组
///     (仿真预览的简化框)。
class SensorFovFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "sensor_fov";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;

    /// 局部空间视锥节点 (实体坐标系): 视轴 = 实体系 +Z 先绕 X 俯仰再绕 Y 方位。
    /// `mid_plane`/`axis_line` 控制中间截面与视轴线 (场景设计器全开)。
    static vsg::ref_ptr<vsg::Node> makeLocalConeNode(const FovSensor& s,
                                                     const QColor& color,
                                                     bool mid_plane = true,
                                                     bool axis_line = true);

    /// 世界空间视锥: 孔径位置 + 视线方向 + 角度/长度 (仿真预览 15 m 简化框)。
    struct WorldFov {
        vsg::dvec3 origin{0.0, 0.0, 0.0};   ///< 孔径位置 (已含安装偏移)
        vsg::dvec3 fwd{0.0, 0.0, 1.0};      ///< 视线方向 (单位化)
        double fov_h = 60.0;                ///< deg
        double fov_v = 30.0;                ///< deg
        double range = 15.0;                ///< 视锥长度 (m)
        QColor color = QColor(0x9e, 0x9e, 0x9e);
        // B7 §7.2 扫描机构可视化 (scan.mode 非 fixed 时: 扫描扇形 + 摆动扫描线)
        std::string scan_mode;              ///< 空/fixed = 无扫描效果
        double scan_az_center = 0.0;        ///< 扫描中心方位 (°, 相对标称视轴)
        double scan_az_extent = 0.0;        ///< 方位扫描范围 (°)
        double scan_az_rate = 0.0;          ///< 方位速率 (°/s; circular 为 rpm)
    };

    /// 全量替换世界空间视锥组。
    void setWorldFovs(const std::vector<WorldFov>& fovs);
    void clearWorldFovs();

    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

private:
    /// 世界空间扫描线 (扇面内摆动, onFrame 逐帧改写端点; B7 §7.2)。
    struct SweepLine {
        vsg::ref_ptr<vsg::vec3Array> verts; ///< 2 端点 (中心→弧), DYNAMIC_DATA
        vsg::vec3 center;                   ///< 扇心 (贴地投影)
        double radius = 0.0;
        double base_az = 0.0;               ///< 标称视轴世界方位 (°, az=0→+Z)
        double az_center = 0.0;
        double az_extent = 0.0;
        double az_rate = 0.0;
        bool circular = false;              ///< circular=匀速周扫, 其余=三角波往复
    };

    vsg::ref_ptr<vsg::Group> world_root_;
    ViewportContext* ctx_ = nullptr;
    std::vector<WorldFov> last_fovs_;   ///< 快照去重 (重复喂入免重建+免编译)
    std::vector<SweepLine> sweeps_;     ///< 摆动扫描线 (setWorldFovs 重建)
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_SENSORFOVFEATURE_H
