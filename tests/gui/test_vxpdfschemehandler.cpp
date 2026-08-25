// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_vxpdfschemehandler.cpp
//
// Unit coverage for the `vxpdf://` scheme handler.
//
// `requestStarted()` takes a `QWebEngineUrlRequestJob`, which has no public
// constructor, so the ROUTING DECISION was factored out into the pure
// `VxPdfScheme::routeFor()` and is table-tested exhaustively below. What remains
// untestable without loading a real page is only the mechanical tail of
// `requestStarted()`: opening the device and calling `reply()`/`fail()`. In
// particular the reply-device lifetime (every `QFile`/`QBuffer` is parented to
// the job) still has to be covered by a manual/integration run.
//
// Also covered: the MIME map and the document token registry — the two halves
// that CAN drift silently.
//
//   * A wrong MIME type on a `.mjs` fails the module load with no useful error,
//     because Chromium MIME-checks module scripts strictly.
//   * A token that is never revoked keeps a registry entry (and therefore a
//     readable path) alive for the whole process lifetime.

#include <QtTest>

#include <QSet>
#include <QString>

#include <core/vxpdfscheme.h>
#include <gui/services/vxpdfschemehandler.h>

using namespace vnotex;

Q_DECLARE_METATYPE(vnotex::VxPdfScheme::Route::Kind)

namespace tests {

class TestVxPdfSchemeHandler : public QObject {
  Q_OBJECT

private slots:
  void mimeTypeForPath_data();
  void mimeTypeForPath();
  void moduleScriptsGetAJavaScriptMimeType();
  void routeFor_data();
  void routeFor();
  void everyDefaultResourceIsRoutable();
  void registerDocumentReturnsAUsableUniqueToken();
  void registerDocumentRejectsAnEmptyPath();
  void unregisterDocumentRevokesTheToken();
  void unregisterIsIdempotentAndUnknownTokensAreUnknown();

private:
  static VxPdfSchemeHandler *makeHandler(QObject *p_parent);
};

VxPdfSchemeHandler *TestVxPdfSchemeHandler::makeHandler(QObject *p_parent) {
  // Neither dependency is touched by the token registry, so a null ConfigMgr2
  // and an empty template accessor are enough here.
  return new VxPdfSchemeHandler(nullptr, []() { return QString(); }, p_parent);
}

void TestVxPdfSchemeHandler::mimeTypeForPath_data() {
  QTest::addColumn<QString>("path");
  QTest::addColumn<QByteArray>("mime");

  QTest::newRow("html") << QStringLiteral("web/pdf.js/web/viewer.html")
                        << QByteArrayLiteral("text/html");
  QTest::newRow("css") << QStringLiteral("web/pdf.js/web/viewer.css")
                       << QByteArrayLiteral("text/css");
  QTest::newRow("mjs") << QStringLiteral("web/pdf.js/build/pdf.mjs")
                       << QByteArrayLiteral("text/javascript");
  QTest::newRow("js") << QStringLiteral("web/js/qwebchannel.js")
                      << QByteArrayLiteral("text/javascript");
  QTest::newRow("json") << QStringLiteral("web/pdf.js/web/locale/locale.json")
                        << QByteArrayLiteral("application/json");
  QTest::newRow("wasm") << QStringLiteral("web/pdf.js/web/wasm/openjpeg.wasm")
                        << QByteArrayLiteral("application/wasm");
  QTest::newRow("svg") << QStringLiteral("web/pdf.js/web/images/loading.svg")
                       << QByteArrayLiteral("image/svg+xml");
  QTest::newRow("ftl") << QStringLiteral("web/pdf.js/web/locale/en-US/viewer.ftl")
                       << QByteArrayLiteral("text/plain");
  QTest::newRow("bcmap") << QStringLiteral("web/pdf.js/web/cmaps/78-EUC-H.bcmap")
                         << QByteArrayLiteral("application/octet-stream");
  QTest::newRow("ttf") << QStringLiteral("web/pdf.js/web/standard_fonts/Liberation.ttf")
                       << QByteArrayLiteral("font/ttf");
  QTest::newRow("uppercase suffix")
      << QStringLiteral("web/pdf.js/build/PDF.MJS") << QByteArrayLiteral("text/javascript");
  QTest::newRow("unknown falls back") << QStringLiteral("web/pdf.js/web/mystery.xyz")
                                      << QByteArrayLiteral("application/octet-stream");
}

void TestVxPdfSchemeHandler::mimeTypeForPath() {
  QFETCH(QString, path);
  QFETCH(QByteArray, mime);
  QCOMPARE(VxPdfSchemeHandler::mimeTypeForPath(path), mime);
}

// The single most breakage-prone entry: a generic type here makes every module
// script fail to load, with a Chromium error that names neither the file nor
// the MIME type as the cause.
void TestVxPdfSchemeHandler::moduleScriptsGetAJavaScriptMimeType() {
  static const QSet<QByteArray> c_acceptable = {QByteArrayLiteral("text/javascript"),
                                                QByteArrayLiteral("application/javascript")};
  const auto mime = VxPdfSchemeHandler::mimeTypeForPath(QStringLiteral("pdf.worker.mjs"));
  QVERIFY2(c_acceptable.contains(mime),
           qPrintable(QStringLiteral("bad .mjs MIME: %1").arg(QString::fromLatin1(mime))));
}

using Kind = VxPdfScheme::Route::Kind;

void TestVxPdfSchemeHandler::routeFor_data() {
  QTest::addColumn<QByteArray>("method");
  QTest::addColumn<QString>("host");
  QTest::addColumn<QString>("path");
  QTest::addColumn<Kind>("kind");
  QTest::addColumn<QString>("target");

  const QString viewerPath =
      VxPdfScheme::assetPathPrefix() + VxPdfScheme::viewerTemplateRelativePath();

  // --- happy paths -------------------------------------------------------
  QTest::newRow("GET viewer template")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf") << viewerPath << Kind::ViewerTemplate
      << VxPdfScheme::viewerTemplateRelativePath();
  QTest::newRow("HEAD viewer template")
      << QByteArrayLiteral("HEAD") << QStringLiteral("pdf") << viewerPath << Kind::ViewerTemplate
      << VxPdfScheme::viewerTemplateRelativePath();
  QTest::newRow("GET module") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                              << QStringLiteral("/asset/web/pdf.js/build/pdf.mjs") << Kind::Asset
                              << QStringLiteral("web/pdf.js/build/pdf.mjs");
  QTest::newRow("GET allowlisted classic script")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/asset/web/js/qwebchannel.js") << Kind::Asset
      << QStringLiteral("web/js/qwebchannel.js");
  QTest::newRow("GET document") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                                << QStringLiteral("/document/tok-1") << Kind::Document
                                << QStringLiteral("tok-1");

  // --- method ------------------------------------------------------------
  // Checked before anything else, so even a well-formed asset is denied.
  QTest::newRow("POST asset") << QByteArrayLiteral("POST") << QStringLiteral("pdf")
                              << QStringLiteral("/asset/web/pdf.js/build/pdf.mjs")
                              << Kind::DenyMethod << QString();
  QTest::newRow("PUT document") << QByteArrayLiteral("PUT") << QStringLiteral("pdf")
                                << QStringLiteral("/document/tok-1") << Kind::DenyMethod
                                << QString();
  QTest::newRow("DELETE") << QByteArrayLiteral("DELETE") << QStringLiteral("pdf") << viewerPath
                          << Kind::DenyMethod << QString();
  QTest::newRow("lowercase get is not GET") << QByteArrayLiteral("get") << QStringLiteral("pdf")
                                            << viewerPath << Kind::DenyMethod << QString();

  // --- host / unknown routes --------------------------------------------
  QTest::newRow("wrong host") << QByteArrayLiteral("GET") << QStringLiteral("evil")
                              << QStringLiteral("/asset/web/pdf.js/build/pdf.mjs") << Kind::NotFound
                              << QString();
  QTest::newRow("empty host") << QByteArrayLiteral("GET") << QString() << viewerPath
                              << Kind::NotFound << QString();
  QTest::newRow("unknown route") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                                 << QStringLiteral("/config/vnotex.json") << Kind::NotFound
                                 << QString();
  QTest::newRow("bare root") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                             << QStringLiteral("/") << Kind::NotFound << QString();
  QTest::newRow("empty document token")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf") << QStringLiteral("/document/")
      << Kind::NotFound << QString();
  // A token must be one segment: `/document/x/../../y` must never become a path.
  QTest::newRow("document token with a slash")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/document/tok/../../secrets.txt") << Kind::NotFound << QString();

  // --- path rejection ----------------------------------------------------
  // QUrl::path() has already decoded percent-escapes, so an encoded traversal
  // arrives here as literal `..` and is caught by the same check.
  QTest::newRow("traversal") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                             << QStringLiteral("/asset/web/pdf.js/../../../vnotex.json")
                             << Kind::DenyPath << QString();
  QTest::newRow("decoded traversal")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/asset/web/pdf.js/../secrets.txt") << Kind::DenyPath << QString();
  QTest::newRow("dot segment") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                               << QStringLiteral("/asset/web/pdf.js/./build/pdf.mjs")
                               << Kind::DenyPath << QString();
  QTest::newRow("absolute path") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                                 << QStringLiteral("/asset//web/pdf.js/build/pdf.mjs")
                                 << Kind::DenyPath << QString();
  QTest::newRow("windows drive") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                                 << QStringLiteral("/asset/C:/Windows/win.ini") << Kind::DenyPath
                                 << QString();
  QTest::newRow("backslash") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                             << QStringLiteral("/asset/web\\pdf.js\\build\\pdf.mjs")
                             << Kind::DenyPath << QString();
  QTest::newRow("empty asset path") << QByteArrayLiteral("GET") << QStringLiteral("pdf")
                                    << QStringLiteral("/asset/") << Kind::DenyPath << QString();

  // --- least privilege ---------------------------------------------------
  // The handler is NOT a generic proxy for the deployed web/ tree.
  QTest::newRow("markdown asset is off limits")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/asset/web/js/markdownviewer.js") << Kind::DenyPath << QString();
  QTest::newRow("user css is off limits")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/asset/web/css/user.css") << Kind::DenyPath << QString();
  QTest::newRow("themes are off limits")
      << QByteArrayLiteral("GET") << QStringLiteral("pdf")
      << QStringLiteral("/asset/themes/pure/palette.json") << Kind::DenyPath << QString();
}

void TestVxPdfSchemeHandler::routeFor() {
  QFETCH(QByteArray, method);
  QFETCH(QString, host);
  QFETCH(QString, path);
  QFETCH(Kind, kind);
  QFETCH(QString, target);

  const auto route = VxPdfScheme::routeFor(method, host, path);
  QCOMPARE(static_cast<int>(route.m_kind), static_cast<int>(kind));
  if (!target.isEmpty()) {
    QCOMPARE(route.m_target, target);
  }
}

// The allowlist is exact, so tightening it must not silently stop serving a
// script the PDF template actually injects. This pins the two lists together.
void TestVxPdfSchemeHandler::everyDefaultResourceIsRoutable() {
  const QStringList shipped = {QStringLiteral("web/js/qwebchannel.js"),
                               QStringLiteral("web/js/eventemitter.js"),
                               QStringLiteral("web/js/utils.js"),
                               QStringLiteral("web/js/vxcore.js"),
                               QStringLiteral("web/pdf.js/pdfviewercore.js"),
                               QStringLiteral("web/pdf.js/build/pdf.mjs"),
                               QStringLiteral("web/pdf.js/build/pdf.worker.mjs"),
                               QStringLiteral("web/pdf.js/web/viewer.mjs"),
                               QStringLiteral("web/pdf.js/web/viewer.css"),
                               QStringLiteral("web/pdf.js/pdfviewer.mjs"),
                               QStringLiteral("web/pdf.js/pdfviewer.css"),
                               QStringLiteral("web/pdf.js/web/locale/locale.json"),
                               QStringLiteral("web/pdf.js/web/locale/en-US/viewer.ftl"),
                               QStringLiteral("web/pdf.js/web/images/loading.svg"),
                               QStringLiteral("web/pdf.js/web/cmaps/78-EUC-H.bcmap"),
                               QStringLiteral("web/pdf.js/web/wasm/openjpeg.wasm"),
                               QStringLiteral("web/pdf.js/web/standard_fonts/LiberationSans.ttf")};

  for (const auto &rel : shipped) {
    const auto route = VxPdfScheme::routeFor(QByteArrayLiteral("GET"), VxPdfScheme::host(),
                                             VxPdfScheme::assetPathPrefix() + rel);
    QVERIFY2(route.m_kind == Kind::Asset || route.m_kind == Kind::ViewerTemplate,
             qPrintable(QStringLiteral("a shipped resource is not routable: %1").arg(rel)));
  }
}

void TestVxPdfSchemeHandler::registerDocumentReturnsAUsableUniqueToken() {
  auto *handler = makeHandler(this);

  const auto tokenA = handler->registerDocument(QStringLiteral("C:/tmp/a.pdf"));
  const auto tokenB = handler->registerDocument(QStringLiteral("C:/tmp/b.pdf"));

  QVERIFY(!tokenA.isEmpty());
  QVERIFY(!tokenB.isEmpty());
  QVERIFY2(tokenA != tokenB, "two documents must never share a token");
  QVERIFY(handler->hasDocument(tokenA));
  QVERIFY(handler->hasDocument(tokenB));

  // The token must survive percent-encoding into the `?file=` query unchanged,
  // or the viewer would request a document the registry does not know.
  QCOMPARE(QUrl::toPercentEncoding(tokenA), tokenA.toUtf8());

  // Registering the SAME path twice still yields distinct tokens: two windows on
  // one file each own their own lifetime.
  const auto tokenA2 = handler->registerDocument(QStringLiteral("C:/tmp/a.pdf"));
  QVERIFY(tokenA2 != tokenA);
  QVERIFY(handler->hasDocument(tokenA));
}

void TestVxPdfSchemeHandler::registerDocumentRejectsAnEmptyPath() {
  auto *handler = makeHandler(this);
  QVERIFY(handler->registerDocument(QString()).isEmpty());
}

void TestVxPdfSchemeHandler::unregisterDocumentRevokesTheToken() {
  auto *handler = makeHandler(this);

  const auto token = handler->registerDocument(QStringLiteral("C:/tmp/a.pdf"));
  QVERIFY(handler->hasDocument(token));

  handler->unregisterDocument(token);
  QVERIFY2(!handler->hasDocument(token),
           "a revoked token must not keep the document path readable");
}

void TestVxPdfSchemeHandler::unregisterIsIdempotentAndUnknownTokensAreUnknown() {
  auto *handler = makeHandler(this);

  QVERIFY(!handler->hasDocument(QStringLiteral("never-registered")));

  const auto token = handler->registerDocument(QStringLiteral("C:/tmp/a.pdf"));
  handler->unregisterDocument(token);
  // PdfViewWindow2 revokes on reload AND in the destructor, so a double revoke is
  // an ordinary path, not an error.
  handler->unregisterDocument(token);
  handler->unregisterDocument(QStringLiteral("never-registered"));
  QVERIFY(!handler->hasDocument(token));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVxPdfSchemeHandler)
#include "test_vxpdfschemehandler.moc"
