#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <controllers/recyclebincontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <temp_dir_fixture.h>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

namespace {

bool WriteFileAt(const QString &p_path, qint64 p_modifiedUtcMs) {
  if (!QDir().mkpath(QFileInfo(p_path).absolutePath())) {
    return false;
  }
  QFile file(p_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  if (file.write("content") < 0) {
    return false;
  }
  file.close();
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  return file.setFileTime(QDateTime::fromMSecsSinceEpoch(p_modifiedUtcMs, Qt::UTC),
                          QFileDevice::FileModificationTime);
}

} // namespace

class TestNotebookService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Notebook operations tests.
  void testCreateNotebook();
  void testOpenCloseNotebook();
  void testListNotebooks();
  void testGetNotebookConfig();
  void testUpdateNotebookConfig();

  // Folder operations tests.
  void testCreateFolder();
  void testCreateFolderPath();
  void testDeleteFolder();
  void testRenameFolderAndMove();
  void testCopyFolder();

  // File operations tests.
  void testCreateFile();
  void testDeleteFile();
  void testRenameFileAndMove();
  void testCopyFile();
  void testImportFile();
  void testGetFileInfo();

  // Hook tests (delete).
  void testDeleteFileCancelledByHook();
  void testDeleteFolderCancelledByHook();
  void testDeleteFileFiresBeforeHook();
  void testDeleteFolderFiresBeforeHook();

  // Hook tests (rename).
  void testRenameFileCancelledByHook();
  void testRenameFolderCancelledByHook();
  void testRenameFileFiresAfterHook();
  void testRenameFolderFiresAfterHook();

  // Hook tests (move).
  void testMoveFileCancelledByHook();
  void testMoveFolderCancelledByHook();

  void testRecycleBinCleanupServiceWrapper();
  void testAutomaticCleanupStartupOpenAndDuplicateSuppression();
  void testAutomaticCleanupGraceRepair();
  void testAutomaticCleanupSkipsReadOnlyAndRaw();
  void testAutomaticCleanupDestructorCancelsGateWait();

private:
  // Helper to create test notebook and return ID.
  QString createTestNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  HookManager *m_hookMgr = nullptr;
  TempDirFixture m_tempDir;
  ConfigCoreService *m_configService = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  ServiceLocator *m_services = nullptr;
};

void TestNotebookService::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Enable test mode to use isolated temp directories instead of real AppData.
  vxcore_set_test_mode(1);

  // Initialize VxCore context.
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookService with context.
  m_service = new NotebookCoreService(m_context, this);
  QVERIFY(m_service != nullptr);

  // Create HookManager and wire it to the service.
  m_hookMgr = new HookManager(this);
  m_service->setHookManager(m_hookMgr);

  m_configService = new ConfigCoreService(m_context);
  m_configMgr = new ConfigMgr2(m_configService);
  m_configMgr->init();
  m_ioGate = new NotebookIoGate();
  m_service->setNotebookIoGate(m_ioGate);
  m_services = new ServiceLocator();
  m_services->registerService<NotebookCoreService>(m_service);
  m_services->registerService<HookManager>(m_hookMgr);
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_services->registerService<NotebookIoGate>(m_ioGate);
}

void TestNotebookService::cleanupTestCase() {
  delete m_services;
  m_services = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configService;
  m_configService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookService::cleanup() {
  m_configMgr->getCoreConfig().setRecycleBinAutoCleanupEnabled(false, 0);
  m_configMgr->getCoreConfig().setRecycleBinRetentionDays(60);
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

QString TestNotebookService::createTestNotebook(const QString &p_path) {
  QString configJson = R"({
    "name": "Test Notebook",
    "description": "Test notebook for unit tests",
    "version": "1"
  })";
  return m_service->createNotebook(p_path, configJson, NotebookType::Bundled);
}

void TestNotebookService::testCreateNotebook() {
  QString nbPath = m_tempDir.filePath("test_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Verify notebook exists in list.
  QJsonArray notebooks = m_service->listNotebooks();
  QVERIFY(notebooks.size() > 0);

  bool found = false;
  for (const auto &notebookVal : notebooks) {
    QJsonObject notebook = notebookVal.toObject();
    if (notebook["id"].toString() == nbId) {
      found = true;
      break;
    }
  }
  QVERIFY(found);
}

void TestNotebookService::testOpenCloseNotebook() {
  QString nbPath = m_tempDir.filePath("test_open_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Close notebook.
  m_service->closeNotebook(nbId);

  // Re-open notebook.
  QString reopenedId = m_service->openNotebook(nbPath);
  QVERIFY(!reopenedId.isEmpty());
  QCOMPARE(reopenedId, nbId);
}

void TestNotebookService::testListNotebooks() {
  QString nbPath1 = m_tempDir.filePath("notebook1");
  QString nbPath2 = m_tempDir.filePath("notebook2");

  QString nbId1 = createTestNotebook(nbPath1);
  QString nbId2 = createTestNotebook(nbPath2);

  QVERIFY(!nbId1.isEmpty());
  QVERIFY(!nbId2.isEmpty());

  QJsonArray notebooks = m_service->listNotebooks();
  QVERIFY(notebooks.size() >= 2);
}

void TestNotebookService::testGetNotebookConfig() {
  QString nbPath = m_tempDir.filePath("test_config_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QJsonObject config = m_service->getNotebookConfig(nbId);
  QVERIFY(!config.isEmpty());
  QCOMPARE(config["name"].toString(), QString("Test Notebook"));
}

void TestNotebookService::testUpdateNotebookConfig() {
  QString nbPath = m_tempDir.filePath("test_update_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString newConfig = R"({
    "name": "Updated Notebook",
    "description": "Updated description",
    "version": "1"
  })";

  m_service->updateNotebookConfig(nbId, newConfig);

  QJsonObject config = m_service->getNotebookConfig(nbId);
  QCOMPARE(config["name"].toString(), QString("Updated Notebook"));
}

void TestNotebookService::testCreateFolder() {
  QString nbPath = m_tempDir.filePath("folder_test_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString folderId = m_service->createFolder(nbId, "", "TestFolder");
  QVERIFY(!folderId.isEmpty());

  // Verify folder config exists.
  QJsonObject folderConfig = m_service->getFolderConfig(nbId, "TestFolder");
  QVERIFY(!folderConfig.isEmpty());
}

void TestNotebookService::testCreateFolderPath() {
  QString nbPath = m_tempDir.filePath("folderpath_test_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString folderId = m_service->createFolderPath(nbId, "Parent/Child/GrandChild");
  QVERIFY(!folderId.isEmpty());

  // Verify nested folder exists.
  QJsonObject folderConfig = m_service->getFolderConfig(nbId, "Parent/Child/GrandChild");
  QVERIFY(!folderConfig.isEmpty());
}

void TestNotebookService::testDeleteFolder() {
  QString nbPath = m_tempDir.filePath("delete_folder_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString folderId = m_service->createFolder(nbId, "", "FolderToDelete");
  QVERIFY(!folderId.isEmpty());

  m_service->deleteFolder(nbId, "FolderToDelete");

  // Verify folder no longer exists.
  QJsonObject folderConfig = m_service->getFolderConfig(nbId, "FolderToDelete");
  QVERIFY(folderConfig.isEmpty());
}

void TestNotebookService::testRenameFolderAndMove() {
  QString nbPath = m_tempDir.filePath("rename_folder_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "OriginalFolder");
  m_service->renameFolder(nbId, "OriginalFolder", "RenamedFolder");

  QJsonObject folderConfig = m_service->getFolderConfig(nbId, "RenamedFolder");
  QVERIFY(!folderConfig.isEmpty());

  // Test move folder.
  m_service->createFolder(nbId, "", "TargetParent");
  m_service->moveFolder(nbId, "RenamedFolder", "TargetParent");

  QJsonObject movedConfig = m_service->getFolderConfig(nbId, "TargetParent/RenamedFolder");
  QVERIFY(!movedConfig.isEmpty());
}

void TestNotebookService::testCopyFolder() {
  QString nbPath = m_tempDir.filePath("copy_folder_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "SourceFolder");
  m_service->createFolder(nbId, "", "DestParent");

  QString copiedId = m_service->copyFolder(nbId, "SourceFolder", "DestParent", "CopiedFolder");
  QVERIFY(!copiedId.isEmpty());

  QJsonObject copiedConfig = m_service->getFolderConfig(nbId, "DestParent/CopiedFolder");
  QVERIFY(!copiedConfig.isEmpty());
}

void TestNotebookService::testCreateFile() {
  QString nbPath = m_tempDir.filePath("file_test_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString fileId = m_service->createFile(nbId, "", "test.md");
  QVERIFY(!fileId.isEmpty());

  // Verify file info exists.
  QJsonObject fileInfo = m_service->getFileInfo(nbId, "test.md");
  QVERIFY(!fileInfo.isEmpty());
}

void TestNotebookService::testDeleteFile() {
  QString nbPath = m_tempDir.filePath("delete_file_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString fileId = m_service->createFile(nbId, "", "delete_me.md");
  QVERIFY(!fileId.isEmpty());

  m_service->deleteFile(nbId, "delete_me.md");

  // Verify file no longer exists.
  QJsonObject fileInfo = m_service->getFileInfo(nbId, "delete_me.md");
  QVERIFY(fileInfo.isEmpty());
}

void TestNotebookService::testRenameFileAndMove() {
  QString nbPath = m_tempDir.filePath("rename_file_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "original.md");
  m_service->renameFile(nbId, "original.md", "renamed.md");

  QJsonObject fileInfo = m_service->getFileInfo(nbId, "renamed.md");
  QVERIFY(!fileInfo.isEmpty());

  // Test move file.
  m_service->createFolder(nbId, "", "TargetFolder");
  m_service->moveFile(nbId, "renamed.md", "TargetFolder");

  QJsonObject movedInfo = m_service->getFileInfo(nbId, "TargetFolder/renamed.md");
  QVERIFY(!movedInfo.isEmpty());
}

void TestNotebookService::testCopyFile() {
  QString nbPath = m_tempDir.filePath("copy_file_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "source.md");
  m_service->createFolder(nbId, "", "DestFolder");

  QString copiedId = m_service->copyFile(nbId, "source.md", "DestFolder", "copied.md");
  QVERIFY(!copiedId.isEmpty());

  QJsonObject copiedInfo = m_service->getFileInfo(nbId, "DestFolder/copied.md");
  QVERIFY(!copiedInfo.isEmpty());
}

void TestNotebookService::testImportFile() {
  QString nbPath = m_tempDir.filePath("import_file_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  // Create an external file to import.
  QString externalFilePath = m_tempDir.filePath("external_file.md");
  QFile externalFile(externalFilePath);
  QVERIFY(externalFile.open(QIODevice::WriteOnly));
  externalFile.write("# External File Content\n\nThis is test content.");
  externalFile.close();

  // Create a destination folder.
  m_service->createFolder(nbId, "", "ImportDest");

  // Import the external file.
  QString importedId = m_service->importFile(nbId, "ImportDest", externalFilePath);
  QVERIFY(!importedId.isEmpty());

  // Verify imported file exists.
  QJsonObject fileInfo = m_service->getFileInfo(nbId, "ImportDest/external_file.md");
  QVERIFY(!fileInfo.isEmpty());

  // Verify file content was copied.
  QJsonObject nbConfig = m_service->getNotebookConfig(nbId);
  QString rootFolder = nbConfig["rootFolder"].toString();
  QString importedFilePath = rootFolder + "/ImportDest/external_file.md";
  QFile importedFile(importedFilePath);
  QVERIFY(importedFile.open(QIODevice::ReadOnly));
  QString content = QString::fromUtf8(importedFile.readAll());
  importedFile.close();
  QVERIFY(content.contains("External File Content"));
}

void TestNotebookService::testGetFileInfo() {
  QString nbPath = m_tempDir.filePath("fileinfo_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "info.md");
  QJsonObject fileInfo = m_service->getFileInfo(nbId, "info.md");
  QVERIFY(!fileInfo.isEmpty());

  // Test metadata operations.
  QString metadataJson = R"({"custom_field": "custom_value"})";
  m_service->updateFileMetadata(nbId, "info.md", metadataJson);

  QJsonObject metadata = m_service->getFileMetadata(nbId, "info.md");
  QVERIFY(!metadata.isEmpty());
}

// ===== Hook tests: Delete =====

void TestNotebookService::testDeleteFileCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_del_file_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "hook_delete.md");
  QJsonObject before = m_service->getFileInfo(nbId, "hook_delete.md");
  QVERIFY(!before.isEmpty());

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->deleteFile(nbId, "hook_delete.md");
  QVERIFY(!result);

  // File should still exist.
  QJsonObject after = m_service->getFileInfo(nbId, "hook_delete.md");
  QVERIFY(!after.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testDeleteFolderCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_del_folder_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "HookDeleteFolder");
  QJsonObject before = m_service->getFolderConfig(nbId, "HookDeleteFolder");
  QVERIFY(!before.isEmpty());

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->deleteFolder(nbId, "HookDeleteFolder");
  QVERIFY(!result);

  // Folder should still exist.
  QJsonObject after = m_service->getFolderConfig(nbId, "HookDeleteFolder");
  QVERIFY(!after.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testDeleteFileFiresBeforeHook() {
  QString nbPath = m_tempDir.filePath("hook_del_file_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "hook_fires_delete.md");

  bool hookFired = false;
  QVariantMap capturedArgs;
  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete,
      [&hookFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        hookFired = true;
        capturedArgs = p_args;
      },
      10);

  m_service->deleteFile(nbId, "hook_fires_delete.md");

  QVERIFY(hookFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("relativePath")].toString(),
           QStringLiteral("hook_fires_delete.md"));
  QCOMPARE(capturedArgs[QStringLiteral("isFolder")].toBool(), false);
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("delete"));

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testDeleteFolderFiresBeforeHook() {
  QString nbPath = m_tempDir.filePath("hook_del_folder_fires_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "HookFiresDeleteFolder");

  bool hookFired = false;
  QVariantMap capturedArgs;
  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete,
      [&hookFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        hookFired = true;
        capturedArgs = p_args;
      },
      10);

  m_service->deleteFolder(nbId, "HookFiresDeleteFolder");

  QVERIFY(hookFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("relativePath")].toString(),
           QStringLiteral("HookFiresDeleteFolder"));
  QCOMPARE(capturedArgs[QStringLiteral("isFolder")].toBool(), true);
  QCOMPARE(capturedArgs[QStringLiteral("operation")].toString(), QStringLiteral("delete"));

  m_hookMgr->removeAction(hookId);
}

// ===== Hook tests: Rename =====

void TestNotebookService::testRenameFileCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_ren_file_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "hook_rename_orig.md");

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeRename, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->renameFile(nbId, "hook_rename_orig.md", "hook_rename_new.md");
  QVERIFY(!result);

  // Original file should still exist under old name.
  QJsonObject origInfo = m_service->getFileInfo(nbId, "hook_rename_orig.md");
  QVERIFY(!origInfo.isEmpty());

  // New name should not exist.
  QJsonObject newInfo = m_service->getFileInfo(nbId, "hook_rename_new.md");
  QVERIFY(newInfo.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testRenameFolderCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_ren_folder_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "HookRenameOrigFolder");

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeRename, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->renameFolder(nbId, "HookRenameOrigFolder", "HookRenameNewFolder");
  QVERIFY(!result);

  // Original folder should still exist.
  QJsonObject origConfig = m_service->getFolderConfig(nbId, "HookRenameOrigFolder");
  QVERIFY(!origConfig.isEmpty());

  // New name should not exist.
  QJsonObject newConfig = m_service->getFolderConfig(nbId, "HookRenameNewFolder");
  QVERIFY(newConfig.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testRenameFileFiresAfterHook() {
  QString nbPath = m_tempDir.filePath("hook_ren_file_after_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "hook_after_orig.md");

  bool afterFired = false;
  QVariantMap capturedArgs;
  int hookId = m_hookMgr->addAction(
      HookNames::NodeAfterRename,
      [&afterFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        afterFired = true;
        capturedArgs = p_args;
      },
      10);

  bool result = m_service->renameFile(nbId, "hook_after_orig.md", "hook_after_new.md");
  QVERIFY(result);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("oldName")].toString(),
           QStringLiteral("hook_after_orig.md"));
  QCOMPARE(capturedArgs[QStringLiteral("newName")].toString(), QStringLiteral("hook_after_new.md"));
  QCOMPARE(capturedArgs[QStringLiteral("isFolder")].toBool(), false);

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testRenameFolderFiresAfterHook() {
  QString nbPath = m_tempDir.filePath("hook_ren_folder_after_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "HookAfterOrigFolder");

  bool afterFired = false;
  QVariantMap capturedArgs;
  int hookId = m_hookMgr->addAction(
      HookNames::NodeAfterRename,
      [&afterFired, &capturedArgs](HookContext &, const QVariantMap &p_args) {
        afterFired = true;
        capturedArgs = p_args;
      },
      10);

  bool result = m_service->renameFolder(nbId, "HookAfterOrigFolder", "HookAfterNewFolder");
  QVERIFY(result);
  QVERIFY(afterFired);
  QCOMPARE(capturedArgs[QStringLiteral("notebookId")].toString(), nbId);
  QCOMPARE(capturedArgs[QStringLiteral("oldName")].toString(),
           QStringLiteral("HookAfterOrigFolder"));
  QCOMPARE(capturedArgs[QStringLiteral("newName")].toString(),
           QStringLiteral("HookAfterNewFolder"));
  QCOMPARE(capturedArgs[QStringLiteral("isFolder")].toBool(), true);

  m_hookMgr->removeAction(hookId);
}

// ===== Hook tests: Move =====

void TestNotebookService::testMoveFileCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_mv_file_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "hook_move_file.md");
  m_service->createFolder(nbId, "", "MoveDestFolder");

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeMove, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->moveFile(nbId, "hook_move_file.md", "MoveDestFolder");
  QVERIFY(!result);

  // File should remain at original location.
  QJsonObject origInfo = m_service->getFileInfo(nbId, "hook_move_file.md");
  QVERIFY(!origInfo.isEmpty());

  // File should NOT be at destination.
  QJsonObject movedInfo = m_service->getFileInfo(nbId, "MoveDestFolder/hook_move_file.md");
  QVERIFY(movedInfo.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testMoveFolderCancelledByHook() {
  QString nbPath = m_tempDir.filePath("hook_mv_folder_cancel_nb");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFolder(nbId, "", "HookMoveFolder");
  m_service->createFolder(nbId, "", "MoveFolderDest");

  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeMove, [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); },
      10);

  bool result = m_service->moveFolder(nbId, "HookMoveFolder", "MoveFolderDest");
  QVERIFY(!result);

  // Folder should remain at original location.
  QJsonObject origConfig = m_service->getFolderConfig(nbId, "HookMoveFolder");
  QVERIFY(!origConfig.isEmpty());

  // Folder should NOT be at destination.
  QJsonObject movedConfig = m_service->getFolderConfig(nbId, "MoveFolderDest/HookMoveFolder");
  QVERIFY(movedConfig.isEmpty());

  m_hookMgr->removeAction(hookId);
}

void TestNotebookService::testRecycleBinCleanupServiceWrapper() {
  const qint64 now = Q_INT64_C(1785337074532);
  const qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
  const QString root = m_tempDir.filePath(QStringLiteral("cleanup_service_notebook"));
  const QString notebookId = createTestNotebook(root);
  QVERIFY(!notebookId.isEmpty());
  const QString recycleBin = m_service->getRecycleBinPath(notebookId);
  QVERIFY(!recycleBin.isEmpty());
  const QString oldPath = QDir(recycleBin).filePath(QStringLiteral("old.md"));
  const QString newPath = QDir(recycleBin).filePath(QStringLiteral("new.md"));
  QVERIFY(WriteFileAt(oldPath, now - 2 * day));
  QVERIFY(WriteFileAt(newPath, now));

  auto prepared = m_service->prepareRecycleBinCleanup(notebookId, now - day);
  QVERIFY(prepared.isValid());
  const RecycleBinCleanupResult result = m_service->executeRecycleBinCleanup(prepared);
  QCOMPARE(result.m_error, VXCORE_OK);
  QCOMPARE(result.m_removedCount, 1);
  QVERIFY(!QFileInfo::exists(oldPath));
  QVERIFY(QFileInfo::exists(newPath));

  const QString cancelledPath = QDir(recycleBin).filePath(QStringLiteral("cancelled.md"));
  QVERIFY(WriteFileAt(cancelledPath, now - 2 * day));
  auto cancelled = m_service->prepareRecycleBinCleanup(notebookId, now - day);
  QVERIFY(cancelled.isValid());
  cancelled.cancel();
  const RecycleBinCleanupResult cancelledResult = m_service->executeRecycleBinCleanup(cancelled);
  QCOMPARE(cancelledResult.m_error, VXCORE_ERR_CANCELLED);
  QCOMPARE(cancelledResult.m_removedCount, 0);
  QVERIFY(QFileInfo::exists(cancelledPath));
}

void TestNotebookService::testAutomaticCleanupStartupOpenAndDuplicateSuppression() {
  const qint64 now = Q_INT64_C(1785337074532);
  const qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinRetentionDays(1);
  config.setRecycleBinAutoCleanupEnabled(true, now - 2 * day);

  const QString firstRoot = m_tempDir.filePath(QStringLiteral("cleanup_controller_startup"));
  const QString firstId = createTestNotebook(firstRoot);
  const QString firstOld =
      QDir(m_service->getRecycleBinPath(firstId)).filePath(QStringLiteral("old.md"));
  QVERIFY(WriteFileAt(firstOld, now - 2 * day));

  RecycleBinController controller(*m_services);
  controller.setNowProviderForTesting([now]() { return now; });
  QStringList completedIds;
  QList<VxCoreError> completedErrors;
  QList<int> removedCounts;
  connect(&controller, &RecycleBinController::automaticCleanupFinished, this,
          [&](const QString &p_notebookId, VxCoreError p_error, int p_removedCount) {
            completedIds.append(p_notebookId);
            completedErrors.append(p_error);
            removedCounts.append(p_removedCount);
          });
  controller.startAutomaticCleanup();
  m_hookMgr->doAction(HookNames::MainWindowAfterStart);
  QTRY_COMPARE_WITH_TIMEOUT(completedIds.size(), 1, 5000);
  QCOMPARE(completedIds[0], firstId);
  QCOMPARE(completedErrors[0], VXCORE_OK);
  QCOMPARE(removedCounts[0], 1);
  QVERIFY(!QFileInfo::exists(firstOld));

  const QString secondRoot = m_tempDir.filePath(QStringLiteral("cleanup_controller_open"));
  const QString secondId = createTestNotebook(secondRoot);
  const QString secondOld =
      QDir(m_service->getRecycleBinPath(secondId)).filePath(QStringLiteral("old.md"));
  QVERIFY(WriteFileAt(secondOld, now - 2 * day));
  QVERIFY(m_service->closeNotebook(secondId));
  QCOMPARE(m_service->openNotebook(secondRoot), secondId);
  QTRY_COMPARE_WITH_TIMEOUT(completedIds.size(), 2, 5000);
  QCOMPARE(completedIds[1], secondId);
  QCOMPARE(completedErrors[1], VXCORE_OK);
  QCOMPARE(removedCounts[1], 1);
  QVERIFY(!QFileInfo::exists(secondOld));

  const QString duplicateOld =
      QDir(m_service->getRecycleBinPath(firstId)).filePath(QStringLiteral("duplicate.md"));
  QVERIFY(WriteFileAt(duplicateOld, now - 2 * day));
  NotebookOpenEvent event;
  event.notebookId = firstId;
  m_hookMgr->doAction(HookNames::NotebookAfterOpen, event);
  m_hookMgr->doAction(HookNames::NotebookAfterOpen, event);
  QTRY_COMPARE_WITH_TIMEOUT(completedIds.size(), 3, 5000);
  QTest::qWait(200);
  QCOMPARE(completedIds.size(), 3);
  QCOMPARE(completedIds[2], firstId);
  QCOMPARE(removedCounts[2], 1);
}

void TestNotebookService::testAutomaticCleanupGraceRepair() {
  const qint64 now = Q_INT64_C(1785337074532);
  const qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinRetentionDays(60);
  config.setRecycleBinAutoCleanupEnabled(true, now - 90 * day);
  config.setRecycleBinCleanupEnabledSinceUtc(0);

  const QString root = m_tempDir.filePath(QStringLiteral("cleanup_controller_grace"));
  const QString notebookId = createTestNotebook(root);
  const QString oldPath =
      QDir(m_service->getRecycleBinPath(notebookId)).filePath(QStringLiteral("old.md"));
  QVERIFY(WriteFileAt(oldPath, now - 90 * day));

  RecycleBinController controller(*m_services);
  controller.setNowProviderForTesting([now]() { return now; });
  int completionCount = 0;
  connect(&controller, &RecycleBinController::automaticCleanupFinished, this,
          [&](const QString &, VxCoreError, int) { ++completionCount; });
  controller.startAutomaticCleanup();
  m_hookMgr->doAction(HookNames::MainWindowAfterStart);
  QCOMPARE(config.getRecycleBinCleanupEnabledSinceUtc(), now);
  QCOMPARE(completionCount, 0);
  QVERIFY(QFileInfo::exists(oldPath));

  config.setRecycleBinCleanupEnabledSinceUtc(now + day);
  NotebookOpenEvent event;
  event.notebookId = notebookId;
  m_hookMgr->doAction(HookNames::NotebookAfterOpen, event);
  QCOMPARE(config.getRecycleBinCleanupEnabledSinceUtc(), now);
  QCOMPARE(completionCount, 0);
  QVERIFY(QFileInfo::exists(oldPath));
}

void TestNotebookService::testAutomaticCleanupSkipsReadOnlyAndRaw() {
  const qint64 now = Q_INT64_C(1785337074532);
  const qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinRetentionDays(1);
  config.setRecycleBinAutoCleanupEnabled(true, now - 2 * day);

  const QString bundledRoot = m_tempDir.filePath(QStringLiteral("cleanup_controller_readonly"));
  const QString bundledId = createTestNotebook(bundledRoot);
  const QString oldPath =
      QDir(m_service->getRecycleBinPath(bundledId)).filePath(QStringLiteral("old.md"));
  QVERIFY(WriteFileAt(oldPath, now - 2 * day));
  QCOMPARE(vxcore_notebook_set_read_only(m_context, bundledId.toUtf8().constData(), 1), VXCORE_OK);

  const QString rawRoot = m_tempDir.filePath(QStringLiteral("cleanup_controller_raw"));
  const QString rawId =
      m_service->createNotebook(rawRoot, QStringLiteral(R"({"name":"Raw"})"), NotebookType::Raw);
  QVERIFY(!rawId.isEmpty());

  RecycleBinController controller(*m_services);
  controller.setNowProviderForTesting([now]() { return now; });
  QList<VxCoreError> errors;
  connect(&controller, &RecycleBinController::automaticCleanupFinished, this,
          [&](const QString &, VxCoreError p_error, int) { errors.append(p_error); });
  controller.startAutomaticCleanup();
  m_hookMgr->doAction(HookNames::MainWindowAfterStart);
  QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 2, 5000);
  QVERIFY(errors.contains(VXCORE_ERR_READ_ONLY));
  QVERIFY(errors.contains(VXCORE_ERR_UNSUPPORTED));
  QVERIFY(QFileInfo::exists(oldPath));
  QCOMPARE(vxcore_notebook_set_read_only(m_context, bundledId.toUtf8().constData(), 0), VXCORE_OK);
}

void TestNotebookService::testAutomaticCleanupDestructorCancelsGateWait() {
  const qint64 now = Q_INT64_C(1785337074532);
  const qint64 day = Q_INT64_C(24) * 60 * 60 * 1000;
  auto &config = m_configMgr->getCoreConfig();
  config.setRecycleBinRetentionDays(1);
  config.setRecycleBinAutoCleanupEnabled(true, now - 2 * day);

  const QString root = m_tempDir.filePath(QStringLiteral("cleanup_controller_cancel"));
  const QString notebookId = createTestNotebook(root);
  const QString oldPath =
      QDir(m_service->getRecycleBinPath(notebookId)).filePath(QStringLiteral("old.md"));
  QVERIFY(WriteFileAt(oldPath, now - 2 * day));

  std::atomic_bool releaseGate{false};
  std::promise<void> gateLocked;
  auto gateLockedFuture = gateLocked.get_future();
  std::thread holder([&]() {
    NotebookIoGate::ScopedLock lock(*m_ioGate, notebookId);
    gateLocked.set_value();
    while (!releaseGate.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  gateLockedFuture.wait();

  auto *controller = new RecycleBinController(*m_services);
  controller->setNowProviderForTesting([now]() { return now; });
  controller->startAutomaticCleanup();
  NotebookOpenEvent event;
  event.notebookId = notebookId;
  m_hookMgr->doAction(HookNames::NotebookAfterOpen, event);
  QTest::qWait(150);

  QElapsedTimer elapsed;
  elapsed.start();
  delete controller;
  const qint64 destructionMs = elapsed.elapsed();
  releaseGate.store(true, std::memory_order_relaxed);
  holder.join();

  QVERIFY2(destructionMs < 2000, "controller teardown remained blocked on the notebook I/O gate");
  QVERIFY(QFileInfo::exists(oldPath));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookService)
#include "test_notebookservice.moc"
