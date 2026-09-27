#include <QSemaphore>
#include <QtTest>

#include <thread>

#include <core/services/isyncnotebookservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/syncops.h>
#include <sync/sync_backend.h>

namespace tests {

// Hold each phase until the test has observed whether another save could acquire
// the notebook gate. Scheduling delays must not change the observation window.
class FakeSyncNotebookService : public vnotex::ISyncNotebookService {
public:
  VxCoreError syncCapabilities(const QString &, uint32_t *p_capabilities) override {
    if (!p_capabilities) {
      return VXCORE_ERR_NULL_POINTER;
    }
    *p_capabilities = m_capabilities;
    return VXCORE_OK;
  }

  VxCoreError syncApplyPhase(const QString &, VxCoreSyncCancellation *, const QStringList &,
                             QStringList *p_changedPaths) override {
    if (!p_changedPaths) {
      return VXCORE_ERR_NULL_POINTER;
    }
    p_changedPaths->clear();
    return VXCORE_ERR_NOT_IMPLEMENTED;
  }
  VxCoreError syncStageOnly(const QString &p_notebookId,
                            VxCoreSyncCancellation *p_cancellationToken,
                            bool *p_didCommit) override {
    Q_UNUSED(p_notebookId);
    Q_UNUSED(p_cancellationToken);
    m_stageEntered.release();
    m_continueStage.acquire();
    if (p_didCommit) {
      *p_didCommit = true;
    }
    return VXCORE_OK;
  }

  VxCoreError syncNetworkPhase(const QString &p_notebookId,
                               VxCoreSyncCancellation *p_cancellationToken) override {
    Q_UNUSED(p_notebookId);
    Q_UNUSED(p_cancellationToken);
    m_networkEntered.release();
    m_continueNetwork.acquire();
    return VXCORE_OK;
  }

  QSemaphore m_stageEntered;
  uint32_t m_capabilities = 0;
  QSemaphore m_continueStage;
  QSemaphore m_networkEntered;
  QSemaphore m_continueNetwork;
};

class TestSyncOpsGateRelease : public QObject {
  Q_OBJECT

private slots:
  void testGateReleasedBeforeNetworkPhase();
  void testDeferredApplyRequiresCallback();
  void testDeferredApplyFailureReachesCompletionOutsideGate();
};

void TestSyncOpsGateRelease::testGateReleasedBeforeNetworkPhase() {
  FakeSyncNotebookService fake;
  vnotex::NotebookIoGate gate;
  const QString notebookId = QStringLiteral("nb-1");
  VxCoreError finishedCode = VXCORE_ERR_UNKNOWN;
  int finishedCount = 0;
  std::thread worker([&]() {
    vnotex::SyncOps::triggerSync(
        &fake, notebookId, nullptr,
        [&](VxCoreError p_code) {
          finishedCode = p_code;
          ++finishedCount;
        },
        &gate);
  });

  const bool stageEntered = fake.m_stageEntered.tryAcquire(1, 5000);
  bool gateHeldDuringStage = false;
  if (stageEntered) {
    vnotex::NotebookIoGate::ScopedTryLock probe(gate, notebookId, 0);
    gateHeldDuringStage = !probe.isLocked();
  }
  fake.m_continueStage.release();

  const bool networkEntered = fake.m_networkEntered.tryAcquire(1, 5000);
  bool gateReleasedDuringNetwork = false;
  if (networkEntered) {
    vnotex::NotebookIoGate::ScopedTryLock probe(gate, notebookId, 0);
    gateReleasedDuringNetwork = probe.isLocked();
  }
  fake.m_continueNetwork.release();

  // Join before asserting so a failed observation cannot destroy a joinable
  // std::thread and abort the test process instead of reporting the failure.
  worker.join();
  QVERIFY2(stageEntered, "Worker did not enter the staging phase");
  QVERIFY2(gateHeldDuringStage, "Staging must exclude concurrent notebook saves");
  QVERIFY2(networkEntered, "Worker did not enter the network phase");
  QVERIFY2(gateReleasedDuringNetwork, "Network work must not block notebook saves");
  QCOMPARE(finishedCode, VXCORE_OK);
  QCOMPARE(finishedCount, 1);
}

void TestSyncOpsGateRelease::testDeferredApplyRequiresCallback() {
  FakeSyncNotebookService fake;
  fake.m_capabilities = static_cast<uint32_t>(vxcore::SyncCapability::DeferredLocalApply);
  // A broken guard must fail the assertions, not hang inside the fake phases.
  fake.m_continueStage.release();
  fake.m_continueNetwork.release();
  VxCoreError result = VXCORE_OK;
  vnotex::SyncOps::triggerSync(&fake, QStringLiteral("deferred"), nullptr,
                               [&](VxCoreError p_result) { result = p_result; });
  QCOMPARE(result, VXCORE_ERR_NOT_IMPLEMENTED);
  QCOMPARE(fake.m_stageEntered.available(), 0);
  QCOMPARE(fake.m_networkEntered.available(), 0);
}

void TestSyncOpsGateRelease::testDeferredApplyFailureReachesCompletionOutsideGate() {
  FakeSyncNotebookService fake;
  fake.m_capabilities = static_cast<uint32_t>(vxcore::SyncCapability::DeferredLocalApply);
  fake.m_continueStage.release();
  fake.m_continueNetwork.release();
  vnotex::NotebookIoGate gate;
  const auto notebookId = QStringLiteral("deferred");
  VxCoreError result = VXCORE_OK;
  bool applied = false;
  bool gateAvailable = false;
  std::thread worker([&] {
    vnotex::SyncOps::triggerSync(
        &fake, notebookId, nullptr, [&](VxCoreError p_result) { result = p_result; }, &gate,
        [&] {
          applied = true;
          vnotex::NotebookIoGate::ScopedTryLock probe(gate, notebookId, 0);
          gateAvailable = probe.isLocked();
          return VXCORE_ERR_IO;
        });
  });
  worker.join();
  QVERIFY(applied);
  QVERIFY(gateAvailable);
  QCOMPARE(result, VXCORE_ERR_IO);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncOpsGateRelease)
#include "test_syncops_gate_release.moc"
