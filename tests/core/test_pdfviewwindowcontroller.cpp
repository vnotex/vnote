#include <QTest>
#include <QUrl>

#include <controllers/pdfviewwindowcontroller.h>

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
};

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewWindowController)
#include "test_pdfviewwindowcontroller.moc"
