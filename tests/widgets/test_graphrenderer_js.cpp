// SPDX-License-Identifier: LGPL-3.0-or-later
//
// test_graphrenderer_js.cpp
//
// The two properties of the read-mode graph render path that can hang or corrupt
// the viewer, gated at the JS level:
//
//   1. GraphRenderer calls finishWork() EXACTLY ONCE per pass, on every exit path.
//      This is a liveness requirement, not tidiness: MarkdownViewerCore queues
//      every incoming markdown update while numOfOngoingWorkers > 0, so a pass
//      that never calls finishWork() deadlocks the viewer for the rest of the
//      session, and one that calls it twice corrupts the counter the same way.
//
//   2. GraphCache invokes the renderer at most once per key, INCLUDING when every
//      request for that key arrives before the first result comes back. A plain
//      get-then-set LRU yields almost no hits in this path, because all 150 copies
//      of a diagram are dispatched before any of them completes.
//
// Follows the QJSEngine seam of test_markdownviewer_js.cpp: the REAL
// src/data/extra/web/js/{lrucache,graphcache,graphrenderer}.js are read from disk
// and evaluated unmodified; only their collaborators (VxWorker, Utils, document,
// setTimeout) are stubbed.

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QXmlStreamReader>
#include <QtTest>

#include <widgets/editors/graphvizhelper.h>

namespace tests {

namespace {

const char *c_graphvizSource = R"DOT(graph G {
  a -- b;
  b -- c;
  c -- a;
  a -- d;
  d -- e;
  e -- b;
})DOT";

QString webJsDir() {
#ifdef VNOTE_SRC_DIR
  return QStringLiteral(VNOTE_SRC_DIR) + QStringLiteral("/data/extra/web/js");
#else
  return QDir::currentPath() + QStringLiteral("/../../../src/data/extra/web/js");
#endif
}

// Host environment. QJSEngine gives no DOM, no timers and no window.
//
// setTimeout is queued rather than run, so a test can assert that the dispatch
// loop really did hand control back to the host (which is what gives the
// compositor a chance to paint) instead of finishing the whole pass in one task.
const char *c_prelude = R"JS(
var window = this;
window.__errors = [];
var console = {
  log: function(){}, info: function(){}, warn: function(){},
  error: function(){ window.__errors.push(Array.prototype.join.call(arguments, ' ')); }
};

// QJSEngine rejects `this` inside these stubs, so the listener table is held on
// window rather than on the document object itself.
window.__docListeners = {};
var document = {
  currentScript: { src: 'file:///web/js/graphrenderer.js' },
  hidden: false,
  addEventListener: function(p_type, p_cb) {
    var table = window.__docListeners;
    (table[p_type] = table[p_type] || []).push(p_cb);
  }
};
// Chromium throttles and then freezes timers in a hidden page. __hidePage()
// models the part that actually breaks a live pass: the pending macrotask
// continuation is dropped and never runs.
window.__hidePage = function() {
  document.hidden = true;
  var dropped = window.__timers.length;
  window.__timers = [];
  return dropped;
};
window.__showPage = function() {
  document.hidden = false;
  var cbs = window.__docListeners['visibilitychange'] || [];
  for (var i = 0; i < cbs.length; ++i) { cbs[i](); }
  return cbs.length;
};

window.__timers = [];
function setTimeout(p_cb) { window.__timers.push(p_cb); return window.__timers.length; }
// Run one macrotask generation. Returns how many callbacks fired.
window.__runTimers = function() {
  var due = window.__timers;
  window.__timers = [];
  for (var i = 0; i < due.length; ++i) { due[i](); }
  return due.length;
};

window.__finishWorkCalls = 0;
class VxWorker {
  constructor() { this.name = ''; this.vxcore = null; this.id = 1; }
  finishWork() { ++window.__finishWorkCalls; }
}

class Utils {
  static parentFolder(p_path) { return '/web/js'; }

  // Scripts are "loaded" only when the test says so, so the asynchronous
  // initialize() path can be driven deterministically. Note that loadScript's
  // onerror arm invokes the same callback, so a FAILED load is indistinguishable
  // from a successful one here - which is exactly the case that matters.
  static loadScripts(p_srcs, p_cb) { window.__scriptLoads.push(p_cb); }
}
window.__scriptLoads = [];

// A class declaration is lexically scoped to the script that contains it, so it
// is NOT visible to a later evaluate(). Publish the collaborators explicitly.
window.VxWorker = VxWorker;
window.Utils = Utils;
)JS";

// A GraphRenderer whose renderOne() completion is driven by the test.
const char *c_testRenderer = R"JS(
class TestRenderer extends GraphRenderer {
  constructor() {
    super();
    this.name = 'test';
    // Skip the dynamic-script machinery.
    this.initialized = true;

    // 'async' (default): completion is deferred until completeAll().
    // 'sync':  renderOne() completes before returning, like flowchart/wavedrom.
    // 'throw': renderOne() throws, like a renderer given malformed input.
    // 'reject': an async renderOne() whose promise rejects WITHOUT having called
    //           finishRenderingOne() - what Mermaid does when anything outside its
    //           own try block throws. This is the shape that used to hang the pass
    //           forever, and under batching also stopped every remaining batch.
    this.mode = 'async';

    this.renderCount = 0;
    this.inFlight = 0;
    this.maxInFlight = 0;
    this.completers = [];
  }

  renderOne(p_node, p_idx) {
    ++this.renderCount;
    ++this.inFlight;
    if (this.inFlight > this.maxInFlight) { this.maxInFlight = this.inFlight; }

    if (this.mode === 'sync') {
      --this.inFlight;
      this.finishRenderingOne();
      return true;
    }
    if (this.mode === 'throw') {
      --this.inFlight;
      throw new Error('renderOne exploded');
    }
    if (this.mode === 'reject') {
      --this.inFlight;
      return Promise.reject(new Error('renderOne rejected'));
    }


    this.completers.push(() => { --this.inFlight; this.finishRenderingOne(); });
    return true;
  }

  completeAll() {
    var due = this.completers;
    this.completers = [];
    for (var i = 0; i < due.length; ++i) { due[i](); }
    return due.length;
  }

  // Complete exactly one outstanding render, to observe whether the window
  // refills per completion (sliding) or only once it has fully drained (barrier).
  completeOne() {
    if (this.completers.length === 0) { return 0; }
    this.completers.shift()();
    return 1;
  }
}

// Models Mermaid/Graphviz: extra scripts to load, and an initialize() wrapper
// that dereferences the library they provide. When the load fails, that wrapper
// throws before ever reaching the callback that would arm the pass.
class FailingInitRenderer extends TestRenderer {
  constructor() {
    super();
    this.initialized = false;
    this.extraScripts = ['/web/js/some-library.js'];
  }

  initializeRenderer() {
    throw new ReferenceError('someLibrary is not defined');
  }
}

class QueuedInitRenderer extends TestRenderer {
  constructor() {
    super();
    this.initialized = false;
    this.extraScripts = ['/web/js/some-library.js'];
    this.libraryInitCount = 0;
    this.callbackCount = 0;
  }

  initializeRenderer() { ++this.libraryInitCount; }
}

class NoScriptFailingInitRenderer extends FailingInitRenderer {
  constructor() {
    super();
    this.extraScripts = [];
  }
}

class MissingLibraryRenderer extends QueuedInitRenderer {
  initializeRenderer() {
    if (typeof missingLibrary === 'undefined') {
      throw new Error('library is not available');
    }
  }
}

class MissingThemeRenderer extends QueuedInitRenderer {
  initializeRenderer() {
    window.WaveDrom = {RenderWaveForm: function() {}};
    if (typeof WaveSkin === 'undefined' || !Array.isArray(WaveSkin.default)) {
      throw new Error('theme is not available');
    }
  }
}

window.__startFailingInitPass = function(p_count) {
  window.__finishWorkCalls = 0;
  window.__timers = [];
  window.__scriptLoads = [];

  var r = new FailingInitRenderer();
  r.nodesToRender = [];
  for (var i = 0; i < p_count; ++i) { r.nodesToRender.push({ idx: i }); }
  r.numOfRenderedNodes = 0;
  window.__r = r;

  r.doRender();
  return r;
};

window.__queueInitializations = function(p_count) {
  window.__scriptLoads = [];
  var r = new QueuedInitRenderer();
  window.__r = r;
  for (var i = 0; i < p_count; ++i) {
    r.initialize(function() { ++r.callbackCount; });
  }
  return r;
};

window.__queueFailingInitializations = function(p_count) {
  window.__finishWorkCalls = 0;
  window.__scriptLoads = [];
  var r = new FailingInitRenderer();
  r.failureCount = 0;
  window.__r = r;
  r.nodesToRender = [{idx: 0}];
  r.doRender();
  for (var i = 0; i < p_count; ++i) {
    r.initialize(function() {}, function() { ++r.failureCount; });
  }
  return r;
};

window.__queueThrowingCallback = function() {
  window.__scriptLoads = [];
  var r = new QueuedInitRenderer();
  window.__r = r;
  r.initialize(function() { throw new Error('callback failed'); });
  r.initialize(function() { ++r.callbackCount; });
  return r;
};

window.__queueRejectingCallback = function() {
  window.__scriptLoads = [];
  var r = new QueuedInitRenderer();
  r.failureCount = 0;
  window.__r = r;
  r.initialize(function() { return Promise.reject(new Error('callback rejected')); },
               function() { ++r.failureCount; });
  r.initialize(function() { ++r.callbackCount; });
  return r;
};

window.__startNoScriptFailingInitPass = function() {
  window.__finishWorkCalls = 0;
  var r = new NoScriptFailingInitRenderer();
  r.nodesToRender = [{idx: 0}];
  window.__r = r;
  r.doRender();
  return r;
};

window.__retryAfterMissingLibrary = function() {
  window.__scriptLoads = [];
  var r = new MissingLibraryRenderer();
  r.failureCount = 0;
  window.__r = r;
  r.initialize(function() {}, function() { ++r.failureCount; });
  window.__scriptLoads.shift()();
  r.initialize(function() {}, function() { ++r.failureCount; });
  return r;
};

window.__retryAfterMissingTheme = function() {
  window.__scriptLoads = [];
  var r = new MissingThemeRenderer();
  r.failureCount = 0;
  window.__r = r;
  r.initialize(function() {}, function() { ++r.failureCount; });
  window.__scriptLoads.shift()();
  r.initialize(function() {}, function() { ++r.failureCount; });
  return r;
};

// @p_count nodes, concurrency @p_limit, completion @p_mode.
window.__startPass = function(p_count, p_limit, p_mode) {
  window.__finishWorkCalls = 0;
  window.__timers = [];

  var r = new TestRenderer();
  r.concurrencyLimit = p_limit;
  r.mode = p_mode || 'async';
  r.nodesToRender = [];
  for (var i = 0; i < p_count; ++i) { r.nodesToRender.push({ idx: i }); }
  r.numOfRenderedNodes = 0;
  window.__r = r;

  r.doRender();
  return r;
};

// Drive the pass to completion, alternating completions and macrotask
// generations. Returns the number of macrotask generations consumed, or -1 if it
// failed to terminate (which is the deadlock this test exists to catch).
window.__drivePass = function() {
  var generations = 0;
  for (var i = 0; i < 1000; ++i) {
    window.__r.completeAll();
    if (window.__finishWorkCalls > 0 && window.__timers.length === 0) {
      return generations;
    }
    if (window.__runTimers() === 0) {
      return window.__finishWorkCalls > 0 ? generations : -1;
    }
    ++generations;
  }
  return -1;
};
)JS";

// Cache harness. p_compute() hands back a Promise the test resolves by hand, so
// that "every request arrives before the first result" is exact rather than
// approximate.
const char *c_cacheHarness = R"JS(
window.__cache = new GraphCache();
window.__computeCount = 0;
window.__resolvers = [];
window.__settled = [];

window.__request = function(p_key) {
  window.__cache.request(p_key, function() {
    ++window.__computeCount;
    return new Promise(function(p_res, p_rej) {
      window.__resolvers.push({ key: p_key, resolve: p_res, reject: p_rej });
    });
  }).then(function(p_v) { window.__settled.push(p_v); },
          function() { window.__settled.push('ERR'); });
};

window.__key = function(p_src) {
  return window.__cache.generateKey(['test', 'svg', 'default', p_src]);
};

window.__resolveAll = function(p_value) {
  var due = window.__resolvers;
  window.__resolvers = [];
  for (var i = 0; i < due.length; ++i) { due[i].resolve(p_value + due[i].key.length); }
  return due.length;
};

window.__rejectAll = function() {
  var due = window.__resolvers;
  window.__resolvers = [];
  for (var i = 0; i < due.length; ++i) { due[i].reject(new Error('nope')); }
  return due.length;
};
)JS";

} // namespace

class TestGraphRendererJs : public QObject {
  Q_OBJECT

private slots:
  void testGraphvizEngines_data();
  void testGraphvizEngines();
  void testGraphvizNativeEngineCache();

  void testPlantUmlPages_data();
  void testPlantUmlPages();

  // finishWork() exactly-once, over every exit path.
  void testPass_emptyNodeListFinishesExactlyOnce();
  void testPass_boundedDispatchFinishesExactlyOnce();
  void testPass_unboundedDispatchFinishesExactlyOnce();
  void testPass_synchronousRendererYieldsBetweenBatches();
  void testPass_throwingRenderOneStillFinishes();
  void testPass_rejectedAsyncRenderOneStillFinishes();
  void testPass_resetWhileLiveIsReported();
  void testPass_frozenWhileHiddenResumesWhenShown();
  void testPass_surplusCompletionDoesNotFinishTwice();

  // The bound itself.
  void testConcurrency_neverExceedsTheLimit();
  void testConcurrency_zeroMeansUnbounded();
  void testConcurrency_windowRefillsPerCompletion();

  // Initialization and diagnostics must not be able to cost the finish.
  void testInit_failureInAsyncInitializeStillFinishes();
  void testInit_concurrentCallersShareLoadAndAllResume();
  void testInit_failureSettlesAllCallersAndRemainsUninitialized();
  void testInit_throwingCallbackDoesNotSuppressLaterCallbacks();
  void testInit_rejectingCallbackSettlesFailure();
  void testInit_noScriptFailureStillFinishes();
  void testInit_missingLibraryRemainsRetryable();
  void testInit_partialDependencyRemainsRetryable();
  void testTiming_reportFailureDoesNotBlockFinish();

  // GraphCache.
  void testKey_isUnambiguous();
  void testLruCache_boundsBothEntryCountAndWeight();
  void testCache_coalescesRequestsThatAllArriveBeforeTheFirstResult();
  void testCache_servesCompletedEntriesWithoutRecomputing();
  void testCache_failureFansOutAndAllowsRetry();

private:
  void setupRenderer(QJSEngine &p_engine);
  void setupCache(QJSEngine &p_engine);
  void evalFile(QJSEngine &p_engine, const QString &p_name, const QString &p_exportedClass);
  static void run(QJSEngine &p_engine, const QString &p_js);
  static int intOf(QJSEngine &p_engine, const QString &p_js);

  // QJSEngine does not drain the promise job queue when evaluate() returns: V4
  // posts its jobs to the engine object, so they only run once the event loop
  // gets a turn. Every assertion on a .then() handler needs this first.
  static void drainMicrotasks();
};

void TestGraphRendererJs::drainMicrotasks() {
  for (int i = 0; i < 8; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCoreApplication::sendPostedEvents();
  }
}

void TestGraphRendererJs::evalFile(QJSEngine &p_engine, const QString &p_name,
                                   const QString &p_exportedClass) {
  QFile f(webJsDir() + QLatin1Char('/') + p_name);
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
           qPrintable(QStringLiteral("cannot open %1").arg(p_name)));
  // The class the file declares has to be republished on the global object, or
  // the next evaluate() cannot see it.
  const QString src =
      QString::fromUtf8(f.readAll()) + QStringLiteral("\n;this.%1 = %1;\n").arg(p_exportedClass);
  auto res = p_engine.evaluate(src, p_name);
  QVERIFY2(!res.isError(), qPrintable(p_name + QStringLiteral(": ") + res.toString()));
}

void TestGraphRendererJs::run(QJSEngine &p_engine, const QString &p_js) {
  auto res = p_engine.evaluate(p_js);
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

int TestGraphRendererJs::intOf(QJSEngine &p_engine, const QString &p_js) {
  auto res = p_engine.evaluate(p_js);
  if (res.isError()) {
    qWarning() << res.toString();
    return -9999;
  }
  return res.toInt();
}

void TestGraphRendererJs::setupRenderer(QJSEngine &p_engine) {
  auto res = p_engine.evaluate(QString::fromUtf8(c_prelude));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));

  evalFile(p_engine, QStringLiteral("lrucache.js"), QStringLiteral("LruCache"));
  evalFile(p_engine, QStringLiteral("graphcache.js"), QStringLiteral("GraphCache"));
  evalFile(p_engine, QStringLiteral("graphrenderer.js"), QStringLiteral("GraphRenderer"));

  // GraphRenderer funnels every contract violation and render failure through
  // reportProblem(), which logs at INFO level on purpose: WebPage only forwards
  // InfoMessageLevel, so a diagnostic written with console.error never reaches
  // vnote.log. Capture the funnel rather than a console level, so these
  // assertions keep meaning what they say if the level ever changes again.
  auto hook = p_engine.evaluate(
      QStringLiteral("GraphRenderer.prototype.reportProblem = function() {"
                     "  window.__errors.push(Array.prototype.join.call(arguments, ' '));"
                     "};"));
  QVERIFY2(!hook.isError(), qPrintable(hook.toString()));

  res = p_engine.evaluate(QString::fromUtf8(c_testRenderer));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

void TestGraphRendererJs::setupCache(QJSEngine &p_engine) {
  setupRenderer(p_engine);
  auto res = p_engine.evaluate(QString::fromUtf8(c_cacheHarness));
  QVERIFY2(!res.isError(), qPrintable(res.toString()));
}

void TestGraphRendererJs::testPass_emptyNodeListFinishesExactlyOnce() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(0, 4, 'async')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__timers.length")), 0);
}

void TestGraphRendererJs::testPass_boundedDispatchFinishesExactlyOnce() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(10, 3, 'async')"));
  // Only the first batch has been launched; nothing has finished.
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 3);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 0);

  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 10);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testPass_unboundedDispatchFinishesExactlyOnce() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(10, 0, 'async')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 10);

  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testPass_synchronousRendererYieldsBetweenBatches() {
  QJSEngine engine;
  setupRenderer(engine);

  // A renderer that completes inside renderOne() is the worst case: without the
  // batch bound the entire document renders in one task and nothing is painted
  // until it ends. The pass must therefore consume more than one macrotask.
  run(engine, QStringLiteral("window.__startPass(20, 4, 'sync')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 4);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__timers.length")), 1);

  const int generations = intOf(engine, QStringLiteral("window.__drivePass()"));
  QVERIFY2(generations >= 4, qPrintable(QStringLiteral("generations=%1").arg(generations)));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 20);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testPass_throwingRenderOneStillFinishes() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(7, 3, 'throw')"));
  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 7);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 7);
}

void TestGraphRendererJs::testPass_rejectedAsyncRenderOneStillFinishes() {
  QJSEngine engine;
  setupRenderer(engine);

  // An async renderOne() signals failure by rejecting, which the synchronous
  // try/catch in dispatchBatch() never sees. Before the rejection arm existed
  // this pass never completed, and - because the batch never drained - it also
  // stopped every remaining batch.
  run(engine, QStringLiteral("window.__startPass(7, 3, 'reject')"));

  for (int i = 0; i < 40 && intOf(engine, QStringLiteral("window.__finishWorkCalls")) == 0; ++i) {
    drainMicrotasks();
    run(engine, QStringLiteral("window.__runTimers()"));
  }

  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 7);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 7);
}

void TestGraphRendererJs::testPass_resetWhileLiveIsReported() {
  QJSEngine engine;
  setupRenderer(engine);

  // reset() runs from the basicMarkdownRendered handler at the start of the next
  // round. Reaching it with a live pass means the serialisation invariant in
  // MarkdownViewerCore.setMarkdownText() was violated; clearing the state here is
  // exactly what would otherwise hide that, so it must be reported.
  run(engine, QStringLiteral("window.__startPass(5, 0, 'async')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 0);

  run(engine, QStringLiteral("window.__r.reset()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);

  // The retired pass must not be able to complete the counter behind reset()'s
  // back, in either direction.
  run(engine, QStringLiteral("window.__r.completeAll()"));
  drainMicrotasks();
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 0);
}

void TestGraphRendererJs::testPass_frozenWhileHiddenResumesWhenShown() {
  QJSEngine engine;
  setupRenderer(engine);

  // Switching to edit mode hides the viewer, and Chromium throttles then
  // freezes timers in a hidden page. A pass that is live at that moment loses
  // the macrotask continuation scheduled by scheduleNextBatch() and freezes:
  // nextBatchScheduled latched true, nothing in flight, nodes undispatched,
  // finishWork() never called. Observed in the field as a read-mode Mermaid
  // pass that stopped at 72 of 200 nodes and never recovered.
  run(engine, QStringLiteral("window.__startPass(20, 4, 'async')"));
  run(engine, QStringLiteral("window.__r.completeAll()"));
  drainMicrotasks();

  // A continuation is pending at this point; hiding the page drops it.
  QVERIFY(intOf(engine, QStringLiteral("window.__hidePage()")) > 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__timers.length")), 0);
  QVERIFY(intOf(engine, QStringLiteral("window.__r.nextBatchScheduled ? 1 : 0")) == 1);

  const int dispatched = intOf(engine, QStringLiteral("window.__r.nextNodeIndex"));
  QVERIFY2(dispatched < 20, qPrintable(QStringLiteral("dispatched=%1").arg(dispatched)));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 0);

  // Frozen: no timer can revive it, which is what made this invisible.
  QCOMPARE(intOf(engine, QStringLiteral("window.__runTimers()")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.nextNodeIndex")), dispatched);

  // Showing the page again must clear the latch and resume dispatching.
  QCOMPARE(intOf(engine, QStringLiteral("window.__showPage()")), 1);
  QVERIFY(intOf(engine, QStringLiteral("window.__r.nextNodeIndex")) > dispatched);

  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 20);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testPass_surplusCompletionDoesNotFinishTwice() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(5, 0, 'async')"));
  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);

  // A renderer that reports one node twice must not decrement
  // numOfOngoingWorkers a second time.
  run(engine, QStringLiteral("window.__r.finishRenderingOne();"
                             "window.__r.finishRenderingOne();"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testConcurrency_neverExceedsTheLimit() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(50, 4, 'async')"));
  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 50);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.maxInFlight")), 4);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testConcurrency_zeroMeansUnbounded() {
  QJSEngine engine;
  setupRenderer(engine);

  // The I/O-bound renderers depend on this: throttling them would be a large
  // regression, since they land their results concurrently.
  run(engine, QStringLiteral("window.__startPass(50, 0, 'async')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.maxInFlight")), 50);
}

void TestGraphRendererJs::testConcurrency_windowRefillsPerCompletion() {
  QJSEngine engine;
  setupRenderer(engine);

  // Sliding window, not a barrier. Completing ONE of four outstanding renders
  // must free exactly one slot; requiring the whole window to drain first would
  // make every batch cost its slowest member, which for the I/O-bound renderers
  // serializes round trips that used to overlap.
  run(engine, QStringLiteral("window.__startPass(20, 4, 'async')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 4);

  QCOMPARE(intOf(engine, QStringLiteral("window.__r.completeOne()")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__runTimers()")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 5);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.inFlight")), 4);

  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 20);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.maxInFlight")), 4);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
}

void TestGraphRendererJs::testInit_failureInAsyncInitializeStillFinishes() {
  QJSEngine engine;
  setupRenderer(engine);

  // doRender() returned without finishing, having handed control to an async
  // initialize(). The subclass wrapper then throws, because the library it
  // dereferences failed to load. Nothing downstream is armed to notice, so the
  // debt has to be released here or the viewer deadlocks for the session.
  run(engine, QStringLiteral("window.__startFailingInitPass(5)"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__scriptLoads.length")), 1);

  run(engine, QStringLiteral("window.__scriptLoads[0]()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.renderCount")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);
}

void TestGraphRendererJs::testInit_concurrentCallersShareLoadAndAllResume() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__queueInitializations(7)"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__scriptLoads.length")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.callbackCount")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.libraryInitCount")), 0);

  run(engine, QStringLiteral("window.__scriptLoads[0]()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.callbackCount")), 7);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.libraryInitCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initialized ? 1 : 0")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initializing ? 1 : 0")), 0);
}

void TestGraphRendererJs::testInit_failureSettlesAllCallersAndRemainsUninitialized() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__queueFailingInitializations(7)"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__scriptLoads.length")), 1);
  run(engine, QStringLiteral("window.__scriptLoads[0]()"));

  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.failureCount")), 7);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initialized ? 1 : 0")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initializing ? 1 : 0")), 0);
}

void TestGraphRendererJs::testInit_throwingCallbackDoesNotSuppressLaterCallbacks() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__queueThrowingCallback()"));
  run(engine, QStringLiteral("window.__scriptLoads[0]()"));

  QCOMPARE(intOf(engine, QStringLiteral("window.__r.callbackCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);
}

void TestGraphRendererJs::testInit_rejectingCallbackSettlesFailure() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__queueRejectingCallback()"));
  run(engine, QStringLiteral("window.__scriptLoads[0]()"));
  drainMicrotasks();

  QCOMPARE(intOf(engine, QStringLiteral("window.__r.callbackCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.failureCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);
}

void TestGraphRendererJs::testInit_noScriptFailureStillFinishes() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startNoScriptFailingInitPass()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initialized ? 1 : 0")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);
}

void TestGraphRendererJs::testInit_missingLibraryRemainsRetryable() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__retryAfterMissingLibrary()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.failureCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initialized ? 1 : 0")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__scriptLoads.length")), 1);
}

void TestGraphRendererJs::testInit_partialDependencyRemainsRetryable() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__retryAfterMissingTheme()"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.failureCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__r.initialized ? 1 : 0")), 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__scriptLoads.length")), 1);
}

void TestGraphRendererJs::testTiming_reportFailureDoesNotBlockFinish() {
  QJSEngine engine;
  setupRenderer(engine);

  run(engine, QStringLiteral("window.__startPass(3, 0, 'async')"));

  // completePass() calls reportTiming() before finishWork(), and reportTiming()
  // reaches into graphCache. A half-updated %APPDATA%/web/js can leave an older
  // GraphCache there with no statsString(); diagnostics must never be able to
  // cost the finish.
  run(engine, QStringLiteral("window.__r.graphCache = {"
                             "  statsString: function() { throw new Error('stale cache'); }"
                             "};"));

  QVERIFY(intOf(engine, QStringLiteral("window.__drivePass()")) >= 0);
  QCOMPARE(intOf(engine, QStringLiteral("window.__finishWorkCalls")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__errors.length")), 1);
}

void TestGraphRendererJs::testKey_isUnambiguous() {
  QJSEngine engine;
  setupCache(engine);

  // The previous bare concatenation made these two the same key.
  run(engine, QStringLiteral("window.__k1 = window.__cache.generateKey(['ab', 'c']);"
                             "window.__k2 = window.__cache.generateKey(['a', 'bc']);"));
  QVERIFY(!engine.evaluate(QStringLiteral("window.__k1 === window.__k2")).toBool());

  // Identical source, different format => different key.
  run(engine, QStringLiteral("window.__k3 = window.__cache.generateKey(['t', 'svg', 'S']);"
                             "window.__k4 = window.__cache.generateKey(['t', 'png', 'S']);"));
  QVERIFY(!engine.evaluate(QStringLiteral("window.__k3 === window.__k4")).toBool());

  // Same components => same key.
  run(engine, QStringLiteral("window.__k5 = window.__cache.generateKey(['t', 'svg', 'S']);"));
  QVERIFY(engine.evaluate(QStringLiteral("window.__k3 === window.__k5")).toBool());
}

void TestGraphRendererJs::testLruCache_boundsBothEntryCountAndWeight() {
  QJSEngine engine;
  setupRenderer(engine);

  // Entry count alone is a useless bound for rendered SVGs: 256 diagrams says
  // nothing about how many megabytes are pinned.
  run(engine, QStringLiteral("window.__lru = new LruCache(100, 10,"
                             "  function(p_k, p_v) { return p_v.length; });"
                             "window.__lru.set('a', '12345');"
                             "window.__lru.set('b', '12345');"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__lru.cache.size")), 2);
  QCOMPARE(intOf(engine, QStringLiteral("window.__lru.weight")), 10);

  run(engine, QStringLiteral("window.__lru.set('c', '12345')"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__lru.cache.size")), 2);
  QCOMPARE(intOf(engine, QStringLiteral("window.__lru.weight")), 10);
  QVERIFY(engine.evaluate(QStringLiteral("window.__lru.get('a') === undefined")).toBool());
  QVERIFY(engine.evaluate(QStringLiteral("window.__lru.get('c') === '12345'")).toBool());

  // The entry-count bound still works on its own, and the old `size == capacity`
  // equality could not recover from an overshoot; a loop can.
  run(engine, QStringLiteral("window.__lru2 = new LruCache(2);"
                             "window.__lru2.set('a', 1);"
                             "window.__lru2.set('b', 2);"
                             "window.__lru2.set('c', 3);"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__lru2.cache.size")), 2);
  QVERIFY(engine.evaluate(QStringLiteral("window.__lru2.get('a') === undefined")).toBool());
}

void TestGraphRendererJs::testCache_coalescesRequestsThatAllArriveBeforeTheFirstResult() {
  QJSEngine engine;
  setupCache(engine);

  // 300 blocks over 10 distinct sources, every one of them dispatched before any
  // result comes back - exactly the graphs-duplicates.md shape.
  run(engine, QStringLiteral("for (var i = 0; i < 300; ++i) {"
                             "  window.__request(window.__key('src' + (i % 10)));"
                             "}"));

  QCOMPARE(intOf(engine, QStringLiteral("window.__computeCount")), 10);
  QCOMPARE(intOf(engine, QStringLiteral("window.__cache.stats.invocations")), 10);
  QCOMPARE(intOf(engine, QStringLiteral("window.__cache.stats.misses")), 10);
  QCOMPARE(intOf(engine, QStringLiteral("window.__cache.stats.joins")), 290);
  QCOMPARE(intOf(engine, QStringLiteral("window.__cache.stats.hits")), 0);

  // Every waiter must be served, exactly once.
  QCOMPARE(intOf(engine, QStringLiteral("window.__resolveAll('svg')")), 10);
  drainMicrotasks();
  QCOMPARE(intOf(engine, QStringLiteral("window.__settled.length")), 300);
}

void TestGraphRendererJs::testCache_servesCompletedEntriesWithoutRecomputing() {
  QJSEngine engine;
  setupCache(engine);

  run(engine, QStringLiteral("window.__request(window.__key('only'))"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__resolveAll('svg')")), 1);
  drainMicrotasks();
  QCOMPARE(intOf(engine, QStringLiteral("window.__settled.length")), 1);

  // Now that the entry is complete, later requests are hits, not misses.
  run(engine, QStringLiteral("for (var i = 0; i < 5; ++i) {"
                             "  window.__request(window.__key('only'));"
                             "}"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__computeCount")), 1);
  QCOMPARE(intOf(engine, QStringLiteral("window.__cache.stats.hits")), 5);
}

void TestGraphRendererJs::testCache_failureFansOutAndAllowsRetry() {
  QJSEngine engine;
  setupCache(engine);

  run(engine, QStringLiteral("for (var i = 0; i < 4; ++i) {"
                             "  window.__request(window.__key('bad'));"
                             "}"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__computeCount")), 1);

  QCOMPARE(intOf(engine, QStringLiteral("window.__rejectAll()")), 1);
  drainMicrotasks();
  // Every waiter completes, exactly once, and all of them see the failure.
  QCOMPARE(intOf(engine, QStringLiteral("window.__settled.length")), 4);
  QVERIFY(engine
              .evaluate(QStringLiteral("window.__settled.every(function(v){"
                                       "  return v === 'ERR'; })"))
              .toBool());

  // A failure is not cached: the next request retries.
  run(engine, QStringLiteral("window.__request(window.__key('bad'))"));
  QCOMPARE(intOf(engine, QStringLiteral("window.__computeCount")), 2);
}

void TestGraphRendererJs::testPlantUmlPages_data() {
  QTest::addColumn<QString>("scenario");
  QTest::addColumn<int>("pages");
  QTest::addColumn<bool>("incomplete");
  QTest::newRow("empty-400-end") << QStringLiteral("400") << 2 << false;
  QTest::newRow("legacy-index-crash-end") << QStringLiteral("509-index") << 2 << false;
  QTest::newRow("unrelated-crash-retains-pages") << QStringLiteral("509-other") << 2 << true;
  QTest::newRow("non-sequence-remains-single-page") << QStringLiteral("class") << 1 << false;
  QTest::newRow("identical-sequence-pages-are-not-end")
      << QStringLiteral("identical") << 2 << false;
  QTest::newRow("ignored-index-is-bounded") << QStringLiteral("cap") << 256 << true;
  QTest::newRow("preview-combines-page-pixels") << QStringLiteral("preview") << 2 << false;
  QTest::newRow("protected-preview-never-renders") << QStringLiteral("protected") << 0 << false;
}

void TestGraphRendererJs::testPlantUmlPages() {
  QFETCH(QString, scenario);
  QFETCH(int, pages);
  QFETCH(bool, incomplete);
  // QJSEngine rejects async/await. Use the viewer's actual Chromium engine,
  // with only the external renderer response boundary replaced by fixtures.
  QString source = QStringLiteral(R"JS(
(async function() {
try {
window.vxOptions = { protectedView: false };
let completed = 0;
let onFinished = null;
let renderer;
window.vxcore = {
  registerWorker(worker) { renderer = worker; worker.vxcore = this; },
  on() {},
  finishWorker() { ++completed; if (onFinished) onFinished(); }
};
)JS");
  for (const auto *name : {"utils.js", "lrucache.js", "vxworker.js", "graphrenderer.js",
                           "imageviewer.js", "plantuml.js"}) {
    QFile file(webJsDir() + QLatin1Char('/') + QLatin1String(name));
    QVERIFY(file.open(QIODevice::ReadOnly));
    source += QString::fromUtf8(file.readAll()) + QLatin1Char('\n');
  }
  const QJsonObject options{{QStringLiteral("scenario"), scenario}};
  source += QStringLiteral("const scenario = %1.scenario;\n")
                .arg(QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
  source += QStringLiteral(R"JS(
renderer.initialized = true;
renderer.getPlantUMLOnlineUrl = (server, format, text, index) => format + '/' + index;
const svg = (text) => '<svg xmlns="http://www.w3.org/2000/svg" data-diagram-type="'
  + (scenario === 'class' ? 'CLASS' : 'SEQUENCE') + '"><text>' + text + '</text></svg>';
const pngs = [[2, 2, 'red'], [3, 1, 'blue']].map(([width, height, color]) => {
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const context = canvas.getContext('2d');
  context.fillStyle = color; context.fillRect(0, 0, width, height);
  const bytes = atob(canvas.toDataURL('image/png').split(',')[1]);
  return new Blob([Uint8Array.from(bytes, c => c.charCodeAt(0))], {type: 'image/png'});
});
let requests = 0;
Utils.httpGet = (url, type, callback) => {
  ++requests;
  const [format, indexText] = url.split('/');
  const index = Number(indexText);
  let status = 200;
  let mime = format === 'svg' ? 'image/svg+xml' : 'image/png';
  let data = format === 'svg' ? svg(index === 0 || scenario === 'identical' ? 'FIRST' : 'SECOND')
                              : pngs[index];
  if (scenario === 'class' && index > 0) {
    throw new Error('Non-sequence diagram must not enumerate a broken server');
  }
  if (index >= 2 && scenario !== 'cap') {
    status = scenario.startsWith('509') ? 509 : 400;
    mime = status === 400 ? 'text/html' : 'image/svg+xml';
    data = status === 400 ? '' : svg(scenario === '509-index'
      ? 'java.lang.IndexOutOfBoundsException: Index 1 out of bounds for length 1\n'
        + 'net.sourceforge.plantuml.sequencediagram.SequenceDiagram.getTitle(SequenceDiagram.java:113)'
      : 'java.lang.OutOfMemoryError');
  }
  callback(data, {status, getResponseHeader: () => mime});
};
if (scenario === 'preview' || scenario === 'protected') {
  vxOptions.protectedView = scenario === 'protected';
  const result = await new Promise(resolve => renderer.renderText('source', (format, data) => resolve({format, data})));
  if (scenario === 'protected') {
    window.__plantResult = JSON.stringify({requests, empty: result.data === ''});
  } else {
    const image = new Image();
    await new Promise((resolve, reject) => {
      image.onload = resolve;
      image.onerror = () => reject(new Error('Preview is not a decodable PNG'));
      image.src = 'data:image/png;base64,' + result.data;
    });
    const canvas = document.createElement('canvas');
    canvas.width = image.naturalWidth; canvas.height = image.naturalHeight;
    const context = canvas.getContext('2d'); context.drawImage(image, 0, 0);
    const pixel = (x, y) => Array.from(context.getImageData(x, y, 1, 1).data).join(',');
    window.__plantResult = JSON.stringify({format: result.format, width: canvas.width,
      height: canvas.height, first: pixel(1, 1), second: pixel(2, 2)});
  }
} else {
  const node = document.createElement('code'); node.textContent = 'source';
  document.body.appendChild(node);
  await new Promise(resolve => {
    onFinished = resolve;
    renderer.nodesToRender = [node]; renderer.renderNodes();
  });
  await new Promise(resolve => setTimeout(resolve, 0));
  window.__plantResult = JSON.stringify({completed, active: renderer.passActive,
    pages: document.querySelectorAll('.vx-plantuml-page').length,
    labels: Array.from(document.querySelectorAll('.vx-plantuml-page text'), n => n.textContent),
    incomplete: !!document.querySelector('[role=alert]')});
}
} catch(error) { window.__plantResult = JSON.stringify({error: String(error) + '\n' + error.stack}); }
})();
)JS");
  const QJsonObject scriptData{{QStringLiteral("source"), source}};
  const auto execute =
      QStringLiteral(R"JS(
var script = document.createElement('script');
script.textContent = %1.source;
document.head.appendChild(script);
script.remove();
)JS")
          .arg(QString::fromUtf8(QJsonDocument(scriptData).toJson(QJsonDocument::Compact)));
  QVariant result;
  bool finished = false;
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(QStringLiteral("<!doctype html><html><body></body></html>"));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 10000);
  QVERIFY(loaded.at(0).at(0).toBool());
  page.runJavaScript(execute);
  QTimer poll;
  connect(&poll, &QTimer::timeout, &page, [&]() {
    page.runJavaScript(QStringLiteral("window.__plantResult"), [&](const QVariant &p_result) {
      if (p_result.isValid()) {
        result = p_result;
        finished = true;
        poll.stop();
      }
    });
  });
  poll.start(10);
  QTRY_VERIFY_WITH_TIMEOUT(finished, 10000);
  const auto values = QJsonDocument::fromJson(result.toString().toUtf8()).object();
  QVERIFY2(!values.contains(QStringLiteral("error")), qPrintable(result.toString()));
  if (scenario == QStringLiteral("protected")) {
    QCOMPARE(values.value(QStringLiteral("requests")).toInt(), 0);
    QVERIFY(values.value(QStringLiteral("empty")).toBool());
  } else if (scenario == QStringLiteral("preview")) {
    QCOMPARE(values.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
    QCOMPARE(values.value(QStringLiteral("width")).toInt(), 3);
    QCOMPARE(values.value(QStringLiteral("height")).toInt(), 3);
    QCOMPARE(values.value(QStringLiteral("first")).toString(), QStringLiteral("255,0,0,255"));
    QCOMPARE(values.value(QStringLiteral("second")).toString(), QStringLiteral("0,0,255,255"));
  } else {
    QCOMPARE(values.value(QStringLiteral("pages")).toInt(), pages);
    QCOMPARE(values.value(QStringLiteral("incomplete")).toBool(), incomplete);
    QCOMPARE(values.value(QStringLiteral("completed")).toInt(), 1);
    QVERIFY(!values.value(QStringLiteral("active")).toBool());
    const auto labels = values.value(QStringLiteral("labels")).toArray();
    QCOMPARE(labels.first().toString(), QStringLiteral("FIRST"));
    if (pages > 1) {
      QCOMPARE(labels.at(1).toString(), scenario == QStringLiteral("identical")
                                            ? QStringLiteral("FIRST")
                                            : QStringLiteral("SECOND"));
    }
  }
}

void TestGraphRendererJs::testGraphvizEngines_data() {
  QTest::addColumn<QString>("scenario");
  for (const auto *scenario :
       {"mixed", "preview", "svg-recovery", "png-recovery", "preview-recovery"}) {
    QTest::newRow(scenario) << QString::fromLatin1(scenario);
  }
}

void TestGraphRendererJs::testGraphvizEngines() {
  QFETCH(QString, scenario);
  // File-backed scripts exercise document.currentScript and the production lazy
  // loader. In particular, concurrent previews must not bypass initialization.
  QString html = QStringLiteral(R"HTML(<!doctype html><html><body>
<main id="content"></main><div id="preview"></div><script>
window.vxOptions = {protectedView: false, webGraphviz: true};
window.completed = 0;
window.onFinished = null;
window.workers = {markdownit: {addLangsToSkipHighlight() {}}};
window.previewResults = [];
window.previewWaiters = new Map();
window.vxcore = {
  contentContainer: document.getElementById('content'),
  on() {},
  registerWorker(worker) { workers[worker.name] = worker; worker.register(this); },
  getWorker(name) { return workers[name]; },
  finishWorker() { ++completed; if (onFinished) onFinished(); },
  renderGraph() { throw new Error('Browser rendering must not dispatch a native process'); },
  setGraphPreviewData(data) {
    previewResults.push(data);
    const waiter = previewWaiters.get(data.id);
    if (waiter) { previewWaiters.delete(data.id); waiter(data); }
  }
};
</script>)HTML");
  for (const auto *name : {"utils.js", "vxworker.js", "graphrenderer.js", "imageviewer.js",
                           "graphpreviewer.js", "markdown-it/markdown-it.min.js", "graphviz.js"}) {
    QVERIFY(QFile::exists(webJsDir() + QLatin1Char('/') + QLatin1String(name)));
    html += QStringLiteral("<script src=\"%1\"></script>").arg(QLatin1String(name));
  }
  const QJsonObject options{{QStringLiteral("scenario"), scenario},
                            {QStringLiteral("source"), QString::fromLatin1(c_graphvizSource)}};
  html += QStringLiteral("<script>const options = %1;\n")
              .arg(QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
  html += QStringLiteral(R"JS(
(async function() {
try {
  const check = (ok, message) => { if (!ok) throw new Error(message); };
  const fixture = options.source;
  const renderer = workers.graphviz;
  const container = vxcore.contentContainer;
  const previewer = new GraphPreviewer(vxcore, document.getElementById('preview'));
  const geometry = svg => Object.fromEntries(Array.from(svg.querySelectorAll('g.node')).map(n => {
    const ellipse = n.querySelector('ellipse');
    return [n.querySelector('title').textContent,
            ['cx', 'cy'].map(a => ellipse.getAttribute(a))];
  }).sort((a, b) => a[0].localeCompare(b[0])));
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  const labels = svg => Array.from(svg.querySelectorAll('g.node title'), n => n.textContent).sort();
  const fence = (lang, text = fixture) => '```' + lang + '\n' + text + '\n```';
  const render = (markdown, format = 'svg') => new Promise(resolve => {
    renderer.reset();
    container.innerHTML = markdownit({langPrefix: 'lang-'}).render(markdown);
    onFinished = resolve;
    renderer.render(container, renderer.langs.map(lang => 'lang-' + lang), format);
  });
  const preview = (id, lang, text = fixture) => new Promise(resolve => {
    previewWaiters.set(id, resolve);
    previewer.previewGraph(id, 1, lang, text);
  });
  const previewGeometry = data => geometry(new DOMParser().parseFromString(data.data, 'image/svg+xml'));
  const reference = async engine => geometry(await new Viz().renderSVGElement(fixture, {engine}));
  const imageLoaded = image => new Promise((resolve, reject) => {
    if (image.complete && image.naturalWidth > 0) { resolve(); return; }
    image.onload = resolve;
    image.onerror = () => reject(new Error('PNG could not be decoded'));
  });
  let result = {};
  if (options.scenario === 'mixed') {
    const languages = ['dot', 'graphviz', 'neato', 'twopi', 'circo', 'fdp', 'patchwork', 'osage'];
    await render(languages.concat(['sfdp', 'unknown-graph-engine']).map(lang => fence(lang)).join('\n\n'));
    const diagrams = Array.from(container.querySelectorAll('svg'));
    check(diagrams.length === 8, 'All seven browser engines and graphviz alias must render');
    check(diagrams.every(svg => same(labels(svg), ['a', 'b', 'c', 'd', 'e'])), 'Missing named nodes');
    const dot = await reference('dot');
    const circo = await reference('circo');
    check(!same(dot, circo), 'Fixture must distinguish engine geometry');
    check(same(geometry(diagrams[0]), dot), 'dot fence geometry differs from dot reference');
    check(same(geometry(diagrams[1]), dot), 'graphviz alias is not dot');
    check(same(geometry(diagrams[4]), circo), 'circo fence geometry differs from circo reference');
    const retained = Array.from(container.querySelectorAll('code'));
    check(same(retained.map(n => n.className), ['lang-sfdp', 'lang-unknown-graph-engine']),
          'Unsupported languages must remain source');
    check(retained.every(n => n.textContent.trim() === fixture.trim()), 'Source was rewritten');
    check(completed === 1 && !renderer.passActive, 'Mixed pass did not finish exactly once');
    // Same body, only the fence changes, then changes back.
    for (const engine of ['dot', 'circo', 'dot']) {
      await render(fence(engine));
      check(same(geometry(container.querySelector('svg')), engine === 'dot' ? dot : circo),
            'Engine-only fence edit retained the previous layout');
    }
    // DOT's graph-level layout attribute keeps its own precedence.
    await render(fence('dot', fixture.replace('graph G {', 'graph G { layout=circo;')));
    check(same(geometry(container.querySelector('svg')), circo), 'DOT layout precedence changed');
    result = {diagrams: diagrams.length, completed};
  } else if (options.scenario === 'preview') {
    // Both requests are issued while the actual scripts are still loading.
    const pair = await Promise.all([preview(1, 'dot'), preview(2, 'circo')]);
    const dot = await reference('dot');
    const circo = await reference('circo');
    check(!same(dot, circo), 'Fixture must distinguish preview layouts');
    check(pair.every(p => p.format === 'svg'), 'Preview did not deliver SVG');
    check(same(previewGeometry(pair[0]), dot), 'dot preview has wrong layout');
    check(same(previewGeometry(pair[1]), circo), 'circo preview has wrong layout');
    const unsupported = await preview(3, 'sfdp');
    check(unsupported.data === '', 'Browser sfdp preview must be empty');
    const valid = await preview(4, 'circo');
    check(same(previewGeometry(valid), circo), 'sfdp poisoned subsequent preview');
    check(same(previewResults.map(p => p.id).sort(), [1, 2, 3, 4]), 'Preview completion count');
    result = {previews: previewResults.length};
  } else if (options.scenario === 'preview-recovery') {
    const pair = await Promise.all([preview(1, 'dot', 'graph {'), preview(2, 'circo')]);
    check(pair[0].data === '', 'Malformed preview must fail');
    check(pair[1].format === 'svg' && same(previewGeometry(pair[1]), await reference('circo')),
          'Malformed input poisoned the concurrently queued preview');
    check(previewResults.length === 2, 'Each queued preview must complete once');
    result = {previews: previewResults.length};
  } else {
    const format = options.scenario === 'png-recovery' ? 'png' : 'svg';
    await render(fence('circo', 'graph {') + '\n\n' + fence('circo'), format);
    check(completed === 1 && !renderer.passActive, 'Malformed input stalled or doubled completion');
    const retained = Array.from(container.querySelectorAll('code'));
    check(retained.length === 1 && retained[0].textContent.trim() === 'graph {',
          'Only the invalid source should remain');
    if (format === 'svg') {
      check(same(geometry(container.querySelector('svg')), await reference('circo')),
            'Valid graph after malformed source has wrong layout');
    } else {
      const image = container.querySelector('img');
      check(!!image, 'Missing PNG result');
      const blob = await (await fetch(image.src)).blob();
      check(blob.type === 'image/png', 'Rendered image is not PNG');
      await imageLoaded(image);
      const canvas = document.createElement('canvas');
      canvas.width = image.naturalWidth; canvas.height = image.naturalHeight;
      check(canvas.width > 0 && canvas.height > 0, 'PNG dimensions are empty');
      const context = canvas.getContext('2d'); context.drawImage(image, 0, 0);
      const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
      let ink = false;
      for (let i = 4; i < pixels.length; i += 4) {
        if (pixels[i + 3] && [0, 1, 2, 3].some(c => pixels[i + c] !== pixels[c])) {
          ink = true; break;
        }
      }
      check(ink, 'PNG contains only background pixels');
      const direct = await new Viz().renderImageElement(fixture, {engine: 'circo'});
      await imageLoaded(direct);
      check(image.naturalWidth === direct.naturalWidth && image.naturalHeight === direct.naturalHeight,
            'PNG dimensions do not match the selected circo layout');
    }
    result = {format, completed};
  }
  window.__graphResult = JSON.stringify(result);
} catch (error) {
  window.__graphResult = JSON.stringify({error: String(error) + '\n' + error.stack});
}
})();
</script></body></html>)JS");
  QWebEngineProfile profile;
  QWebEnginePage page(&profile);
  QSignalSpy loaded(&page, &QWebEnginePage::loadFinished);
  page.setHtml(html, QUrl::fromLocalFile(webJsDir() + QLatin1Char('/')));
  QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 30000);
  QVERIFY(loaded.at(0).at(0).toBool());
  QVariant result;
  bool finished = false;
  QTimer poll;
  connect(&poll, &QTimer::timeout, &page, [&]() {
    page.runJavaScript(QStringLiteral("window.__graphResult"), [&](const QVariant &p_result) {
      if (p_result.isValid()) {
        result = p_result;
        finished = true;
        poll.stop();
      }
    });
  });
  poll.start(10);
  QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
  const auto values = QJsonDocument::fromJson(result.toString().toUtf8()).object();
  QVERIFY2(!values.contains(QStringLiteral("error")), qPrintable(result.toString()));
  qInfo().noquote() << scenario << result.toString();
}

void TestGraphRendererJs::testGraphvizNativeEngineCache() {
  const auto dot = QStandardPaths::findExecutable(QStringLiteral("dot"));
  if (dot.isEmpty()) {
    QSKIP("Native Graphviz integration requires dot on PATH");
  }
  const auto geometry = [](const QString &p_svg) {
    QMap<QString, QPointF> nodes;
    QXmlStreamReader xml(p_svg);
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement() || xml.name() != QLatin1String("g") ||
          xml.attributes().value(QLatin1String("class")) != QLatin1String("node")) {
        continue;
      }
      QString name;
      while (xml.readNextStartElement()) {
        if (xml.name() == QLatin1String("title")) {
          name = xml.readElementText();
        } else if (xml.name() == QLatin1String("ellipse")) {
          const auto attrs = xml.attributes();
          nodes.insert(name, QPointF(attrs.value(QLatin1String("cx")).toDouble(),
                                     attrs.value(QLatin1String("cy")).toDouble()));
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }
    }
    return nodes;
  };
  QMap<QString, QMap<QString, QPointF>> references;
  for (const auto &engine : {QStringLiteral("dot"), QStringLiteral("circo")}) {
    QProcess process;
    process.start(dot, {QStringLiteral("-K") + engine, QStringLiteral("-Tsvg")});
    QVERIFY(process.waitForStarted());
    process.write(c_graphvizSource);
    process.closeWriteChannel();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    references.insert(engine, geometry(QString::fromUtf8(process.readAllStandardOutput())));
    QCOMPARE(references[engine].keys(),
             QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                          QStringLiteral("d"), QStringLiteral("e")}));
  }
  QVERIFY(references[QStringLiteral("dot")] != references[QStringLiteral("circo")]);
  auto &helper = vnotex::GraphvizHelper::getInst();
  struct ResetHelper {
    ~ResetHelper() { vnotex::GraphvizHelper::getInst().update(QString()); }
  } resetHelper;
  helper.update(dot);
  const QStringList engines{QStringLiteral("dot"), QStringLiteral("circo"), QStringLiteral("dot")};
  QStringList results;
  QList<bool> successes;
  QObject owner;
  for (int i = 0; i < engines.size(); ++i) {
    helper.process(
        i, 1, QStringLiteral("svg"), QString::fromLatin1(c_graphvizSource), &owner,
        [&](quint64, vnotex::TimeStamp, const QString &, const QString &p_data, bool p_success) {
          results.append(p_data);
          successes.append(p_success);
        },
        0, engines[i]);
  }
  QTRY_COMPARE_WITH_TIMEOUT(results.size(), 3, 30000);
  for (int i = 0; i < engines.size(); ++i) {
    QVERIFY(successes[i]);
    QCOMPARE(geometry(results[i]), references[engines[i]]);
  }
}

} // namespace tests

int main(int argc, char **argv) {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication app(argc, argv);
  tests::TestGraphRendererJs test;
  return QTest::qExec(&test, argc, argv);
}
#include "test_graphrenderer_js.moc"
