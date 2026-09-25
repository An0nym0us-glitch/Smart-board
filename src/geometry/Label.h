#pragma once

#include <QColor>
#include <QPointF>
#include <QString>

class QPainter;

namespace cb {

/// Draws a readable value label (rounded dark plate + coloured text) centred at anchor.
/// counterRotation undoes the object's rotation so the label stays upright.
void paintValueLabel(QPainter& painter, const QPointF& anchor, const QString& text, const QColor& color,
                     qreal counterRotation = 0.0, qreal pixelSize = 22.0);

} // namespace cb
