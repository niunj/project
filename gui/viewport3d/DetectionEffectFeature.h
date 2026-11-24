#pragma once
/// @file DetectionEffectFeature.h
/// @brief 装备探测特效特性: 探测范围 / 搜索角度 一体显示。
///
/// 每个传感器条目聚合以下几何 (统一线框风格, 与世界空间挂点关联场景树元素):
///   - 探测范围: 地面圆形范围圈 (半径 = 作用距离, 含距离刻度);
///   - 搜索角度: 地面搜索扇扫 (方位中心 ± fov_h/2, 覆盖扇形);
///   - 三维波束: 视锥四棱台 (水平/垂直视场角定形, 作用距离定长),
///     侧壁半透明渐隐 + 棱线/远端矩形线框, 精确标识波束覆盖空间。
/// 场景设计按固定配置喂入; 仿真控制按平台/设备实时快照逐帧更新。
#ifndef VSGRAYSIM_VIEWPORT3D_DETECTIONEFFECTFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_DETECTIONEFFECTFEATURE_H

#include "IViewportFeature.h"
#include "ViewportKit.h"

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/MatrixTransform.h>

#include <QColor>

#include <memory>
#include <utility>
#include <vector>

namespace vsg { class Group; }
namespace vsgraysim { namespace viewport3d {
class OcclusionQueryService;
class ViewportContext;

class DetectionEffectFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "detection_effect";

    /// 单个探测特效: 关联到场景树的 实体(id + 传感器序号)。
    struct Effect {
        std::string entity_id;     ///< 关联实体 id (与场景树一致)
        int         sensor_idx = 0;///< 实体内传感器序号
        vsg::dvec3  pos{0.0, 0.0, 0.0};  ///< 孔径世界位置
        double yaw      = 0.0;  ///< 平台航向 (deg, 世界系绕 +Y)
        double az       = 0.0;  ///< 传感器方位安装角 (deg, 平台系)
        double el       = 0.0;  ///< 传感器俯仰安装角 (deg, 正值向上)
        double fov_h    = 60.0; ///< 搜索角度/水平视场 (deg)
        double fov_v    = 30.0; ///< 垂直视场 (deg)
        double range    = 500.0;///< 探测范围/作用距离 (m)
        QColor color    = QColor(0x9e,0x9e,0x9e);
        bool show_sector = true; ///< 地面搜索扇扫
        // ---- 业务化表现 ----
        bool fill       = true; ///< 半透明填充 (范围盘/扇区)
        // ---- 3D 体积参数 ----
        enum class VolumeShape { Hemisphere, SectorHemisphere, FullSphere };
        VolumeShape volume_shape = VolumeShape::SectorHemisphere;
        bool rt_occlusion = true;       ///< 是否启用遮挡查询
        bool scan_animation = true;    ///< 扫描波束旋转动画
        double scan_speed = 30.0;      ///< 扫描转速 (deg/s)
        double current_scan_az = 0.0;  ///< 当前扫描方位 (onFrame 推进)
        // ---- 锁定状态梯度 (搜索 → 跟踪 → 锁定) ----
        enum class LockState { Search, Track, Lock };
        LockState lock_state = LockState::Search;
        double track_az = 0.0;         ///< 锁定时波束指向 (世界方位角, deg)
    };

    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;
    /// 整体显隐开关 (从世界场景根增删本特性节点)。
    void setEnabled(bool on) override;
    /// 每帧驱动: 扫描波束旋转 + push-constant 时间更新。
    void onFrame(ViewportContext& ctx, const FrameTick& tick) override;

    /// 全量刷新 (探测特效 + 关联数据 + 遮挡查询)。
    void setEffects(const std::vector<Effect>& fx);
    void clear();

    /// 注入遮挡查询服务 (由 TerrainLayerFeature onAttach 时设置)。
    void setOcclusionService(std::shared_ptr<OcclusionQueryService> svc)
    { occlusion_svc_ = std::move(svc); }

private:
    void rebuild();
    void setInWorld(bool add);

    ViewportContext* ctx_ = nullptr;
    vsg::ref_ptr<vsg::Group> root_;
    std::vector<Effect> fx_;
    std::shared_ptr<OcclusionQueryService> occlusion_svc_;
    bool in_world_ = false;
    double elapsed_sec_ = 0.0;  ///< 全局时间 (onFrame 累积)
    /// 半球体积节点的逐帧更新句柄 (first = fx_ 索引, 供扫描方位关联)。
    std::vector<std::pair<int, gui::ViewportKit::ShaderHandle>> shader_handles_;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_DETECTIONEFFECTFEATURE_H