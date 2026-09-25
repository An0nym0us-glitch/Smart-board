#pragma once

#include <QImage>
#include <QObject>
#include <QVector>

#include <memory>

class QProcess;
class QTemporaryDir;

namespace cb {

/// Converts PDF pages to images for annotation.
///
/// Uses Qt PDF when ClassBoard is built against it; otherwise the Poppler command line tool
/// "pdftoppm" if it is installed (it is free and available for Windows). The conversion runs
/// asynchronously and never blocks the interface.
class PdfImporter : public QObject
{
    Q_OBJECT
public:
    explicit PdfImporter(QObject* parent = nullptr);
    ~PdfImporter() override;

    /// True if a PDF backend is available on this machine.
    static bool isAvailable();
    static QString unavailableReason();

    /// Starts converting; emits finished() when done.
    void start(const QString& pdfPath, int dpi = 110);

signals:
    void finished(const QVector<QImage>& pages, const QString& error);

private:
    void finishProcess();
    std::unique_ptr<QTemporaryDir> m_dir;
    QProcess* m_process = nullptr;
};

} // namespace cb
