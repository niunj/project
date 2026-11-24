#pragma once
/// @file PreviewRenderer.h
/// @brief 预览图生成: 地形高度场伪彩色渲染 + 场景合成预览兜底。
///
/// 地形预览在 CPU 侧从高度数据直接渲染 (可靠, 不依赖 VSG)。
/// 场景预览优先使用 VsgQtWidget::grabFrame() 的真实截屏, 失败时用
/// compositeScenePreview() 合成占位图, 保证启动列表永远有图。
#ifndef VSGRAYSIM_GUI_PREVIEWRENDERER_H
#define VSGRAYSIM_GUI_PREVIEWRENDERER_H

#include <QImage>
#include <QString>
#include <vector>

namespace vsgraysim { namespace gui {

/// 高度场 → 伪彩色地形预览图 (256x256, 从顶向下)。
/// `heights` 为行优先 (z*n+x), `n` 为边长 (米), `water_level` 为水面高度。
QImage terrainPreviewFromHeights(const std::vector<double>& heights,
                                 uint32_t n, double water_level);

/// 场景合成预览 (真实截屏不可用时的兜底图)。
QImage compositeScenePreview(const QString& name, int objects, int sensors,
                             QSize size = QSize(320, 240));

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_PREVIEWRENDERER_H
