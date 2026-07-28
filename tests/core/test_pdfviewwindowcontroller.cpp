#include <QTest>
#include <QUrl>

#include <controllers/pdfviewwindowcontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>

using namespace vnotex;

namespace tests {

class TestPdfViewWindowController : public QObject {
  Q_OBJECT

private slots:
  // ============ preparePdfUrl ============

  void testPreparePdfUrl_validPaths()
  {
    auto state = PdfViewWindowController::preparePdfUrl(
        "C:/notebooks/docs/test.pdf", "C:/config/web/pdfviewer/index.html");
    QVERIFY(state.valid);
    QVERIFY(!state.templateUrl.isEmpty());
    // Template URL should be based on the template path
    QVERIFY(state.templateUrl.toLocalFile().contains("index.html")
            || state.templateUrl.toString().contains("index.html"));
    // Query should contain encoded file URL
    QVERIFY(state.templateUrl.hasQuery());
    QVERIFY(state.templateUrl.query().contains("file="));
  }

  void testPreparePdfUrl_emptyContentPath()
  {
    auto state = PdfViewWindowController::preparePdfUrl(
        "", "C:/config/web/pdfviewer/index.html");
    QVERIFY(!state.valid);
  }

  void testPreparePdfUrl_emptyTemplatePath()
  {
    auto state = PdfViewWindowController::preparePdfUrl(
        "C:/notebooks/docs/test.pdf", "");
    QVERIFY(!state.valid);
  }

  void testPreparePdfUrl_bothEmpty()
  {
    auto state = PdfViewWindowController::preparePdfUrl("", "");
    QVERIFY(!state.valid);
  }

  void testPreparePdfUrl_specialCharsInFilename()
  {
    // File names with # + & should be percent-encoded
    auto state = PdfViewWindowController::preparePdfUrl(
        "C:/notebooks/docs/file #1 & notes + extra.pdf",
        "C:/config/web/pdfviewer/index.html");
    QVERIFY(state.valid);
    QString query = state.templateUrl.query(QUrl::FullyEncoded);
    // The special characters should be encoded in the query
    QVERIFY(!query.contains("#") || query.contains("%23"));
    QVERIFY(!query.contains("&") || query.contains("%26"));
    QVERIFY(!query.contains("+") || query.contains("%2B"));
  }

  // ============ buildAbsolutePath (external files) ============

  // An external file (e.g. drag&dropped into the main window) is represented by
  // a NodeIdentifier with an EMPTY notebookId and the absolute on-disk path in
  // relativePath. buildAbsolutePath must return that absolute path verbatim
  // (the old code returned empty because NodeIdentifier::isValid() is false when
  // notebookId is empty, which produced a blank PDF page). The external branch
  // touches no service, so an empty ServiceLocator (no vxcore context) suffices.
  void testBuildAbsolutePath_externalFile()
  {
    ServiceLocator services;
    PdfViewWindowController controller(services);

    NodeIdentifier nodeId;
    nodeId.relativePath = QStringLiteral("C:/tmp/report.pdf");
    QCOMPARE(controller.buildAbsolutePath(nodeId), nodeId.relativePath);
  }

  void testBuildAbsolutePath_externalFileUnicode()
  {
    ServiceLocator services;
    PdfViewWindowController controller(services);

    NodeIdentifier nodeId;
    // A Chinese-named external PDF ("量子算法综述.pdf").
    nodeId.relativePath = QString::fromUtf8("Q:/tmp/\xE9\x87\x8F\xE5\xAD\x90"
                                            "\xE7\xAE\x97\xE6\xB3\x95\xE7\xBB\xBC\xE8\xBF\xB0.pdf");
    QCOMPARE(controller.buildAbsolutePath(nodeId), nodeId.relativePath);
  }

  void testBuildAbsolutePath_emptyIdentifier()
  {
    ServiceLocator services;
    PdfViewWindowController controller(services);

    NodeIdentifier nodeId; // empty notebookId + empty relativePath
    QVERIFY(controller.buildAbsolutePath(nodeId).isEmpty());
  }
};

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewWindowController)
#include "test_pdfviewwindowcontroller.moc"
