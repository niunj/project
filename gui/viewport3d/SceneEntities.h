#pragma once
/// @file SceneEntities.h
/// @brief viewport3d 库场景实体公共类型: 阵营 / 航路点 / 传感器视锥参数。
///
/// 库侧轻量定义, 不依赖窗口业务结构 (SceneDesignerWindow 的同名结构
/// 值兼容, 迁移时直接转换)。
#ifndef VSGRAYSIM_VIEWPORT3D_SCENEENTITIES_H
#define VSGRAYSIM_VIEWPORT3D_SCENEENTITIES_H

#include <QColor>

namespace vsgraysim { namespace viewport3d {

/// 阵营 (红方/蓝方/中立) —— 航迹线与视锥着色依据。
enum class Faction : int { Red = 0, Blue = 1, Neutral = 2 };

/// 阵营 → 配色 (三软件统一: 红 #ff5252 / 蓝 #448aff / 中立 #9e9e9e)。
inline QColor factionColor(Faction f)
{
    switch (f) {
        case Faction::Red:  return QColor(0xff, 0x52, 0x52);
        case Faction::Blue: return QColor(0x44, 0x8a, 0xff);
        case Faction::Neutral: break;
    }
    return QColor(0x9e, 0x9e, 0x9e);
}

/// 航路点: 时间 + 世界坐标位置 (航迹线 / 回放运动共用)。
struct Waypoint {
    double time = 0.0;                ///< 相对场景开始的时间 (s)
    double pos[3] = {0.0, 0.0, 0.0};  ///< 世界坐标 (m)
};

/// 传感器视锥几何参数 (局部空间视锥构建用)。
struct FovSensor {
    double fov_h   = 60.0;   ///< 水平视场角 (deg)
    double fov_v   = 30.0;   ///< 垂直视场角 (deg)
    double az_deg  = 0.0;    ///< 方位角 (deg, 实体系绕 Y)
    double el_deg  = 0.0;    ///< 俯仰角 (deg, 实体系绕 X, 正值向上)
    double range_m = 500.0;  ///< 视锥长度/作用距离 (m)
    double offset_y = 2.0;   ///< 孔径相对实体原点的高度 (m)
};

}} // namespace vsgraysim::viewport3d
#endif // VSGRAYSIM_VIEWPORT3D_SCENEENTITIES_H
