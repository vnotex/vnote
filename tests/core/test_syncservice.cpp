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
  // affected by changes to disk config fields (syncEnabled, syncBackend, etc.).
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncService)
#include "test_syncservice.moc"
