#include "input/InputManager.h"

#include "core/Geometry.h"
#include "input/PalmGesture.h"

#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cb {

namespace {
// A second finger turns a stroke into pan/zoom only while the stroke is "young": it has barely
// moved (a finger resting before a pinch), or it started a moment ago and is still short (two
// fingers of a pinch landing a few frames apart). A line that is really being drawn is never
// interrupted.
constexpr qreal kYoungStrokeMm = 7.0;      // moved less than this: always young
constexpr qint64 kYoungStrokeMs = 120;     // started less than this ago ...
constexpr qreal kYoungStrokeMaxMm = 20.0;  // ... and moved less than this: young
// The wipe eraser thresholds are in input/PalmGesture.h.
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
    default:
        break;
    }
    // What was wiped so far stays (one undo step).
    endPalm();
    m_tracks.clear();
    m_mode = TouchMode::None;
    m_primaryId = -1;
}

bool InputManager::stylusActive() const
{
    return m_stylusDown || (m_lastStylusTime >= 0 && m_clock.elapsed() - m_lastStylusTime < kStylusProximityMs);
}

InputManager::TouchState InputManager::touchState() const
{
    if (palmActive())
        return TouchState::PalmErase;
    switch (m_mode) {
    case TouchMode::None: return TouchState::Idle;
    case TouchMode::Pointer: return TouchState::Drawing;
    case TouchMode::MultiDraw: return TouchState::MultiDrawing;
    case TouchMode::PanZoom: return TouchState::PanZoom;
    case TouchMode::PalmErase: return TouchState::PalmErase;
    case TouchMode::WaitRelease: return TouchState::Ignoring;
    }
    return TouchState::Idle;
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
    m_lastStylusTime = m_clock.elapsed();

    switch (e->type()) {
    case QEvent::TabletPress:
        if (!m_stylusDown) {
            // Pen priority: a hand that touched the board just before the pen must not leave ink or
            // keep zooming. Young finger strokes are cancelled; the fingers are ignored until lifted.
            if (m_mode == TouchMode::Pointer || m_mode == TouchMode::MultiDraw) {
                for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
                    if (it->forwarded) {
                        forwardTouch(it.key(), PointerPhase::Cancel, *it, Qt::NoModifier);
                        it->forwarded = false;
                    }
                }
                endPalm(); // a wipe running beside multi-user drawing
                m_primaryId = -1;
                m_mode = TouchMode::WaitRelease;
            } else if (m_mode == TouchMode::PanZoom) {
                GestureEvent g;
                g.type = GestureType::PanZoom;
                g.phase = GesturePhase::End;
                g.centroid = m_prevCentroid;
                m_sink.gestureEvent(g);
                m_mode = TouchMode::WaitRelease;
            } else if (m_mode == TouchMode::PalmErase) {
                endPalm();
                m_mode = TouchMode::WaitRelease;
            }
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

QPointF InputManager::palmCentroid() const
{
    QPointF c;
    int n = 0;
    for (int id : m_palmIds) {
        auto it = m_tracks.constFind(id);
        if (it == m_tracks.constEnd())
            continue;
        c += it->pos;
        ++n;
    }
    return n > 0 ? c / n : m_prevCentroid;
}

qreal InputManager::palmRadius(const QPointF& c) const
{
    qreal r = PALM_ERASER_MIN_RADIUS_MM * m_pixelsPerMm;
    for (int id : m_palmIds) {
        auto it = m_tracks.constFind(id);
        if (it != m_tracks.constEnd())
            r = std::max(r, geom::distance(it->pos, c) + it->diameter * 0.5 + PALM_ERASER_RADIUS_MARGIN_MM * m_pixelsPerMm);
    }
    return r;
}

bool InputManager::isPalmContact(const Track& t) const
{
    return t.diameter >= PALM_ERASER_PALM_CONTACT_MM * m_pixelsPerMm;
}

QVector<int> InputManager::findPalmGroup(int newId, qint64 now) const
{
    // The new finger plus the nearest fingers that landed with it (within the group window) and
    // have not travelled yet. Only if those are packed tightly together is it a wipe; fingers
    // spread apart stay writing / pinching.
    if (!m_palmEnabled || palmActive() || PALM_ERASER_FINGER_COUNT < 2)
        return {};
    auto added = m_tracks.constFind(newId);
    if (added == m_tracks.constEnd())
        return {};
    struct Candidate
    {
        int id;
        qreal distance;
    };
    std::vector<Candidate> candidates;
    for (auto it = m_tracks.constBegin(); it != m_tracks.constEnd(); ++it) {
        if (it.key() == newId)
            continue;
        if (now - it->startTime > PALM_ERASER_GROUP_WINDOW_MS)
            continue;
        if (geom::distance(it->start, it->pos) > PALM_ERASER_MAX_TRAVEL_MM * m_pixelsPerMm)
            continue;
        candidates.push_back({it.key(), geom::distance(it->pos, added->pos)});
    }
    const int needed = PALM_ERASER_FINGER_COUNT - 1;
    if (static_cast<int>(candidates.size()) < needed)
        return {};
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    QVector<int> ids{newId};
    QVector<QPointF> positions{added->pos};
    for (int i = 0; i < needed; ++i) {
        ids << candidates[i].id;
        positions << m_tracks.value(candidates[i].id).pos;
    }
    if (!isTightPalmGroup(measurePalmGroup(positions), m_pixelsPerMm))
        return {};
    return ids;
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

void InputManager::beginPalm(const QVector<int>& ids)
{
    // Fingers of the group that were already drawing (multi-user mode) leave no ink.
    for (int id : ids) {
        auto it = m_tracks.find(id);
        if (it != m_tracks.end() && it->forwarded) {
            forwardTouch(id, PointerPhase::Cancel, *it, Qt::NoModifier);
            it->forwarded = false;
        }
        if (id == m_primaryId)
            m_primaryId = -1;
    }
    m_palmIds = ids;
    // In multi-user mode the other fingers keep drawing next to the wipe.
    m_mode = m_multiUser ? TouchMode::MultiDraw : TouchMode::PalmErase;
    updatePalm(GesturePhase::Begin);
}

void InputManager::updatePalm(GesturePhase phase)
{
    GestureEvent g;
    g.type = GestureType::PalmErase;
    g.phase = phase;
    g.centroid = palmCentroid();
    g.radius = palmRadius(g.centroid);
    g.touchCount = m_palmIds.size();
    m_prevCentroid = g.centroid;
    m_sink.gestureEvent(g);
}

void InputManager::endPalm()
{
    if (!palmActive())
        return;
    updatePalm(GesturePhase::End);
    m_palmIds.clear();
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
            if (stylusActive()) {
                // The pen is writing or hovering: this is the writing hand resting on the board.
                m_mode = TouchMode::WaitRelease;
            } else if (m_palmEnabled && isPalmContact(t)) {
                beginPalm({tp.id()});
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
            const qreal moved = geom::distance(primary.start, primary.pos);
            const bool young = moved < kYoungStrokeMm * m_pixelsPerMm
                || ((now - primary.startTime) < kYoungStrokeMs && moved < kYoungStrokeMaxMm * m_pixelsPerMm);
            if (young) {
                forwardTouch(m_primaryId, PointerPhase::Cancel, primary, mods);
                m_tracks[m_primaryId].forwarded = false;
                m_primaryId = -1;
                const QVector<int> group = findPalmGroup(tp.id(), now);
                if (!group.isEmpty())
                    beginPalm(group);
                else if (m_palmEnabled && isPalmContact(t))
                    beginPalm({tp.id()});
                else
                    beginPanZoom();
            }
            // Otherwise the extra finger is ignored so an ongoing stroke is not disturbed.
            break;
        }
        case TouchMode::MultiDraw: {
            // Every finger writes on its own, unless it completes a tight group (a wipe).
            const QVector<int> group = findPalmGroup(tp.id(), now);
            if (!group.isEmpty()) {
                beginPalm(group);
            } else if (m_palmEnabled && !palmActive() && isPalmContact(t)) {
                beginPalm({tp.id()});
            } else {
                m_tracks[tp.id()].forwarded = true;
                forwardTouch(tp.id(), PointerPhase::Down, t, mods);
            }
            break;
        }
        case TouchMode::PanZoom: {
            const QVector<int> group = findPalmGroup(tp.id(), now);
            if (!group.isEmpty()) {
                GestureEvent g;
                g.type = GestureType::PanZoom;
                g.phase = GesturePhase::Cancel;
                m_sink.gestureEvent(g);
                beginPalm(group);
            } else {
                rebaseline();
            }
            break;
        }
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
        case TouchMode::MultiDraw: {
            bool palmMoved = false;
            for (const QTouchEvent::TouchPoint& tp : points) {
                if (tp.state() != Qt::TouchPointMoved)
                    continue;
                auto it = m_tracks.constFind(tp.id());
                if (it != m_tracks.constEnd() && it->forwarded)
                    forwardTouch(tp.id(), PointerPhase::Move, *it, mods);
                palmMoved = palmMoved || m_palmIds.contains(tp.id());
            }
            if (palmMoved)
                updatePalm(GesturePhase::Update);
            break;
        }
        case TouchMode::PanZoom:
            updatePanZoom();
            break;
        case TouchMode::PalmErase: {
            // Only the fingers of the group steer the wipe; other fingers are ignored.
            bool palmMoved = false;
            for (const QTouchEvent::TouchPoint& tp : points)
                palmMoved = palmMoved || (tp.state() == Qt::TouchPointMoved && m_palmIds.contains(tp.id()));
            if (palmMoved)
                updatePalm(GesturePhase::Update);
            break;
        }
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
            if (m_palmIds.removeAll(tp.id()) > 0 && m_palmIds.isEmpty())
                updatePalm(GesturePhase::End); // the last finger of the group was lifted
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
            // The wipe stays active until every finger of the group is lifted.
            m_tracks.remove(tp.id());
            if (m_palmIds.removeAll(tp.id()) > 0 && m_palmIds.isEmpty())
                updatePalm(GesturePhase::End); // the last finger of the group was lifted
            if (!palmActive())
                m_mode = m_tracks.isEmpty() ? TouchMode::None : TouchMode::WaitRelease;
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
