// Invalid settings must stop before vault or backend work.
#include <QSignalSpy>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
class UnexpectedVault final : public SyncCredentialsStore {
public:
  explicit UnexpectedVault(ServiceLocator &p_services) : SyncCredentialsStore(p_services) {}
  void storeCredentials(const QString &, const SyncCredential &) override { ++writes; }
  int writes = 0;
};
} // namespace

class TestSyncService : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void invalidSettings_data();
  void invalidSettings();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncService::initTestCase() {
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
}

void TestSyncService::cleanupTestCase() { vxcore_context_destroy(m_context); }

void TestSyncService::invalidSettings_data() {
  QTest::addColumn<QString>("backend");
  QTest::addColumn<QString>("remote");
  QTest::addColumn<QString>("credentialBackend");
  QTest::addColumn<QString>("username");
  QTest::addColumn<QString>("secret");
  QTest::addColumn<int>("code");
  QTest::newRow("git-empty-token")
      << QStringLiteral("git") << QStringLiteral("https://example.com/repo.git")
      << QStringLiteral("git") << QString() << QString() << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("git-empty-url") << QStringLiteral("git") << QString() << QStringLiteral("git")
                                 << QString() << QStringLiteral("token")
                                 << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-empty-username")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("webdav") << QString() << QStringLiteral("secret")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-empty-password")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("webdav") << QStringLiteral("user") << QString()
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-insecure-url")
      << QStringLiteral("webdav") << QStringLiteral("http://127.0.0.1/notebook/")
      << QStringLiteral("webdav") << QStringLiteral("user") << QStringLiteral("secret")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-rejects-git-credential")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("git") << QStringLiteral("user") << QStringLiteral("token")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("unknown-backend")
      << QStringLiteral("unsupported") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("unsupported") << QStringLiteral("user") << QStringLiteral("secret")
      << int(VXCORE_ERR_UNKNOWN_BACKEND);
}

void TestSyncService::invalidSettings() {
  QFETCH(QString, backend);
  QFETCH(QString, remote);
  QFETCH(QString, credentialBackend);
  QFETCH(QString, username);
  QFETCH(QString, secret);
  QFETCH(int, code);
  ServiceLocator services;
  NotebookCoreService notebooks(m_context);
  UnexpectedVault vault(services);
  services.registerService<NotebookCoreService>(&notebooks);
  services.registerService<SyncCredentialsStore>(&vault);
  SyncService sync(services);
  const QString id = QStringLiteral("invalid-settings-notebook");
  QSignalSpy finished(&sync, &SyncService::enableFinished);
  sync.enableSyncForNotebook(id, {backend, remote, {credentialBackend, username, secret}});
  QTRY_COMPARE(finished.count(), 1);
  QCOMPARE(finished.first().at(1).toInt(), code);
  QCOMPARE(vault.writes, 0);
  QVERIFY(!sync.isSyncRegistered(id));
  QVERIFY(!sync.isSyncInProgress(id));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncService)
#include "test_syncservice.moc"
