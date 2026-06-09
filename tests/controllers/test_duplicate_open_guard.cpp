// Tests for duplicate-open guard across controllers.
//
// The bug (Item 1 of openurl-followups.md): four controller methods were
// reading the wrong JSON key ("root_path" instead of "rootFolder"), causing
// the duplicate-open check to silently fail. This test verifies the fix by:
//
// 1. Creating a notebook via vxcore with a known root folder.
// 2. Attempting to open/create another notebook at the same root.
// 3. Asserting the validation error is raised with the expected message.
//
// Each controller has a validateRootFolder / validateRootPath method that
// checks NotebookCoreService::listNotebooks() and compares via QDir(path)
// equality. The test exercises OpenNotebookController (the primary case with
// two code paths: local-folder open and clone-and-open) and optionally
// NewNotebookController and OpenVNote3NotebookController if time permits.

#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <vxcore/vxcore.h>

#include <controllers/newnotebookcontroller.h>
#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

using namespace vnotex;

namespace tests {

class TestDuplicateOpenGuard : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // OpenNotebookController tests (mandatory).
  void testOpenNotebookRejectsLocalFolderDuplicate();
  void testOpenNotebookRejectsDuplicateOnCloneAndOpen();

  // NewNotebookController test (optional but good coverage).
  void testNewNotebookRejectsDuplicate();

private:
  VxCoreContextHandle m_ctx = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
};

void TestDuplicateOpenGuard::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Enable test mode BEFORE vxcore_context_create.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  auto *nbSvc = new NotebookCoreService(m_ctx, this);
  QVERIFY(nbSvc != nullptr);
  m_services.registerService<NotebookCoreService>(nbSvc);
}

void TestDuplicateOpenGuard::cleanupTestCase() {
  auto *nbSvc = m_services.get<NotebookCoreService>();
  if (nbSvc) {
    const QJsonArray nbs = nbSvc->listNotebooks();
    for (const auto &val : nbs) {
      const QString id = val.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        nbSvc->closeNotebook(id);
      }
    }
  }
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestDuplicateOpenGuard::testOpenNotebookRejectsLocalFolderDuplicate() {
  // Create the first notebook at rootFolder1 and KEEP IT OPEN.
  auto *nbSvc = m_services.get<NotebookCoreService>();
  QVERIFY(nbSvc != nullptr);

  const QString rootFolder1 = m_tempDir.filePath(QStringLiteral("notebook1"));
  QVERIFY(QDir().mkpath(rootFolder1));

  // Build config JSON for the notebook.
  const QString configJson1 =
      QStringLiteral("{\"id\":\"notebook1-id\",\"name\":\"Test Notebook 1\","
                     "\"description\":\"First test notebook\",\"version\":\"1\","
                     "\"imageFolder\":\"_v_images\",\"attachmentFolder\":\"_v_attachments\","
                     "\"recycleBinFolder\":\"_v_recycle_bin\",\"createdUtc\":1700000000000}");

  QString nbId1 = nbSvc->createNotebook(rootFolder1, configJson1, NotebookType::Bundled);
  QVERIFY(!nbId1.isEmpty());
  // IMPORTANT: Keep the notebook OPEN during the test.

  // Verify the notebook is now in the list.
  QJsonArray notebooks = nbSvc->listNotebooks();
  QVERIFY(notebooks.size() >= 1);
  bool found = false;
  QString retrievedRootFolder;
  for (const auto &nb : notebooks) {
    if (nb.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("notebook1-id")) {
      found = true;
      // Verify the rootFolder key is present and matches.
      retrievedRootFolder = nb.toObject().value(QStringLiteral("rootFolder")).toString();
      QCOMPARE(retrievedRootFolder, rootFolder1);
      break;
    }
  }
  QVERIFY2(found, "Notebook1 not found in list after creation");

  // Now try to open a notebook at the same rootFolder via OpenNotebookController.
  OpenNotebookController controller(m_services);

  // Call validateRootFolder with the same path.
  OpenNotebookValidationResult result = controller.validateRootFolder(rootFolder1);

  // Expect validation to FAIL with the duplicate-open message.
  QVERIFY2(
      !result.valid,
      qPrintable(
          QStringLiteral("Validation should have rejected duplicate: %1").arg(result.message)));
  QVERIFY2(
      result.message.contains(QStringLiteral("already open"), Qt::CaseInsensitive),
      qPrintable(
          QStringLiteral("Error message should mention 'already open': %1").arg(result.message)));
}

void TestDuplicateOpenGuard::testOpenNotebookRejectsDuplicateOnCloneAndOpen() {
  // Create a notebook at a specific location and KEEP IT OPEN.
  auto *nbSvc = m_services.get<NotebookCoreService>();
  QVERIFY(nbSvc != nullptr);

  const QString rootFolder2 = m_tempDir.filePath(QStringLiteral("notebook2"));
  QVERIFY(QDir().mkpath(rootFolder2));

  const QString configJson2 =
      QStringLiteral("{\"id\":\"notebook2-id\",\"name\":\"Test Notebook 2\","
                     "\"description\":\"Second test notebook\",\"version\":\"1\","
                     "\"imageFolder\":\"_v_images\",\"attachmentFolder\":\"_v_attachments\","
                     "\"recycleBinFolder\":\"_v_recycle_bin\",\"createdUtc\":1700000000000}");

  QString nbId2 = nbSvc->createNotebook(rootFolder2, configJson2, NotebookType::Bundled);
  QVERIFY(!nbId2.isEmpty());
  // IMPORTANT: Keep the notebook OPEN during the test.

  // Verify notebook2 is in the list.
  QJsonArray notebooks = nbSvc->listNotebooks();
  QVERIFY(notebooks.size() >= 1);
  bool found = false;
  for (const auto &nb : notebooks) {
    if (nb.toObject().value(QStringLiteral("id")).toString() == QStringLiteral("notebook2-id")) {
      found = true;
      QCOMPARE(nb.toObject().value(QStringLiteral("rootFolder")).toString(), rootFolder2);
      break;
    }
  }
  QVERIFY2(found, "Notebook2 not found in list after creation");

  // Now try the clone-and-open validation path with the same final destination.
  OpenNotebookController controller(m_services);

  // The cloneAndOpen path calls validateCloneInput, which checks for duplicates.
  // We simulate the final directory being the same as notebook2's root.
  CloneAndOpenInput cloneInput;
  cloneInput.remoteUrl = QStringLiteral("file:///fake");
  cloneInput.finalDestDir = rootFolder2;

  CloneAndOpenValidationResult result = controller.validateCloneInput(cloneInput);

  QVERIFY2(!result.valid,
           qPrintable(QStringLiteral("Clone validation should have rejected duplicate")));
  QVERIFY2(result.message.contains(QStringLiteral("already open"), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("Clone error message should mention 'already open': %1")
                          .arg(result.message)));
}

void TestDuplicateOpenGuard::testNewNotebookRejectsDuplicate() {
  // Create a notebook and KEEP IT OPEN.
  auto *nbSvc = m_services.get<NotebookCoreService>();
  QVERIFY(nbSvc != nullptr);

  const QString rootFolder3 = m_tempDir.filePath(QStringLiteral("notebook3"));
  QVERIFY(QDir().mkpath(rootFolder3));

  const QString configJson3 =
      QStringLiteral("{\"id\":\"notebook3-id\",\"name\":\"Test Notebook 3\","
                     "\"description\":\"Third test notebook\",\"version\":\"1\","
                     "\"imageFolder\":\"_v_images\",\"attachmentFolder\":\"_v_attachments\","
                     "\"recycleBinFolder\":\"_v_recycle_bin\",\"createdUtc\":1700000000000}");

  QString nbId3 = nbSvc->createNotebook(rootFolder3, configJson3, NotebookType::Bundled);
  QVERIFY(!nbId3.isEmpty());
  // IMPORTANT: Keep the notebook OPEN during the test.

  // Try to create another notebook with the same root folder.
  NewNotebookController controller(m_services);

  // Call validateRootFolder with the same path.
  ValidationResult result = controller.validateRootFolder(rootFolder3, NotebookType::Bundled);

  QVERIFY2(
      !result.valid,
      qPrintable(
          QStringLiteral("Validation should have rejected duplicate: %1").arg(result.message)));
  QVERIFY2(
      result.message.contains(QStringLiteral("already exists"), Qt::CaseInsensitive),
      qPrintable(QStringLiteral("Error message should mention duplicate: %1").arg(result.message)));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestDuplicateOpenGuard)
#include "test_duplicate_open_guard.moc"
