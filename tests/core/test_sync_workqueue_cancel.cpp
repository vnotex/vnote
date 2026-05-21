#include <QAtomicInt>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QtTest>

#include <core/services/syncworkqueuemanager.h>

namespace tests {

class TestSyncWorkQueueCancel : public QObject {
  Q_OBJECT

private slots:
  void cancel_pending_drops_queued_items_runs_inflight();
  void reentrant_enqueue_from_callback_does_not_deadlock();
  void cancel_unknown_id_returns_zero_no_signal();
  void cancel_after_shutdown_returns_zero_no_signal();
};

void TestSyncWorkQueueCancel::cancel_pending_drops_queued_items_runs_inflight() {
  vnotex::SyncWorkQueueManager mgr;
  qRegisterMetaType<QString>("QString");
  const QString nbId = QStringLiteral("nb-cancel");

  QSignalSpy spy(&mgr, &vnotex::SyncWorkQueueManager::pendingCancelled);

  QSemaphore firstStarted;
  QSemaphore canFinish;
  QAtomicInt firstRan{0};
  QAtomicInt secondBodyRan{0};
  QAtomicInt thirdBodyRan{0};
  QAtomicInt secondCancelled{0};
  QAtomicInt thirdCancelled{0};

  // Item 1: blocks until released. Will run to completion.
  (void)mgr.enqueue(nbId, [&]() {
    firstStarted.release(1);
    canFinish.acquire(1);
    firstRan.fetchAndAddOrdered(1);
  });

  // Wait for item 1 to start so items 2/3 land behind it.
  QVERIFY(firstStarted.tryAcquire(1, 5000));

  // Items 2 and 3 with onCancelled callbacks.
  (void)mgr.enqueue(
      nbId, [&]() { secondBodyRan.fetchAndAddOrdered(1); },
      [&]() { secondCancelled.fetchAndAddOrdered(1); });
  (void)mgr.enqueue(
      nbId, [&]() { thirdBodyRan.fetchAndAddOrdered(1); },
      [&]() { thirdCancelled.fetchAndAddOrdered(1); });

  QCOMPARE(mgr.queueDepth(nbId), 2);

  // Cancel — should drop 2 queued items, leave in-flight alone.
  const int dropped = mgr.cancelPending(nbId);
  QCOMPARE(dropped, 2);
  QCOMPARE(mgr.queueDepth(nbId), 0);
  QVERIFY(mgr.isRunning(nbId));  // In-flight still running
  QVERIFY(mgr.hasPending(nbId)); // Reflects in-flight only

  // Both cancellation callbacks fired.
  QCOMPARE(secondCancelled.loadAcquire(), 1);
  QCOMPARE(thirdCancelled.loadAcquire(), 1);

  // Signal emitted exactly once with count=2.
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), nbId);
  QCOMPARE(spy.at(0).at(1).toInt(), 2);

  // Now let item 1 finish; it must complete normally.
  canFinish.release(1);
  QTRY_VERIFY_WITH_TIMEOUT(!mgr.isRunning(nbId), 5000);

  QCOMPARE(firstRan.loadAcquire(), 1);
  QCOMPARE(secondBodyRan.loadAcquire(), 0); // bodies never ran
  QCOMPARE(thirdBodyRan.loadAcquire(), 0);
  QVERIFY(!mgr.hasPending(nbId));
}

void TestSyncWorkQueueCancel::reentrant_enqueue_from_callback_does_not_deadlock() {
  vnotex::SyncWorkQueueManager mgr;
  const QString nbId = QStringLiteral("nb-reentrant");

  QSemaphore firstStarted;
  QSemaphore canFinish;
  QAtomicInt reentrantRan{0};

  // Item 1: blocks.
  (void)mgr.enqueue(nbId, [&]() {
    firstStarted.release(1);
    canFinish.acquire(1);
  });
  QVERIFY(firstStarted.tryAcquire(1, 5000));

  // Item 2: when cancelled, re-enqueues a new item for SAME notebook id.
  (void)mgr.enqueue(
      nbId, [&]() {},
      [&]() {
        // Re-entrant enqueue for the same notebook id.
        // Must not deadlock — callback runs outside m_mutex.
        (void)mgr.enqueue(nbId, [&]() { reentrantRan.fetchAndAddOrdered(1); });
      });

  // Cancel — should drop item 2 and trigger re-enqueue from its callback.
  const int dropped = mgr.cancelPending(nbId);
  QCOMPARE(dropped, 1);

  // Release first so the queue can drain (in-flight + reentrant item).
  canFinish.release(1);

  // The reentrant item must run after the first finishes.
  QTRY_VERIFY_WITH_TIMEOUT(reentrantRan.loadAcquire() == 1, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(!mgr.isRunning(nbId), 5000);
}

void TestSyncWorkQueueCancel::cancel_unknown_id_returns_zero_no_signal() {
  vnotex::SyncWorkQueueManager mgr;
  qRegisterMetaType<QString>("QString");

  QSignalSpy spy(&mgr, &vnotex::SyncWorkQueueManager::pendingCancelled);

  const int dropped = mgr.cancelPending(QStringLiteral("does-not-exist"));
  QCOMPARE(dropped, 0);
  QCOMPARE(spy.count(), 0);
}

void TestSyncWorkQueueCancel::cancel_after_shutdown_returns_zero_no_signal() {
  vnotex::SyncWorkQueueManager mgr;
  qRegisterMetaType<QString>("QString");
  const QString nbId = QStringLiteral("nb-postshutdown");

  // Enqueue a quick item and let it drain.
  QAtomicInt ran{0};
  (void)mgr.enqueue(nbId, [&]() { ran.fetchAndAddOrdered(1); });
  QTRY_VERIFY_WITH_TIMEOUT(!mgr.isRunning(nbId), 5000);

  // Shutdown clears queues.
  QVERIFY(mgr.shutdown(5000));

  QSignalSpy spy(&mgr, &vnotex::SyncWorkQueueManager::pendingCancelled);
  const int dropped = mgr.cancelPending(nbId);
  QCOMPARE(dropped, 0);
  QCOMPARE(spy.count(), 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorkQueueCancel)
#include "test_sync_workqueue_cancel.moc"
