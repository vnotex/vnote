#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

#include <controllers/firstruncontroller.h>
#include <controllers/newnotebookcontroller.h>
#include <core/configmgr2.h>
#include <core/error.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/sessionconfig.h>
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
  void test_targetNonEmpty_skip_data();
  void test_targetNonEmpty_skip();
  void test_targetEmptyDir_create();
  void test_targetIsFile_skip();
  void test_existingDestination_reopen();
  void test_quickAccessInitialization_data();
  void test_quickAccessInitialization();

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
  QVERIFY(m_service->isNotebookTrusted(nb.value(QLatin1String(vxcore::kJsonKeyId)).toString()));
  QCOMPARE(nb.value(QLatin1String(vxcore::kJsonKeyName)).toString(), QStringLiteral("My Notebook"));
  QCOMPARE(nb.value(QLatin1String(vxcore::kJsonKeyDescription)).toString(),
           QStringLiteral("My default notebook in VNote"));
  QVERIFY(nb.value(QLatin1String(vxcore::kJsonKeyRootFolder))
              .toString()
              .endsWith(QStringLiteral("vnote_notebook")));

  // The bundled notebook content was copied onto disk and is writable.
  const QString root = parentDir + QStringLiteral("/vnote_notebook");
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

// S4: non-notebook content is not overwritten, even with a usable bundled source.
void TestFirstRunController::test_targetNonEmpty_skip_data() {
  QTest::addColumn<bool>("corruptNotebook");
  QTest::newRow("ordinary-folder") << false;
  QTest::newRow("corrupt-notebook") << true;
}

void TestFirstRunController::test_targetNonEmpty_skip() {
  QFETCH(bool, corruptNotebook);
  const QString parent = QStringLiteral("s4-%1").arg(QString::fromLatin1(QTest::currentDataTag()));
  QString override = m_tempDir.createDir(parent);
  const QString root = m_tempDir.createDir(parent + QStringLiteral("/vnote_notebook"));
  QString sentinel =
      m_tempDir.createFile(parent + QStringLiteral("/vnote_notebook/sentinel.txt"), "keep");
  QVERIFY(QFileInfo::exists(sentinel));
  QString configPath;
  if (corruptNotebook) {
    m_tempDir.createDir(parent + QStringLiteral("/vnote_notebook/vx_notebook"));
    configPath = m_tempDir.createFile(
        parent + QStringLiteral("/vnote_notebook/vx_notebook/config.json"), "{broken notebook");
  }

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.createDefaultNotebook(), false);
  QFile sentinelFile(sentinel);
  QVERIFY(sentinelFile.open(QIODevice::ReadOnly));
  QCOMPARE(sentinelFile.readAll(), QByteArray("keep"));
  QVERIFY(!QFileInfo::exists(root + QStringLiteral("/Welcome.md")));
  if (corruptNotebook) {
    QFile configFile(configPath);
    QVERIFY(configFile.open(QIODevice::ReadOnly));
    QCOMPARE(configFile.readAll(), QByteArray("{broken notebook"));
  }
  QCOMPARE(m_service->listNotebooks().size(), 0);
  QCOMPARE(spy.count(), 0);
}

// S5: target dir exists but empty -> copy + open.
void TestFirstRunController::test_targetEmptyDir_create() {
  QString override = m_tempDir.createDir("s5");
  m_tempDir.createDir("s5/vnote_notebook");

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QVERIFY(controller.createDefaultNotebook());
  QCOMPARE(m_service->listNotebooks().size(), 1);
  QCOMPARE(spy.count(), 1);
  QVERIFY(m_service->isNotebookTrusted(spy.first().first().toString()));
}

// S6: target is a regular file -> skip; file preserved.
void TestFirstRunController::test_targetIsFile_skip() {
  QString override = m_tempDir.createDir("s6");
  QString file = m_tempDir.createFile("s6/vnote_notebook", "iamafile");
  QVERIFY(QFileInfo(file).isFile());

  FirstRunController controller(m_services);
  controller.setParentDirOverrideForTesting(override);
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QCOMPARE(controller.createDefaultNotebook(), false);
  QVERIFY(QFileInfo(file).isFile());
  QCOMPARE(m_service->listNotebooks().size(), 0);
  QCOMPARE(spy.count(), 0);
}

// S7: a notebook left on disk is reopened without needing bundled resources.
void TestFirstRunController::test_existingDestination_reopen() {
  FirstRunController controller(m_services);
  const QString parent = m_tempDir.createDir("s7");
  controller.setParentDirOverrideForTesting(parent);
  controller.setSourceDirOverrideForTesting(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
  QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);

  QVERIFY(controller.createDefaultNotebook());
  const QString notebookId = spy.first().first().toString();
  QVERIFY(m_service->isNotebookTrusted(notebookId));
  QVERIFY(m_service->closeNotebook(notebookId));
  QCOMPARE(m_service->listNotebooks().size(), 0);

  const QString welcomePath = parent + QStringLiteral("/vnote_notebook/Welcome.md");
  QFile welcome(welcomePath);
  QVERIFY(welcome.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(welcome.write("user edits"), qint64(10));
  welcome.close();
  const auto originalPermissions = welcome.permissions();
  const auto restorePermissions = qScopeGuard([&] { welcome.setPermissions(originalPermissions); });
  QVERIFY(welcome.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));
  const auto readOnlyPermissions = welcome.permissions();
  const QString sentinel = m_tempDir.createFile("s7/vnote_notebook/keep.txt", "keep");

  controller.setSourceDirOverrideForTesting(m_tempDir.filePath("missing-source"));
  QVERIFY(controller.shouldCreateDefaultNotebook(true));
  QVERIFY(controller.createDefaultNotebook());

  QCOMPARE(m_service->listNotebooks().size(), 1);
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.last().first().toString(), notebookId);
  QVERIFY(!m_service->isNotebookTrusted(notebookId));
  QVERIFY(!controller.shouldCreateDefaultNotebook(true));
  QCOMPARE(welcome.permissions(), readOnlyPermissions);
  QVERIFY(welcome.open(QIODevice::ReadOnly));
  QCOMPARE(welcome.readAll(), QByteArray("user edits"));
  QFile sentinelFile(sentinel);
  QVERIFY(sentinelFile.open(QIODevice::ReadOnly));
  QCOMPARE(sentinelFile.readAll(), QByteArray("keep"));
}

void TestFirstRunController::test_quickAccessInitialization_data() {
  QTest::addColumn<bool>("existingItem");
  QTest::addColumn<bool>("collision");
  QTest::addColumn<bool>("unreadableSession");
  QTest::addColumn<bool>("reuseDestination");
  QTest::newRow("first-run") << false << false << false << false;
  QTest::newRow("existing-quick-access") << true << false << false << false;
  QTest::newRow("creation-skipped") << false << true << false << false;
  QTest::newRow("unreadable-session") << false << false << true << false;
  QTest::newRow("reopen-empty-quick-access") << false << false << false << true;
  QTest::newRow("reopen-existing-quick-access") << true << false << false << true;
}

void TestFirstRunController::test_quickAccessInitialization() {
  QFETCH(bool, existingItem);
  QFETCH(bool, collision);
  QFETCH(bool, unreadableSession);
  QFETCH(bool, reuseDestination);

  ConfigCoreService configService(m_context);
  const auto savedMain = configService.getConfigByName(DataLocation::App, QStringLiteral("vnotex"));
  const QString sessionPath =
      QDir(configService.getDataPath(DataLocation::Local)).filePath(QStringLiteral("session.json"));
  QFile sessionFile(sessionPath);
  const bool sessionExisted = sessionFile.exists();
  QByteArray savedSession;
  if (sessionExisted) {
    QVERIFY(sessionFile.open(QIODevice::ReadOnly));
    savedSession = sessionFile.readAll();
    sessionFile.close();
  }
  const auto restore = qScopeGuard([&] {
    configService.updateConfigByName(DataLocation::App, QStringLiteral("vnotex"), savedMain);
    if (sessionExisted) {
      if (sessionFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sessionFile.write(savedSession);
        sessionFile.close();
      }
    } else {
      sessionFile.remove();
    }
  });
  QVERIFY(configService
              .updateConfigByName(
                  DataLocation::App, QStringLiteral("vnotex"),
                  {{QStringLiteral("metadata"),
                    QJsonObject{{QStringLiteral("version"), ConfigMgr2::getApplicationVersion()}}}})
              .isOk());

  QJsonArray expectedItems;
  if (existingItem) {
    SessionConfig::QuickAccessItem item;
    item.m_path = m_tempDir.createFile(QStringLiteral("existing-quick-access.md"), "keep");
    item.m_openMode = QuickAccessOpenMode::Read;
    item.m_uuid = QStringLiteral("aef273cb-65cb-458b-985a-5aa6c5415a59");
    expectedItems.append(item.toJson());
    QVERIFY(configService
                .updateConfigByName(DataLocation::Local, QStringLiteral("session"),
                                    {{QStringLiteral("core"),
                                      QJsonObject{{QStringLiteral("quickAccess"), expectedItems}}}})
                .isOk());
  } else if (unreadableSession) {
    QVERIFY(sessionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(sessionFile.write("{broken session"), qint64(15));
    sessionFile.close();
  } else if (sessionFile.exists()) {
    QVERIFY(sessionFile.remove());
  }

  {
    ConfigMgr2 configMgr(&configService);
    configMgr.init();
    ServiceLocator services;
    services.registerService<ConfigMgr2>(&configMgr);
    services.registerService<ConfigCoreService>(&configService);
    services.registerService<NotebookCoreService>(m_service);

    const QString relativeParent =
        QStringLiteral("quick-access-%1").arg(QString::fromLatin1(QTest::currentDataTag()));
    const QString parent = m_tempDir.createDir(relativeParent);
    if (collision) {
      m_tempDir.createDir(relativeParent + QStringLiteral("/vnote_notebook"));
      m_tempDir.createFile(relativeParent + QStringLiteral("/vnote_notebook/keep"), "keep");
    }
    if (reuseDestination) {
      m_tempDir.copyFrom(QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR),
                         relativeParent + QStringLiteral("/vnote_notebook"));
    }
    FirstRunController controller(services);
    controller.setParentDirOverrideForTesting(parent);
    controller.setSourceDirOverrideForTesting(reuseDestination
                                                  ? m_tempDir.filePath("missing-source")
                                                  : QStringLiteral(DEFAULT_NOTEBOOK_FIXTURE_DIR));
    QSignalSpy spy(&controller, &FirstRunController::defaultNotebookCreated);
    QCOMPARE(controller.createDefaultNotebook(), !collision);
    QCOMPARE(spy.count(), collision ? 0 : 1);
    if (!collision) {
      QCOMPARE(m_service->isNotebookTrusted(spy.first().first().toString()), !reuseDestination);
    }
    if (collision) {
      const QString root = ConfigMgr2::getDefaultNotebookPath(parent);
      QFile sentinel(root + QStringLiteral("/keep"));
      QVERIFY(sentinel.open(QIODevice::ReadOnly));
      QCOMPARE(sentinel.readAll(), QByteArray("keep"));
      QVERIFY(!QFileInfo::exists(root + QStringLiteral("/Welcome.md")));
    }

    if (!collision && !existingItem && !unreadableSession) {
      const QString notebookId = spy.first().first().toString();
      SessionConfig::QuickAccessItem item;
      item.m_path = m_service->buildAbsolutePath(notebookId, QStringLiteral("all-in-one.md"));
      item.m_uuid = m_service->getFileInfo(notebookId, QStringLiteral("all-in-one.md"))
                        .value(QLatin1String(vxcore::kJsonKeyId))
                        .toString();
      QVERIFY(QFileInfo(item.m_path).isFile());
      QVERIFY(!item.m_uuid.isEmpty());
      expectedItems.append(item.toJson());
    }
    QCOMPARE(configMgr.getSessionConfig()
                 .toJson()
                 .value(QStringLiteral("core"))
                 .toObject()
                 .value(QStringLiteral("quickAccess"))
                 .toArray(),
             expectedItems);
  }

  if (unreadableSession) {
    QVERIFY(sessionFile.open(QIODevice::ReadOnly));
    QCOMPARE(sessionFile.readAll(), QByteArray("{broken session"));
    sessionFile.close();
  } else {
    QCOMPARE(configService.getConfigByName(DataLocation::Local, QStringLiteral("session"))
                 .value(QStringLiteral("core"))
                 .toObject()
                 .value(QStringLiteral("quickAccess"))
                 .toArray(),
             expectedItems);
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFirstRunController)
#include "test_firstruncontroller.moc"
