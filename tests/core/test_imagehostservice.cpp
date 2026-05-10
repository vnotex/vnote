#include <QtTest>

#include <core/editorconfig.h>
#include <core/services/hookmanager.h>
#include <core/services/imagehostservice.h>
#include <imagehost/iimagehostprovider.h>

using namespace vnotex;

namespace tests {

class TestImageHostService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testCreateProviderGitHub();
  void testCreateProviderGitee();
  void testCreateProviderCustomCommand();
  void testCreateProviderUnknown();
  void testRegisterAndFind();
  void testFindProviderByName();
  void testDefaultProvider();
  void testAvailableTypeIds();
  void testConfigRoundTrip();
  void testRemoveProvider();

private:
  HookManager *m_hookMgr = nullptr;
  ImageHostService *m_service = nullptr;
};

void TestImageHostService::initTestCase() {
  m_hookMgr = new HookManager(this);
  QVERIFY(m_hookMgr != nullptr);
}

void TestImageHostService::cleanupTestCase() {
  delete m_hookMgr;
  m_hookMgr = nullptr;
}

void TestImageHostService::testCreateProviderGitHub() {
  ImageHostService service(m_hookMgr);
  auto *provider = service.createProvider(QStringLiteral("github"), QStringLiteral("myhost"));
  QVERIFY(provider != nullptr);
  QCOMPARE(provider->typeId(), QStringLiteral("github"));
  QCOMPARE(provider->getName(), QStringLiteral("myhost"));
}

void TestImageHostService::testCreateProviderGitee() {
  ImageHostService service(m_hookMgr);
  auto *provider = service.createProvider(QStringLiteral("gitee"), QStringLiteral("myhost"));
  QVERIFY(provider != nullptr);
  QCOMPARE(provider->typeId(), QStringLiteral("gitee"));
  QCOMPARE(provider->getName(), QStringLiteral("myhost"));
}

void TestImageHostService::testCreateProviderCustomCommand() {
  ImageHostService service(m_hookMgr);
  auto *provider =
      service.createProvider(QStringLiteral("custom_command"), QStringLiteral("myhost"));
  QVERIFY(provider != nullptr);
  QCOMPARE(provider->typeId(), QStringLiteral("custom_command"));
  QCOMPARE(provider->getName(), QStringLiteral("myhost"));
}

void TestImageHostService::testCreateProviderUnknown() {
  ImageHostService service(m_hookMgr);
  auto *provider = service.createProvider(QStringLiteral("unknown"), QStringLiteral("test"));
  QVERIFY(provider == nullptr);
}

void TestImageHostService::testRegisterAndFind() {
  ImageHostService service(m_hookMgr);
  auto *provider = service.createProvider(QStringLiteral("github"), QStringLiteral("mygithub"));
  QVERIFY(provider != nullptr);

  // createProvider already parents to service, but we register for the lookup list.
  service.registerProvider(provider);

  auto *found = service.findProvider(QStringLiteral("mygithub"));
  QVERIFY(found != nullptr);
  QCOMPARE(found, provider);
}

void TestImageHostService::testFindProviderByName() {
  ImageHostService service(m_hookMgr);

  auto *gh = service.createProvider(QStringLiteral("github"), QStringLiteral("gh1"));
  auto *gitee = service.createProvider(QStringLiteral("gitee"), QStringLiteral("ge1"));
  auto *cmd = service.createProvider(QStringLiteral("custom_command"), QStringLiteral("cmd1"));

  service.registerProvider(gh);
  service.registerProvider(gitee);
  service.registerProvider(cmd);

  QCOMPARE(service.findProvider(QStringLiteral("gh1")), gh);
  QCOMPARE(service.findProvider(QStringLiteral("ge1")), gitee);
  QCOMPARE(service.findProvider(QStringLiteral("cmd1")), cmd);
  QVERIFY(service.findProvider(QStringLiteral("nonexistent")) == nullptr);
}

void TestImageHostService::testDefaultProvider() {
  ImageHostService service(m_hookMgr);

  auto *gh = service.createProvider(QStringLiteral("github"), QStringLiteral("gh1"));
  auto *gitee = service.createProvider(QStringLiteral("gitee"), QStringLiteral("ge1"));

  service.registerProvider(gh);
  service.registerProvider(gitee);

  // No default initially.
  QVERIFY(service.getDefaultProvider() == nullptr);

  service.setDefaultProvider(QStringLiteral("gh1"));
  QCOMPARE(service.getDefaultProvider(), gh);

  service.setDefaultProvider(QStringLiteral("ge1"));
  QCOMPARE(service.getDefaultProvider(), gitee);
}

void TestImageHostService::testAvailableTypeIds() {
  ImageHostService service(m_hookMgr);
  QStringList types = service.availableTypeIds();

  QVERIFY(types.contains(QStringLiteral("github")));
  QVERIFY(types.contains(QStringLiteral("gitee")));
  QVERIFY(types.contains(QStringLiteral("custom_command")));
}

void TestImageHostService::testConfigRoundTrip() {
  // Create a service with some providers.
  ImageHostService service1(m_hookMgr);

  auto *gh = service1.createProvider(QStringLiteral("github"), QStringLiteral("myGitHub"));
  QJsonObject ghConfig;
  ghConfig[QStringLiteral("token")] = QStringLiteral("abc123");
  gh->setConfig(ghConfig);
  service1.registerProvider(gh);

  auto *cmd = service1.createProvider(QStringLiteral("custom_command"), QStringLiteral("myCmd"));
  QJsonObject cmdConfig;
  cmdConfig[QStringLiteral("command")] = QStringLiteral("echo test");
  cmd->setConfig(cmdConfig);
  service1.registerProvider(cmd);

  service1.setDefaultProvider(QStringLiteral("myGitHub"));

  // Save to config.
  QVector<ImageHostItem> items = service1.saveToConfig();
  QCOMPARE(items.size(), 2);

  // Load into a new service.
  ImageHostService service2(m_hookMgr);
  service2.loadFromConfig(items, QStringLiteral("myGitHub"));

  // Verify providers exist in new service.
  QVERIFY(service2.findProvider(QStringLiteral("myGitHub")) != nullptr);
  QVERIFY(service2.findProvider(QStringLiteral("myCmd")) != nullptr);
  QCOMPARE(service2.findProvider(QStringLiteral("myGitHub"))->typeId(), QStringLiteral("github"));
  QCOMPARE(service2.findProvider(QStringLiteral("myCmd"))->typeId(),
           QStringLiteral("custom_command"));

  // Default provider restored.
  QVERIFY(service2.getDefaultProvider() != nullptr);
  QCOMPARE(service2.getDefaultProvider()->getName(), QStringLiteral("myGitHub"));
}

void TestImageHostService::testRemoveProvider() {
  ImageHostService service(m_hookMgr);

  auto *gh = service.createProvider(QStringLiteral("github"), QStringLiteral("toRemove"));
  service.registerProvider(gh);

  QVERIFY(service.findProvider(QStringLiteral("toRemove")) != nullptr);

  service.removeProvider(gh);

  QVERIFY(service.findProvider(QStringLiteral("toRemove")) == nullptr);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestImageHostService)
#include "test_imagehostservice.moc"
