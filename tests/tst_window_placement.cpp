// Where the window opens on start-up: never under the taskbar (which would hide the ribbon).
// Full screen if closed full screen; otherwise maximized, or the saved size only if it still fits.
#include "app/AppSettings.h"
#include "app/MainWindow.h"
#include "document/Document.h"
#include "ui/UiContext.h"

#include <QScreen>
#include <QtTest>

using namespace cb;

class TestWindowPlacement : public QObject
{
    Q_OBJECT
private:
    using Placement = MainWindow::Placement;
    // A 1920×1080 board at 150 % scaling with a 48 px taskbar at the bottom.
    const QRect m_available{0, 0, 1280, 672};
    std::unique_ptr<UiContext> m_ui;

    std::unique_ptr<MainWindow> openWindow(AppSettings& settings)
    {
        auto window = std::make_unique<MainWindow>(*m_ui, settings);
        // The offscreen test screen is only 800×600: leave room below the window's minimum size.
        window->setMinimumSize(320, 240);
        window->showOnStartup();
        if (!QTest::qWaitForWindowExposed(window.get()))
            return nullptr;
        QTest::qWait(50);
        return window;
    }

    void close(MainWindow& window)
    {
        window.document().markSaved();
        window.close();
    }

private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(classboard);
        QStandardPaths::setTestModeEnabled(true);
        m_ui = std::make_unique<UiContext>();
        QVERIFY(m_ui->theme.load(QStringLiteral(":/themes/dark.json")));
    }

    void savedSizeOnlyWhenItFits()
    {
        QVERIFY(MainWindow::fitsOnScreen(QRect(100, 80, 1000, 500), m_available));
        // The old default (1440×900) is bigger than the space above the taskbar.
        QVERIFY(!MainWindow::fitsOnScreen(QRect(0, 0, 1440, 900), m_available));
        // A window as tall as the free space has no room left for its title bar.
        QVERIFY(!MainWindow::fitsOnScreen(QRect(0, 0, 1280, 672), m_available));
        // A rectangle from a monitor that is no longer connected.
        QVERIFY(!MainWindow::fitsOnScreen(QRect(2000, 100, 800, 500), m_available));
        QVERIFY(!MainWindow::fitsOnScreen(QRect(), m_available));
    }

    void placement()
    {
        const QRect small(100, 80, 1000, 500);
        QCOMPARE(MainWindow::startupPlacement(true, false, small, m_available), Placement::FullScreen);
        QCOMPARE(MainWindow::startupPlacement(false, true, small, m_available), Placement::Maximized);
        QCOMPARE(MainWindow::startupPlacement(false, false, small, m_available), Placement::Normal);
        // First start (nothing saved) and a saved size too big for the screen: maximized.
        QCOMPARE(MainWindow::startupPlacement(false, false, QRect(), m_available), Placement::Maximized);
        QCOMPARE(MainWindow::startupPlacement(false, false, QRect(0, 0, 1440, 900), m_available), Placement::Maximized);
    }

    void firstStartIsMaximizedAboveTheTaskbar()
    {
        AppSettings settings;
        settings.setFullScreen(false);
        settings.setWindowMaximized(true);
        settings.setWindowNormalGeometry(QRect());
        auto window = openWindow(settings);
        QVERIFY(window);
        QVERIFY(window->isMaximized());
        QVERIFY(!window->isFullScreen());
        const QRect available = window->screen()->availableGeometry();
        QVERIFY2(available.contains(window->geometry()),
                 qPrintable(QStringLiteral("window %1,%2 %3×%4 outside the free area")
                                .arg(window->x()).arg(window->y()).arg(window->width()).arg(window->height())));
        // The size it gets when it is no longer maximized fits on the screen too.
        QVERIFY(MainWindow::fitsOnScreen(window->normalGeometry(), available));
        close(*window);
        QVERIFY(settings.windowMaximized());
        QVERIFY(!settings.fullScreen());
    }

    void reopensFullScreen()
    {
        AppSettings settings;
        settings.setFullScreen(true);
        auto window = openWindow(settings);
        QVERIFY(window);
        QVERIFY(window->isFullScreen());
        close(*window);
        QVERIFY(settings.fullScreen());
        settings.setFullScreen(false);
    }

    void reopensAtASmallerSavedSize()
    {
        AppSettings settings;
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        const QRect small(available.topLeft() + QPoint(60, 60), available.size() / 2);
        settings.setFullScreen(false);
        settings.setWindowMaximized(false);
        settings.setWindowNormalGeometry(small);
        auto window = openWindow(settings);
        QVERIFY(window);
        QVERIFY(!window->isMaximized());
        QCOMPARE(window->size(), small.size());
        close(*window);
        QVERIFY(!settings.windowMaximized());
        QCOMPARE(settings.windowNormalGeometry().size(), small.size());

        // The same window saved too big for the screen opens maximized instead.
        settings.setWindowNormalGeometry(QRect(available.topLeft(), available.size() + QSize(200, 200)));
        auto big = openWindow(settings);
        QVERIFY(big);
        QVERIFY(big->isMaximized());
        close(*big);
    }
};

QTEST_MAIN(TestWindowPlacement)
#include "tst_window_placement.moc"
