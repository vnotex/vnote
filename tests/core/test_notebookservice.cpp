#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

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

  // Tag operations tests.
  void testCreateAndDeleteTag();
  void testTagFile();
  void testListTags();
  void testMoveTag();

  // Signal tests.
  void testNoteCreatedSignal();
  void testNoteUpdatedSignal();
  void testNoteDeletedSignal();
  void testTagAddedSignal();
  void testNotebookOpenedClosedSignals();

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

private:
  // Helper to create test notebook and return ID.
  QString createTestNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  HookManager *m_hookMgr = nullptr;
  TempDirFixture m_tempDir;
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
}

void TestNotebookService::cleanupTestCase() {
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

void TestNotebookService::testCreateAndDeleteTag() {
  QString nbPath = m_tempDir.filePath("tag_test_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createTag(nbId, "TestTag");

  QJsonArray tags = m_service->listTags(nbId);
  QVERIFY(tags.size() > 0);

  bool found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "TestTag") {
      found = true;
      break;
    }
  }
  QVERIFY(found);

  m_service->deleteTag(nbId, "TestTag");
  tags = m_service->listTags(nbId);

  found = false;
  for (const auto &tagVal : tags) {
    if (tagVal.toObject()["name"].toString() == "TestTag") {
      found = true;
      break;
    }
  }
  QVERIFY(!found);
}

void TestNotebookService::testTagFile() {
  QString nbPath = m_tempDir.filePath("tagfile_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "tagged.md");
  m_service->createTag(nbId, "FileTag");
  m_service->tagFile(nbId, "tagged.md", "FileTag");

  // Verify file is tagged.
  QJsonObject fileInfo = m_service->getFileInfo(nbId, "tagged.md");
  QVERIFY(!fileInfo.isEmpty());

  m_service->untagFile(nbId, "tagged.md", "FileTag");
}

void TestNotebookService::testListTags() {
  QString nbPath = m_tempDir.filePath("listtags_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createTag(nbId, "Tag1");
  m_service->createTag(nbId, "Tag2");

  QJsonArray tags = m_service->listTags(nbId);
  QVERIFY(tags.size() >= 2);
}

void TestNotebookService::testMoveTag() {
  QString nbPath = m_tempDir.filePath("movetag_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createTag(nbId, "ParentTag");
  m_service->createTag(nbId, "ChildTag");
  m_service->moveTag(nbId, "ChildTag", "ParentTag");

  QJsonArray tags = m_service->listTags(nbId);
  QVERIFY(tags.size() >= 2);
}

void TestNotebookService::testNoteCreatedSignal() {
  QString nbPath = m_tempDir.filePath("signal_create_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QSignalSpy spy(m_service, &NotebookCoreService::noteCreated);

  m_service->createFile(nbId, "", "signal_test.md");

  // Wait for signal (vxcore events may be async).
  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);

  QList<QVariant> arguments = spy.takeFirst();
  QString payloadJson = arguments.at(0).toString();
  quint64 timestamp = arguments.at(1).toULongLong();

  QVERIFY(!payloadJson.isEmpty());
  QVERIFY(timestamp > 0);
}

void TestNotebookService::testNoteUpdatedSignal() {
  QString nbPath = m_tempDir.filePath("signal_update_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "update_test.md");

  QSignalSpy spy(m_service, &NotebookCoreService::noteUpdated);

  QString metadataJson = R"({"updated": "true"})";
  m_service->updateFileMetadata(nbId, "update_test.md", metadataJson);

  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);
}

void TestNotebookService::testNoteDeletedSignal() {
  QString nbPath = m_tempDir.filePath("signal_delete_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "delete_test.md");

  QSignalSpy spy(m_service, &NotebookCoreService::noteDeleted);

  m_service->deleteFile(nbId, "delete_test.md");

  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);
}

void TestNotebookService::testTagAddedSignal() {
  QString nbPath = m_tempDir.filePath("signal_tag_notebook");
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  m_service->createFile(nbId, "", "tag_signal_test.md");
  m_service->createTag(nbId, "SignalTag");

  QSignalSpy spy(m_service, &NotebookCoreService::tagAdded);

  m_service->tagFile(nbId, "tag_signal_test.md", "SignalTag");

  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);
}

void TestNotebookService::testNotebookOpenedClosedSignals() {
  QString nbPath = m_tempDir.filePath("signal_openclose_notebook");

  QSignalSpy openSpy(m_service, &NotebookCoreService::notebookOpened);
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(openSpy.wait(1000));
  QCOMPARE(openSpy.count(), 1);

  QSignalSpy closeSpy(m_service, &NotebookCoreService::notebookClosed);
  m_service->closeNotebook(nbId);

  QVERIFY(closeSpy.wait(1000));
  QCOMPARE(closeSpy.count(), 1);
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
      HookNames::NodeBeforeDelete,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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
      HookNames::NodeBeforeDelete,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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
      HookNames::NodeBeforeRename,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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
      HookNames::NodeBeforeRename,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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
  QCOMPARE(capturedArgs[QStringLiteral("newName")].toString(),
           QStringLiteral("hook_after_new.md"));
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
      HookNames::NodeBeforeMove,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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
      HookNames::NodeBeforeMove,
      [](HookContext &p_ctx, const QVariantMap &) { p_ctx.cancel(); }, 10);

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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookService)
#include "test_notebookservice.moc"
