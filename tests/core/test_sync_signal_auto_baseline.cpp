// T2 — Characterization test: current AUTO sync signal sequence.
//
// Goal: lock in CURRENT (pre-refactor) behavior of the auto-sync path
// triggered by a Buffer save through the vxcore WorkQueue/drain-thread path.
//
// Hypothesis being asserted (per Metis finding referenced by the plan):
//   * Auto-sync flows file.saved → SyncManager::MaybeEnqueueSync →
//     (T9: emit sync.should_run) → consumer subscribes and drives
//     SyncManager::TriggerSync, which emits sync.started / sync.finished
//     via EventManager. EventBridge translates these to Qt signals.
//   * SyncWorker (Qt) is NOT involved on the auto path — its syncStarted /
//     syncFinished must remain at zero.
//
// T9 update (sync-queue-convergence): MaybeEnqueueSync no longer enqueues
// a job into WorkQueue("sync"). It emits a sync.should_run event instead.
// The downstream Qt auto-route consumer (T31, future) will subscribe and
// drive TriggerSync. Until that consumer lands, this test installs a local
// vxcore_on_event subscriber that calls vxcore_sync_trigger to mimic the
// post-T31 behavior, so the started/finished assertions still hold.
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
#include <QJsonDocument>
#include <QJsonObject>
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
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

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
    // T5: track every UUID-based notebook write so cleanup runs before the
    // vxcore context is torn down. Defensive track() after enable below.
    tests::KeychainGuard guard(&credStore);
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

    // T24: SyncWorker class deleted. Worker-side spies are no longer
    // meaningful; the EventBridge spies alone characterize the auto path.

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

    // Defensive: track the id even though credentialsStored should auto-track.
    guard.track(nbId);

    // Enabling sync ran an INITIAL sync via SyncOps. Reset bridge spies so we
    // measure ONLY the auto path that follows.
    bridgeStartedSpy.clear();
    bridgeFinishedSpy.clear();
    bridgeConflictSpy.clear();

    // T9 shim: subscribe to sync.should_run and call vxcore_sync_trigger
    // synchronously. This mimics what the future Qt auto-route consumer will
    // do (T31) so EventBridge::syncStarted/Finished still fire on the auto
    // path. The callback runs on whatever thread emits sync.should_run; in
    // this test that is the test main thread (buffer save is synchronous).
    struct AutoRouteCtx {
      VxCoreContextHandle ctx;
    };
    AutoRouteCtx autoRouteCtx{ctx};
    auto autoRouteCb = [](const char *, const char *json_data, void *userdata) {
      auto *uc = static_cast<AutoRouteCtx *>(userdata);
      QJsonParseError parseErr;
      QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data), &parseErr);
      if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
        return;
      QJsonValue v = doc.object().value(QStringLiteral("notebookId"));
      if (!v.isString())
        return;
      QByteArray nb = v.toString().toUtf8();
      vxcore_sync_trigger(uc->ctx, nb.constData());
    };
    QCOMPARE(vxcore_on_event(ctx, "sync.should_run", autoRouteCb, &autoRouteCtx), VXCORE_OK);

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
    // Allow up to 15s: file.saved → MaybeEnqueueSync → sync.should_run event
    //   → vxcore TriggerSync → libgit2 stage/commit/fetch/push (seconds)
    //   → EventBridge::syncFinished.
    //
    // Updated 1 → 3 after the vxcore-metadata-events plan:
    // SaveFolderConfig now emits folder.config_changed (T4 persistence event)
    // which drives mark_dirty → sync.should_run. Each of the three file
    // operations below produces one extra sync round-trip:
    //   1. createFile("auto.md")            → folder.config_changed → 1st sync
    //   2. openBuffer → RecordFileOpen      → folder.config_changed → 2nd sync
    //      (UpdateFileMetadata writes "last_opened_*" via SaveFolderConfig)
    //   3. buf.save()                       → file.saved tail        → 3rd sync
    // The auto-route shim drives vxcore_sync_trigger synchronously for each
    // sync.should_run, so the bridge spies see started+finished × 3.
    const int autoSyncTimeoutMs = 15000;
    const int expectedRoundTrips = 3;
    QElapsedTimer t;
    t.start();
    while (bridgeFinishedSpy.count() < expectedRoundTrips && t.elapsed() < autoSyncTimeoutMs) {
      // Pump the EventBridge's QueuedConnection emissions onto this thread.
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }

    // ---- Assertions: characterize the CURRENT (post-metadata-events) emission --
    // EventBridge fires started + finished once per sync.should_run hop. See
    // the comment above the wait loop for the 1 → 3 derivation.
    QCOMPARE(bridgeStartedSpy.count(), expectedRoundTrips);
    QCOMPARE(bridgeFinishedSpy.count(), expectedRoundTrips);

    // T24: SyncWorker is gone; no worker-side spies to assert against.

    // Bridge finished should report VXCORE_OK for each sync round-trip.
    for (int i = 0; i < expectedRoundTrips; ++i) {
      const VxCoreError finishedResult =
          static_cast<VxCoreError>(bridgeFinishedSpy.at(i).at(1).toInt());
      QCOMPARE(finishedResult, VXCORE_OK);
    }

    // ---- Tear down -------------------------------------------------------------
    syncService.shutdown();
    // T5: cleanup keychain BEFORE the ctx-holders scope closes, so the guard
    // talks to a live SyncCredentialsStore and finishes its delete-and-wait
    // before vxcore_context_destroy runs below.
    guard.cleanup();
  } // close ctx-holders scope before destroying the context
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncSignalAutoBaseline)
#include "test_sync_signal_auto_baseline.moc"
