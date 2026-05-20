// T2 — Characterization test: current AUTO sync signal sequence.
//
// Goal: lock in CURRENT (pre-refactor) behavior of the auto-sync path
// triggered by a Buffer save through the vxcore WorkQueue/drain-thread path.
//
// Hypothesis being asserted (per Metis finding referenced by the plan):
//   * Auto-sync flows file.saved → SyncManager::MaybeEnqueueSync →
//     vxcore WorkQueue("sync") → WorkQueueDrainThread →
//     SyncManager::TriggerSync, which emits sync.started / sync.finished
//     via EventManager. EventBridge translates these to Qt signals.
//   * SyncWorker (Qt) is NOT involved on the auto path — its syncStarted /
//     syncFinished must remain at zero.
//
// This test is a REGRESSION DETECTOR for Wave 2 (T7/T9). If a future change
// causes SyncWorker to fire on the auto path, OR causes EventBridge to stop
// firing, this test fails and forces a deliberate update.
//
// NOTE: tests under tests/core/ run with PATH prepended per
// AGENTS.md "Running execs that depend on VTextEdit.dll" — irrelevant here
// (GUILESS), but documented for orientation.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/eventbridge.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworker.h>
#include <core/services/workqueuedrainthread.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestSyncSignalAutoBaseline : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void autoSyncEmitsViaEventBridgeOnly();

private:
  // Initialize a bare git repo and seed it with one commit so the GitSyncBackend
  // can fetch/clone it. Returns the file:// URL, or empty on failure.
  static QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestSyncSignalAutoBaseline::initTestCase() {
  // CRITICAL: must run BEFORE vxcore_context_create. Without this, vxcore
  // points at real AppData and corrupts user state.
  vxcore_set_test_mode(1);
}

void TestSyncSignalAutoBaseline::cleanupTestCase() {}

QString TestSyncSignalAutoBaseline::seedBareRepo(const QString &p_bareRepoPath,
                                                 TempDirFixture &p_workTemp) {
  // Try with explicit initial branch (newer git); fall back without if it fails.
  if (QProcess::execute(QStringLiteral("git"),
                        {QStringLiteral("init"), QStringLiteral("--bare"),
                         QStringLiteral("--initial-branch=main"), p_bareRepoPath}) != 0) {
    QDir().rmpath(p_bareRepoPath);
    if (QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                  p_bareRepoPath}) != 0) {
      return QString();
    }
  }

  QString workDir = p_workTemp.filePath(QStringLiteral("seed_work_") +
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

void TestSyncSignalAutoBaseline::autoSyncEmitsViaEventBridgeOnly() {
  // ---- Context + ServiceLocator wiring ---------------------------------------
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  // Scope all Qt objects that hold ctx so they destruct BEFORE
  // vxcore_context_destroy. Otherwise EventBridge's dtor calls
  // vxcore_off_event on an already-freed context and crashes.
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    BufferService bufferService(ctx, &hookMgr, AutoSavePolicy::None);
    SyncService syncService(services);

    // ---- Seed bare repo + create notebook --------------------------------------
    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
    QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      vxcore_context_destroy(ctx);
      QSKIP("git not available or bare-repo seeding failed");
    }

    QString nbRoot = localTemp.filePath(QStringLiteral("nb_root"));
    QDir().mkpath(nbRoot);
    QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"Auto Sync NB","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    // ---- Spies on BOTH potential signal sources --------------------------------
    // The test asserts which side fires today. Per the plan, only EventBridge
    // should fire on the auto path.
    QSignalSpy bridgeStartedSpy(&eventBridge, &EventBridge::syncStarted);
    QSignalSpy bridgeFinishedSpy(&eventBridge, &EventBridge::syncFinished);
    QSignalSpy bridgeConflictSpy(&eventBridge, &EventBridge::syncConflict);

    SyncWorker *worker = syncService.worker();
    QVERIFY(worker != nullptr);
    QSignalSpy workerStartedSpy(worker, &SyncWorker::syncStarted);
    QSignalSpy workerFinishedSpy(worker, &SyncWorker::syncFinished);
    QSignalSpy workerFailedSpy(worker, &SyncWorker::syncFailed);
    QSignalSpy workerConflictsSpy(worker, &SyncWorker::conflictsDetected);

    // ---- Enable sync ----------------------------------------------------------
    // Note: NotebookConfig::sync_interval_seconds default is 60s. We can't easily
    // override that through SyncService::enableSyncForNotebook (it always builds
    // {"backend":"git","remoteUrl":...} without intervalSeconds), but vxcore's
    // MaybeEnqueueSync debounce uses `now - last < interval`, and `last` is the
    // default time_point (zero) on the FIRST event, so `now - 0 = now` is much
    // larger than any interval. The first file.saved therefore passes the gate
    // regardless. We document this in the notepad — the plan's "intervalSeconds=0"
    // hint is wrong because vxcore explicitly skips when interval_seconds <= 0
    // (sync_manager.cpp:100).
    QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
    syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_T2_BASELINE"));
    QVERIFY2(enableSpy.wait(15000), "enableFinished did not arrive within 15s");
    // enableFinished payload: (notebookId, VxCoreError, message).
    QCOMPARE(enableSpy.first().at(1).toInt(), static_cast<int>(VXCORE_OK));

    // Enabling sync ran an INITIAL sync via the worker. Reset worker spies so we
    // measure ONLY the auto path that follows. EventBridge spies are also reset
    // because Wave 2 of the refactor will change which side fires on the manual
    // path; T1 covers that and we keep T2 strictly about the AUTO path.
    workerStartedSpy.clear();
    workerFinishedSpy.clear();
    workerFailedSpy.clear();
    workerConflictsSpy.clear();
    bridgeStartedSpy.clear();
    bridgeFinishedSpy.clear();
    bridgeConflictSpy.clear();

    // ---- Start the drain thread that consumes WorkQueue("sync") ----------------
    // Without this, MaybeEnqueueSync enqueues a lambda that never runs.
    WorkQueueDrainThread drain(ctx);
    drain.start();

    // ---- Trigger auto-sync by saving a buffer ----------------------------------
    // BufferService::openBuffer -> BufferCoreService::openBuffer (vxcore).
    // Buffer2::save -> BufferCoreService::saveBuffer -> vxcore buffer_manager,
    // which Emit()s events::kFileSaved. SyncManager subscribes to that event in
    // its mark_dirty lambda and invokes MaybeEnqueueSync.
    QString fileId = notebookService.createFile(nbId, QString(), QStringLiteral("auto.md"));
    QVERIFY(!fileId.isEmpty());
    Buffer2 buf = bufferService.openBuffer(NodeIdentifier{nbId, QStringLiteral("auto.md")});
    QVERIFY(buf.isValid());
    QVERIFY(buf.setContentRaw(QByteArray("auto sync trigger\n")));
    QVERIFY(buf.save());

    // ---- Wait for the auto-sync round trip -------------------------------------
    // Allow up to 15s: file.saved → MaybeEnqueueSync → WorkQueue.Enqueue
    //   → drain thread poll (≤500ms) → vxcore TriggerSync → libgit2
    //   stage/commit/fetch/push (seconds) → EventBridge::syncFinished.
    const int autoSyncTimeoutMs = 15000;
    QElapsedTimer t;
    t.start();
    while (bridgeFinishedSpy.isEmpty() && t.elapsed() < autoSyncTimeoutMs) {
      // Pump the EventBridge's QueuedConnection emissions onto this thread.
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }

    // ---- Stop the drain thread BEFORE asserting --------------------------------
    // requestStop() also shuts down the WorkQueue so process_next returns.
    drain.requestStop();
    QVERIFY2(drain.wait(5000), "WorkQueueDrainThread did not stop within 5s");

    // ---- Assertions: characterize the CURRENT (pre-refactor) emission ----------
    // EventBridge fires exactly once for started + finished on the auto path.
    QCOMPARE(bridgeStartedSpy.count(), 1);
    QCOMPARE(bridgeFinishedSpy.count(), 1);

    // SyncWorker is NOT involved in the auto path today.
    QCOMPARE(workerStartedSpy.count(), 0);
    QCOMPARE(workerFinishedSpy.count(), 0);
    QCOMPARE(workerFailedSpy.count(), 0);
    QCOMPARE(workerConflictsSpy.count(), 0);

    // Bridge finished should report VXCORE_OK (push to a seeded bare repo).
    const VxCoreError finishedResult =
        static_cast<VxCoreError>(bridgeFinishedSpy.first().at(1).toInt());
    QCOMPARE(finishedResult, VXCORE_OK);

    // ---- Tear down -------------------------------------------------------------
    syncService.shutdown();
  } // close ctx-holders scope before destroying the context
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncSignalAutoBaseline)
#include "test_sync_signal_auto_baseline.moc"
