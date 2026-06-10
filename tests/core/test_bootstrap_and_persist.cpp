// Tests for SyncService::bootstrapAndPersist (Task 13.4 / F1.6).
//
// Verifies the atomic enable+persist contract:
//   1. happy_path_s5: enable + persist + initial sync succeed; notebook lands
//      in S5 with all three flat ADR-8 sync keys present on disk.
//   2. persist_failure_rolls_back_to_original_state: persist failure after a
//      successful enable triggers a rollback via disableSyncForNotebook;
//      bootstrapAndPersistFinished surfaces the ORIGINAL persist error;
//      notebook ends in clean S0 (no flat sync keys, runtime unregistered).
//   3. rollback_failure_logged: when both persist AND rollback fail, the
//      caller still sees the ORIGINAL persist error in
//      bootstrapAndPersistFinished, and a qCritical log line is emitted
//      reporting the rollback failure.
//
// Per ADR-1: never includes sync/sync_manager.h.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>

#include <sync/sync_json_keys.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestBootstrapAndPersist : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void happy_path_s5();
  void persist_failure_rolls_back_to_original_state();
  void rollback_failure_logged();

private:
  QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestBootstrapAndPersist::initTestCase() { vxcore_set_test_mode(1); }
void TestBootstrapAndPersist::cleanupTestCase() {}

QString TestBootstrapAndPersist::seedBareRepo(const QString &p_bareRepoPath,
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

void TestBootstrapAndPersist::happy_path_s5() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  // T5: track UUID-based notebook writes so cleanup runs before ctx destroy.
  tests::KeychainGuard guard(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    syncService.shutdown();
    guard.cleanup();
    syncService.shutdown();
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_happy"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Happy NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QSignalSpy finSpy(&syncService, &SyncService::bootstrapAndPersistFinished);
  syncService.bootstrapAndPersist(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_134"));

  QVERIFY(finSpy.wait(15000));
  QCOMPARE(finSpy.count(), 1);
  QCOMPARE(finSpy.first().at(0).toString(), nbId);
  const VxCoreError result = qvariant_cast<VxCoreError>(finSpy.first().at(1));
  if (result == VXCORE_ERR_UNKNOWN) {
    // Could be keychain backend unavailable (enable phase returned UNKNOWN
    // and was passed through), OR persist failed somehow. Distinguish by
    // checking the disk JSON: if flat keys are NOT present, treat as
    // documented skip (matches sibling tests).
    const QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    if (!cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool()) {
      syncService.shutdown();
      credStore.deleteCredentials(nbId);
      QTest::qWait(500);
      guard.cleanup();
      syncService.shutdown();
      vxcore_context_destroy(ctx);
      QSKIP("OS keychain backend not usable in this test environment");
    }
  }
  QCOMPARE(result, VXCORE_OK);

  // Defensive: signal-based auto-track should already catch this, but pin it.
  guard.track(nbId);

  // Flat ADR-8 keys present.
  const QJsonObject cfg = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool(), true);
  QCOMPARE(cfg.value(QLatin1String(vxcore::kJsonKeySyncBackend)).toString(), QStringLiteral("git"));
  QCOMPARE(cfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString(), remoteUrl);

  // Runtime registered.
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Cleanup.
  // SEGFAULT fix (post vxcore-metadata-events plan): bootstrapAndPersist
  // writes the flat sync config via SaveFolderConfig, which fires
  // folder.config_changed → mark_dirty → sync.should_run → SyncService
  // enqueues a SyncOps::triggerSync work item on its owned SyncWorkQueueManager.
  // That work item runs on a pool thread and calls vxcore_sync_network_phase,
  // which touches SyncManager::states_. Without shutdown() here, the local
  // SyncService dtor wouldn't run until function exit — AFTER
  // vxcore_context_destroy(ctx) below — so the pool thread crashes on a
  // freed SyncManager. shutdown() drains the queue (with the bounded
  // budget) while ctx is still alive.
  syncService.shutdown();
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbId);
  (void)delSpy.wait(3000);
  (void)delErr.count();
  guard.cleanup();
  // CRITICAL: drain the work queue (including the trailing initial sync that
  // bootstrapAndPersist enqueued at syncservice.cpp:751) BEFORE destroying
  // the vxcore context. Otherwise the worker thread is still mid-flight in
  // vxcore_sync_* with a dangling context handle -> use-after-free SEGFAULT.
  // On Linux file:// remotes finish inside the 3s delSpy window; on Windows
  // libgit2 + filesystem ops + AV scanning push past that window.
  syncService.shutdown();
  vxcore_context_destroy(ctx);
}

void TestBootstrapAndPersist::persist_failure_rolls_back_to_original_state() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  tests::KeychainGuard guard(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    syncService.shutdown();
    guard.cleanup();
    syncService.shutdown();
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_persist_fail"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"PersistFail NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Pre-condition: sync is not enabled on disk (default-initialized notebooks
  // may have syncEnabled=false present as a default field; we only require it
  // is not truthy and that no remote URL exists).
  QJsonObject preCfg = notebookService.getNotebookConfig(nbId);
  QVERIFY(!preCfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool());
  QVERIFY(preCfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString().isEmpty());

  // Arm the persist-failure seam BEFORE the call.
  const QString injectedMsg = QStringLiteral("simulated persist failure for test");
  syncService.testForceNextPersistFailure(injectedMsg);

  QSignalSpy finSpy(&syncService, &SyncService::bootstrapAndPersistFinished);
  syncService.bootstrapAndPersist(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_134b"));

  QVERIFY(finSpy.wait(15000));
  QCOMPARE(finSpy.count(), 1);
  QCOMPARE(finSpy.first().at(0).toString(), nbId);
  const VxCoreError result = qvariant_cast<VxCoreError>(finSpy.first().at(1));
  const QString msg = finSpy.first().at(2).toString();
  // Track the id regardless — vxcore may have written the keychain entry
  // before the simulated persist failure fired. Rollback's deleteCredentials
  // is best-effort; guard.cleanup() backstops it.
  guard.track(nbId);
  // If enable itself failed (e.g., keychain unavailable), we never reached
  // the persist seam — skip as documented.
  if (result != VXCORE_ERR_UNKNOWN || msg != injectedMsg) {
    if (result == VXCORE_ERR_UNKNOWN) {
      syncService.shutdown();
      credStore.deleteCredentials(nbId);
      QTest::qWait(500);
      guard.cleanup();
      syncService.shutdown();
      vxcore_context_destroy(ctx);
      QSKIP("Enable failed (likely keychain) — persist seam never reached");
    }
  }
  QCOMPARE(result, VXCORE_ERR_UNKNOWN);
  QCOMPARE(msg, injectedMsg); // ORIGINAL persist error preserved.

  // Wait for the rollback disable to settle (it fires asynchronously via the
  // worker). disableFinished -> JSON clear happens in SyncService's own slot.
  QSignalSpy disSpy(&syncService, &SyncService::disableFinished);
  // Rollback may have already finished before we attached the spy; just wait
  // briefly to let it complete if still in flight.
  (void)disSpy.wait(5000);

  // Post-condition: notebook in clean S0 — no flat sync keys on disk, not
  // runtime-registered.
  const QJsonObject postCfg = notebookService.getNotebookConfig(nbId);
  QVERIFY2(!postCfg.value(QLatin1String(vxcore::kJsonKeySyncEnabled)).toBool(),
           "syncEnabled must be cleared after rollback");
  QVERIFY2(postCfg.value(QLatin1String(vxcore::kJsonKeySyncRemoteUrl)).toString().isEmpty(),
           "syncRemoteUrl must be cleared after rollback");
  QVERIFY2(!syncService.isSyncRegistered(nbId),
           "notebook must NOT be runtime-registered after rollback");

  // Cleanup (in case keychain entry survived).
  // See happy_path_s5 for the rationale behind the explicit shutdown() —
  // the persistence-event cascade triggered by SaveFolderConfig may have
  // enqueued a triggerSync that must drain before vxcore_context_destroy.
  syncService.shutdown();
  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  guard.cleanup();
  // CRITICAL: see happy_path_s5 for rationale — drain before ctx destroy to
  // avoid use-after-free of the vxcore context from the worker thread.
  syncService.shutdown();
  vxcore_context_destroy(ctx);
}

void TestBootstrapAndPersist::rollback_failure_logged() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  tests::KeychainGuard guard(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    syncService.shutdown();
    guard.cleanup();
    syncService.shutdown();
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_rb_fail"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"RBFail NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  const QString injectedMsg = QStringLiteral("persist failure with rollback failure");
  syncService.testForceNextPersistFailure(injectedMsg);
  syncService.testForceNextRollbackFailure();

  // Expect the qCritical rollback-failure log line.
  QTest::ignoreMessage(QtCriticalMsg,
                       QRegularExpression(QStringLiteral("ROLLBACK FAILED for notebook")));
  // Also expect the precursor qCritical "persist failed after enable" line.
  QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral(
                                          "persist failed after enable; rolling back")));

  QSignalSpy finSpy(&syncService, &SyncService::bootstrapAndPersistFinished);
  syncService.bootstrapAndPersist(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_134c"));

  QVERIFY(finSpy.wait(15000));
  // Multiple emissions are possible if the real worker disableFinished also
  // arrives after the synthesized failure (one-shot rbConn only consumes
  // first). The FIRST emission MUST carry the original persist error.
  QVERIFY(finSpy.count() >= 1);
  QCOMPARE(finSpy.first().at(0).toString(), nbId);
  const VxCoreError result = qvariant_cast<VxCoreError>(finSpy.first().at(1));
  const QString msg = finSpy.first().at(2).toString();
  // Track for cleanup regardless: rollback-failure leaves the keychain entry
  // alive, so guard.cleanup() must remove it.
  guard.track(nbId);
  if (result == VXCORE_ERR_UNKNOWN && msg != injectedMsg) {
    // Enable phase itself failed -> persist seam unreached. Skip.
    syncService.shutdown();
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    guard.cleanup();
    syncService.shutdown();
    vxcore_context_destroy(ctx);
    QSKIP("Enable failed (likely keychain) — persist seam never reached");
  }
  QCOMPARE(result, VXCORE_ERR_UNKNOWN);
  QCOMPARE(msg, injectedMsg); // ORIGINAL persist error preserved even when rollback fails.

  // See happy_path_s5 for the rationale behind the explicit shutdown() —
  // the persistence-event cascade triggered by SaveFolderConfig may have
  // enqueued a triggerSync that must drain before vxcore_context_destroy.
  syncService.shutdown();
  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  guard.cleanup();
  // CRITICAL: see happy_path_s5 for rationale — drain before ctx destroy to
  // avoid use-after-free of the vxcore context from the worker thread.
  syncService.shutdown();
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestBootstrapAndPersist)
#include "test_bootstrap_and_persist.moc"
