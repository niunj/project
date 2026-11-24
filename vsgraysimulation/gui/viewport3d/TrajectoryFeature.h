#pragma once
/// @file TrajectoryFeature.h
/// @brief 航迹线特性: 世界空间航迹折线 (按阵营色, 迁移自
///        SceneDesignerWindow::updateTrajectoryVisuals)。
#ifndef VSGRAYSIM_VIEWPORT3D_TRAJECTORYFEATURE_H
#define VSGRAYSIM_VIEWPORT3D_TRAJECTORYFEATURE_H

#include "IViewportFeature.h"
#include "SceneEntities.h"

#include <vsg/core/ref_ptr.h>

#include <vector>

namespace vsg { class Group; }

namespace vsgraysim { namespace viewport3d {

/// 航迹线: 宿主 (门面) 下发全部航迹数据后整体重建线段节点。
class TrajectoryFeature : public IViewportFeature {
public:
    static constexpr const char* kId = "trajectory";
    const char* id() const override { return kId; }
    void onAttach(ViewportContext& ctx) override;
    void onDetach(ViewportContext& ctx) override;

    /// 一条航迹: >= 2 个航路点 + 着色 (通常为阵营色)。
    struct Track {
        std::vector<Waypoint> points;
        QColor color = QColor(0x9e, 0x9e, 0x9e);
    };

    /// 全量替换航迹线 (< 2 点的航迹忽略)。
    void setTracks(const std::vector<Track>& tracks);
    void clear();

private:
    vsg::ref_ptr<vsg::Group> root_;
    ViewportContext* ctx_ = nullptr;
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_TRAJECTORYFEATURE_H
