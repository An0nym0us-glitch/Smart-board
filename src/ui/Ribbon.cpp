#include "ui/Ribbon.h"

#include "ui/UiContext.h"
#include "ui/widgets/TouchButton.h"

#include <QHBoxLayout>
#include <QPainter>

namespace cb {

namespace {
class Separator : public QWidget
{
public:
    Separator(const UiContext& ui, QWidget* parent)
        : QWidget(parent)
        , m_ui(ui)
    {
        setFixedWidth(ui.theme.dpi(13));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Theme& t = m_ui.theme;
        p.setPen(QPen(t.color(ThemeColor::Separator), t.dp(1)));
        const int x = width() / 2;
        p.drawLine(x, t.dpi(16), x, height() - t.dpi(16));
    }

private:
    const UiContext& m_ui;
};
} // namespace

Ribbon::Ribbon(const UiContext& ui, QWidget* parent)
    : QWidget(parent)
    , m_ui(ui)
{
    const Theme& t = ui.theme;
    setFixedHeight(t.scaledInt(ThemeMetric::RibbonHeight));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(t.dpi(8), t.dpi(6), t.dpi(8), t.dpi(6));
    layout->setSpacing(t.dpi(4));

    m_more = addButton(QStringLiteral("more"), tr("More"));
    m_more->setCheckable(true);
    layout->addWidget(m_more);
    layout->addWidget(new Separator(ui, this));

    m_pen = addButton(QStringLiteral("pen"), tr("Pen"));
    m_eraser = addButton(QStringLiteral("eraser"), tr("Eraser"));
    m_select = addButton(QStringLiteral("select"), tr("Select"));
    m_toolChip = addButton(QString(), QString());
    for (TouchButton* b : {m_pen, m_eraser, m_select, m_toolChip}) {
        b->setCheckable(true);
        b->setHasOptions(true);
        layout->addWidget(b);
    }
    m_toolChip->setVisible(false);
    layout->addWidget(new Separator(ui, this));

    m_undo = addButton(QStringLiteral("undo"), tr("Undo"));
    m_redo = addButton(QStringLiteral("redo"), tr("Redo"));
    layout->addWidget(m_undo);
    layout->addWidget(m_redo);

    layout->addStretch(1);

    m_prev = new TouchButton(ui, QStringLiteral("chevron-left"), tr("Previous page"), TouchButton::Style::Icon, this);
    m_page = new TouchButton(ui, QString(), QStringLiteral("1 / 1"), TouchButton::Style::Pill, this);
    m_page->setToolTip(tr("Pages"));
    m_page->setMinimumWidth(t.dpi(120));
    m_next = new TouchButton(ui, QStringLiteral("chevron-right"), tr("Next page"), TouchButton::Style::Icon, this);
    m_newPage = addButton(QStringLiteral("page-add"), tr("Page"));
    layout->addWidget(m_prev);
    layout->addWidget(m_page);
    layout->addWidget(m_next);
    layout->addWidget(m_newPage);

    layout->addStretch(1);

    m_zoom = new TouchButton(ui, QString(), QStringLiteral("100%"), TouchButton::Style::Pill, this);
    m_zoom->setToolTip(tr("Zoom: tap to fit the page"));
    m_zoom->setMinimumWidth(t.dpi(84));
    m_fullScreen = new TouchButton(ui, QStringLiteral("fullscreen"), tr("Full screen"), TouchButton::Style::Icon, this);
    layout->addWidget(m_zoom);
    layout->addWidget(m_fullScreen);

    connect(m_more, &QAbstractButton::clicked, this, &Ribbon::moreClicked);
    connect(m_pen, &QAbstractButton::clicked, this, &Ribbon::penClicked);
    connect(m_eraser, &QAbstractButton::clicked, this, &Ribbon::eraserClicked);
    connect(m_select, &QAbstractButton::clicked, this, &Ribbon::selectClicked);
    connect(m_toolChip, &QAbstractButton::clicked, this, &Ribbon::toolChipClicked);
    connect(m_undo, &QAbstractButton::clicked, this, &Ribbon::undoClicked);
    connect(m_redo, &QAbstractButton::clicked, this, &Ribbon::redoClicked);
    connect(m_prev, &QAbstractButton::clicked, this, &Ribbon::previousPageClicked);
    connect(m_next, &QAbstractButton::clicked, this, &Ribbon::nextPageClicked);
    connect(m_page, &QAbstractButton::clicked, this, &Ribbon::pageIndicatorClicked);
    connect(m_newPage, &QAbstractButton::clicked, this, &Ribbon::newPageClicked);
    connect(m_zoom, &QAbstractButton::clicked, this, &Ribbon::zoomClicked);
    connect(m_fullScreen, &QAbstractButton::clicked, this, &Ribbon::fullScreenClicked);
}

TouchButton* Ribbon::addButton(const QString& icon, const QString& text)
{
    return new TouchButton(m_ui, icon, text, TouchButton::Style::Ribbon, this);
}

QSize Ribbon::sizeHint() const
{
    return QSize(800, m_ui.theme.scaledInt(ThemeMetric::RibbonHeight));
}

void Ribbon::setActiveTool(ToolId id)
{
    // Checkable buttons are driven by the tool state, not by clicks.
    m_pen->setChecked(id == ToolId::Pen);
    m_eraser->setChecked(id == ToolId::Eraser);
    m_select->setChecked(id == ToolId::Select);
    m_toolChip->setChecked(m_toolChip->isVisible() && id != ToolId::Pen && id != ToolId::Eraser && id != ToolId::Select);
}

void Ribbon::setToolChip(const QString& icon, const QString& label)
{
    m_toolChip->setIconName(icon);
    m_toolChip->setText(label);
    m_toolChip->setToolTip(label);
    m_toolChip->setVisible(!icon.isEmpty());
    m_toolChip->setChecked(!icon.isEmpty());
    m_toolChip->updateGeometry();
}

void Ribbon::setPenColor(const QColor& color)
{
    m_pen->setIndicatorColor(color);
}

void Ribbon::setPageInfo(int currentIndex, int count)
{
    m_page->setText(QStringLiteral("%1 / %2").arg(currentIndex + 1).arg(count));
    m_prev->setEnabled(currentIndex > 0);
    m_next->setEnabled(currentIndex + 1 < count);
    m_page->updateGeometry();
}

void Ribbon::setUndoRedo(bool canUndo, bool canRedo, const QString& undoText, const QString& redoText)
{
    m_undo->setEnabled(canUndo);
    m_redo->setEnabled(canRedo);
    m_undo->setToolTip(canUndo ? tr("Undo %1").arg(undoText) : tr("Undo"));
    m_redo->setToolTip(canRedo ? tr("Redo %1").arg(redoText) : tr("Redo"));
}

void Ribbon::setZoomPercent(int percent)
{
    m_zoom->setText(QStringLiteral("%1%").arg(percent));
}

void Ribbon::setFullScreen(bool on)
{
    m_fullScreen->setIconName(on ? QStringLiteral("fullscreen-exit") : QStringLiteral("fullscreen"));
}

void Ribbon::setMoreOpen(bool open)
{
    m_more->setChecked(open);
}

void Ribbon::paintEvent(QPaintEvent*)
{
    const Theme& t = m_ui.theme;
    QPainter p(this);
    p.fillRect(rect(), t.color(ThemeColor::Ribbon));
    p.setPen(QPen(t.color(ThemeColor::RibbonBorder), t.dp(1)));
    p.drawLine(0, 0, width(), 0);
}

} // namespace cb
