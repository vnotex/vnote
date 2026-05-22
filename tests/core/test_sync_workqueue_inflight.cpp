#include <QAtomicInt>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QThread>
#include <QtTest>

#include <core/services/syncworkqueuemanager.h>

namespace tests {

class TestSyncWorkQueueInFlight : public QObject {
  Q_OBJECT

private slots:
  void initial_state_is_idle();
  void after_enqueue_has_pending_true();
  void during_execution_running_is_true();
  void after_completion_both_false();
  void multiple_enqueues_hasPending_stays_true();
  void unknown_notebook_returns_defaults();
  void inFlightState_returns_snapshot();
  void hasPending_correlates_with_queue_depth();
};

void TestSyncWorkQueueInFlight::initial_state_is_idle() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-initial");

  // Before any enqueue: both should be false
  QVERIFY(!mgr.isRunning(nbId));
  QVERIFY(!mgr.hasPending(nbId));

  auto state = mgr.inFlightState(nbId);
  QCOMPARE(state.running, false);
  QCOMPARE(state.hasPending, false);
  QCOMPARE(state.cancellation, nullptr);
}

void TestSyncWorkQueueInFlight::after_enqueue_has_pending_true() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-enqueue");

  QSemaphore workStarted;
  QSemaphore workCanFinish;

  // Enqueue a slow work item that will block until we allow it
  (void)mgr.enqueue(nbId, [&]() {
    workStarted.release(1);
    workCanFinish.acquire(1);
  });

  // Give the runner thread a moment to start
  QThread::msleep(50);

  // At this point: running should be true (worker picked it up)
  // and hasPending should be true (item is being executed)
  QVERIFY(mgr.isRunning(nbId));
  QVERIFY(mgr.hasPending(nbId));

  // Release work to finish
  workCanFinish.release(1);

  // Wait for queue to drain
  QTRY_VERIFY_WITH_TIMEOUT(mgr.queueDepth(nbId) == 0 && !mgr.isRunning(nbId), 5000);

  // After completion: both false
  QVERIFY(!mgr.isRunning(nbId));
  QVERIFY(!mgr.hasPending(nbId));
}

void TestSyncWorkQueueInFlight::during_execution_running_is_true() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-running");

  QSemaphore workStarted;
  QSemaphore workCanFinish;

  (void)mgr.enqueue(nbId, [&]() {
    workStarted.release(1);
    workCanFinish.acquire(1);
  });

  // Wait for work to start
  QVERIFY(workStarted.tryAcquire(1, 5000));

  // While executing: running must be true
  QVERIFY(mgr.isRunning(nbId));

  auto state = mgr.inFlightState(nbId);
  QCOMPARE(state.running, true);

  // Release work
  workCanFinish.release(1);

  // Wait for drain
  QTRY_VERIFY_WITH_TIMEOUT(!mgr.isRunning(nbId), 5000);
}

void TestSyncWorkQueueInFlight::after_completion_both_false() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-completion");

  QAtomicInt execCount{0};

  (void)mgr.enqueue(nbId, [&]() {
    QThread::msleep(10);
    execCount.fetchAndAddOrdered(1);
  });

  // Wait for queue to drain
  QTRY_VERIFY_WITH_TIMEOUT(mgr.queueDepth(nbId) == 0 && !mgr.isRunning(nbId), 5000);

  // Verify work ran
  QCOMPARE(execCount.loadAcquire(), 1);

  // Check final state
  auto state = mgr.inFlightState(nbId);
  QCOMPARE(state.running, false);
  QCOMPARE(state.hasPending, false);
}

void TestSyncWorkQueueInFlight::multiple_enqueues_hasPending_stays_true() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-multi");

  QSemaphore firstStarted;
  QSemaphore canContinue;

  // Enqueue 3 items, first one blocks
  (void)mgr.enqueue(nbId, [&]() {
    firstStarted.release(1);
    canContinue.acquire(1);
    QThread::msleep(10);
  });

  (void)mgr.enqueue(nbId, [&]() { QThread::msleep(20); });

  (void)mgr.enqueue(nbId, [&]() { QThread::msleep(20); });

  // Wait for first item to start
  QVERIFY(firstStarted.tryAcquire(1, 5000));

  // Now: running=true, hasPending=true (2 more queued + 1 running)
  QVERIFY(mgr.isRunning(nbId));
  QVERIFY(mgr.hasPending(nbId));
  QCOMPARE(mgr.queueDepth(nbId), 2);

  auto state = mgr.inFlightState(nbId);
  QCOMPARE(state.running, true);
  QCOMPARE(state.hasPending, true);

  // Release first and let the queue drain
  canContinue.release(1);

  QTRY_VERIFY_WITH_TIMEOUT(mgr.queueDepth(nbId) == 0 && !mgr.isRunning(nbId), 5000);

  // After all done
  QVERIFY(!mgr.isRunning(nbId));
  QVERIFY(!mgr.hasPending(nbId));
}

void TestSyncWorkQueueInFlight::unknown_notebook_returns_defaults() {
  vnotex::SyncWorkQueueManager mgr;

  // Query nonexistent notebook
  QVERIFY(!mgr.hasPending(QStringLiteral("does-not-exist")));

  auto state = mgr.inFlightState(QStringLiteral("does-not-exist"));
  QCOMPARE(state.running, false);
  QCOMPARE(state.hasPending, false);
  QCOMPARE(state.cancellation, nullptr);
}

void TestSyncWorkQueueInFlight::inFlightState_returns_snapshot() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-snapshot");

  QSemaphore workStarted;
  QSemaphore canFinish;

  (void)mgr.enqueue(nbId, [&]() {
    workStarted.release(1);
    canFinish.acquire(1);
  });

  // Wait for execution
  QVERIFY(workStarted.tryAcquire(1, 5000));

  // Get snapshot
  auto snapshot1 = mgr.inFlightState(nbId);
  QCOMPARE(snapshot1.running, true);
  QCOMPARE(snapshot1.hasPending, true);

  // Snapshot should be independent (value type, not reference)
  auto snapshot2 = mgr.inFlightState(nbId);
  QCOMPARE(snapshot2.running, true);
  QCOMPARE(snapshot2.hasPending, true);

  // Both should be identical
  QCOMPARE(snapshot1.running, snapshot2.running);
  QCOMPARE(snapshot1.hasPending, snapshot2.hasPending);

  // Allow work to finish
  canFinish.release(1);

  // New snapshot should differ
  QTRY_VERIFY_WITH_TIMEOUT(!mgr.isRunning(nbId), 5000);
  auto snapshot3 = mgr.inFlightState(nbId);
  QCOMPARE(snapshot3.running, false);
  QCOMPARE(snapshot3.hasPending, false);
}

void TestSyncWorkQueueInFlight::hasPending_correlates_with_queue_depth() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-correlation");

  QSemaphore workStarted;
  QSemaphore canContinue;

  // Enqueue slow + 2 quick
  (void)mgr.enqueue(nbId, [&]() {
    workStarted.release(1);
    canContinue.acquire(1);
  });

  (void)mgr.enqueue(nbId, [&]() { QThread::msleep(5); });
  (void)mgr.enqueue(nbId, [&]() { QThread::msleep(5); });

  // Let first start
  QVERIFY(workStarted.tryAcquire(1, 5000));

  // Observation point: running first, 2 more queued
  int depth = mgr.queueDepth(nbId);
  bool pending = mgr.hasPending(nbId);
  bool running = mgr.isRunning(nbId);

  // Semantically: hasPending should be true if queue non-empty OR running
  // In this case: running=true, depth=2, so hasPending=true
  QVERIFY(running);
  QVERIFY(pending);
  QCOMPARE(depth, 2);
  QVERIFY(pending == (running || depth > 0));

  // Release and drain
  canContinue.release(1);

  QTRY_VERIFY_WITH_TIMEOUT(mgr.queueDepth(nbId) == 0 && !mgr.isRunning(nbId), 5000);

  // Final: depth=0, running=false, pending=false
  QCOMPARE(mgr.queueDepth(nbId), 0);
  QVERIFY(!mgr.isRunning(nbId));
  QVERIFY(!mgr.hasPending(nbId));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorkQueueInFlight)
#include "test_sync_workqueue_inflight.moc"
