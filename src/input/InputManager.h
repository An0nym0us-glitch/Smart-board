#pragma once

#include "input/InputEvent.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointF>

class QEvent;
class QMouseEvent;
class QTabletEvent;
class QTouchEvent;
class QWheelEvent;

namespace cb {

/// Receiver of normalised input.
class InputSink
{
public:
    virtual ~InputSink() = default;
    virtual void pointerEvent(PointerEvent& event) = 0;
    virtual void gestureEvent(const GestureEvent& event) = 0;
    virtual void wheelZoom(const QPointF& viewPos, qreal factor) = 0;
    virtual void wheelPan(const QPointF& viewDelta) = 0;
    /// Middle/right mouse button drag.
    virtual void dragPan(const QPointF& viewDelta) = 0;
};

/// Converts Qt mouse, tablet and touch events into device independent pointer events and
/// recognises multi-touch gestures (pan/zoom and the grouped palm-erase gesture).
///
/// Touch handling state machine:
///  * one finger          -> pointer (draws with the active tool)
///  * second finger soon  -> the young stroke is cancelled and a pan/zoom gesture starts
///  * >= 4 fingers placed together within a short window, or a very large contact
///                        -> palm erase gesture (one grouped action, not four pointers)
///  * multi-user mode     -> every finger is an independent pointer (several students drawing)
class InputManager
{
public:
    explicit InputManager(InputSink& sink);

    /// Handles an event delivered to the canvas widget. Returns true if consumed.
    bool handleEvent(QEvent* event);

    void setPalmEraseEnabled(bool enabled) { m_palmEnabled = enabled; }
    bool palmEraseEnabled() const { return m_palmEnabled; }
    void setMultiUserTouch(bool enabled) { m_multiUser = enabled; }
    bool multiUserTouch() const { return m_multiUser; }
    /// Screen density used to convert physical thresholds (mm) to pixels.
    void setPixelsPerMm(qreal ppm) { m_pixelsPerMm = ppm > 0.5 ? ppm : 3.78; }

    /// Cancels every active pointer / gesture (e.g. when the page changes).
    void cancelAll();

    static constexpr int kMousePointerId = 0;
    static constexpr int kStylusPointerId = 1;
    static constexpr int kTouchPointerBase = 100;

private:
    enum class TouchMode { None, Pointer, MultiDraw, PanZoom, PalmErase, WaitRelease };

    struct Track
    {
        QPointF start;
        QPointF pos;
        qint64 startTime = 0;
        qreal diameter = 0.0;
        bool forwarded = false;
    };

    bool handleMouse(QMouseEvent* e);
    bool handleTablet(QTabletEvent* e);
    bool handleTouch(QTouchEvent* e);
    bool handleWheel(QWheelEvent* e);

    void forwardTouch(int id, PointerPhase phase, const Track& t, Qt::KeyboardModifiers mods);
    void beginPanZoom();
    void updatePanZoom();
    void beginPalm(Qt::KeyboardModifiers mods);
    void updatePalm(GesturePhase phase);
    void rebaseline();
    QPointF centroid() const;
    qreal spread(const QPointF& c) const;
    qreal angle(const QPointF& c) const;
    qreal palmRadius(const QPointF& c) const;
    bool isPalmContact(const Track& t) const;
    bool looksLikeHand() const;

    InputSink& m_sink;
    bool m_palmEnabled = true;
    bool m_multiUser = false;
    qreal m_pixelsPerMm = 3.78;

    // Mouse
    bool m_mouseDown = false;
    bool m_mousePanning = false;
    QPointF m_lastMousePos;

    // Stylus
    bool m_stylusDown = false;
    PointerDevice m_stylusDevice = PointerDevice::Stylus;

    // Touch
    QElapsedTimer m_clock;
    QHash<int, Track> m_tracks;
    TouchMode m_mode = TouchMode::None;
    int m_primaryId = -1;
    qint64 m_firstTouchTime = 0;
    QPointF m_prevCentroid;
    qreal m_prevSpread = 0.0;
    qreal m_prevAngle = 0.0;
};

} // namespace cb
