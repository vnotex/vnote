// test_sync_service_debounce.cpp - Tests for the SyncService trailing-throttle
// auto-sync debounce (T7).
//
// The auto-sync path collapses bursts of vxcore `sync.should_run` events into a
// single trailing sync per `autoSyncDebounceSeconds` window. The debounce engine
// is a set of private helpers (`armOrIgnoreDebounce`, `onDebounceTimeout`,
// `enqueueAutoSync`) plus a per-notebook `QTimer` map. These tests drive that
// engine DETERMINISTICALLY through the unconditional T7 test seams and assert via
// the timer-state seams — NO real network, NO real-clock debounce sleeps, NO real
// vxcore sync registration.
//
// Test seams (all unconditional, ADR-6):
//   - testSetDebounceOverrideSeconds(int)         // -1 = config; >=0 forces N
//   - testIsDebounceTimerActive(id) -> bool
//   - testDebounceRemainingMs(id)   -> int        // -1 if no timer object
//   - testFireDebounceNow(id)                     // invokes onDebounceTimeout()
//   - testForceLastSyncUtc(id, ms)                // -1 clears
//   - testSetMaybeTriggerBypassReadinessCheck(bool)
//
// IMPORTANT — why we drive `onDebounceTimeout` (via testFireDebounceNow) rather
// than the public auto-sync entry slot `onSyncShouldRun`:
//
//   `SyncService::onSyncShouldRun` (syncservice.cpp:1491) gates on
//   `isSyncEnabled(id) && isSyncRegistered(id)` and — unlike `onDebounceTimeout`
//   and `maybeTriggerPostReconcile` — does NOT honor
//   `testSetMaybeTriggerBypassReadinessCheck`. `isSyncRegistered` requires a live
//   vxcore `states_` entry, which only a real bare-repo + keychain enable flow
//   produces. So `onSyncShouldRun`'s debounce-arming branch is not reachable in a
//   pure unit test without standing up real sync. The task brief anticipates this
//   ("if you genuinely cannot drive onSyncShouldRun, prefer asserting through
//   armOrIgnoreDebounce effects that ARE observable via the timer-state seams").
//
//   Both `onSyncShouldRun` and `onDebounceTimeout` delegate to the SAME engine
//   helpers (`armOrIgnoreDebounce` / `enqueueAutoSync`). `onDebounceTimeout`
//   honors the readiness bypass and routes through `armOrIgnoreDebounce` (when no
//   timer exists yet) and `enqueueAutoSync` (on flush), so firing it exercises the
//   exact arm / re-arm / flush logic under test. No `testInvokeOnSyncShouldRun`
//   seam was added (it would not help — the readiness gate would still short
//   the engine, yielding a weak, misleading assertion).

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworkqueuemanager.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

// Window constants. N=60s is comfortably larger than any scheduling jitter, so
// remaining-time bounds derived from forced timestamps are stable.
static constexpr int kDebounceSeconds = 60;
static constexpr qint64 kWindowMs = static_cast<qint64>(kDebounceSeconds) * 1000;

class TestSyncServiceDebounce : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void test_zero_seconds_enqueues_immediately_no_timer();
  void test_burst_collapses_to_single_armed_timer();
  void test_trailing_flush_drops_timer_when_stale();
  void test_rearms_when_last_sync_still_fresh();
  void test_manual_trigger_creates_no_debounce_timer();
  void test_shutdown_clears_debounce_timers();
  void test_independent_notebooks_have_independent_timers();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncServiceDebounce::initTestCase() {
  // CRITICAL: enable test mode BEFORE creating vxcore context (per tests/AGENTS.md)
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);
}

void TestSyncServiceDebounce::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

// Case 1: N == 0 (immediate cadence). Firing the debounce engine must NOT create
// any debounce timer — the auto path enqueues straight away. Asserts the strong
// invariant that the N==0 branch never instantiates a QTimer (remainingMs == -1).
void TestSyncServiceDebounce::test_zero_seconds_enqueues_immediately_no_timer() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString id = QStringLiteral("test-nb-debounce-zero");

  syncService.testSetDebounceOverrideSeconds(0);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now - 10 * 1000); // irrelevant when N==0

  syncService.testFireDebounceNow(id);

  QVERIFY2(!syncService.testIsDebounceTimerActive(id), "N==0 must not arm a timer");
  QCOMPARE(syncService.testDebounceRemainingMs(id), -1); // no QTimer object created at all

  QTest::qWait(50); // drain the immediate auto-sync enqueue before teardown
  syncService.testForceLastSyncUtc(id, -1);
  guard.cleanup();
}

// Case 2: a burst of auto-sync requests inside an open window must collapse to
// exactly ONE armed timer (not one per request). We simulate the burst with 5
// fires while the window is open; the engine keeps a single QTimer in the map and
// re-arms it rather than spawning new timers. Remaining time stays bounded within
// the window.
void TestSyncServiceDebounce::test_burst_collapses_to_single_armed_timer() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString id = QStringLiteral("test-nb-debounce-burst");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  // Window open: last sync 10s ago, so ~50s remain in the 60s window.
  syncService.testForceLastSyncUtc(id, now - 10 * 1000);

  for (int i = 0; i < 5; ++i) {
    syncService.testFireDebounceNow(id);
  }

  QVERIFY2(syncService.testIsDebounceTimerActive(id),
           "burst inside the window must leave exactly one armed timer");
  const int remaining = syncService.testDebounceRemainingMs(id);
  QVERIFY2(remaining > 0, "armed timer must have positive remaining time");
  // Bounded by the window; the forced 10s-ago last sync leaves <= ~50s.
  QVERIFY2(remaining <= static_cast<int>(kWindowMs),
           "remaining time must not exceed the debounce window");

  syncService.testForceLastSyncUtc(id, -1);
  guard.cleanup();
}

// Case 3: when the timer fires and the last successful sync is now OLDER than the
// window, the engine must FLUSH (enqueue the trailing sync) and leave the timer
// inactive — proving it enqueued rather than re-armed.
void TestSyncServiceDebounce::test_trailing_flush_drops_timer_when_stale() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString id = QStringLiteral("test-nb-debounce-flush");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);

  // Arm a timer first (window open: last sync 10s ago).
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now - 10 * 1000);
  syncService.testFireDebounceNow(id);
  QVERIFY2(syncService.testIsDebounceTimerActive(id), "precondition: timer armed");

  // Now make the last sync older than the window, then fire: must flush.
  now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now - 10 * kWindowMs); // far outside window
  syncService.testFireDebounceNow(id);

  QVERIFY2(!syncService.testIsDebounceTimerActive(id),
           "stale fire must flush (enqueue) and leave the timer inactive");

  QTest::qWait(50); // drain the flushed auto-sync enqueue before teardown
  syncService.testForceLastSyncUtc(id, -1);
  guard.cleanup();
}

// Case 4: when the timer fires but the last successful sync is STILL fresh (a save
// landed late in the window), the engine must RE-ARM rather than enqueue — the
// timer stays active with positive remaining time.
void TestSyncServiceDebounce::test_rearms_when_last_sync_still_fresh() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString id = QStringLiteral("test-nb-debounce-rearm");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);

  // Arm a timer (window open: last sync 10s ago).
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now - 10 * 1000);
  syncService.testFireDebounceNow(id);
  QVERIFY2(syncService.testIsDebounceTimerActive(id), "precondition: timer armed");

  // Last sync is now the freshest possible; firing must re-arm (not enqueue).
  now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now);
  syncService.testFireDebounceNow(id);

  QVERIFY2(syncService.testIsDebounceTimerActive(id),
           "fresh fire must re-arm the timer, not enqueue");
  QVERIFY2(syncService.testDebounceRemainingMs(id) > 0,
           "re-armed timer must have positive remaining time");

  syncService.testForceLastSyncUtc(id, -1);
  guard.cleanup();
}

// Case 5: the manual "Sync Now" path (triggerSyncNow) is NEVER throttled. It must
// not create a debounce timer of its own, and it must not disturb a debounce timer
// already armed by a prior auto event.
void TestSyncServiceDebounce::test_manual_trigger_creates_no_debounce_timer() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString armedId = QStringLiteral("test-nb-debounce-manual-armed");
  const QString manualId = QStringLiteral("test-nb-debounce-manual-fresh");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);

  // Arm an auto-debounce timer for armedId.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(armedId, now - 10 * 1000);
  syncService.testFireDebounceNow(armedId);
  QVERIFY2(syncService.testIsDebounceTimerActive(armedId), "precondition: armed timer present");
  const int remainingBefore = syncService.testDebounceRemainingMs(armedId);

  // Manual trigger on a DIFFERENT notebook that has no timer: must create none.
  syncService.triggerSyncNow(manualId);
  QVERIFY2(!syncService.testIsDebounceTimerActive(manualId),
           "manual trigger must not arm a debounce timer");
  QCOMPARE(syncService.testDebounceRemainingMs(manualId), -1); // no QTimer object created

  // Manual trigger on the armed notebook must leave its auto timer untouched.
  syncService.triggerSyncNow(armedId);
  QVERIFY2(syncService.testIsDebounceTimerActive(armedId),
           "manual trigger must not drop a pre-existing auto debounce timer");
  QVERIFY2(syncService.testDebounceRemainingMs(armedId) > 0,
           "pre-existing auto timer must remain armed after a manual trigger");
  // The manual path does not touch the timer, so remaining time is essentially
  // unchanged (allowing only for elapsed scheduling time).
  QVERIFY2(syncService.testDebounceRemainingMs(armedId) <= remainingBefore,
           "manual trigger must not extend the auto timer");

  QTest::qWait(100); // drain the two manual triggerSyncNow enqueues before teardown
  syncService.testForceLastSyncUtc(armedId, -1);
  guard.cleanup();
}

// Case 6: shutdown() must stop and clear all debounce timers.
void TestSyncServiceDebounce::test_shutdown_clears_debounce_timers() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString id = QStringLiteral("test-nb-debounce-shutdown");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(id, now - 10 * 1000);
  syncService.testFireDebounceNow(id);
  QVERIFY2(syncService.testIsDebounceTimerActive(id), "precondition: timer armed");

  syncService.shutdown();

  QVERIFY2(!syncService.testIsDebounceTimerActive(id),
           "shutdown() must stop and clear the debounce timer");
  QCOMPARE(syncService.testDebounceRemainingMs(id), -1); // timer removed from the map

  syncService.testForceLastSyncUtc(id, -1);
  guard.cleanup();
}

// Case 7: timers are per-notebook and independent. Two notebooks armed with
// different forced last-sync timestamps get distinct remaining times, and flushing
// one leaves the other untouched.
void TestSyncServiceDebounce::test_independent_notebooks_have_independent_timers() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString a = QStringLiteral("test-nb-debounce-indep-A");
  const QString b = QStringLiteral("test-nb-debounce-indep-B");

  syncService.testSetDebounceOverrideSeconds(kDebounceSeconds);
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  // A: 10s into the window (~50s remain). B: 30s into the window (~30s remain).
  syncService.testForceLastSyncUtc(a, now - 10 * 1000);
  syncService.testForceLastSyncUtc(b, now - 30 * 1000);
  syncService.testFireDebounceNow(a);
  syncService.testFireDebounceNow(b);

  QVERIFY2(syncService.testIsDebounceTimerActive(a), "A timer must be armed");
  QVERIFY2(syncService.testIsDebounceTimerActive(b), "B timer must be armed");
  const int remA = syncService.testDebounceRemainingMs(a);
  const int remB = syncService.testDebounceRemainingMs(b);
  QVERIFY2(remA > remB,
           "independent timers must hold independent remaining times (A later than B)");

  // Flush A only (make its last sync stale, then fire). B must be unaffected.
  const qint64 now2 = QDateTime::currentMSecsSinceEpoch();
  syncService.testForceLastSyncUtc(a, now2 - 10 * kWindowMs);
  syncService.testFireDebounceNow(a);

  QVERIFY2(!syncService.testIsDebounceTimerActive(a), "flushing A must drop A's timer");
  QVERIFY2(syncService.testIsDebounceTimerActive(b), "B's timer must survive A's flush");
  QVERIFY2(syncService.testDebounceRemainingMs(b) > 0, "B must still have positive remaining time");

  QTest::qWait(50); // drain A's flushed enqueue before teardown
  syncService.testForceLastSyncUtc(a, -1);
  syncService.testForceLastSyncUtc(b, -1);
  guard.cleanup();
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncServiceDebounce)
#include "test_sync_service_debounce.moc"
