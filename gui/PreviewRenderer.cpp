#include "PreviewRenderer.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace vsgraysim { namespace gui {

QImage terrainPreviewFromHeights(const std::vector<double>& heights,
                                 uint32_t n, double water_level)
{
    if (heights.size() < size_t(n) * n || n == 0)
        return QImage(256, 256, QImage::Format_RGB32);

    double lo = 0.0, hi = 1.0;
    {
        auto [a, b] = std::minmax_element(heights.begin(), heights.end());
        lo = *a; hi = *b;
        if (hi - lo < 1e-9) hi = lo + 1.0;
    }

    QImage img(256, 256, QImage::Format_RGB32);
    auto shade = [](double t) -> QRgb {
        // 深绿 → 草绿 → 褐 → 灰 → 白 (雪山)
        t = std::clamp(t, 0.0, 1.0);
        const QRgb ramp[] = {
            qRgb(0x2e, 0x5d, 0x2e),
            qRgb(0x4a, 0x8c, 0x3f),
            qRgb(0x9a, 0x8a, 0x54),
            qRgb(0x8a, 0x82, 0x74),
            qRgb(0xd8, 0xd8, 0xd8),
        };
        const double s = t * (4 - 1e-9);
        const int i = int(s);
        const double f = s - i;
        const QRgb a = ramp[i], b = ramp[std::min(i + 1, 4)];
        return qRgb(int(qRed(a) + (qRed(b) - qRed(a)) * f),
                    int(qGreen(a) + (qGreen(b) - qGreen(a)) * f),
                    int(qBlue(a) + (qBlue(b) - qBlue(a)) * f));
    };

    const double water_h = water_level;
    for (int py = 0; py < 256; ++py) {
        const double wz = double(256 - 1 - py) / 255.0 * double(n - 1);
        const uint32_t z = uint32_t(std::clamp(wz, 0.0, double(n - 1)));
        for (int px = 0; px < 256; ++px) {
            const double wx = double(px) / 255.0 * double(n - 1);
            const uint32_t x = uint32_t(std::clamp(wx, 0.0, double(n - 1)));
            const double h = heights[size_t(z) * n + x];
            QRgb c;
            if (h <= water_h && water_h > lo)
                c = qRgb(0x1f, 0x4a, 0x6e);   // 水面
            else
                c = shade((h - lo) / (hi - lo));
            img.setPixel(px, py, c);
        }
    }
    return img;
}

QImage compositeScenePreview(const QString& name, int objects, int sensors,
                             QSize size)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor("#161c27"));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    // 地平线 + 网格示意
    const int horizon = int(size.height() * 0.62);
    QLinearGradient sky(0, 0, 0, horizon);
    sky.setColorAt(0.0, QColor("#10131a"));
    sky.setColorAt(1.0, QColor("#2a3a55"));
    p.fillRect(0, 0, size.width(), horizon, sky);
    QLinearGradient ground(0, horizon, 0, size.height());
    ground.setColorAt(0.0, QColor("#26303f"));
    ground.setColorAt(1.0, QColor("#141a24"));
    p.fillRect(0, horizon, size.width(), size.height() - horizon, ground);

    p.setPen(QPen(QColor(90, 144, 217, 90), 1.0));
    for (int gx = size.width() / 4; gx < size.width(); gx += size.width() / 4) {
        p.drawLine(QPoint(gx, horizon), QPoint(size.width() / 2, size.height()));
        p.drawLine(QPoint(size.width() - gx, horizon), QPoint(size.width() / 2, size.height()));
    }
    for (double gz = 0.35; gz < 1.0; gz += 0.3) {
        const int y = int(horizon + (size.height() - horizon) * (gz * gz));
        p.setPen(QPen(QColor(90, 144, 217, 60), 1.0));
        p.drawLine(0, y, size.width(), y);
    }
    // 太阳
    p.setBrush(QColor(255, 214, 130, 200));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPoint(size.width() / 2, int(horizon * 0.35)), 14, 14);

    if (!name.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(11);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor("#e8ecf3"));
        p.drawText(img.rect().adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft, name);
    }
    const QString stats = QString("实体 %1  ·  传感器 %2").arg(objects).arg(sensors);
    QFont f = p.font();
    f.setPointSize(9);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#9aa5b8"));
    p.drawText(img.rect().adjusted(8, -4, -8, -8),
               Qt::AlignBottom | Qt::AlignLeft, stats);
    return img;
}

}} // namespace vsgraysim::gui
