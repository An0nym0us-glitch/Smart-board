#pragma once

#include "tools/Tool.h"

#include <QMainWindow>

#include <memory>

namespace cb {

struct AppServices;
struct UiContext;
class AppSettings;
class CanvasWidget;
class Document;
class EditOperations;
class ExportController;
class LessonController;
class PopoverController;
class PopoverHost;
class RecognizerRegistry;
class Ribbon;
class SelectionBar;
class TableCellEditor;
class TemplateLibrary;
class ThumbnailCache;
class Toast;
class ToolSettings;

/// The ClassBoard window: a full-screen board with the command ribbon at the bottom.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(UiContext& ui, AppSettings& settings, QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Opens a lesson passed on the command line (or restores a crashed session).
    void startup(const QString& fileToOpen);

    CanvasWidget& canvas() { return *m_canvas; }
    Document& document() { return *m_doc; }
    LessonController& lesson() { return *m_lesson; }
    PopoverController& popovers() { return *m_popovers; }
    PopoverHost& popoverHost() { return *m_popoverHost; }
    Ribbon& ribbon() { return *m_ribbon; }
    ToolSettings& toolSettings() { return *m_tools; }
    Toast& toast() { return *m_toast; }

    void activateTool(ToolId id);
    void applyUiScale(qreal scale);
    static qreal automaticUiScale(const QWidget* window);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildRibbon();
    void createShortcuts();
    void updateTitle();
    void updateRibbonState();
    void updateToolChip();
    void onCoreToolClicked(ToolId id, const QString& popoverKey);
    void editObject(const ObjectId& id);
    void toggleFullScreen();
    void addPage();

    UiContext& m_ui;
    AppSettings& m_settings;
    std::unique_ptr<ToolSettings> m_tools;
    std::unique_ptr<Document> m_doc;
    std::unique_ptr<TemplateLibrary> m_templates;
    std::unique_ptr<RecognizerRegistry> m_recognizers;

    QWidget* m_boardArea = nullptr;
    CanvasWidget* m_canvas = nullptr;
    Ribbon* m_ribbon = nullptr;
    PopoverHost* m_popoverHost = nullptr;
    Toast* m_toast = nullptr;
    SelectionBar* m_selectionBar = nullptr;

    std::unique_ptr<EditOperations> m_edit;
    std::unique_ptr<ThumbnailCache> m_thumbnails;
    std::unique_ptr<LessonController> m_lesson;
    std::unique_ptr<ExportController> m_exporter;
    std::unique_ptr<PopoverController> m_popovers;
    std::unique_ptr<TableCellEditor> m_cellEditor;
    std::unique_ptr<AppServices> m_services;
    bool m_closing = false;
};

} // namespace cb
