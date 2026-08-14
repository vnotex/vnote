// ImageHostController::persistToConfig(): snapshot the live ImageHostService
// providers + default host into EditorConfig.
//
// Regression coverage for "a newly created image host is lost on restart": the
// load half (main.cpp -> ImageHostService::loadFromConfig) was wired, but
// nothing ever wrote the service state back into EditorConfig.
//
// GUILESS: the controller is a plain QObject and never touches a widget.

#include <QtTest>

#include <QJsonObject>

#include <controllers/imagehostcontroller.h>
#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/imagehostservice.h>
#include <imagehost/iimagehostprovider.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestImageHostControllerPersist : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void test_providersAndDefaultArePersisted();
  void test_noDefaultProviderPersistsEmptyDefaultHost();
  void test_removedProviderDisappearsFromConfig();

private:
  IImageHostProvider *addHost(const QString &p_typeId, const QString &p_name, const QString &p_key,
                              const QString &p_value);

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;

  ServiceLocator *m_services = nullptr;
  ImageHostService *m_imageHost = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  ImageHostController *m_controller = nullptr;
};

void TestImageHostControllerPersist::initTestCase() {
  // CRITICAL: before any vxcore context is created, so config writes land in an
  // isolated temp app-data folder instead of the real one.
  vxcore_set_test_mode(1);

  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_configService = new ConfigCoreService(m_context);
}

void TestImageHostControllerPersist::cleanupTestCase() {
  delete m_configService;
  m_configService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestImageHostControllerPersist::init() {
  m_services = new ServiceLocator();

  m_imageHost = new ImageHostService(nullptr);
  m_services->registerService<ImageHostService>(m_imageHost);

  m_configMgr = new ConfigMgr2(m_configService);
  m_services->registerService<ConfigMgr2>(m_configMgr);

  m_controller = new ImageHostController(*m_services);

  // Start from a clean slate: another test case (or a previously persisted
  // temp-mode config) must not leak into the assertions below.
  auto &editorConfig = m_configMgr->getEditorConfig();
  editorConfig.setImageHosts({});
  editorConfig.setDefaultImageHost(QString());
}

void TestImageHostControllerPersist::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_imageHost;
  m_imageHost = nullptr;
  delete m_services;
  m_services = nullptr;
}

IImageHostProvider *TestImageHostControllerPersist::addHost(const QString &p_typeId,
                                                            const QString &p_name,
                                                            const QString &p_key,
                                                            const QString &p_value) {
  auto *provider = m_controller->addProvider(p_typeId, p_name);
  if (!provider) {
    return nullptr;
  }
  QJsonObject config = provider->getConfig();
  config[p_key] = p_value;
  provider->setConfig(config);
  return provider;
}

void TestImageHostControllerPersist::test_providersAndDefaultArePersisted() {
  QVERIFY(addHost(QStringLiteral("github"), QStringLiteral("myGitHub"),
                  QStringLiteral("personalAccessToken"), QStringLiteral("abc123")) != nullptr);
  QVERIFY(addHost(QStringLiteral("custom_command"), QStringLiteral("myCmd"),
                  QStringLiteral("command"), QStringLiteral("echo test")) != nullptr);

  m_controller->setDefaultProvider(QStringLiteral("myGitHub"));

  m_controller->persistToConfig();

  const auto &hosts = m_configMgr->getEditorConfig().getImageHosts();
  QCOMPARE(hosts.size(), 2);

  // Registration order is preserved end to end.
  QCOMPARE(hosts[0].m_name, QStringLiteral("myGitHub"));
  QCOMPARE(hosts[0].m_type, QStringLiteral("github"));
  QCOMPARE(hosts[0].m_config.value(QStringLiteral("personalAccessToken")).toString(),
           QStringLiteral("abc123"));

  QCOMPARE(hosts[1].m_name, QStringLiteral("myCmd"));
  QCOMPARE(hosts[1].m_type, QStringLiteral("custom_command"));
  QCOMPARE(hosts[1].m_config.value(QStringLiteral("command")).toString(),
           QStringLiteral("echo test"));

  QCOMPARE(m_configMgr->getEditorConfig().getDefaultImageHost(), QStringLiteral("myGitHub"));
}

void TestImageHostControllerPersist::test_noDefaultProviderPersistsEmptyDefaultHost() {
  QVERIFY(addHost(QStringLiteral("github"), QStringLiteral("myGitHub"),
                  QStringLiteral("personalAccessToken"), QStringLiteral("abc123")) != nullptr);

  // "Local" in the settings combo carries an empty currentData(), which
  // resolves to no default provider.
  m_controller->setDefaultProvider(QString());
  QVERIFY(m_controller->getDefaultProvider() == nullptr);

  m_controller->persistToConfig();

  QCOMPARE(m_configMgr->getEditorConfig().getImageHosts().size(), 1);
  QVERIFY(m_configMgr->getEditorConfig().getDefaultImageHost().isEmpty());
}

void TestImageHostControllerPersist::test_removedProviderDisappearsFromConfig() {
  QVERIFY(addHost(QStringLiteral("github"), QStringLiteral("keepMe"),
                  QStringLiteral("personalAccessToken"), QStringLiteral("t1")) != nullptr);
  QVERIFY(addHost(QStringLiteral("gitee"), QStringLiteral("dropMe"),
                  QStringLiteral("personalAccessToken"), QStringLiteral("t2")) != nullptr);

  m_controller->persistToConfig();
  QCOMPARE(m_configMgr->getEditorConfig().getImageHosts().size(), 2);

  m_controller->removeProvider(QStringLiteral("dropMe"));
  m_controller->persistToConfig();

  const auto &hosts = m_configMgr->getEditorConfig().getImageHosts();
  QCOMPARE(hosts.size(), 1);
  QCOMPARE(hosts[0].m_name, QStringLiteral("keepMe"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestImageHostControllerPersist)
#include "test_imagehostcontroller_persist.moc"
