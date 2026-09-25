#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>

#include <functional>
#include <memory>

class QWidget;

namespace cb {

struct UiContext;
class AppSettings;
class AutosaveManager;
class CanvasWidget;
class Document;
class PdfImporter;
class TemplateLibrary;
class Toast;

/// File level workflows: new / open / save / save as, importing lessons, images and PDFs,
/// autosave and crash recovery. Uses the native file picker only for choosing files; every
/// other interaction (confirmations, progress, errors) stays inside the board.
class LessonController : public QObject
{
    Q_OBJECT
public:
    LessonController(const UiContext& ui, Document& doc, AppSettings& settings, TemplateLibrary& templates,
                     Toast& toast, CanvasWidget& canvas, QWidget* window, QObject* parent = nullptr);
    ~LessonController() override;

    AutosaveManager& autosave() { return *m_autosave; }

    void newLesson();
    void open();
    void openFile(const QString& path);
    /// Saves (asking for a path if the lesson is new). done(true) after a successful write.
    void save(std::function<void(bool)> done = {});
    void saveAs(std::function<void(bool)> done = {});
    bool isSaving() const { return m_saving; }

    void importLesson();
    void importImages();
    void insertImageFiles(const QStringList& paths, const QPointF& pageCenter);
    void importPdf();

    /// Runs proceed() after the user saved or discarded unsaved changes (or immediately if clean).
    void confirmDiscard(std::function<void()> proceed);

    /// Offers to restore lessons left by a crash.
    void checkRecovery();

    static QString lessonFilter();
    static QString imageFilter();

private:
    void writeTo(const QString& path, std::function<void(bool)> done);
    QString chooseSavePath();
    QString startDirectory() const;
    void rememberDirectory(const QString& path);

    const UiContext& m_ui;
    Document& m_doc;
    AppSettings& m_settings;
    TemplateLibrary& m_templates;
    Toast& m_toast;
    CanvasWidget& m_canvas;
    QPointer<QWidget> m_window;
    std::unique_ptr<AutosaveManager> m_autosave;
    QPointer<PdfImporter> m_pdf;
    bool m_saving = false;
};

} // namespace cb
