#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <controllers/openvnote3notebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/vnote3migrationservice.h>

#include <vxcore/vxcore.h>

// Test-only failpoint declared in vnote3migrationservice.cpp
namespace vnotex {
extern void vnote3MigrationService_setFailAfterCreate(bool p_fail);
} // namespace vnotex

namespace tests {

class TestOpenVNote3NotebookController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testUnconfirmedConversionRejected();
  void testInvalidSourceRejected();
  void testSamePathRejected();
  void testSourceInsideDestinationRejected();
  void testDestinationInsideSourceRejected();
  void testNonEmptyDestinationRejected();
  void testAlreadyOpenedDestinationRejected();

  void testConvertAndOpenUnconfirmedRejected();
  void testConvertAndOpenInvalidSourceFails();
  void testConvertAndOpenInvalidDestinationFails();
  void testConvertAndOpenForcedConversionFailure();
  void testConvertAndOpenHappyPath();

private:
  struct ControllerFixture {
    vnotex::ServiceLocator services;
    vnotex::NotebookCoreService *notebookService = nullptr;
    vnotex::TagCoreService *tagCoreService = nullptr;
    vnotex::VNote3MigrationService *migrationService = nullptr;
    vnotex::OpenVNote3NotebookController *controller = nullptr;

    explicit ControllerFixture(VxCoreContextHandle p_ctx) {
      notebookService = new vnotex::NotebookCoreService(p_ctx);
      tagCoreService = new vnotex::TagCoreService(p_ctx);
      migrationService = new vnotex::VNote3MigrationService(notebookService, tagCoreService);
      services.registerService<vnotex::NotebookCoreService>(notebookService);
      services.registerService<vnotex::VNote3MigrationService>(migrationService);
      controller = new vnotex::OpenVNote3NotebookController(services);
    }

    ~ControllerFixture() {
      delete controller;
      delete migrationService;
      delete tagCoreService;
      delete notebookService;
    }
  };

  static void writeVNote3Config(const QString &p_basePath, const QString &p_name) {
    QDir(p_basePath).mkpath(QStringLiteral("vx_notebook"));
    QJsonObject jobj;
    jobj[QStringLiteral("version")] = 3;
    jobj[QStringLiteral("config_mgr")] = QStringLiteral("vx.vnotex");
    jobj[QStringLiteral("name")] = p_name;
    jobj[QStringLiteral("description")] = QString();
    QFile f(p_basePath + QStringLiteral("/vx_notebook/vx_notebook.json"));
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(jobj).toJson(QJsonDocument::Compact));
  }

  VxCoreContextHandle m_ctx = nullptr;
};

void TestOpenVNote3NotebookController::initTestCase() {
  vxcore_set_test_mode(1);
  vxcore_set_app_info("VNote", "VNoteTestOpenVNote3NotebookController");

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_ctx != nullptr);
}

void TestOpenVNote3NotebookController::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestOpenVNote3NotebookController::testUnconfirmedConversionRejected() {
  ControllerFixture fixture(m_ctx);

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = QStringLiteral("/some/source");
  input.destinationRootFolderPath = QStringLiteral("/some/destination");
  input.confirmedConversion = false;

  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
}

void TestOpenVNote3NotebookController::testInvalidSourceRejected() {
  ControllerFixture fixture(m_ctx);

  // A path with no VNote3 config file should be invalid.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  vnotex::VNote3SourceInspectionResult result =
      fixture.controller->inspectSourceRootFolder(tempDir.path());
  QVERIFY(!result.valid);
  QVERIFY(!result.errorMessage.isEmpty());
}

void TestOpenVNote3NotebookController::testSamePathRejected() {
  ControllerFixture fixture(m_ctx);

  const QString path = QStringLiteral("/some/path");
  vnotex::OpenNotebookValidationResult result =
      fixture.controller->validateDestinationRootFolder(path, path);
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

void TestOpenVNote3NotebookController::testSourceInsideDestinationRejected() {
  ControllerFixture fixture(m_ctx);

  // source = /dest/subdir, destination = /dest  => source inside destination
  const QString src = QStringLiteral("/dest/subdir");
  const QString dst = QStringLiteral("/dest");
  vnotex::OpenNotebookValidationResult result =
      fixture.controller->validateDestinationRootFolder(src, dst);
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

void TestOpenVNote3NotebookController::testDestinationInsideSourceRejected() {
  ControllerFixture fixture(m_ctx);

  // source = /src, destination = /src/subdir => destination inside source
  const QString src = QStringLiteral("/src");
  const QString dst = QStringLiteral("/src/subdir");
  vnotex::OpenNotebookValidationResult result =
      fixture.controller->validateDestinationRootFolder(src, dst);
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

void TestOpenVNote3NotebookController::testNonEmptyDestinationRejected() {
  ControllerFixture fixture(m_ctx);

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  // Create a file inside the temp dir to make it non-empty.
  QFile f(tempDir.filePath(QStringLiteral("dummy.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x");
  f.close();

  const QString src = QStringLiteral("/some/other/source");
  vnotex::OpenNotebookValidationResult result =
      fixture.controller->validateDestinationRootFolder(src, tempDir.path());
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

void TestOpenVNote3NotebookController::testAlreadyOpenedDestinationRejected() {
  ControllerFixture fixture(m_ctx);

  // Create a real notebook at destDir so it gets opened in vxcore.
  QTemporaryDir destDir;
  QVERIFY(destDir.isValid());
  const QString destPath = destDir.path() + QStringLiteral("/opened_nb");
  const QString notebookId = fixture.notebookService->createNotebook(destPath, QStringLiteral("{}"),
                                                                     vnotex::NotebookType::Bundled);
  QVERIFY(!notebookId.isEmpty());

  // Now validate: destination is already opened — should be rejected.
  const QString src = QStringLiteral("/some/other/source");
  vnotex::OpenNotebookValidationResult result =
      fixture.controller->validateDestinationRootFolder(src, destPath);
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());

  // Cleanup: close the notebook.
  fixture.notebookService->closeNotebook(notebookId);
}

void TestOpenVNote3NotebookController::testConvertAndOpenUnconfirmedRejected() {
  ControllerFixture fixture(m_ctx);

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = QStringLiteral("/some/source");
  input.destinationRootFolderPath = QStringLiteral("/some/destination");
  input.confirmedConversion = false;

  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  QVERIFY(!result.success);
  QCOMPARE(result.errorMessage, QStringLiteral("Conversion not confirmed by user"));
}

void TestOpenVNote3NotebookController::testConvertAndOpenInvalidSourceFails() {
  ControllerFixture fixture(m_ctx);

  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  QTemporaryDir destDir;
  QVERIFY(destDir.isValid());
  // Remove dest so it's empty/non-existent for the destination check.
  destDir.remove();

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = tempDir.path(); // no vx_notebook/vx_notebook.json
  input.destinationRootFolderPath = destDir.path();
  input.confirmedConversion = true;

  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
}

void TestOpenVNote3NotebookController::testConvertAndOpenInvalidDestinationFails() {
  ControllerFixture fixture(m_ctx);

  QTemporaryDir srcDir;
  QVERIFY(srcDir.isValid());
  writeVNote3Config(srcDir.path(), QStringLiteral("TestNotebook"));

  // Create a non-empty destination directory.
  QTemporaryDir destDir;
  QVERIFY(destDir.isValid());
  QFile f(destDir.filePath(QStringLiteral("existing_file.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("not empty");
  f.close();

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = srcDir.path();
  input.destinationRootFolderPath = destDir.path();
  input.confirmedConversion = true;

  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
}

void TestOpenVNote3NotebookController::testConvertAndOpenForcedConversionFailure() {
  ControllerFixture fixture(m_ctx);

  QTemporaryDir srcDir;
  QVERIFY(srcDir.isValid());
  writeVNote3Config(srcDir.path(), QStringLiteral("TestNotebook"));

  QTemporaryDir destParent;
  QVERIFY(destParent.isValid());
  const QString destPath = destParent.path() + QStringLiteral("/forced_fail_dest");

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = srcDir.path();
  input.destinationRootFolderPath = destPath;
  input.confirmedConversion = true;

  vnotex::vnote3MigrationService_setFailAfterCreate(true);
  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  vnotex::vnote3MigrationService_setFailAfterCreate(false);

  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
}

void TestOpenVNote3NotebookController::testConvertAndOpenHappyPath() {
  ControllerFixture fixture(m_ctx);

  QTemporaryDir srcDir;
  QVERIFY(srcDir.isValid());
  writeVNote3Config(srcDir.path(), QStringLiteral("TestNotebook"));

  QTemporaryDir destParent;
  QVERIFY(destParent.isValid());
  const QString destPath = destParent.path() + QStringLiteral("/converted");

  vnotex::OpenVNote3NotebookInput input;
  input.sourceRootFolderPath = srcDir.path();
  input.destinationRootFolderPath = destPath;
  input.confirmedConversion = true;

  vnotex::OpenVNote3NotebookResult result = fixture.controller->convertAndOpen(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QVERIFY(!result.notebookId.isEmpty());
  QCOMPARE(result.notebookName, QStringLiteral("TestNotebook"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestOpenVNote3NotebookController)
#include "test_openvnote3notebookcontroller.moc"
