// Tests for SyncService — the GUI-thread facade owning a SyncWorker on a
// private QThread and wrapping SyncCredentialsStore.
//
// These tests verify:
//   1. roundtripViaServiceLocator: a real ServiceLocator wired with
//      NotebookCoreService + SyncCredentialsStore + SyncService can enable
//      sync against a seeded bare-repo file:// URL. enableFinished arrives
//      with VXCORE_OK.
//   2. inProgressFlagToggles: isSyncInProgress() flips true on syncStarted
//      and back to false on syncFinished.
//   3. testSetInProgressSeam: testSetInProgress() directly mutates the
//      in-progress map (used by T17 for BlockClose simulations).
//
// Per ADR-1: this test never includes sync/sync_manager.h.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncworker.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestSyncService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void roundtripViaServiceLocator();
  void inProgressFlagToggles();
  void testSetInProgressSeam();
  void testIsSyncRegisteredTrueWhenStatesEntry();
  void testIsSyncRegisteredFalseWhenAbsent();
  void testIsSyncRegisteredUnaffectedByDiskFields();

  // W2.T3 — F4 chicken-and-egg fix: updateCredentials must route to
  // enableSyncForNotebook when the notebook is sync-enabled on disk but
  // not registered in vxcore's runtime states_ map.
  void testUpdateCredentialsOnRegisteredCallsSetCreds();
  void testUpdateCredentialsOnUnregisteredCallsEnable();
  void testUpdateCredentialsOnUnregisteredNoUrlEmitsError();
  void testUpdateCredentialsEmitsCredentialsSetFinishedInBothPaths();

  // W2.T5 — disable JSON clear + S6 orphan PAT cleanup. Closes B8 (disable
  // resurrection trap) by clearing on-disk sync fields after successful
  // disable; closes S6 by sweeping orphan keychain entries on app start.
  void testDisableClearsJsonFields();
  void testDisableKeepsJsonOnFailure();
  void testS6OrphanPatCleanedOnAppStart();

private:
  // Initialize a bare git repo at p_bareRepoPath and seed it with one commit
  // containing a single file "seed.md" on the default branch (main). Returns
  // the file:// URL suitable for cloning, or empty on failure (e.g., git not
  // installed). Mirrors the helper in test_notebookcoreservice_sync.cpp.
  QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);

  // Wait for either of two signal spies, returning 1 if @p_a fired first,
  // 2 if @p_b fired first, or 0 on timeout.
  static int waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs);
};

void TestSyncService::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
}

void TestSyncService::cleanupTestCase() {}

int TestSyncService::waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < p_timeoutMs) {
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

QString TestSyncService::seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp) {
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

void TestSyncService::roundtripViaServiceLocator() {
  // Create vxcore context.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  // Wire a real ServiceLocator with the dependencies SyncService needs.
  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Seed a bare repo and create a notebook root.
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
      nbRoot, R"({"name":"Sync NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Spy on enableFinished. Use a non-empty PAT so the keychain-store call has
  // something concrete to write; the file:// remote does not enforce auth.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_T7"));

  // Wait for enableFinished. Timeout generous to allow keychain + clone time.
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);

  const auto args = enableSpy.first();
  QCOMPARE(args.at(0).toString(), nbId);
  const VxCoreError result = qvariant_cast<VxCoreError>(args.at(1));
  // If the keychain backend is not usable in this test environment, our
  // facade will report VXCORE_ERR_UNKNOWN with a backend error message.
  // Accept that as a documented skip rather than a test failure.
  if (result == VXCORE_ERR_UNKNOWN) {
    qWarning() << "enableSyncForNotebook returned VXCORE_ERR_UNKNOWN; message:"
               << args.at(2).toString();
    // Cleanup keychain entry best-effort and skip.
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(result, VXCORE_OK);

  // Clean up keychain entry created by the store.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbId);
  waitForEither(delSpy, delErr, 5000);

  vxcore_context_destroy(ctx);
}

void TestSyncService::inProgressFlagToggles() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  const QString nbId = QStringLiteral("nb_inprogress");

  QSignalSpy startedSpy(&syncService, &SyncService::syncStarted);
  QSignalSpy finishedSpy(&syncService, &SyncService::syncFinished);

  // Arm the worker's hang seam so syncStarted is delivered first and there is
  // a window to observe the in-progress flag before syncFinished arrives.
  syncService.worker()->testHangNextOperation(500);

  // Trigger sync against a non-existent notebook ID. The vxcore call will
  // fail (VXCORE_ERR_NOT_FOUND or similar), but the syncStarted/syncFinished
  // sequence still fires from the worker.
  syncService.triggerSyncNow(nbId);

  // Wait for syncStarted to arrive on the GUI thread.
  QVERIFY(startedSpy.wait(5000));
  QCOMPARE(startedSpy.count(), 1);
  QCOMPARE(startedSpy.first().at(0).toString(), nbId);
  // Inside the started window the flag must read true. The spy and our internal
  // forwarder are both connected to the worker's syncStarted signal; depending
  // on connection ordering the spy may fire before our internal flag-set slot.
  // Drain pending queued events so the internal slot has run before we read
  // the flag.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  QVERIFY(syncService.isSyncInProgress(nbId));

  // Wait for syncFinished.
  QVERIFY(finishedSpy.wait(5000));
  QCOMPARE(finishedSpy.count(), 1);
  QCOMPARE(finishedSpy.first().at(0).toString(), nbId);

  // Allow GUI-thread queued slots a moment to run after the spy unblocks.
  QTest::qWait(50);
  QVERIFY(!syncService.isSyncInProgress(nbId));

  vxcore_context_destroy(ctx);
}

void TestSyncService::testSetInProgressSeam() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  QVERIFY(!syncService.isSyncInProgress(QStringLiteral("nbA")));
  syncService.testSetInProgress(QStringLiteral("nbA"), true);
  QVERIFY(syncService.isSyncInProgress(QStringLiteral("nbA")));
  syncService.testSetInProgress(QStringLiteral("nbA"), false);
  QVERIFY(!syncService.isSyncInProgress(QStringLiteral("nbA")));

  vxcore_context_destroy(ctx);
}

void TestSyncService::testIsSyncRegisteredTrueWhenStatesEntry() {
  // Test that isSyncRegistered returns true after enableSyncForNotebook
  // successfully registers the notebook in vxcore's runtime states_.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Seed a bare repo and create a notebook.
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_registered"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Registered NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Before enable: isSyncRegistered should return false.
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Enable sync and wait for completion.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("test_pat_12345"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);

  const auto args = enableSpy.first();
  const VxCoreError result = qvariant_cast<VxCoreError>(args.at(1));
  if (result == VXCORE_ERR_UNKNOWN) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(result, VXCORE_OK);

  // After enable: isSyncRegistered should return true (states_[nbId] exists).
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Cleanup.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbId);
  waitForEither(delSpy, delErr, 5000);

  vxcore_context_destroy(ctx);
}

void TestSyncService::testIsSyncRegisteredFalseWhenAbsent() {
  // Test that isSyncRegistered returns false for a notebook that has
  // sync enabled on disk (isSyncReady=true) but has never been registered
  // at runtime (no enableSyncForNotebook call in this process).
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Create a notebook with sync enabled on disk.
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_unregistered"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Unregistered NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Manually set sync config on disk (without calling enableSyncForNotebook).
  QJsonObject cfg;
  cfg.insert(QStringLiteral("syncEnabled"), true);
  cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfg.insert(QStringLiteral("syncRemoteUrl"), QStringLiteral("file:///fake/remote.git"));
  QJsonDocument doc(cfg);
  notebookService.updateNotebookConfig(nbId, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

  // isSyncReady should return true (disk config is complete).
  QVERIFY(syncService.isSyncReady(nbId));

  // But isSyncRegistered should return false (no runtime states_ entry).
  QVERIFY(!syncService.isSyncRegistered(nbId));

  vxcore_context_destroy(ctx);
}

void TestSyncService::testIsSyncRegisteredUnaffectedByDiskFields() {
  // Test that isSyncRegistered queries runtime state only and is not
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Create a notebook with sync enabled on disk.
  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_disk_unaffected"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Disk Unaffected NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Set sync config on disk.
  QJsonObject cfg;
  cfg.insert(QStringLiteral("syncEnabled"), true);
  cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfg.insert(QStringLiteral("syncRemoteUrl"), QStringLiteral("file:///fake/remote.git"));
  QJsonDocument doc(cfg);
  notebookService.updateNotebookConfig(nbId, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

  // isSyncRegistered is false (no runtime registration).
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Disable sync on disk.
  cfg.insert(QStringLiteral("syncEnabled"), false);
  QJsonDocument doc2(cfg);
  notebookService.updateNotebookConfig(nbId,
                                       QString::fromUtf8(doc2.toJson(QJsonDocument::Compact)));

  // isSyncRegistered should still be false (disk change doesn't affect runtime state).
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Re-enable sync on disk.
  cfg.insert(QStringLiteral("syncEnabled"), true);
  QJsonDocument doc3(cfg);
  notebookService.updateNotebookConfig(nbId,
                                       QString::fromUtf8(doc3.toJson(QJsonDocument::Compact)));

  // isSyncRegistered should still be false (disk change doesn't affect runtime state).
  QVERIFY(!syncService.isSyncRegistered(nbId));

  vxcore_context_destroy(ctx);
}

// =====================================================================
// W2.T3 — updateCredentials chicken-and-egg routing (F4)
// =====================================================================

void TestSyncService::testUpdateCredentialsOnRegisteredCallsSetCreds() {
  // After a notebook is registered (post-enableSyncForNotebook), updateCredentials
  // must take the setCredentials worker dispatch path — NOT the
  // enableSyncForNotebook routing path. Verification: credentialsSetFinished
  // fires exactly once and no spurious enableFinished is emitted by the
  // updateCredentials call itself.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_registered_set"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Registered Set NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Phase 1: Enable to register the notebook in vxcore states_.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("test_pat_12345"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  const VxCoreError enableResult = qvariant_cast<VxCoreError>(enableSpy.first().at(1));
  if (enableResult == VXCORE_ERR_UNKNOWN) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(enableResult, VXCORE_OK);
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Phase 2: Call updateCredentials and verify the setCreds path runs (not
  // the enable routing path). Clear the enableSpy first so we can detect any
  // spurious enableFinished emission caused by accidental routing.
  enableSpy.clear();
  QSignalSpy credsSpy(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(nbId, QStringLiteral("test_pat_12345"));

  QVERIFY(credsSpy.wait(15000));
  QCOMPARE(credsSpy.count(), 1);
  QCOMPARE(credsSpy.first().at(0).toString(), nbId);
  // SetCredentials on a properly-registered notebook should return VXCORE_OK
  // (per vxcore SetCredentials contract — states_[id] exists).
  const VxCoreError credsResult = qvariant_cast<VxCoreError>(credsSpy.first().at(1));
  QCOMPARE(credsResult, VXCORE_OK);

  // Critical assertion: NO enableFinished should be emitted because we took
  // the setCreds path, not the enable routing path.
  QCOMPARE(enableSpy.count(), 0);

  // Cleanup.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbId);
  waitForEither(delSpy, delErr, 5000);

  vxcore_context_destroy(ctx);
}

void TestSyncService::testUpdateCredentialsOnUnregisteredCallsEnable() {
  // Notebook with disk syncEnabled=true and valid syncRemoteUrl but no prior
  // enable call in this process. updateCredentials must route to
  // enableSyncForNotebook, which registers the notebook and re-emits as
  // credentialsSetFinished. After the call, isSyncRegistered must return true.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_unreg_enable"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Unregistered Enable NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Persist sync config to disk WITHOUT calling enableSyncForNotebook (so
  // states_ stays empty: isSyncRegistered=false, isSyncEnabled=true).
  QJsonObject cfg;
  cfg.insert(QStringLiteral("syncEnabled"), true);
  cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfg.insert(QStringLiteral("syncRemoteUrl"), remoteUrl);
  notebookService.updateNotebookConfig(
      nbId, QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));

  // Preconditions: enabled on disk, not registered at runtime.
  QVERIFY(syncService.isSyncEnabled(nbId));
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Act: updateCredentials must route to enableSyncForNotebook.
  QSignalSpy credsSpy(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(nbId, QStringLiteral("test_pat_12345"));

  QVERIFY(credsSpy.wait(15000));
  QCOMPARE(credsSpy.count(), 1);
  QCOMPARE(credsSpy.first().at(0).toString(), nbId);
  const VxCoreError credsResult = qvariant_cast<VxCoreError>(credsSpy.first().at(1));
  if (credsResult == VXCORE_ERR_UNKNOWN) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(credsResult, VXCORE_OK);

  // Critical assertion: the notebook is NOW registered in vxcore's states_
  // (the full enable flow ran, not just SetCredentials).
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Cleanup.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbId);
  waitForEither(delSpy, delErr, 5000);

  vxcore_context_destroy(ctx);
}

void TestSyncService::testUpdateCredentialsOnUnregisteredNoUrlEmitsError() {
  // Notebook with syncEnabled=true but empty syncRemoteUrl on disk.
  // updateCredentials must emit credentialsSetFinished with
  // VXCORE_ERR_INVALID_PARAM and NOT dispatch any worker call.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_unreg_nourl"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"No URL NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Persist syncEnabled=true but leave syncRemoteUrl empty.
  QJsonObject cfg;
  cfg.insert(QStringLiteral("syncEnabled"), true);
  cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfg.insert(QStringLiteral("syncRemoteUrl"), QString());
  notebookService.updateNotebookConfig(
      nbId, QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));

  // Preconditions: enabled on disk, not registered, URL empty.
  QVERIFY(syncService.isSyncEnabled(nbId));
  QVERIFY(!syncService.isSyncRegistered(nbId));

  // Act: updateCredentials must immediately fail with INVALID_PARAM.
  QSignalSpy credsSpy(&syncService, &SyncService::credentialsSetFinished);
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.updateCredentials(nbId, QStringLiteral("test_pat_12345"));

  // credentialsSetFinished is emitted synchronously in this error branch.
  // Allow a short event-loop drain in case of any queued delivery.
  if (credsSpy.isEmpty()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QCOMPARE(credsSpy.count(), 1);
  QCOMPARE(credsSpy.first().at(0).toString(), nbId);
  const VxCoreError credsResult = qvariant_cast<VxCoreError>(credsSpy.first().at(1));
  QCOMPARE(credsResult, VXCORE_ERR_INVALID_PARAM);

  // Critical assertion: NO enable was dispatched (URL precondition failed
  // before routing).
  QCOMPARE(enableSpy.count(), 0);

  // No keychain entry should have been stored (we never reached the routing
  // path). Notebook should remain unregistered.
  QVERIFY(!syncService.isSyncRegistered(nbId));

  vxcore_context_destroy(ctx);
}

void TestSyncService::testUpdateCredentialsEmitsCredentialsSetFinishedInBothPaths() {
  // API contract verification: BOTH paths (registered -> setCreds AND
  // unregistered -> enable routing) must emit credentialsSetFinished
  // exactly once with the notebookId of the call. Callers may rely on
  // credentialsSetFinished as the single completion signal regardless of
  // which path runs internally.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  // ---- Path A: unregistered (routes to enable) ----
  QString bareDirA = localTemp.filePath(QStringLiteral("remoteA.git"));
  QString remoteUrlA = seedBareRepo(bareDirA, localTemp);
  if (remoteUrlA.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRootA = localTemp.filePath(QStringLiteral("nb_path_a"));
  QDir().mkpath(nbRootA);
  QString nbIdA = notebookService.createNotebook(
      nbRootA, R"({"name":"Path A NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbIdA.isEmpty());

  QJsonObject cfgA;
  cfgA.insert(QStringLiteral("syncEnabled"), true);
  cfgA.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfgA.insert(QStringLiteral("syncRemoteUrl"), remoteUrlA);
  notebookService.updateNotebookConfig(
      nbIdA, QString::fromUtf8(QJsonDocument(cfgA).toJson(QJsonDocument::Compact)));
  QVERIFY(!syncService.isSyncRegistered(nbIdA));

  QSignalSpy credsSpyA(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(nbIdA, QStringLiteral("test_pat_12345"));
  QVERIFY(credsSpyA.wait(15000));
  QCOMPARE(credsSpyA.count(), 1);
  QCOMPARE(credsSpyA.first().at(0).toString(), nbIdA);
  const VxCoreError resultA = qvariant_cast<VxCoreError>(credsSpyA.first().at(1));
  if (resultA == VXCORE_ERR_UNKNOWN) {
    credStore.deleteCredentials(nbIdA);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(resultA, VXCORE_OK);

  // ---- Path B: registered (uses setCreds) ----
  // Reuse nbIdA which is now registered after Path A's routing completed.
  QVERIFY(syncService.isSyncRegistered(nbIdA));

  QSignalSpy credsSpyB(&syncService, &SyncService::credentialsSetFinished);
  syncService.updateCredentials(nbIdA, QStringLiteral("test_pat_12345"));
  QVERIFY(credsSpyB.wait(15000));
  QCOMPARE(credsSpyB.count(), 1);
  QCOMPARE(credsSpyB.first().at(0).toString(), nbIdA);
  const VxCoreError resultB = qvariant_cast<VxCoreError>(credsSpyB.first().at(1));
  QCOMPARE(resultB, VXCORE_OK);

  // Cleanup.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErr(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.deleteCredentials(nbIdA);
  waitForEither(delSpy, delErr, 5000);

  vxcore_context_destroy(ctx);
}

// W2.T5 tests appended below.

void TestSyncService::testDisableClearsJsonFields() {
  // After a successful disable, syncEnabled / syncBackend / syncRemoteUrl
  // must be cleared from the on-disk notebook config (vxcore disable_sync
  // only clears in-memory maps per W1.T2 evidence; the Qt-side W2.T5 reset
  // is what closes S0 transition). The keychain entry and runtime registration
  // must also be gone.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString bareDir = localTemp.filePath(QStringLiteral("remote.git"));
  QString remoteUrl = seedBareRepo(bareDir, localTemp);
  if (remoteUrl.isEmpty()) {
    vxcore_context_destroy(ctx);
    QSKIP("git not available or bare-repo seeding failed");
  }

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_disable_clear"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Disable Clear NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Phase 1: Enable so notebook is registered in runtime states_.
  QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
  syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("test_pat_12345"));
  QVERIFY(enableSpy.wait(15000));
  QCOMPARE(enableSpy.count(), 1);
  const VxCoreError enableResult = qvariant_cast<VxCoreError>(enableSpy.first().at(1));
  if (enableResult == VXCORE_ERR_UNKNOWN) {
    credStore.deleteCredentials(nbId);
    QTest::qWait(500);
    vxcore_context_destroy(ctx);
    QSKIP("OS keychain backend not usable in this test environment");
  }
  QCOMPARE(enableResult, VXCORE_OK);

  // Write the flat sync keys to JSON to simulate the post-bootstrap on-disk
  // state (these are normally written by NewNotebookController, not by
  // enableSyncForNotebook itself per W1.T2 evidence).
  {
    QJsonObject cfg = notebookService.getNotebookConfig(nbId);
    cfg.insert(QStringLiteral("syncEnabled"), true);
    cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
    cfg.insert(QStringLiteral("syncRemoteUrl"), remoteUrl);
    QVERIFY(notebookService.updateNotebookConfig(
        nbId, QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact))));
  }

  // Sanity preconditions before disable.
  QVERIFY(syncService.isSyncEnabled(nbId));
  QVERIFY(syncService.isSyncRegistered(nbId));

  // Phase 2: Disable. Wait for disableFinished, then drain to let the
  // post-finished JSON-clear + deleteCredentials path complete.
  QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);
  QSignalSpy credDelSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy credDelErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  syncService.disableSyncForNotebook(nbId);
  QVERIFY(disableSpy.wait(10000));
  QCOMPARE(disableSpy.count(), 1);
  QCOMPARE(qvariant_cast<VxCoreError>(disableSpy.first().at(1)), VXCORE_OK);

  waitForEither(credDelSpy, credDelErrSpy, 5000);

  // Assertion 1: JSON sync fields are cleared.
  const QJsonObject afterCfg = notebookService.getNotebookConfig(nbId);
  QVERIFY2(!afterCfg.contains(QStringLiteral("syncEnabled")) ||
               afterCfg.value(QStringLiteral("syncEnabled")).toBool() == false,
           "syncEnabled should be absent or false after disable");
  QVERIFY2(!afterCfg.contains(QStringLiteral("syncBackend")) ||
               afterCfg.value(QStringLiteral("syncBackend")).toString().isEmpty(),
           "syncBackend should be absent or empty after disable");
  QVERIFY2(!afterCfg.contains(QStringLiteral("syncRemoteUrl")) ||
               afterCfg.value(QStringLiteral("syncRemoteUrl")).toString().isEmpty(),
           "syncRemoteUrl should be absent or empty after disable");

  // Assertion 2: keychain entry is gone.
  QVERIFY(!credStore.hasCredentials(nbId));

  // Assertion 3: runtime registration is gone.
  QVERIFY(!syncService.isSyncRegistered(nbId));

  vxcore_context_destroy(ctx);
}

void TestSyncService::testDisableKeepsJsonOnFailure() {
  // When vxcore disable_sync fails (simulated via SyncWorker::testForceError),
  // the on-disk JSON sync fields must NOT be cleared so the user can retry.
  // The keychain entry is still removed (best-effort) — this test focuses on
  // the JSON preservation invariant.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_disable_fail"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"Disable Fail NB","description":"","version":"1"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Persist sync config on disk WITHOUT runtime enable. The forced error
  // bypasses the actual vxcore call.
  const QString remoteUrl = QStringLiteral("file:///fake/remote.git");
  QJsonObject cfg;
  cfg.insert(QStringLiteral("syncEnabled"), true);
  cfg.insert(QStringLiteral("syncBackend"), QStringLiteral("git"));
  cfg.insert(QStringLiteral("syncRemoteUrl"), remoteUrl);
  QVERIFY(notebookService.updateNotebookConfig(
      nbId, QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact))));

  // Force the next worker disable to report a synthetic failure code.
  syncService.worker()->testForceError(static_cast<int>(VXCORE_ERR_UNKNOWN));

  QSignalSpy disableSpy(&syncService, &SyncService::disableFinished);
  syncService.disableSyncForNotebook(nbId);
  QVERIFY(disableSpy.wait(10000));
  QCOMPARE(disableSpy.count(), 1);
  QCOMPARE(qvariant_cast<VxCoreError>(disableSpy.first().at(1)), VXCORE_ERR_UNKNOWN);

  // Allow the disable callback's keychain delete + any pending events to drain.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

  // CRITICAL assertion: JSON sync fields are PRESERVED on failure so the
  // user can retry without losing config state.
  const QJsonObject afterCfg = notebookService.getNotebookConfig(nbId);
  QCOMPARE(afterCfg.value(QStringLiteral("syncEnabled")).toBool(), true);
  QCOMPARE(afterCfg.value(QStringLiteral("syncBackend")).toString(), QStringLiteral("git"));
  QCOMPARE(afterCfg.value(QStringLiteral("syncRemoteUrl")).toString(), remoteUrl);

  vxcore_context_destroy(ctx);
}

void TestSyncService::testS6OrphanPatCleanedOnAppStart() {
  // S6 invariant: a notebook with syncEnabled=false (or absent) on disk but
  // a PAT still in the keychain (left over from a legacy disable that did
  // not delete credentials) must have its orphan PAT removed when the
  // MainWindowAfterStart hook fires.
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);
  SyncService syncService(services);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbRoot = localTemp.filePath(QStringLiteral("nb_s6_orphan"));
  QDir().mkpath(nbRoot);
  QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"S6 Orphan NB","description":"","version":"1"})", NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Disk says sync is NOT enabled (syncEnabled absent or false). Default
  // notebook config has no sync keys, so leave it as-is.
  QVERIFY(!syncService.isSyncEnabled(nbId));

  // Plant an orphan PAT in the keychain.
  QSignalSpy storedSpy(&credStore, &SyncCredentialsStore::credentialsStored);
  QSignalSpy storedErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  credStore.storeCredentials(nbId, QStringLiteral("test_pat_12345"));
  const int storedWho = waitForEither(storedSpy, storedErrSpy, 5000);
  if (storedWho != 1) {
    QString reason = storedErrSpy.isEmpty() ? QStringLiteral("timeout storing keychain credentials")
                                            : storedErrSpy.first().at(1).toString();
    vxcore_context_destroy(ctx);
    QSKIP(qPrintable(QStringLiteral("OS keychain backend not usable: ") + reason));
  }
  QVERIFY(credStore.hasCredentials(nbId));

  // Fire the hook the same way MainWindow2 does at startup. The SyncService
  // ctor registered an action for MainWindowAfterStart which invokes
  // onMainWindowAfterStart() (S6 sweep included).
  hookMgr.doAction(HookNames::MainWindowAfterStart, QVariantMap());

  // The S6 sweep schedules a deleteCredentials call; wait for the signal.
  QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy delErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
  waitForEither(delSpy, delErrSpy, 5000);

  // CRITICAL assertion: orphan PAT has been removed.
  QVERIFY(!credStore.hasCredentials(nbId));

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncService)
#include "test_syncservice.moc"
