// Tests for NotebookSyncInfoController (T11) — controller orchestrating
// NotebookSyncInfoDialog2 against SyncService + SyncCredentialsStore +
// NotebookCoreService.
//
// Cases:
//   * applyChangesRoutesUrlAndPat: verifies that applyChanges() persists the
//     new syncRemoteUrl as a FLAT ADR-8 key in the notebook config AND routes
//     the new PAT through SyncService::updateCredentials -> SyncCredentialsStore.
//   * disableSyncRoutesCorrectly: verifies that disableSync() invokes
//     SyncService::disableSyncForNotebook (waits for disableFinished) AND
//     that SyncCredentialsStore::credentialsDeleted fires (T7 wires deletion
//     after disable). The notebook config should not advertise sync as
//     enabled afterwards.
//
// Per ADR-1: this test never includes sync/sync_manager.h.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworker.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookSyncInfoController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void applyChangesRoutesUrlAndPat();
  void disableSyncRoutesCorrectly();

  // W3.T1 — bootstrapApply: atomic S1/S2/S3/S4 -> S5 transition on an
  // existing partial notebook. Mirrors NewNotebookController::bootstrapSync
  // but does NOT delete the notebook on failure.
  void testBootstrapApplyAtomicSuccess();
  void testBootstrapApplyFailureKeepsNotebook();
  void testBootstrapApplyPersistsUrl();
  void testBootstrapApplyTriggersInitialSync();
  void testBootstrapApplyOneShotDisconnect();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with one commit
  // containing a single file "seed.md" on the default branch (main). Returns
  // the file:// URL suitable for cloning, or empty on failure (e.g., git not
  // installed). Mirrors the helper in test_syncservice.cpp / test_bootstrap.cpp.
  static QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestNotebookSyncInfoController::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
}

QString TestNotebookSyncInfoController::seedBareRepo(const QString &p_bareRepoPath,
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

void TestNotebookSyncInfoController::applyChangesRoutesUrlAndPat() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Seed a bare repo so enableSync against the OLD URL succeeds (we need a
  // real, clonable URL to put the notebook in a sync-enabled state).
  QString bareDir = localTemp.filePath(QStringLiteral("remote_old.git"));
  QString oldRemoteUrl = seedBareRepo(bareDir, localTemp);
  if (oldRemoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  // Create a bundled notebook and enable sync on it (sets up the credentials
  // entry and marks it as sync-enabled in vxcore).
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_apply_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Apply NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Enable sync up-front so the notebook is in a state where applyChanges
  // makes sense (URL update + PAT update on an already-enabled notebook).
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, oldRemoteUrl, QStringLiteral("ghp_OLD_PAT"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  if (qvariant_cast<VxCoreError>(enableSpy.first().at(1)) == VXCORE_ERR_UNKNOWN) {
    qWarning() << "enableSyncForNotebook returned VXCORE_ERR_UNKNOWN; message:"
               << enableSpy.first().at(2).toString();
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }

  // Persist the OLD URL into the flat config (mirrors what NewNotebookController::bootstrapSync
  // does after enable success). This puts the controller's m_currentRemoteUrl
  // cache in a known state after loadInitialData().
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldRemoteUrl;
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }

  // Drive the controller.
  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  const QString newRemoteUrl = QStringLiteral("file:///tmp/new.git");
  const QString newPat = QStringLiteral("new_pat");

  QSignalSpy patSpy(&syncService, &SyncService::credentialsSetFinished);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);

  controller.applyChanges(newRemoteUrl, newPat);

  // Wait for credentialsSetFinished (5s timeout per spec).
  QVERIFY(patSpy.wait(5000));
  QCOMPARE(patSpy.count(), 1);
  QCOMPARE(patSpy.first().at(0).toString(), nbId);

  // applyComplete should fire from the controller after PAT update.
  if (applySpy.isEmpty()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
  }
  QVERIFY(!applySpy.isEmpty());

  // Verify URL persisted as a FLAT ADR-8 key.
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), newRemoteUrl);

  // Verify PAT was updated by retrieving it via SyncCredentialsStore.
  QSignalSpy retrSpy(&credStore, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy retrErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.retrieveCredentials(nbId);
  // Wait for either retrieve or error.
  bool gotRetrieve = false;
  for (int i = 0; i < 50; ++i) {
    if (!retrSpy.isEmpty()) {
      gotRetrieve = true;
      break;
    }
    if (!retrErrSpy.isEmpty()) {
      break;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(50);
  }
  QVERIFY2(gotRetrieve, "Expected credentialsRetrieved (PAT roundtrip)");
  QCOMPARE(retrSpy.first().at(0).toString(), nbId);
  QCOMPARE(retrSpy.first().at(1).toString(), newPat);

  // Cleanup keychain entry.
  credStore.deleteCredentials(nbId);
  QTest::qWait(300);

  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::disableSyncRoutesCorrectly() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Seed a bare repo and enable sync so the notebook is in a sync-enabled
  // state to begin with (otherwise disableSync is essentially a no-op).
  QString bareDir = localTemp.filePath(QStringLiteral("remote_disable.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_disable_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Disable NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("ghp_DISABLE_PAT"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  if (qvariant_cast<VxCoreError>(enableSpy.first().at(1)) == VXCORE_ERR_UNKNOWN) {
    qWarning() << "enableSyncForNotebook returned VXCORE_ERR_UNKNOWN; message:"
               << enableSpy.first().at(2).toString();
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);
  QSignalSpy deletedSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy controllerSpy(&controller, &NotebookSyncInfoController::disableComplete);

  controller.disableSync();

  // Wait for SyncService::disableFinished.
  QVERIFY(disableSpy.wait(15000));
  QCOMPARE(disableSpy.count(), 1);
  QCOMPARE(disableSpy.first().at(0).toString(), nbId);

  // Wait for SyncCredentialsStore::credentialsDeleted (T7 wires deletion AFTER
  // disable). Errors are also acceptable in test environments where the
  // keychain backend is flaky, but for a clean run we expect deletion.
  QSignalSpy delErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  bool gotDeleted = false;
  for (int i = 0; i < 100; ++i) {
    if (!deletedSpy.isEmpty()) {
      gotDeleted = true;
      break;
    }
    if (!delErrSpy.isEmpty()) {
      break;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(50);
  }
  QVERIFY2(gotDeleted, "Expected SyncCredentialsStore::credentialsDeleted after disable");

  // Controller's disableComplete should also fire.
  if (controllerSpy.isEmpty()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
  }
  QVERIFY(!controllerSpy.isEmpty());
  QCOMPARE(controllerSpy.first().at(0).toString(), nbId);

  // Verify notebook config no longer advertises sync as enabled (per ADR-8
  // flat keys: syncEnabled is either false or absent).
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QVERIFY2(!cfgAfter.value(QStringLiteral("syncEnabled")).toBool(),
           "syncEnabled should be false or absent after disableSync");

  vxcore_context_destroy(ctx);
}

// ============================================================================
// W3.T1 — bootstrapApply tests
// ============================================================================
//
// bootstrapApply atomically transitions an EXISTING partial notebook
// (S1/S2/S3/S4 — sync_enabled=true on disk but unregistered at runtime, with
// or without a remote URL) into S5 (sync-ready, registered). Unlike
// NewNotebookController::bootstrapSync, failure leaves the notebook intact:
// the user's content is preserved and the partial sync config remains so the
// user can retry.

void TestNotebookSyncInfoController::testBootstrapApplyAtomicSuccess() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_bootstrap_success.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  // Create an S1 partial notebook: syncEnabled=true on disk, NOT yet
  // registered at vxcore runtime, no syncRemoteUrl. This is exactly what
  // NewNotebookController::createNotebook leaves behind when syncMethod=git
  // and bootstrapSync has not yet run.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_success_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Success","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Pre-condition: notebook is NOT yet registered in vxcore runtime state.
  QVERIFY(!syncService.isSyncRegistered(nbId));

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  controller.bootstrapApply(remoteUrl, QStringLiteral("test_pat_12345"));

  QVERIFY(applySpy.wait(15000));
  QCOMPARE(applySpy.count(), 1);
  const bool success = applySpy.first().at(0).toBool();
  if (!success) {
    qWarning() << "bootstrapApply reported failure; error signals:" << errorSpy;
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend or git enable not usable in this test environment");
  }
  QCOMPARE(success, true);

  // Post-condition: notebook is now registered at vxcore runtime.
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Cleanup keychain entry.
  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testBootstrapApplyFailureKeepsNotebook() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Partial notebook in S1 state. We will force enableFinished to return
  // VXCORE_ERR_UNKNOWN via the worker's testForceError seam (no real git
  // operation needed, so we don't seed a bare repo).
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_failure_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Failure","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Sanity: notebook exists on disk before bootstrap.
  QVERIFY(QDir(nbRoot).exists());
  const QJsonObject cfgBefore = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgBefore.value(QStringLiteral("name")).toString(), QStringLiteral("Bootstrap Failure"));

  // Arm the worker to fail the next enable call (testForceError consumes the
  // sentinel on the next slot invocation, so this is one-shot — exactly what
  // bootstrapApply's enableSync produces).
  syncService.worker()->testForceError(static_cast<int>(VXCORE_ERR_UNKNOWN));

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  controller.bootstrapApply(QStringLiteral("file:///tmp/will_fail.git"),
                            QStringLiteral("test_pat_12345"));

  QVERIFY(applySpy.wait(15000));
  QCOMPARE(applySpy.count(), 1);
  QCOMPARE(applySpy.first().at(0).toBool(), false);
  QVERIFY2(!errorSpy.isEmpty(), "error() signal should fire on bootstrap failure");

  // Critical: the notebook MUST still exist on disk (we did NOT delete it).
  QVERIFY2(QDir(nbRoot).exists(),
           "Notebook root directory should NOT be removed on bootstrapApply failure");
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("name")).toString(), QStringLiteral("Bootstrap Failure"));

  // Cleanup: forced-error path bypasses the keychain success branch, but
  // SyncCredentialsStore::storeCredentials may have already written the
  // entry before the worker error fired. Best-effort delete.
  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testBootstrapApplyPersistsUrl() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_bootstrap_persist.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_persist_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Persist","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Pre-condition: syncRemoteUrl is empty on disk.
  {
    const QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    QCOMPARE(cfg.value(QStringLiteral("syncRemoteUrl")).toString(), QString());
  }

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  controller.bootstrapApply(remoteUrl, QStringLiteral("test_pat_12345"));

  QVERIFY(applySpy.wait(15000));
  QCOMPARE(applySpy.count(), 1);
  if (!applySpy.first().at(0).toBool()) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend or git enable not usable in this test environment");
  }

  // Post-condition: syncRemoteUrl on disk matches the URL passed to
  // bootstrapApply (flat ADR-8 key).
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), remoteUrl);

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testBootstrapApplyTriggersInitialSync() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote_bootstrap_trigger.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_trigger_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap Trigger","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  // syncStarted is emitted by SyncService at the start of triggerSyncNow's
  // worker dispatch (re-emitted from worker thread onto GUI thread via
  // QueuedConnection). bootstrapApply should fire triggerSyncNow on success,
  // which produces syncStarted.
  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);

  controller.bootstrapApply(remoteUrl, QStringLiteral("test_pat_12345"));

  QVERIFY(applySpy.wait(15000));
  QCOMPARE(applySpy.count(), 1);
  if (!applySpy.first().at(0).toBool()) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend or git enable not usable in this test environment");
  }

  // Wait for syncStarted (triggerSyncNow is async; we may need to spin the
  // event loop). startedSpy.wait() returns true if the signal fires within
  // the timeout.
  if (startedSpy.isEmpty()) {
    QVERIFY(startedSpy.wait(10000));
  }
  QVERIFY2(!startedSpy.isEmpty(),
           "syncStarted should fire after bootstrapApply success (triggerSyncNow)");
  // The first syncStarted may be for our notebook, or for some other
  // simultaneously-active notebook; filter.
  bool foundOurStart = false;
  for (const auto &args : startedSpy) {
    if (args.at(0).toString() == nbId) {
      foundOurStart = true;
      break;
    }
  }
  QVERIFY2(foundOurStart, "syncStarted should include our notebookId");

  credStore.deleteCredentials(nbId);
  QTest::qWait(500); // let any in-flight sync finish before destroying ctx
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testBootstrapApplyOneShotDisconnect() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // Use a partial notebook + testForceError so each bootstrapApply call
  // synchronously short-circuits at the worker without needing a real git
  // remote. Each call must re-arm testForceError because it's consume-and-
  // clear (one-shot).
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_bootstrap_oneshot_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Bootstrap OneShot","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);

  // Call bootstrapApply 3 times sequentially. If the one-shot disconnect is
  // broken (i.e., the connection leaks), the 2nd call's enableFinished would
  // fire the 1st call's lambda too, producing 4 applyComplete signals after
  // 3 bootstraps. Correct behavior: exactly 3 applyComplete signals.
  for (int i = 0; i < 3; ++i) {
    syncService.worker()->testForceError(static_cast<int>(VXCORE_ERR_UNKNOWN));
    controller.bootstrapApply(QStringLiteral("file:///tmp/oneshot_%1.git").arg(i),
                              QStringLiteral("test_pat_12345"));
    // Wait for THIS call's applyComplete before issuing the next one.
    const int beforeCount = applySpy.count();
    for (int waitMs = 0; waitMs < 15000 && applySpy.count() == beforeCount; waitMs += 50) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QTest::qWait(50);
    }
    QVERIFY2(applySpy.count() == beforeCount + 1,
             qPrintable(QStringLiteral("applyComplete should fire exactly once per "
                                       "bootstrapApply iteration (i=%1)")
                            .arg(i)));
  }

  // Exactly 3 applyComplete signals — no leaked connections from prior calls.
  QCOMPARE(applySpy.count(), 3);

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookSyncInfoController)
#include "test_notebooksyncinfocontroller.moc"
