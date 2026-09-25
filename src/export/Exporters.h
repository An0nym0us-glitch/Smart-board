#pragma once

#include "document/Document.h"

#include <functional>

namespace cb {

/// Progress callback: (done, total). Return false to cancel.
using ExportProgress = std::function<bool(int done, int total)>;

/// Vector PDF export: text stays text, shapes and ink stay vector, images are embedded once per
/// page at full quality. One PDF page per lesson page, 16:9 like the board.
class PdfExporter
{
public:
    static bool exportPages(const DocumentSnapshot& snapshot, const QString& path, QString* error,
                            const ExportProgress& progress = {});
};

/// PowerPoint (.pptx) export. Each page becomes a slide with a high resolution rendering of the
/// page (native conversion of ink/formulas is not possible in PowerPoint, so visual fidelity is
/// preserved instead) at the exact 16:9 slide size.
class PptxExporter
{
public:
    static bool exportPages(const DocumentSnapshot& snapshot, const QString& path, QString* error,
                            const ExportProgress& progress = {});
    /// Builds the package in memory (used by tests).
    static QByteArray buildPackage(const QVector<QByteArray>& slidePngs, const QString& title);
};

/// PNG export of single pages.
class ImageExporter
{
public:
    static QImage renderPage(const Page& page, const ImageStore& images, const CoordinateSystem& coordinates,
                             int pixelWidth);
    static bool exportPage(const DocumentSnapshot& snapshot, int index, const QString& path, QString* error);
};

} // namespace cb
