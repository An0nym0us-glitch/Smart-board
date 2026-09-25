#include "app/LessonController.h"

#include "app/AppSettings.h"
#include "canvas/CanvasWidget.h"
#include "document/Commands.h"
#include "document/Document.h"
#include "document/ObjectFactory.h"
#include "document/TemplateLibrary.h"
#include "storage/AutosaveManager.h"
#include "storage/PdfImporter.h"
#include "storage/ProjectSerializer.h"
#include "ui/Toast.h"
#include "ui/UiContext.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QThread>

namespace cb {

LessonController::LessonController(const UiContext& ui, Document& doc, AppSettings& settings,
                                   TemplateLibrary& templates, Toast& toast, CanvasWidget& canvas, QWidget* window,
                                   QObject* parent)
    : QObject(parent)
    , m_ui(ui)
    , m_doc(doc)
    , m_settings(settings)
    , m_templates(templates)
    , m_toast(toast)
    , m_canvas(canvas)
    , m_window(window)
    , m_autosave(std::make_unique<AutosaveManager>(doc))
{
    m_autosave->setIntervalSeconds(settings.autosaveSeconds());
    m_autosave->start();
    connect(&settings, &AppSettings::changed, this, [this]() { m_autosave->setIntervalSeconds(m_settings.autosaveSeconds()); });
    connect(m_autosave.get(), &AutosaveManager::failed, this, [this](const QString& error) {
        m_toast.showMessage(tr("Autosave failed: %1").arg(error), 5000);
    });
}

LessonController::~LessonController() = default;

QString LessonController::lessonFilter()
{
    return tr("ClassBoard lessons (*.classboard)");
}

QString LessonController::imageFilter()
{
    return tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg)");
}

QString LessonController::startDirectory() const
{
    if (!m_settings.lastDirectory().isEmpty())
        return m_settings.lastDirectory();
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void LessonController::rememberDirectory(const QString& path)
{
    m_settings.setLastDirectory(QFileInfo(path).absolutePath());
}

void LessonController::confirmDiscard(std::function<void()> proceed)
{
    if (!m_doc.isModified()) {
        proceed();
        return;
    }
    ConfirmOverlay::ask(m_ui, m_window, tr("Save changes?"),
                        tr("“%1” has changes that are not saved yet.").arg(m_doc.displayName()),
                        {{tr("Cancel"), {}, false, false},
                         {tr("Don't save"), [proceed]() { proceed(); }, false, true},
                         {tr("Save"),
                          [this, proceed]() {
                              save([proceed](bool ok) {
                                  if (ok)
                                      proceed();
                              });
                          },
                          true, false}});
}

void LessonController::newLesson()
{
    confirmDiscard([this]() {
        m_doc.resetToNew(m_templates.find(m_settings.defaultTemplateId()));
        m_autosave->discard();
        m_toast.showMessage(tr("New lesson"), 1500);
    });
}

void LessonController::open()
{
    confirmDiscard([this]() {
        const QString path = QFileDialog::getOpenFileName(m_window, tr("Open lesson"), startDirectory(), lessonFilter());
        if (!path.isEmpty())
            openFile(path);
    });
}

void LessonController::openFile(const QString& path)
{
    rememberDirectory(path);
    m_toast.showProgress(tr("Opening %1").arg(QFileInfo(path).fileName()), -1);
    auto contents = std::make_shared<DocumentContents>();
    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    auto skipped = std::make_shared<int>(0);
    QThread* worker = QThread::create([path, contents, error, ok, skipped]() {
        *ok = ProjectSerializer::load(path, contents.get(), error.get(), skipped.get());
    });
    QPointer<LessonController> self(this);
    connect(worker, &QThread::finished, this, [self, worker, path, contents, error, ok, skipped]() {
        worker->deleteLater();
        if (!self)
            return;
        if (!*ok) {
            self->m_toast.showMessage(tr("Could not open the lesson: %1").arg(*error), 6000);
            self->m_settings.removeRecentFile(path);
            return;
        }
        self->m_doc.setContents(std::move(*contents));
        self->m_doc.setFilePath(path);
        self->m_settings.addRecentFile(path);
        self->m_autosave->discard();
        if (*skipped > 0)
            self->m_toast.showMessage(tr("Opened. %n item(s) from a newer ClassBoard version were skipped.", "", *skipped), 6000);
        else
            self->m_toast.showMessage(tr("Opened %1").arg(QFileInfo(path).completeBaseName()), 2000);
    });
    worker->start();
}

QString LessonController::chooseSavePath()
{
    QString suggested = m_doc.filePath();
    if (suggested.isEmpty())
        suggested = startDirectory() + QLatin1Char('/') + m_doc.displayName() + QStringLiteral(".classboard");
    QString path = QFileDialog::getSaveFileName(m_window, tr("Save lesson"), suggested, lessonFilter());
    if (path.isEmpty())
        return path;
    if (!path.endsWith(QStringLiteral(".classboard"), Qt::CaseInsensitive))
        path += QStringLiteral(".classboard");
    return path;
}

void LessonController::save(std::function<void(bool)> done)
{
    if (m_doc.filePath().isEmpty()) {
        saveAs(std::move(done));
        return;
    }
    writeTo(m_doc.filePath(), std::move(done));
}

void LessonController::saveAs(std::function<void(bool)> done)
{
    const QString path = chooseSavePath();
    if (path.isEmpty()) {
        if (done)
            done(false);
        return;
    }
    rememberDirectory(path);
    writeTo(path, std::move(done));
}

void LessonController::writeTo(const QString& path, std::function<void(bool)> done)
{
    if (m_saving) {
        if (done)
            done(false);
        return;
    }
    m_saving = true;
    m_doc.metadata().modified = QDateTime::currentDateTimeUtc();
    const int savedIndex = m_doc.commands().index();
    auto contents = std::make_shared<DocumentContents>(m_doc.copyContents());
    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    m_toast.showProgress(tr("Saving"), -1);
    QThread* worker = QThread::create([path, contents, error, ok]() {
        *ok = ProjectSerializer::save(path, *contents, error.get());
    });
    QPointer<LessonController> self(this);
    connect(worker, &QThread::finished, this, [self, worker, path, error, ok, savedIndex, done]() {
        worker->deleteLater();
        if (!self)
            return;
        self->m_saving = false;
        if (*ok) {
            self->m_doc.setFilePath(path);
            self->m_doc.commands().setCleanIndex(savedIndex);
            self->m_settings.addRecentFile(path);
            self->m_toast.showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 1800);
        } else {
            self->m_toast.showMessage(tr("Could not save: %1").arg(*error), 7000);
        }
        if (done)
            done(*ok);
    });
    worker->start();
}

void LessonController::importLesson()
{
    const QString path = QFileDialog::getOpenFileName(m_window, tr("Import pages from a lesson"), startDirectory(),
                                                      lessonFilter());
    if (path.isEmpty())
        return;
    rememberDirectory(path);
    DocumentContents contents;
    QString error;
    if (!ProjectSerializer::load(path, &contents, &error)) {
        m_toast.showMessage(tr("Could not import: %1").arg(error), 6000);
        return;
    }
    m_doc.images().mergeFrom(contents.images);
    auto macro = std::make_unique<CompositeCommand>(tr("Import lesson"));
    int index = m_doc.currentPageIndex() + 1;
    const int count = static_cast<int>(contents.pages.size());
    for (auto& page : contents.pages) {
        page->setId(newId());
        auto cmd = std::make_unique<InsertPageCommand>(index++, std::move(page));
        cmd->redo(m_doc);
        macro->add(std::move(cmd));
    }
    m_doc.commands().pushApplied(std::move(macro));
    m_toast.showMessage(tr("Imported %n page(s)", "", count), 2500);
}

void LessonController::importImages()
{
    const QStringList paths = QFileDialog::getOpenFileNames(m_window, tr("Insert images"), startDirectory(), imageFilter());
    if (paths.isEmpty())
        return;
    rememberDirectory(paths.first());
    insertImageFiles(paths, m_canvas.viewCenterInPage());
}

void LessonController::insertImageFiles(const QStringList& paths, const QPointF& pageCenter)
{
    Page* page = m_doc.currentPage();
    if (!page)
        return;
    std::vector<ObjectPtr> objects;
    QStringList failed;
    QPointF offset;
    for (const QString& path : paths) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            failed << QFileInfo(path).fileName();
            continue;
        }
        QString error;
        const QString key = m_doc.images().addEncoded(f.readAll(), &error);
        if (key.isEmpty()) {
            failed << QFileInfo(path).fileName();
            continue;
        }
        if (ObjectPtr o = ObjectFactory::createImage(key, m_doc.images().originalSize(key), pageCenter + offset))
            objects.push_back(std::move(o));
        offset += QPointF(40, 40);
    }
    if (!objects.empty()) {
        QRectF bounds;
        for (const auto& o : objects)
            bounds = bounds.isNull() ? o->sceneBounds() : bounds.united(o->sceneBounds());
        m_doc.commands().push(std::make_unique<AddObjectsCommand>(page->id(), std::move(objects), tr("Insert image")));
        m_canvas.ensureVisible(bounds);
    }
    if (!failed.isEmpty())
        m_toast.showMessage(tr("Could not read: %1").arg(failed.join(QStringLiteral(", "))), 5000);
}

void LessonController::importPdf()
{
    if (!PdfImporter::isAvailable()) {
        m_toast.showMessage(PdfImporter::unavailableReason(), 7000);
        return;
    }
    if (m_pdf) {
        m_toast.showMessage(tr("A PDF is already being imported."), 2500);
        return;
    }
    const QString path = QFileDialog::getOpenFileName(m_window, tr("Import PDF"), startDirectory(), tr("PDF documents (*.pdf)"));
    if (path.isEmpty())
        return;
    rememberDirectory(path);
    m_pdf = new PdfImporter(this);
    m_toast.showProgress(tr("Converting %1").arg(QFileInfo(path).fileName()), -1);
    connect(m_pdf, &PdfImporter::finished, this, [this, path](const QVector<QImage>& pages, const QString& error) {
        m_pdf->deleteLater();
        m_pdf = nullptr;
        if (!error.isEmpty() || pages.isEmpty()) {
            m_toast.showMessage(error.isEmpty() ? tr("The PDF is empty.") : error, 6000);
            return;
        }
        auto macro = std::make_unique<CompositeCommand>(tr("Import PDF"));
        int index = m_doc.currentPageIndex() + 1;
        const QString base = QFileInfo(path).completeBaseName();
        for (int i = 0; i < pages.size(); ++i) {
            const QString key = m_doc.images().addImage(pages[i]);
            if (key.isEmpty())
                continue;
            PagePtr page = m_doc.createPage();
            TemplateSpec spec;
            spec.id = QStringLiteral("pdf");
            spec.name = QStringLiteral("PDF");
            spec.kind = TemplateKind::Image;
            spec.background = QColor(255, 255, 255);
            spec.imageKey = key;
            page->setBackground(spec);
            page->setName(QStringLiteral("%1 p.%2").arg(base).arg(i + 1));
            auto cmd = std::make_unique<InsertPageCommand>(index++, std::move(page));
            cmd->redo(m_doc);
            macro->add(std::move(cmd));
        }
        m_doc.commands().pushApplied(std::move(macro));
        m_toast.showMessage(tr("Imported %n PDF page(s). Choose a dark pen colour to annotate.", "", pages.size()), 4000);
    });
    m_pdf->start(path);
}

void LessonController::checkRecovery()
{
    const QVector<RecoveryInfo> infos = AutosaveManager::findRecoverable();
    if (infos.isEmpty())
        return;
    const RecoveryInfo info = infos.first();
    const QString when = info.savedAt.isValid() ? info.savedAt.toString(QStringLiteral("dd MMM, HH:mm")) : QString();
    m_toast.showActions(
        tr("ClassBoard closed unexpectedly. Restore “%1” (%2)?").arg(info.title, when),
        {{tr("Discard"),
          [this, info]() {
              AutosaveManager::discardRecovery(info);
              checkRecovery();
          },
          false},
         {tr("Restore"),
          [this, info]() {
              DocumentContents contents;
              QString error;
              if (!AutosaveManager::restore(info, &contents, &error)) {
                  m_toast.showMessage(tr("Could not restore: %1").arg(error), 6000);
                  return;
              }
              m_doc.setContents(std::move(contents));
              m_doc.setFilePath(QFile::exists(info.originalPath) ? info.originalPath : QString());
              m_doc.commands().setCleanIndex(-1); // restored content is unsaved
              AutosaveManager::discardRecovery(info);
              m_autosave->saveNow();
              m_toast.showMessage(tr("Lesson restored. Save it to keep it."), 4000);
          },
          true}});
}

} // namespace cb
