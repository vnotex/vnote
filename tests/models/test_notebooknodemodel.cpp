#include <QtTest>

#include <QTemporaryDir>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <models/notebooknodemodel.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookNodeModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Regression test: empty-state contract
  void testEmptyStateAfterSetNotebookId();

private:
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  ServiceLocator m_services;
};

void TestNotebookNodeModel::initTestCase() {
  // CRITICAL: Enable test mode BEFORE creating vxcore context
  // to prevent tests from corrupting real user data
  vxcore_set_test_mode(1);

  // Initialize VxCore context
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookCoreService and register it
  m_service = new NotebookCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
  m_services.registerService<NotebookCoreService>(m_service);
}

void TestNotebookNodeModel::cleanupTestCase() {
  // Destroy service before context (service depends on context)
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookNodeModel::testEmptyStateAfterSetNotebookId() {
  // Create a temporary directory for the test notebook
  QTemporaryDir tmpDir;
  QVERIFY(tmpDir.isValid());

  // Create a test notebook with a child file to ensure populated state
  QString configJson = R"({
    "name": "Test",
    "description": "Test notebook",
    "version": "1"
  })";
  QString nbId =
      m_service->createNotebook(tmpDir.filePath("nb"), configJson, NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Add a child file so the populated assertion is meaningful
  QString fileId = m_service->createFile(nbId, QString(), QStringLiteral("note.md"));
  QVERIFY(!fileId.isEmpty());

  // Create the model and set it to the populated notebook
  NotebookNodeModel model(m_services);
  model.setNotebookId(nbId);

  // Fetch children to populate the model
  QModelIndex root; // invalid == notebook root
  if (model.canFetchMore(root)) {
    model.fetchMore(root);
  }

  // PRE-RESET SANITY: Verify the model is populated before the reset
  int rowCountBefore = model.rowCount(root);
  QVERIFY2(rowCountBefore > 0, "Fixture must be non-empty for a meaningful regression test. "
                               "Model should have at least one child (the created file).");

  // THE TEST: Set notebook ID to empty string
  model.setNotebookId(QString());

  // ASSERTION 1: rowCount must be 0 after reset
  int rowCountAfter = model.rowCount(QModelIndex());
  QCOMPARE(rowCountAfter, 0);

  // ASSERTION 2: getNotebookId must return empty string
  QVERIFY(model.getNotebookId().isEmpty());

  // Cleanup: close the notebook
  m_service->closeNotebook(nbId);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeModel)
#include "test_notebooknodemodel.moc"
