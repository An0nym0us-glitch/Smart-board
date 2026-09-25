#include "document/Document.h"
#include "document/ShapeObject.h"
#include "document/StrokeObject.h"
#include "document/TextObject.h"
#include "export/Exporters.h"
#include "export/ZipWriter.h"
#include "app/ExportController.h"
#include "storage/PdfImporter.h"

#include <QSignalSpy>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace cb;

namespace {
quint32 le32(const QByteArray& b, int pos)
{
    return quint32(uchar(b[pos])) | (quint32(uchar(b[pos + 1])) << 8) | (quint32(uchar(b[pos + 2])) << 16)
        | (quint32(uchar(b[pos + 3])) << 24);
}
quint16 le16(const QByteArray& b, int pos) { return quint16(uchar(b[pos]) | (uchar(b[pos + 1]) << 8)); }

/// Reads the central directory of a ZIP archive and returns entry names.
QStringList zipEntries(const QByteArray& zip)
{
    QStringList names;
    const int eocd = zip.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (eocd < 0)
        return names;
    const int count = le16(zip, eocd + 10);
    int pos = static_cast<int>(le32(zip, eocd + 16));
    for (int i = 0; i < count; ++i) {
        if (le32(zip, pos) != 0x02014b50)
            return {};
        const int nameLen = le16(zip, pos + 28);
        const int extra = le16(zip, pos + 30);
        const int comment = le16(zip, pos + 32);
        names << QString::fromUtf8(zip.mid(pos + 46, nameLen));
        pos += 46 + nameLen + extra + comment;
    }
    return names;
}

std::unique_ptr<DocumentSnapshot> sample()
{
    Document doc;
    Page* page = doc.currentPage();
    InkStyle ink;
    ink.width = 8;
    page->insertObject(0, StrokeObject::fromPagePoints({{QPointF(100, 100), 1}, {QPointF(800, 600), 1}}, ink));
    page->insertObject(1, TextObject::create(QStringLiteral("Photosynthesis"), QPointF(200, 200), TextFormat()));
    page->insertObject(2, ShapeObject::createBox(ShapeKind::Circle, QRectF(900, 300, 300, 300), ShapeStyle()));
    doc.insertPage(1, doc.createPage());
    return doc.snapshot();
}
} // namespace

class TestExport : public QObject
{
    Q_OBJECT
private slots:
    void zipWriterProducesValidArchive()
    {
        ZipWriter zip;
        zip.addFile(QStringLiteral("a.txt"), QByteArray(1000, 'a'));
        zip.addFile(QStringLiteral("dir/b.bin"), QByteArray("xyz"), false);
        const QByteArray data = zip.finish();
        QCOMPARE(zipEntries(data), (QStringList{QStringLiteral("a.txt"), QStringLiteral("dir/b.bin")}));
        // Deflated entry: method 8 and smaller than the input.
        QCOMPARE(le16(data, 8), quint16(8));
        QVERIFY(le32(data, 18) < 1000);
    }

    void pdfExport()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("lesson.pdf"));
        QString error;
        int lastProgress = 0;
        QVERIFY2(PdfExporter::exportPages(*sample(), path, &error, [&](int done, int) {
                     lastProgress = done;
                     return true;
                 }),
                 qPrintable(error));
        QCOMPARE(lastProgress, 2);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.startsWith("%PDF"));
        QCOMPARE(bytes.count("/Type /Page\n") + bytes.count("/Type /Page\r") + bytes.count("/Type /Page "), 2);
    }

    void pptxExport()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("lesson.pptx"));
        QString error;
        QVERIFY2(PptxExporter::exportPages(*sample(), path, &error), qPrintable(error));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList entries = zipEntries(f.readAll());
        for (const char* required : {"[Content_Types].xml", "_rels/.rels", "ppt/presentation.xml",
                                     "ppt/_rels/presentation.xml.rels", "ppt/slideMasters/slideMaster1.xml",
                                     "ppt/slideLayouts/slideLayout1.xml", "ppt/theme/theme1.xml", "ppt/slides/slide1.xml",
                                     "ppt/slides/slide2.xml", "ppt/media/image1.png", "ppt/media/image2.png"})
            QVERIFY2(entries.contains(QString::fromLatin1(required)), required);
    }

    void pngExport()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        QString error;
        QVERIFY2(ImageExporter::exportPage(*sample(), 0, path, &error), qPrintable(error));
        const QImage img(path);
        QCOMPARE(img.width(), 3840);
        QCOMPARE(img.height(), 2160);
    }

    void writeSamplesForExternalValidation()
    {
        // CI / developers can open these in PowerPoint, LibreOffice or a PDF viewer.
        const QString dir = qEnvironmentVariable("CLASSBOARD_EXPORT_DIR");
        if (dir.isEmpty())
            QSKIP("Set CLASSBOARD_EXPORT_DIR to write sample exports");
        QString error;
        QVERIFY(PptxExporter::exportPages(*sample(), dir + QStringLiteral("/sample.pptx"), &error));
        QVERIFY(PdfExporter::exportPages(*sample(), dir + QStringLiteral("/sample.pdf"), &error));
    }

    void pdfImportRoundTrip()
    {
        if (!PdfImporter::isAvailable())
            QSKIP("No PDF backend (Qt PDF or Poppler's pdftoppm) on this machine");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("roundtrip.pdf"));
        QVERIFY(PdfExporter::exportPages(*sample(), path, nullptr));
        PdfImporter importer;
        QSignalSpy finished(&importer, &PdfImporter::finished);
        importer.start(path, 50);
        QVERIFY(finished.wait(20000));
        const auto pages = finished.first().at(0).value<QVector<QImage>>();
        QVERIFY2(finished.first().at(1).toString().isEmpty(), qPrintable(finished.first().at(1).toString()));
        QCOMPARE(pages.size(), 2);
        // 16:9 pages rendered at 50 dpi.
        QVERIFY(qAbs(pages.first().width() / double(pages.first().height()) - 16.0 / 9.0) < 0.02);
    }

    void pageRanges()
    {
        QVector<int> pages;
        QVERIFY(ExportController::parseRange(QStringLiteral("1-3, 5"), 6, &pages));
        QCOMPARE(pages, (QVector<int>{0, 1, 2, 4}));
        QVERIFY(ExportController::parseRange(QStringLiteral("4-"), 6, &pages));
        QCOMPARE(pages, (QVector<int>{3, 4, 5}));
        QVERIFY(ExportController::parseRange(QStringLiteral("2 2 1"), 3, &pages));
        QCOMPARE(pages, (QVector<int>{0, 1}));
        QVERIFY(!ExportController::parseRange(QStringLiteral("0"), 3, &pages));
        QVERIFY(!ExportController::parseRange(QStringLiteral("2-9"), 3, &pages));
        QVERIFY(!ExportController::parseRange(QStringLiteral("abc"), 3, &pages));
    }
};

QTEST_MAIN(TestExport)
#include "tst_export.moc"
