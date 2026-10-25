#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QString>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QUrl>

#ifdef VNOTE_TEST_WEB_TRANSLATIONS
#include <QJSEngine>
#include <QTranslator>
#include <utils/webutils.h>
#endif

#include <core/markdownwebglobaloptions.h>
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
  void testProtectedTemplate_bundledAllowlist();
  void testProtectedTemplate_invalidIdentityAndMissingBundleFailClosed();

#ifdef VNOTE_TEST_WEB_TRANSLATIONS
  void testMarkdownWebGlobalOptions_headingFoldingBoolean();
  void testWebTranslationsFollowUiLanguage_data();
  void testWebTranslationsFollowUiLanguage();
  void testWebTranslationsAreInlineScriptSafe();
#endif

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
  // Regression test: the original path-based fillThemeStyles is preserved for Mind Map.
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

void TestHtmlTemplateService::testProtectedTemplate_bundledAllowlist() {
  const QFileInfo bundle(QStringLiteral(VNOTE_TEST_EXTRA_RCC));
  QVERIFY2(bundle.isFile(), qPrintable(bundle.absoluteFilePath()));
  const auto oldPaths = QDir::searchPaths(QStringLiteral("app"));
  const auto restorePaths =
      qScopeGuard([&]() { QDir::setSearchPaths(QStringLiteral("app"), oldPaths); });
  QDir::setSearchPaths(QStringLiteral("app"), {bundle.absolutePath()});
  vnotex::HtmlTemplateService service(nullptr);
  const auto token = QStringLiteral("0123456789abcdef0123456789abcdef");
  const auto nonce = QStringLiteral("fedcba9876543210fedcba9876543210");
  QHash<QString, QByteArray> resources;
  const auto html = service.protectedMarkdownViewerTemplate(
      token, nonce, QStringLiteral("pure"), false, QStringLiteral("katex"), resources);
  QVERIFY(!html.isEmpty());
  QVERIFY(!resources.isEmpty());
  const auto policy = vnotex::HtmlTemplateService::protectedContentSecurityPolicy(nonce);
  QVERIFY(html.contains(policy.toHtmlEscaped()));
  for (const auto *directive : {"default-src 'none'", "connect-src 'none'", "frame-src 'none'",
                                "object-src 'none'", "base-uri 'none'", "form-action 'none'"}) {
    QVERIFY2(policy.contains(QLatin1String(directive)), directive);
  }
  QVERIFY(policy.contains(QStringLiteral("script-src 'nonce-%1'").arg(nonce)));
  QVERIFY(!policy.contains(QStringLiteral("unsafe-eval")));
  static const QRegularExpression resourceTag(
      QStringLiteral("<(?:script|link)\\b[^>]*(?:src|href)=\"([^\"]+)\"[^>]*>"));
  auto tags = resourceTag.globalMatch(html);
  int count = 0;
  while (tags.hasNext()) {
    const auto match = tags.next();
    const QUrl url(match.captured(1));
    QCOMPARE(url.scheme(), QStringLiteral("vxnote"));
    QCOMPARE(url.host(), token);
    QVERIFY(url.query().isEmpty());
    QVERIFY(url.fragment().isEmpty());
    QVERIFY2(resources.contains(url.path()), qPrintable(url.toString()));
    QVERIFY2(!resources.value(url.path()).isEmpty(), qPrintable(url.toString()));
    if (match.captured(0).startsWith(QStringLiteral("<script"))) {
      QVERIFY(match.captured(0).contains(QStringLiteral("nonce=\"%1\"").arg(nonce)));
    }
    ++count;
  }
  QVERIFY(count > 0);
  static const QRegularExpression scriptTag(QStringLiteral("<script\\b[^>]*>"));
  auto scripts = scriptTag.globalMatch(html);
  while (scripts.hasNext()) {
    QVERIFY(scripts.next().captured(0).contains(QStringLiteral("nonce=\"%1\"").arg(nonce)));
  }
  for (auto it = resources.cbegin(); it != resources.cend(); ++it) {
    QVERIFY(it.key().startsWith(QStringLiteral("/app/")));
    QVERIFY(!it.key().contains(QStringLiteral("..")));
    QVERIFY(!it.value().isEmpty());
  }
  QVERIFY(!resources.contains(QStringLiteral("/app/user.css")));
  QVERIFY(!resources.contains(QStringLiteral("/files/private.md")));
  // A custom or traversal-like theme name cannot widen the shipped execution allowlist.
  QHash<QString, QByteArray> customResources;
  const auto custom =
      service.protectedMarkdownViewerTemplate(token, nonce, QStringLiteral("../../user-template"),
                                              false, QStringLiteral("katex"), customResources);
  QCOMPARE(custom, html);
  QCOMPARE(customResources, resources);
  // Renderer preferences cannot introduce an external engine or widen execution resources.
  for (const auto &renderer :
       {QStringLiteral("mathjax"), QString(), QStringLiteral("'; window.injected = true; //")}) {
    QHash<QString, QByteArray> fallbackResources;
    const auto fallback = service.protectedMarkdownViewerTemplate(
        token, nonce, QStringLiteral("pure"), false, renderer, fallbackResources);
    QCOMPARE(fallback, html);
    QCOMPARE(fallbackResources, resources);
  }
  QHash<QString, QByteArray> typstResources;
  const auto typst = service.protectedMarkdownViewerTemplate(
      token, nonce, QStringLiteral("pure"), false, QStringLiteral("typst"), typstResources);
  QVERIFY(typst.contains(policy.toHtmlEscaped()));
  QCOMPARE(typstResources, resources);
}

void TestHtmlTemplateService::testProtectedTemplate_invalidIdentityAndMissingBundleFailClosed() {
  vnotex::HtmlTemplateService service(nullptr);
  const auto valid = QStringLiteral("0123456789abcdef0123456789abcdef");
  QHash<QString, QByteArray> resources;
  for (const auto &identity :
       {QString(), QStringLiteral("../private"), valid + QStringLiteral("\" injected=\"yes")}) {
    resources.insert(QStringLiteral("/app/old.js"), QByteArray("old"));
    QVERIFY(service
                .protectedMarkdownViewerTemplate(identity, valid, QStringLiteral("pure"), false,
                                                 QStringLiteral("typst"), resources)
                .isEmpty());
    QVERIFY(resources.isEmpty());
    resources.insert(QStringLiteral("/app/old.js"), QByteArray("old"));
    QVERIFY(service
                .protectedMarkdownViewerTemplate(valid, identity, QStringLiteral("pure"), false,
                                                 QStringLiteral("typst"), resources)
                .isEmpty());
    QVERIFY(resources.isEmpty());
  }
  QTemporaryDir emptyDirectory;
  QVERIFY(emptyDirectory.isValid());
  const auto oldPaths = QDir::searchPaths(QStringLiteral("app"));
  const auto restorePaths =
      qScopeGuard([&]() { QDir::setSearchPaths(QStringLiteral("app"), oldPaths); });
  QDir::setSearchPaths(QStringLiteral("app"), {emptyDirectory.path()});
  resources.insert(QStringLiteral("/app/old.js"), QByteArray("old"));
  QVERIFY(service
              .protectedMarkdownViewerTemplate(valid, valid, QStringLiteral("pure"), false,
                                               QStringLiteral("typst"), resources)
              .isEmpty());
  QVERIFY(resources.isEmpty());
}

#ifdef VNOTE_TEST_WEB_TRANSLATIONS
void TestHtmlTemplateService::testMarkdownWebGlobalOptions_headingFoldingBoolean() {
  QJSEngine engine;
  engine.globalObject().setProperty("window", engine.newObject());
  vnotex::MarkdownWebGlobalOptions options;
  for (bool enabled : {true, false}) {
    options.m_headingFoldingEnabled = enabled;
    QVERIFY(!engine.evaluate(options.toJavascriptObject()).isError());
    QCOMPARE(engine.evaluate("window.vxOptions.headingFoldingEnabled").toBool(), enabled);
    QCOMPARE(engine.evaluate("window.vxI18n.tr('code.collapse')").toString(),
             QStringLiteral("Collapse"));
  }
}

void TestHtmlTemplateService::testWebTranslationsFollowUiLanguage_data() {
  QTest::addColumn<QString>("locale");
  QTest::addColumn<QStringList>("expected");
  QTest::newRow("simplified-chinese") << QStringLiteral("zh_CN")
                                      << QStringList{QStringLiteral("\u5927\u7eb2"),
                                                     QStringLiteral("\u672c\u9875\u76ee\u5f55"),
                                                     QStringLiteral("\u663e\u793a\u5927\u7eb2"),
                                                     QStringLiteral("\u9690\u85cf\u5927\u7eb2"),
                                                     QStringLiteral("\u590d\u5236"),
                                                     QStringLiteral("\u6298\u53e0"),
                                                     QStringLiteral("\u5c55\u5f00")};
  QTest::newRow("japanese")
      << QStringLiteral("ja")
      << QStringList{QStringLiteral("\u30a2\u30a6\u30c8\u30e9\u30a4\u30f3"),
                     QStringLiteral("\u3053\u306e\u30da\u30fc\u30b8\u306e\u5185\u5bb9"),
                     QStringLiteral("\u30a2\u30a6\u30c8\u30e9\u30a4\u30f3\u3092\u8868\u793a"),
                     QStringLiteral("\u30a2\u30a6\u30c8\u30e9\u30a4\u30f3\u3092\u975e\u8868\u793a"),
                     QStringLiteral("\u30b3\u30d4\u30fc"),
                     QStringLiteral("\u6298\u308a\u305f\u305f\u3080"),
                     QStringLiteral("\u5c55\u958b")};
}

void TestHtmlTemplateService::testWebTranslationsFollowUiLanguage() {
  QFETCH(QString, locale);
  QFETCH(QStringList, expected);
  QJSEngine engine;
  engine.globalObject().setProperty("window", engine.newObject());
  QVERIFY(!engine.evaluate(vnotex::WebUtils::translationScript()).isError());
  auto englishLookup = engine.globalObject().property("window").property("vxI18n").property("tr");

  QTranslator translator;
  QVERIFY(translator.load(QStringLiteral(VNOTE_TRANSLATIONS_DIR "/vnote_%1.qm").arg(locale)));
  QVERIFY(QCoreApplication::installTranslator(&translator));
  const auto cleanup =
      qScopeGuard([&translator]() { QCoreApplication::removeTranslator(&translator); });
  QVERIFY(!engine.evaluate(vnotex::WebUtils::translationScript()).isError());
  auto lookup = engine.globalObject().property("window").property("vxI18n").property("tr");
  const QStringList ids{"outline.title", "outline.onThisPage", "outline.show", "outline.hide",
                        "code.copy",     "code.collapse",      "code.expand"};
  for (int i = 0; i < ids.size(); ++i) {
    QCOMPARE(lookup.call({QJSValue(ids[i])}).toString(), expected[i]);
  }
  // An already-exported page keeps its snapshot when the application language changes.
  QCOMPARE(englishLookup.call({QJSValue("outline.title")}).toString(), QStringLiteral("Outline"));
  QVERIFY(QCoreApplication::removeTranslator(&translator));
  QVERIFY(!engine.evaluate(vnotex::WebUtils::translationScript()).isError());
  QCOMPARE(engine.evaluate("window.vxI18n.tr('outline.title')").toString(),
           QStringLiteral("Outline"));
}

void TestHtmlTemplateService::testWebTranslationsAreInlineScriptSafe() {
  class TextTranslator : public QTranslator {
  public:
    QString text;
    bool isEmpty() const override { return false; }
    QString translate(const char *p_context, const char *p_source, const char *,
                      int) const override {
      return qstrcmp(p_context, "WebUtils") == 0 && qstrcmp(p_source, "Outline") == 0 ? text
                                                                                      : QString();
    }
  } translator;
  translator.text =
      QStringLiteral("\"quoted\" \\ newline\n</ScRiPt><script>injected=true</script><!-- & "
                     "/* VX_SCRIPTS_PLACEHOLDER */") +
      QChar(0x2028) + QChar(0x2029);
  QVERIFY(QCoreApplication::installTranslator(&translator));
  const auto cleanup =
      qScopeGuard([&translator]() { QCoreApplication::removeTranslator(&translator); });
  auto script = vnotex::WebUtils::translationScript();
  QVERIFY(!script.contains(QLatin1Char('<')));
  script += QStringLiteral("\n/* VX_SCRIPTS_PLACEHOLDER */");
  script.replace(QStringLiteral("/* VX_SCRIPTS_PLACEHOLDER */"),
                 QStringLiteral("window.handlerLoaded = true;"));
  QJSEngine engine;
  engine.globalObject().setProperty("window", engine.newObject());
  QVERIFY(!engine.evaluate(script).isError());
  QCOMPARE(engine.evaluate("window.vxI18n.tr('outline.title')").toString(), translator.text);
  QVERIFY(engine.evaluate("window.handlerLoaded").toBool());
  for (const auto &id : {"unknown.id", "toString", "__proto__"}) {
    auto lookup = engine.globalObject().property("window").property("vxI18n").property("tr");
    QCOMPARE(lookup.call({QJSValue(QString::fromLatin1(id))}).toString(), QString::fromLatin1(id));
  }
  QVERIFY(engine.globalObject().property("injected").isUndefined());
}
#endif

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHtmlTemplateService)
#include "test_htmltemplateservice.moc"
