// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_pdfviewercore_js.cpp
//
// JS half of the PDF outline contract. This is the actual regression gate for
// the two hard bugs the feature exists to avoid; test_pdfvieweradapter_outline
// cannot catch either of them, because every case there hand-feeds the adapter a
// QJsonArray:
//
//   1. A buildOutline() that emits only { name, level } and drops `index`.
//   2. A lossy `if (window.vxAdapter)` guard on the publish path, which discards
//      the outline forever when the PDF finishes loading before the QWebChannel
//      callback runs ('documentloaded' does not fire again for that document).
//
// === Why QJSEngine and not a browser / npm harness ===
// There is no JS test infrastructure in this repo: no package.json, no node in
// CI, no QWebEngine or QJSEngine anywhere else under tests/. pdfviewercore.js
// and pdfviewer.js were deliberately structured so that the entire testable
// surface — including the pdf.js event wiring — is synchronous, DOM-free and
// pdf.js-free plain JS, which QJSEngine executes directly.
//
// === Why the REAL files are evaluated ===
// Both src/data/extra/web/pdf.js/pdfviewercore.js and .../pdfviewer.js are read
// from disk and evaluated unmodified. Testing a transcribed copy would gate
// nothing.
//
// === Why getOutline() / initializedPromise are SYNCHRONOUS thenables ===
// The production code only ever does `.then(...).catch(...)` and `.then(...)`,
// never `await` and never `Promise.resolve`. Stubbing them as plain objects with
// a `then` method keeps the whole path synchronous and sidesteps QJSEngine
// microtask scheduling entirely, so every assertion runs on settled state.

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <QtTest>

namespace tests {

namespace {

QString webDir() {
  // VNOTE_SRC_DIR is injected by CMake via target_compile_definitions; see
  // tests/widgets/CMakeLists.txt for the registration of this test target.
#ifdef VNOTE_SRC_DIR
  return QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra/web");
#else
  return QDir::currentPath() + QStringLiteral("/../../../src/data/extra/web");
#endif
}

QString readFile(const QString &p_path, QString *p_error) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *p_error = QStringLiteral("cannot open %1").arg(p_path);
    return QString();
  }
  return QString::fromUtf8(f.readAll());
}

// moc HAZARD, read before editing the JS below: moc's lexer scans `//` as a
// line comment even INSIDE a C++11 raw string literal. A `//` that leaves an
// unterminated quote on its line (the classic case is a "https://..." URL)
// desyncs moc's quote parity for the rest of the file, after which it reports
// "No relevant classes found", emits an EMPTY .moc, and the target fails to
// link with LNK2001 on metaObject/qt_metacast/qt_metacall. `#ifndef Q_MOC_RUN`
// does NOT help — the desync happens in the lexer, before the conditional is
// evaluated. Write such URLs with escaped slashes (`https:\/\/`), which JS
// treats as an ordinary `/`.

// Everything pdfviewercore.js needs from the host environment. QJSEngine gives
// no DOM, and pdfviewercore.js reads document.currentScript.src at construction
// time to derive the worker path.
const char *const c_corePrelude = R"JS(
var window = this;
var console = { log: function(){}, warn: function(){}, error: function(){} };
var document = { currentScript: { src: "qrc:/web/pdf.js/pdfviewercore.js" } };
class VXCore { constructor() {} on() {} emit() {} }
)JS";

// Recorders and fakes used by the pdfviewercore.js cases. Kept out of the
// prelude so the real file is evaluated against nothing but host globals.
const char *const c_coreHarness = R"JS(
window.__published = [];
window.__stub = { setOutline: function(o) { window.__published.push(o); } };

window.__gotoCalls = [];
window.PDFViewerApplication = {
    pdfLinkService: {
        goToDestination: function(d) { window.__gotoCalls.push(JSON.stringify(d)); }
    }
};

// A document whose getOutline() resolves synchronously. When p_supersede is
// true it swaps app.pdfDocument out from under the in-flight call first, which
// is how the "document replaced mid-flight" guard is exercised.
window.__makeDoc = function(p_app, p_raw, p_supersede) {
    return {
        getOutline: function() {
            return {
                then: function(cb) {
                    if (p_supersede) {
                        p_app.pdfDocument = { getOutline: function() {} };
                    }
                    cb(p_raw);
                    return { catch: function() {} };
                }
            };
        }
    };
};

window.__makeApp = function(p_raw, p_supersede) {
    var handlers = {};
    var app = {
        eventBus: {
            on: function(name, fn) {
                if (!handlers[name]) { handlers[name] = []; }
                handlers[name].push(fn);
            },
            fire: function(name) {
                var hs = handlers[name] || [];
                for (var i = 0; i < hs.length; ++i) { hs[i](); }
            }
        },
        pdfDocument: null
    };
    app.pdfDocument = window.__makeDoc(app, p_raw, p_supersede);
    return app;
};

// Mixes destination-bearing and destination-less entries across three levels,
// plus a title that needs sanitizing.
window.__fixture = [
    { title: "Chapter 1", dest: "ch1", items: [
        { title: "Section 1.1", dest: [{ num: 1, gen: 0 }, { name: "XYZ" }, 0, 700, null] },
        { title: "External link", url: "https:\/\/example.com" },
        { title: "Section 1.2", dest: "s12", items: [
            { title: " Deep\u0000 \n\t heading ", dest: "deep" }
        ]}
    ]},
    { title: "Chapter 2", dest: "ch2" }
];
)JS";

// Everything pdfviewer.js touches at file scope. window.PDFViewerApplication is
// replaced by a full fake app here, because pdfviewer.js registers the bridge
// from initializedPromise and the bridge needs an eventBus.
const char *const c_gluePrelude = R"JS(
window.__channelCb = null;
function QWebChannel(p_transport, p_cb) { window.__channelCb = p_cb; }
var qt = { webChannelTransport: {} };
var pdfjsLib = { GlobalWorkerOptions: {} };

window.__initDeferred = {
    cbs: [],
    then: function(cb) { this.cbs.push(cb); return this; },
    resolve: function() { for (var i = 0; i < this.cbs.length; ++i) { this.cbs[i](); } }
};

window.__app = window.__makeApp(window.__fixture, false);
window.__app.initializedPromise = window.__initDeferred;
window.__app.pdfLinkService = {
    goToDestination: function(d) { window.__gotoCalls.push(JSON.stringify(d)); }
};
window.PDFViewerApplication = window.__app;

// Stand-in for viewer.js's AppOptions (exported as window.PDFViewerApplicationOptions).
// pdfviewer.js sets the sidebar options through it at file scope.
window.__options = {};
window.PDFViewerApplicationOptions = {
    set: function(k, v) { window.__options[k] = v; }
};

// Must satisfy EVERYTHING the existing channel callback does, not just the new
// outline calls: it connects urlUpdated unconditionally and first, so omitting
// that would make the callback throw before setOutlineAdapter() is reached.
window.__scrollHandler = null;
window.__fakeAdapter = {
    urlUpdated:                 { connect: function() {} },
    outlineItemScrollRequested: { connect: function(fn) { window.__scrollHandler = fn; } },
    setOutline: function(o) { window.__published.push(o); },
    setReady:   function() {}
};
)JS";

} // namespace

class TestPdfViewerCoreJs : public QObject {
  Q_OBJECT

private slots:
  void wireContractCarriesDenseDestinationIndex();
  void gotoOutlineItemRoundTrips();
  void rendezvousAdapterFirst();
  void rendezvousOutlineFirst();
  void publishHappensExactlyOnce();
  void bridgeAdapterFirst();
  void bridgeOutlineFirst();
  void supersededDocumentPublishesNothing();
  void resetOnPagesDestroy();
  void resetOnDocumentError();
  void documentInitMustNotBlankPublishedOutline();
  void rebuildResetsDestinations();
  void gluePublishesWhenChannelArrivesFirst();
  void gluePublishesWhenOutlineArrivesFirst();
  void glueHidesSidebarOnLoad();

private:
  // Fresh engine per case: pdfviewercore.js declares `class PdfViewerCore` at
  // top level, so re-evaluating it in the same engine would be a redeclaration.
  void loadCore(QJSEngine &p_engine);
  void loadGlue(QJSEngine &p_engine);
  static QJSValue eval(QJSEngine &p_engine, const QString &p_src);
  static QString json(QJSEngine &p_engine, const QString &p_expr);
};

QJSValue TestPdfViewerCoreJs::eval(QJSEngine &p_engine, const QString &p_src) {
  return p_engine.evaluate(p_src);
}

QString TestPdfViewerCoreJs::json(QJSEngine &p_engine, const QString &p_expr) {
  return p_engine.evaluate(QStringLiteral("JSON.stringify(%1)").arg(p_expr)).toString();
}

void TestPdfViewerCoreJs::loadCore(QJSEngine &p_engine) {
  QString error;

  const QString utils = readFile(webDir() + QStringLiteral("/js/utils.js"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  const QString core = readFile(webDir() + QStringLiteral("/pdf.js/pdfviewercore.js"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  // One evaluate() so the `class` declarations share a single script scope: the
  // real file's `class PdfViewerCore extends VXCore` must see both VXCore (from
  // the prelude) and Utils. The shipped bytes are still evaluated verbatim.
  const QString program = QString::fromUtf8(c_corePrelude) + utils + QLatin1Char('\n') + core;

  const QJSValue res = p_engine.evaluate(program);
  QVERIFY2(!res.isError(),
           qPrintable(QStringLiteral("pdfviewercore.js failed to evaluate: %1 (line %2)")
                          .arg(res.toString())
                          .arg(res.property(QStringLiteral("lineNumber")).toInt())));

  const QJSValue harness = p_engine.evaluate(QString::fromUtf8(c_coreHarness));
  QVERIFY2(!harness.isError(), qPrintable(harness.toString()));

  QVERIFY(!p_engine.evaluate(QStringLiteral("window.vxcore")).isUndefined());
}

void TestPdfViewerCoreJs::loadGlue(QJSEngine &p_engine) {
  QString error;
  const QString glue = readFile(webDir() + QStringLiteral("/pdf.js/pdfviewer.js"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  const QJSValue prelude = p_engine.evaluate(QString::fromUtf8(c_gluePrelude));
  QVERIFY2(!prelude.isError(), qPrintable(prelude.toString()));

  const QJSValue res = p_engine.evaluate(glue);
  QVERIFY2(!res.isError(),
           qPrintable(QStringLiteral("pdfviewer.js failed to evaluate: %1 (line %2)")
                          .arg(res.toString())
                          .arg(res.property(QStringLiteral("lineNumber")).toInt())));
}

// Fails against a buildOutline() that emits only { name, level }.
void TestPdfViewerCoreJs::wireContractCarriesDenseDestinationIndex() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__flat = window.vxcore.buildOutline(window.__fixture);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__flat.length")).toInt(), 6);

  // Every entry carries all three fields.
  QCOMPARE(eval(engine, QStringLiteral(
                            "window.__flat.every(function(e) { return typeof e.name === 'string' "
                            "&& typeof e.level === 'number' && typeof e.index === 'number'; })"))
               .toBool(),
           true);

  // Pre-order DFS, 1-based levels, dense index over real destinations only.
  QCOMPARE(json(engine, QStringLiteral("window.__flat.map(function(e) { return e.name; })")),
           QStringLiteral("[\"Chapter 1\",\"Section 1.1\",\"External link\",\"Section 1.2\","
                          "\"Deep heading\",\"Chapter 2\"]"));
  QCOMPARE(json(engine, QStringLiteral("window.__flat.map(function(e) { return e.level; })")),
           QStringLiteral("[1,2,2,2,3,1]"));
  QCOMPARE(json(engine, QStringLiteral("window.__flat.map(function(e) { return e.index; })")),
           QStringLiteral("[0,1,-1,2,3,4]"));

  // outlineDests holds exactly the real destinations, in index order.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 5);
}

void TestPdfViewerCoreJs::gotoOutlineItemRoundTrips() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__flat = window.vxcore.buildOutline(window.__fixture);"));

  // index 0 -> "ch1", index 2 -> "s12", index 3 -> "deep", index 4 -> "ch2".
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(0);"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(2);"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(3);"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(4);"));
  QCOMPARE(json(engine, QStringLiteral("window.__gotoCalls")),
           QStringLiteral("[\"\\\"ch1\\\"\",\"\\\"s12\\\"\",\"\\\"deep\\\"\",\"\\\"ch2\\\"\"]"));

  // The explicit-array destination survives verbatim.
  eval(engine, QStringLiteral("window.__gotoCalls = [];"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(1);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__gotoCalls.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__gotoCalls[0] === "
                                       "JSON.stringify(window.__fixture[0].items[0].dest)"))
               .toBool(),
           true);

  // -1 and out-of-range are inert.
  eval(engine, QStringLiteral("window.__gotoCalls = [];"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(-1);"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(5);"));
  eval(engine, QStringLiteral("window.vxcore.gotoOutlineItem(999);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__gotoCalls.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::rendezvousAdapterFirst() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 0);

  eval(engine, QStringLiteral("window.vxcore.setPendingOutline([{name:'a',level:1,index:0}]);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0]")),
           QStringLiteral("[{\"name\":\"a\",\"level\":1,\"index\":0}]"));
}

void TestPdfViewerCoreJs::rendezvousOutlineFirst() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.vxcore.setPendingOutline([{name:'a',level:1,index:0}]);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 0);

  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0]")),
           QStringLiteral("[{\"name\":\"a\",\"level\":1,\"index\":0}]"));
}

void TestPdfViewerCoreJs::publishHappensExactlyOnce() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.vxcore.setPendingOutline([]);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);

  // A legitimately empty outline is truthy in JS and IS published above; a bare
  // republish afterwards must not re-deliver it.
  eval(engine, QStringLiteral("window.vxcore.publishOutline();"));
  eval(engine, QStringLiteral("window.vxcore.publishOutline();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
}

void TestPdfViewerCoreJs::bridgeAdapterFirst() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, false);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0].map(function(e){return e.index;})")),
           QStringLiteral("[0,1,-1,2,3,4]"));
}

// THE case. Fails if anyone reintroduces an `if (window.vxAdapter)` guard on the
// publish path, or forgets to call setPendingOutline from the bridge.
void TestPdfViewerCoreJs::bridgeOutlineFirst() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, false);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 0);

  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0].map(function(e){return e.index;})")),
           QStringLiteral("[0,1,-1,2,3,4]"));
}

void TestPdfViewerCoreJs::supersededDocumentPublishesNothing() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, true);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 0);
  // Nothing was built, so no destination from the old document can linger.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::resetOnPagesDestroy() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, false);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 5);

  eval(engine, QStringLiteral("window.__app.eventBus.fire('pagesdestroy');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 2);
  QCOMPARE(json(engine, QStringLiteral("window.__published[1]")), QStringLiteral("[]"));
  // A stale index can no longer resolve to the previous document's destination.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::resetOnDocumentError() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, false);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));

  eval(engine, QStringLiteral("window.__app.eventBus.fire('documenterror');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 2);
  QCOMPARE(json(engine, QStringLiteral("window.__published[1]")), QStringLiteral("[]"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 0);
}

// 'documentinit' is dispatched from a DIFFERENT promise chain than
// 'documentloaded' and can arrive AFTER it, so using it as the teardown signal
// would blank a good outline permanently. Fails immediately if anyone
// "simplifies" the teardown signal back to it.
void TestPdfViewerCoreJs::documentInitMustNotBlankPublishedOutline() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeApp(window.__fixture, false);"));
  eval(engine, QStringLiteral("window.vxcore.attachOutlineBridge(window.__app);"));
  eval(engine, QStringLiteral("window.vxcore.setOutlineAdapter(window.__stub);"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);

  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentinit');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 5);
}

void TestPdfViewerCoreJs::rebuildResetsDestinations() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.vxcore.buildOutline(window.__fixture);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 5);

  eval(engine, QStringLiteral("window.vxcore.buildOutline([{title:'solo',dest:'x'}]);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 1);

  eval(engine, QStringLiteral("window.vxcore.buildOutline([]);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.outlineDests.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::gluePublishesWhenChannelArrivesFirst() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  eval(engine,
       QStringLiteral("window.__channelCb({ objects: { vxAdapter: window.__fakeAdapter } });"));
  eval(engine, QStringLiteral("window.__initDeferred.resolve();"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0].map(function(e){return e.index;})")),
           QStringLiteral("[0,1,-1,2,3,4]"));

  // The scroll signal is wired through to gotoOutlineItem.
  eval(engine, QStringLiteral("window.__scrollHandler(0);"));
  QCOMPARE(json(engine, QStringLiteral("window.__gotoCalls")), QStringLiteral("[\"\\\"ch1\\\"\"]"));
}

// Gates an adapter-presence guard around attachOutlineBridge(): the
// initialized-promise path runs before the channel callback, so such a guard
// would skip bridge registration entirely and nothing would ever be published.
// Also gates omission (or non-execution) of setOutlineAdapter().
void TestPdfViewerCoreJs::gluePublishesWhenOutlineArrivesFirst() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  eval(engine, QStringLiteral("window.__initDeferred.resolve();"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('documentloaded');"));
  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 0);

  eval(engine,
       QStringLiteral("window.__channelCb({ objects: { vxAdapter: window.__fakeAdapter } });"));

  QCOMPARE(eval(engine, QStringLiteral("window.__published.length")).toInt(), 1);
  QCOMPARE(json(engine, QStringLiteral("window.__published[0].map(function(e){return e.index;})")),
           QStringLiteral("[0,1,-1,2,3,4]"));
}

// Gates removal of the sidebar defaults. sidebarViewOnLoad must be SidebarView.NONE (0), and
// disablePreferences must be true — otherwise viewer.js's _initializeOptions() overwrites it
// with the -1 (UNKNOWN) preference default, which lets the stored state or the document's
// /PageMode re-open the navigation pane.
void TestPdfViewerCoreJs::glueHidesSidebarOnLoad() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  QCOMPARE(eval(engine, QStringLiteral("window.__options.sidebarViewOnLoad")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__options.disablePreferences")).toBool(), true);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerCoreJs)
#include "test_pdfviewercore_js.moc"
