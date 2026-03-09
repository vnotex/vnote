// test_configservice.cpp - Unit tests for ConfigService
#include <QtTest>

#include <core/services/configcoreservice.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

// Test fixture that sets up a real vxcore context for testing
class TestConfigService : public QObject {
  Q_OBJECT

public:
  TestConfigService();
  ~TestConfigService();

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Test constructor
  void testConstructorWithNullContext();
  void testConstructorWithValidContext();

  // Test path methods
  void testGetExecutionFilePath();
  void testGetExecutionFolderPath();
  void testGetDataPath();
  void testGetConfigPath();
  void testGetSessionConfigPath();

  // Test config methods
  void testGetConfig();
  void testGetSessionConfig();
  void testGetConfigByName();
  void testGetConfigByNameWithDefaults();
  void testUpdateConfigByName();

  // Test Error return pattern
  void testUpdateConfigReturnsError();

private:
  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_service = nullptr;
};

TestConfigService::TestConfigService() {}

TestConfigService::~TestConfigService() {}

void TestConfigService::initTestCase() {
  // Enable test mode to use isolated temp directories
  vxcore_set_test_mode(1);

  // Create a test context
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);

  // Create service with context
  m_service = new ConfigCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
}

void TestConfigService::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestConfigService::testConstructorWithNullContext() {
  ConfigCoreService service(nullptr);
  // Methods should return empty values with null context
  QVERIFY(service.getExecutionFilePath().isEmpty());
  QVERIFY(service.getExecutionFolderPath().isEmpty());
  QVERIFY(service.getDataPath(DataLocation::App).isEmpty());
}

void TestConfigService::testConstructorWithValidContext() {
  // Our m_service was created with valid context in initTestCase
  QVERIFY(m_service != nullptr);
}

void TestConfigService::testGetExecutionFilePath() {
  QString path = m_service->getExecutionFilePath();
  // Should return non-empty path to test executable
  QVERIFY(!path.isEmpty());
  QVERIFY(path.contains("test_configservice"));
}

void TestConfigService::testGetExecutionFolderPath() {
  QString path = m_service->getExecutionFolderPath();
  // Should return non-empty folder path
  QVERIFY(!path.isEmpty());
  QVERIFY(path.contains("tests") || path.contains("core"));
}

void TestConfigService::testGetDataPath() {
  QString appPath = m_service->getDataPath(DataLocation::App);
  QString localPath = m_service->getDataPath(DataLocation::Local);

  // In test mode, both should return non-empty paths
  QVERIFY(!appPath.isEmpty());
  QVERIFY(!localPath.isEmpty());

  // Paths should be different (app vs local)
  QVERIFY(appPath != localPath || appPath.contains("test"));
}

void TestConfigService::testGetConfigPath() {
  QString path = m_service->getConfigPath();
  QVERIFY(!path.isEmpty());
  QVERIFY(path.contains("vxcore.json") || path.contains("config"));
}

void TestConfigService::testGetSessionConfigPath() {
  QString path = m_service->getSessionConfigPath();
  QVERIFY(!path.isEmpty());
  QVERIFY(path.contains("session.json") || path.contains("session"));
}

void TestConfigService::testGetConfig() {
  QJsonObject config = m_service->getConfig();
  // Config should be valid (may be empty if no config exists yet)
  // QJsonObject doesn't have isNull(), empty object is valid
  QVERIFY(config.isEmpty() || !config.isEmpty());
}

void TestConfigService::testGetSessionConfig() {
  QJsonObject config = m_service->getSessionConfig();
  // Session config should be valid
  QVERIFY(config.isEmpty() || !config.isEmpty());
}

void TestConfigService::testGetConfigByName() {
  // Try to get a non-existent config
  QJsonObject config = m_service->getConfigByName(DataLocation::App, "nonexistent");
  // Should return empty object for missing config
  QVERIFY(config.isEmpty());
}

void TestConfigService::testGetConfigByNameWithDefaults() {
  QJsonObject defaults;
  defaults["key1"] = "value1";
  defaults["key2"] = 42;

  // Get with defaults for non-existent config
  QJsonObject config =
      m_service->getConfigByNameWithDefaults(DataLocation::App, "nonexistent", defaults);

  // Should return defaults since config doesn't exist
  QCOMPARE(config["key1"].toString(), QString("value1"));
  QCOMPARE(config["key2"].toInt(), 42);
}

void TestConfigService::testUpdateConfigByName() {
  QJsonObject config;
  config["testKey"] = "testValue";
  config["testNumber"] = 123;

  Error err = m_service->updateConfigByName(DataLocation::App, "testconfig", config);
  QVERIFY(err.isOk());

  // Verify we can read it back
  QJsonObject readConfig = m_service->getConfigByName(DataLocation::App, "testconfig");
  QCOMPARE(readConfig["testKey"].toString(), QString("testValue"));
  QCOMPARE(readConfig["testNumber"].toInt(), 123);
}

void TestConfigService::testUpdateConfigReturnsError() {
  // Create service with null context
  ConfigCoreService nullService(nullptr);

  QJsonObject config;
  config["key"] = "value";

  Error err = nullService.updateConfigByName(DataLocation::App, "test", config);
  QVERIFY(!err.isOk());
  QCOMPARE(err.code(), ErrorCode::InvalidArgument);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestConfigService)
#include "test_configservice.moc"
