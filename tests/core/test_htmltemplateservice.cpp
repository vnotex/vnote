#include <QtTest>

#include <QRegularExpression>
#include <QString>
#include <QTemporaryFile>

#include <core/services/htmltemplateservice.h>

namespace tests {

class TestHtmlTemplateService : public QObject {
  Q_OBJECT

private slots:
  void testFillThemeStylesWithContent_inlinesWebContent();
  void testFillThemeStylesWithContent_emptyContentSkipsStyleBlock();
  void testFillThemeStylesWithContent_highlightStillUsesLink();
  void testFillThemeStylesWithContent_rejectsClosingStyleTag();
  void testFillThemeStyles_pathBasedUnchanged();
};

namespace {
constexpr const char *kPlaceholder = "<!-- VX_THEME_STYLES_PLACEHOLDER -->";
} // anonymous namespace

void TestHtmlTemplateService::testFillThemeStylesWithContent_inlinesWebContent() {
  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  QString webCss = QStringLiteral("body { color: red; }");
  vnotex::HtmlTemplateService::fillThemeStylesWithContent(tmpl, webCss, QString());

  QVERIFY2(tmpl.contains(QStringLiteral("<style type=\"text/css\">")),
           qPrintable(QStringLiteral("expected <style> opener; got: %1").arg(tmpl)));
  QVERIFY2(tmpl.contains(webCss),
           qPrintable(QStringLiteral("expected CSS body inlined; got: %1").arg(tmpl)));
  QVERIFY2(!tmpl.contains(QString::fromLatin1(kPlaceholder)),
           "placeholder should be replaced");
}

void TestHtmlTemplateService::testFillThemeStylesWithContent_emptyContentSkipsStyleBlock() {
  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  vnotex::HtmlTemplateService::fillThemeStylesWithContent(tmpl, QString(), QString());

  QVERIFY2(!tmpl.contains(QStringLiteral("<style")),
           qPrintable(QStringLiteral("empty content should skip style block; got: %1").arg(tmpl)));
}

void TestHtmlTemplateService::testFillThemeStylesWithContent_highlightStillUsesLink() {
  // Use a temp file path for the highlight stylesheet so fillStyleTag generates a <link>.
  QTemporaryFile hlFile;
  QVERIFY(hlFile.open());
  hlFile.write("/* highlight css */");
  hlFile.flush();

  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  vnotex::HtmlTemplateService::fillThemeStylesWithContent(
      tmpl, QStringLiteral("body{}"), hlFile.fileName());

  QVERIFY2(tmpl.contains(QStringLiteral("<link rel=\"stylesheet\"")),
           qPrintable(QStringLiteral("expected <link> for highlight; got: %1").arg(tmpl)));
}

void TestHtmlTemplateService::testFillThemeStylesWithContent_rejectsClosingStyleTag() {
  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  QString maliciousContent =
      QStringLiteral("body { color: red; }</style><script>alert(1)</script>");

  // The defensive guard should refuse to inline this. Suppress the expected qWarning.
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral(".*</style>.*refusing.*")));

  vnotex::HtmlTemplateService::fillThemeStylesWithContent(tmpl, maliciousContent, QString());

  QVERIFY2(!tmpl.contains(QStringLiteral("<style")),
           qPrintable(QStringLiteral("malicious content must not be inlined; got: %1").arg(tmpl)));
  QVERIFY2(!tmpl.contains(QStringLiteral("<script>")),
           "script tag must be rejected too (collateral)");
}

void TestHtmlTemplateService::testFillThemeStyles_pathBasedUnchanged() {
  // Regression test: the original path-based fillThemeStyles is preserved for MindMap.
  QTemporaryFile webFile;
  QVERIFY(webFile.open());
  webFile.write("/* web css */");
  webFile.flush();

  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  vnotex::HtmlTemplateService::fillThemeStyles(tmpl, webFile.fileName(), QString());

  QVERIFY2(tmpl.contains(QStringLiteral("<link rel=\"stylesheet\"")),
           qPrintable(QStringLiteral("path-based should produce <link>; got: %1").arg(tmpl)));
  QVERIFY2(!tmpl.contains(QStringLiteral("<style")),
           "path-based should NOT inline content as <style>");
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHtmlTemplateService)
#include "test_htmltemplateservice.moc"
