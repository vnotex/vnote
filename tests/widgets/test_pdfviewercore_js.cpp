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
// console.warn is a RECORDER, not a no-op: checkBuiltInToolbar() and the
// regular-expression refusal are OBSERVABLE only through it.
window.__warnings = [];
var console = {
    log: function(){},
    warn: function(m){ window.__warnings.push(String(m)); },
    error: function(){}
};
window.__listeners = {};
// Elements checkBuiltInToolbar() may find. Empty by default, so the tripwire
// case is the DEFAULT behaviour and has to be opted out of.
window.__domIds = {};
var document = {
    currentScript: { src: "qrc:/web/pdf.js/pdfviewercore.js" },
    addEventListener: function(p_name, p_cb) { window.__listeners[p_name] = p_cb; },
    getElementById: function(p_id) { return window.__domIds[p_id] || null; }
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
        __dispatched: [],
        eventBus: {
            on: function(name, fn) {
                if (!handlers[name]) { handlers[name] = []; }
                handlers[name].push(fn);
            },
            // The event object is forwarded, because the viewer bridge reads
            // evt.pageNumber / evt.scale / evt.mode / evt.matchesCount. The
            // outline and comment handlers ignore it.
            fire: function(name, evt) {
                var hs = handlers[name] || [];
                for (var i = 0; i < hs.length; ++i) { hs[i](evt); }
            },
            // RECORD-ONLY, deliberately: the production commands dispatch
            // pdf.js's COMMAND events ('switchscrollmode') while the bridge
            // listens for its STATE events ('scrollmodechanged'), so a fake that
            // also re-entered the listeners would hide exactly the confusion
            // these cases exist to catch.
            dispatch: function(name, payload) {
                app.__dispatched.push({ name: name, payload: payload });
            },
            count: function(name) {
                return (handlers[name] || []).length;
            }
        },
        pdfDocument: null
    };
    app.pdfDocument = window.__makeDoc(app, p_raw, p_supersede);
    return app;
};

// A viewer app with the members the control bridge reads, plus a recording
// viewsManager (there is NO PDFViewerApplication.pdfSidebar in pdf.js v6).
window.__makeViewerApp = function() {
    var app = window.__makeApp([], false);
    app.pagesCount = 20;
    app.pdfViewer = {
        currentPageNumber: 1,
        currentScale: 1,
        currentScaleValue: 'auto',
        pagesRotation: 0,
        scrollMode: 0,
        spreadMode: 0
    };
    app.__sidebarToggles = 0;
    app.viewsManager = {
        visibleView: 0,
        toggle: function() { app.__sidebarToggles += 1; }
    };
    return app;
};

// Records every setViewerState / setFindText push.
window.__makeViewerAdapter = function() {
    return {
        states: [],
        finds: [],
        setViewerState: function(s) { this.states.push(s); },
        setFindText: function(t, total, index) {
            this.finds.push({ texts: t, total: total, index: index });
        }
    };
};

window.__dispatchedNames = function(p_app) {
    return p_app.__dispatched.map(function(e) { return e.name; });
};

window.__lastDispatch = function(p_app, p_name) {
    for (var i = p_app.__dispatched.length - 1; i >= 0; --i) {
        if (p_app.__dispatched[i].name === p_name) { return p_app.__dispatched[i].payload; }
    }
    return null;
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
// Every viewer-control signal, captured so a case can drive the C++ side of the
// bridge. ONE missing stub makes the whole channel callback throw at that
// point and silently skips every connection after it.
window.__viewerHandlers = {};
window.__connectViewer = function(p_name) {
    return { connect: function(fn) { window.__viewerHandlers[p_name] = fn; } };
};
window.__viewerStates = [];
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
    pageRequested:              window.__connectViewer('page'),
    zoomRequested:              window.__connectViewer('zoom'),
    zoomStepRequested:          window.__connectViewer('zoomStep'),
    rotationRequested:          window.__connectViewer('rotation'),
    scrollModeRequested:        window.__connectViewer('scrollMode'),
    spreadModeRequested:        window.__connectViewer('spreadMode'),
    cursorToolRequested:        window.__connectViewer('cursorTool'),
    sidebarToggleRequested:     window.__connectViewer('sidebar'),
    documentPropertiesRequested: window.__connectViewer('documentProperties'),
    findTextRequested:          window.__connectViewer('find'),
    findCleared:                window.__connectViewer('findCleared'),
    setOutline: function(o) { window.__published.push(o); },
    setDocumentPageCount: function() {},
    setViewerState: function(s) { window.__viewerStates.push(s); },
    setFindText: function() {},
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

  // Moving a free-text box by dragging it.
  void aShortPressStillSelectsInsteadOfMoving();
  void draggingABoxCommitsExactlyOneMoveOnPointerUp();
  void aDragIsRefusedWhileAToolIsArmedOrTheStoreIsReadOnly();
  void aDragIsRefusedWhileTheBoxIsBeingEdited();
  void aSecondPointerDuringADragIsRefused();
  void escapeAndPointerCancelRevertADragWithoutWriting();
  void aRepaintDuringADragKeepsThePreviewAtTheDraggedPoint();
  void aZoomDuringADragKeepsTheGrabOffset();
  void aDropOnAnotherPageCommitsThatPage();
  void aDropOffAnyPageClampsIntoTheSourcePage();
  void aDragThatReturnsToItsStartSendsNothingAndClearsItsState();
  void anUnrelatedCommentPublishDoesNotEndAnActiveDrag();
  void deletingTheDraggedCommentDuringADragCancelsIt();
  void aPendingMoveIsClearedByTheMatchingPublishAndOverriddenByADifferentOne();
  void aSecondDragLeavesExactlyOneSetOfWindowListeners();
  void aDragThatProducesNoClickDoesNotSuppressTheNextClick();
  void aPendingMoveIsCancellableAndBlocksASecondDrag();
  void glueEscapeCancelsBothAnActiveDragAndAPendingMove();

  // The viewer-control bridge behind the native toolbar.
  void viewerBridgeRegistersItsListeners();
  void eachViewerEventProducesExactlyOneStatePush();
  void modeStateComesFromTheChangedEventNotTheSwitchCommand();
  void toggleSidebarReachesViewsManager();
  void gotoPageClampsAndRejectsGarbage();
  void setZoomAcceptsPresetsAndRefusesGarbage();
  void findDistinguishesANewQueryFromNextAndPrevious();
  void findReportsTheFirstMatchAsIndexZero();
  void anEmptyFindQueryClosesTheFindBar();
  void aRegularExpressionFindIsRefused();
  void theBuiltInToolbarGuardWarnsWhenTheContainerIsGone();
  void theSidebarIsNotInsideTheHiddenToolbarSubtree();
  void changingAFindOptionStartsAFreshSearch();
  void glueWiresTheViewerControlSignals();

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
window.__moves = [];

window.__adapter = {
    setDocumentPageCount: function(n) { window.__pageCount = n; },
    setOutline: function() {},
    requestAddComment: function(a, c) { window.__added.push({ anchor: a, color: c }); },
    requestSelectComment: function(id) { window.__selected.push(id); },
    requestDeleteComment: function(id) { window.__deleted.push(id); },
    requestSetCommentText: function(id, t) { window.__texts.push({ id: id, text: t }); },
    requestMoveComment: function(id, page, x, y) {
        window.__moves.push({ id: id, page: page, x: x, y: y });
    },
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

// A SECOND page, retained independently, so a cross-page drag can be asserted
// on the PARENT of the preview node rather than merely on its CSS coordinates.
// It sits directly below the first one, with the same width.
window.__pageDiv2 = {
    __layer: null,
    getBoundingClientRect: function() {
        return { left: 100, top: 900, right: 700, bottom: 1700, width: 600, height: 800 };
    },
    querySelector: function() { return this.__layer; },
    appendChild: function(c) { this.__layer = c; return c; }
};

// Window-level listeners are ARRAYS with real removal, because the drag
// registers three of them per gesture and the leak-free teardown is exactly
// what has to be gated.
window.__winL = {};
window.addEventListener = function(p_name, p_cb) {
    if (!window.__winL[p_name]) { window.__winL[p_name] = []; }
    window.__winL[p_name].push(p_cb);
};
window.removeEventListener = function(p_name, p_cb) {
    var list = window.__winL[p_name];
    if (!list) { return; }
    for (var i = list.length - 1; i >= 0; --i) {
        if (list[i] === p_cb) { list.splice(i, 1); }
    }
};
window.__winCount = function(p_name) {
    return window.__winL[p_name] ? window.__winL[p_name].length : 0;
};
window.__fireWin = function(p_name, p_event) {
    var list = (window.__winL[p_name] || []).slice();
    var evt = p_event || {};
    if (typeof evt.preventDefault !== 'function') { evt.preventDefault = function() {}; }
    if (typeof evt.stopPropagation !== 'function') { evt.stopPropagation = function() {}; }
    for (var i = 0; i < list.length; ++i) { list[i](evt); }
    return list.length;
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

// Opt-in two-page mode. Kept separate so no existing single-page case changes
// behaviour just because the drag cases needed a second page.
window.__useTwoPages = function(p_scale) {
    var scale = p_scale || 2;
    window.__app.pagesCount = 2;
    window.__app.pdfViewer.getPageView = function(i) {
        if (i === 0) {
            return { div: window.__pageDiv, viewport: window.__mkViewport(scale, 800) };
        }
        if (i === 1) {
            return { div: window.__pageDiv2, viewport: window.__mkViewport(scale, 800) };
        }
        return null;
    };
};

// Rebind page 0 at a different zoom, so a scale change mid-drag can be staged.
window.__setScale = function(p_scale) {
    window.__app.pdfViewer.getPageView = function(i) {
        return i === 0 ? { div: window.__pageDiv, viewport: window.__mkViewport(p_scale, 800) }
                       : null;
    };
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

// ============ Moving a free-text box ============
//
// The gesture is press-and-drag on the box body, and it is the FIRST geometry
// mutation in the comment subsystem. The properties that matter are: it never
// fires during the gesture, it commits exactly once, it reverts for free, and
// it can never strand window listeners or leave the preview on the wrong page.

namespace {
// Layered ON TOP of c_toolHarness. Two free-text boxes, so a click-suppression
// case can prove the suppression is scoped to ONE id.
const char *const c_dragHarness = R"JS(
window.__seedBoxes = function() {
    window.vxcore.setComments([
        { id: 'ft1', color: 'yellow', text: 'one',
          anchor: { type: 'pdf-freetext', page: 0, x: 20, y: 780, fontSize: 12 } },
        { id: 'ft2', color: 'yellow', text: 'two',
          anchor: { type: 'pdf-freetext', page: 0, x: 200, y: 400, fontSize: 12 } }
    ]);
};

window.__layerOf = function(p_div) { return p_div.__layer; };

window.__box = function(p_id) {
    var divs = [window.__pageDiv, window.__pageDiv2];
    for (var d = 0; d < divs.length; ++d) {
        var layer = divs[d].__layer;
        if (!layer) { continue; }
        for (var i = 0; i < layer.childNodes.length; ++i) {
            var el = layer.childNodes[i];
            if (el && el.attrs && el.attrs['data-vx-id'] === p_id) { return el; }
        }
    }
    return null;
};

// The page div whose layer currently holds the box, or null.
window.__boxPage = function(p_id) {
    var divs = [window.__pageDiv, window.__pageDiv2];
    for (var d = 0; d < divs.length; ++d) {
        var layer = divs[d].__layer;
        if (!layer) { continue; }
        for (var i = 0; i < layer.childNodes.length; ++i) {
            var el = layer.childNodes[i];
            if (el && el.attrs && el.attrs['data-vx-id'] === p_id) { return divs[d]; }
        }
    }
    return null;
};

window.__press = function(p_id, p_x, p_y, p_pointerId) {
    var el = window.__box(p_id);
    if (!el) { throw new Error('no box ' + p_id); }
    return window.__fire(el, 'pointerdown',
                         { clientX: p_x, clientY: p_y, button: 0,
                           pointerId: (p_pointerId === undefined ? 1 : p_pointerId) });
};

window.__drag = function(p_x, p_y, p_pointerId) {
    return window.__fireWin('pointermove',
                            { clientX: p_x, clientY: p_y,
                              pointerId: (p_pointerId === undefined ? 1 : p_pointerId) });
};

window.__release = function(p_x, p_y, p_pointerId) {
    return window.__fireWin('pointerup',
                            { clientX: p_x, clientY: p_y,
                              pointerId: (p_pointerId === undefined ? 1 : p_pointerId) });
};

window.__armed = function() {
    return window.__winCount('pointermove') + window.__winCount('pointerup') +
           window.__winCount('pointercancel');
};

// QJSEngine has no setTimeout, and the click-suppression fallback is armed
// through one. Without a real queue the case could not tell "the fallback ran"
// from "the fallback does not exist", which is exactly the regression it is
// supposed to gate.
window.__timeouts = [];
window.setTimeout = function(p_cb) {
    window.__timeouts.push(p_cb);
    return window.__timeouts.length;
};
window.__runTimeouts = function() {
    var due = window.__timeouts;
    window.__timeouts = [];
    for (var i = 0; i < due.length; ++i) { due[i](); }
    return due.length;
};

window.__ready = function() {
    window.vxcore.setCommentsEditable(true);
    window.__seedBoxes();
    window.vxcore.renderAllComments();
};
)JS";
} // namespace

// Below the threshold the gesture is a CLICK. Without this every click on a box
// would nudge it by a pixel or two and write a comment to disk.
void TestPdfViewerCoreJs::aShortPressStillSelectsInsteadOfMoving() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === 'ft1'")).toBool());

  // Two pixels: still a click.
  eval(engine, QStringLiteral("window.__drag(142, 91);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragMoved")).toBool(), false);
  eval(engine, QStringLiteral("window.__release(142, 91);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);

  // The box's own click handler still selects, because the sub-threshold path
  // deliberately does NOT repaint (a repaint would destroy the node the click
  // is about to be delivered to).
  eval(engine, QStringLiteral("window.__fire(window.__box('ft1'), 'click', {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__selected")), QStringLiteral("[\"ft1\"]"));
}

void TestPdfViewerCoreJs::draggingABoxCommitsExactlyOneMoveOnPointerUp() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));

  // The grab point IS the anchor here, so the offset is zero and the drop point
  // is simply the pointer's page-space position.
  eval(engine, QStringLiteral("window.__drag(200, 150);"));
  eval(engine, QStringLiteral("window.__drag(260, 210);"));
  QVERIFY2(eval(engine, QStringLiteral("window.__moves.length === 0")).toBool(),
           "no bridge traffic may happen during the gesture");

  eval(engine, QStringLiteral("window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].id")).toString(), QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].page")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].x")).toNumber(), 50.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].y")).toNumber(), 750.0);

  // The preview survives until the authoritative set comes back, so the box
  // does not visibly snap home and out again.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.pendingMoveId")).toString(),
           QStringLiteral("ft1"));
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
}

void TestPdfViewerCoreJs::aDragIsRefusedWhileAToolIsArmedOrTheStoreIsReadOnly() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  // Read-only: no movable class, and the intent is refused even if called
  // directly.
  eval(engine, QStringLiteral("window.__seedBoxes(); window.vxcore.renderAllComments();"));
  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft1',"
                               "  { clientX: 140, clientY: 90, button: 0, pointerId: 1 }, null)"))
               .toBool(),
           false);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());

  // Armed tool: the layer is authoring, and a press must draw, not move.
  eval(engine, QStringLiteral("window.__ready(); window.vxcore.setTool('ink');"
                              "window.vxcore.renderAllComments();"));
  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft1',"
                               "  { clientX: 140, clientY: 90, button: 0, pointerId: 1 }, null)"))
               .toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
}

void TestPdfViewerCoreJs::aDragIsRefusedWhileTheBoxIsBeingEdited() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"
                              "window.vxcore.beginFreeTextEdit('ft1', false);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.editingCommentId")).toString(),
           QStringLiteral("ft1"));

  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft1',"
                               "  { clientX: 140, clientY: 90, button: 0, pointerId: 1 }, null)"))
               .toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);

  // The OTHER box is still movable while this one is being edited.
  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft2',"
                               "  { clientX: 140, clientY: 90, button: 0, pointerId: 1 }, null)"))
               .toBool(),
           true);
}

// Same rule as ink: a palm alongside a pen must not hijack (or duplicate) the
// gesture.
void TestPdfViewerCoreJs::aSecondPointerDuringADragIsRefused() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90, 1);"));
  const int armed = eval(engine, QStringLiteral("window.__armed()")).toInt();
  QCOMPARE(armed, 3);

  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft2',"
                               "  { clientX: 300, clientY: 300, button: 0, pointerId: 2 }, null)"))
               .toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 3);

  // A move/up carrying the OTHER pointer id is ignored outright.
  eval(engine, QStringLiteral("window.__drag(300, 300, 2);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragMoved")).toBool(), false);
  eval(engine, QStringLiteral("window.__release(300, 300, 2);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === 'ft1'")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::escapeAndPointerCancelRevertADragWithoutWriting() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(300, 300);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragMoved")).toBool(), true);

  eval(engine, QStringLiteral("window.__fireWin('pointercancel', { pointerId: 1 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
  // Snapped back to the persisted anchor.
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("40px"));

  // Esc takes the same route (the glue calls cancelFreeTextDrag directly).
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"
                              "window.__drag(300, 300);"
                              "window.vxcore.cancelFreeTextDrag();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
}

// A scroll / zoom / rotate during a drag repaints EVERY layer, which destroys
// the preview node. The drag point lives in page space precisely so the repaint
// re-projects it instead of losing it.
void TestPdfViewerCoreJs::aRepaintDuringADragKeepsThePreviewAtTheDraggedPoint() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(200, 150);"));

  eval(engine, QStringLiteral("window.vxcore.renderAllComments();"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === 'ft1'")).toBool());
  // (50, 750) in page space projects to (100, 100) at scale 2.
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("100px"));
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.top")).toString(),
           QStringLiteral("100px"));
  // ...and the preview node the drag holds is the one that is actually in the
  // layer, or a subsequent in-place move would update a detached node.
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragEl === window.__box('ft1')")).toBool());
}

// The grab offset is stored in PDF UNITS. In client pixels it would go stale
// the moment the user zoomed mid-drag, and the box would jump.
void TestPdfViewerCoreJs::aZoomDuringADragKeepsTheGrabOffset() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  // Grabbed at page-space (30, 770) on an anchor at (20, 780): offset (-10, +10).
  eval(engine, QStringLiteral("window.__press('ft1', 160, 110);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragGrabDx")).toNumber(), -10.0);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragGrabDy")).toNumber(), 10.0);

  eval(engine, QStringLiteral("window.__setScale(4);"));
  // At scale 4 the pointer at (300, 250) is page-space (50, 750).
  eval(engine, QStringLiteral("window.__drag(300, 250);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragPoint.x")).toNumber(), 40.0);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragPoint.y")).toNumber(), 760.0);
}

// The preview must be rendered into the TARGET page's layer with that page's
// viewport. Asserting only the CSS coordinates would pass for a node appended
// to the wrong layer.
void TestPdfViewerCoreJs::aDropOnAnotherPageCommitsThatPage() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__useTwoPages(2); window.__ready();"));
  QVERIFY(eval(engine, QStringLiteral("window.__boxPage('ft1') === window.__pageDiv")).toBool());

  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(200, 1000);"));

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragPoint.page")).toInt(), 1);
  QVERIFY2(eval(engine, QStringLiteral("window.__boxPage('ft1') === window.__pageDiv2")).toBool(),
           "the preview must live in the TARGET page's comment layer");

  eval(engine, QStringLiteral("window.__release(200, 1000);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].page")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].x")).toNumber(), 50.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].y")).toNumber(), 750.0);
}

// A drop in the gutter is CLAMPED into the source page, not refused: the user
// plainly meant "over there", and a silent no-op reads as a broken gesture.
void TestPdfViewerCoreJs::aDropOffAnyPageClampsIntoTheSourcePage() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__useTwoPages(2); window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(200, 150);"));
  // 870 is between page 0's bottom (850) and page 1's top (900).
  eval(engine, QStringLiteral("window.__release(200, 870);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].page")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].x")).toNumber(), 50.0);
  // Clamped to the page's bottom edge: (850 - 50) / 2 = 400 from the top.
  QCOMPARE(eval(engine, QStringLiteral("window.__moves[0].y")).toNumber(), 400.0);
}

// The controller refuses a same-point move, so no publish would ever come back
// to clear a pending preview. Nothing may be left waiting on one.
void TestPdfViewerCoreJs::aDragThatReturnsToItsStartSendsNothingAndClearsItsState() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(300, 300);"));
  eval(engine, QStringLiteral("window.__release(140, 90);"));

  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.pendingMoveId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("40px"));
}

// setComments() fires for EVERY mutation, including an unrelated dock edit and
// the inline editor's own debounced flush. Ending the drag on one would make
// dragging while anything else happens impossible.
void TestPdfViewerCoreJs::anUnrelatedCommentPublishDoesNotEndAnActiveDrag() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(200, 150);"));

  eval(engine, QStringLiteral(
                   "window.vxcore.setComments(["
                   "  { id: 'ft1', color: 'yellow', text: 'one',"
                   "    anchor: { type: 'pdf-freetext', page: 0, x: 20, y: 780, fontSize: 12 } },"
                   "  { id: 'ft2', color: 'blue', text: 'edited elsewhere',"
                   "    anchor: { type: 'pdf-freetext', page: 0, x: 200, y: 400,"
                   "              fontSize: 12 } }]);"));

  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === 'ft1'")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.dragPoint.x")).toNumber(), 50.0);
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("100px"));

  eval(engine, QStringLiteral("window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
}

void TestPdfViewerCoreJs::deletingTheDraggedCommentDuringADragCancelsIt() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
  eval(engine, QStringLiteral("window.__drag(200, 150);"));

  eval(engine, QStringLiteral("window.vxcore.setComments(["
                              "  { id: 'ft2', color: 'yellow', text: 'two',"
                              "    anchor: { type: 'pdf-freetext', page: 0, x: 200, y: 400,"
                              "              fontSize: 12 } }]);"));

  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);

  // A late pointerup from the abandoned gesture writes nothing.
  eval(engine, QStringLiteral("window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
}

// C++ is the source of truth: whatever the publish says wins, and the local
// preview is dropped either way.
void TestPdfViewerCoreJs::aPendingMoveIsClearedByTheMatchingPublishAndOverriddenByADifferentOne() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"
                              "window.__drag(200, 150);"
                              "window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.pendingMoveId")).toString(),
           QStringLiteral("ft1"));

  // The matching publish clears the preview and the box keeps its position.
  eval(engine, QStringLiteral("window.vxcore.setComments(["
                              "  { id: 'ft1', color: 'yellow', text: 'one',"
                              "    anchor: { type: 'pdf-freetext', page: 0, x: 50, y: 750,"
                              "              fontSize: 12 } }]);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.pendingMoveId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("100px"));

  // A publish carrying a DIFFERENT anchor wins over the local preview.
  eval(engine, QStringLiteral("window.__press('ft1', 200, 150);"
                              "window.__drag(300, 250);"
                              "window.__release(300, 250);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.pendingMoveId")).toString(),
           QStringLiteral("ft1"));
  eval(engine, QStringLiteral("window.vxcore.setComments(["
                              "  { id: 'ft1', color: 'yellow', text: 'one',"
                              "    anchor: { type: 'pdf-freetext', page: 0, x: 20, y: 780,"
                              "              fontSize: 12 } }]);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.pendingMoveId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("40px"));
}

// Three window listeners are registered per gesture. Leaking one set per drag
// would make every subsequent pointer move run N stale handlers.
void TestPdfViewerCoreJs::aSecondDragLeavesExactlyOneSetOfWindowListeners() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  for (int i = 0; i < 2; ++i) {
    eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"));
    QCOMPARE(eval(engine, QStringLiteral("window.__winCount('pointermove')")).toInt(), 1);
    QCOMPARE(eval(engine, QStringLiteral("window.__winCount('pointerup')")).toInt(), 1);
    QCOMPARE(eval(engine, QStringLiteral("window.__winCount('pointercancel')")).toInt(), 1);
    eval(engine, QStringLiteral("window.__drag(200, 150); window.__release(200, 150);"
                                "window.vxcore.pendingMoveId = null;"
                                "window.vxcore.pendingMovePoint = null;"
                                "window.__seedBoxes();"));
    QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);
  }
}

// suppressClickForId is an ID, not a global boolean: a drag released off the
// box produces no click at all, and a stuck flag would swallow the next genuine
// click on a DIFFERENT comment.
void TestPdfViewerCoreJs::aDragThatProducesNoClickDoesNotSuppressTheNextClick() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"
                              "window.__drag(200, 150);"
                              "window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.suppressClickForId")).toString(),
           QStringLiteral("ft1"));

  // A click on ANOTHER box is unaffected.
  eval(engine, QStringLiteral("window.__fire(window.__box('ft2'), 'click', {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__selected")), QStringLiteral("[\"ft2\"]"));

  // THE case: this drag produced no trailing click at all (released off the box,
  // or the node was replaced). The unconditional next-turn fallback must clear
  // the flag anyway, or the next genuine click on that box would be swallowed.
  QCOMPARE(eval(engine, QStringLiteral("window.__runTimeouts()")).toInt(), 1);
  QVERIFY2(eval(engine, QStringLiteral("window.vxcore.suppressClickForId === null")).toBool(),
           "a drag that produced no click must not leave the suppression armed");
  eval(engine, QStringLiteral("window.__selected = [];"
                              "window.__fire(window.__box('ft1'), 'click', {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__selected")), QStringLiteral("[\"ft1\"]"));

  // ...and when the trailing click DOES arrive, it is swallowed exactly once.
  eval(engine, QStringLiteral("window.__seedBoxes();"
                              "window.__press('ft1', 200, 150);"
                              "window.__drag(260, 210);"
                              "window.__release(260, 210);"
                              "window.__selected = [];"
                              "window.__fire(window.__box('ft1'), 'click', {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__selected")), QStringLiteral("[]"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.suppressClickForId === null")).toBool());

  eval(engine, QStringLiteral("window.__fire(window.__box('ft1'), 'click', {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__selected")), QStringLiteral("[\"ft1\"]"));
}

// A pending commit is NOT guaranteed a publish: the adapter can reject the move
// (stale page count, non-finite value) and the controller can refuse it
// (editability lost in the meantime). A cancel that cleared only the ACTIVE
// drag would leave the box drawn forever at a position that was never saved.
void TestPdfViewerCoreJs::aPendingMoveIsCancellableAndBlocksASecondDrag() {
  QJSEngine engine;
  loadCore(engine);
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());

  eval(engine, QStringLiteral("window.__ready();"));
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"
                              "window.__drag(200, 150);"
                              "window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.pendingMoveId")).toString(),
           QStringLiteral("ft1"));

  // A second drag on the still-unreconciled preview would take its origin and
  // grab offset from the STALE persisted anchor, so the box would jump.
  QCOMPARE(eval(engine,
                QStringLiteral("window.vxcore.beginFreeTextDrag('ft1',"
                               "  { clientX: 200, clientY: 150, button: 0, pointerId: 2 }, null)"))
               .toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);

  // Losing editability cancels the pending preview too, and writes nothing.
  eval(engine, QStringLiteral("window.vxcore.setCommentsEditable(false);"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.pendingMoveId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("40px"));
}

// Escape is routed by the SHIPPED glue, not by the core, so it has to be driven
// through pdfviewer.mjs's own listener. A core-only test would stay green while
// the production condition covered the wrong state.
void TestPdfViewerCoreJs::glueEscapeCancelsBothAnActiveDragAndAPendingMove() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);
  // The bridge block bails out early without a #viewerContainer, and the Escape
  // listener is registered AFTER it -- so the container stub is what makes this
  // case reach the handler at all.
  eval(engine,
       QStringLiteral("window.__container = { listeners: {},"
                      "  addEventListener: function(t, fn) { this.listeners[t] = fn; } };"
                      "document.getElementById = function() { return window.__container; };"));
  // The keydown listener is registered from initializedPromise, and the glue
  // prelude's recorder is what captures it.
  eval(engine, QStringLiteral("window.__initDeferred.resolve();"));
  QVERIFY2(eval(engine, QStringLiteral("typeof window.__winListeners['keydown'] === 'function'"))
               .toBool(),
           "pdfviewer.mjs must register the global Escape handler");

  // Now layer the page/DOM fakes on top (they replace window.addEventListener
  // with the array recorder the drag needs; the keydown callback above is
  // already captured).
  eval(engine, QStringLiteral("window.__escape = window.__winListeners['keydown'];"));
  QVERIFY(!eval(engine, QString::fromUtf8(c_toolHarness)).isError());
  QVERIFY(!eval(engine, QString::fromUtf8(c_dragHarness)).isError());
  eval(engine, QStringLiteral("window.__ready();"));

  // An ACTIVE drag: Escape reverts it with no bridge traffic.
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90); window.__drag(300, 300);"));
  eval(engine, QStringLiteral("window.__escape({ key: 'Escape',"
                              "  preventDefault: function() {},"
                              "  stopPropagation: function() {} });"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === null")).toBool());
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__armed()")).toInt(), 0);

  // A PENDING move: the request may have been rejected by the adapter or
  // refused by the controller, in which case no publish will ever reconcile it
  // and beginFreeTextDrag() refuses every further drag. Escape is the way out.
  eval(engine, QStringLiteral("window.__press('ft1', 140, 90);"
                              "window.__drag(200, 150);"
                              "window.__release(200, 150);"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.pendingMoveId")).toString(),
           QStringLiteral("ft1"));

  eval(engine, QStringLiteral("window.__escape({ key: 'Escape',"
                              "  preventDefault: function() {},"
                              "  stopPropagation: function() {} });"));
  QVERIFY2(eval(engine, QStringLiteral("window.vxcore.pendingMoveId === null")).toBool(),
           "Escape must clear a pending move, or free-text dragging stays disabled");
  QCOMPARE(eval(engine, QStringLiteral("window.__moves.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__box('ft1').style.left")).toString(),
           QStringLiteral("40px"));

  // ...and a new drag is possible again.
  QCOMPARE(eval(engine, QStringLiteral("window.__press('ft1', 140, 90)")).toInt(), 1);
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.dragCommentId === 'ft1'")).toBool());
}

// ============ The viewer-control bridge ============
//
// The back end of the native toolbar that replaces pdf.js's hidden built-in
// strip. Two classes of defect are gated here and are invisible from C++:
//
//   1. Listening to the COMMAND event ('switchscrollmode') instead of the STATE
//      event ('scrollmodechanged'), which ticks the toolbar from our own
//      request and misses the mode pdf.js forces internally.
//   2. Reaching for PDFViewerApplication.pdfSidebar, which does not exist in
//      v6 -- the pane belongs to `viewsManager` -- yielding a dead toggle.

void TestPdfViewerCoreJs::viewerBridgeRegistersItsListeners() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"));

  for (const auto &name :
       {QStringLiteral("pagechanging"), QStringLiteral("pagesloaded"),
        QStringLiteral("documentloaded"), QStringLiteral("scalechanging"),
        QStringLiteral("rotationchanging"), QStringLiteral("scrollmodechanged"),
        QStringLiteral("spreadmodechanged"), QStringLiteral("cursortoolchanged"),
        QStringLiteral("sidebarviewchanged"), QStringLiteral("updatefindmatchescount"),
        QStringLiteral("updatefindcontrolstate")}) {
    QVERIFY2(eval(engine, QStringLiteral("window.__app.eventBus.count('%1')").arg(name)).toInt() >
                 0,
             qPrintable(QStringLiteral("no viewer-bridge listener for %1").arg(name)));
  }
}

void TestPdfViewerCoreJs::eachViewerEventProducesExactlyOneStatePush() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.__adapter = window.__makeViewerAdapter();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.setViewerAdapter(window.__adapter);"));
  // setViewerAdapter() publishes once itself, so the rendezvous cannot lose the
  // state when the adapter arrives after 'documentloaded'.
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states.length")).toInt(), 1);

  eval(engine, QStringLiteral("window.__adapter.states = [];"));
  eval(engine, QStringLiteral("window.__app.eventBus.fire('pagechanging', { pageNumber: 7 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states.length")).toInt(), 1);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[0].page")).toInt(), 7);

  eval(engine,
       QStringLiteral(
           "window.__app.eventBus.fire('scalechanging', { scale: 1.5, presetValue: '1.5' });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states.length")).toInt(), 2);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[1].scaleValue")).toString(),
           QStringLiteral("1.5"));
  // The page is CARRIED, not reset: the state object is cumulative.
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[1].page")).toInt(), 7);

  // A zoom with no preset (Ctrl+wheel) still reports a usable value, or the
  // combo would show a stale "Automatic".
  eval(engine, QStringLiteral("window.__app.eventBus.fire('scalechanging', { scale: 1.37 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[2].scaleValue")).toString(),
           QStringLiteral("1.37"));

  eval(engine,
       QStringLiteral("window.__app.eventBus.fire('rotationchanging', { pagesRotation: 90 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[3].rotation")).toInt(), 90);

  eval(engine, QStringLiteral("window.__app.eventBus.fire('cursortoolchanged', { tool: 1 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[4].cursorTool")).toInt(), 1);

  // SidebarView.NONE === 0 means closed.
  eval(engine, QStringLiteral("window.__app.eventBus.fire('sidebarviewchanged', { view: 1 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[5].sidebarOpen")).toBool(), true);
  eval(engine, QStringLiteral("window.__app.eventBus.fire('sidebarviewchanged', { view: 0 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[6].sidebarOpen")).toBool(), false);

  // 'pagesloaded' snapshots everything at once: several fields change together
  // and there is no per-field event for them.
  eval(engine, QStringLiteral("window.__app.pagesCount = 42;"
                              "window.__app.pdfViewer.currentPageNumber = 3;"
                              "window.__app.eventBus.fire('pagesloaded', {});"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[7].pageCount")).toInt(), 42);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[7].page")).toInt(), 3);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states.length")).toInt(), 8);
}

// Fails against a bridge wired to 'switchscrollmode' / 'switchspreadmode' --
// the COMMANDS, not the state events. That version would tick the toolbar from
// our own request and never notice the scroll mode presentation mode forces.
void TestPdfViewerCoreJs::modeStateComesFromTheChangedEventNotTheSwitchCommand() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.__adapter = window.__makeViewerAdapter();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.setViewerAdapter(window.__adapter);"
                              "window.__adapter.states = [];"));

  // The COMMAND events must move nothing.
  eval(engine, QStringLiteral("window.__app.eventBus.fire('switchscrollmode', { mode: 3 });"
                              "window.__app.eventBus.fire('switchspreadmode', { mode: 2 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states.length")).toInt(), 0);

  // The STATE events are what the toolbar follows.
  eval(engine, QStringLiteral("window.__app.eventBus.fire('scrollmodechanged', { mode: 3 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[0].scrollMode")).toInt(), 3);
  eval(engine, QStringLiteral("window.__app.eventBus.fire('spreadmodechanged', { mode: 2 });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.states[1].spreadMode")).toInt(), 2);

  // ...and the commands go out under pdf.js's own names.
  eval(engine, QStringLiteral("window.vxcore.setScrollMode(1); window.vxcore.setSpreadMode(1);"
                              "window.vxcore.setCursorTool(1);"));
  QCOMPARE(json(engine, QStringLiteral("window.__dispatchedNames(window.__app)")),
           QStringLiteral("[\"switchscrollmode\",\"switchspreadmode\",\"switchcursortool\"]"));

  // Out-of-range modes are refused rather than forwarded.
  eval(engine, QStringLiteral("window.__app.__dispatched = [];"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setScrollMode(9)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setSpreadMode(-1)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setCursorTool('hand')")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__dispatched.length")).toInt(), 0);
}

// There is NO PDFViewerApplication.pdfSidebar in pdf.js v6. Reaching for it is
// not a compile error in JS -- it is a permanently dead toggle.
void TestPdfViewerCoreJs::toggleSidebarReachesViewsManager() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"));

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.toggleSidebar()")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__sidebarToggles")).toInt(), 1);
  // Nothing goes through the event bus for this one.
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__dispatched.length")).toInt(), 0);

  // An app without a viewsManager is an inert no-op, not a throw.
  eval(engine, QStringLiteral("delete window.__app.viewsManager;"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.toggleSidebar()")).toBool(), false);
}

void TestPdfViewerCoreJs::gotoPageClampsAndRejectsGarbage() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.__app.eventBus.fire('pagesloaded', {});"));

  eval(engine, QStringLiteral("window.vxcore.gotoPage(7);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app,"
                                       " 'pagenumberchanged').value"))
               .toString(),
           QStringLiteral("7"));

  // CLAMPED, not forwarded: the C++ spin box is bounded too, and the two caps
  // are independent by design.
  eval(engine, QStringLiteral("window.vxcore.gotoPage(9999);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app,"
                                       " 'pagenumberchanged').value"))
               .toString(),
           QStringLiteral("20"));
  eval(engine, QStringLiteral("window.vxcore.gotoPage(-4);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app,"
                                       " 'pagenumberchanged').value"))
               .toString(),
           QStringLiteral("1"));

  // Garbage is refused outright rather than clamped to 1: a NaN page is a bug
  // somewhere, not a user intent.
  eval(engine, QStringLiteral("window.__app.__dispatched = [];"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.gotoPage('seven')")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.gotoPage(undefined)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__dispatched.length")).toInt(), 0);
}

void TestPdfViewerCoreJs::setZoomAcceptsPresetsAndRefusesGarbage() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"));

  for (const auto &value :
       {QStringLiteral("auto"), QStringLiteral("page-actual"), QStringLiteral("page-fit"),
        QStringLiteral("page-width"), QStringLiteral("1.5")}) {
    QVERIFY2(eval(engine, QStringLiteral("window.vxcore.setZoom('%1')").arg(value)).toBool(),
             qPrintable(QStringLiteral("setZoom refused %1").arg(value)));
  }
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'scalechanged').value"))
               .toString(),
           QStringLiteral("1.5"));

  eval(engine, QStringLiteral("window.__app.__dispatched = [];"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('page-everything')")).toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('')")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom(-2)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__dispatched.length")).toInt(), 0);

  // The bounds are pdf.js's own MIN_SCALE / MAX_SCALE (0.1 / 25.0). Too tight a
  // ceiling is not harmless: it would reject a legitimate zoom and leave the
  // toolbar reporting a scale the document no longer has.
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('25')")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('0.1')")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('25.5')")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setZoom('0.05')")).toBool(), false);

  // The step verbs go out under pdf.js's own names.
  eval(engine, QStringLiteral("window.__app.__dispatched = [];"
                              "window.vxcore.zoomIn(); window.vxcore.zoomOut();"));
  QCOMPARE(json(engine, QStringLiteral("window.__dispatchedNames(window.__app)")),
           QStringLiteral("[\"zoomin\",\"zoomout\"]"));

  // Document properties goes through the event bus rather than by poking app
  // internals.
  eval(engine, QStringLiteral("window.__app.__dispatched = [];"
                              "window.vxcore.showDocumentProperties();"));
  QCOMPARE(json(engine, QStringLiteral("window.__dispatchedNames(window.__app)")),
           QStringLiteral("[\"documentproperties\"]"));

  // Two verbs that CANNOT be completed from inside the page, and so must not
  // come back as commands that cross the bridge and dead-end:
  //
  //   * printing -- pdf.js destroys its prepared #printContainer on a hardcoded
  //     20 ms timer (web/viewer.mjs:14052) that Qt's asynchronous print cannot
  //     be sequenced against, and QWebEngineView::print() alone cannot reach the
  //     print service at all (web/viewer.mjs:14134);
  //   * presentation mode -- Chromium refuses requestFullscreen() without
  //     transient renderer user activation, which a Qt QAction click relayed
  //     over QWebChannel does not carry.
  for (const auto &name : {QStringLiteral("printDocument"), QStringLiteral("startPrint"),
                           QStringLiteral("enterPresentationMode")}) {
    QVERIFY2(
        eval(engine, QStringLiteral("typeof window.vxcore.%1").arg(name)).toString() ==
            QStringLiteral("undefined"),
        qPrintable(
            QStringLiteral("%1() must not come back: it cannot work from the page").arg(name)));
  }

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setRotation(90)")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.pdfViewer.pagesRotation")).toInt(), 90);
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.setRotation(45)")).toBool(), false);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.pdfViewer.pagesRotation")).toInt(), 90);
}

// type:'' starts a NEW search; type:'again' is next/previous. Getting this
// wrong makes "find next" restart from the top forever.
void TestPdfViewerCoreJs::findDistinguishesANewQueryFromNextAndPrevious() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.__adapter = window.__makeViewerAdapter();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.setViewerAdapter(window.__adapter);"));

  eval(engine, QStringLiteral("window.vxcore.findText(['alpha'], {});"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QString());
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').query")).toString(),
      QStringLiteral("alpha"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').highlightAll"))
               .toBool(),
           true);

  // The SAME query again is next, not a restart.
  eval(engine, QStringLiteral("window.vxcore.findText(['alpha'], {});"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QStringLiteral("again"));

  eval(engine, QStringLiteral("window.vxcore.findText(['alpha'], { findBackward: true });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').findPrevious"))
               .toBool(),
           true);

  // A different query is a fresh search again.
  eval(engine, QStringLiteral("window.vxcore.findText(['beta'], "
                              "{ caseSensitive: true, wholeWordOnly: true });"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QString());
  // wholeWordOnly maps to entireWord -- pdf.js's spelling, not VNote's.
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').entireWord"))
               .toBool(),
           true);
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').caseSensitive"))
               .toBool(),
           true);
}

// pdf.js reports `current` 1-BASED; ViewWindow2::showFindResult expects a
// 0-based index and adds one for display. Forwarding it unconverted makes the
// first match read "2/N".
void TestPdfViewerCoreJs::findReportsTheFirstMatchAsIndexZero() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.__adapter = window.__makeViewerAdapter();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.setViewerAdapter(window.__adapter);"
                              "window.vxcore.findText(['alpha'], {});"));

  eval(engine, QStringLiteral("window.__app.eventBus.fire('updatefindmatchescount',"
                              " { matchesCount: { current: 1, total: 5 } });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds[0].index")).toInt(), 0);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds[0].total")).toInt(), 5);
  QCOMPARE(json(engine, QStringLiteral("window.__adapter.finds[0].texts")),
           QStringLiteral("[\"alpha\"]"));

  // No match at all: -1, not 0 -- which showFindResult would render as "1/0".
  eval(engine, QStringLiteral("window.__app.eventBus.fire('updatefindcontrolstate',"
                              " { matchesCount: { current: 0, total: 0 } });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds[1].index")).toInt(), -1);
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds[1].total")).toInt(), 0);
}

void TestPdfViewerCoreJs::anEmptyFindQueryClosesTheFindBar() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.__adapter = window.__makeViewerAdapter();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.setViewerAdapter(window.__adapter);"
                              "window.vxcore.findText(['alpha'], {});"
                              "window.__app.__dispatched = [];"
                              "window.__adapter.finds = [];"));

  eval(engine, QStringLiteral("window.vxcore.findText([''], {});"));
  QCOMPARE(json(engine, QStringLiteral("window.__dispatchedNames(window.__app)")),
           QStringLiteral("[\"findbarclose\"]"));
  QVERIFY(eval(engine, QStringLiteral("window.vxcore.findQuery === null")).toBool());
  // The counter is cleared too, or the find bar keeps showing the last count.
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds[0].index")).toInt(), -1);

  // With no retained query, a stray match report is ignored rather than
  // resurrecting a stale count.
  eval(engine, QStringLiteral("window.__adapter.finds = [];"
                              "window.__app.eventBus.fire('updatefindmatchescount',"
                              " { matchesCount: { current: 1, total: 3 } });"));
  QCOMPARE(eval(engine, QStringLiteral("window.__adapter.finds.length")).toInt(), 0);
}

// REFUSED, not silently searched literally: pdf.js's findController has no
// regular-expression mode, and quietly matching the pattern text would look
// like a broken search.
void TestPdfViewerCoreJs::aRegularExpressionFindIsRefused() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"));

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.findText(['a.*b'],"
                                       " { regularExpression: true })"))
               .toBool(),
           false);
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__dispatched.length")).toInt(), 0);
  QVERIFY2(eval(engine, QStringLiteral("window.__warnings.join('|')"))
               .toString()
               .contains(QStringLiteral("regular-expression")),
           "a refused regex find must say so, or it looks like nothing happened");
}

// The strip is hidden with CSS rather than removed from the DOM (viewer.mjs
// holds references to its children and would throw). A silent upstream rename
// would therefore just show it again; this is the tripwire.
//
// #viewsManager is checked too, and it is the SUBTLE one: in v6 the sidebar
// pane lives INSIDE #toolbarContainer, so the CSS hides the strip's chrome
// piece by piece rather than the container. A rename there leaves an invisible
// sidebar behind a live Toggle Sidebar button.
void TestPdfViewerCoreJs::theBuiltInToolbarGuardWarnsWhenTheContainerIsGone() {
  QJSEngine engine;
  loadCore(engine);

  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.checkBuiltInToolbar()")).toBool(), false);
  QVERIFY2(eval(engine, QStringLiteral("window.__warnings.join('|')"))
               .toString()
               .contains(QStringLiteral("toolbarContainer")),
           "a renamed #toolbarContainer must produce a log line, not silence");

  // Only the container back: the sidebar id is still reported.
  eval(engine, QStringLiteral("window.__warnings = [];"
                              "window.__domIds['toolbarContainer'] = { id: 'toolbarContainer' };"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.checkBuiltInToolbar()")).toBool(), false);
  QVERIFY2(eval(engine, QStringLiteral("window.__warnings.join('|')"))
               .toString()
               .contains(QStringLiteral("viewsManager")),
           "a renamed #viewsManager must warn too -- the sidebar is INSIDE the hidden strip");

  eval(engine, QStringLiteral("window.__warnings = [];"
                              "window.__domIds['viewsManager'] = { id: 'viewsManager' };"));
  QCOMPARE(eval(engine, QStringLiteral("window.vxcore.checkBuiltInToolbar()")).toBool(), true);
  QCOMPARE(eval(engine, QStringLiteral("window.__warnings.length")).toInt(), 0);
}

// The shipped CSS must NOT hide #toolbarContainer outright: in pdf.js v6 the
// sidebar (#viewsManager, with its own thumbnails/outline/attachments/layers
// selector row) is a DESCENDANT of it, so `display: none` there makes Toggle
// Sidebar open something invisible. Asserted against the real files, because
// this is a relationship between two of them.
void TestPdfViewerCoreJs::theSidebarIsNotInsideTheHiddenToolbarSubtree() {
  QString error;

  const QString html =
      readFile(webDir() + QStringLiteral("/pdf.js/web/pdf-viewer-template.html"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const QString css = readFile(webDir() + QStringLiteral("/pdf.js/pdfviewer.css"), &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  // The premise: the sidebar really is nested inside the strip. If pdf.js ever
  // moves it out, this whole constraint (and the CSS gymnastics it forces)
  // can be simplified -- so fail loudly rather than silently over-constraining.
  const int containerAt = html.indexOf(QStringLiteral("id=\"toolbarContainer\""));
  const int sidebarAt = html.indexOf(QStringLiteral("id=\"viewsManager\""));
  const int viewerAt = html.indexOf(QStringLiteral("id=\"viewerContainer\""));
  QVERIFY(containerAt > 0 && sidebarAt > containerAt && viewerAt > sidebarAt);

  QVERIFY2(!css.contains(QRegularExpression(
               QStringLiteral("#toolbarContainer\\s*\\{[^}]*display\\s*:\\s*none"))),
           "#toolbarContainer must not be display:none -- #viewsManager is inside it");
  QVERIFY2(!css.contains(QRegularExpression(
               QStringLiteral("#toolbarViewerLeft\\s*\\{[^}]*display\\s*:\\s*none"))),
           "#toolbarViewerLeft must not be display:none -- #viewsManager is inside it");
  // ...and the strip's chrome IS hidden, or the whole feature is a no-op.
  QVERIFY(css.contains(QStringLiteral("#toolbarViewerLeft > *:not(#viewsManager)")));
  QVERIFY(css.contains(QStringLiteral("#toolbarViewerMiddle")));
  QVERIFY(css.contains(QStringLiteral("#toolbarViewerRight")));
}

// Fails against a bridge that decides new-vs-again from the query TEXT alone.
// pdf.js's own dirty check compares the query and treats type:'again' as pure
// navigation, so toggling Case Sensitive on the same word would merely step
// through the matches computed with the OLD setting.
void TestPdfViewerCoreJs::changingAFindOptionStartsAFreshSearch() {
  QJSEngine engine;
  loadCore(engine);

  eval(engine, QStringLiteral("window.__app = window.__makeViewerApp();"
                              "window.vxcore.attachViewerBridge(window.__app);"
                              "window.vxcore.findText(['alpha'], {});"));

  eval(engine, QStringLiteral("window.vxcore.findText(['alpha'], { caseSensitive: true });"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QString());

  eval(engine, QStringLiteral("window.vxcore.findText(['alpha'], { caseSensitive: true });"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QStringLiteral("again"));

  eval(engine,
       QStringLiteral(
           "window.vxcore.findText(['alpha'], { caseSensitive: true, wholeWordOnly: true });"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QString());

  // findBackward is NAVIGATION, not a match-set change: it must stay 'again',
  // or Find Previous would restart the search from the top every time.
  eval(engine,
       QStringLiteral("window.vxcore.findText(['alpha'],"
                      " { caseSensitive: true, wholeWordOnly: true, findBackward: true });"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').type")).toString(),
      QStringLiteral("again"));
}

// The glue is where a missing connect silently disables a whole control: the
// QWebChannel callback connects every signal in order, so ONE typo throws and
// skips everything after it.
void TestPdfViewerCoreJs::glueWiresTheViewerControlSignals() {
  QJSEngine engine;
  loadCore(engine);
  loadGlue(engine);

  const QJSValue res =
      eval(engine, QStringLiteral("window.__channelCb("
                                  "{ objects: { vxAdapter: window.__fakeAdapter } })"));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  for (const auto &name :
       {QStringLiteral("page"), QStringLiteral("zoom"), QStringLiteral("zoomStep"),
        QStringLiteral("rotation"), QStringLiteral("scrollMode"), QStringLiteral("spreadMode"),
        QStringLiteral("cursorTool"), QStringLiteral("sidebar"),
        QStringLiteral("documentProperties"), QStringLiteral("find"),
        QStringLiteral("findCleared")}) {
    QVERIFY2(eval(engine,
                  QStringLiteral("typeof window.__viewerHandlers['%1'] === 'function'").arg(name))
                 .toBool(),
             qPrintable(QStringLiteral("glue did not connect %1").arg(name)));
  }

  // The glue prelude's app carries the outline fixture; give it the members the
  // viewer bridge reads before the deferred block runs.
  eval(engine, QStringLiteral("window.__app.pagesCount = 20;"
                              "window.__app.pdfViewer = { currentPageNumber: 1, currentScale: 1,"
                              "  currentScaleValue: 'auto', pagesRotation: 0, scrollMode: 0,"
                              "  spreadMode: 0 };"
                              "window.__app.__sidebarToggles = 0;"
                              "window.__app.viewsManager = { visibleView: 0,"
                              "  toggle: function() { window.__app.__sidebarToggles += 1; } };"));

  // The bridge and the tripwire are registered from initializedPromise, NOT
  // from the channel callback -- registering them there inverts the race and
  // can miss 'documentloaded' entirely.
  QCOMPARE(eval(engine, QStringLiteral("window.__app.eventBus.count('scrollmodechanged')")).toInt(),
           0);
  eval(engine, QStringLiteral("window.__initDeferred.resolve();"));
  QVERIFY(eval(engine, QStringLiteral("window.__app.eventBus.count('scrollmodechanged')")).toInt() >
          0);
  QVERIFY2(eval(engine, QStringLiteral("window.__warnings.join('|')"))
               .toString()
               .contains(QStringLiteral("toolbarContainer")),
           "checkBuiltInToolbar() must run from the glue's initializedPromise block");

  // ...and the adapter reaches the core, so a replayed page/zoom is not lost.
  eval(engine, QStringLiteral("window.__viewerHandlers['page'](5);"));
  QCOMPARE(eval(engine, QStringLiteral("window.__lastDispatch(window.__app,"
                                       " 'pagenumberchanged').value"))
               .toString(),
           QStringLiteral("5"));

  eval(engine, QStringLiteral("window.__viewerHandlers['sidebar']();"));
  QCOMPARE(eval(engine, QStringLiteral("window.__app.__sidebarToggles")).toInt(), 1);

  eval(engine, QStringLiteral("window.__app.__dispatched = [];"
                              "window.__viewerHandlers['find'](['alpha'], {});"));
  QCOMPARE(
      eval(engine, QStringLiteral("window.__lastDispatch(window.__app, 'find').query")).toString(),
      QStringLiteral("alpha"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestPdfViewerCoreJs)
#include "test_pdfviewercore_js.moc"
