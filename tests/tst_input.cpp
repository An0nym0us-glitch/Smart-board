#include "input/InputManager.h"

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

QTEST_MAIN(TestInput)
#include "tst_input.moc"
