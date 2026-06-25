#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

#include <controllers/firstruncontroller.h>
#include <controllers/newnotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

using namespace vnotex;

namespace tests {

class TestFirstRunController : public QObject {
  Q_OBJECT

private slots:
  // Lifecycle.
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Scenarios S1-S7.
  void test_versionUnchanged_noCreation();
  void test_create_targetAbsent();
  void test_existingNotebook_skip();
  void test_targetNonEmpty_skip();
  void test_targetEmptyDir_create();
  void test_targetIsFile_skip();
  void test_doubleTrigger_single();

private:
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
};

void TestFirstRunController::initTestCase() {
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

void TestFirstRunController::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestFirstRunController::cleanup() {
  // Close all notebooks after each test so each scenario starts clean.
  QJsonArray notebooks = m_service->listNotebooks();
  for (const auto &notebookVal : notebooks) {
    QJsonObject notebook = notebookVal.toObject();
    QString id = notebook["id"].toString();
    if (!id.isEmpty()) {
      m_service->closeNotebook(id);
    }
  }
}

// S1: version unchanged -> no creation.
void TestFirstRunController::test_versionUnchanged_noCreation() {
  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(m_tempDir.createDir("s1"));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.shouldCreateDefaultNotebook(false), false);
  QCOMPARE(m_service->listNotebooks().size(), 0);
  QCOMPARE(spy.count(), 0);
}

// S2: target absent -> copy bundled notebook + open, verify config fields,
// copied files on disk, and signal.
void TestFirstRunController::test_create_targetAbsent() {
  FirstRunController controller(m_services);
  const QString parentDir = m_tempDir.createDir("s2");
  controller.setParentDirOverrideForTesting(parentDir);
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.shouldCreateDefaultNotebook(true), true);
  QVERIFY(controller.createDefaultNotebook());

  QJsonArray notebooks = m_service->listNotebooks();
  QCOMPARE(notebooks.size(), 1);

  QJsonObject nb = notebooks.at(0).toObject();
  QCOMPARE(nb.value(QLatin1String(vxcore::kJsonKeyName)).toString(), QStringLiteral("My Notebook"));
  QCOMPARE(nb.value(QLatin1String(vxcore::kJsonKeyDescription)).toString(),
           QStringLiteral("My default notebook in VNote"));
  QVERIFY(nb.value(QLatin1String(vxcore::kJsonKeyRootFolder))
              .toString()
              .endsWith(QStringLiteral("my_notebook")));

  // The bundled notebook content was copied onto disk and is writable.
  const QString root = parentDir + QStringLiteral("/my_notebook");
  QVERIFY(QFileInfo::exists(root + QStringLiteral("/Welcome.md")));
  QVERIFY(QFileInfo::exists(root + QStringLiteral("/vx_notebook/config.json")));
  QFileInfo welcomeInfo(root + QStringLiteral("/Welcome.md"));
  QVERIFY(welcomeInfo.isWritable());

  QCOMPARE(spy.count(), 1);
  QVERIFY(!spy.at(0).at(0).toString().isEmpty());
}

// S3: a notebook already exists -> skip (no creation, no signal).
void TestFirstRunController::test_existingNotebook_skip() {
  // Pre-create one notebook in a SEPARATE temp dir.
  NewNotebookInput pre;
  pre.name = QStringLiteral("Existing");
  pre.rootFolderPath = m_tempDir.createDir("s3_existing");
  pre.type = NotebookType::Bundled;
  pre.syncMethod = QStringLiteral("none");
  NewNotebookController pc(m_services);
  NewNotebookResult preRes = pc.createNotebook(pre);
  QVERIFY2(preRes.success, qPrintable(preRes.errorMessage));
  QCOMPARE(m_service->listNotebooks().size(), 1);

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(m_tempDir.createDir("s3"));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.shouldCreateDefaultNotebook(true), false);
  QCOMPARE(m_service->listNotebooks().size(), 1);
  QCOMPARE(spy.count(), 0);
}

// S4: target dir non-empty -> skip; sentinel preserved.
void TestFirstRunController::test_targetNonEmpty_skip() {
  QString override = m_tempDir.createDir("s4");
  m_tempDir.createDir("s4/my_notebook");
  QString sentinel = m_tempDir.createFile("s4/my_notebook/sentinel.txt", "keep");
  QVERIFY(QFileInfo::exists(sentinel));

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.createDefaultNotebook(), false);
  QVERIFY(QFileInfo::exists(sentinel));
  QCOMPARE(m_service->listNotebooks().size(), 0);
  QCOMPARE(spy.count(), 0);
}

// S5: target dir exists but empty -> copy + open.
void TestFirstRunController::test_targetEmptyDir_create() {
  QString override = m_tempDir.createDir("s5");
  m_tempDir.createDir("s5/my_notebook");

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QVERIFY(controller.createDefaultNotebook());
  QCOMPARE(m_service->listNotebooks().size(), 1);
  QCOMPARE(spy.count(), 1);
}

// S6: target is a regular file -> skip; file preserved.
void TestFirstRunController::test_targetIsFile_skip() {
  QString override = m_tempDir.createDir("s6");
  QString file = m_tempDir.createFile("s6/my_notebook", "iamafile");
  QVERIFY(QFileInfo(file).isFile());

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.createDefaultNotebook(), false);
  QVERIFY(QFileInfo(file).isFile());
  QCOMPARE(m_service->listNotebooks().size(), 0);
  QCOMPARE(spy.count(), 0);
}

// S7: calling createDefaultNotebook twice creates exactly one notebook.
// The first creation populates vx_notebook/ in the root, so the second call is
// blocked by the non-empty-dir collision guard.
void TestFirstRunController::test_doubleTrigger_single() {
  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(m_tempDir.createDir("s7"));
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QVERIFY(controller.createDefaultNotebook());
  controller.createDefaultNotebook();

  QCOMPARE(m_service->listNotebooks().size(), 1);
  QCOMPARE(spy.count(), 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFirstRunController)
#include "test_firstruncontroller.moc"
