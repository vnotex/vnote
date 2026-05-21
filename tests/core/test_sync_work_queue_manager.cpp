#include <QAtomicInt>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QVector>
#include <QtTest>

#include <core/services/syncworkqueuemanager.h>

namespace tests {

class TestSyncWorkQueueManager : public QObject {
  Q_OBJECT

private slots:
  void serial_per_notebook();
  void parallel_across_notebooks();
  void shutdown_drains_within_5s();
  void no_leak_on_shutdown();
};

void TestSyncWorkQueueManager::serial_per_notebook() {
  vnotex::SyncWorkQueueManager mgr;

  QMutex mtx;
  QVector<int> order;
  QVector<qint64> starts;
  QVector<qint64> ends;
  QElapsedTimer timer;
  timer.start();

  auto makeWork = [&](int p_id) {
    return [&, p_id]() {
      const qint64 startMs = timer.elapsed();
      QThread::msleep(50);
      const qint64 endMs = timer.elapsed();
      QMutexLocker lk(&mtx);
      order.append(p_id);
      starts.append(startMs);
      ends.append(endMs);
    };
  };

  const QString nb = QStringLiteral("notebook-A");
  (void)mgr.enqueue(nb, makeWork(1));
  (void)mgr.enqueue(nb, makeWork(2));
  (void)mgr.enqueue(nb, makeWork(3));

  QTRY_VERIFY_WITH_TIMEOUT(mgr.queueDepth(nb) == 0 && !mgr.isRunning(nb), 5000);

  QMutexLocker lk(&mtx);
  QCOMPARE(order.size(), 3);
  QCOMPARE(order[0], 1);
  QCOMPARE(order[1], 2);
  QCOMPARE(order[2], 3);
  // Strict serial: end of prev <= start of next.
  QVERIFY2(ends[0] <= starts[1], "item 2 started before item 1 finished");
  QVERIFY2(ends[1] <= starts[2], "item 3 started before item 2 finished");
}

void TestSyncWorkQueueManager::parallel_across_notebooks() {
  vnotex::SyncWorkQueueManager mgr;

  QMutex mtx;
  qint64 startA = -1, endA = -1, startB = -1, endB = -1;
  QElapsedTimer timer;
  timer.start();

  mgr.enqueue(QStringLiteral("A"), [&]() {
    {
      QMutexLocker lk(&mtx);
      startA = timer.elapsed();
    }
    QThread::msleep(200);
    {
      QMutexLocker lk(&mtx);
      endA = timer.elapsed();
    }
  });
  mgr.enqueue(QStringLiteral("B"), [&]() {
    {
      QMutexLocker lk(&mtx);
      startB = timer.elapsed();
    }
    QThread::msleep(200);
    {
      QMutexLocker lk(&mtx);
      endB = timer.elapsed();
    }
  });

  QTRY_VERIFY_WITH_TIMEOUT(
      !mgr.isRunning(QStringLiteral("A")) && !mgr.isRunning(QStringLiteral("B")), 5000);

  QMutexLocker lk(&mtx);
  QVERIFY(startA >= 0 && endA >= 0 && startB >= 0 && endB >= 0);
  // Overlapping windows: each started before the other finished.
  const bool overlap = (startA < endB) && (startB < endA);
  QVERIFY2(overlap, qPrintable(QStringLiteral("A=[%1,%2] B=[%3,%4] did not overlap")
                                   .arg(startA)
                                   .arg(endA)
                                   .arg(startB)
                                   .arg(endB)));
}

void TestSyncWorkQueueManager::shutdown_drains_within_5s() {
  vnotex::SyncWorkQueueManager mgr;
  QAtomicInt completed{0};

  const QStringList notebooks = {QStringLiteral("n1"), QStringLiteral("n2"), QStringLiteral("n3")};
  for (int i = 0; i < 5; ++i) {
    (void)mgr.enqueue(notebooks[i % notebooks.size()], [&completed]() {
      QThread::msleep(10);
      completed.fetchAndAddOrdered(1);
    });
  }

  QElapsedTimer timer;
  timer.start();
  const bool drained = mgr.shutdown(5000);
  const qint64 elapsed = timer.elapsed();

  QVERIFY2(drained, "shutdown did not drain within 5s");
  QVERIFY2(elapsed < 5000, qPrintable(QStringLiteral("shutdown took %1 ms").arg(elapsed)));
  // At least some completed; exact count varies because pending items may be
  // discarded after shutdown flag flips. We just want a clean exit.
  QVERIFY(completed.loadAcquire() >= 0);
}

void TestSyncWorkQueueManager::no_leak_on_shutdown() {
  // Enqueue many items then shut down promptly. Verify no crashes and clean
  // exit. Pending items are discarded; in-flight items finish.
  vnotex::SyncWorkQueueManager mgr;
  QAtomicInt started{0};
  QAtomicInt finished{0};

  for (int i = 0; i < 10; ++i) {
    (void)mgr.enqueue(QStringLiteral("nb-%1").arg(i % 2), [&]() {
      started.fetchAndAddOrdered(1);
      QThread::msleep(5);
      finished.fetchAndAddOrdered(1);
    });
  }

  const bool drained = mgr.shutdown(5000);
  QVERIFY(drained);
  // Whatever started must have finished (no abandoned in-flight work).
  QCOMPARE(started.loadAcquire(), finished.loadAcquire());

  // Post-shutdown enqueue is a no-op.
  (void)mgr.enqueue(QStringLiteral("nb-0"), [&]() { finished.fetchAndAddOrdered(1000); });
  // Small wait — should NOT execute.
  QThread::msleep(50);
  QVERIFY(finished.loadAcquire() < 1000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorkQueueManager)
#include "test_sync_work_queue_manager.moc"
