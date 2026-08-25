#include <QtTest>

#include <QRegularExpression>
#include <QString>
#include <QTemporaryFile>

#include <core/services/htmltemplateservice.h>
#include <core/vxpdfscheme.h>
#include <core/webresource.h>

namespace tests {

class TestHtmlTemplateService : public QObject {
  Q_OBJECT

private slots:
  void testFillThemeStylesWithContent_inlinesWebContent();
  void testFillThemeStylesWithContent_emptyContentSkipsStyleBlock();
  void testFillThemeStylesWithContent_highlightStillUsesLink();
  void testFillThemeStylesWithContent_rejectsClosingStyleTag();
  void testFillThemeStyles_pathBasedUnchanged();
  void testFillPdfResources_emitsSameOriginVxPdfUrls();
  void testFillPdfResources_moduleTypeOnlyForMjs();
  void testFillPdfResources_skipsDisabledAndGlobal();

private:
  // Mirrors PdfViewerConfig::defaultViewerResource(), which is private to that
  // class. Kept as a local fixture so this test stays in core_services' reach.
  static vnotex::WebResource pdfDefaultResource();
};

vnotex::WebResource TestHtmlTemplateService::pdfDefaultResource() {
  vnotex::WebResource res;
  res.m_template = QStringLiteral("web/pdf.js/web/pdf-viewer-template.html");
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("built_in");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/js/qwebchannel.js"),
                              QStringLiteral("web/js/eventemitter.js"),
                              QStringLiteral("web/js/utils.js"), QStringLiteral("web/js/vxcore.js"),
                              QStringLiteral("web/pdf.js/pdfviewercore.js")};
    res.m_resources.append(r);
  }
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("pdf.js");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/pdf.js/build/pdf.mjs"),
                              QStringLiteral("web/pdf.js/web/viewer.mjs")};
    r.m_styles = QStringList{QStringLiteral("web/pdf.js/web/viewer.css")};
    res.m_resources.append(r);
  }
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("pdf_viewer");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/pdf.js/pdfviewer.mjs")};
    r.m_styles = QStringList{QStringLiteral("web/pdf.js/pdfviewer.css")};
    res.m_resources.append(r);
  }
  return res;
}

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
  QVERIFY2(!tmpl.contains(QString::fromLatin1(kPlaceholder)), "placeholder should be replaced");
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
  vnotex::HtmlTemplateService::fillThemeStylesWithContent(tmpl, QStringLiteral("body{}"),
                                                          hlFile.fileName());

  QVERIFY2(tmpl.contains(QStringLiteral("<link rel=\"stylesheet\"")),
           qPrintable(QStringLiteral("expected <link> for highlight; got: %1").arg(tmpl)));
}

void TestHtmlTemplateService::testFillThemeStylesWithContent_rejectsClosingStyleTag() {
  QString tmpl =
      QStringLiteral("<html><head>%1</head></html>").arg(QString::fromLatin1(kPlaceholder));
  QString maliciousContent =
      QStringLiteral("body { color: red; }</style><script>alert(1)</script>");

  // The defensive guard should refuse to inline this. Suppress the expected qWarning.
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*</style>.*refusing.*")));

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

// ============ PDF resources (vxpdf:// + ESM) ============

// The viewer page is served over vxpdf://, so a file:// script/style URL would be
// cross-origin and Chromium would refuse to load the module. This is the gate.
void TestHtmlTemplateService::testFillPdfResources_emitsSameOriginVxPdfUrls() {
  vnotex::WebResource res = pdfDefaultResource();

  QString tmpl = QStringLiteral("<html><head><!-- VX_STYLES_PLACEHOLDER -->\n"
                                "<!-- VX_SCRIPTS_PLACEHOLDER --></head></html>");
  vnotex::HtmlTemplateService::fillPdfResources(tmpl, res);

  QVERIFY2(!tmpl.contains(QStringLiteral("file:")),
           qPrintable(
               QStringLiteral("no file: URL may survive in the PDF template; got: %1").arg(tmpl)));
  QVERIFY(!tmpl.contains(QStringLiteral("VX_STYLES_PLACEHOLDER")));
  QVERIFY(!tmpl.contains(QStringLiteral("VX_SCRIPTS_PLACEHOLDER")));

  static const QRegularExpression srcRe(QStringLiteral("(?:src|href)=\"([^\"]+)\""));
  auto it = srcRe.globalMatch(tmpl);
  int count = 0;
  while (it.hasNext()) {
    const QString url = it.next().captured(1);
    ++count;
    QVERIFY2(url.startsWith(vnotex::VxPdfScheme::origin() + vnotex::VxPdfScheme::assetPathPrefix()),
             qPrintable(QStringLiteral("resource URL is not a vxpdf asset URL: %1").arg(url)));
  }
  QVERIFY2(count > 0, "no resource URLs were emitted; the gate would be vacuous");
}

// Module scripts are deferred, classic ones are not. pdfviewer.mjs depends on
// running AFTER viewer.mjs, which only holds while the decision is by extension.
void TestHtmlTemplateService::testFillPdfResources_moduleTypeOnlyForMjs() {
  vnotex::WebResource res = pdfDefaultResource();

  QString tmpl = QStringLiteral("<html><head><!-- VX_STYLES_PLACEHOLDER -->\n"
                                "<!-- VX_SCRIPTS_PLACEHOLDER --></head></html>");
  vnotex::HtmlTemplateService::fillPdfResources(tmpl, res);

  static const QRegularExpression scriptRe(
      QStringLiteral("<script type=\"([^\"]+)\" src=\"([^\"]+)\"></script>"));
  auto it = scriptRe.globalMatch(tmpl);
  int mjsCount = 0;
  int classicCount = 0;
  while (it.hasNext()) {
    const auto m = it.next();
    const QString type = m.captured(1);
    const QString url = m.captured(2);
    const bool isMjs = url.endsWith(QStringLiteral(".mjs"));
    if (isMjs) {
      ++mjsCount;
      QCOMPARE(type, QStringLiteral("module"));
    } else {
      ++classicCount;
      QCOMPARE(type, QStringLiteral("text/javascript"));
    }
  }
  QVERIFY2(mjsCount >= 3, "expected pdf.mjs, viewer.mjs and pdfviewer.mjs as modules");
  QVERIFY2(classicCount >= 1, "expected the qwebchannel/vxcore glue to stay classic");
}

// A disabled or global resource block must not contribute tags, exactly like the
// generic fillResources() path.
void TestHtmlTemplateService::testFillPdfResources_skipsDisabledAndGlobal() {
  vnotex::WebResource res;
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("global_styles");
    r.m_enabled = true;
    r.m_styles = QStringList{QStringLiteral("web/global.css")};
    res.m_resources.append(r);
  }
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("off");
    r.m_enabled = false;
    r.m_scripts = QStringList{QStringLiteral("web/off.mjs")};
    res.m_resources.append(r);
  }
  {
    vnotex::WebResource::Resource r;
    r.m_name = QStringLiteral("on");
    r.m_enabled = true;
    r.m_scripts = QStringList{QStringLiteral("web/on.mjs")};
    res.m_resources.append(r);
  }

  QString tmpl = QStringLiteral("<html><head><!-- VX_STYLES_PLACEHOLDER -->\n"
                                "<!-- VX_SCRIPTS_PLACEHOLDER --></head></html>");
  vnotex::HtmlTemplateService::fillPdfResources(tmpl, res);

  QVERIFY(tmpl.contains(QStringLiteral("web/on.mjs")));
  QVERIFY(!tmpl.contains(QStringLiteral("web/off.mjs")));
  QVERIFY(!tmpl.contains(QStringLiteral("web/global.css")));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHtmlTemplateService)
#include "test_htmltemplateservice.moc"
