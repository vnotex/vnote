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
// and pdfviewer.mjs were deliberately structured so that the entire testable
// surface — including the pdf.js event wiring — is synchronous, DOM-free and
// pdf.js-free plain JS, which QJSEngine executes directly.
//
// === Why the REAL files are evaluated ===
// Both src/data/extra/web/pdf.js/pdfviewercore.js and .../pdfviewer.mjs are read
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
//
// `document.addEventListener` is a RECORDER, not a no-op: the file registers its
// 'webviewerloaded' listener at load time, and that listener being present (and
// firing) is the whole AppOptions contract — see
// coreRegistersAWebViewerLoadedListener().
const char *const c_corePrelude = R"JS(
var window = this;
var console = { log: function(){}, warn: function(){}, error: function(){} };
window.__listeners = {};
var document = {
    currentScript: { src: "qrc:/web/pdf.js/pdfviewercore.js" },
    addEventListener: function(p_name, p_cb) { window.__listeners[p_name] = p_cb; }
};

// Stand-in for viewer.mjs's AppOptions. Starts EMPTY so a case can prove nothing
// was configured before the hook fired.
window.__options = {};
window.PDFViewerApplicationOptions = {
    set: function(k, v) { window.__options[k] = v; },
    get: function(k) { return window.__options[k]; }
};

// Dispatches the event pdf.js fires immediately before PDFViewerApplication.run().
window.__fireWebViewerLoaded = function() {
    var cb = window.__listeners['webviewerloaded'];
    if (!cb) { throw new Error('no webviewerloaded listener registered'); }
    cb({ type: 'webviewerloaded' });
};

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

// Everything pdfviewer.mjs touches at file scope. window.PDFViewerApplication is
// replaced by a full fake app here, because pdfviewer.mjs registers the bridge
// from initializedPromise and the bridge needs an eventBus.
//
// Note there is deliberately no `pdfjsLib` stand-in: under the ESM build there is
// no reliable global, and pdfviewer.mjs must not depend on one. If a future edit
// reintroduces `pdfjsLib.…` at file scope, this prelude makes it a hard failure
// instead of a silent runtime error in the browser.
const char *const c_gluePrelude = R"JS(
window.__channelCb = null;
function QWebChannel(p_transport, p_cb) { window.__channelCb = p_cb; }
var qt = { webChannelTransport: {} };

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

// AppOptions is supplied by the CORE prelude, because pdfviewercore.js is what
// configures it (from 'webviewerloaded'). pdfviewer.mjs must NOT touch it: a
// deferred module runs after pdf.js has already read every option. Redefining
// the stub here would mask that.

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
  void coreRegistersAWebViewerLoadedListener();
  void webViewerLoadedHidesSidebarOnLoad();
  void webViewerLoadedSetsModuleWorkerSrc();
  void webViewerLoadedDisablesPdfJsOwnAnnotationEditors();
  void appOptionsAreUntouchedBeforeTheHookFires();

  // Comment overlay coordinate math. Pure functions on PdfViewerCore, driven
  // with a fake viewport so no DOM (and no pdf.js) is needed.
  void clientRectProjectsIntoPdfPageSpace();
  void pdfQuadProjectsBackToACssBox();
  void projectionRoundTripsThroughAScaledViewport();
  void degenerateQuadsProduceNoBox();
  void selectionRectsGroupPerPage();
  void selectionRectsAreCappedAndDegenerateOnesDropped();

  // Tool state machine + the two new anchor types.
  void toolIsAModeAndEscLeavesIt();
  void inkDragProducesAPageSpaceStroke();
  void aClickWithoutADragCommitsNoInk();
  void freeTextIsOneShotAndPlacesAPageSpacePoint();
  void inkStrokesProjectToPolylinePoints();
  void aSecondPointerCannotHijackAnInkStroke();
  void cancellingAnInkGestureDiscardsIt();
  void extendInkDoesNotRebuildExistingComments();

  void eachToolUsesItsOwnColour();

  void setCommentColorIsHighlightOnly();

  void scalarOptionsReachTheAnchors();

  void inkDraftDomReflectsTheConfiguredInk();

  void inkOpacityReachesTheAnchorTheDraftAndTheRender();

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
  const QString glue = readFile(webDir() + QStringLiteral("/pdf.js/pdfviewer.mjs"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  const QJSValue prelude = p_engine.evaluate(QString::fromUtf8(c_gluePrelude));
  QVERIFY2(!prelude.isError(), qPrintable(prelude.toString()));

  const QJSValue res = p_engine.evaluate(glue);
  QVERIFY2(!res.isError(),
           qPrintable(QStringLiteral("pdfviewer.mjs failed to evaluate: %1 (line %2)")
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
// === AppOptions: the WHEN matters more than the WHAT ===
//
// These used to assert `window.__options.<key>` after evaluating pdfviewer.mjs,
// which proved nothing: a deferred module runs when readyState is already
// "interactive", so viewer.mjs has ALREADY called webViewerLoad() ->
// PDFViewerApplication.run(). The value landed in the options object and the
// test went green while pdf.js had long since read the default. The editor
// buttons stayed live and the sidebar options never applied.
//
// So the contract under test is now: pdfviewercore.js (a CLASSIC script, which
// runs before any module) registers a 'webviewerloaded' listener, and that
// listener is what configures AppOptions -- 'webviewerloaded' being pdf.js's
// documented hook, dispatched immediately before run().

void TestPdfViewerCoreJs::coreRegistersAWebViewerLoadedListener() {
  QJSEngine engine;
  loadCore(engine);

  QVERIFY2(
      eval(engine, QStringLiteral("window.__listeners['webviewerloaded'] !== undefined")).toBool(),
      "pdfviewercore.js must register a 'webviewerloaded' listener; a module is too late "
      "to configure AppOptions");
}

void TestPdfViewerCoreJs::webViewerLoadedHidesSidebarOnLoad() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QStringLiteral("window.__fireWebViewerLoaded();")).isError());

  QCOMPARE(eval(engine, QStringLiteral("window.__options.sidebarViewOnLoad")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__options.disablePreferences")).toBool(), true);
}

// The ESM bundle has no dependable `pdfjsLib` global, so the worker MUST be set
// through AppOptions. It must also point at the `.mjs` worker: pdf.js decides
// whether to spawn a module worker from that extension, and a `.js` URL would be
// a 404 in the vendored v6 tree.
void TestPdfViewerCoreJs::webViewerLoadedSetsModuleWorkerSrc() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QStringLiteral("window.__fireWebViewerLoaded();")).isError());

  const QString workerSrc = eval(engine, QStringLiteral("window.__options.workerSrc")).toString();
  QVERIFY2(!workerSrc.isEmpty(), "the webviewerloaded handler must set the workerSrc AppOption");
  QVERIFY2(
      workerSrc.endsWith(QStringLiteral("/build/pdf.worker.mjs")),
      qPrintable(QStringLiteral("workerSrc must point at the ESM worker, got: %1").arg(workerSrc)));
}

// pdf.js's own Comment / Signature / Highlight / Text / Draw / Image editors
// mutate the IN-MEMORY PDF and are persisted only through saveDocument(), which
// VNote does not expose (it never modifies the PDF binary). Leaving them enabled
// silently discards the user's work on tab close, and puts a second, incompatible
// "Highlight" next to VNote's own. -1 is AnnotationEditorType.DISABLE.
void TestPdfViewerCoreJs::webViewerLoadedDisablesPdfJsOwnAnnotationEditors() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QStringLiteral("window.__fireWebViewerLoaded();")).isError());

  const QJSValue mode = eval(engine, QStringLiteral("window.__options.annotationEditorMode"));
  QVERIFY2(!mode.isUndefined(),
           "the webviewerloaded handler must disable pdf.js's built-in annotation editors");
  QCOMPARE(mode.toInt(), -1);
}

// Nothing may be configured before the hook fires: that is what would silently
// regress to the broken ordering.
void TestPdfViewerCoreJs::appOptionsAreUntouchedBeforeTheHookFires() {
  QJSEngine engine;
  loadCore(engine);

  QVERIFY(
      eval(engine, QStringLiteral("window.__options.annotationEditorMode === undefined")).toBool());
  QVERIFY(eval(engine, QStringLiteral("window.__options.workerSrc === undefined")).toBool());
}
// ============ Comment overlay coordinate math ============
//
// Anchors are persisted in PDF PAGE SPACE, which is what makes a stored
// highlight survive zoom, rotation and window resize: only the projection
// changes. These cases pin both directions of that projection with a fake
// viewport, so they fail if someone "simplifies" the math into CSS pixels.

namespace {
// A viewport that mimics pdf.js's: a scale, and a flipped Y axis (PDF space has
// its origin at the bottom-left, CSS at the top-left).
//
// NOTE the `VXC` alias. `class PdfViewerCore` is a LEXICAL top-level binding, so
// it does not become a property of the global object and is not visible to a
// later, separate QJSEngine::evaluate() call. Reaching the statics through the
// instance's constructor is what keeps these cases running against the REAL
// shipped file rather than a copy.
const char *const c_viewportHarness = R"JS(
var VXC = window.vxcore.constructor;

window.__makeViewport = function(p_scale, p_pageHeightPdf) {
    return {
        convertToPdfPoint: function(x, y) {
            return [x / p_scale, p_pageHeightPdf - (y / p_scale)];
        },
        convertToViewportPoint: function(x, y) {
            return [x * p_scale, (p_pageHeightPdf - y) * p_scale];
        }
    };
};

window.__rect = function(l, t, r, b) {
    return { left: l, top: t, right: r, bottom: b, width: r - l, height: b - t };
};

window.__pageRect = function(l, t) { return { left: l, top: t }; };
)JS";
} // namespace

void TestPdfViewerCoreJs::clientRectProjectsIntoPdfPageSpace() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  // scale 2, page 500 PDF units tall, page element offset by (100, 50) on screen.
  QVERIFY(!eval(engine, QStringLiteral("window.__vp = window.__makeViewport(2, 500);")).isError());
  QVERIFY(!eval(engine, QStringLiteral("window.__q = VXC.clientRectToPdfQuad("
                                       "  window.__rect(120, 70, 140, 90),"
                                       "  window.__pageRect(100, 50), window.__vp);"))
               .isError());

  QCOMPARE(eval(engine, QStringLiteral("window.__q.length")).toInt(), 8);
  // Viewport coords are (20,20)-(40,40); at scale 2 that is PDF x 10..20 and,
  // with the Y flip, PDF y 490 (top) down to 480 (bottom).
  QCOMPARE(eval(engine, QStringLiteral("window.__q[0]")).toNumber(), 10.0);  // TL x
  QCOMPARE(eval(engine, QStringLiteral("window.__q[1]")).toNumber(), 490.0); // TL y
  QCOMPARE(eval(engine, QStringLiteral("window.__q[2]")).toNumber(), 20.0);  // TR x
  QCOMPARE(eval(engine, QStringLiteral("window.__q[5]")).toNumber(), 480.0); // BR y
  QCOMPARE(eval(engine, QStringLiteral("window.__q[6]")).toNumber(), 10.0);  // BL x
}

void TestPdfViewerCoreJs::pdfQuadProjectsBackToACssBox() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  QVERIFY(!eval(engine, QStringLiteral("window.__vp = window.__makeViewport(2, 500);")).isError());
  QVERIFY(!eval(engine, QStringLiteral("window.__box = VXC.pdfQuadToPageBox("
                                       "  [10,490, 20,490, 20,480, 10,480], window.__vp);"))
               .isError());

  QVERIFY(!eval(engine, QStringLiteral("window.__box === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__box.left")).toNumber(), 20.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.top")).toNumber(), 20.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.width")).toNumber(), 20.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.height")).toNumber(), 20.0);
}

// The property that actually matters: a highlight captured at one zoom must
// land in the same place on the page at any other zoom.
void TestPdfViewerCoreJs::projectionRoundTripsThroughAScaledViewport() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  QVERIFY(!eval(engine, QStringLiteral(
                            "window.__captureVp = window.__makeViewport(1, 800);"
                            "window.__q = VXC.clientRectToPdfQuad("
                            "  window.__rect(10, 100, 210, 130), window.__pageRect(0, 0),"
                            "  window.__captureVp);"
                            // Re-project at 3x zoom.
                            "window.__renderVp = window.__makeViewport(3, 800);"
                            "window.__box = VXC.pdfQuadToPageBox(window.__q, window.__renderVp);"))
               .isError());

  // The box must scale exactly with the viewport, with no drift.
  QCOMPARE(eval(engine, QStringLiteral("window.__box.left")).toNumber(), 30.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.top")).toNumber(), 300.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.width")).toNumber(), 600.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box.height")).toNumber(), 90.0);
}

// A corrupt or hand-edited anchor must not produce an invisible zero-sized
// element or a NaN-positioned one; it is simply not drawn.
void TestPdfViewerCoreJs::degenerateQuadsProduceNoBox() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());
  QVERIFY(!eval(engine, QStringLiteral("window.__vp = window.__makeViewport(1, 500);")).isError());

  // Each expression is evaluated on its own so a THROW (which would make
  // `=== null` unreachable and the assertion vacuous) is distinguishable from a
  // genuine null.
  const auto boxIsNull = [&engine](const QString &p_quad) {
    const auto result =
        eval(engine, QStringLiteral("VXC.pdfQuadToPageBox(%1, window.__vp) === null").arg(p_quad));
    return !result.isError() && result.toBool();
  };

  QVERIFY(boxIsNull(QStringLiteral("null")));
  QVERIFY(boxIsNull(QStringLiteral("[1,2,3]")));
  // Zero area.
  QVERIFY(boxIsNull(QStringLiteral("[5,5, 5,5, 5,5, 5,5]")));
  // Non-finite.
  QVERIFY(boxIsNull(QStringLiteral("[0,0, Infinity,0, 10,10, 0,10]")));

  // Sanity: a good quad is NOT null, so the helper is not vacuously true.
  QVERIFY(!boxIsNull(QStringLiteral("[0,500, 10,500, 10,490, 0,490]")));
}

// A selection dragged across a page break must become ONE anchor PER PAGE, or
// the single `page` field on an anchor would be a lie.
void TestPdfViewerCoreJs::selectionRectsGroupPerPage() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  QVERIFY(
      !eval(engine, QStringLiteral("window.__vp = window.__makeViewport(1, 500);"
                                   "window.__lookup = function(p_rect) {"
                                   "  var page = p_rect.top < 100 ? 0 : 1;"
                                   "  return { pageNumber: page, pageRect: window.__pageRect(0, 0),"
                                   "           viewport: window.__vp };"
                                   "};"
                                   "window.__groups = VXC.groupRectsByPage(["
                                   "  window.__rect(0, 10, 10, 20),"
                                   "  window.__rect(0, 30, 10, 40),"
                                   "  window.__rect(0, 110, 10, 120)"
                                   "], window.__lookup, 512);"))
           .isError());

  QCOMPARE(eval(engine, QStringLiteral("window.__groups.length")).toInt(), 2);
  QCOMPARE(eval(engine, QStringLiteral("window.__groups[0].page")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__groups[0].quads.length")).toInt(), 2);
  QCOMPARE(eval(engine, QStringLiteral("window.__groups[1].page")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__groups[1].quads.length")).toInt(), 1);
}

void TestPdfViewerCoreJs::selectionRectsAreCappedAndDegenerateOnesDropped() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  QVERIFY(
      !eval(engine, QStringLiteral("window.__vp = window.__makeViewport(1, 500);"
                                   "window.__lookup = function() {"
                                   "  return { pageNumber: 0, pageRect: window.__pageRect(0, 0),"
                                   "           viewport: window.__vp };"
                                   "};"
                                   // A collapsed caret rect carries no area and must be skipped.
                                   "window.__rects = [window.__rect(0, 0, 0, 0)];"
                                   "for (var i = 0; i < 50; ++i) {"
                                   "  window.__rects.push(window.__rect(0, i, 10, i + 5));"
                                   "}"
                                   "window.__groups = VXC.groupRectsByPage("
                                   "  window.__rects, window.__lookup, 10);"))
           .isError());

  QCOMPARE(eval(engine, QStringLiteral("window.__groups.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__groups[0].quads.length")).toInt(), 10);

  // A page the lookup rejects contributes nothing rather than defaulting to 0.
  QVERIFY(!eval(engine, QStringLiteral(
                            "window.__none = VXC.groupRectsByPage("
                            "  [window.__rect(0, 0, 10, 10)], function() { return null; }, 10);"))
               .isError());
  QCOMPARE(eval(engine, QStringLiteral("window.__none.length")).toInt(), 0);
}

// ============ Tools: ink and free text ============
//
// The tools are MODES, and a mode that cannot be left (or that leaks across a
// document) is worse than no mode at all. These drive PdfViewerCore directly
// with a fake pdf.js app, so the anchor geometry is checked without a browser.

namespace {
// A minimal pdf.js stand-in: one page, a known rect, and the same flipped-Y
// viewport the real one uses.
const char *const c_toolHarness = R"JS(
// Enough DOM for the render path: extendInk() repaints during the drag so the
// user sees the stroke as they draw, and renderAllComments() builds real nodes.
window.__mkEl = function() {
    var el = {
        style: {},
        className: '',
        textContent: '',
        attrs: {},
        children: [],
        // renderInkDraft() reaches the <polyline> through svg.firstChild, so the
        // stub has to maintain it or the whole draft path silently no-ops.
        firstChild: null,
        classList: {
            add: function() {}, remove: function() {}, toggle: function() {},
            contains: function() { return false; }
        },
        setAttribute: function(k, v) { this.attrs[k] = v; },
        getAttribute: function(k) { return this.attrs[k]; },
        appendChild: function(c) {
            this.children.push(c);
            if (this.children.length === 1) { this.firstChild = c; }
            return c;
        },
        addEventListener: function() {},
        querySelector: function() { return null; }
    };
    return el;
};
document.createElement = function() { return window.__mkEl(); };
// Every SVG node the draft path builds is recorded, which is how the ink-draft
// DOM assertions reach the <polyline> without teaching the stub a real
// querySelector (that would change what every other test sees).
window.__nsEls = [];
document.createElementNS = function() {
    var el = window.__mkEl();
    window.__nsEls.push(el);
    return el;
};
window.__attrEl = function(name) {
    for (var i = window.__nsEls.length - 1; i >= 0; --i) {
        if (window.__nsEls[i].attrs[name] !== undefined) { return window.__nsEls[i]; }
    }
    return null;
};

window.__added = [];
window.__toolFinished = 0;
window.__pageCount = 0;

window.__adapter = {
    setDocumentPageCount: function(n) { window.__pageCount = n; },
    setOutline: function() {},
    requestAddComment: function(a, c) { window.__added.push({ anchor: a, color: c }); },
    requestSelectComment: function() {},
    requestDeleteComment: function() {},
    notifyToolFinished: function() { window.__toolFinished++; }
};

window.__mkViewport = function(scale, pageHeightPdf) {
    return {
        scale: scale,
        convertToPdfPoint: function(x, y) {
            return [x / scale, pageHeightPdf - (y / scale)];
        },
        convertToViewportPoint: function(x, y) {
            return [x * scale, (pageHeightPdf - y) * scale];
        }
    };
};

window.__pageDiv = {
    getBoundingClientRect: function() {
        return { left: 100, top: 50, right: 700, bottom: 850, width: 600, height: 800 };
    },
    querySelector: function() { return null; },
    appendChild: function() {}
};

window.__app = {
    pagesCount: 1,
    pdfDocument: {},
    pdfViewer: {
        getPageView: function(i) {
            return i === 0 ? { div: window.__pageDiv, viewport: window.__mkViewport(2, 800) } : null;
        }
    },
    eventBus: { on: function() {} }
};

window.vxcore.commentApp = window.__app;
window.vxcore.setCommentAdapter(window.__adapter);
)JS";

// Minimal text-selection stub, layered ON TOP of c_toolHarness: one non-empty
// rect well inside the harness page, so captureSelection() produces exactly one
// pdf-quads intent.
const char *const c_selectionHarness = R"JS(
window.getSelection = function() {
    return {
        isCollapsed: false,
        rangeCount: 1,
        toString: function() { return 'selected text'; },
        getRangeAt: function() {
            return {
                getClientRects: function() {
                    return [{ left: 150, top: 100, right: 250, bottom: 120,
                              width: 100, height: 20 }];
                }
            };
        },
        removeAllRanges: function() {}
    };
};
)JS";
} // namespace

void TestPdfViewerCoreJs::toolIsAModeAndEscLeavesIt() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.tool")).toString(), QStringLiteral("none"));

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.tool")).toString(), QStringLiteral("ink"));

  // finishTool() is what Esc and the one-shot Text tool both call: it must drop
  // the tool AND tell C++, or the toolbar toggle stays pressed.
  eval(engine, QStringLiteral("window.vxcore.finishTool();"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.tool")).toString(), QStringLiteral("none"));
  QCOMPARE(eval(engine, QStringLiteral("window.__toolFinished")).toInt(), 1);

  // Idempotent: leaving a tool that is not armed must not spam the bridge.
  eval(engine, QStringLiteral("window.vxcore.finishTool();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__toolFinished")).toInt(), 1);

  // Switching tools must drop any in-flight stroke, or it would be committed
  // into the wrong tool's gesture.
  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.setTool('freetext');"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.inkDraft === null")).toBool());
}

void TestPdfViewerCoreJs::inkDragProducesAPageSpaceStroke() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  // scale 2, page 800 tall, page element at (100, 50). The INK tool's own
  // options drive the colour -- setCommentColor() is highlight-only now.
  const QJSValue drag =
      eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { color: 'blue' });"
                                  "window.vxcore.setTool('ink');"
                                  "window.vxcore.beginInk(140, 90);"
                                  "window.vxcore.extendInk(200, 150);"
                                  "window.vxcore.endInk();"));
  QVERIFY2(!drag.isError(), qPrintable(drag.toString()));

  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.type")).toString(),
           QStringLiteral("pdf-ink"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].color")).toString(),
           QStringLiteral("blue"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.page")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.strokes.length")).toInt(), 1);

  // Client (140,90) is (40,40) on the page; at scale 2 that is PDF (20, 780).
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.strokes[0][0]")).toNumber(), 20.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.strokes[0][1]")).toNumber(),
           780.0);
  // Client (200,150) is (100,100) -> PDF (50, 750).
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.strokes[0][2]")).toNumber(), 50.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.strokes[0][3]")).toNumber(),
           750.0);

  // The draft is cleared, so the next drag starts fresh.
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.inkDraft === null")).toBool());
}

// A click while Draw is armed is not a stroke; committing it would litter the
// page with invisible one-point scribbles.
void TestPdfViewerCoreJs::aClickWithoutADragCommitsNoInk() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.endInk();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 0);

  // A drag that starts off-page is not a stroke either.
  eval(engine, QStringLiteral("window.vxcore.beginInk(5, 5);"
                              "window.vxcore.extendInk(6, 6);"
                              "window.vxcore.endInk();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::freeTextIsOneShotAndPlacesAPageSpacePoint() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setTool('freetext');"
                              "window.vxcore.placeFreeText(140, 90);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.type")).toString(),
           QStringLiteral("pdf-freetext"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.x")).toNumber(), 20.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.y")).toNumber(), 780.0);
  QVERIFY(eval(engine, QStringLiteral("window.__added[0].anchor.fontSize > 0")).toBool());

  // ONE-SHOT: placing disarms the tool and tells C++, so the toolbar toggle
  // un-presses. That rule lives in placeFreeText() rather than only in the
  // pointerdown handler precisely so it is reachable here.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.tool")).toString(), QStringLiteral("none"));
  QCOMPARE(eval(engine, QStringLiteral("window.__toolFinished")).toInt(), 1);

  // ...and a second click without re-arming places nothing more.
  eval(engine, QStringLiteral("window.vxcore.placeFreeText(200, 200);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);

  // Off-page clicks place nothing, and must NOT disarm.
  eval(engine, QStringLiteral("window.vxcore.setTool('freetext');"
                              "window.vxcore.placeFreeText(5, 5);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.tool")).toString(),
           QStringLiteral("freetext"));
}

// Ink is stored in page space, so a stroke must re-project when the zoom
// changes -- the same property the highlight quads have.
void TestPdfViewerCoreJs::inkStrokesProjectToPolylinePoints() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.constructor.inkStrokeToPolylinePoints("
                                       "  [20, 780, 50, 750], window.__mkViewport(1, 800))"))
               .toString(),
           QStringLiteral("20,20 50,50"));

  // Same stroke at 3x zoom: every coordinate scales, nothing drifts.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.constructor.inkStrokeToPolylinePoints("
                                       "  [20, 780, 50, 750], window.__mkViewport(3, 800))"))
               .toString(),
           QStringLiteral("60,60 150,150"));

  // A malformed stroke draws nothing rather than a NaN path.
  for (const auto &bad :
       {QStringLiteral("null"), QStringLiteral("[1,2,3]"), QStringLiteral("[0, Infinity]")}) {
    QCOMPARE(eval(engine, QStringLiteral("window.vxcore.constructor.inkStrokeToPolylinePoints("
                                         "  %1, window.__mkViewport(1, 800))")
                              .arg(bad))
                 .toString(),
             QString());
  }
}

// Two pointers (a palm plus a pen, or two fingers) must not share one stroke.
// Without pointer ownership the second down overwrites the draft, either
// pointer's move extends it, and either pointer's up commits it.
void TestPdfViewerCoreJs::aSecondPointerCannotHijackAnInkStroke() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90, 1);"));

  // A second pointer is refused outright.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.beginInk(300, 300, 2)")).toBool(), false);

  // Its movement does not extend the first pointer's stroke...
  eval(engine, QStringLiteral("window.vxcore.extendInk(400, 400, 2);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.inkDraft.points.length")).toInt(), 2);

  // ...and its release does not commit it.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.endInk(2)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.inkDraft !== null")).toBool());

  // The owning pointer still works.
  eval(engine, QStringLiteral("window.vxcore.extendInk(200, 150, 1);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.endInk(1)")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);
}

// pointercancel / lostpointercapture mean the gesture did NOT complete. Saving
// it would persist a stroke the user aborted.
void TestPdfViewerCoreJs::cancellingAnInkGestureDiscardsIt() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90, 1);"
                              "window.vxcore.extendInk(200, 150, 1);"
                              "window.vxcore.abortInk();"));

  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.inkDraft === null")).toBool());

  // Leaving the tool mid-stroke discards too, rather than committing a
  // half-drawn scribble.
  eval(engine, QStringLiteral("window.vxcore.beginInk(140, 90, 1);"
                              "window.vxcore.extendInk(200, 150, 1);"
                              "window.vxcore.finishTool();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.inkDraft === null")).toBool());
}

// A pen emits 60-240 samples a second. Rebuilding every comment on every page
// per sample is quadratic in the comment set and visibly freezes the page, so
// the draft owns a separate node and extendInk() must not touch the rest.
void TestPdfViewerCoreJs::extendInkDoesNotRebuildExistingComments() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  // Count how often the render path builds comment nodes.
  QVERIFY(!eval(engine, QStringLiteral(
                            "window.__renderCalls = 0;"
                            "window.vxcore.renderQuads = function() { window.__renderCalls++; };"
                            "window.vxcore.renderInk = function() { window.__renderCalls++; };"
                            "window.vxcore.renderFreeText = function() { window.__renderCalls++; };"
                            "window.vxcore.setComments([{ id: 'a', color: 'yellow',"
                            "  anchor: { type: 'pdf-quads', page: 0,"
                            "            quads: [[0,0,1,0,1,1,0,1]], text: 'x' } }]);"))
               .isError());

  const int afterPublish = eval(engine, QStringLiteral("window.__renderCalls")).toInt();
  QVERIFY2(afterPublish > 0, "publishing a comment set must render it");

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90, 1);"));
  for (int i = 0; i < 20; ++i) {
    eval(engine, QStringLiteral("window.vxcore.extendInk(%1, %2, 1);").arg(150 + i).arg(100 + i));
  }

  QCOMPARE(eval(engine, QStringLiteral("window.__renderCalls")).toInt(), afterPublish);
}

// Requirement 4, at the page: each tool paints with ITS OWN colour. Config and
// adapter independence can pass while the page still paints everything one
// colour, so this is a separate gate.
void TestPdfViewerCoreJs::eachToolUsesItsOwnColour() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_selectionHarness)).isError());

  QVERIFY(!eval(engine,
                QStringLiteral("window.vxcore.setToolOptions('highlight', { color: 'yellow' });"
                               "window.vxcore.setToolOptions('ink', { color: 'blue' });"
                               "window.vxcore.setToolOptions('freetext', { color: 'purple' });"))
               .isError());

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.extendInk(200, 150);"
                              "window.vxcore.endInk();"));
  eval(engine, QStringLiteral("window.vxcore.setTool('freetext');"
                              "window.vxcore.placeFreeText(140, 90);"));
  eval(engine, QStringLiteral("window.vxcore.setTool('highlight');"
                              "window.vxcore.captureSelection();"));

  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 3);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.type")).toString(),
           QStringLiteral("pdf-ink"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].color")).toString(),
           QStringLiteral("blue"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[1].anchor.type")).toString(),
           QStringLiteral("pdf-freetext"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[1].color")).toString(),
           QStringLiteral("purple"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[2].anchor.type")).toString(),
           QStringLiteral("pdf-quads"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[2].color")).toString(),
           QStringLiteral("yellow"));
}

// setCommentColor() survives, but its meaning NARROWED to the highlight tool:
// the page context menu carries an explicit colour with its capture request.
// Every other tool must be untouched by it.
void TestPdfViewerCoreJs::setCommentColorIsHighlightOnly() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { color: 'blue' });"
                              "window.vxcore.setToolOptions('freetext', { color: 'purple' });"
                              "window.vxcore.setCommentColor('pink');"));

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('highlight').color")).toString(),
           QStringLiteral("pink"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('ink').color")).toString(),
           QStringLiteral("blue"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('freetext').color")).toString(),
           QStringLiteral("purple"));
}

// Width and font size were hardcoded constants; the menus now drive them, and
// the anchor is what the store keeps.
void TestPdfViewerCoreJs::scalarOptionsReachTheAnchors() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { width: 3.0 });"
                              "window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.extendInk(200, 150);"
                              "window.vxcore.endInk();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.width")).toNumber(), 3.0);

  eval(engine, QStringLiteral("window.vxcore.setToolOptions('freetext', { fontSize: 16 });"
                              "window.vxcore.setTool('freetext');"
                              "window.vxcore.placeFreeText(140, 90);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added.length")).toInt(), 2);
  QCOMPARE(eval(engine, QStringLiteral("window.__added[1].anchor.fontSize")).toNumber(), 16.0);

  // An absent key leaves the current value alone rather than resetting it.
  eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { color: 'green' });"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('ink').width")).toNumber(), 3.0);
}

// renderInkDraft() is changed but otherwise ungated: the provisional stroke the
// user sees WHILE drawing must already match the configured ink settings.
void TestPdfViewerCoreJs::inkDraftDomReflectsTheConfiguredInk() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { color: 'green', width: 3.0 });"
                              "window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.extendInk(200, 150);"));

  // The polyline is the node carrying stroke-width. The harness viewport is at
  // scale 2, so 3.0 PDF units render as 6 CSS pixels.
  QCOMPARE(eval(engine, QStringLiteral("window.__attrEl('stroke-width').attrs['stroke-width']"))
               .toString(),
           QStringLiteral("6"));
  QCOMPARE(eval(engine, QStringLiteral("window.__attrEl('data-vx-color').attrs['data-vx-color']"))
               .toString(),
           QStringLiteral("green"));
}

// Opacity is the one option the C++ chain can persist and push while JS
// silently discards it, in which case the slider looks functional and does
// nothing. Gate all four hops: retained -> anchor -> draft DOM -> render DOM.
void TestPdfViewerCoreJs::inkOpacityReachesTheAnchorTheDraftAndTheRender() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());

  // The default is solid, so a build that never sets it renders as before.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('ink').opacity")).toNumber(), 1.0);

  eval(engine, QStringLiteral("window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.extendInk(200, 150);"
                              "window.vxcore.endInk();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.opacity")).toNumber(), 1.0);

  // setToolOptions() must COPY it -- this is the hop that would otherwise drop
  // the whole feature on the floor.
  eval(engine, QStringLiteral("window.vxcore.setToolOptions('ink', { opacity: 0.35 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.optionsFor('ink').opacity")).toNumber(),
           0.35);

  eval(engine, QStringLiteral("window.__added = [];"
                              "window.vxcore.setTool('ink');"
                              "window.vxcore.beginInk(140, 90);"
                              "window.vxcore.extendInk(200, 150);"));
  // The in-flight stroke previews at the chosen opacity. Unlike the width it is
  // NOT multiplied by viewport.scale.
  QCOMPARE(eval(engine, QStringLiteral("window.__attrEl('stroke-opacity').attrs['stroke-opacity']"))
               .toString(),
           QStringLiteral("0.35"));

  eval(engine, QStringLiteral("window.vxcore.endInk();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__added[0].anchor.opacity")).toNumber(), 0.35);

  // A stored comment renders with ITS OWN opacity, so two strokes drawn at
  // different settings keep them.
  eval(engine, QStringLiteral("window.__nsEls = [];"
                              "window.vxcore.renderInk(window.__mkEl(),"
                              "  { id: 'a', color: 'yellow',"
                              "    anchor: { type: 'pdf-ink', page: 0, strokes: [[20,780,50,750]],"
                              "              width: 1.5, opacity: 0.2 } },"
                              "  window.__mkViewport(2, 800));"));
  QCOMPARE(eval(engine, QStringLiteral("window.__attrEl('stroke-opacity').attrs['stroke-opacity']"))
               .toString(),
           QStringLiteral("0.2"));

  // ...and a LEGACY comment with no opacity key renders solid.
  eval(engine, QStringLiteral("window.__nsEls = [];"
                              "window.vxcore.renderInk(window.__mkEl(),"
                              "  { id: 'b', color: 'yellow',"
                              "    anchor: { type: 'pdf-ink', page: 0, strokes: [[20,780,50,750]],"
                              "              width: 1.5 } },"
                              "  window.__mkViewport(2, 800));"));
  QCOMPARE(eval(engine, QStringLiteral("window.__attrEl('stroke-opacity').attrs['stroke-opacity']"))
               .toString(),
           QStringLiteral("1"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerCoreJs)
#include "test_pdfviewercore_js.moc"
