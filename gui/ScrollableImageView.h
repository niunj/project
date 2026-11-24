#pragma once
/// @file ScrollableImageView.h
/// @brief 大幅面图像视图 (SAR/红外共用) — 第二阶段专业增强。
///
/// 支持: 缩放 (滚轮/按钮)、平移 (拖拽)、十字光标 (像素值读出)、
///       ROI 矩形 (统计)、自定义叠加 (目标框/测温区域)。
#ifndef VSGRAYSIM_GUI_SCROLLABLE_IMAGE_VIEW_H
#define VSGRAYSIM_GUI_SCROLLABLE_IMAGE_VIEW_H

#include <QWidget>
#include <QImage>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace vsgraysim { namespace gui {

class ScrollableImageView : public QWidget {
    Q_OBJECT
public:
    /// 叠加图形类型
    struct Overlay {
        enum class Kind { Box, Cross, Line, Text };
        Kind kind = Kind::Box;
        // 像素坐标 (图像坐标系)
        double x = 0, y = 0, w = 0, h = 0;
        double x2 = 0, y2 = 0;
        QString text;
        QColor color = QColor("#ff5252");
        bool filled = false;
    };

    explicit ScrollableImageView(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    /// Height-to-width ratio of a displayed pixel, used for calibrated axes.
    void setPixelAspect(double heightToWidth);
    struct Axis {
        double origin;
        double spacing;
        QString title;
    };
    void setAxes(std::optional<std::pair<Axis,Axis>> axes);
    void setOverlays(const std::vector<Overlay>& ov);
    void clear();
    /// Reset automatic fitting; a manual zoom/pan stays fixed across frames.
    void fitToWindow();

    /// ROI 使能 (拖拽选择统计区域)
    void setRoiEnabled(bool on) { roi_enabled_ = on; }
    bool roiEnabled() const { return roi_enabled_; }

    /// 十字光标使能
    void setCrosshairEnabled(bool on) { crosshair_ = on; }

    /// 当前 ROI (图像像素坐标, 无效为空)
    QRect roi() const { return roi_rect_; }
    bool hasRoi() const { return roi_rect_.isValid(); }

    /// 像素值查表回调 (图像像素坐标 -> 值字符串)
    std::function<QString(int, int)> pixelText;

signals:
    /// 光标移动 (图像像素坐标); 越界 (-1,-1)
    void cursorMoved(int img_x, int img_y);
    /// ROI 选择完成
    void roiChanged(const QRect& roi);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QPointF imgToWidget(const QPointF& img_pt) const;
    QPointF widgetToImg(const QPointF& w_pt) const;
    void clampOffset();
    QRectF imageViewport() const;
    void drawAxes(QPainter& painter, const QRectF& viewport) const;

    QImage img_;
    std::vector<Overlay> overlays_;
    bool auto_fit_=true;
    double scale_ = 1.0;        ///< 缩放因子
    double pixel_aspect_ = 1.0;
    std::optional<std::pair<Axis,Axis>> axes_;
    QPointF offset_;            ///< 平移 (widget 像素)
    bool crosshair_ = true;
    bool roi_enabled_ = false;
    bool dragging_ = false;
    bool selecting_ = false;
    QPointF press_pos_;
    QPointF cur_mouse_;
    QRect roi_rect_;
};

}} // namespace vsgraysim::gui
#endif // VSGRAYSIM_GUI_SCROLLABLE_IMAGE_VIEW_H
