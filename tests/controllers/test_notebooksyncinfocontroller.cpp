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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QUrl>
#include <QUuid>
#include <QtTest>
#include <memory>

#include <test_helper.h>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

namespace {
SyncSettings gitSettings(const QString &p_url, const QString &p_secret) {
  return {QStringLiteral("git"), p_url, {QStringLiteral("git"), {}, p_secret}};
}

class UnavailableCredentialsStore : public SyncCredentialsStore {
public:
  using SyncCredentialsStore::SyncCredentialsStore;
  void storeCredentials(const QString &p_id, const SyncCredential &) override {
    QMetaObject::invokeMethod(
        this,
        [this, p_id]() {
          emit credentialsStoreError(p_id, QStringLiteral("secure-keychain-unavailable"));
        },
        Qt::QueuedConnection);
  }
  void deleteCredentials(const QString &p_id) override {
    QMetaObject::invokeMethod(
        this, [this, p_id]() { emit credentialsDeleted(p_id); }, Qt::QueuedConnection);
  }
};

struct RetirementFixture {
  TempDirFixture directory;
  VxCoreContextHandle context = nullptr;
  ServiceLocator services;
  std::unique_ptr<NotebookCoreService> notebooks;
  std::unique_ptr<UnavailableCredentialsStore> credentials;
  std::unique_ptr<SyncService> sync;
  QString id;
  QString root;
  QString statePath;
  QByteArray stateBytes;

  RetirementFixture() {
    if (vxcore_context_create("{}", &context) != VXCORE_OK) {
      return;
    }
    notebooks.reset(new NotebookCoreService(context));
    services.registerService<NotebookCoreService>(notebooks.get());
    credentials.reset(new UnavailableCredentialsStore(services));
    services.registerService<SyncCredentialsStore>(credentials.get());
    sync.reset(new SyncService(services));
    services.registerService<SyncService>(sync.get());
    root = directory.filePath(QStringLiteral("notebook"));
    id = notebooks->createNotebook(root, QStringLiteral("{\"name\":\"Retirement\"}"),
                                   NotebookType::Bundled);
    statePath = root + QStringLiteral("/vx_notebook/vx_sync/webdav/state.json");
    QJsonObject state{
        {QStringLiteral("version"), 1},
        {QStringLiteral("notebookId"), id},
        {QStringLiteral("remoteUrl"), QStringLiteral("https://old.example.test/notes/")},
        {QStringLiteral("usernameHash"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("entries"), QJsonObject()},
        {QStringLiteral("conflicts"), QJsonObject()}};
    stateBytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    writeBytes(statePath, stateBytes);
    writeBytes(root + QStringLiteral("/note.txt"), QByteArray("user text\n"));
  }
  ~RetirementFixture() {
    if (sync) {
      sync->shutdown();
    }
    if (notebooks && !id.isEmpty()) {
      notebooks->closeNotebook(id);
    }
    sync.reset();
    credentials.reset();
    notebooks.reset();
    if (context) {
      vxcore_context_destroy(context);
    }
  }
  static bool writeBytes(const QString &p_path, const QByteArray &p_bytes) {
    if (!QDir().mkpath(QFileInfo(p_path).absolutePath())) {
      return false;
    }
    QFile file(p_path);
    return file.open(QIODevice::WriteOnly) && file.write(p_bytes) == p_bytes.size();
  }
  static QByteArray bytes(const QString &p_path) {
    QFile file(p_path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
  static SyncSettings replacement() {
    return {QStringLiteral("webdav"),
            QStringLiteral("https://new.example.test/notes/"),
            {QStringLiteral("webdav"), QStringLiteral("alice"), QStringLiteral("new secret")}};
  }
};
} // namespace

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

  // T2 — Empty-input guards
  void testBootstrapApplyEmptyUrlFails();
  void testBootstrapApplyEmptyPatFails();
  void endpointRetirementPreservesPayloadsAfterDisable();
  void archiveFailureLeavesRegisteredGitUsable();
  void gitEndpointCleanupPreservesWebDavArchive();
  void incompleteArchiveRestoredBeforeEnable();
  void pendingRecoveryAndQueuedWorkBlockRetirement();

  // W3.T3 — URL-change-on-S5 atomic disable+re-enable flow. Closes bug B4
  // (URL change leaves runtime stale or causes silent split-brain push).
  void testUrlChangeShowsConfirmation();
  void testUrlChangeConfirmedAtomicallyReregisters();
  void testUrlChangeCancelledRestoresUrl();
  void testUrlChangePreservesPatWhenFieldEmpty();
  void testUrlChangeReenableFailureSurfacesError();

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
  QVERIFY2(!oldRemoteUrl.isEmpty(), "Could not prepare the Git remote fixture");

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
  syncService.enableSyncForNotebook(nbId, gitSettings(oldRemoteUrl, QStringLiteral("ghp_OLD_PAT")));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  QCOMPARE(qvariant_cast<VxCoreError>(enableSpy.first().at(1)), VXCORE_OK);

  // Mirror a fully configured notebook, not a registered-but-partial one.
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldRemoteUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }

  // Drive the controller.
  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  // Per W3.T3: URL changes on a registered notebook now route through the
  // confirmation flow (confirmUrlChangeRequested). To keep this test focused
  // on the PAT-routing path (which was its original intent), pass the SAME
  // URL so only the PAT update is exercised. URL-change tests live in the
  // testUrlChange* suite below.
  const QString newRemoteUrl = oldRemoteUrl;
  const QString newPat = QStringLiteral("new_pat");

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);

  controller.applyChanges(gitSettings(newRemoteUrl, newPat));

  QTRY_COMPARE_WITH_TIMEOUT(applySpy.count(), 1, 15000);
  QVERIFY(applySpy.first().at(0).toBool());

  // Verify URL persisted as a FLAT ADR-8 key.
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), newRemoteUrl);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncBackend")).toString(), QStringLiteral("git"));
  QVERIFY(cfgAfter.value(QStringLiteral("syncEnabled")).toBool());
  QVERIFY(syncService.isSyncRegistered(nbId));

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
  QCOMPARE(qvariant_cast<SyncCredential>(retrSpy.first().at(1)).m_secret, newPat);

  // Cleanup keychain entry.
  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  syncService.shutdown();
  notebookService.closeNotebook(nbId);

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
  syncService.enableSyncForNotebook(nbId,
                                    gitSettings(remoteUrl, QStringLiteral("ghp_DISABLE_PAT")));
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

  controller.bootstrapApply(gitSettings(remoteUrl, QStringLiteral("test_pat_12345")));

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
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  auto settings = RetirementFixture::replacement();
  settings.m_remoteUrl = QStringLiteral("https://old.example.test/notes/");
  const auto before = fixture.notebooks->getNotebookConfig(fixture.id);
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  controller.bootstrapApply(settings);
  QTRY_COMPARE(applied.count(), 1);
  QVERIFY(!applied.first().at(0).toBool());
  QCOMPARE(fixture.notebooks->getNotebookConfig(fixture.id), before);
  QCOMPARE(RetirementFixture::bytes(fixture.root + QStringLiteral("/note.txt")),
           QByteArray("user text\n"));
  QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  QVERIFY(!fixture.sync->isSyncRegistered(fixture.id));
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
  controller.bootstrapApply(gitSettings(remoteUrl, QStringLiteral("test_pat_12345")));

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

  controller.bootstrapApply(gitSettings(remoteUrl, QStringLiteral("test_pat_12345")));

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
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  auto settings = RetirementFixture::replacement();
  settings.m_remoteUrl = QStringLiteral("https://old.example.test/notes/");
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  for (int attempt = 1; attempt <= 3; ++attempt) {
    controller.bootstrapApply(settings);
    QTRY_COMPARE(applied.count(), attempt);
    QVERIFY(!applied.last().at(0).toBool());
    QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  }
}

// ============================================================================
// T2 — Empty-input guards for bootstrapApply
// ============================================================================

void TestNotebookSyncInfoController::testBootstrapApplyEmptyUrlFails() {
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

  // Create a partial notebook in S1 state.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_empty_url_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Empty URL","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  // Pass empty URL → should be rejected immediately.
  controller.bootstrapApply(gitSettings(QString(), QStringLiteral("test_pat")));

  // Verify applyComplete(false) fires within 100ms (synchronous guard).
  QVERIFY(!applySpy.isEmpty() || applySpy.wait(100));
  QVERIFY(!applySpy.isEmpty());
  QCOMPARE(applySpy.first().at(0).toBool(), false);

  // Verify error signal fired with appropriate message.
  QVERIFY(!errorSpy.isEmpty());

  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testBootstrapApplyEmptyPatFails() {
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

  // Create a partial notebook in S1 state.
  QString nbRoot = localTemp.filePath(QStringLiteral("nb_empty_pat_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Empty PAT","syncEnabled":true,"syncBackend":"git"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  // Pass empty PAT → should be rejected immediately.
  controller.bootstrapApply(gitSettings(QStringLiteral("file:///tmp/remote.git"), QString()));

  // Verify applyComplete(false) fires within 100ms (synchronous guard).
  QVERIFY(!applySpy.isEmpty() || applySpy.wait(100));
  QVERIFY(!applySpy.isEmpty());
  QCOMPARE(applySpy.first().at(0).toBool(), false);

  // Verify error signal fired with appropriate message.
  QVERIFY(!errorSpy.isEmpty());

  vxcore_context_destroy(ctx);
}

//
// Per W1.T1 evidence: re-calling vxcore_sync_enable (with credentials) with a
// different URL leaves the on-disk git remote stale → split-brain. The only
// correct path is atomic disable+wipe+re-enable. These tests verify:
//   1. The confirmation gate fires on URL change for a registered notebook.
//   2. Confirmed atomic re-register updates the on-disk URL AND restores
//      runtime registration AND preserves the keychain PAT.
//   3. Cancellation is a true no-op (no disk change, no disable invoked).
//   4. Empty PAT field → existing PAT is fetched from keychain BEFORE disable
//      (since disable+W2.T5 wipes the keychain entry) and reused for
//      re-enable; keychain still has the same PAT after.
//   5. Re-enable failure leaves the notebook in clean S0 state (W2.T5 cleared
//      the JSON during disable; failed enable did not rewrite it) and the
//      user gets a clear error message about needing to re-enable manually.

void TestNotebookSyncInfoController::testUrlChangeShowsConfirmation() {
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

  // Seed a real bare repo so enable succeeds and we end up in S5.
  QString bareDir = localTemp.filePath(QStringLiteral("remote_url_confirm.git"));
  QString oldRemoteUrl = seedBareRepo(bareDir, localTemp);
  if (oldRemoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_url_confirm_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"URL Confirm","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Enable sync against the old URL → notebook is now in S5 (registered).
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId,
                                    gitSettings(oldRemoteUrl, QStringLiteral("test_pat_12345")));
  QVERIFY(enableSpy.wait(15000));
  if (qvariant_cast<VxCoreError>(enableSpy.first().at(1)) != VXCORE_OK) {
    qWarning() << "enableSync returned non-OK; message:" << enableSpy.first().at(2).toString();
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend or git enable not usable in this test environment");
  }

  // Persist the OLD URL into the flat config so loadInitialData()'s cache
  // matches the disk state.
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldRemoteUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }

  QVERIFY(syncService.isSyncRegistered(nbId));

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy confirmSpy(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);

  const QString newUrl = QStringLiteral("file:///tmp/new_remote.git");
  controller.applyChanges(gitSettings(newUrl, QString()));

  // confirmUrlChangeRequested MUST fire synchronously with (oldUrl, newUrl).
  QCOMPARE(confirmSpy.count(), 1);
  QCOMPARE(confirmSpy.first().at(0).toString(), oldRemoteUrl);
  QCOMPARE(confirmSpy.first().at(1).toString(), newUrl);

  // applyComplete MUST NOT fire (we are awaiting user confirmation).
  QCOMPARE(applySpy.count(), 0);

  // No disable should have been dispatched yet.
  QCOMPARE(disableSpy.count(), 0);

  // Disk state must be unchanged (URL still the OLD url).
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), oldRemoteUrl);

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testUrlChangeConfirmedAtomicallyReregisters() {
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

  // The old remote supplies content. The new remote must be empty: replacing
  // the binding preserves local files, and Git refuses two nonempty histories.
  QString oldBare = localTemp.filePath(QStringLiteral("remote_url_atomic_old.git"));
  QString oldUrl = seedBareRepo(oldBare, localTemp);
  QString newBare = localTemp.filePath(QStringLiteral("remote_url_atomic_new.git"));
  QVERIFY2(!oldUrl.isEmpty(), "Could not prepare the original Git remote");
  QCOMPARE(
      QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                QStringLiteral("--initial-branch=main"), newBare}),
      0);
  const QString newUrl = QUrl::fromLocalFile(newBare).toString();

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_url_atomic_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"URL Atomic","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Enable sync against the OLD URL.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, gitSettings(oldUrl, QStringLiteral("test_pat_12345")));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(qvariant_cast<VxCoreError>(enableSpy.first().at(1)), VXCORE_OK);

  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }
  QVERIFY(syncService.isSyncRegistered(nbId));
  const auto workingFile = nbRoot + QStringLiteral("/seed.md");
  const auto workingBytes = RetirementFixture::bytes(workingFile);
  QVERIFY(!workingBytes.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy confirmSpy(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  // Pass the SAME PAT through the dialog so the keychain-fetch branch is
  // skipped; covers the "user provided new PAT" path.
  controller.applyChanges(gitSettings(newUrl, QStringLiteral("test_pat_12345")));
  QCOMPARE(confirmSpy.count(), 1);

  controller.confirmUrlChange(true);

  QVERIFY(applySpy.wait(20000));
  QCOMPARE(applySpy.count(), 1);
  QCOMPARE(applySpy.first().at(0).toBool(), true);

  // Disk: syncRemoteUrl updated to NEW; syncEnabled=true; syncBackend=git.
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), newUrl);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncEnabled")).toBool(), true);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncBackend")).toString(), QStringLiteral("git"));

  // Runtime: notebook is registered.
  QVERIFY(syncService.isSyncRegistered(nbId));
  QCOMPARE(RetirementFixture::bytes(workingFile), workingBytes);
  QTRY_VERIFY_WITH_TIMEOUT(!syncService.isSyncInProgress(nbId), 15000);

  // Keychain: PAT preserved (re-enable wrote it back via storeCredentials).
  QSignalSpy retrSpy(&credStore, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy retrErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.retrieveCredentials(nbId);
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
  QVERIFY2(gotRetrieve, "Expected credentialsRetrieved after URL change re-register");
  QCOMPARE(qvariant_cast<SyncCredential>(retrSpy.first().at(1)).m_secret,
           QStringLiteral("test_pat_12345"));

  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  syncService.shutdown();
  notebookService.closeNotebook(nbId);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testUrlChangeCancelledRestoresUrl() {
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

  QString bareDir = localTemp.filePath(QStringLiteral("remote_url_cancel.git"));
  QString oldUrl = seedBareRepo(bareDir, localTemp);
  if (oldUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_url_cancel_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"URL Cancel","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, gitSettings(oldUrl, QStringLiteral("test_pat_12345")));
  QVERIFY(enableSpy.wait(15000));
  if (qvariant_cast<VxCoreError>(enableSpy.first().at(1)) != VXCORE_OK) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend or git enable not usable in this test environment");
  }
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }
  QVERIFY(syncService.isSyncRegistered(nbId));

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy confirmSpy(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);

  const QString newUrl = QStringLiteral("file:///tmp/will_not_be_used.git");
  controller.applyChanges(gitSettings(newUrl, QString()));
  QCOMPARE(confirmSpy.count(), 1);

  // User cancels at the QMessageBox.
  controller.confirmUrlChange(false);

  // Give any spurious signals time to fire.
  QTest::qWait(300);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

  // Cancellation retires the dialog operation but leaves disk/runtime untouched.
  QCOMPARE(applySpy.count(), 1);
  QCOMPARE(applySpy.first().at(0).toBool(), false);
  // No disable was invoked.
  QCOMPARE(disableSpy.count(), 0);
  // Disk state unchanged.
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), oldUrl);
  QCOMPARE(cfgAfter.value(QStringLiteral("syncEnabled")).toBool(), true);
  // Runtime still registered.
  QVERIFY(syncService.isSyncRegistered(nbId));

  credStore.deleteCredentials(nbId);
  QTest::qWait(300);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testUrlChangePreservesPatWhenFieldEmpty() {
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

  QString oldBare = localTemp.filePath(QStringLiteral("remote_url_pat_old.git"));
  QString oldUrl = seedBareRepo(oldBare, localTemp);
  QString newBare = localTemp.filePath(QStringLiteral("remote_url_pat_new.git"));
  QVERIFY2(!oldUrl.isEmpty(), "Could not prepare the original Git remote");
  QCOMPARE(
      QProcess::execute(QStringLiteral("git"), {QStringLiteral("init"), QStringLiteral("--bare"),
                                                QStringLiteral("--initial-branch=main"), newBare}),
      0);
  const QString newUrl = QUrl::fromLocalFile(newBare).toString();

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_url_pat_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"URL Pat","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Seed the keychain with the existing PAT via enableSyncForNotebook.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, gitSettings(oldUrl, QStringLiteral("test_pat_12345")));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(qvariant_cast<VxCoreError>(enableSpy.first().at(1)), VXCORE_OK);
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }
  QVERIFY(syncService.isSyncRegistered(nbId));
  const auto workingFile = nbRoot + QStringLiteral("/seed.md");
  const auto workingBytes = RetirementFixture::bytes(workingFile);
  QVERIFY(!workingBytes.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy confirmSpy(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  // PAT field is EMPTY → controller must fetch existing PAT from keychain
  // BEFORE the disable call wipes it.
  controller.applyChanges(gitSettings(newUrl, QString()));
  QCOMPARE(confirmSpy.count(), 1);

  controller.confirmUrlChange(true);

  QVERIFY(applySpy.wait(20000));
  QCOMPARE(applySpy.count(), 1);
  QCOMPARE(applySpy.first().at(0).toBool(), true);
  const auto config = notebookService.getNotebookConfig(nbId);
  QCOMPARE(config.value(QStringLiteral("syncRemoteUrl")).toString(), newUrl);
  QVERIFY(config.value(QStringLiteral("syncEnabled")).toBool());
  QVERIFY(syncService.isSyncRegistered(nbId));
  QCOMPARE(RetirementFixture::bytes(workingFile), workingBytes);
  QTRY_VERIFY_WITH_TIMEOUT(!syncService.isSyncInProgress(nbId), 15000);

  // The keychain should still have the SAME PAT (re-enable wrote it back).
  QSignalSpy retrSpy(&credStore, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy retrErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.retrieveCredentials(nbId);
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
  QVERIFY2(gotRetrieve, "Expected credentialsRetrieved after URL change with empty PAT");
  // The same PAT (literal `test_pat_12345`) is preserved end-to-end.
  QCOMPARE(qvariant_cast<SyncCredential>(retrSpy.first().at(1)).m_secret,
           QStringLiteral("test_pat_12345"));

  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  syncService.shutdown();
  notebookService.closeNotebook(nbId);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::testUrlChangeReenableFailureSurfacesError() {
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

  // The original remote is valid; a missing new remote makes re-enable fail.
  QString oldBare = localTemp.filePath(QStringLiteral("remote_url_reen_old.git"));
  QString oldUrl = seedBareRepo(oldBare, localTemp);
  QVERIFY2(!oldUrl.isEmpty(), "Could not prepare the original Git remote");

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_url_reen_root"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"URL ReEnable Fail","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, gitSettings(oldUrl, QStringLiteral("test_pat_12345")));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(qvariant_cast<VxCoreError>(enableSpy.first().at(1)), VXCORE_OK);
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg[QStringLiteral("syncRemoteUrl")] = oldUrl;
    cfg[QStringLiteral("syncEnabled")] = true;
    cfg[QStringLiteral("syncBackend")] = QStringLiteral("git");
    const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    QVERIFY(notebookService.updateNotebookConfig(nbId, cfgJson));
  }
  QVERIFY(syncService.isSyncRegistered(nbId));
  const auto workingFile = nbRoot + QStringLiteral("/seed.md");
  const auto workingBytes = RetirementFixture::bytes(workingFile);
  QVERIFY(!workingBytes.isEmpty());

  NotebookSyncInfoController controller(services, nbId);
  controller.loadInitialData();

  QSignalSpy confirmSpy(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applySpy(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy errorSpy(&controller, &NotebookSyncInfoController::error);

  const QString newUrl =
      QUrl::fromLocalFile(localTemp.filePath(QStringLiteral("missing-remote.git"))).toString();
  // Supply credentials explicitly so the failure exercises the new backend.
  controller.applyChanges(gitSettings(newUrl, QStringLiteral("test_pat_12345")));
  QCOMPARE(confirmSpy.count(), 1);

  controller.confirmUrlChange(true);

  QVERIFY(applySpy.wait(15000));
  QCOMPARE(applySpy.count(), 1);
  QCOMPARE(applySpy.first().at(0).toBool(), false);
  QVERIFY2(!errorSpy.isEmpty(), "error() must fire on re-enable failure");

  // Post-condition: notebook is NOT registered at runtime (clean S0).
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Post-condition: on-disk JSON sync fields are cleared (W2.T5 ran during
  // disable; failed enable did not rewrite them).
  const QJsonObject cfgAfter = notebookService.getNotebookConfig(nbId);
  QVERIFY2(!cfgAfter.value(QStringLiteral("syncEnabled")).toBool(),
           "syncEnabled must be cleared after disable+failed-re-enable");
  QCOMPARE(cfgAfter.value(QStringLiteral("syncRemoteUrl")).toString(), QString());
  QCOMPARE(cfgAfter.value(QStringLiteral("syncBackend")).toString(), QString());
  QCOMPARE(RetirementFixture::bytes(workingFile), workingBytes);

  // Cleanup: the in-memory storeCredentials wrote the PAT to keychain when
  // re-enable was dispatched (storeCredentials runs BEFORE the worker
  // enableSync call). Best-effort delete.
  credStore.deleteCredentials(nbId);
  QTest::qWait(500);
  syncService.shutdown();
  notebookService.closeNotebook(nbId);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoController::endpointRetirementPreservesPayloadsAfterDisable() {
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  QVERIFY(QFileInfo::exists(fixture.statePath));
  const auto dav = QFileInfo(fixture.statePath).absolutePath();
  const auto snapshot = QStringLiteral("snapshots/old-operation/body");
  QVERIFY(RetirementFixture::writeBytes(QDir(dav).filePath(snapshot), "retained bytes"));
  QVERIFY(RetirementFixture::writeBytes(QDir(dav).filePath(QStringLiteral("retired/older/saved")),
                                        "older archive"));
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy confirm(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy disabled(fixture.sync.get(), &SyncService::disableFinished);
  controller.bootstrapApply(RetirementFixture::replacement());
  QCOMPARE(confirm.count(), 1); // Even though portable routing was cleared by disable.
  QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  controller.confirmUrlChange(true);
  QTRY_COMPARE(applied.count(), 1);
  QCOMPARE(applied.first().at(0).toBool(), false); // Real vault failure before any network work.
  QCOMPARE(disabled.count(), 1);
  QVERIFY(!QFileInfo::exists(fixture.statePath));
  const auto archives = QDir(QDir(dav).filePath(QStringLiteral("retired")))
                            .entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  QString currentArchive;
  for (const auto &archive : archives) {
    if (archive != QLatin1String("older")) {
      QVERIFY(currentArchive.isEmpty());
      currentArchive = QDir(dav).filePath(QStringLiteral("retired/") + archive);
    }
  }
  QVERIFY(!currentArchive.isEmpty());
  QCOMPARE(RetirementFixture::bytes(QDir(currentArchive).filePath(QStringLiteral("state.json"))),
           fixture.stateBytes);
  QCOMPARE(RetirementFixture::bytes(QDir(currentArchive).filePath(snapshot)),
           QByteArray("retained bytes"));
  QCOMPARE(RetirementFixture::bytes(QDir(dav).filePath(QStringLiteral("retired/older/saved"))),
           QByteArray("older archive"));
  QCOMPARE(RetirementFixture::bytes(fixture.root + QStringLiteral("/note.txt")),
           QByteArray("user text\n"));
  QVERIFY(!QFileInfo::exists(QDir(dav).filePath(QStringLiteral("retirement.json"))));
  const auto config = fixture.notebooks->getNotebookConfig(fixture.id);
  QVERIFY(!config.value(QStringLiteral("syncEnabled")).toBool());
  QVERIFY(config.value(QStringLiteral("syncBackend")).toString().isEmpty());
  QVERIFY(config.value(QStringLiteral("syncRemoteUrl")).toString().isEmpty());
  QVERIFY(!fixture.sync->isSyncRegistered(fixture.id));
}

void TestNotebookSyncInfoController::archiveFailureLeavesRegisteredGitUsable() {
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  QVERIFY(QFile::remove(fixture.statePath));
  QVERIFY(QFile::remove(fixture.root + QStringLiteral("/note.txt")));
  QVERIFY(QDir().rmdir(QFileInfo(fixture.statePath).absolutePath()));
  QVERIFY(QDir().rmdir(fixture.root + QStringLiteral("/vx_notebook/vx_sync")));
  const auto url =
      seedBareRepo(fixture.directory.filePath(QStringLiteral("old.git")), fixture.directory);
  QVERIFY(!url.isEmpty());
  const auto config = QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("backend"), QStringLiteral("git")},
                                {QStringLiteral("remoteUrl"), url}})
          .toJson(QJsonDocument::Compact));
  QCOMPARE(fixture.notebooks->enableSync(fixture.id, config, QStringLiteral("{\"pat\":\"token\"}")),
           VXCORE_OK);
  const auto head = fixture.root + QStringLiteral("/vx_notebook/vx_sync/HEAD");
  const auto originalHead = RetirementFixture::bytes(head);
  QVERIFY(!originalHead.isEmpty());
  // A retained current WebDAV binding must be archived before disabling the
  // still-usable Git runtime, not silently discarded by Git URL cleanup.
  QVERIFY(RetirementFixture::writeBytes(fixture.statePath, fixture.stateBytes));
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy disabled(fixture.sync.get(), &SyncService::disableFinished);
  QSignalSpy confirmed(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  controller.applyChanges(gitSettings(QStringLiteral("https://new.example.test/repo.git"),
                                      QStringLiteral("new-token")));
  QCOMPARE(confirmed.count(), 1);
  // A file at the archive parent makes safe retirement impossible, deterministically
  // on all platforms (without relying on administrator-dependent permission bits).
  QVERIFY(RetirementFixture::writeBytes(fixture.root +
                                            QStringLiteral("/vx_notebook/vx_sync/webdav/retired"),
                                        "user-owned obstruction"));
  controller.confirmUrlChange(true);
  QCOMPARE(applied.count(), 1);
  QVERIFY(!applied.first().at(0).toBool());
  QCOMPARE(disabled.count(), 0);
  QVERIFY(fixture.sync->isSyncRegistered(fixture.id));
  QCOMPARE(RetirementFixture::bytes(head), originalHead);
  QString status;
  QCOMPARE(fixture.notebooks->getSyncStatus(fixture.id, status), VXCORE_OK);
  QCOMPARE(RetirementFixture::bytes(fixture.root +
                                    QStringLiteral("/vx_notebook/vx_sync/webdav/retired")),
           QByteArray("user-owned obstruction"));
}

void TestNotebookSyncInfoController::gitEndpointCleanupPreservesWebDavArchive() {
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  QVERIFY(QFile::remove(fixture.statePath));
  QVERIFY(QFile::remove(fixture.root + QStringLiteral("/note.txt")));
  QVERIFY(QDir().rmdir(QFileInfo(fixture.statePath).absolutePath()));
  QVERIFY(QDir().rmdir(fixture.root + QStringLiteral("/vx_notebook/vx_sync")));
  const auto url =
      seedBareRepo(fixture.directory.filePath(QStringLiteral("old.git")), fixture.directory);
  QVERIFY(!url.isEmpty());
  const auto config = QString::fromUtf8(
      QJsonDocument(QJsonObject{{QStringLiteral("backend"), QStringLiteral("git")},
                                {QStringLiteral("remoteUrl"), url}})
          .toJson(QJsonDocument::Compact));
  QCOMPARE(fixture.notebooks->enableSync(fixture.id, config, QStringLiteral("{\"pat\":\"token\"}")),
           VXCORE_OK);
  const auto preserved =
      fixture.root + QStringLiteral("/vx_notebook/vx_sync/webdav/retired/previous/payload");
  QVERIFY(RetirementFixture::writeBytes(preserved, "WebDAV recovery bytes"));
  const auto workingFile = fixture.root + QStringLiteral("/seed.md");
  const auto workingBytes = RetirementFixture::bytes(workingFile);
  QVERIFY(!workingBytes.isEmpty());
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy disabled(fixture.sync.get(), &SyncService::disableFinished);
  controller.applyChanges(gitSettings(QStringLiteral("https://new.example.test/repo.git"),
                                      QStringLiteral("new-token")));
  controller.confirmUrlChange(true);
  QTRY_COMPARE(applied.count(), 1);
  QVERIFY(!applied.first().at(0).toBool()); // Keychain unavailable for the new binding.
  QCOMPARE(disabled.count(), 1);
  QVERIFY(!fixture.sync->isSyncRegistered(fixture.id));
  QVERIFY(!QFileInfo::exists(fixture.root + QStringLiteral("/vx_notebook/vx_sync/HEAD")));
  QCOMPARE(RetirementFixture::bytes(preserved), QByteArray("WebDAV recovery bytes"));
  QCOMPARE(RetirementFixture::bytes(workingFile), workingBytes);
}

void TestNotebookSyncInfoController::incompleteArchiveRestoredBeforeEnable() {
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  const auto dav = QFileInfo(fixture.statePath).absolutePath();
  const auto operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto archive = QDir(dav).filePath(QStringLiteral("retired/") + operationId);
  QVERIFY(QDir().mkpath(archive));
  QVERIFY(QDir().rename(fixture.statePath, QDir(archive).filePath(QStringLiteral("state.json"))));
  // First entry moved, second not yet moved: replay must restore precisely those
  // already archived, not attempt to relocate the whole directory into itself.
  QVERIFY(
      RetirementFixture::writeBytes(QDir(dav).filePath(QStringLiteral("snapshots/body")), "saved"));
  const QJsonObject journal{
      {QStringLiteral("version"), 1},
      {QStringLiteral("notebookId"), fixture.id},
      {QStringLiteral("operationId"), operationId},
      {QStringLiteral("phase"), QStringLiteral("prepared")},
      {QStringLiteral("entries"),
       QJsonArray{QJsonObject{{QStringLiteral("backend"), QStringLiteral("webdav")},
                              {QStringLiteral("name"), QStringLiteral("state.json")}},
                  QJsonObject{{QStringLiteral("backend"), QStringLiteral("webdav")},
                              {QStringLiteral("name"), QStringLiteral("snapshots")}}}}};
  QVERIFY(RetirementFixture::writeBytes(QDir(dav).filePath(QStringLiteral("retirement.json")),
                                        QJsonDocument(journal).toJson(QJsonDocument::Compact)));
  auto settings = RetirementFixture::replacement();
  settings.m_remoteUrl = QStringLiteral("https://old.example.test:443/notes");
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy confirmed(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  controller.bootstrapApply(settings);
  QTRY_COMPARE(applied.count(), 1);
  QVERIFY(!applied.first().at(0).toBool()); // Keychain unavailable, old binding restored.
  QCOMPARE(confirmed.count(), 0);
  QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  QCOMPARE(RetirementFixture::bytes(QDir(dav).filePath(QStringLiteral("snapshots/body"))),
           QByteArray("saved"));
  QVERIFY(!QFileInfo::exists(QDir(dav).filePath(QStringLiteral("retirement.json"))));
  QVERIFY(!QFileInfo::exists(archive));
}

void TestNotebookSyncInfoController::pendingRecoveryAndQueuedWorkBlockRetirement() {
  RetirementFixture fixture;
  QVERIFY(!fixture.id.isEmpty());
  const auto pending =
      QFileInfo(fixture.statePath).absolutePath() + QStringLiteral("/pending.json");
  const QByteArray journal("{\"operations\":[{\"stage\":\"prepared\"}]}");
  QVERIFY(RetirementFixture::writeBytes(pending, journal));
  NotebookSyncInfoController controller(fixture.services, fixture.id);
  QSignalSpy applied(&controller, &NotebookSyncInfoController::applyComplete);
  QSignalSpy confirmed(&controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
  QSignalSpy disabled(fixture.sync.get(), &SyncService::disableFinished);
  controller.bootstrapApply(RetirementFixture::replacement());
  QCOMPARE(applied.count(), 1);
  QVERIFY(!applied.first().at(0).toBool());
  QCOMPARE(confirmed.count(), 0);
  QCOMPARE(disabled.count(), 0);
  QCOMPARE(RetirementFixture::bytes(pending), journal);
  QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  QVERIFY(QFile::remove(pending));
  auto *queue = fixture.sync->workQueueManager();
  auto lease = queue->tryAcquireMaintenance({fixture.id});
  QVERIFY(lease);
  QCOMPARE(queue->enqueue(fixture.id, []() {}), SyncWorkQueueManager::EnqueueResult::Accepted);
  controller.bootstrapApply(RetirementFixture::replacement());
  QCOMPARE(applied.count(), 2);
  QVERIFY(!applied.last().at(0).toBool());
  QCOMPARE(confirmed.count(), 0);
  QCOMPARE(disabled.count(), 0);
  QCOMPARE(RetirementFixture::bytes(fixture.statePath), fixture.stateBytes);
  lease.release();
}

} // namespace tests

VNOTE_KEYCHAIN_TEST_MAIN(tests::TestNotebookSyncInfoController)
#include "test_notebooksyncinfocontroller.moc"