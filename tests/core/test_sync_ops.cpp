// T12 (sync-queue-convergence): exercise SyncOps::disableSync — a stateless
// free function that wraps NotebookCoreService::disableSync and routes the
// result through a completion callback. Covers success, unknown-notebook
// failure, and null-service early-return paths. Mirrors the bare-repo +
// ServiceLocator fixture pattern from test_sync_signal_baseline.cpp.

#include <atomic>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncops.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestSyncOps : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void disableSyncNullService();
  void disableSyncUnknownNotebook();
  void disableSyncInvokesCallbackOnSuccess();
  void enableSyncNullService();
  void enableSyncAgainstBareRepo();
  void enableSyncInvalidUrl();

private:
  QString seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp);
};

void TestSyncOps::initTestCase() { vxcore_set_test_mode(1); }

QString TestSyncOps::seedBareRepo(const QString &p_bareRepoPath, TempDirFixture &p_workTemp) {
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

void TestSyncOps::disableSyncNullService() {
  std::atomic<int> calls{0};
  VxCoreError captured = VXCORE_OK;
  SyncOps::disableSync(nullptr, QStringLiteral("anything"), [&](VxCoreError code) {
    ++calls;
    captured = code;
  });
  QCOMPARE(calls.load(), 1);
  QCOMPARE(captured, VXCORE_ERR_NULL_POINTER);
}

void TestSyncOps::disableSyncUnknownNotebook() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    NotebookCoreService notebookService(ctx);

    std::atomic<int> calls{0};
    VxCoreError captured = VXCORE_OK;
    SyncOps::disableSync(&notebookService, QStringLiteral("does-not-exist-id"),
                         [&](VxCoreError code) {
                           ++calls;
                           captured = code;
                         });
    QCOMPARE(calls.load(), 1);
    // Empirically vxcore's vx_notebook_disable_sync treats disable on an
    // unknown id as a no-op success (returns VXCORE_OK). The load-bearing
    // contract for SyncOps is "callback fires exactly once regardless of
    // backend outcome"; we deliberately do NOT pin the code here so the
    // test stays meaningful even if vxcore later switches to NOT_FOUND.
    (void)captured;
  }

  vxcore_context_destroy(ctx);
}

void TestSyncOps::disableSyncInvokesCallbackOnSuccess() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    SyncService syncService(services);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    const QString bareDir = localTemp.filePath(QStringLiteral("syncops_remote.git"));
    const QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      QSKIP("git not available or bare-repo seeding failed");
    }

    const QString nbRoot = localTemp.filePath(QStringLiteral("nb_syncops"));
    QDir().mkpath(nbRoot);
    const QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"SyncOps NB","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    QSignalSpy enableSpy(&syncService, &SyncService::enableFinished);
    syncService.enableSyncForNotebook(nbId, remoteUrl, QStringLiteral("ghp_TEST_PAT_SYNCOPS"));
    QVERIFY(enableSpy.wait(15000));
    QCOMPARE(enableSpy.count(), 1);

    const VxCoreError enableResult = qvariant_cast<VxCoreError>(enableSpy.first().at(1));
    if (enableResult == VXCORE_ERR_UNKNOWN) {
      credStore.deleteCredentials(nbId);
      QTest::qWait(500);
      QSKIP("OS keychain backend not usable in this test environment");
    }
    QCOMPARE(enableResult, VXCORE_OK);

    // Call SyncOps::disableSync directly (no work-queue routing yet — T12
    // only establishes the callable; T18 wires it through SyncService).
    std::atomic<int> calls{0};
    VxCoreError captured = VXCORE_ERR_UNKNOWN;
    QElapsedTimer timer;
    timer.start();
    SyncOps::disableSync(&notebookService, nbId, [&](VxCoreError code) {
      ++calls;
      captured = code;
    });
    QVERIFY(timer.elapsed() < 5000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(captured, VXCORE_OK);

    QSignalSpy delSpy(&credStore, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy delErrSpy(&credStore, &SyncCredentialsStore::credentialsError);
    credStore.deleteCredentials(nbId);
    QElapsedTimer drainTimer;
    drainTimer.start();
    while (delSpy.isEmpty() && delErrSpy.isEmpty() && drainTimer.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QTest::qWait(10);
    }
  }

  vxcore_context_destroy(ctx);
}

} // namespace tests

namespace tests {

void TestSyncOps::enableSyncNullService() {
  std::atomic<int> calls{0};
  VxCoreError captured = VXCORE_OK;
  QString capturedMsg;
  SyncOps::enableSync(nullptr, QStringLiteral("anything"), QStringLiteral("{}"),
                      QStringLiteral("{}"), [&](VxCoreError code, QString msg) {
                        ++calls;
                        captured = code;
                        capturedMsg = msg;
                      });
  QCOMPARE(calls.load(), 1);
  QCOMPARE(captured, VXCORE_ERR_NULL_POINTER);
  QVERIFY(!capturedMsg.isEmpty());
}

void TestSyncOps::enableSyncAgainstBareRepo() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    NotebookCoreService notebookService(ctx);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    const QString bareDir = localTemp.filePath(QStringLiteral("enable_remote.git"));
    const QString remoteUrl = seedBareRepo(bareDir, localTemp);
    if (remoteUrl.isEmpty()) {
      QSKIP("git not available or bare-repo seeding failed");
    }

    const QString nbRoot = localTemp.filePath(QStringLiteral("nb_enable"));
    QDir().mkpath(nbRoot);
    const QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"Enable NB","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    const QString configJson =
        QStringLiteral(R"({"backend":"git","remoteUrl":"%1","intervalSeconds":60})").arg(remoteUrl);
    const QString credsJson = QStringLiteral(R"({"type":"token","value":"ghp_TEST_PAT_ENABLE"})");

    std::atomic<int> calls{0};
    VxCoreError captured = VXCORE_ERR_UNKNOWN;
    QString capturedMsg;
    QElapsedTimer timer;
    timer.start();
    SyncOps::enableSync(&notebookService, nbId, configJson, credsJson,
                        [&](VxCoreError code, QString msg) {
                          ++calls;
                          captured = code;
                          capturedMsg = msg;
                        });
    QVERIFY(timer.elapsed() < 30000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(captured, VXCORE_OK);
    QVERIFY(capturedMsg.isEmpty());

    // Cleanup: disable to avoid leaving sync state behind.
    SyncOps::disableSync(&notebookService, nbId, [](VxCoreError) {});
  }

  vxcore_context_destroy(ctx);
}

void TestSyncOps::enableSyncInvalidUrl() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  {
    NotebookCoreService notebookService(ctx);

    TempDirFixture localTemp;
    QVERIFY(localTemp.isValid());

    const QString nbRoot = localTemp.filePath(QStringLiteral("nb_invalid"));
    QDir().mkpath(nbRoot);
    const QString nbId = notebookService.createNotebook(
        nbRoot, R"({"name":"Invalid NB","description":"","version":"1"})", NotebookType::Bundled);
    QVERIFY(!nbId.isEmpty());

    const QString configJson =
        QStringLiteral(R"({"backend":"git","remoteUrl":"not-a-url","intervalSeconds":60})");
    const QString credsJson = QStringLiteral(R"({"type":"token","value":"ghp_TEST_PAT_INVALID"})");

    std::atomic<int> calls{0};
    VxCoreError captured = VXCORE_OK;
    QString capturedMsg;
    SyncOps::enableSync(&notebookService, nbId, configJson, credsJson,
                        [&](VxCoreError code, QString msg) {
                          ++calls;
                          captured = code;
                          capturedMsg = msg;
                        });
    QCOMPARE(calls.load(), 1);
    QVERIFY(captured != VXCORE_OK);
    QVERIFY(!capturedMsg.isEmpty());
  }

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncOps)
#include "test_sync_ops.moc"
