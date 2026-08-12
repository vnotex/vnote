// test_wkhtmltopdfhtmlpatch.cpp - Tests for the wkhtmltopdf HTML patches.
//
// Regression context:
//  1. wkhtmltopdf's embedded QtWebKit never finishes rendering ("Loading pages 50%" forever) when
//     a CSS `stroke` is applied to an SVG <marker>. Mermaid emits exactly that, so every note
//     containing a Mermaid diagram used to hang the PDF export indefinitely.
//  2. The same renderer has no SVG intrinsic-aspect-ratio support, so Mermaid's
//     `<svg width="100%" viewBox="...">` resolved to zero height and every diagram was silently
//     dropped from the PDF.
//  3. It does not fall back per glyph along the CSS font-family list either: it renders
//     everything with the first INSTALLED family, so CJK text was served by the Qt system
//     fallback (MS UI Gothic, JIS coverage) and simplified-only characters came out as squares.
#include <QJSEngine>
#include <QPageLayout>
#include <QPageSize>
#include <QtTest>

#include <export/wkhtmltopdfhtmlpatch.h>

using namespace vnotex;

namespace tests {

class TestWkhtmltopdfHtmlPatch : public QObject {
  Q_OBJECT

private slots:
  void testInsertsBeforeHead();
  void testInsertsBeforeMixedCaseHead();
  void testInsertsBeforeLastHead();
  void testMissingHeadFails();
  void testMissingHeadLeavesInputUntouched();
  void testUtf8BytesArePreserved();
  void testNonUtf8BytesArePreserved();
  void testSelectorIsScopedToMermaid();
  void testStyleDisablesWrapperScrolling();
  void testSizeFixScriptIsInserted();
  void testSizeFixScriptScalesToParentWidth();

  // Behavioral tests: the emitted script is executed against a mocked DOM.
  void testScriptScalesWideGraphToParentWidth();
  void testScriptKeepsNarrowGraphAtNaturalSize();
  void testScriptHandlesCommaSeparatedViewBox();
  void testScriptIgnoresGraphWithoutViewBox();
  void testScriptFallsBackWhenParentHasNoWidth();
  void testScriptRunsOnDomContentLoadedWhileLoading();
  void testScriptClampsTallGraphToPageHeight();
  void testScriptClampsAfterWidthScaling();
  void testScriptLeavesShortGraphUnclamped();
  void testScriptWithoutHeightLimitDoesNotClamp();
  void testScriptUsesPassedWidthWhenDomReportsNone();
  void testScriptIgnoresDomWidthEntirely();

  // Page geometry derivation.
  void testContentWidthPxA4();
  void testContentWidthPxFollowsMargins();
  void testContentWidthPxZeroWhenGuttersConsumeIt();
  void testPageContentHeightPxA4();
  void testPageContentHeightPxFollowsMargins();
  void testPageContentHeightPxFollowsOrientation();
  void testPageContentHeightPxZeroForEmptyLayout();

  // Rasterized Mermaid diagrams (<img>) are clamped like the vector ones.
  void testScriptClampsMermaidPngToWidth();
  void testScriptLeavesSmallMermaidPngAlone();
  void testScriptIgnoresImageWithoutMermaidSize();

  // Font override (the CJK tofu workaround).
  void testFontOverrideIsInserted();
  void testFontOverrideEmptyWithoutFamilies();
  void testFontOverrideKeepsCodeMonospace();
  void testFontOverrideRejectsNonAsciiFamily();
  void testFontOverrideCannotBreakOutOfTheRule();
};

void TestWkhtmltopdfHtmlPatch::testInsertsBeforeHead() {
  QByteArray html = "<html><head><title>t</title></head><body>b</body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QVERIFY(html.contains(c_wkhtmltopdfMermaidFixStyle));
  QVERIFY(html.indexOf(c_wkhtmltopdfMermaidFixStyle) < html.indexOf("</head>"));
}

void TestWkhtmltopdfHtmlPatch::testInsertsBeforeMixedCaseHead() {
  QByteArray html = "<html><HEAD></HEAD><body></body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QVERIFY(html.indexOf(c_wkhtmltopdfMermaidFixStyle) < html.indexOf("</HEAD>"));
}

void TestWkhtmltopdfHtmlPatch::testInsertsBeforeLastHead() {
  // A raw "</head>" earlier in the document must not steal the insertion point.
  QByteArray html = "<html><head></head><body><pre></head></pre></body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  const int styleIdx = html.indexOf(c_wkhtmltopdfMermaidFixStyle);
  QVERIFY(styleIdx > html.indexOf("</head>"));
  QVERIFY(styleIdx < html.lastIndexOf("</head>"));
}

void TestWkhtmltopdfHtmlPatch::testMissingHeadFails() {
  QByteArray html = "<html><body>no head here</body></html>";
  QVERIFY(!insertWkhtmltopdfHtmlPatches(html, 0, 0));
}

void TestWkhtmltopdfHtmlPatch::testMissingHeadLeavesInputUntouched() {
  const QByteArray original = "<html><body>no head here</body></html>";
  QByteArray html = original;
  QVERIFY(!insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QCOMPARE(html, original);
}

void TestWkhtmltopdfHtmlPatch::testUtf8BytesArePreserved() {
  // "Android 4G Modem 检测" in UTF-8, the shape of the file that first reproduced the hang.
  const QByteArray body = QStringLiteral("Android 4G Modem 检测").toUtf8();
  const QByteArray original = "<html><head></head><body>" + body + "</body></html>";
  QByteArray html = original;
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QVERIFY(html.contains(body));
  QCOMPARE(html.size(), original.size() + qstrlen(c_wkhtmltopdfMermaidFixStyle) +
                            wkhtmltopdfMermaidSizeFixScript(0, 0).size());
}

void TestWkhtmltopdfHtmlPatch::testNonUtf8BytesArePreserved() {
  // Bytes that are invalid UTF-8 must survive: the patch is byte-level and never transcodes.
  QByteArray body("\xB2\xE2\xCA\xD4", 4);
  QByteArray html = "<html><head></head><body>" + body + "</body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QVERIFY(html.contains(body));
}

void TestWkhtmltopdfHtmlPatch::testSelectorIsScopedToMermaid() {
  // The workaround must not strip strokes from unrelated inline SVG in a note.
  const QByteArray style(c_wkhtmltopdfMermaidFixStyle);
  QVERIFY(style.contains("vx-mermaid-graph"));
  QVERIFY(!style.contains("<style>svg .marker"));
  QVERIFY(style.contains("stroke: none !important"));
}

void TestWkhtmltopdfHtmlPatch::testStyleDisablesWrapperScrolling() {
  // div.vx-mermaid-graph is `overflow-y: hidden`, which CSS turns into `overflow-x: auto`. In a PDF
  // that paints a dead scrollbar and clips the diagram, so the wrapper must be forced back to
  // `visible`.
  const QByteArray style(c_wkhtmltopdfMermaidFixStyle);
  QVERIFY(style.contains("div.vx-mermaid-graph { overflow: visible !important; }"));
}

void TestWkhtmltopdfHtmlPatch::testSizeFixScriptIsInserted() {
  QByteArray html = "<html><head></head><body></body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QVERIFY(html.contains(wkhtmltopdfMermaidSizeFixScript(0, 0)));
  // Both workarounds land inside the head, in a deterministic order.
  const int styleIdx = html.indexOf(c_wkhtmltopdfMermaidFixStyle);
  const int scriptIdx = html.indexOf(wkhtmltopdfMermaidSizeFixScript(0, 0));
  QVERIFY(styleIdx >= 0);
  QVERIFY(styleIdx < scriptIdx);
  QVERIFY(scriptIdx < html.indexOf("</head>"));
}

void TestWkhtmltopdfHtmlPatch::testSizeFixScriptScalesToParentWidth() {
  // The script must derive the box from the viewBox and shrink it to the available width, which is
  // what keeps a wide diagram from overflowing the PDF page. It must also be scoped to Mermaid
  // graphs.
  const QByteArray script(wkhtmltopdfMermaidSizeFixScript(0, 0));
  QVERIFY(script.contains("svg[id^=\"vx-mermaid-graph\"]"));
  QVERIFY(script.contains("viewBox"));
  QVERIFY(script.contains("h = h * maxWidth / w"));
  QVERIFY(script.contains("setAttribute('width'"));
  QVERIFY(script.contains("setAttribute('height'"));
  // Runs whether or not the document has already finished parsing.
  QVERIFY(script.contains("DOMContentLoaded"));
}

} // namespace tests

namespace tests {

// Strip the <script> wrapper so the body can be evaluated by QJSEngine.
static QString scriptBody(int p_maxWidthPx = 0, int p_maxHeightPx = 0) {
  QString script =
      QString::fromLatin1(wkhtmltopdfMermaidSizeFixScript(p_maxWidthPx, p_maxHeightPx));
  const int start = script.indexOf(QStringLiteral(">")) + 1;
  const int end = script.lastIndexOf(QStringLiteral("</script>"));
  return script.mid(start, end - start);
}

// A minimal DOM stub: one Mermaid <svg> with p_viewBox. There is deliberately no layout geometry on
// it — wkhtmltopdf's QtWebKit reports `clientWidth == 0` for every element, which is exactly why
// the limits are injected instead of measured.
static QString domStub(const QString &p_viewBox,
                       const QString &p_readyState = QStringLiteral("complete")) {
  const QString viewBoxLiteral =
      p_viewBox.isNull() ? QStringLiteral("null") : QStringLiteral("'%1'").arg(p_viewBox);
  return QStringLiteral(R"JS(
var svg = {
  attrs: { viewBox: %1 },
  style: {},
  parentNode: { clientWidth: 0 },
  getAttribute: function (p_name) {
    return this.attrs[p_name] === undefined ? null : this.attrs[p_name];
  },
  setAttribute: function (p_name, p_value) { this.attrs[p_name] = p_value; }
};
var selectors = [];
var domContentLoadedHandlers = [];
var document = {
  readyState: '%2',
  querySelectorAll: function (p_selector) { selectors.push(p_selector); return [svg]; },
  addEventListener: function (p_event, p_handler) {
    if (p_event === 'DOMContentLoaded') { domContentLoadedHandlers.push(p_handler); }
  }
};
)JS")
      .arg(viewBoxLiteral)
      .arg(p_readyState);
}

static QJSValue runScript(QJSEngine &p_engine, const QString &p_dom, const QString &p_epilogue,
                          int p_maxWidthPx, int p_maxHeightPx = 0) {
  QJSValue result = p_engine.evaluate(p_dom + scriptBody(p_maxWidthPx, p_maxHeightPx) + p_epilogue);
  return result;
}

void TestWkhtmltopdfHtmlPatch::testScriptScalesWideGraphToParentWidth() {
  QJSEngine engine;
  // The real reproducing document's widest graph: 2636.59 x 666 in a 700px column.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 2636.589111328125 666")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height, selectors[0], "
                                          "svg.style.maxWidth];"),
                           700);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 700);
  // Aspect ratio preserved: 666 * 700 / 2636.589... = 176.8 -> 177.
  QCOMPARE(res.property(1).toInt(), 177);
  QCOMPARE(res.property(2).toString(), QStringLiteral("svg[id^=\"vx-mermaid-graph\"]"));
  QCOMPARE(res.property(3).toString(), QStringLiteral("none"));
}

void TestWkhtmltopdfHtmlPatch::testScriptKeepsNarrowGraphAtNaturalSize() {
  QJSEngine engine;
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 300 150")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 700);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 300);
  QCOMPARE(res.property(1).toInt(), 150);
}

void TestWkhtmltopdfHtmlPatch::testScriptHandlesCommaSeparatedViewBox() {
  QJSEngine engine;
  // A negative origin must not be mistaken for the width/height fields.
  QJSValue res = runScript(engine, domStub(QStringLiteral("-10,-20, 400, 200")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 200);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 200);
  QCOMPARE(res.property(1).toInt(), 100);
}

void TestWkhtmltopdfHtmlPatch::testScriptIgnoresGraphWithoutViewBox() {
  QJSEngine engine;
  QJSValue res = runScript(engine, domStub(QString()),
                           QStringLiteral("[svg.attrs.width === undefined, "
                                          "svg.attrs.height === undefined];"),
                           700);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QVERIFY(res.property(0).toBool());
  QVERIFY(res.property(1).toBool());
}

void TestWkhtmltopdfHtmlPatch::testScriptFallsBackWhenParentHasNoWidth() {
  QJSEngine engine;
  // No width limit (0 = disabled): keep the natural box rather than divide by zero.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 800 400")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 0);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 800);
  QCOMPARE(res.property(1).toInt(), 400);
}

void TestWkhtmltopdfHtmlPatch::testScriptRunsOnDomContentLoadedWhileLoading() {
  QJSEngine engine;
  QJSValue res =
      runScript(engine, domStub(QStringLiteral("0 0 800 400"), QStringLiteral("loading")),
                QStringLiteral("var pending = svg.attrs.width === undefined;"
                               "domContentLoadedHandlers.forEach(function (h) { h(); });"
                               "[pending, domContentLoadedHandlers.length, svg.attrs.width, "
                               "svg.attrs.height];"),
                400);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  // Nothing happens until the event fires, and then the graph is sized.
  QVERIFY(res.property(0).toBool());
  QCOMPARE(res.property(1).toInt(), 1);
  QCOMPARE(res.property(2).toInt(), 400);
  QCOMPARE(res.property(3).toInt(), 200);
}

void TestWkhtmltopdfHtmlPatch::testScriptClampsTallGraphToPageHeight() {
  QJSEngine engine;
  // Fits the column width but is taller than one page: an <svg> cannot be split, so an unclamped
  // one is pushed whole to the next page and leaves the previous one nearly blank.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 584 1244")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 718, 1000);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(1).toInt(), 1000);
  // Aspect ratio preserved: 584 * 1000 / 1244 = 469.45 -> 469.
  QCOMPARE(res.property(0).toInt(), 469);
}

void TestWkhtmltopdfHtmlPatch::testScriptClampsAfterWidthScaling() {
  QJSEngine engine;
  // Too wide AND, once scaled to the column, still too tall. Both clamps apply, in order.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 1436 2860")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 718, 1000);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  // 1436x2860 -> width clamp -> 718x1430 -> height clamp -> 502x1000.
  QCOMPARE(res.property(1).toInt(), 1000);
  QCOMPARE(res.property(0).toInt(), 502);
}

void TestWkhtmltopdfHtmlPatch::testScriptLeavesShortGraphUnclamped() {
  QJSEngine engine;
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 400 200")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 718, 1000);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 400);
  QCOMPARE(res.property(1).toInt(), 200);
}

void TestWkhtmltopdfHtmlPatch::testScriptWithoutHeightLimitDoesNotClamp() {
  QJSEngine engine;
  // 0 means "no usable page layout": keep the previous behavior rather than invent a limit.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 584 1244")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 718, 0);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 584);
  QCOMPARE(res.property(1).toInt(), 1244);
}

void TestWkhtmltopdfHtmlPatch::testScriptUsesPassedWidthWhenDomReportsNone() {
  QJSEngine engine;
  // This is the production case: wkhtmltopdf reports clientWidth == 0 for every element, so the
  // width has to come from the injected limit or the diagram keeps its natural size and overflows.
  QJSValue res = runScript(engine, domStub(QStringLiteral("0 0 2636.589111328125 666")),
                           QStringLiteral("[svg.attrs.width, svg.attrs.height];"), 658);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toInt(), 658);
  QCOMPARE(res.property(1).toInt(), 166);
}

void TestWkhtmltopdfHtmlPatch::testScriptIgnoresDomWidthEntirely() {
  // The script must never read layout geometry: in wkhtmltopdf it is always 0, and honoring it
  // would silently re-enable a clamp the caller asked to switch off.
  const QByteArray script(wkhtmltopdfMermaidSizeFixScript(658, 0));
  QVERIFY(!script.contains("clientWidth"));
  QVERIFY(!script.contains("parentNode"));
}

// A4 portrait is 210 mm wide; with 10 mm horizontal margins the paint rect is 190 mm == 718 px, and
// the export template's gutters take a measured 50 px (60 px is subtracted to keep slack).
void TestWkhtmltopdfHtmlPatch::testContentWidthPxA4() {
  QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 16, 10, 10),
                     QPageLayout::Millimeter);
  QCOMPARE(wkhtmltopdfContentWidthPx(layout), 658);
}

void TestWkhtmltopdfHtmlPatch::testContentWidthPxFollowsMargins() {
  QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(40, 16, 40, 10),
                     QPageLayout::Millimeter);
  // 210 - 80 = 130 mm -> 491 px - 60 = 431.
  QCOMPARE(wkhtmltopdfContentWidthPx(layout), 431);
}

void TestWkhtmltopdfHtmlPatch::testContentWidthPxZeroWhenGuttersConsumeIt() {
  // A page narrower than the gutters must disable the clamp rather than go negative.
  QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(100, 10, 100, 10),
                     QPageLayout::Millimeter);
  QCOMPARE(wkhtmltopdfContentWidthPx(layout), 0);
}

// A4 portrait, 297 mm tall. Content height with VNote's default 16 mm top / 10 mm bottom margins is
// 271 mm; at 96 CSS px per inch with the 2% safety margin that is 1003 px.
void TestWkhtmltopdfHtmlPatch::testPageContentHeightPxA4() {
  QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 16, 10, 10),
                     QPageLayout::Millimeter);
  QCOMPARE(wkhtmltopdfPageContentHeightPx(layout), 1003);
}

void TestWkhtmltopdfHtmlPatch::testPageContentHeightPxFollowsMargins() {
  QPageLayout wide(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 50, 10, 50),
                   QPageLayout::Millimeter);
  // 297 - 100 = 197 mm -> 197 / 25.4 * 96 * 0.98 = 729.
  QCOMPARE(wkhtmltopdfPageContentHeightPx(wide), 729);
}

void TestWkhtmltopdfHtmlPatch::testPageContentHeightPxFollowsOrientation() {
  QPageLayout portrait(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 10, 10, 10),
                       QPageLayout::Millimeter);
  QPageLayout landscape(QPageSize(QPageSize::A4), QPageLayout::Landscape, QMarginsF(10, 10, 10, 10),
                        QPageLayout::Millimeter);
  QVERIFY(wkhtmltopdfPageContentHeightPx(landscape) < wkhtmltopdfPageContentHeightPx(portrait));
}

void TestWkhtmltopdfHtmlPatch::testPageContentHeightPxZeroForEmptyLayout() {
  // A degenerate layout must disable the clamp rather than produce a bogus limit.
  QPageLayout empty;
  QCOMPARE(wkhtmltopdfPageContentHeightPx(empty), 0);
}

// A DOM stub holding one rasterized Mermaid diagram (<img data-mermaid-png>) and no <svg>.
// wkhtmltopdf's QtWebKit measures nothing, so the intrinsic size travels in data- attributes.
static QString pngDomStub(const QString &p_width, const QString &p_height) {
  return QStringLiteral(R"JS(
var img = {
  attrs: { 'data-mermaid-png': 'true', 'data-mermaid-width': %1, 'data-mermaid-height': %2 },
  style: {},
  getAttribute: function (p_name) {
    return this.attrs[p_name] === undefined ? null : this.attrs[p_name];
  },
  setAttribute: function (p_name, p_value) { this.attrs[p_name] = p_value; }
};
var selectors = [];
var domContentLoadedHandlers = [];
var document = {
  readyState: 'complete',
  querySelectorAll: function (p_selector) {
    selectors.push(p_selector);
    return p_selector.indexOf('img') === 0 ? [img] : [];
  },
  addEventListener: function (p_event, p_handler) {
    if (p_event === 'DOMContentLoaded') { domContentLoadedHandlers.push(p_handler); }
  }
};
)JS")
      .arg(p_width)
      .arg(p_height);
}

void TestWkhtmltopdfHtmlPatch::testScriptClampsMermaidPngToWidth() {
  QJSEngine engine;
  QJSValue res = runScript(engine, pngDomStub(QStringLiteral("2000"), QStringLiteral("1000")),
                           QStringLiteral("[img.style.width, img.style.height, img.style.maxWidth, "
                                          "selectors[1]];"),
                           700);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toString(), QStringLiteral("700px"));
  QCOMPARE(res.property(1).toString(), QStringLiteral("350px"));
  QCOMPARE(res.property(2).toString(), QStringLiteral("none"));
  QCOMPARE(res.property(3).toString(), QStringLiteral("img[data-mermaid-png]"));
}

void TestWkhtmltopdfHtmlPatch::testScriptLeavesSmallMermaidPngAlone() {
  QJSEngine engine;
  // Fits the column and the page: keep the on-screen size, do not upscale.
  QJSValue res = runScript(engine, pngDomStub(QStringLiteral("300"), QStringLiteral("150")),
                           QStringLiteral("[img.style.width, img.style.height];"), 700, 900);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QCOMPARE(res.property(0).toString(), QStringLiteral("300px"));
  QCOMPARE(res.property(1).toString(), QStringLiteral("150px"));
}

void TestWkhtmltopdfHtmlPatch::testScriptIgnoresImageWithoutMermaidSize() {
  QJSEngine engine;
  QJSValue res = runScript(engine, pngDomStub(QStringLiteral("null"), QStringLiteral("null")),
                           QStringLiteral("[img.style.width === undefined];"), 700);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
  QVERIFY(res.property(0).toBool());
}

void TestWkhtmltopdfHtmlPatch::testFontOverrideIsInserted() {
  QByteArray html = "<html><head></head><body></body></html>";
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0, "Noto Sans SC", "Sarasa Mono SC"));
  const QByteArray style = wkhtmltopdfFontOverrideStyle("Noto Sans SC", "Sarasa Mono SC");
  QVERIFY(!style.isEmpty());
  QVERIFY(html.contains(style));
  QVERIFY(html.indexOf(style) < html.indexOf("</head>"));
  // The forced family must come FIRST: QtWebKit uses the first installed family for everything.
  QVERIFY(style.contains("body, body * { font-family: \"Noto Sans SC\", sans-serif !important; }"));
}

void TestWkhtmltopdfHtmlPatch::testFontOverrideEmptyWithoutFamilies() {
  // No usable CJK font on the machine: inject nothing rather than name a missing family.
  QCOMPARE(wkhtmltopdfFontOverrideStyle(QByteArray(), QByteArray()), QByteArray());

  const QByteArray original = "<html><head></head><body></body></html>";
  QByteArray html = original;
  QVERIFY(insertWkhtmltopdfHtmlPatches(html, 0, 0));
  QCOMPARE(html.size(), original.size() + qstrlen(c_wkhtmltopdfMermaidFixStyle) +
                            wkhtmltopdfMermaidSizeFixScript(0, 0).size());
}

void TestWkhtmltopdfHtmlPatch::testFontOverrideKeepsCodeMonospace() {
  const QByteArray style = wkhtmltopdfFontOverrideStyle("Noto Sans SC", "Sarasa Mono SC");
  const int textIdx = style.indexOf("body, body *");
  const int monoIdx = style.indexOf("body pre");
  QVERIFY(textIdx >= 0);
  // Declared after, and with a higher specificity, so code keeps its monospace face.
  QVERIFY(monoIdx > textIdx);
  QVERIFY(style.contains("body pre *"));
  QVERIFY(style.contains("body code *"));
  QVERIFY(style.contains("\"Sarasa Mono SC\", monospace !important"));
}

void TestWkhtmltopdfHtmlPatch::testFontOverrideRejectsNonAsciiFamily() {
  // The patch is byte-level and never transcodes, so a CJK-named family cannot be emitted.
  const QByteArray style =
      wkhtmltopdfFontOverrideStyle(QStringLiteral("微软雅黑").toUtf8(), QByteArray());
  QCOMPARE(style, QByteArray());
}

void TestWkhtmltopdfHtmlPatch::testFontOverrideCannotBreakOutOfTheRule() {
  const QByteArray style =
      wkhtmltopdfFontOverrideStyle("Evil\", x { display: none; } b:hover \"", QByteArray());
  // Whatever the family contains, it stays inside one quoted string in one declaration block: the
  // characters that could terminate either are dropped.
  QCOMPARE(style.count('{'), 1);
  QCOMPARE(style.count('}'), 1);
  // Two quotes from the <style type="text/css"> tag, two around the family.
  QCOMPARE(style.count('"'), 4);
  // The only semicolon is the one closing our own declaration.
  QCOMPARE(style.count(';'), 1);
  QCOMPARE(style.count("</style>"), 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestWkhtmltopdfHtmlPatch)
#include "test_wkhtmltopdfhtmlpatch.moc"
