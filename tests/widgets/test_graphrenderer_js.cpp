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
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <QtTest>

namespace tests {

namespace {

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

var document = { currentScript: { src: 'file:///web/js/graphrenderer.js' } };

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

  initialize(p_callback) {
    return super.initialize(() => {
      throw new ReferenceError('someLibrary is not defined');
    });
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
  // finishWork() exactly-once, over every exit path.
  void testPass_emptyNodeListFinishesExactlyOnce();
  void testPass_boundedDispatchFinishesExactlyOnce();
  void testPass_unboundedDispatchFinishesExactlyOnce();
  void testPass_synchronousRendererYieldsBetweenBatches();
  void testPass_throwingRenderOneStillFinishes();
  void testPass_rejectedAsyncRenderOneStillFinishes();
  void testPass_resetWhileLiveIsReported();
  void testPass_surplusCompletionDoesNotFinishTwice();

  // The bound itself.
  void testConcurrency_neverExceedsTheLimit();
  void testConcurrency_zeroMeansUnbounded();
  void testConcurrency_windowRefillsPerCompletion();

  // Initialization and diagnostics must not be able to cost the finish.
  void testInit_failureInAsyncInitializeStillFinishes();
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestGraphRendererJs)
#include "test_graphrenderer_js.moc"
