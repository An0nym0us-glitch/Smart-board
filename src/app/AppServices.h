#pragma once

#include "tools/Tool.h"

#include <functional>

namespace cb {

struct UiContext;
class AppSettings;
class CanvasWidget;
class Document;
class EditOperations;
class ExportController;
class LessonController;
class PopoverController;
class RecognizerRegistry;
class TemplateLibrary;
class ThumbnailCache;
class Toast;
class ToolSettings;

/// References to the application services that popover panels work with. Owned by MainWindow,
/// passed explicitly (no global state).
struct AppServices
{
    const UiContext& ui;
    Document& doc;
    ToolSettings& tools;
    AppSettings& settings;
    CanvasWidget& canvas;
    LessonController& lesson;
    ExportController& exporter;
    TemplateLibrary& templates;
    ThumbnailCache& thumbnails;
    EditOperations& edit;
    Toast& toast;
    PopoverController& popovers;
    const RecognizerRegistry& recognizers;
    std::function<void(ToolId)> activateTool;
    std::function<void(qreal)> applyUiScale;
};

} // namespace cb
