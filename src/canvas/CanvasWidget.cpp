#include "canvas/CanvasWidget.h"

#include "canvas/PageRenderer.h"
#include "core/Geometry.h"
#include "document/Commands.h"
#include "document/Document.h"
#include "geometry/InstrumentLayer.h"
#include "tools/ConstructTool.h"
#include "tools/EraserTool.h"
#include "tools/GraphTool.h"
#include "tools/MeasureTool.h"
#include "tools/PenTool.h"
#include "tools/SelectTool.h"
#include "tools/ShapeTool.h"
#include "tools/TextTool.h"
#include "tools/SelectionModel.h"
#include "tools/ToolController.h"
#include "tools/ToolSettings.h"
#include "ui/UiContext.h"

#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include <cmath>

namespace cb {

CanvasWidget::CanvasWidget(const UiContext& ui, Document& doc, ToolSettings& settings, QWidget* parent)
    : QWidget(parent)
    , m_ui(ui)
    , m_doc(doc)
    , m_settings(settings)
    , m_input(*this)
    , m_selection(std::make_unique<SelectionModel>())
    , m_instruments(std::make_unique<InstrumentLayer>())
    , m_tools(std::make_unique<ToolController>(*this))
{
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_TabletTracking, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);

    auto eraser = std::make_unique<EraserTool>(*this);
    m_eraser = eraser.get();
    m_tools->registerTool(std::make_unique<PenTool>(*this));
    m_tools->registerTool(std::move(eraser));
    m_tools->registerTool(std::make_unique<SelectTool>(*this));
    m_tools->registerTool(std::make_unique<ShapeTool>(*this));
    m_tools->registerTool(std::make_unique<TextTool>(*this));
    m_tools->registerTool(std::make_unique<MeasureTool>(*this));
    m_tools->registerTool(std::make_unique<ConstructTool>(*this));
    m_tools->registerTool(std::make_unique<GraphTool>(*this));
    m_tools->setActiveTool(ToolId::Pen);

    connect(m_tools.get(), &ToolController::activeToolChanged, this, [this](ToolId) {
        if (Tool* t = m_tools->activeTool())
            setCursor(t->cursor());
        update();
    });
    setCursor(m_tools->activeTool()->cursor());

    connect(&m_doc, &Document::objectAdded, this, &CanvasWidget::onObjectAdded);
    connect(&m_doc, &Document::objectRemoved, this, &CanvasWidget::onObjectRemoved);
    connect(&m_doc, &Document::objectChanged, this, &CanvasWidget::onObjectChanged);
    connect(&m_doc, &Document::pageChanged, this, &CanvasWidget::onPageChanged);
    connect(&m_doc, &Document::currentPageChanged, this, &CanvasWidget::onCurrentPageChanged);
    connect(&m_doc, &Document::documentReset, this, &CanvasWidget::onDocumentReset);

    connect(m_instruments.get(), &InstrumentLayer::changed, this, [this]() { update(); });
    connect(m_instruments.get(), &InstrumentLayer::statusText, this, &CanvasWidget::statusText);
    m_instruments->setUnits(m_doc.coordinates().pxPerUnit(), m_doc.coordinates().unitLabel());

    connect(m_selection.get(), &SelectionModel::changed, this, [this]() { update(); });
    connect(&m_settings, &ToolSettings::changed, this, [this]() {
        if (Tool* t = m_tools->activeTool())
            setCursor(t->cursor());
        update();
    });

    m_zoomSettle.setSingleShot(true);
    m_zoomSettle.setInterval(160);
    connect(&m_zoomSettle, &QTimer::timeout, this, &CanvasWidget::finishInteractiveZoom);
}

CanvasWidget::~CanvasWidget()
{
    // Tools may reference the selection/instruments: destroy them first.
    m_tools.reset();
}

Page* CanvasWidget::page() const
{
    return m_doc.currentPage();
}

const Theme& CanvasWidget::theme() const
{
    return m_ui.theme;
}

qreal CanvasWidget::uiScale() const
{
    return m_ui.theme.uiScale();
}

// ---------------------------------------------------------------------------------------- view

void CanvasWidget::fitPage()
{
    if (!page() || width() < 10 || height() < 10)
        return;
    m_view.fit(page()->frameRect(), QRectF(rect()), m_ui.theme.dp(20));
    m_viewInitialised = true;
    invalidateAll();
    viewUpdated();
}

void CanvasWidget::zoomBy(qreal factor)
{
    zoomView(QPointF(width() / 2.0, height() / 2.0), factor, false);
}

QPointF CanvasWidget::viewCenterInPage() const
{
    return m_view.viewToPage(QPointF(width() / 2.0, height() / 2.0));
}

QRect CanvasWidget::pageRectToWidget(const QRectF& pageRect) const
{
    return m_view.pageToView(pageRect).toAlignedRect();
}

void CanvasWidget::setShowPageFrame(bool on)
{
    m_showFrame = on;
    invalidateAll();
}

void CanvasWidget::ensureVisible(const QRectF& pageRect)
{
    const QRectF v = m_view.pageToView(pageRect);
    const QRectF area = QRectF(rect()).adjusted(40, 40, -40, -40);
    if (area.contains(v))
        return;
    QPointF delta;
    if (v.width() > area.width() || v.height() > area.height()) {
        delta = area.center() - v.center();
    } else {
        if (v.left() < area.left())
            delta.rx() = area.left() - v.left();
        else if (v.right() > area.right())
            delta.rx() = area.right() - v.right();
        if (v.top() < area.top())
            delta.ry() = area.top() - v.top();
        else if (v.bottom() > area.bottom())
            delta.ry() = area.bottom() - v.bottom();
    }
    m_view.panBy(delta);
    invalidateAll();
    viewUpdated();
}

void CanvasWidget::viewUpdated()
{
    if (page())
        m_pageViews.insert(page()->id(), m_view);
    emit zoomChanged(m_view.zoom());
    emit viewChanged();
    update();
}

void CanvasWidget::panView(const QPointF& delta)
{
    const qreal dpr = devicePixelRatioF();
    const QPoint d = (delta * dpr).toPoint();
    if (d.isNull())
        return;
    const QPointF logical = QPointF(d) / dpr;
    m_view.panBy(logical);
    const bool cacheUsable = !m_cache.isNull() && !m_interactiveZoom && m_cacheView.zoom() == m_view.zoom();
    if (cacheUsable) {
        QRegion exposed;
        m_cache.scroll(d.x(), d.y(), m_cache.rect(), &exposed);
        m_cacheView = m_view;
        m_dirty.translate(logical.toPoint());
        for (const QRect& r : exposed) {
            const QRectF lr(QPointF(r.topLeft()) / dpr, QSizeF(r.size()) / dpr);
            m_dirty += lr.toAlignedRect().adjusted(-2, -2, 2, 2);
        }
    } else if (!m_interactiveZoom) {
        invalidateAll();
    }
    viewUpdated();
}

void CanvasWidget::zoomView(const QPointF& viewPos, qreal factor, bool interactive)
{
    m_view.zoomAt(viewPos, factor);
    if (interactive) {
        m_interactiveZoom = true;
        m_zoomSettle.start();
    } else {
        m_interactiveZoom = false;
        invalidateAll();
    }
    viewUpdated();
}

void CanvasWidget::finishInteractiveZoom()
{
    if (m_gestureTarget == GestureTarget::Canvas)
        return; // still pinching
    m_interactiveZoom = false;
    invalidateAll();
    update();
}

// ------------------------------------------------------------------------------------ rendering

void CanvasWidget::invalidatePageRect(const QRectF& pageRect)
{
    if (pageRect.isNull())
        return;
    const QRect r = m_cacheView.pageToView(pageRect).toAlignedRect().adjusted(-3, -3, 3, 3) & rect();
    if (r.isEmpty())
        return;
    m_dirty += r;
    update(m_view.pageToView(pageRect).toAlignedRect().adjusted(-3, -3, 3, 3));
}

void CanvasWidget::invalidateAll()
{
    m_dirty = QRegion(rect());
    m_cacheView = m_view;
    update();
}

void CanvasWidget::renderDirty()
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize = size() * dpr;
    if (m_cache.size() != pixelSize) {
        m_cache = QPixmap(pixelSize);
        m_cache.setDevicePixelRatio(dpr);
        m_dirty = QRegion(rect());
        m_cacheView = m_view;
    }
    if (m_dirty.isEmpty() || m_interactiveZoom)
        return;
    Page* p = page();
    QPainter painter(&m_cache);
    painter.setClipRegion(m_dirty);
    painter.fillRect(m_dirty.boundingRect(), p ? p->background().background : m_ui.theme.color(ThemeColor::Window));
    if (p) {
        painter.setTransform(m_cacheView.toTransform());
        const QRectF pageArea = m_cacheView.viewToPage(QRectF(m_dirty.boundingRect())).adjusted(-2, -2, 2, 2);
        PageRenderer::Options options;
        options.hidden = &m_hidden;
        PageRenderer::render(painter, *p, pageArea, m_cacheView.zoom(), m_doc.images(), m_doc.coordinates(), options);
        if (m_showFrame) {
            QPen framePen(QColor(255, 255, 255, 60), 1.5, Qt::DashLine);
            framePen.setCosmetic(true);
            painter.setPen(framePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(p->frameRect());
        }
    }
    painter.end();
    m_dirty = QRegion();
}

void CanvasWidget::paintEvent(QPaintEvent* event)
{
    renderDirty();
    QPainter painter(this);
    Page* p = page();
    const QColor bg = p ? p->background().background : m_ui.theme.color(ThemeColor::Window);
    if (m_interactiveZoom && m_cacheView.zoom() > 0) {
        // Fast preview while pinching/wheeling: scale the cached layer, re-render when settled.
        painter.fillRect(event->rect(), bg);
        const qreal s = m_view.zoom() / m_cacheView.zoom();
        painter.save();
        painter.translate(m_view.offset() - m_cacheView.offset() * s);
        painter.scale(s, s);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(QPointF(0, 0), m_cache);
        painter.restore();
    } else {
        for (const QRect& r : event->region()) {
            const qreal dpr = devicePixelRatioF();
            const QRectF source(QPointF(r.topLeft()) * dpr, QSizeF(r.size()) * dpr);
            painter.drawPixmap(QRectF(r), m_cache, source);
        }
    }

    // Page-space overlays: live ink, previews, instruments.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.save();
    painter.setTransform(m_view.toTransform());
    m_tools->paintOverlay(painter);
    m_instruments->paint(painter, m_view.zoom(), &m_doc.coordinates());
    painter.restore();

    // View-space overlays: handles, palm eraser.
    m_tools->paintViewOverlay(painter);
    if (m_gestureTarget == GestureTarget::Palm) {
        const QPointF c = m_view.pageToView(m_palmCenter);
        const qreal r = m_palmRadius * m_view.zoom();
        painter.setPen(QPen(QColor(255, 255, 255, 170), m_ui.theme.dp(2), Qt::DashLine));
        painter.setBrush(QColor(255, 255, 255, 30));
        painter.drawEllipse(c, r, r);
    }
}

void CanvasWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_viewInitialised)
        fitPage();
    invalidateAll();
    emit viewChanged();
}

void CanvasWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (QScreen* s = screen()) {
        const qreal ppm = s->physicalDotsPerInch() / 25.4 / std::max(1.0, devicePixelRatioF());
        m_input.setPixelsPerMm(ppm);
    }
}

void CanvasWidget::updateOverlay(const QRectF& pageRect)
{
    update(m_view.pageToView(pageRect).toAlignedRect().adjusted(-4, -4, 4, 4));
}

void CanvasWidget::setObjectHidden(const ObjectId& id, bool hidden)
{
    if (hidden)
        m_hidden.insert(id);
    else
        m_hidden.remove(id);
    if (Page* p = page())
        if (DocumentObject* o = p->object(id))
            invalidatePageRect(o->sceneBounds());
}

void CanvasWidget::requestEdit(const ObjectId& id)
{
    emit editRequested(id);
}

// ------------------------------------------------------------------------------ document events

void CanvasWidget::onObjectAdded(const QUuid& pageId, const QUuid&, const QRectF& bounds)
{
    if (page() && page()->id() == pageId)
        invalidatePageRect(bounds);
}

void CanvasWidget::onObjectRemoved(const QUuid& pageId, const QUuid& objectId, const QRectF& bounds)
{
    if (page() && page()->id() == pageId) {
        invalidatePageRect(bounds);
        m_selection->remove(objectId);
    }
}

void CanvasWidget::onObjectChanged(const QUuid& pageId, const QUuid&, const QRectF& oldBounds, const QRectF& newBounds)
{
    if (page() && page()->id() == pageId) {
        invalidatePageRect(oldBounds);
        invalidatePageRect(newBounds);
    }
}

void CanvasWidget::onPageChanged(const QUuid& pageId)
{
    if (page() && page()->id() == pageId)
        invalidateAll();
}

void CanvasWidget::onCurrentPageChanged(int)
{
    Page* p = page();
    if (!p || p->id() == m_viewPage)
        return;
    m_input.cancelAll();
    m_tools->pageChanged();
    m_selection->clear();
    m_hidden.clear();
    m_viewPage = p->id();
    const auto it = m_pageViews.constFind(p->id());
    if (it != m_pageViews.constEnd()) {
        m_view = *it;
        invalidateAll();
        viewUpdated();
    } else {
        fitPage();
    }
}

void CanvasWidget::onDocumentReset()
{
    m_input.cancelAll();
    m_tools->pageChanged();
    m_selection->clear();
    m_hidden.clear();
    m_pageViews.clear();
    m_viewPage = page() ? page()->id() : PageId();
    m_instruments->setUnits(m_doc.coordinates().pxPerUnit(), m_doc.coordinates().unitLabel());
    fitPage();
}

// ----------------------------------------------------------------------------------------- input

bool CanvasWidget::event(QEvent* event)
{
    if (m_input.handleEvent(event))
        return true;
    return QWidget::event(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent* event)
{
    if (m_tools->keyPress(event))
        return;
    QWidget::keyPressEvent(event);
}

void CanvasWidget::commitInstrumentResult(InstrumentResult& result)
{
    if (result.objects.empty() || !page())
        return;
    const QString text = result.text.isEmpty() ? tr("Construct") : result.text;
    m_doc.commands().push(std::make_unique<AddObjectsCommand>(page()->id(), std::move(result.objects), text));
}

void CanvasWidget::pointerEvent(PointerEvent& e)
{
    e.pagePos = m_view.viewToPage(e.viewPos);
    if (e.phase == PointerPhase::Down) {
        setFocus(Qt::MouseFocusReason);
        m_lastPress = e.pagePos;
    }
    switch (e.phase) {
    case PointerPhase::Down: {
        if (!m_instruments->isEmpty()) {
            const bool inking = m_tools->activeToolId() == ToolId::Pen && e.device != PointerDevice::StylusEraser;
            const bool nearEdge = inking
                && m_instruments->edgeConstraint(e.pagePos, viewToPageLength(m_ui.theme.dp(26)), nullptr);
            if (!nearEdge && m_instruments->pointerDown(e.pointerId, e.pagePos, viewToPageLength(m_ui.theme.dp(6)))) {
                m_instrumentPointers.insert(e.pointerId);
                update();
                return;
            }
        }
        m_tools->pointerEvent(e);
        return;
    }
    case PointerPhase::Move:
        if (m_instrumentPointers.contains(e.pointerId)) {
            m_instruments->pointerMove(e.pointerId, e.pagePos);
            return;
        }
        m_tools->pointerEvent(e);
        return;
    case PointerPhase::Up:
        if (m_instrumentPointers.remove(e.pointerId)) {
            InstrumentResult r = m_instruments->pointerUp(e.pointerId);
            commitInstrumentResult(r);
            update();
            emit interactionFinished();
            return;
        }
        m_tools->pointerEvent(e);
        emit interactionFinished();
        return;
    case PointerPhase::Cancel:
        if (m_instrumentPointers.remove(e.pointerId)) {
            m_instruments->pointerCancel(e.pointerId);
            return;
        }
        m_tools->pointerEvent(e);
        return;
    case PointerPhase::Hover:
        m_tools->pointerEvent(e);
        return;
    }
}

void CanvasWidget::gestureEvent(const GestureEvent& g)
{
    const QPointF pageCentroid = m_view.viewToPage(g.centroid);
    if (g.type == GestureType::PalmErase) {
        const qreal radius = viewToPageLength(g.radius);
        switch (g.phase) {
        case GesturePhase::Begin:
            m_gestureTarget = GestureTarget::Palm;
            m_eraser->beginPalm(pageCentroid, radius);
            break;
        case GesturePhase::Update:
            m_eraser->movePalm(pageCentroid, radius);
            break;
        case GesturePhase::End:
        case GesturePhase::Cancel:
            m_eraser->endPalm();
            m_gestureTarget = GestureTarget::None;
            break;
        }
        m_palmCenter = pageCentroid;
        m_palmRadius = radius;
        update();
        return;
    }

    // Pan / zoom / rotate.
    switch (g.phase) {
    case GesturePhase::Begin:
        m_gestureStartView = m_view;
        m_gestureInstrument = m_instruments->instrumentAt(pageCentroid, viewToPageLength(m_ui.theme.dp(10)));
        if (m_gestureInstrument) {
            m_gestureTarget = GestureTarget::Instrument;
        } else if (m_tools->activeTool() && m_tools->activeTool()->gesture(g, pageCentroid)) {
            m_gestureTarget = GestureTarget::Tool;
        } else {
            m_gestureTarget = GestureTarget::Canvas;
        }
        return;
    case GesturePhase::Update:
        switch (m_gestureTarget) {
        case GestureTarget::Instrument:
            if (m_gestureInstrument) {
                m_gestureInstrument->applyGesture(pageCentroid, g.panDelta / m_view.zoom(), g.rotationDelta);
                update();
            }
            break;
        case GestureTarget::Tool:
            m_tools->activeTool()->gesture(g, pageCentroid);
            break;
        case GestureTarget::Canvas:
            if (!qFuzzyCompare(g.scaleDelta, 1.0)) {
                m_view.zoomAt(g.centroid, g.scaleDelta);
                m_view.panBy(g.panDelta);
                m_interactiveZoom = true;
                viewUpdated();
            } else if (m_interactiveZoom) {
                m_view.panBy(g.panDelta);
                viewUpdated();
            } else {
                panView(g.panDelta);
            }
            break;
        default:
            break;
        }
        return;
    case GesturePhase::End:
    case GesturePhase::Cancel:
        if (m_gestureTarget == GestureTarget::Tool && m_tools->activeTool())
            m_tools->activeTool()->gesture(g, pageCentroid);
        if (m_gestureTarget == GestureTarget::Canvas && g.phase == GesturePhase::Cancel) {
            m_view = m_gestureStartView;
            viewUpdated();
        }
        m_gestureTarget = GestureTarget::None;
        m_gestureInstrument = nullptr;
        m_interactiveZoom = false;
        invalidateAll();
        return;
    }
}

void CanvasWidget::wheelZoom(const QPointF& viewPos, qreal factor)
{
    zoomView(viewPos, factor, true);
}

void CanvasWidget::wheelPan(const QPointF& viewDelta)
{
    panView(viewDelta);
}

void CanvasWidget::dragPan(const QPointF& viewDelta)
{
    panView(viewDelta);
}

} // namespace cb
