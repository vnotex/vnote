// T31 — Auto-sync route end-to-end test.
//
// Verifies the loop:
//   vxcore SyncManager::MaybeEnqueueSync -> emits sync.should_run
//   -> EventBridge::syncShouldRun
//   -> SyncService::onSyncShouldRun
//   -> SyncWorkQueueManager::enqueue(triggerSync)
//   -> vxcore TriggerSync -> EventBridge::syncFinished -> SyncService::syncFinished.

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
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "core/context.h"
#include "core/event_manager.h"
#include "core/event_names.h"

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestSyncAutoRoute : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void test_auto_route_full_roundtrip();
  void test_auto_route_silent_on_queue_full();
  void test_auto_route_disabled_bail();

private:
  static QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
  static void emitShouldRun(VxCoreContextHandle ctx, const QString &nbId);
};

void TestSyncAutoRoute::initTestCase() { vxcore_set_test_mode(1); }

QString TestSyncAutoRoute::seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp) {
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

void TestSyncAutoRoute::emitShouldRun(VxCoreContextHandle ctx, const QString &nbId) {
  auto *evtMgr = reinterpret_cast<vxcore::VxCoreContext *>(ctx)->event_manager.get();
  evtMgr->Emit(vxcore::events::kSyncShouldRun, {{"notebookId", nbId.toStdString()}});
}

void TestSyncAutoRoute::test_auto_route_full_roundtrip() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    // T5: track UUID writes from bootstrapAndPersist below.
    tests::KeychainGuard guard(&credStore);
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wqMgr;
    services.registerService<SyncWorkQueueManager>(&wqMgr);
    BufferService bufferService(ctx, &hookMgr, AutoSavePolicy::None);
    SyncService syncService(services);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
    QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      guard.cleanup();
      vxcore_context_destroy(ctx);
      QSKIP("git not available or bare-repo seeding failed");
    }

    QString nbRoot = localTemp.filePath(QStringLiteral("nb_root"));
    QDir().mkpath(nbRoot);
    QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"AR","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    QSignalSpy enableSpy(&syncService, &SyncService::bootstrapAndPersistFinished);
    syncService.bootstrapAndPersist(nbId, remoteUrl, QStringLiteral("ghp_TEST_T31"));
    QVERIFY2(enableSpy.wait(20000), "bootstrapAndPersistFinished did not arrive within 20s");
    QCOMPARE(enableSpy.first().at(1).toInt(), static_cast<int>(VXCORE_OK));

    // Defensive track in case credentialsStored signal raced cleanup.
    guard.track(nbId);

    // Wait for the initial sync (kicked off by enable) to drain.
    QSignalSpy finishedSpy(&syncService, &SyncService::syncFinished);
    QElapsedTimer t;
    t.start();
    while (finishedSpy.isEmpty() && t.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }
    finishedSpy.clear();

    // Trigger auto-sync by saving a buffer (vxcore emits sync.should_run).
    QString fileId = notebookService.createFile(nbId, QString(), QStringLiteral("auto.md"));
    QVERIFY(!fileId.isEmpty());
    Buffer2 buf = bufferService.openBuffer(NodeIdentifier{nbId, QStringLiteral("auto.md")});
    QVERIFY(buf.isValid());
    QVERIFY(buf.setContentRaw(QByteArray("auto sync trigger\n")));
    QVERIFY(buf.save());

    t.restart();
    while (finishedSpy.isEmpty() && t.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }
    // Post vxcore-metadata-events plan: createFile / openBuffer / save each
    // fire folder.config_changed (T4 persistence event) → mark_dirty →
    // sync.should_run, so the auto-route enqueues up to 3 triggerSync items.
    // SyncWorkQueueManager coalesces concurrent requests on coalesceKey
    // "trigger", so the actual finishedSpy count is timing-dependent (1, 2,
    // or 3 depending on which syncs collapse into the in-flight one).
    // For this end-to-end test we only verify that AT LEAST ONE sync round
    // trip completed via the SyncService auto-route — the exact count is
    // covered by test_sync_signal_auto_baseline which drives vxcore_sync_trigger
    // directly (no coalescing).
    QVERIFY(finishedSpy.count() >= 1);
    QCOMPARE(finishedSpy.first().at(0).toString(), nbId);

    syncService.shutdown();
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

void TestSyncAutoRoute::test_auto_route_silent_on_queue_full() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wqMgr;
    services.registerService<SyncWorkQueueManager>(&wqMgr);
    SyncService syncService(services);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());
    QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
    QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      guard.cleanup();
      vxcore_context_destroy(ctx);
      QSKIP("git not available or bare-repo seeding failed");
    }
    QString nbRoot = localTemp.filePath(QStringLiteral("nb_root"));
    QDir().mkpath(nbRoot);
    QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"AR","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    QSignalSpy enableSpy(&syncService, &SyncService::bootstrapAndPersistFinished);
    syncService.bootstrapAndPersist(nbId, remoteUrl, QStringLiteral("ghp_T31_QF"));
    QVERIFY2(enableSpy.wait(20000), "bootstrapAndPersistFinished did not arrive within 20s");

    guard.track(nbId);

    // Drain any initial sync.
    QTest::qWait(500);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    // Wait for any in-flight work from bootstrap to finish, then tighten the cap.
    QElapsedTimer drainSetup;
    drainSetup.start();
    while ((wqMgr.queueDepth(nbId) > 0 || wqMgr.isRunning(nbId)) && drainSetup.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }
    wqMgr.setMaxDepth(1);

    // Install a long-running blocker as the current item. We enqueue directly
    // for the same notebookId; with maxDepth=1 the next two sync.should_run
    // emissions must result in 0 enqueues beyond the blocker (one slot
    // fills with the first should_run, second is QueueFull -> silent).
    QSignalSpy failedSpy(&syncService, &SyncService::syncFailed);

    // Fire 2 sync.should_run events while a blocker is busy.
    QMutex blockerMutex;
    QWaitCondition blockerCond;
    bool blockerRunning = false;
    bool releaseBlocker = false;
    wqMgr.enqueue(nbId, [&]() {
      QMutexLocker lk(&blockerMutex);
      blockerRunning = true;
      blockerCond.wakeAll();
      while (!releaseBlocker) {
        blockerCond.wait(&blockerMutex, 100);
      }
    });
    // Wait for blocker to actually start.
    {
      QMutexLocker lk(&blockerMutex);
      QElapsedTimer t;
      t.start();
      while (!blockerRunning && t.elapsed() < 2000) {
        blockerCond.wait(&blockerMutex, 100);
      }
    }
    QVERIFY(blockerRunning);

    // Emit should_run twice. First fills the pending slot (queueDepth = 1
    // pending + 1 running); second sees coalesce-match on "trigger" → Coalesced
    // (still silent). Either way: no syncFailed.
    emitShouldRun(ctx, nbId);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    emitShouldRun(ctx, nbId);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    QTest::qWait(300);

    QCOMPARE(failedSpy.count(), 0);

    // Release blocker; let things drain.
    {
      QMutexLocker lk(&blockerMutex);
      releaseBlocker = true;
      blockerCond.wakeAll();
    }
    // Allow the unblocked queue + any auto-trigger to run to completion before
    // shutting down so SyncService teardown does not race with in-flight work.
    QElapsedTimer drainTimer;
    drainTimer.start();
    while ((wqMgr.queueDepth(nbId) > 0 || wqMgr.isRunning(nbId)) && drainTimer.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
      QTest::qWait(50);
    }
    syncService.shutdown();
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

void TestSyncAutoRoute::test_auto_route_disabled_bail() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    tests::KeychainGuard guard(&credStore);
    EventBridge eventBridge(ctx);
    services.registerService<EventBridge>(&eventBridge);
    HookManager hookMgr;
    services.registerService<HookManager>(&hookMgr);
    SyncWorkQueueManager wqMgr;
    services.registerService<SyncWorkQueueManager>(&wqMgr);
    SyncService syncService(services);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());
    QString nbRoot = localTemp.filePath(QStringLiteral("nb_root"));
    QDir().mkpath(nbRoot);
    QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"AR","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    // Notebook is NOT sync-enabled. emit a stray sync.should_run.
    emitShouldRun(ctx, nbId);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    // No enqueue should have occurred.
    QCOMPARE(wqMgr.queueDepth(nbId), 0);
    QCOMPARE(wqMgr.isRunning(nbId), false);

    syncService.shutdown();
    guard.cleanup();
  }
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncAutoRoute)
#include "test_sync_auto_route.moc"
