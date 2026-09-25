#include "storage/PdfImporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>

#ifdef CLASSBOARD_HAVE_QTPDF
#include <QPdfDocument>
#endif

namespace cb {

namespace {
QString pdftoppmPath()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    if (exe.isEmpty()) {
        // A copy shipped next to ClassBoard (e.g. poppler/bin) is also accepted.
        const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/poppler/bin");
        exe = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"), {local});
    }
    return exe;
}
} // namespace

PdfImporter::PdfImporter(QObject* parent)
    : QObject(parent)
{
}

PdfImporter::~PdfImporter()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool PdfImporter::isAvailable()
{
#ifdef CLASSBOARD_HAVE_QTPDF
    return true;
#else
    return !pdftoppmPath().isEmpty();
#endif
}

QString PdfImporter::unavailableReason()
{
    return tr("PDF import needs Poppler's pdftoppm. Install Poppler and add its bin folder to PATH.");
}

void PdfImporter::start(const QString& pdfPath, int dpi)
{
#ifdef CLASSBOARD_HAVE_QTPDF
    auto* worker = QThread::create([this, pdfPath, dpi]() {
        QPdfDocument doc;
        QVector<QImage> pages;
        QString error;
        if (doc.load(pdfPath) != QPdfDocument::NoError) {
            error = tr("The PDF could not be opened.");
        } else {
            for (int i = 0; i < doc.pageCount(); ++i) {
                const QSizeF pt = doc.pageSize(i);
                pages.push_back(doc.render(i, (pt * dpi / 72.0).toSize()));
            }
        }
        QMetaObject::invokeMethod(this, [this, pages, error]() { emit finished(pages, error); }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
#else
    const QString exe = pdftoppmPath();
    if (exe.isEmpty()) {
        QTimer::singleShot(0, this, [this]() { emit finished({}, unavailableReason()); });
        return;
    }
    m_dir = std::make_unique<QTemporaryDir>();
    if (!m_dir->isValid()) {
        QTimer::singleShot(0, this, [this]() { emit finished({}, tr("No temporary folder is available.")); });
        return;
    }
    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &PdfImporter::finishProcess);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_process && m_process->state() == QProcess::NotRunning)
            finishProcess();
    });
    m_process->start(exe, {QStringLiteral("-png"), QStringLiteral("-r"), QString::number(dpi), pdfPath,
                           m_dir->filePath(QStringLiteral("page"))});
#endif
}

void PdfImporter::finishProcess()
{
    if (!m_process)
        return;
    QString error;
    QVector<QImage> pages;
    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0) {
        error = tr("The PDF could not be converted: %1").arg(QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed());
    } else {
        QDir dir(m_dir->path());
        QStringList files = dir.entryList({QStringLiteral("page*.png")}, QDir::Files);
        // pdftoppm zero-pads numbers, but sort numerically to be safe.
        std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) {
            auto num = [](const QString& s) { return s.mid(5, s.size() - 9).toInt(); };
            return num(a) < num(b);
        });
        for (const QString& f : files)
            pages.push_back(QImage(dir.filePath(f)));
        if (pages.isEmpty())
            error = tr("The PDF does not contain any pages.");
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_dir.reset();
    emit finished(pages, error);
}

} // namespace cb
