#include "input/InputManager.h"

#include "core/Geometry.h"

#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace cb {

namespace {
constexpr qint64 kYoungStrokeMs = 280;     // a stroke this young is cancelled by a second finger
constexpr qreal kYoungStrokeMm = 7.0;      // ... or one that moved less than this
constexpr qint64 kPalmWindowMs = 450;      // fingers of a palm gesture must land within this window
constexpr qreal kPalmContactMm = 24.0;     // contact diameter considered a palm/fist
constexpr qreal kHandSpanMm = 190.0;       // maximum distance of fingers from the centroid
constexpr qreal kMinPalmRadiusMm = 14.0;
} // namespace

InputManager::InputManager(InputSink& sink)
    : m_sink(sink)
{
    m_clock.start();
}

bool InputManager::handleEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::MouseButtonDblClick:
        return handleMouse(static_cast<QMouseEvent*>(event));
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
        return handleTablet(static_cast<QTabletEvent*>(event));
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        return handleTouch(static_cast<QTouchEvent*>(event));
    case QEvent::Wheel:
        return handleWheel(static_cast<QWheelEvent*>(event));
    default:
        return false;
    }
}

void InputManager::cancelAll()
{
    if (m_mouseDown) {
        PointerEvent pe;
        pe.pointerId = kMousePointerId;
        pe.phase = PointerPhase::Cancel;
        pe.viewPos = m_lastMousePos;
        m_sink.pointerEvent(pe);
        m_mouseDown = false;
    }
    m_mousePanning = false;
    if (m_stylusDown) {
        PointerEvent pe;
        pe.pointerId = kStylusPointerId;
        pe.device = m_stylusDevice;
        pe.phase = PointerPhase::Cancel;
        m_sink.pointerEvent(pe);
        m_stylusDown = false;
    }
    switch (m_mode) {
    case TouchMode::Pointer:
    case TouchMode::MultiDraw:
        for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it)
            if (it->forwarded)
                forwardTouch(it.key(), PointerPhase::Cancel, *it, Qt::NoModifier);
        break;
    case TouchMode::PanZoom: {
        GestureEvent g;
        g.type = GestureType::PanZoom;
        g.phase = GesturePhase::Cancel;
        m_sink.gestureEvent(g);
        break;
    }
    case TouchMode::PalmErase:
        updatePalm(GesturePhase::End);
        break;
    default:
        break;
    }
    m_tracks.clear();
    m_mode = TouchMode::None;
    m_primaryId = -1;
}

// ------------------------------------------------------------------------------------------ mouse

bool InputManager::handleMouse(QMouseEvent* e)
{
    // Touch and pen input are handled natively; ignore the mouse events the OS/Qt synthesises.
    if (e->source() != Qt::MouseEventNotSynthesized) {
        e->accept();
        return true;
    }
    const QPointF pos = e->localPos();
    PointerEvent pe;
    pe.pointerId = kMousePointerId;
    pe.device = PointerDevice::Mouse;
    pe.viewPos = pos;
    pe.modifiers = e->modifiers();
    pe.timestamp = e->timestamp();

    switch (e->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
            m_mousePanning = true;
            m_lastMousePos = pos;
        } else if (e->button() == Qt::LeftButton && !m_mouseDown) {
            m_mouseDown = true;
            pe.phase = PointerPhase::Down;
            m_sink.pointerEvent(pe);
        }
        break;
    case QEvent::MouseMove:
        if (m_mousePanning) {
            m_sink.dragPan(pos - m_lastMousePos);
        } else {
            pe.phase = m_mouseDown ? PointerPhase::Move : PointerPhase::Hover;
            m_sink.pointerEvent(pe);
        }
        break;
    case QEvent::MouseButtonRelease:
        if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
            m_mousePanning = false;
        } else if (e->button() == Qt::LeftButton && m_mouseDown) {
            m_mouseDown = false;
            pe.phase = PointerPhase::Up;
            m_sink.pointerEvent(pe);
        }
        break;
    default:
        break;
    }
    m_lastMousePos = pos;
    e->accept();
    return true;
}

// ----------------------------------------------------------------------------------------- stylus

bool InputManager::handleTablet(QTabletEvent* e)
{
    PointerEvent pe;
    pe.pointerId = kStylusPointerId;
    pe.device = e->pointerType() == QTabletEvent::Eraser ? PointerDevice::StylusEraser : PointerDevice::Stylus;
    pe.viewPos = e->posF();
    pe.pressure = std::clamp(e->pressure(), 0.0, 1.0);
    pe.hasPressure = true;
    pe.modifiers = e->modifiers();
    pe.timestamp = e->timestamp();

    switch (e->type()) {
    case QEvent::TabletPress:
        if (!m_stylusDown) {
            m_stylusDown = true;
            m_stylusDevice = pe.device;
            pe.phase = PointerPhase::Down;
            m_sink.pointerEvent(pe);
        }
        break;
    case QEvent::TabletMove:
        if (m_stylusDown) {
            pe.device = m_stylusDevice;
            pe.phase = PointerPhase::Move;
        } else {
            pe.phase = PointerPhase::Hover;
        }
        m_sink.pointerEvent(pe);
        break;
    case QEvent::TabletRelease:
        if (m_stylusDown) {
            m_stylusDown = false;
            pe.device = m_stylusDevice;
            pe.phase = PointerPhase::Up;
            m_sink.pointerEvent(pe);
        }
        break;
    default:
        break;
    }
    e->accept();
    return true;
}

// ------------------------------------------------------------------------------------------ wheel

bool InputManager::handleWheel(QWheelEvent* e)
{
    const QPoint angle = e->angleDelta();
    const QPoint pixels = e->pixelDelta();
    if (e->modifiers() & Qt::ShiftModifier) {
        const int d = angle.y() != 0 ? angle.y() : angle.x();
        m_sink.wheelPan(QPointF(d * 0.6, 0));
    } else if (!pixels.isNull() && !(e->modifiers() & Qt::ControlModifier)) {
        m_sink.wheelPan(QPointF(pixels));
    } else if (angle.y() != 0) {
        m_sink.wheelZoom(e->position(), std::pow(1.0015, angle.y()));
    }
    e->accept();
    return true;
}

// ------------------------------------------------------------------------------------------ touch

void InputManager::forwardTouch(int id, PointerPhase phase, const Track& t, Qt::KeyboardModifiers mods)
{
    PointerEvent pe;
    pe.pointerId = kTouchPointerBase + id;
    pe.device = PointerDevice::Touch;
    pe.phase = phase;
    pe.viewPos = t.pos;
    pe.modifiers = mods;
    pe.timestamp = static_cast<unsigned long>(m_clock.elapsed());
    m_sink.pointerEvent(pe);
}

QPointF InputManager::centroid() const
{
    QPointF c;
    if (m_tracks.isEmpty())
        return c;
    for (const Track& t : m_tracks)
        c += t.pos;
    return c / m_tracks.size();
}

qreal InputManager::spread(const QPointF& c) const
{
    if (m_tracks.isEmpty())
        return 0.0;
    qreal s = 0.0;
    for (const Track& t : m_tracks)
        s += geom::distance(t.pos, c);
    return s / m_tracks.size();
}

qreal InputManager::angle(const QPointF& c) const
{
    Q_UNUSED(c);
    if (m_tracks.size() < 2)
        return 0.0;
    QList<int> ids = m_tracks.keys();
    std::sort(ids.begin(), ids.end());
    return geom::angleDeg(m_tracks.value(ids[1]).pos - m_tracks.value(ids[0]).pos);
}

qreal InputManager::palmRadius(const QPointF& c) const
{
    qreal r = kMinPalmRadiusMm * m_pixelsPerMm;
    for (const Track& t : m_tracks)
        r = std::max(r, geom::distance(t.pos, c) + t.diameter * 0.5 + 6.0 * m_pixelsPerMm);
    return r;
}

bool InputManager::isPalmContact(const Track& t) const
{
    return t.diameter >= kPalmContactMm * m_pixelsPerMm;
}

bool InputManager::looksLikeHand() const
{
    const QPointF c = centroid();
    for (const Track& t : m_tracks)
        if (geom::distance(t.pos, c) > kHandSpanMm * m_pixelsPerMm)
            return false;
    return true;
}

void InputManager::rebaseline()
{
    m_prevCentroid = centroid();
    m_prevSpread = spread(m_prevCentroid);
    m_prevAngle = angle(m_prevCentroid);
}

void InputManager::beginPanZoom()
{
    m_mode = TouchMode::PanZoom;
    rebaseline();
    GestureEvent g;
    g.type = GestureType::PanZoom;
    g.phase = GesturePhase::Begin;
    g.centroid = m_prevCentroid;
    g.touchCount = m_tracks.size();
    m_sink.gestureEvent(g);
}

void InputManager::updatePanZoom()
{
    const QPointF c = centroid();
    const qreal s = spread(c);
    const qreal a = angle(c);
    GestureEvent g;
    g.type = GestureType::PanZoom;
    g.phase = GesturePhase::Update;
    g.centroid = c;
    g.panDelta = c - m_prevCentroid;
    g.scaleDelta = (m_prevSpread > 8.0 && s > 8.0) ? s / m_prevSpread : 1.0;
    g.rotationDelta = m_tracks.size() >= 2 ? geom::angleDifference(m_prevAngle, a) : 0.0;
    g.touchCount = m_tracks.size();
    m_prevCentroid = c;
    m_prevSpread = s;
    m_prevAngle = a;
    m_sink.gestureEvent(g);
}

void InputManager::beginPalm(Qt::KeyboardModifiers mods)
{
    Q_UNUSED(mods);
    m_mode = TouchMode::PalmErase;
    updatePalm(GesturePhase::Begin);
}

void InputManager::updatePalm(GesturePhase phase)
{
    GestureEvent g;
    g.type = GestureType::PalmErase;
    g.phase = phase;
    g.centroid = m_tracks.isEmpty() ? m_prevCentroid : centroid();
    g.radius = palmRadius(g.centroid);
    g.touchCount = m_tracks.size();
    m_prevCentroid = g.centroid;
    m_sink.gestureEvent(g);
}

bool InputManager::handleTouch(QTouchEvent* e)
{
    e->accept();
    if (e->type() == QEvent::TouchCancel) {
        cancelAll();
        return true;
    }
    const qint64 now = m_clock.elapsed();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const auto& points = e->touchPoints();

    // 1. Update positions of known tracks.
    bool anyMoved = false;
    for (const QTouchEvent::TouchPoint& tp : points) {
        auto it = m_tracks.find(tp.id());
        if (it == m_tracks.end())
            continue;
        if (it->pos != tp.pos()) {
            it->pos = tp.pos();
            anyMoved = true;
        }
        const QSizeF d = tp.ellipseDiameters();
        it->diameter = std::max(it->diameter, std::max(d.width(), d.height()));
    }

    // 2. New contacts.
    for (const QTouchEvent::TouchPoint& tp : points) {
        if (tp.state() != Qt::TouchPointPressed || m_tracks.contains(tp.id()))
            continue;
        Track t;
        t.start = tp.pos();
        t.pos = tp.pos();
        t.startTime = now;
        const QSizeF d = tp.ellipseDiameters();
        t.diameter = std::max(d.width(), d.height());
        m_tracks.insert(tp.id(), t);

        switch (m_mode) {
        case TouchMode::None:
            m_firstTouchTime = now;
            if (m_palmEnabled && isPalmContact(t)) {
                beginPalm(mods);
            } else if (m_multiUser) {
                m_mode = TouchMode::MultiDraw;
                m_tracks[tp.id()].forwarded = true;
                forwardTouch(tp.id(), PointerPhase::Down, t, mods);
            } else {
                m_mode = TouchMode::Pointer;
                m_primaryId = tp.id();
                m_tracks[tp.id()].forwarded = true;
                forwardTouch(tp.id(), PointerPhase::Down, t, mods);
            }
            break;
        case TouchMode::Pointer: {
            const Track primary = m_tracks.value(m_primaryId);
            const bool young = (now - primary.startTime) < kYoungStrokeMs
                || geom::distance(primary.start, primary.pos) < kYoungStrokeMm * m_pixelsPerMm;
            if (young) {
                forwardTouch(m_primaryId, PointerPhase::Cancel, primary, mods);
                m_tracks[m_primaryId].forwarded = false;
                m_primaryId = -1;
                if (m_palmEnabled && isPalmContact(t))
                    beginPalm(mods);
                else
                    beginPanZoom();
            }
            // Otherwise the extra finger is ignored so an ongoing stroke is not disturbed.
            break;
        }
        case TouchMode::MultiDraw:
            m_tracks[tp.id()].forwarded = true;
            forwardTouch(tp.id(), PointerPhase::Down, t, mods);
            break;
        case TouchMode::PanZoom:
            if (m_palmEnabled && m_tracks.size() >= 4 && (now - m_firstTouchTime) < kPalmWindowMs && looksLikeHand()) {
                GestureEvent g;
                g.type = GestureType::PanZoom;
                g.phase = GesturePhase::Cancel;
                m_sink.gestureEvent(g);
                beginPalm(mods);
            } else {
                rebaseline();
            }
            break;
        case TouchMode::PalmErase:
        case TouchMode::WaitRelease:
            break;
        }
    }

    // 3. Movement.
    if (anyMoved) {
        switch (m_mode) {
        case TouchMode::Pointer:
            if (m_primaryId >= 0 && m_tracks.contains(m_primaryId))
                forwardTouch(m_primaryId, PointerPhase::Move, m_tracks.value(m_primaryId), mods);
            break;
        case TouchMode::MultiDraw:
            for (const QTouchEvent::TouchPoint& tp : points) {
                if (tp.state() != Qt::TouchPointMoved)
                    continue;
                auto it = m_tracks.constFind(tp.id());
                if (it != m_tracks.constEnd() && it->forwarded)
                    forwardTouch(tp.id(), PointerPhase::Move, *it, mods);
            }
            break;
        case TouchMode::PanZoom:
            updatePanZoom();
            break;
        case TouchMode::PalmErase:
            updatePalm(GesturePhase::Update);
            break;
        default:
            break;
        }
    }

    // 4. Released contacts.
    for (const QTouchEvent::TouchPoint& tp : points) {
        if (tp.state() != Qt::TouchPointReleased)
            continue;
        auto it = m_tracks.find(tp.id());
        if (it == m_tracks.end())
            continue;
        const Track t = *it;
        it->pos = tp.pos();
        switch (m_mode) {
        case TouchMode::Pointer:
            if (tp.id() == m_primaryId) {
                Track released = t;
                released.pos = tp.pos();
                forwardTouch(tp.id(), PointerPhase::Up, released, mods);
                m_primaryId = -1;
            }
            m_tracks.remove(tp.id());
            m_mode = m_tracks.isEmpty() ? TouchMode::None : TouchMode::WaitRelease;
            if (m_primaryId >= 0)
                m_mode = TouchMode::Pointer;
            break;
        case TouchMode::MultiDraw:
            if (t.forwarded) {
                Track released = t;
                released.pos = tp.pos();
                forwardTouch(tp.id(), PointerPhase::Up, released, mods);
            }
            m_tracks.remove(tp.id());
            if (m_tracks.isEmpty())
                m_mode = TouchMode::None;
            break;
        case TouchMode::PanZoom:
            m_tracks.remove(tp.id());
            if (m_tracks.size() < 2) {
                GestureEvent g;
                g.type = GestureType::PanZoom;
                g.phase = GesturePhase::End;
                g.centroid = m_prevCentroid;
                m_sink.gestureEvent(g);
                m_mode = m_tracks.isEmpty() ? TouchMode::None : TouchMode::WaitRelease;
            } else {
                rebaseline();
            }
            break;
        case TouchMode::PalmErase:
            m_tracks.remove(tp.id());
            if (m_tracks.isEmpty()) {
                updatePalm(GesturePhase::End);
                m_mode = TouchMode::None;
            }
            break;
        case TouchMode::WaitRelease:
        case TouchMode::None:
            m_tracks.remove(tp.id());
            if (m_tracks.isEmpty())
                m_mode = TouchMode::None;
            break;
        }
    }
    if (e->type() == QEvent::TouchEnd && !m_tracks.isEmpty()) {
        // Some drivers end the sequence without releasing every point.
        cancelAll();
    }
    return true;
}

} // namespace cb
