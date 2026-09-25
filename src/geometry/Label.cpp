#include "geometry/Label.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

namespace cb {

void paintValueLabel(QPainter& p, const QPointF& anchor, const QString& text, const QColor& color,
                     qreal counterRotation, qreal pixelSize)
{
    if (text.isEmpty())
        return;
    p.save();
    p.translate(anchor);
    if (counterRotation != 0.0)
        p.rotate(-counterRotation);
    QFont f = p.font();
    f.setPixelSize(static_cast<int>(pixelSize));
    f.setBold(true);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const qreal w = fm.horizontalAdvance(text) + pixelSize * 0.8;
    const qreal h = fm.height() + pixelSize * 0.3;
    const QRectF box(-w / 2, -h / 2, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 14, 190));
    p.drawRoundedRect(box, h * 0.3, h * 0.3);
    p.setPen(color);
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}

} // namespace cb
