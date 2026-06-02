#include <QAtomicInt>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QtTest>

#include <thread>

#include <core/services/isyncnotebookservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncops.h>

namespace tests {

// Fake ISyncNotebookService — sleeps in each phase so we can observe gate
// hold windows from the outside via contention timing.
class FakeSyncNotebookService : public vnotex::ISyncNotebookService {
public:
  VxCoreError syncStageOnly(const QString &p_notebookId,
                            VxCoreSyncCancellation *p_cancellationToken,
                            bool *p_didCommit) override {
    Q_UNUSED(p_notebookId);
    Q_UNUSED(p_cancellationToken);
    m_stageEntered.storeRelaxed(1);
    QThread::msleep(50);
    if (p_didCommit) {
      *p_didCommit = true;
    }
    m_stageExited.storeRelaxed(1);
    return VXCORE_OK;
  }

  VxCoreError syncNetworkPhase(const QString &p_notebookId,
                               VxCoreSyncCancellation *p_cancellationToken) override {
    Q_UNUSED(p_notebookId);
    Q_UNUSED(p_cancellationToken);
    m_networkEntered.storeRelaxed(1);
    QThread::msleep(200);
    m_networkExited.storeRelaxed(1);
    return VXCORE_OK;
  }

  QAtomicInt m_stageEntered{0};
  QAtomicInt m_stageExited{0};
  QAtomicInt m_networkEntered{0};
  QAtomicInt m_networkExited{0};
};

class TestSyncOpsGateRelease : public QObject {
  Q_OBJECT

private slots:
  void testGateReleasedBeforeNetworkPhase();
  void testStagedPatternCompletesSuccessfully();
};

void TestSyncOpsGateRelease::testGateReleasedBeforeNetworkPhase() {
  FakeSyncNotebookService fake;
  vnotex::NotebookIoGate gate;
  const QString notebookId = QStringLiteral("nb-1");

  QSemaphore finishedSem(0);
  QAtomicInt finishedCode{-1};
  auto onFinished = [&](VxCoreError code) {
    finishedCode.storeRelaxed(static_cast<int>(code));
    finishedSem.release();
  };

  // 1. Hold the gate on the main thread.
  auto mainLock = std::make_unique<vnotex::NotebookIoGate::ScopedLock>(gate, notebookId);

  // 2. Dispatch triggerSync on a worker. It will block in stage acquiring the gate.
  std::thread worker(
      [&]() { vnotex::SyncOps::triggerSync(&fake, notebookId, nullptr, onFinished, &gate); });

  // 3. Sleep so the worker is definitely blocked in gate acquire.
  QThread::msleep(100);
  QVERIFY2(fake.m_stageEntered.loadRelaxed() == 0,
           "Worker should still be blocked on gate acquire (stage not entered)");

  // 4. Release main's lock so worker proceeds.
  QElapsedTimer sinceRelease;
  sinceRelease.start();
  mainLock.reset();

  // 5. Wait long enough for worker to enter (and likely exit) stage, then
  //    be inside the network phase (network sleeps 200ms).
  //    Stage = 50ms. After ~120ms post-release we should be solidly in the
  //    network phase.
  QThread::msleep(120);
  QVERIFY2(fake.m_networkEntered.loadRelaxed() == 1, "Worker should be in network phase by now");
  QVERIFY2(fake.m_networkExited.loadRelaxed() == 0,
           "Worker should NOT have finished network phase yet (sleeps 200ms)");

  // 6. KEY ASSERTION: try to re-acquire the gate from the main thread.
  //    If the gate were still held during network phase, this would block
  //    ~80ms (remaining network time). If released, it's essentially instant.
  QElapsedTimer acquireTimer;
  acquireTimer.start();
  {
    vnotex::NotebookIoGate::ScopedLock probe(gate, notebookId);
    const qint64 acquireMs = acquireTimer.elapsed();
    QVERIFY2(acquireMs < 30,
             qPrintable(QStringLiteral("Gate re-acquire took %1ms; expected <30ms "
                                       "(gate should be released during network phase)")
                            .arg(acquireMs)));
  }

  // 7. Wait for worker completion.
  QVERIFY(finishedSem.tryAcquire(1, 5000));
  QCOMPARE(static_cast<VxCoreError>(finishedCode.loadRelaxed()), VXCORE_OK);
  worker.join();
}

void TestSyncOpsGateRelease::testStagedPatternCompletesSuccessfully() {
  FakeSyncNotebookService fake;
  vnotex::NotebookIoGate gate;
  const QString notebookId = QStringLiteral("nb-2");

  QSemaphore finishedSem(0);
  QAtomicInt finishedCode{-1};
  auto onFinished = [&](VxCoreError code) {
    finishedCode.storeRelaxed(static_cast<int>(code));
    finishedSem.release();
  };

  QElapsedTimer timer;
  timer.start();
  std::thread worker(
      [&]() { vnotex::SyncOps::triggerSync(&fake, notebookId, nullptr, onFinished, &gate); });

  QVERIFY(finishedSem.tryAcquire(1, 5000));
  const qint64 elapsed = timer.elapsed();
  worker.join();

  QCOMPARE(static_cast<VxCoreError>(finishedCode.loadRelaxed()), VXCORE_OK);
  QCOMPARE(fake.m_stageExited.loadRelaxed(), 1);
  QCOMPARE(fake.m_networkExited.loadRelaxed(), 1);

  // Stage 50ms + network 200ms = ~250ms; allow generous CI headroom.
  QVERIFY2(elapsed >= 240, qPrintable(QStringLiteral("Expected >=240ms, got %1ms").arg(elapsed)));
  QVERIFY2(elapsed < 600, qPrintable(QStringLiteral("Expected <600ms, got %1ms").arg(elapsed)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncOpsGateRelease)
#include "test_syncops_gate_release.moc"
