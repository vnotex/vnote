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

// pdfviewer.mjs registers its last-chance free-text flush at FILE SCOPE, on
// window 'pagehide' and document 'visibilitychange', so the recorders have to
// exist BEFORE the file is evaluated. (The core prelude's document recorder has
// already captured 'webviewerloaded' by now, so replacing it here is safe.)
window.__winListeners = {};
window.addEventListener = function(p_name, p_cb) { window.__winListeners[p_name] = p_cb; };
window.__docListeners = {};
document.addEventListener = function(p_name, p_cb) { window.__docListeners[p_name] = p_cb; };
document.visibilityState = 'visible';

// AppOptions is supplied by the CORE prelude, because pdfviewercore.js is what
// configures it (from 'webviewerloaded'). pdfviewer.mjs must NOT touch it: a
// deferred module runs after pdf.js has already read every option. Redefining
// the stub here would mask that.

// Must satisfy EVERYTHING the channel callback does, not just the outline
// calls: it connects every signal unconditionally and in order, so ONE missing
// stub makes the whole callback throw at that point and silently skips every
// connection after it. (setOutlineAdapter() runs early, so the outline cases
// would still pass while the comment wiring was never established -- which is
// exactly what happened before this list was completed. Assert the callback
// itself does not throw.)
window.__scrollHandler = null;
window.__editHandler = null;
window.__editableHandler = null;
window.__texts = [];
window.__deleted = [];
window.__fakeAdapter = {
    urlUpdated:                 { connect: function() {} },
    outlineItemScrollRequested: { connect: function(fn) { window.__scrollHandler = fn; } },
    commentsUpdated:            { connect: function() {} },
    commentScrollRequested:     { connect: function() {} },
    commentTextEditRequested:   { connect: function(fn) { window.__editHandler = fn; } },
    commentsEditableChanged:    { connect: function(fn) { window.__editableHandler = fn; } },
    captureSelectionRequested:  { connect: function() {} },
    toolOptionsChanged:         { connect: function() {} },
    toolChanged:                { connect: function() {} },
    setOutline: function(o) { window.__published.push(o); },
    setDocumentPageCount: function() {},
    requestSetCommentText: function(id, t) { window.__texts.push({ id: id, text: t }); },
    requestDeleteComment: function(id) { window.__deleted.push(id); },
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

  // The inline free-text editor: the Text tool's typing half.
  void freeTextEditIsRefusedWhenTheStoreIsReadOnly();
  void freeTextEditCommitsTheBodyAsAnIntent();
  void anAbandonedNewBoxIsRemovedNotLeftEmpty();
  void anExistingBoxClearedOnPurposeIsEmptiedNotDeleted();
  void typingIsStreamedSoATeardownCannotLoseIt();
  void escapeRevertsAnExistingBoxAndDropsANewOne();
  void aRepaintKeepsTheEditorOpenAndItsDraftIntact();
  void aGenuineBlurCommitsExactlyOnce();
  void freeTextBodyIsNormalizedAndCapped();
  void caretOffsetResolvesToATextNodePosition();
  void caretAndBodyAreMeasuredInTheSameCoordinates();
  void glueFlushesTheDraftOnPageLifecycleEvents();
  void glueWiresTheInlineEditorSignals();

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

  // The callback connects every signal in order, so a throw partway through
  // would silently skip the rest of the wiring while leaving the outline
  // assertions below green.
  const QJSValue cb =
      eval(engine,
           QStringLiteral("window.__channelCb({ objects: { vxAdapter: window.__fakeAdapter } });"));
  QVERIFY2(!cb.isError(), qPrintable(cb.toString()));
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
//
// Nodes carry `nodeType` / `nodeName` / `childNodes` and textContent installs a
// real TEXT NODE, so the production body reader (flattenEditorText(), chosen for
// anything with childNodes) is the branch these cases execute -- a stub without
// childNodes would silently exercise the fallback instead.
//
// Listeners are RETAINED rather than swallowed, and replacing a node's
// textContent dispatches 'blur' on the element children it removes. A browser
// gives no such guarantee for an arbitrary removal; this is a deliberately
// CONSERVATIVE worst case, so the blur-vs-DOM-churn guard is exercised on every
// layer rebuild instead of only in the (unreproducible) real timing.
window.__fire = function(p_el, p_type, p_event) {
    if (!p_el || !p_el.listeners || !p_el.listeners[p_type]) {
        return 0;
    }
    var evt = p_event || {};
    if (typeof evt.preventDefault !== 'function') { evt.preventDefault = function() {}; }
    if (typeof evt.stopPropagation !== 'function') { evt.stopPropagation = function() {}; }
    var list = p_el.listeners[p_type];
    for (var i = 0; i < list.length; ++i) { list[i](evt); }
    return list.length;
};

window.__mkTextNode = function(p_value) {
    return { nodeType: 3, nodeValue: p_value, childNodes: [] };
};

window.__mkEl = function() {
    var el = {
        nodeType: 1,
        nodeName: 'DIV',
        childNodes: [],
        style: {},
        className: '',
        attrs: {},
        listeners: {},
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
            this.childNodes.push(c);
            this.firstChild = this.childNodes[0];
            return c;
        },
        addEventListener: function(t, fn) {
            if (!this.listeners[t]) { this.listeners[t] = []; }
            this.listeners[t].push(fn);
        },
        querySelector: function() { return null; }
    };
    Object.defineProperty(el, 'children', {
        get: function() {
            var out = [];
            for (var i = 0; i < el.childNodes.length; ++i) {
                if (el.childNodes[i].nodeType === 1) { out.push(el.childNodes[i]); }
            }
            return out;
        }
    });
    Object.defineProperty(el, 'textContent', {
        get: function() {
            var out = '';
            for (var i = 0; i < el.childNodes.length; ++i) {
                if (el.childNodes[i].nodeType === 3) { out += el.childNodes[i].nodeValue; }
            }
            return out;
        },
        set: function(v) {
            // Assigning textContent REMOVES every existing child, which is what
            // detaches a focused contenteditable during a layer rebuild.
            var removed = el.children;
            for (var i = 0; i < removed.length; ++i) {
                window.__fire(removed[i], 'blur');
            }
            el.childNodes = (v === '') ? [] : [window.__mkTextNode(v)];
            el.firstChild = el.childNodes.length ? el.childNodes[0] : null;
        }
    });
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
window.__deleted = [];
window.__texts = [];
window.__selected = [];

window.__adapter = {
    setDocumentPageCount: function(n) { window.__pageCount = n; },
    setOutline: function() {},
    requestAddComment: function(a, c) { window.__added.push({ anchor: a, color: c }); },
    requestSelectComment: function(id) { window.__selected.push(id); },
    requestDeleteComment: function(id) { window.__deleted.push(id); },
    requestSetCommentText: function(id, t) { window.__texts.push({ id: id, text: t }); },
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
    // The layer is RETAINED, as a real page div retains it: that is what makes
    // the next renderAllComments() clear a layer that still holds the editor,
    // and therefore fire the blur the guard has to ignore.
    __layer: null,
    getBoundingClientRect: function() {
        return { left: 100, top: 50, right: 700, bottom: 850, width: 600, height: 800 };
    },
    querySelector: function() { return this.__layer; },
    appendChild: function(c) { this.__layer = c; return c; }
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

// ============ The inline free-text editor ============
//
// The Text tool is a PLACE-then-TYPE gesture, and until the editor existed only
// the first half worked: placing a box left an empty "…" placeholder whose only
// editor was the comment dock, which is closed by default. These cases pin the
// decision table (new/existing x empty/non-empty) and the repaint behaviour.

namespace {
// Layered ON TOP of c_toolHarness: one free-text comment on the harness page,
// plus a "type this" helper. The stub element carries textContent (and no
// innerText), which is exactly what currentFreeTextEditText() falls back to.
const char *const c_freeTextHarness = R"JS(
window.__seedFreeText = function(p_text) {
    window.vxcore.setComments([{
        id: 'ft1',
        color: 'yellow',
        text: p_text || '',
        anchor: { type: 'pdf-freetext', page: 0, x: 20, y: 780, fontSize: 12 }
    }]);
};

window.__type = function(p_text) {
    window.vxcore.editingEl.textContent = p_text;
    // Through the REAL 'input' listener, so the draft bookkeeping and the
    // streaming debounce are exercised rather than bypassed.
    window.__fire(window.vxcore.editingEl, 'input');
};
)JS";
} // namespace

// A read-only store must not accept keystrokes at all. Letting the box open and
// then dropping the write is the same silent-discard failure the pdf.js editors
// were disabled for.
void TestPdfViewerCoreJs::freeTextEditIsRefusedWhenTheStoreIsReadOnly() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  // The default is NOT editable, so a page that comes up before C++ has said
  // anything is inert.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.commentsEditable")).toBool(), false);

  eval(engine, QStringLiteral("window.__seedFreeText('');"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.beginFreeTextEdit('ft1', true)")).toBool(),
           false);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());

  // ...and a mid-edit loss of editability makes ONE best-effort flush of the
  // unstreamed tail (the flag may have changed for a reason other than a
  // refused write, and that is the only moment the tail can still be handed
  // over) and then closes the box.
  //
  // It must NOT revert or delete: `CommentController` gates setCommentText and
  // deleteComment on the same flag it has just cleared, so either would be
  // refused and would only fake a restore that never happened.
  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('typed');"
                              "window.vxcore.flushFreeTextDraft();"
                              "window.__texts = [];"
                              // A tail the debounce has not carried yet.
                              "window.__type('typed more');"
                              "window.vxcore.setCommentsEditable(false);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("typed more"));
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::freeTextEditCommitsTheBodyAsAnIntent() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));
  // The editor node exists and is the box itself, not a floating overlay.
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingEl !== null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingEl.attrs['data-vx-id']")).toString(),
           QStringLiteral("ft1"));

  eval(engine, QStringLiteral("window.__type('a note');"
                              "window.vxcore.commitFreeTextEdit();"));

  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].id")).toString(), QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("a note"));
  // Committing CLOSES the editor, and never deletes a box that carries text.
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
}

// "I clicked by mistake" has to look like nothing happened. Leaving the empty
// placeholder behind is what made the tool read as broken in the first place.
void TestPdfViewerCoreJs::anAbandonedNewBoxIsRemovedNotLeftEmpty() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              // Whitespace only is still empty.
                              "window.__type('   \\n  ');"
                              "window.vxcore.commitFreeTextEdit();"));

  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted[0]")).toString(), QStringLiteral("ft1"));
}

// The mirror case: clearing an EXISTING box is an explicit edit, so it writes
// an EMPTY BODY rather than deleting the comment. The dock's editor and the
// box's editor edit the same field and must not disagree -- and deleting a
// comment nobody asked to delete is worse than a visible empty placeholder.
void TestPdfViewerCoreJs::anExistingBoxClearedOnPurposeIsEmptiedNotDeleted() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('old body');"
                              "window.vxcore.beginFreeTextEdit('ft1', false);"
                              "window.__type('');"
                              "window.vxcore.commitFreeTextEdit();"));

  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].id")).toString(), QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(), QString());
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());
}

// The body must not live only in the contenteditable: a tab close, a reload or
// a window close does not reliably deliver a blur, and every teardown path
// discards the draft (after flushing it, which is what the tail of this case
// pins).
void TestPdfViewerCoreJs::typingIsStreamedSoATeardownCannotLoseIt() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('half a sentence');"));

  // The debounce timer is driven directly: QJSEngine has no setTimeout, which
  // is exactly why the flush body is a separate, callable method.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.flushFreeTextDraft()")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("half a sentence"));
  // The editor is STILL OPEN: streaming is not committing.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));

  // Unchanged text does not re-dispatch...
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.flushFreeTextDraft()")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);

  // ...and a draft that has been emptied is NOT streamed as a delete or as an
  // empty write: a mid-gesture select-all must not remove the box.
  eval(engine, QStringLiteral("window.__type('   ');"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.flushFreeTextDraft()")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);

  // The debounce leaves a window in which nothing has been streamed yet, so
  // document teardown ('pagesdestroy' / 'documenterror' -> resetComments) must
  // flush FIRST, while the adapter and the ids are still the old document's.
  eval(engine, QStringLiteral("window.__texts = [];"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('never streamed');"
                              "window.vxcore.resetComments();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("never streamed"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());
  // Teardown never deletes: the box belongs to a document that is going away.
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::escapeRevertsAnExistingBoxAndDropsANewOne() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  // Esc on a NEW box abandons the box, not merely the keystrokes -- even when
  // something was typed, and even when it was already streamed. Driven through
  // the SHIPPED keydown listener rather than by calling the method.
  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('half-written');"
                              "window.vxcore.flushFreeTextDraft();"
                              "window.__texts = [];"
                              "window.__fire(window.vxcore.editingEl, 'keydown',"
                              "              { key: 'Escape' });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 1);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());

  // Esc on an EXISTING box REVERTS: the streaming flush may already have
  // written part of the draft, so merely stopping is not enough.
  eval(engine, QStringLiteral("window.__deleted = [];"
                              "window.__texts = [];"
                              "window.__seedFreeText('stored');"
                              "window.vxcore.beginFreeTextEdit('ft1', false);"
                              "window.__type('scratch');"
                              "window.vxcore.flushFreeTextDraft();"
                              "window.vxcore.cancelFreeTextEdit();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
  // One streamed write, then the revert.
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 2);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[1].text")).toString(),
           QStringLiteral("stored"));

  // ...and an existing box that was never streamed needs no revert write at all.
  eval(engine, QStringLiteral("window.__texts = [];"
                              "window.__seedFreeText('stored');"
                              "window.vxcore.beginFreeTextEdit('ft1', false);"
                              "window.vxcore.cancelFreeTextEdit();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 0);
}

// Scrolling, zooming or rotating rebuilds every comment layer, which detaches
// the editor's node and fires a blur. The editor must come back with the
// uncommitted text still in it -- and that blur must NOT be mistaken for the
// user leaving the box, or the session would end on every scroll.
void TestPdfViewerCoreJs::aRepaintKeepsTheEditorOpenAndItsDraftIntact() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('mid-sentence');"
                              "window.__oldEl = window.vxcore.editingEl;"
                              // What 'pagerendered' / 'scalechanging' do.
                              "window.vxcore.renderAllComments();"));

  // The rebuild really did replace the node (otherwise the guard below would be
  // vacuous: the harness layer fires blur on every child it detaches).
  QVERIFY(eval(engine, QStringLiteral("window.__oldEl !== window.vxcore.editingEl")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingEl.textContent")).toString(),
           QStringLiteral("mid-sentence"));
  // ...and it is read back through the PRODUCTION path: the node carries real
  // child nodes, so currentFreeTextEditText() takes its flattenEditorText()
  // branch here rather than the stub-only fallback.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingEl.childNodes.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingEl.childNodes[0].nodeType")).toInt(),
           3);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.currentFreeTextEditText()")).toString(),
           QStringLiteral("mid-sentence"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__deleted.length")).toInt(), 0);
  // The flag is transient: a genuine blur AFTER the repaint must still commit.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingRerender")).toBool(), false);

  // A blur from the STALE node is inert -- it is not the editor any more.
  eval(engine, QStringLiteral("window.__fire(window.__oldEl, 'blur');"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));
}

// The other half of the guard: a real blur, through the shipped listener,
// commits once and closes the session.
void TestPdfViewerCoreJs::aGenuineBlurCommitsExactlyOnce() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_freeTextHarness)).isError());

  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(true);"
                              "window.__seedFreeText('');"
                              "window.vxcore.beginFreeTextEdit('ft1', true);"
                              "window.__type('typed by hand');"
                              "window.__el = window.vxcore.editingEl;"
                              "window.__fire(window.__el, 'blur');"));

  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("typed by hand"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.editingCommentId === null")).toBool());

  // A second blur on the same (now detached) node dispatches nothing more.
  eval(engine, QStringLiteral("window.__fire(window.__el, 'blur');"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
}

void TestPdfViewerCoreJs::freeTextBodyIsNormalizedAndCapped() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  // TRANSPORT fixes only: CRLF, the single trailing newline Chromium keeps for
  // its closing <br>, and the NUL that would truncate on the C++ side.
  QCOMPARE(eval(engine, QStringLiteral("VXC.normalizeFreeTextBody('a\\r\\nb\\n')")).toString(),
           QStringLiteral("a\nb"));
  QCOMPARE(eval(engine, QStringLiteral("VXC.normalizeFreeTextBody('a\\u0000b')")).toString(),
           QStringLiteral("ab"));
  QCOMPARE(eval(engine, QStringLiteral("VXC.normalizeFreeTextBody(null)")).toString(), QString());

  // The user's OWN whitespace round-trips: an indented body must survive.
  QCOMPARE(eval(engine, QStringLiteral("VXC.normalizeFreeTextBody('  indented  ')")).toString(),
           QStringLiteral("  indented  "));

  // "Is it empty" is a SEPARATE question, and NBSP counts -- that is what
  // contenteditable stores for a run of spaces.
  QVERIFY(eval(engine, QStringLiteral("VXC.isBlankFreeTextBody('')")).toBool());
  QVERIFY(eval(engine, QStringLiteral("VXC.isBlankFreeTextBody(' \\n\\u00a0 ')")).toBool());
  QVERIFY(eval(engine, QStringLiteral("VXC.isBlankFreeTextBody(null)")).toBool());
  QVERIFY(!eval(engine, QStringLiteral("VXC.isBlankFreeTextBody('  x  ')")).toBool());

  // Capped independently of the C++ side, which re-applies its own cap.
  QCOMPARE(eval(engine, QStringLiteral("VXC.normalizeFreeTextBody("
                                       "  new Array(20000).join('x')).length"))
               .toInt(),
           16384);
}

// The caret has to survive a layer rebuild, and "put it back at the end" is not
// good enough: a repaint arriving mid-word (a scroll, a zoom, the echo of a
// streamed write) would otherwise move the caret out from under the user and
// interleave the rest of the sentence. The resolver is pure tree-walking, so it
// is testable without a real selection.
void TestPdfViewerCoreJs::caretOffsetResolvesToATextNodePosition() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  // Chromium splits an edited box into several text nodes around its <br>s, so
  // the offset is a CHARACTER count across the whole subtree, not an index into
  // one node.
  QVERIFY(!eval(engine, QStringLiteral("window.__txt = function(v) {"
                                       "  return { nodeType: 3, nodeValue: v, childNodes: [] };"
                                       "};"
                                       "window.__el = { nodeType: 1, childNodes: ["
                                       "  window.__txt('abc'),"
                                       "  { nodeType: 1, childNodes: [window.__txt('de')] },"
                                       "  window.__txt('fg')"
                                       "] };"))
               .isError());

  const auto resolve = [&engine](int p_offset) {
    return json(engine, QStringLiteral("(function(t) {"
                                       "  return t === null ? null"
                                       "                    : { v: t.node.nodeValue,"
                                       "                        o: t.offset };"
                                       "})(VXC.caretTargetForOffset(window.__el, %1))")
                            .arg(p_offset));
  };

  QCOMPARE(resolve(0), QStringLiteral("{\"v\":\"abc\",\"o\":0}"));
  QCOMPARE(resolve(2), QStringLiteral("{\"v\":\"abc\",\"o\":2}"));
  // Crossing into the nested element, in document order.
  QCOMPARE(resolve(4), QStringLiteral("{\"v\":\"de\",\"o\":1}"));
  QCOMPARE(resolve(6), QStringLiteral("{\"v\":\"fg\",\"o\":1}"));
  // The very end resolves rather than falling off.
  QCOMPARE(resolve(7), QStringLiteral("{\"v\":\"fg\",\"o\":2}"));

  // Past the end / unknown offset yields null, and focusFreeTextEditor()
  // collapses to the end instead of silently landing at 0.
  QCOMPARE(resolve(99), QStringLiteral("null"));
  QCOMPARE(resolve(-1), QStringLiteral("null"));
}

// The body and the caret MUST be measured in one coordinate system.
//
// Chromium represents an entered line break as a <br> (or a block child), which
// innerText reports as '\n' but a DOM Range does NOT contribute a character
// for. Measuring the caret with Range.toString() while reading the body with
// innerText therefore leaves the offset short by one PER LINE -- and since the
// rebuilt node is re-seeded from the draft, which does carry the newlines, the
// caret would silently drift backwards and the rest of the sentence would land
// in the middle of the previous one.
void TestPdfViewerCoreJs::caretAndBodyAreMeasuredInTheSameCoordinates() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_viewportHarness)).isError());

  QVERIFY(
      !eval(engine, QStringLiteral("window.__txt = function(v) {"
                                   "  return { nodeType: 3, nodeValue: v, childNodes: [] };"
                                   "};"
                                   "window.__br = { nodeType: 1, nodeName: 'BR', childNodes: [] };"
                                   "window.__t1 = window.__txt('abc');"
                                   "window.__t2 = window.__txt('def');"
                                   // What a two-line plaintext-only box actually is.
                                   "window.__el = { nodeType: 1, nodeName: 'DIV',"
                                   "                childNodes: [window.__t1, window.__br,"
                                   "                             window.__t2] };"))
           .isError());

  // The body carries the line break, exactly as innerText would report it.
  QCOMPARE(eval(engine, QStringLiteral("VXC.flattenEditorText(window.__el)")).toString(),
           QStringLiteral("abc\ndef"));

  // ...and the caret after the 'd' is offset 5 in THOSE coordinates. A
  // Range.toString() measurement would have said 4, i.e. before the 'd'.
  QCOMPARE(eval(engine, QStringLiteral("VXC.walkEditorText(window.__el, window.__t2, 1).offset"))
               .toInt(),
           5);
  // The boundary before the <br>, and the very start / end of the box.
  QCOMPARE(eval(engine, QStringLiteral("VXC.walkEditorText(window.__el, window.__t1, 3).offset"))
               .toInt(),
           3);
  QCOMPARE(eval(engine, QStringLiteral("VXC.walkEditorText(window.__el, window.__el, 0).offset"))
               .toInt(),
           0);
  QCOMPARE(eval(engine, QStringLiteral("VXC.walkEditorText(window.__el, window.__el, 3).offset"))
               .toInt(),
           7);

  // Round trip: that offset resolves back into the SINGLE text node the rebuild
  // produces (renderFreeText seeds the new node through textContent).
  QVERIFY(!eval(engine, QStringLiteral("window.__rebuilt = { nodeType: 1, nodeName: 'DIV',"
                                       "  childNodes: [window.__txt('abc\\ndef')] };"))
               .isError());
  QCOMPARE(json(engine, QStringLiteral("(function(t) { return { v: t.node.nodeValue, o: t.offset };"
                                       "})(VXC.caretTargetForOffset(window.__rebuilt, 5))")),
           QStringLiteral("{\"v\":\"abc\\ndef\",\"o\":5}"));

  // A block-element line boundary counts the same way.
  QVERIFY(!eval(engine, QStringLiteral(
                            "window.__blocks = { nodeType: 1, nodeName: 'DIV', childNodes: ["
                            "  { nodeType: 1, nodeName: 'DIV', childNodes: [window.__txt('one')] },"
                            "  { nodeType: 1, nodeName: 'DIV', childNodes: [window.__txt('two')] }"
                            "] };"))
               .isError());
  QCOMPARE(eval(engine, QStringLiteral("VXC.flattenEditorText(window.__blocks)")).toString(),
           QStringLiteral("one\ntwo"));
}

// The debounce leaves a window of unstreamed characters, so the page-lifecycle
// events are the last chance to get them across the bridge. They are registered
// at file scope in pdfviewer.mjs, which is why this drives the GLUE.
void TestPdfViewerCoreJs::glueFlushesTheDraftOnPageLifecycleEvents() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  QVERIFY2(eval(engine, QStringLiteral("typeof window.__winListeners['pagehide'] === 'function'"))
               .toBool(),
           "pdfviewer.mjs must register a pagehide flush");
  QVERIFY2(eval(engine,
                QStringLiteral("typeof window.__docListeners['visibilitychange'] === 'function'"))
               .toBool(),
           "pdfviewer.mjs must register a visibilitychange flush");

  const QJSValue cb =
      eval(engine,
           QStringLiteral("window.__channelCb({ objects: { vxAdapter: window.__fakeAdapter } });"));
  QVERIFY2(!cb.isError(), qPrintable(cb.toString()));

  // An open editor holding text the debounce has NOT yet streamed.
  QVERIFY(!eval(engine,
                QStringLiteral("window.__texts = [];"
                               "window.vxcore.setCommentsEditable(true);"
                               "window.vxcore.comments = [{ id: 'ft1', color: 'yellow', text: '',"
                               "  anchor: { type: 'pdf-freetext', page: 0, x: 1, y: 1,"
                               "            fontSize: 12 } }];"
                               "window.vxcore.editingCommentId = 'ft1';"
                               "window.vxcore.editingIsNew = true;"
                               "window.vxcore.editingDraftText = 'unstreamed';"))
               .isError());

  // A VISIBLE visibilitychange is not a teardown and must dispatch nothing.
  eval(engine, QStringLiteral("document.visibilityState = 'visible';"
                              "window.__docListeners['visibilitychange']();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 0);

  eval(engine, QStringLiteral("document.visibilityState = 'hidden';"
                              "window.__docListeners['visibilitychange']();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__texts[0].text")).toString(),
           QStringLiteral("unstreamed"));

  // pagehide right afterwards must not write the same body twice.
  eval(engine, QStringLiteral("window.__winListeners['pagehide']();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__texts.length")).toInt(), 1);
}

// The channel callback connects every signal in order, so a missing connection
// is invisible unless it is asserted: the outline cases would stay green while
// the inline editor was never wired at all.
void TestPdfViewerCoreJs::glueWiresTheInlineEditorSignals() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  const QJSValue cb =
      eval(engine,
           QStringLiteral("window.__channelCb({ objects: { vxAdapter: window.__fakeAdapter } });"));
  QVERIFY2(!cb.isError(), qPrintable(cb.toString()));

  QVERIFY(eval(engine, QStringLiteral("typeof window.__editHandler === 'function'")).toBool());
  QVERIFY(eval(engine, QStringLiteral("typeof window.__editableHandler === 'function'")).toBool());

  // commentsEditableChanged reaches the core...
  eval(engine, QStringLiteral("window.__editableHandler(true);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.commentsEditable")).toBool(), true);

  // ...and commentTextEditRequested opens the editor, flagged as a NEW box (the
  // only route C++ drives it from is a just-placed one).
  eval(engine, QStringLiteral("window.vxcore.comments = [{ id: 'ft1', color: 'yellow', text: '',"
                              "  anchor: { type: 'pdf-freetext', page: 0, x: 1, y: 1,"
                              "            fontSize: 12 } }];"
                              "window.__editHandler('ft1');"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingIsNew")).toBool(), true);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerCoreJs)
#include "test_pdfviewercore_js.moc"
