#include "ui/widgets/SegmentedControl.h"

#include "ui/UiContext.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace cb {

SegmentedControl::SegmentedControl(const UiContext& ui, const QVector<Option>& options, QWidget* parent)
    : QWidget(parent)
    , m_ui(ui)
    , m_options(options)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SegmentedControl::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_options.size() || index == m_current)
        return;
    m_current = index;
    update();
}

QSize SegmentedControl::sizeHint() const
{
    const Theme& t = m_ui.theme;
    const QFontMetrics fm(t.font(t.metric(ThemeMetric::SmallFontSize)));
    int w = 0;
    bool anyIcon = false;
    for (const Option& o : m_options) {
        w += std::max(t.dpi(64), fm.horizontalAdvance(o.label) + t.dpi(20));
        anyIcon = anyIcon || !o.icon.isEmpty();
    }
    return QSize(w, anyIcon ? t.dpi(72) : t.dpi(48));
}

QRectF SegmentedControl::segmentRect(int i) const
{
    const qreal w = qreal(width()) / std::max(1, m_options.size());
    return QRectF(i * w, 0, w, height());
}

void SegmentedControl::paintEvent(QPaintEvent*)
{
    const Theme& t = m_ui.theme;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = t.scaled(ThemeMetric::CornerRadius);
    p.setPen(Qt::NoPen);
    p.setBrush(t.color(ThemeColor::Input));
    p.drawRoundedRect(QRectF(rect()), radius, radius);
    const qreal dpr = devicePixelRatioF();
    for (int i = 0; i < m_options.size(); ++i) {
        const QRectF r = segmentRect(i).adjusted(t.dp(3), t.dp(3), -t.dp(3), -t.dp(3));
        const bool sel = i == m_current;
        if (sel) {
            p.setBrush(t.color(ThemeColor::ButtonChecked));
            p.setPen(QPen(t.color(ThemeColor::Accent), t.dp(1.5)));
            p.drawRoundedRect(r, radius - t.dp(3), radius - t.dp(3));
        }
        const QColor fg = sel ? t.color(ThemeColor::Accent) : t.color(ThemeColor::Text);
        const Option& o = m_options[i];
        const QFont f = t.font(t.metric(ThemeMetric::SmallFontSize), sel);
        p.setFont(f);
        p.setPen(fg);
        if (o.icon.isEmpty()) {
            p.drawText(r, Qt::AlignCenter, o.label);
        } else {
            const qreal iconSize = t.scaled(ThemeMetric::IconSize);
            const qreal textH = o.label.isEmpty() ? 0 : QFontMetricsF(f).height();
            const qreal total = iconSize + (textH > 0 ? t.dp(4) + textH : 0);
            const qreal top = r.center().y() - total / 2;
            p.drawPixmap(QPointF(r.center().x() - iconSize / 2, top), m_ui.icons.pixmap(o.icon, iconSize, fg, dpr));
            if (textH > 0)
                p.drawText(QRectF(r.left(), top + iconSize + t.dp(4), r.width(), textH), Qt::AlignCenter, o.label);
        }
    }
}

void SegmentedControl::mouseReleaseEvent(QMouseEvent* e)
{
    for (int i = 0; i < m_options.size(); ++i) {
        if (segmentRect(i).contains(e->localPos())) {
            if (i != m_current) {
                m_current = i;
                update();
                emit currentChanged(i);
            }
            return;
        }
    }
}

} // namespace cb
