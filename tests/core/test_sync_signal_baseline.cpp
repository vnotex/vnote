// Characterization test (T1 of sync-queue-convergence plan).
//
// Captures the CURRENT (pre-refactor) signal-emission behavior of a manual
// sync trigger. The intent is NOT to assert the desired post-Wave-2 contract
// but to lock down today's behavior so any regression during the refactor
// becomes a test failure. T23 will UPDATE these assertions when EventBridge
// becomes the single source of lifecycle signals.
//
// Baseline (per Metis review):
//   - SyncWorker::syncStarted   fires exactly once per triggerSyncNow.
//   - SyncWorker::syncFinished  fires exactly once per triggerSyncNow.
//   - EventBridge::syncStarted  does NOT fire for manual sync today
//     (vxcore SyncManager::TriggerSync does not emit lifecycle events;
//     only MaybeEnqueueSync does, which is the auto path).
//   - EventBridge::syncFinished does NOT fire for manual sync today.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/eventbridge.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
// T24: SyncWorker class deleted; this test now relies solely on EventBridge
// + SyncService re-emit signals to characterize the manual sync path.
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestSyncSignalBaseline : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void manualSyncSignalSequence();

private:
  // Mirror of TestSyncService::seedBareRepo — uses the git CLI to create a
  // bare repo and seed it with one commit so vxcore can clone/enable against
  // a file:// remote. Returns the file:// URL or empty on failure.
  QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);

  static int waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs);
};

void TestSyncSignalBaseline::initTestCase() {
  // CRITICAL: enable vxcore test mode BEFORE any context is created.
  vxcore_set_test_mode(1);
}

int TestSyncSignalBaseline::waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < p_timeoutMs) {
    if (!p_a.isEmpty()) {
      return 1;
    }
    if (!p_b.isEmpty()) {
      return 2;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(10);
  }
  if (!p_a.isEmpty()) {
    return 1;
  }
  if (!p_b.isEmpty()) {
    return 2;
  }
  return 0;
}

QString TestSyncSignalBaseline::seedBareRepo(const QString &p_bareRepoPath,
                                             TempDirFixture &p_workTemp) {
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("init"), QStringLiteral("--bare"),
                         QStringLiteral("--initial-branch=main"), p_bareRepoPath}) != 0) {
    QDir().rmpath(p_bareRepoPath);
    if (QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                  p_bareRepoPath}) != 0) {
      return QString();
    }
  }

  const QString workDir = p_workTemp.filePath(QStringLiteral("seed_work_") +
                                              QString::number(QDateTime::currentMSecsSinceEpoch()));
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("clone"), p_bareRepoPath, workDir}) != 0) {
    return QString();
  }

  QProcess::execute(QStringLiteral("git"),
                    {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                     QStringLiteral("user.email"), QStringLiteral("seed@example.com")});
  QProcess::execute(QStringLiteral("git"), {QStringLiteral("-C"), workDir, QStringLiteral("config"),
                                            QStringLiteral("user.name"), QStringLiteral("Seed")});

  QFile seed(workDir + QStringLiteral("/seed.md"));
  if (!seed.open(QIODevice::WriteOnly)) {
    return QString();
  }
  seed.write("# Seed\n");
  seed.close();

  if (QProcess::execute(
          QStringLiteral("git"),
          {QStringLiteral("-C"), workDir, QStringLiteral("add"), QStringLiteral("seed.md")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("commit"),
                         QStringLiteral("-m"), QStringLiteral("seed")}) != 0) {
    return QString();
  }
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("-C"), workDir, QStringLiteral("push"),
                         QStringLiteral("origin"), QStringLiteral("HEAD")}) != 0) {
    return QString();
  }

  QString normalized = QDir::fromNativeSeparators(p_bareRepoPath);
  if (!normalized.startsWith(QLatin1Char('/'))) {
    normalized.prepend(QLatin1Char('/'));
  }
  return QStringLiteral("file://") + normalized;
}

void TestSyncSignalBaseline::manualSyncSignalSequence() {
  // ---- Wire vxcore context + ServiceLocator -------------------------------
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  // Inner scope ensures EventBridge (and all other stack objects holding ctx)
  // are destroyed BEFORE vxcore_context_destroy runs at the end. Without this,
  // ~EventBridge calls vxcore_off_event against a freed context and crashes.
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    // T5: track UUID writes so cleanup runs before the inner scope closes
    // and vxcore_context_destroy is called below.
    tests::KeychainGuard guard(&credStore);
    // EventBridge subscribes to vxcore sync.* events on construction; if no
    // events are emitted (current baseline for manual sync), its Qt signals
    // stay silent.
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    SyncService syncService(services);

    // ---- Seed bare repo + create local notebook ------------------------------
    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    const QString bareDir = localTemp.filePath(QStringLiteral("baseline_remote.git"));
    const QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      QSKIP("git not available or bare-repo seeding failed");
    }

    const QString nbRoot = localTemp.filePath(QStringLiteral("nb_baseline"));
    QDir().mkpath(nbRoot);
    const QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"Baseline NB","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    // ---- Enable sync so a subsequent trigger has a registered notebook ------
    QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
    syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_BASELINE"));
    QVERIFY(enableSpy.wait(15000));
    QCOMPARE(enableSpy.count(), 1);

    const VxCoreError enableResult = qvariant_cast<VxCoreError>(enableSpy.first().at(1));
    if (enableResult == VXCORE_ERR_UNKNOWN) {
      credStore.deleteCredentials(nbId);
      QTest::qWait(500);
      guard.cleanup();
      QSKIP("OS keychain backend not usable in this test environment");
    }
    QCOMPARE(enableResult, VXCORE_OK);

    // Defensive: signal-based auto-track should catch this, but pin the id
    // explicitly so the post-test cleanup always runs.
    guard.track(nbId);

    // ---- Arm spies on BOTH signal sources before the manual trigger ----------
    // Worker signals fire on the worker thread and are delivered to QSignalSpy
    // via QueuedConnection (Qt handles this transparently). EventBridge signals
    // are emitted via QMetaObject::invokeMethod with QueuedConnection inside its
    // vxcore-callback handler.
    // T21: triggerSyncNow no longer dispatches through SyncWorker — it goes
    // through SyncWorkQueueManager + SyncOps directly. Observe the
    // SyncService-level signals instead (SyncService re-emits sync lifecycle
    // signals on the GUI thread after the queued work completes).
    QSignalSpy workerStartedSpy(&syncService, &SyncService::syncStarted);
    QSignalSpy workerFinishedSpy(&syncService, &SyncService::syncFinished);
    QSignalSpy bridgeStartedSpy(&eventBridge, &EventBridge::syncStarted);
    QSignalSpy bridgeFinishedSpy(&eventBridge, &EventBridge::syncFinished);

    // ---- Act: manual sync trigger -------------------------------------------
    syncService.triggerSyncNow(nbId);

    // Wait for SyncService::syncFinished — the canonical end-of-trigger signal
    // for manual sync. Generous timeout for libgit2 fetch/push of an empty
    // diff against a local bare remote.
    QVERIFY(workerFinishedSpy.wait(15000));

    // Drain any queued EventBridge invocations that might still be in flight,
    // so a sync.started/finished event emitted by vxcore would have been
    // delivered by now. processEvents on its own may exit before all queued
    // lambda invocations execute, so spin a few times.
    for (int i = 0; i < 5; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QTest::qWait(20);
    }

    // ---- Assert CURRENT behavior (NOT desired behavior) ----------------------
    qInfo() << "Baseline counts:"
            << "workerStarted=" << workerStartedSpy.count()
            << "workerFinished=" << workerFinishedSpy.count()
            << "bridgeStarted=" << bridgeStartedSpy.count()
            << "bridgeFinished=" << bridgeFinishedSpy.count();

    // Manual sync emits SyncWorker lifecycle signals exactly once each.
    // Manual path: SyncWorker emits exactly one started + finished.
    QCOMPARE(workerStartedSpy.count(), 1);
    QCOMPARE(workerFinishedSpy.count(), 1);
    QCOMPARE(workerStartedSpy.first().at(0).toString(), nbId);
    QCOMPARE(workerFinishedSpy.first().at(0).toString(), nbId);

    // V3 contract (commit 6887182e): SyncOps::triggerSync now drives
    // vxcore_sync_stage_only + vxcore_sync_network_phase, which deliberately
    // do NOT emit sync.started/sync.finished events. The SyncService
    // orchestrator owns lifecycle signals (see workerStartedSpy /
    // workerFinishedSpy assertions above which alias to SyncService signals
    // and DO still trigger).
    QCOMPARE(bridgeStartedSpy.count(), 0);
    QCOMPARE(bridgeFinishedSpy.count(), 0);

    // ---- Teardown -----------------------------------------------------------
    QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy delErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
    credStore.deleteCredentials(nbId);
    waitForEither(delSpy, delErrSpy, 5000);
    // T5: explicit guard cleanup before the inner scope closes (and
    // EventBridge / SyncService destruct against the still-live ctx).
    guard.cleanup();

  } // close inner scope so EventBridge destructs while ctx is still valid

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncSignalBaseline)
#include "test_sync_signal_baseline.moc"
