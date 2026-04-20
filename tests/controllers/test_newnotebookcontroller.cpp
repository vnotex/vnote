#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include <vxcore/vxcore.h>

#include <controllers/newnotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

using namespace vnotex;

namespace tests {

class TestNewNotebookController : public QObject
{
  Q_OBJECT

private slots:
  // Lifecycle.
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Existing buildConfigJson tests.
  void testBuildConfigJsonDefaultAssetsFolder();
  void testBuildConfigJsonCustomAssetsFolder();
  void testBuildConfigJsonEmptyAssetsFolder();
  void testBuildConfigJsonWhitespaceAssetsFolder();
  void testBuildConfigJsonAbsolutePathAssetsFolder();
  void testBuildConfigJsonRelativePathAssetsFolder();

  // Validation tests.
  void testValidateRootFolderRawEmptyDir();
  void testValidateRootFolderRawNonEmptyDir();
  void testValidateRootFolderBundledEmptyDir();
  void testValidateRootFolderBundledNonEmptyDir();
  void testValidateRootFolderDefaultParamBundled();

  // Creation tests.
  void testCreateRawNotebook();

private:
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
};

void TestNewNotebookController::initTestCase()
{
  QVERIFY(m_tempDir.isValid());

  // Enable test mode to use isolated temp directories instead of real AppData.
  vxcore_set_test_mode(1);

  // Initialize VxCore context.
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookCoreService with context.
  m_service = new NotebookCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
  m_services.registerService<NotebookCoreService>(m_service);
}

void TestNewNotebookController::cleanupTestCase()
{
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNewNotebookController::cleanup()
{
  // Close all notebooks after each test.
  QJsonArray notebooks = m_service->listNotebooks();
  for (const auto &notebookVal : notebooks) {
    QJsonObject notebook = notebookVal.toObject();
    QString id = notebook["id"].toString();
    if (!id.isEmpty()) {
      m_service->closeNotebook(id);
    }
  }
}

// --- Existing buildConfigJson tests ---

void TestNewNotebookController::testBuildConfigJsonDefaultAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  // assetsFolder defaults to "vx_assets"

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(obj.contains("name"));
  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonCustomAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("_assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("_assets"));
}

void TestNewNotebookController::testBuildConfigJsonEmptyAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonWhitespaceAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("   ");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonAbsolutePathAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("/data/assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("/data/assets"));
}

void TestNewNotebookController::testBuildConfigJsonRelativePathAssetsFolder()
{
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("../shared");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("../shared"));
}

// --- Validation tests ---

void TestNewNotebookController::testValidateRootFolderRawEmptyDir()
{
  // Empty dir + Raw type -> should be valid.
  QString emptyDir = m_tempDir.createDir("raw_empty");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(emptyDir, NotebookType::Raw);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderRawNonEmptyDir()
{
  // Non-empty dir + Raw type -> should be valid (raw allows existing content).
  QString dir = m_tempDir.createDir("raw_nonempty");
  m_tempDir.createFile("raw_nonempty/file.md", "# Hello");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir, NotebookType::Raw);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderBundledEmptyDir()
{
  // Empty dir + Bundled type -> should be valid.
  QString emptyDir = m_tempDir.createDir("bundled_empty");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(emptyDir, NotebookType::Bundled);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderBundledNonEmptyDir()
{
  // Non-empty dir + Bundled type -> should be INVALID.
  QString dir = m_tempDir.createDir("bundled_nonempty");
  m_tempDir.createFile("bundled_nonempty/file.md", "# Hello");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir, NotebookType::Bundled);
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains("empty"));
}

void TestNewNotebookController::testValidateRootFolderDefaultParamBundled()
{
  // Calling without type parameter defaults to Bundled -> should reject non-empty.
  QString dir = m_tempDir.createDir("default_param");
  m_tempDir.createFile("default_param/file.md", "test");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir);
  QVERIFY(!result.valid);
}

// --- Creation tests ---

void TestNewNotebookController::testCreateRawNotebook()
{
  // Create a non-empty directory and create a raw notebook in it.
  QString dir = m_tempDir.createDir("create_raw");
  m_tempDir.createFile("create_raw/notes.md", "# Notes");

  NewNotebookInput input;
  input.name = QStringLiteral("RawTest");
  input.rootFolderPath = dir;
  input.type = NotebookType::Raw;

  NewNotebookController controller(m_services);
  NewNotebookResult result = controller.createNotebook(input);

  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QVERIFY(!result.notebookId.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNewNotebookController)
#include "test_newnotebookcontroller.moc"
