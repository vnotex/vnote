// test_sync_service_freshness.cpp - Tests for SyncService::maybeTriggerPostReconcile
//
// Follow-up to vxcore-metadata-events: closes the multi-device staleness window
// where opening a notebook (or app start) only enqueued enableSync; the first
// FetchOrigin had to wait for the next save/manual sync. After reconcile
// registers a notebook with vxcore, SyncService now ALSO enqueues an async
// triggerSync IF the notebook is "stale" (last successful sync > 2 min ago).
//
// Behavior matrix verified by the four test cases below:
//
//   Scenario                                            | lastSyncMs    | inProgress | ready |
//   Expected
//   ----------------------------------------------------+---------------+------------+-------+----------
//   stale (>2 min ago)                                  | now - 10 min  | false      | true  |
//   trigger fresh (<2 min ago, e.g., rapid close/reopen)        | now - 1 min   | false      | true
//   | NO trigger sync already in flight                              | now - 10 min  | true       |
//   true  | NO trigger notebook not ready (simulates post-enable-failure)  | any           | any |
//   false | NO trigger
//
// The four cases exercise the four early-return guards in
// SyncService::maybeTriggerPostReconcile, in order of evaluation.
//
// Test seams:
//   - testForceLastSyncUtc(id, ms) overrides the metadata.db read
//   - testSetMaybeTriggerBypassReadinessCheck(bool) lets us skip the
//     isSyncEnabled/isSyncRegistered defense for tests 1-3 (which want
//     to exercise the gate logic) while leaving test 4 to validate the
//     defense itself
//   - testInvokeMaybeTriggerPostReconcile(id) is the public entry point
//     so tests can drive the helper without staging a real reconcile

#include <QCoreApplication>
#include <QDateTime>
#include <QSignalSpy>
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

class TestSyncServiceFreshness : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void test_post_reconcile_triggers_when_stale();
  void test_post_reconcile_skips_when_fresh();
  void test_post_reconcile_skips_when_in_progress();
  void test_post_reconcile_skips_on_enable_failure();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncServiceFreshness::initTestCase() {
  // CRITICAL: enable test mode BEFORE creating vxcore context (per tests/AGENTS.md)
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);
}

void TestSyncServiceFreshness::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

// Case 1: notebook last synced 10 minutes ago, no sync in progress.
// maybeTriggerPostReconcile must enqueue triggerSyncNow. We detect the
// trigger by spying on syncStarted, which fires from the work item that
// triggerSyncNow enqueues onto SyncWorkQueueManager.
void TestSyncServiceFreshness::test_post_reconcile_triggers_when_stale() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString notebookId = QStringLiteral("test-nb-stale-triggers");

  // Spy on syncStarted, which fires from triggerSyncNow's enqueued work.
  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);

  // Force the readiness check off (no real vxcore registration in this test)
  // and set lastSyncUtc to 10 minutes ago.
  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 tenMinutesAgo = nowMs - (10 * 60 * 1000);
  syncService.testForceLastSyncUtc(notebookId, tenMinutesAgo);

  // Invoke the post-reconcile gate. It should call triggerSyncNow, which
  // enqueues work that emits syncStarted on the next event-loop turn.
  syncService.testInvokeMaybeTriggerPostReconcile(notebookId);

  // syncStarted is emitted via QMetaObject::invokeMethod(... QueuedConnection)
  // from a worker; allow up to 2s for the bounce.
  const bool fired = startedSpy.wait(2000);
  QVERIFY2(fired, "syncStarted did not fire within 2s — triggerSyncNow was not called");
  QCOMPARE(startedSpy.first().at(0).toString(), notebookId);

  syncService.testSetMaybeTriggerBypassReadinessCheck(false);
  guard.cleanup();
}

// Case 2: notebook last synced 1 minute ago — within the 2-minute freshness
// window. maybeTriggerPostReconcile must NOT trigger. This is the critical
// "don't thrash" guarantee for rapid close/reopen cycles.
void TestSyncServiceFreshness::test_post_reconcile_skips_when_fresh() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString notebookId = QStringLiteral("test-nb-fresh-skips");

  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);

  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 oneMinuteAgo = nowMs - (60 * 1000);
  syncService.testForceLastSyncUtc(notebookId, oneMinuteAgo);

  syncService.testInvokeMaybeTriggerPostReconcile(notebookId);

  // Negative assertion: confirm NO syncStarted within a short window. We
  // give the event loop a couple of spins to flush any pending invokes
  // (none should be queued because triggerSyncNow was never called).
  QTest::qWait(300);
  QCOMPARE(startedSpy.count(), 0);

  syncService.testSetMaybeTriggerBypassReadinessCheck(false);
  guard.cleanup();
}

// Case 3: sync is already in flight (per SyncWorkQueueManager state).
// maybeTriggerPostReconcile must NOT trigger another one. Defense against
// thrashing — even though the queue would coalesce, the gate avoids the
// spurious enqueue/coalesce cycle.
void TestSyncServiceFreshness::test_post_reconcile_skips_when_in_progress() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString notebookId = QStringLiteral("test-nb-in-progress-skips");

  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);

  syncService.testSetMaybeTriggerBypassReadinessCheck(true);
  // Stale lastSync so the freshness gate does NOT short-circuit before the
  // in-progress check — we want the in-progress branch to be the sole gate.
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 tenMinutesAgo = nowMs - (10 * 60 * 1000);
  syncService.testForceLastSyncUtc(notebookId, tenMinutesAgo);
  // Force in-flight via the existing seam used by test_sync_close_block.
  syncService.testSetInProgress(notebookId, true);
  QVERIFY(syncService.isSyncInProgress(notebookId));

  syncService.testInvokeMaybeTriggerPostReconcile(notebookId);

  QTest::qWait(300);
  QCOMPARE(startedSpy.count(), 0);

  // Reset the forced in-flight state so the queue dtor doesn't trip on it.
  syncService.testSetInProgress(notebookId, false);
  syncService.testSetMaybeTriggerBypassReadinessCheck(false);
  guard.cleanup();
}

// Case 4: notebook is NOT ready (isSyncEnabled / isSyncRegistered return false).
// This simulates the post-enable-failure aftermath: even if the reconcile
// wiring slips and the gate is invoked, the defense check must short-circuit
// so we never try to triggerSyncNow on a non-registered notebook.
// Production guarantee: reconcileSyncForNotebook only schedules the gate
// when SyncOps::enableSync returns VXCORE_OK, so in normal operation
// maybeTriggerPostReconcile never runs after enable failure. This test
// validates the defense in depth.
void TestSyncServiceFreshness::test_post_reconcile_skips_on_enable_failure() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncWorkQueueManager workQueue;
  services.registerService<SyncWorkQueueManager>(&workQueue);
  SyncService syncService(services);

  const QString notebookId = QStringLiteral("test-nb-enable-failed-skips");

  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);

  // No notebook was created and no registration was performed, so both
  // isSyncEnabled and isSyncRegistered return false. Do NOT bypass the
  // readiness check — that is the gate under test here. Also set a stale
  // lastSyncUtc so we know the gate isn't bailing on freshness instead.
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 tenMinutesAgo = nowMs - (10 * 60 * 1000);
  syncService.testForceLastSyncUtc(notebookId, tenMinutesAgo);
  QVERIFY(!syncService.isSyncEnabled(notebookId));
  QVERIFY(!syncService.isSyncRegistered(notebookId));

  syncService.testInvokeMaybeTriggerPostReconcile(notebookId);

  QTest::qWait(300);
  QCOMPARE(startedSpy.count(), 0);

  guard.cleanup();
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncServiceFreshness)
#include "test_sync_service_freshness.moc"
