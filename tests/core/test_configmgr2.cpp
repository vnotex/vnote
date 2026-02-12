#include <QtTest>

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/configmgr2.h>
#include <core/error.h>
#include <core/services/configservice.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestConfigMgr2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConstruction();
  void testInitialization();
  void testPathAccessors();
  void testConfigDataFolders();
  void testUpdateMainConfig();
  void testUpdateSessionConfig();
  void testDebouncing();

private:
  VxCoreContextHandle m_context = nullptr;
  ConfigService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
};

void TestConfigMgr2::initTestCase() {
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configService = new ConfigService(m_context);

  m_configMgr = new ConfigMgr2(m_configService);
}

void TestConfigMgr2::cleanupTestCase() {
  delete m_configMgr;
  delete m_configService;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestConfigMgr2::testConstruction() {
  QVERIFY(m_configMgr != nullptr);
}

void TestConfigMgr2::testInitialization() {
  m_configMgr->init();
  QVERIFY(!m_configMgr->getAppDataPath().isEmpty());
  QVERIFY(!m_configMgr->getUserConfigPath().isEmpty());
}

void TestConfigMgr2::testPathAccessors() {
  QString appData = m_configMgr->getAppDataPath();
  QString userData = m_configMgr->getUserConfigPath();
  QString logFile = m_configMgr->getLogFile();

  QVERIFY(!appData.isEmpty());
  QVERIFY(!userData.isEmpty());
  QVERIFY(!logFile.isEmpty());
  QVERIFY(logFile.endsWith("vnote.log"));

  qDebug() << "AppData:" << appData;
  qDebug() << "UserData:" << userData;
  qDebug() << "LogFile:" << logFile;
}

void TestConfigMgr2::testConfigDataFolders() {
  QString mainFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Main);
  QString themesFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Themes);
  QString webFolder = m_configMgr->getConfigDataFolder(ConfigMgr2::ConfigDataType::Web);

  QCOMPARE(mainFolder, m_configMgr->getAppDataPath());
  QVERIFY(themesFolder.contains("themes"));
  QVERIFY(webFolder.contains("web"));

  qDebug() << "Main:" << mainFolder;
  qDebug() << "Themes:" << themesFolder;
  qDebug() << "Web:" << webFolder;
}

void TestConfigMgr2::testUpdateMainConfig() {
  QJsonObject testConfig;
  testConfig["test_key"] = "test_value";
  testConfig["version"] = "1.0";

  m_configMgr->updateMainConfig(testConfig);

  // Wait for debounced write (500ms + buffer)
  QTest::qWait(700);

  // Verify config was written via ConfigService
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::App, "vnotex");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["test_key"].toString(), QString("test_value"));
}

void TestConfigMgr2::testUpdateSessionConfig() {
  QJsonObject testConfig;
  testConfig["session_key"] = "session_value";

  m_configMgr->updateSessionConfig(testConfig);

  // Wait for debounced write
  QTest::qWait(700);

  // Verify config was written
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::Local, "session");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["session_key"].toString(), QString("session_value"));
}

void TestConfigMgr2::testDebouncing() {
  // Send multiple rapid updates
  for (int i = 0; i < 5; ++i) {
    QJsonObject config;
    config["counter"] = i;
    m_configMgr->updateMainConfig(config);
    QTest::qWait(50); // Small delay between updates
  }

  // Wait for debounced write
  QTest::qWait(700);

  // Verify only last update was written (debouncing)
  QJsonObject loaded = m_configService->getConfigByName(DataLocation::App, "vnotex");
  QVERIFY(!loaded.isEmpty());
  QCOMPARE(loaded["counter"].toInt(), 4); // Last value
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestConfigMgr2)
#include "test_configmgr2.moc"
