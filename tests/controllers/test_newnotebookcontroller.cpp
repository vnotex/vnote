#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>

#include <vxcore/vxcore.h>

#include <controllers/newnotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>

using namespace vnotex;

namespace tests {

namespace {
class BootstrapCredentialsStore : public SyncCredentialsStore {
public:
  using SyncCredentialsStore::SyncCredentialsStore;
  bool rejectStores = false;
  int deletions = 0;
  void storeCredentials(const QString &p_id, const SyncCredential &) override {
    QTimer::singleShot(0, this, [this, p_id]() {
      if (rejectStores)
        emit credentialsStoreError(p_id, QStringLiteral("secure-keychain-unavailable"));
      else
        emit credentialsStored(p_id);
    });
  }
  void deleteCredentials(const QString &p_id) override {
    ++deletions;
    QTimer::singleShot(0, this, [this, p_id]() { emit credentialsDeleted(p_id); });
  }
};
} // namespace

class TestNewNotebookController : public QObject {
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

  // T9: syncMethod field on NewNotebookInput injects flat vxcore notebook
  // config keys per ADR-8 (NOT a nested "sync" object). Confirms that the
  // empty-root validation rule remains in force for git-sync notebooks per
  // ADR-7 (bootstrap is create-then-enable; no allowNonEmptyRoot bypass).
  void syncMarkerInJsonForGit();
  void noSyncMarkerInJsonForNone();
  void emptyRootStillEnforcedForGit();

  // Validation tests.
  void testValidateRootFolderRawEmptyDir();
  void testValidateRootFolderRawNonEmptyDir();
  void testValidateRootFolderBundledEmptyDir();
  void testValidateRootFolderBundledNonEmptyDir();
  void testValidateRootFolderDefaultParamBundled();

  // Creation tests.
  void testCreateRawNotebook();
  void testPasswordCreationAndRawRejection_data();
  void testPasswordCreationAndRawRejection();
  void testBootstrapEnableFailureRemovesOnlyOwnedRoot_data();
  void testBootstrapEnableFailureRemovesOnlyOwnedRoot();
  void testBootstrapPersistenceBoundary_data();
  void testBootstrapPersistenceBoundary();

private:
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
};

void TestNewNotebookController::initTestCase() {
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

void TestNewNotebookController::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNewNotebookController::cleanup() {
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

void TestNewNotebookController::testBuildConfigJsonDefaultAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  // assetsFolder defaults to "vx_assets"

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(obj.contains("name"));
  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonCustomAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("_assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("_assets"));
}

void TestNewNotebookController::testBuildConfigJsonEmptyAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonWhitespaceAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("   ");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains("assetsFolder"));
}

void TestNewNotebookController::testBuildConfigJsonAbsolutePathAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("/data/assets");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("/data/assets"));
}

void TestNewNotebookController::testBuildConfigJsonRelativePathAssetsFolder() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.assetsFolder = QStringLiteral("../shared");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QCOMPARE(obj["assetsFolder"].toString(), QStringLiteral("../shared"));
}

// --- Validation tests ---

void TestNewNotebookController::testValidateRootFolderRawEmptyDir() {
  // Empty dir + Raw type -> should be valid.
  QString emptyDir = m_tempDir.createDir("raw_empty");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(emptyDir, NotebookType::Raw);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderRawNonEmptyDir() {
  // Non-empty dir + Raw type -> should be valid (raw allows existing content).
  QString dir = m_tempDir.createDir("raw_nonempty");
  m_tempDir.createFile("raw_nonempty/file.md", "# Hello");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir, NotebookType::Raw);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderBundledEmptyDir() {
  // Empty dir + Bundled type -> should be valid.
  QString emptyDir = m_tempDir.createDir("bundled_empty");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(emptyDir, NotebookType::Bundled);
  QVERIFY(result.valid);
}

void TestNewNotebookController::testValidateRootFolderBundledNonEmptyDir() {
  // Non-empty dir + Bundled type -> should be INVALID.
  QString dir = m_tempDir.createDir("bundled_nonempty");
  m_tempDir.createFile("bundled_nonempty/file.md", "# Hello");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir, NotebookType::Bundled);
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains("empty"));
}

void TestNewNotebookController::testValidateRootFolderDefaultParamBundled() {
  // Calling without type parameter defaults to Bundled -> should reject non-empty.
  QString dir = m_tempDir.createDir("default_param");
  m_tempDir.createFile("default_param/file.md", "test");
  NewNotebookController controller(m_services);
  ValidationResult result = controller.validateRootFolder(dir);
  QVERIFY(!result.valid);
}

// --- Creation tests ---

void TestNewNotebookController::testCreateRawNotebook() {
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

// --- T9: syncMethod handling ---

void TestNewNotebookController::syncMarkerInJsonForGit() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.type = NotebookType::Bundled;
  input.syncMethod = QStringLiteral("git");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(obj.contains(QStringLiteral("syncEnabled")));
  QCOMPARE(obj.value(QStringLiteral("syncEnabled")).toBool(), true);
  QCOMPARE(obj.value(QStringLiteral("syncBackend")).toString(), QStringLiteral("git"));
  // ADR-8: flat keys ONLY. No nested "sync" object.
  QVERIFY(!obj.contains(QStringLiteral("sync")));
  // syncRemoteUrl is set by T14 bootstrap, not by createNotebook.
  QVERIFY(!obj.contains(QStringLiteral("syncRemoteUrl")));
}

void TestNewNotebookController::noSyncMarkerInJsonForNone() {
  NewNotebookInput input;
  input.name = QStringLiteral("Test");
  input.type = NotebookType::Bundled;
  input.syncMethod = QStringLiteral("none");

  auto json = NewNotebookController::buildConfigJson(input);
  auto obj = QJsonDocument::fromJson(json.toUtf8()).object();

  QVERIFY(!obj.contains(QStringLiteral("syncEnabled")));
  QVERIFY(!obj.contains(QStringLiteral("syncBackend")));
  QVERIFY(!obj.contains(QStringLiteral("sync")));
}

void TestNewNotebookController::emptyRootStillEnforcedForGit() {
  // Per ADR-7: bootstrap is create-then-enable. The empty-root rule for
  // bundled notebooks MUST still apply when syncMethod == "git" — T9 must
  // NOT introduce any allowNonEmptyRoot bypass.
  QString nonEmptyDir = m_tempDir.createDir("git_nonempty");
  m_tempDir.createFile("git_nonempty/marker.txt", "not empty");

  NewNotebookInput input;
  input.name = QStringLiteral("GitTest");
  input.rootFolderPath = nonEmptyDir;
  input.type = NotebookType::Bundled;
  input.syncMethod = QStringLiteral("git");
  input.syncSettings = {QStringLiteral("git"),
                        QStringLiteral("https://example.test/notebook.git"),
                        {QStringLiteral("git"), {}, QStringLiteral("test-secret")}};

  NewNotebookController controller(m_services);
  ValidationResult resNonEmpty = controller.validateAll(input);
  QVERIFY2(!resNonEmpty.valid,
           "Empty-root rule must still apply when syncMethod == git (per ADR-7).");
  QVERIFY(resNonEmpty.message.contains(QStringLiteral("empty")));

  // Sanity: same input against an empty dir passes.
  QString emptyDir = m_tempDir.createDir("git_empty");
  input.rootFolderPath = emptyDir;
  ValidationResult resEmpty = controller.validateAll(input);
  QVERIFY2(resEmpty.valid, qPrintable(resEmpty.message));
}

void TestNewNotebookController::testPasswordCreationAndRawRejection_data() {
  QTest::addColumn<QString>("backend");
  QTest::newRow("webdav") << QStringLiteral("webdav");
  QTest::newRow("jianguoyun") << QStringLiteral("jianguoyun");
}

void TestNewNotebookController::testPasswordCreationAndRawRejection() {
  QFETCH(QString, backend);
  NewNotebookInput input;
  input.name = QStringLiteral("Password sync notebook");
  input.rootFolderPath = m_tempDir.filePath(backend + QStringLiteral("-created"));
  input.syncMethod = backend;
  input.syncSettings = {backend,
                        backend == QLatin1String("jianguoyun")
                            ? QStringLiteral("https://dav.jianguoyun.com/dav/notebook/")
                            : QStringLiteral("https://example.test/notes/"),
                        {backend, QStringLiteral("alice"), QStringLiteral(" password bytes ")}};
  NewNotebookController controller(m_services);
  const auto result = controller.createNotebook(input);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  const auto config = m_service->getNotebookConfig(result.notebookId);
  QCOMPARE(config.value(QStringLiteral("syncBackend")).toString(), backend);
  QVERIFY(config.value(QStringLiteral("syncEnabled")).toBool());
  const auto bytes = QJsonDocument(config).toJson(QJsonDocument::Compact);
  QVERIFY(!bytes.contains("alice"));
  QVERIFY(!bytes.contains("password bytes"));
  input.rootFolderPath = m_tempDir.filePath(backend + QStringLiteral("-raw-with-sync"));
  input.type = NotebookType::Raw;
  QVERIFY(!controller.createNotebook(input).success);
  QVERIFY(!QFileInfo::exists(input.rootFolderPath));
}

void TestNewNotebookController::testBootstrapEnableFailureRemovesOnlyOwnedRoot_data() {
  QTest::addColumn<QString>("backend");
  QTest::newRow("git") << QStringLiteral("git");
  QTest::newRow("webdav") << QStringLiteral("webdav");
  QTest::newRow("jianguoyun") << QStringLiteral("jianguoyun");
}

void TestNewNotebookController::testBootstrapEnableFailureRemovesOnlyOwnedRoot() {
  QFETCH(QString, backend);
  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_service);
  BootstrapCredentialsStore credentials(services);
  credentials.rejectStores = true;
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService sync(services);
  services.registerService<SyncService>(&sync);
  const auto stop = qScopeGuard([&]() { sync.shutdown(); });
  NewNotebookController controller(services);
  NewNotebookInput input;
  input.name = QStringLiteral("Failed setup");
  input.rootFolderPath = m_tempDir.filePath(backend + QStringLiteral("-failed-setup"));
  input.syncMethod = backend;
  input.syncSettings = {backend,
                        backend == QLatin1String("jianguoyun")
                            ? QStringLiteral("https://dav.jianguoyun.com/dav/unused/")
                            : QStringLiteral("https://127.0.0.1:1/notebook/"),
                        {backend,
                         isPasswordSyncBackend(backend) ? QStringLiteral("account") : QString(),
                         QStringLiteral("test-only-secret")}};
  const auto created = controller.createNotebook(input);
  QVERIFY2(created.success, qPrintable(created.errorMessage));
  const auto foreign = m_tempDir.createFile(backend + QStringLiteral("-foreign.txt"), "keep");
  QSignalSpy failed(&controller, &NewNotebookController::bootstrapFailed);
  QSignalSpy succeeded(&controller, &NewNotebookController::bootstrapSucceeded);
  controller.bootstrapSync(created.notebookId, input.syncSettings, nullptr);
  QTRY_COMPARE(failed.count(), 1);
  QCOMPARE(succeeded.count(), 0);
  QVERIFY(!QFileInfo::exists(input.rootFolderPath));
  QVERIFY(QFileInfo::exists(foreign));
  QVERIFY(credentials.deletions > 0);
  QVERIFY(m_service->getNotebookConfig(created.notebookId).isEmpty());
}

void TestNewNotebookController::testBootstrapPersistenceBoundary_data() {
  QTest::addColumn<bool>("persistFailure");
  QTest::newRow("persist-failure-rolls-back-owned-root") << true;
  QTest::newRow("first-sync-failure-retains-created-root") << false;
}

void TestNewNotebookController::testBootstrapPersistenceBoundary() {
  QFETCH(bool, persistFailure);
  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_service);
  BootstrapCredentialsStore credentials(services);
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService sync(services);
  services.registerService<SyncService>(&sync);
  const auto stop = qScopeGuard([&]() { sync.shutdown(); });
  NewNotebookController controller(services);
  NewNotebookInput input;
  input.name = QStringLiteral("Bootstrap boundary");
  input.rootFolderPath = m_tempDir.filePath(persistFailure ? QStringLiteral("persist-rollback")
                                                           : QStringLiteral("sync-retained"));
  input.syncMethod = QStringLiteral("git");
  const auto missingRemote =
      QUrl::fromLocalFile(m_tempDir.filePath(QStringLiteral("absent.git"))).toString();
  input.syncSettings = {QStringLiteral("git"),
                        missingRemote,
                        {QStringLiteral("git"), QString(), QStringLiteral("test-only-secret")}};
  const auto created = controller.createNotebook(input);
  QVERIFY2(created.success, qPrintable(created.errorMessage));
  // Existing local repository: Initialize is local, while the first fetch must
  // fail against an absent file:// remote. No network or timing injection.
  const auto gitDir = input.rootFolderPath + QStringLiteral("/vx_notebook/vx_sync");
  QCOMPARE(QProcess::execute(QStringLiteral("git"),
                             {QStringLiteral("init"), QStringLiteral("--initial-branch=main"),
                              QStringLiteral("--separate-git-dir"), gitDir, input.rootFolderPath}),
           0);
  const auto git = [&](const QStringList &args) {
    return QProcess::execute(QStringLiteral("git"),
                             QStringList{QStringLiteral("-C"), input.rootFolderPath} + args);
  };
  QCOMPARE(git({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"),
                missingRemote}),
           0);
  QCOMPARE(git({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")}), 0);
  QCOMPARE(git({QStringLiteral("config"), QStringLiteral("user.email"),
                QStringLiteral("test@example.test")}),
           0);
  QFile note(input.rootFolderPath + QStringLiteral("/keep.md"));
  QVERIFY(note.open(QIODevice::WriteOnly));
  QCOMPARE(note.write("local content"), qint64(13));
  note.close();
  QSignalSpy succeeded(&controller, &NewNotebookController::bootstrapSucceeded);
  QSignalSpy failed(&controller, &NewNotebookController::bootstrapFailed);
  QSignalSpy syncFailed(&sync, &SyncService::syncFailed);
  QSignalSpy syncStarted(&sync, &SyncService::syncStarted);
  bool routingPersistedAtCompletion = false;
  connect(&controller, &NewNotebookController::bootstrapSucceeded, this, [&](const QString &id) {
    const auto config = m_service->getNotebookConfig(id);
    routingPersistedAtCompletion =
        config.value(QStringLiteral("syncEnabled")).toBool() &&
        config.value(QStringLiteral("syncRemoteUrl")).toString() == missingRemote;
  });
  if (persistFailure)
    sync.testForceNextPersistFailure();
  controller.bootstrapSync(created.notebookId, input.syncSettings, nullptr);
  if (persistFailure) {
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(succeeded.count(), 0);
    QCOMPARE(syncStarted.count(), 0);
    QVERIFY(!QFileInfo::exists(input.rootFolderPath));
    QVERIFY(!sync.isSyncRegistered(created.notebookId));
  } else {
    QTRY_COMPARE(succeeded.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(syncFailed.count(), 1, 15000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(routingPersistedAtCompletion);
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(note.readAll(), QByteArray("local content"));
    QVERIFY(m_service->getNotebookConfig(created.notebookId)
                .value(QStringLiteral("syncEnabled"))
                .toBool());
    QVERIFY(sync.isSyncRegistered(created.notebookId));
    QCOMPARE(m_service->getLastSyncUtc(created.notebookId), qint64(0));
  }
}

} // namespace tests

QTEST_MAIN(tests::TestNewNotebookController)
#include "test_newnotebookcontroller.moc"
