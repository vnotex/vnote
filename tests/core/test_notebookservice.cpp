#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <core/services/notebookservice.h>
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

private:
  // Helper to create test notebook and return ID.
  QString createTestNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookService *m_service = nullptr;
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
  m_service = new NotebookService(m_context, this);
  QVERIFY(m_service != nullptr);
}

void TestNotebookService::cleanupTestCase() {
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

  QSignalSpy spy(m_service, &NotebookService::noteCreated);

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

  QSignalSpy spy(m_service, &NotebookService::noteUpdated);

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

  QSignalSpy spy(m_service, &NotebookService::noteDeleted);

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

  QSignalSpy spy(m_service, &NotebookService::tagAdded);

  m_service->tagFile(nbId, "tag_signal_test.md", "SignalTag");

  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);
}

void TestNotebookService::testNotebookOpenedClosedSignals() {
  QString nbPath = m_tempDir.filePath("signal_openclose_notebook");

  QSignalSpy openSpy(m_service, &NotebookService::notebookOpened);
  QString nbId = createTestNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QVERIFY(openSpy.wait(1000));
  QCOMPARE(openSpy.count(), 1);

  QSignalSpy closeSpy(m_service, &NotebookService::notebookClosed);
  m_service->closeNotebook(nbId);

  QVERIFY(closeSpy.wait(1000));
  QCOMPARE(closeSpy.count(), 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookService)
#include "test_notebookservice.moc"
