#include "ScrollableImageView.h"
#include "base/PaintTiming.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace gui {

ScrollableImageView::ScrollableImageView(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(480, 360);
    setMouseTracking(true);
    setStyleSheet("background-color:#06080c;");
}

void ScrollableImageView::setImage(const QImage& img)
{
    if(img_.size()!=img.size())auto_fit_=true;
    img_ = img;
    if(auto_fit_)fitToWindow();
    update();
}

void ScrollableImageView::setPixelAspect(double heightToWidth) {
    if(!std::isfinite(heightToWidth)||heightToWidth<=0||pixel_aspect_==heightToWidth)return;
    pixel_aspect_=heightToWidth;
    fitToWindow();
}
void ScrollableImageView::setAxes(std::optional<std::pair<Axis,Axis>> axes) {
    const bool layoutChanged = axes.has_value()!=axes_.has_value();
    axes_ = std::move(axes);
    if(layoutChanged)fitToWindow();
    update();
}

QRectF ScrollableImageView::imageViewport() const {
    const QRectF full(rect());
    return axes_&&width()>180&&height()>110?full.adjusted(83,8,-22,-47):full;
}

void ScrollableImageView::fitToWindow(){
    auto_fit_=true;
    if(img_.isNull())return;
    const auto view = imageViewport();
    scale_=std::clamp(std::min(view.width()/img_.width(),view.height()/(img_.height()*pixel_aspect_))*.95,.05,40.);
    offset_={};clampOffset();update();
}
void ScrollableImageView::resizeEvent(QResizeEvent* e){QWidget::resizeEvent(e);if(auto_fit_)fitToWindow();else clampOffset();}
void ScrollableImageView::mouseDoubleClickEvent(QMouseEvent* e){if(e->button()==Qt::LeftButton){fitToWindow();e->accept();}else QWidget::mouseDoubleClickEvent(e);}

void ScrollableImageView::setOverlays(const std::vector<Overlay>& ov)
{
    overlays_ = ov;
    update();
}

void ScrollableImageView::clear()
{
    img_ = QImage();auto_fit_=true;scale_=1;pixel_aspect_=1;offset_={};axes_.reset();
    overlays_.clear();
    roi_rect_ = QRect();
    update();
}

QPointF ScrollableImageView::imgToWidget(const QPointF& img_pt) const
{
    return QPointF(img_pt.x() * scale_ + offset_.x(),
                   img_pt.y() * scale_ * pixel_aspect_ + offset_.y());
}

QPointF ScrollableImageView::widgetToImg(const QPointF& w_pt) const
{
    return QPointF((w_pt.x() - offset_.x()) / scale_,
                   (w_pt.y() - offset_.y()) / (scale_*pixel_aspect_));
}

void ScrollableImageView::clampOffset()
{
    if (img_.isNull()) return;
    const double iw = img_.width() * scale_;
    const double ih = img_.height() * scale_*pixel_aspect_;
    const auto view = imageViewport();
    if (iw <= view.width()) offset_.setX(view.center().x()-iw*.5);
    else offset_.setX(std::clamp(offset_.x(),view.right()-iw,view.left()));
    if (ih <= view.height()) offset_.setY(view.center().y()-ih*.5);
    else offset_.setY(std::clamp(offset_.y(),view.bottom()-ih,view.top()));
}

void ScrollableImageView::drawAxes(QPainter& painter,const QRectF& viewport) const {
    if(!axes_||viewport.left()==0.)return;
    const QRectF bounds=QRectF(offset_,QSizeF(img_.width()*scale_,img_.height()*scale_*pixel_aspect_)).intersected(viewport);
    const auto first=widgetToImg(bounds.topLeft());
    const auto last=widgetToImg(bounds.bottomRight());
    const auto& [horizontal,vertical]=*axes_;
    painter.setPen(QColor("#93a9b8"));
    painter.drawLine(bounds.bottomLeft(),bounds.bottomRight());
    painter.drawLine(bounds.topLeft(),bounds.bottomLeft());
    const int xTicks=std::clamp(int(bounds.width()/72.),1,5);
    const int yTicks=std::clamp(int(bounds.height()/35.),1,5);
    for(int i=0;i<=xTicks;++i){
        const double fraction=double(i)/xTicks;
        const double x=bounds.left()+fraction*bounds.width();
        const double value=horizontal.origin+std::min(first.x()+fraction*(last.x()-first.x()),double(img_.width()-1))*horizontal.spacing;
        painter.drawLine(QPointF(x,bounds.bottom()),QPointF(x,bounds.bottom()+4));
        painter.drawText(QRectF(x-36,bounds.bottom()+5,72,16),Qt::AlignHCenter,QString::number(value,'f',1));
    }
    for(int i=0;i<=yTicks;++i){
        const double fraction=double(i)/yTicks;
        const double y=std::clamp(bounds.top()+fraction*bounds.height(),9.,viewport.bottom());
        const double value=vertical.origin+std::min(first.y()+fraction*(last.y()-first.y()),double(img_.height()-1))*vertical.spacing;
        painter.drawLine(QPointF(bounds.left()-4,y),QPointF(bounds.left(),y));
        painter.drawText(QRectF(bounds.left()-62,y-8,54,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(value,'f',1));
    }
    painter.drawText(QRectF(bounds.left(),bounds.bottom()+24,bounds.width(),18),Qt::AlignHCenter,horizontal.title);
    painter.save();
    painter.translate(bounds.left()-66,bounds.center().y());
    painter.rotate(-90.);
    painter.drawText(QRectF(-bounds.height()*.5,-11,bounds.height(),18),Qt::AlignHCenter,vertical.title);
    painter.restore();
}

void ScrollableImageView::wheelEvent(QWheelEvent* e)
{
    auto_fit_=false;
    const double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const QPointF before = widgetToImg(e->position());
    scale_ = std::clamp(scale_ * factor, 0.05, 40.0);
    // Keep the image pixel under the pointer fixed at the new scale.
    offset_ = e->position() - QPointF(before.x()*scale_,before.y()*scale_*pixel_aspect_);
    clampOffset();
    update();
}

void ScrollableImageView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (roi_enabled_) {
            selecting_ = true;
            press_pos_ = e->position();
            roi_rect_ = QRect();
        } else {
            dragging_ = true;auto_fit_=false;
            press_pos_ = e->position();
        }
    }
}

void ScrollableImageView::mouseMoveEvent(QMouseEvent* e)
{
    cur_mouse_ = e->position();
    if (dragging_) {
        offset_ += e->position() - press_pos_;
        press_pos_ = e->position();
        clampOffset();
    }
    if (selecting_) {
        const QPointF a = widgetToImg(press_pos_);
        const QPointF b = widgetToImg(e->position());
        roi_rect_ = QRect(int(std::round(std::min(a.x(), b.x()))),
                          int(std::round(std::min(a.y(), b.y()))),
                          int(std::abs(b.x() - a.x())),
                          int(std::abs(b.y() - a.y())));
    }
    // 光标信号
    const QPointF ip = widgetToImg(e->position());
    const int ix = int(std::floor(ip.x())), iy = int(std::floor(ip.y()));
    if (imageViewport().contains(e->position()) && ix >= 0 && ix < img_.width() && iy >= 0 && iy < img_.height())
        emit cursorMoved(ix, iy);
    else
        emit cursorMoved(-1, -1);
    update();
}

void ScrollableImageView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (selecting_) {
            selecting_ = false;
            if (roi_rect_.width() < 3 || roi_rect_.height() < 3)
                roi_rect_ = QRect();   // 太小忽略
            emit roiChanged(roi_rect_);
        }
        dragging_ = false;
    }
}

void ScrollableImageView::paintEvent(QPaintEvent*)
{
    PaintTiming timing{this};
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor("#06080c"));

    if (img_.isNull()) {
        p.setPen(QColor(120, 130, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("图像区域 · 等待数据"));
        return;
    }

    // 图像
    const auto viewport=imageViewport();
    p.save();
    p.setClipRect(viewport);
    const QPointF tl = imgToWidget(QPointF(0, 0));
    p.drawImage(QRectF(tl, QSizeF(img_.width() * scale_,
                                  img_.height() * scale_*pixel_aspect_)), img_);

    // ROI 选择中
    if (selecting_ && roi_rect_.isValid()) {
        const QPointF a = imgToWidget(QPointF(roi_rect_.left(), roi_rect_.top()));
        const QPointF b = imgToWidget(QPointF(roi_rect_.right(), roi_rect_.bottom()));
        p.setPen(QPen(QColor("#4fc3f7"), 1.5, Qt::DashLine));
        p.setBrush(QColor(79, 195, 247, 30));
        p.drawRect(QRectF(a, b));
    }

    // 叠加图形
    for (const auto& ov : overlays_) {
        p.setPen(QPen(ov.color, 1.5));
        p.setBrush(ov.filled ? QColor(ov.color.red(), ov.color.green(),
                                       ov.color.blue(), 40)
                             : Qt::NoBrush);
        switch (ov.kind) {
        case Overlay::Kind::Box: {
            const QPointF a = imgToWidget(QPointF(ov.x, ov.y));
            const QPointF b = imgToWidget(QPointF(ov.x + ov.w, ov.y + ov.h));
            p.drawRect(QRectF(a, b));
            if (!ov.text.isEmpty()) {
                p.setPen(ov.color);
                QFont f = p.font(); f.setPointSizeF(8.0); p.setFont(f);
                p.drawText(QPointF(a.x(), a.y() - 3), ov.text);
            }
            break;
        }
        case Overlay::Kind::Cross: {
            const QPointF c = imgToWidget(QPointF(ov.x, ov.y));
            const double r = 10.0;
            p.drawLine(c - QPointF(r, 0), c + QPointF(r, 0));
            p.drawLine(c - QPointF(0, r), c + QPointF(0, r));
            if (!ov.text.isEmpty()) {
                p.setPen(ov.color);
                QFont f = p.font(); f.setPointSizeF(8.0); p.setFont(f);
                p.drawText(c + QPointF(r + 2, 0), ov.text);
            }
            break;
        }
        case Overlay::Kind::Line: {
            const QPointF a = imgToWidget(QPointF(ov.x, ov.y));
            const QPointF b = imgToWidget(QPointF(ov.x2, ov.y2));
            p.drawLine(a, b);
            if (!ov.text.isEmpty()) {
                p.setPen(ov.color);
                QFont f = p.font(); f.setPointSizeF(8.0); p.setFont(f);
                p.drawText((a + b) / 2.0 + QPointF(3, -3), ov.text);
            }
            break;
        }
        case Overlay::Kind::Text: {
            const QPointF a = imgToWidget(QPointF(ov.x, ov.y));
            p.setPen(ov.color);
            QFont f = p.font(); f.setPointSizeF(8.5); p.setFont(f);
            p.drawText(a, ov.text);
            break;
        }
        }
    }

    // 十字光标
    if (crosshair_ && underMouse()) {
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
        p.drawLine(QPointF(0, cur_mouse_.y()), QPointF(width(), cur_mouse_.y()));
        p.drawLine(QPointF(cur_mouse_.x(), 0), QPointF(cur_mouse_.x(), height()));
    }
    p.restore();
    drawAxes(p,viewport);
}

}} // namespace vsgraysim::gui
