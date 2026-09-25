#include "core/Geometry.h"
#include "input/InputManager.h"

#include <QApplication>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QtTest>

using namespace cb;

namespace {
struct Recorder : InputSink
{
    QVector<PointerEvent> pointers;
    QVector<GestureEvent> gestures;
    int wheelZooms = 0;
    void pointerEvent(PointerEvent& e) override { pointers.push_back(e); }
    void gestureEvent(const GestureEvent& e) override { gestures.push_back(e); }
    void wheelZoom(const QPointF&, qreal) override { ++wheelZooms; }
    void wheelPan(const QPointF&) override {}
    void dragPan(const QPointF&) override {}

    int count(PointerPhase phase) const
    {
        int n = 0;
        for (const auto& p : pointers)
            n += p.phase == phase;
        return n;
    }
    int count(GestureType type, GesturePhase phase) const
    {
        int n = 0;
        for (const auto& g : gestures)
            n += g.type == type && g.phase == phase;
        return n;
    }
};

class TouchScript
{
public:
    explicit TouchScript(InputManager& input)
        : m_input(input)
    {
        if (!s_device)
            s_device = QTest::createTouchDevice();
    }

    void press(int id, QPointF pos, qreal diameter = 8) { send(id, pos, Qt::TouchPointPressed, diameter); }
    /// Several fingers landing in the same event (as touch drivers report a hand).
    void pressMany(const QVector<QPair<int, QPointF>>& fingers)
    {
        const bool begin = m_points.isEmpty();
        QList<QTouchEvent::TouchPoint> points;
        Qt::TouchPointStates states;
        for (const auto& f : fingers) {
            m_points.insert(f.first, f.second);
            m_diameters.insert(f.first, 8);
        }
        for (auto it = m_points.constBegin(); it != m_points.constEnd(); ++it) {
            QTouchEvent::TouchPoint tp(it.key());
            tp.setPos(it.value());
            tp.setScreenPos(it.value());
            tp.setEllipseDiameters(QSizeF(8, 8));
            bool pressed = false;
            for (const auto& f : fingers)
                pressed = pressed || f.first == it.key();
            tp.setState(pressed ? Qt::TouchPointPressed : Qt::TouchPointStationary);
            states |= tp.state();
            points << tp;
        }
        QTouchEvent e(begin ? QEvent::TouchBegin : QEvent::TouchUpdate, s_device, Qt::NoModifier, states, points);
        m_input.handleEvent(&e);
    }
    void cancel()
    {
        QTouchEvent e(QEvent::TouchCancel, s_device, Qt::NoModifier, Qt::TouchPointStates(), {});
        m_input.handleEvent(&e);
        m_points.clear();
        m_diameters.clear();
    }
    void move(int id, QPointF pos) { send(id, pos, Qt::TouchPointMoved, m_diameters.value(id, 8)); }
    void release(int id) { send(id, m_points.value(id), Qt::TouchPointReleased, m_diameters.value(id, 8)); }

private:
    void send(int id, QPointF pos, Qt::TouchPointState state, qreal diameter)
    {
        const bool begin = m_points.isEmpty();
        m_points.insert(id, pos);
        m_diameters.insert(id, diameter);
        QList<QTouchEvent::TouchPoint> points;
        Qt::TouchPointStates states;
        for (auto it = m_points.constBegin(); it != m_points.constEnd(); ++it) {
            QTouchEvent::TouchPoint tp(it.key());
            tp.setPos(it.value());
            tp.setScreenPos(it.value());
            tp.setEllipseDiameters(QSizeF(m_diameters.value(it.key()), m_diameters.value(it.key())));
            const Qt::TouchPointState s = it.key() == id ? state : Qt::TouchPointStationary;
            tp.setState(s);
            states |= s;
            points << tp;
        }
        if (state == Qt::TouchPointReleased) {
            m_points.remove(id);
            m_diameters.remove(id);
        }
        const QEvent::Type type = begin ? QEvent::TouchBegin : (m_points.isEmpty() ? QEvent::TouchEnd : QEvent::TouchUpdate);
        QTouchEvent e(type, s_device, Qt::NoModifier, states, points);
        m_input.handleEvent(&e);
    }

    InputManager& m_input;
    QMap<int, QPointF> m_points;
    QMap<int, qreal> m_diameters;
    static QTouchDevice* s_device;
};
QTouchDevice* TouchScript::s_device = nullptr;

void tablet(InputManager& input, QEvent::Type type, QPointF pos, qreal pressure = 0.6)
{
    const Qt::MouseButtons buttons = type == QEvent::TabletRelease || (type == QEvent::TabletMove && pressure <= 0) ? Qt::NoButton : Qt::LeftButton;
    QTabletEvent e(type, pos, pos, QTabletEvent::Stylus, QTabletEvent::Pen, pressure, 0, 0, 0, 0, 0, Qt::NoModifier, 1,
                   type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton, buttons);
    input.handleEvent(&e);
}

int countDevice(const Recorder& r, PointerDevice device, PointerPhase phase)
{
    int n = 0;
    for (const auto& p : r.pointers)
        n += p.device == device && p.phase == phase;
    return n;
}
} // namespace

class TestInput : public QObject
{
    Q_OBJECT
private slots:
    void singleFingerDraws()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.move(1, QPointF(150, 120));
        t.release(1);
        QCOMPARE(r.count(PointerPhase::Down), 1);
        QCOMPARE(r.count(PointerPhase::Move), 1);
        QCOMPARE(r.count(PointerPhase::Up), 1);
        QCOMPARE(r.pointers.first().device, PointerDevice::Touch);
    }

    void twoFingersPinch()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.press(2, QPointF(200, 100));
        QCOMPARE(r.count(PointerPhase::Cancel), 1); // the young stroke is dropped
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::Begin), 1);
        t.move(2, QPointF(300, 100));
        QVERIFY(!r.gestures.isEmpty());
        QVERIFY(r.gestures.last().scaleDelta > 1.2);
        t.release(2);
        t.release(1);
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::End), 1);
        QCOMPARE(r.count(PointerPhase::Up), 0);
    }

    void fourFingersErase()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.press(2, QPointF(130, 95));
        t.press(3, QPointF(160, 98));
        t.press(4, QPointF(190, 110));
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 1);
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::Cancel), 1);
        t.move(1, QPointF(300, 300));
        QVERIFY(r.count(GestureType::PalmErase, GesturePhase::Update) >= 1);
        QVERIFY(r.gestures.last().radius > 40);
        for (int id = 1; id <= 4; ++id)
            t.release(id);
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::End), 1);
    }

    void palmContactErases()
    {
        Recorder r;
        InputManager input(r);
        input.setPixelsPerMm(4.0);
        TouchScript t(input);
        t.press(1, QPointF(400, 400), 30 * 4.0); // 30 mm contact
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 1);
        QCOMPARE(r.count(PointerPhase::Down), 0);
        t.release(1);
    }

    void palmEraseCanBeDisabled()
    {
        Recorder r;
        InputManager input(r);
        input.setPalmEraseEnabled(false);
        TouchScript t(input);
        for (int id = 1; id <= 4; ++id)
            t.press(id, QPointF(100 + id * 30, 100));
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 0);
    }

    void multiUserDrawing()
    {
        Recorder r;
        InputManager input(r);
        input.setMultiUserTouch(true);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.press(2, QPointF(900, 100));
        t.move(1, QPointF(120, 140));
        t.move(2, QPointF(920, 140));
        t.release(1);
        t.release(2);
        QCOMPARE(r.count(PointerPhase::Down), 2);
        QCOMPARE(r.count(PointerPhase::Up), 2);
        QCOMPARE(r.count(PointerPhase::Cancel), 0);
        QVERIFY(r.gestures.isEmpty());
    }

    void establishedStrokeIgnoresSecondFinger()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        QTest::qWait(320);
        t.move(1, QPointF(300, 100));
        t.press(2, QPointF(600, 600));
        t.move(1, QPointF(320, 110));
        t.release(2);
        t.release(1);
        QCOMPARE(r.count(PointerPhase::Cancel), 0);
        QCOMPARE(r.count(PointerPhase::Up), 1);
        QVERIFY(r.gestures.isEmpty());
    }
};


class TestInputStates : public QObject
{
    Q_OBJECT
private slots:
    void pinchScaleFollowsFingerSpread()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.pressMany({{1, QPointF(400, 300)}, {2, QPointF(600, 300)}}); // both fingers together
        QCOMPARE(r.count(PointerPhase::Down), 1);
        QCOMPARE(r.count(PointerPhase::Cancel), 1);
        QCOMPARE(input.touchState(), InputManager::TouchState::PanZoom);
        // Spread from 200 px to 400 px apart: total scale 2x, no pan (centroid unchanged).
        qreal scale = 1.0;
        QPointF pan;
        for (int i = 1; i <= 10; ++i) {
            t.move(1, QPointF(400 - i * 10, 300));
            t.move(2, QPointF(600 + i * 10, 300));
        }
        for (const auto& g : r.gestures)
            if (g.type == GestureType::PanZoom && g.phase == GesturePhase::Update) {
                scale *= g.scaleDelta;
                pan += g.panDelta;
            }
        QVERIFY2(std::abs(scale - 2.0) < 1e-6, qPrintable(QString::number(scale)));
        QVERIFY(geom::length(pan) < 1e-6);
        // Pinching in again.
        const int updates = r.count(GestureType::PanZoom, GesturePhase::Update);
        t.move(2, QPointF(500, 300));
        QVERIFY(r.count(GestureType::PanZoom, GesturePhase::Update) > updates);
        QVERIFY(r.gestures.last().scaleDelta < 1.0);
        t.release(1);
        t.release(2);
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::End), 1);
        QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        QCOMPARE(r.count(PointerPhase::Up), 0); // the cancelled stroke never completes
    }

    void twoFingerPanMovesView()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(400, 300));
        t.press(2, QPointF(500, 300));
        QPointF pan;
        for (int i = 1; i <= 5; ++i) {
            t.move(1, QPointF(400 + i * 20, 300 + i * 10));
            t.move(2, QPointF(500 + i * 20, 300 + i * 10));
        }
        for (const auto& g : r.gestures)
            if (g.phase == GesturePhase::Update)
                pan += g.panDelta;
        QVERIFY(geom::distance(pan, QPointF(100, 50)) < 1e-6);
    }

    void liftingOneOfThreeFingersKeepsTheGesture()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(300, 300));
        t.press(2, QPointF(400, 300));
        t.press(3, QPointF(350, 400));
        QCOMPARE(input.touchState(), InputManager::TouchState::PanZoom);
        t.release(3);
        QCOMPARE(input.touchState(), InputManager::TouchState::PanZoom);
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::End), 0);
        // No jump: the remaining two fingers continue smoothly (rebaselined).
        t.move(1, QPointF(305, 300));
        QVERIFY(geom::length(r.gestures.last().panDelta) < 5.0);
        t.release(2);
        QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::End), 1);
        // The last finger does not start drawing.
        QCOMPARE(input.touchState(), InputManager::TouchState::Ignoring);
        t.move(1, QPointF(500, 500));
        QCOMPARE(r.count(PointerPhase::Down), 1);
        t.release(1);
        QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        // A new single finger draws again.
        t.press(4, QPointF(100, 100));
        QCOMPARE(input.touchState(), InputManager::TouchState::Drawing);
        QCOMPARE(r.count(PointerPhase::Down), 2);
        t.release(4);
    }

    void establishedStrokeSurvivesSeveralExtraFingers()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.move(1, QPointF(200, 100)); // moved 100 px (well past 7 mm): established
        t.press(2, QPointF(700, 500));
        t.press(3, QPointF(760, 520));
        t.move(2, QPointF(720, 540));
        t.move(1, QPointF(260, 120));
        t.release(3);
        t.release(2);
        t.move(1, QPointF(300, 150));
        t.release(1);
        QCOMPARE(r.count(PointerPhase::Cancel), 0);
        QCOMPARE(r.count(PointerPhase::Down), 1);
        QCOMPARE(r.count(PointerPhase::Up), 1);
        QVERIFY(r.count(PointerPhase::Move) >= 3);
        QVERIFY(r.gestures.isEmpty());
        // Extra fingers never produced pointer events of their own.
        for (const auto& p : r.pointers)
            QCOMPARE(p.pointerId, InputManager::kTouchPointerBase + 1);
    }

    void youngStrokeByTimeButNotDistance()
    {
        // Fast second finger (< 280 ms) cancels even if the first moved far; slow and short too.
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.move(1, QPointF(102, 101)); // tiny movement
        QTest::qWait(320);             // old in time, but still short in distance -> young
        t.press(2, QPointF(300, 300));
        QCOMPARE(r.count(PointerPhase::Cancel), 1);
        QCOMPARE(input.touchState(), InputManager::TouchState::PanZoom);
    }

    void touchCancelInEveryState()
    {
        {
            Recorder r;
            InputManager input(r);
            TouchScript t(input);
            t.press(1, QPointF(100, 100));
            t.move(1, QPointF(150, 100));
            t.cancel(); // e.g. a system gesture or the window losing touch
            QCOMPARE(r.count(PointerPhase::Cancel), 1);
            QCOMPARE(r.count(PointerPhase::Up), 0);
            QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        }
        {
            Recorder r;
            InputManager input(r);
            TouchScript t(input);
            t.pressMany({{1, QPointF(100, 100)}, {2, QPointF(300, 100)}});
            t.move(2, QPointF(400, 100));
            t.cancel();
            QCOMPARE(r.count(GestureType::PanZoom, GesturePhase::Cancel), 1); // the view is restored
            QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        }
        {
            Recorder r;
            InputManager input(r);
            TouchScript t(input);
            t.pressMany({{1, QPointF(100, 100)}, {2, QPointF(130, 95)}, {3, QPointF(160, 98)}, {4, QPointF(190, 110)}});
            QCOMPARE(input.touchState(), InputManager::TouchState::PalmErase);
            t.cancel();
            QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::End), 1); // erased part stays one undo step
            QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        }
    }

    void touchEndWithStuckPointsRecovers()
    {
        // Some drivers end a sequence without releasing every point: state must not stay stuck.
        Recorder r;
        InputManager input(r);
        QTouchDevice* device = QTest::createTouchDevice();
        QTouchEvent::TouchPoint a(1);
        a.setPos(QPointF(100, 100));
        a.setState(Qt::TouchPointPressed);
        QTouchEvent begin(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, {a});
        input.handleEvent(&begin);
        a.setState(Qt::TouchPointStationary);
        QTouchEvent end(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointStationary, {a});
        input.handleEvent(&end);
        QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        QCOMPARE(r.count(PointerPhase::Cancel), 1);
        // Next touch works normally.
        TouchScript t(input);
        t.press(5, QPointF(200, 200));
        t.release(5);
        QCOMPARE(r.count(PointerPhase::Up), 1);
    }

    void cancelAllOnPageChange()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        input.cancelAll();
        QCOMPARE(r.count(PointerPhase::Cancel), 1);
        QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
    }

    void fourFingersOnlyWhenPlacedTogether()
    {
        // Two fingers zooming, then two more much later: stays a zoom (no accidental erase).
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.press(2, QPointF(200, 100));
        QTest::qWait(500);
        t.press(3, QPointF(150, 200));
        t.press(4, QPointF(250, 200));
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 0);
        QCOMPARE(input.touchState(), InputManager::TouchState::PanZoom);
    }

    void fourFingersTooFarApartAreNotAHand()
    {
        Recorder r;
        InputManager input(r);
        input.setPixelsPerMm(4.0);
        TouchScript t(input);
        // Four fingers spread over more than a hand span (two students at once).
        t.pressMany({{1, QPointF(0, 0)}, {2, QPointF(3000, 0)}, {3, QPointF(0, 2000)}, {4, QPointF(3000, 2000)}});
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 0);
    }

    void fourFingerEraseReliableWhenFingersArriveOneByOne()
    {
        // Fingers of a hand rarely land in the same frame: 1, 2, 3, 4 within the window.
        for (int run = 0; run < 20; ++run) {
            Recorder r;
            InputManager input(r);
            input.setPixelsPerMm(3.78);
            TouchScript t(input);
            const QPointF base(400 + run * 3, 300 + run * 2);
            t.press(1, base);
            t.press(2, base + QPointF(28, -8));
            t.press(3, base + QPointF(55, -4));
            t.press(4, base + QPointF(80, 12));
            QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 1);
            QCOMPARE(input.touchState(), InputManager::TouchState::PalmErase);
            // The whole hand moves: the eraser follows the centroid, the radius covers the hand.
            for (int i = 1; i <= 5; ++i)
                for (int id = 1; id <= 4; ++id)
                    t.move(id, base + QPointF(id * 25 + i * 30, i * 20));
            QVERIFY(r.gestures.last().radius >= 14 * 3.78);
            for (int id = 1; id <= 4; ++id)
                t.release(id);
            QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::End), 1);
            QCOMPARE(r.count(PointerPhase::Up), 0); // no ink from any finger
            QCOMPARE(input.touchState(), InputManager::TouchState::Idle);
        }
    }

    void palmStaysActiveUntilLastFingerLifts()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.pressMany({{1, QPointF(100, 100)}, {2, QPointF(130, 95)}, {3, QPointF(160, 98)}, {4, QPointF(190, 110)}});
        t.release(1);
        t.release(2);
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::End), 0);
        t.move(3, QPointF(300, 300));
        QVERIFY(r.count(GestureType::PalmErase, GesturePhase::Update) >= 1);
        t.release(3);
        t.release(4);
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::End), 1);
    }

    void stylusAndTouchAreIndependentDevices()
    {
        Recorder r;
        InputManager input(r);
        tablet(input, QEvent::TabletPress, QPointF(100, 100), 0.3);
        tablet(input, QEvent::TabletMove, QPointF(150, 120), 0.8);
        tablet(input, QEvent::TabletRelease, QPointF(160, 120), 0.0);
        QCOMPARE(countDevice(r, PointerDevice::Stylus, PointerPhase::Down), 1);
        QCOMPARE(countDevice(r, PointerDevice::Stylus, PointerPhase::Up), 1);
        QVERIFY(r.pointers[1].hasPressure);
        QVERIFY(std::abs(r.pointers[1].pressure - 0.8) < 1e-6);
        // The stylus eraser end is its own device (routed to the eraser by the tools).
        QTabletEvent eraser(QEvent::TabletPress, QPointF(10, 10), QPointF(10, 10), QTabletEvent::Stylus, QTabletEvent::Eraser,
                            0.5, 0, 0, 0, 0, 0, Qt::NoModifier, 1, Qt::LeftButton, Qt::LeftButton);
        input.handleEvent(&eraser);
        QCOMPARE(r.pointers.last().device, PointerDevice::StylusEraser);
    }

    void restingHandIsIgnoredWhileThePenWrites()
    {
        Recorder r;
        InputManager input(r);
        input.setPixelsPerMm(4.0);
        TouchScript t(input);
        tablet(input, QEvent::TabletPress, QPointF(500, 500));
        QVERIFY(input.stylusActive());
        // Palm resting on the board while writing: large contact, and some fingers.
        t.press(1, QPointF(700, 650), 30 * 4.0);
        t.press(2, QPointF(760, 640));
        t.move(1, QPointF(720, 660));
        QCOMPARE(input.touchState(), InputManager::TouchState::Ignoring);
        tablet(input, QEvent::TabletMove, QPointF(560, 510));
        tablet(input, QEvent::TabletRelease, QPointF(600, 520), 0);
        t.release(1);
        t.release(2);
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 0);
        QVERIFY(r.gestures.isEmpty());
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Down), 0);
        QCOMPARE(countDevice(r, PointerDevice::Stylus, PointerPhase::Up), 1);
    }

    void penLandingCancelsAYoungFingerStroke()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        t.press(1, QPointF(700, 650)); // the hand touched first
        QCOMPARE(input.touchState(), InputManager::TouchState::Drawing);
        tablet(input, QEvent::TabletPress, QPointF(500, 500));
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Cancel), 1);
        QCOMPARE(input.touchState(), InputManager::TouchState::Ignoring);
        t.move(1, QPointF(720, 660));
        t.release(1);
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Move), 0);
        tablet(input, QEvent::TabletRelease, QPointF(520, 500), 0);
    }

    void touchWorksAgainAfterThePenLeaves()
    {
        Recorder r;
        InputManager input(r);
        TouchScript t(input);
        tablet(input, QEvent::TabletPress, QPointF(500, 500));
        tablet(input, QEvent::TabletRelease, QPointF(510, 500), 0);
        QVERIFY(input.stylusActive()); // still in proximity right after lifting
        t.press(1, QPointF(100, 100));
        t.release(1);
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Down), 0);
        QTest::qWait(int(InputManager::kStylusProximityMs) + 100);
        QVERIFY(!input.stylusActive());
        t.press(2, QPointF(100, 100));
        t.move(2, QPointF(200, 100));
        t.release(2);
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Down), 1);
        QCOMPARE(countDevice(r, PointerDevice::Touch, PointerPhase::Up), 1);
    }

    void pinchIsDisabledInMultiUserMode()
    {
        Recorder r;
        InputManager input(r);
        input.setMultiUserTouch(true);
        TouchScript t(input);
        t.press(1, QPointF(100, 100));
        t.press(2, QPointF(200, 100));
        t.move(2, QPointF(400, 100));
        t.release(1);
        t.release(2);
        QVERIFY(r.gestures.isEmpty());
        QCOMPARE(r.count(PointerPhase::Up), 2);
        // Palm erase still works for a real palm contact in multi-user mode.
        input.setPixelsPerMm(4.0);
        t.press(3, QPointF(500, 500), 30 * 4.0);
        QCOMPARE(r.count(GestureType::PalmErase, GesturePhase::Begin), 1);
        t.release(3);
    }

    void synthesizedMouseIsIgnored()
    {
        // Windows also sends mouse events generated from touch and pen: they must not draw twice.
        Recorder r;
        InputManager input(r);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier, Qt::MouseEventSynthesizedBySystem);
        input.handleEvent(&press);
        QVERIFY(r.pointers.isEmpty());
        QMouseEvent real(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
                         Qt::LeftButton, Qt::NoModifier, Qt::MouseEventNotSynthesized);
        input.handleEvent(&real);
        QCOMPARE(r.count(PointerPhase::Down), 1);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    int status = 0;
    {
        TestInput t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TestInputStates t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}
#include "tst_input.moc"
