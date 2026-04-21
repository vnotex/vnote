#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <core/services/vnote3migrationservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

// Test-only failpoint declared in vnote3migrationservice.cpp
namespace vnotex {
extern void vnote3MigrationService_setFailAfterCreate(bool p_fail);
} // namespace vnotex

namespace tests {

class TestVNote3MigrationService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConstruction();
  void testConvertStubReturnsFalse();

  // inspectSourceNotebook scenarios
  void testInspectMissingConfigFile();
  void testInspectMalformedJson();
  void testInspectWrongVersion();
  void testInspectMissingVersion();
  void testInspectWrongConfigMgr();
  void testInspectAbsoluteImageFolder();
  void testInspectAbsoluteAttachmentFolder();
  void testInspectValidNotebook();
  void testInspectValidNotebookMinimalFields();

  // convertNotebook scenarios
  void testConvertHappyPath();
  void testConvertExcludesVxNotebook();
  void testConvertExcludesVxJson();
  void testConvertExcludesVxRecycleBin();
  void testConvertPreservesVxImages();
  void testConvertPreservesVxAttachments();
  void testConvertInvalidSourceFails();

  // Comprehensive conversion verification
  void testConvertPreservesStructureAndContent();
  void testConvertPreservesNotebookNameAndDescription();
  void testConvertSourceImmutable();

  // Rollback scenarios
  void testRollbackOnForcedFailure();
  void testSourceImmutableAfterFailure();

  // T12: Metadata round-trip tests
  void testConvertPreservesTags();
  void testConvertPreservesTimestamps();
  void testConvertPreservesVisualMetadata();
  void testConvertMigratesAttachments();
  void testConvertPreservesFolderTimestamps();
  void testConvertRealXpayNotebook();

  // T13: Edge case tests
  void testConvertMissingVxJsonGraceful();
  void testConvertGitDirectoryNotImported();
  void testProgressSignalEmitted();
  void testConvertEmptyTagsSkipped();
  void testConvertInvalidTimestampDegrades();
  void testConvertEmptyAttachmentFolderSkipped();
  void testConvertMissingAttachmentDirDegrades();
  void testConvertVxImagesAtMultipleLevels();
  void testConvertDegradedWarningsCollected();

  // T-TagGraph: Tag graph migration tests
  void testConvertPreservesTagGraph();
  void testConvertEmptyTagGraphNoOp();
  void testConvertTagGraphOrphanTagsDegrades();
  void testConvertMalformedTagGraphPairsSkipped();
  void testConvertPreservesMultiLevelTagGraph();

  // Regression: 17-file folder with Chinese filenames and subfolder
  void testConvertFolderWith17FilesAndSubfolder();

  // Regression: attachments in Chinese-named folder should not get garbled paths
  void testConvertMigratesAttachmentsInChineseFolder();

  // Supplementary copy tests
  void testCopyRemainingCopiesUnindexedImageFolder();
  void testCopyRemainingCopiesUnindexedFiles();
  void testCopyRemainingSkipsExclusions();
  void testCopyRemainingDoesNotOverwriteImportedFiles();

private:
  // Returns absolute path to a test data fixture, or empty string if not found.
  QString findFixture(const QString &p_relPath);

  void writeNotebookConfig(const QString &p_basePath, const QByteArray &p_content);
  QByteArray makeConfigJson(const QJsonObject &p_overrides = QJsonObject());
  void buildSyntheticSourceTree(const QString &p_basePath);
  void createSyntheticVNote3Source(const QString &p_basePath);
  bool pathExistsInNotebook(const QString &p_notebookRoot, const QString &p_relPath);
  void writeVxJson(const QString &p_dirPath, const QJsonObject &p_content);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  TagCoreService *m_tagCoreService = nullptr;
  VNote3MigrationService *m_service = nullptr;
  TempDirFixture m_tempDir;
};

void TestVNote3MigrationService::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // Enable test mode to use isolated temp directories instead of real AppData.
  vxcore_set_test_mode(1);

  // Initialize VxCore context.
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  // Create NotebookCoreService with context.
  m_notebookService = new NotebookCoreService(m_context, this);
  QVERIFY(m_notebookService != nullptr);

  // Create TagCoreService with context.
  m_tagCoreService = new TagCoreService(m_context, this);
  QVERIFY(m_tagCoreService != nullptr);

  // Create VNote3MigrationService.
  m_service = new VNote3MigrationService(m_notebookService, m_tagCoreService, this);
  QVERIFY(m_service != nullptr);
}

void TestVNote3MigrationService::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

  delete m_tagCoreService;
  m_tagCoreService = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestVNote3MigrationService::writeNotebookConfig(const QString &p_basePath,
                                                     const QByteArray &p_content) {
  QDir(p_basePath).mkpath(QStringLiteral("vx_notebook"));
  QString filePath = p_basePath + QStringLiteral("/vx_notebook/vx_notebook.json");
  QFile file(filePath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(p_content);
  file.close();
}

QByteArray TestVNote3MigrationService::makeConfigJson(const QJsonObject &p_overrides) {
  QJsonObject jobj;
  jobj[QStringLiteral("version")] = 3;
  jobj[QStringLiteral("name")] = QStringLiteral("My Notebook");
  jobj[QStringLiteral("description")] = QStringLiteral("A test notebook");
  jobj[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  jobj[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  jobj[QStringLiteral("created_time")] = QStringLiteral("2024-01-01T00:00:00Z");
  jobj[QStringLiteral("version_controller")] = QStringLiteral("dummy");
  jobj[QStringLiteral("config_mgr")] = QStringLiteral("vx.vnotex");

  // Apply overrides (insert or replace keys).
  for (auto it = p_overrides.begin(); it != p_overrides.end(); ++it) {
    jobj[it.key()] = it.value();
  }

  return QJsonDocument(jobj).toJson(QJsonDocument::Compact);
}

void TestVNote3MigrationService::testConstruction() { QVERIFY(m_service != nullptr); }

void TestVNote3MigrationService::testConvertStubReturnsFalse() {
  // convertNotebook with invalid source should fail.
  TempDirFixture emptyDir;
  QVERIFY(emptyDir.isValid());
  auto result = m_service->convertNotebook(emptyDir.path(), m_tempDir.filePath("dest_stub"));
  QVERIFY(!result.success);
}

// --- inspectSourceNotebook scenarios ---

void TestVNote3MigrationService::testInspectMissingConfigFile() {
  // Point to a directory without vx_notebook/vx_notebook.json.
  TempDirFixture emptyDir;
  QVERIFY(emptyDir.isValid());

  auto result = m_service->inspectSourceNotebook(emptyDir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("not found")));
}

void TestVNote3MigrationService::testInspectMalformedJson() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());
  writeNotebookConfig(dir.path(), QByteArrayLiteral("{not valid json!!!"));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("Malformed JSON")));
}

void TestVNote3MigrationService::testInspectWrongVersion() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  QJsonObject overrides;
  overrides[QStringLiteral("version")] = 2;
  writeNotebookConfig(dir.path(), makeConfigJson(overrides));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("version")));
}

void TestVNote3MigrationService::testInspectMissingVersion() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  // Build JSON without "version" key.
  QJsonObject jobj;
  jobj[QStringLiteral("name")] = QStringLiteral("No Version");
  jobj[QStringLiteral("config_mgr")] = QStringLiteral("vx.vnotex");
  writeNotebookConfig(dir.path(), QJsonDocument(jobj).toJson(QJsonDocument::Compact));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("version")));
}

void TestVNote3MigrationService::testInspectWrongConfigMgr() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  QJsonObject overrides;
  overrides[QStringLiteral("config_mgr")] = QStringLiteral("some.other.mgr");
  writeNotebookConfig(dir.path(), makeConfigJson(overrides));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("config_mgr")));
}

void TestVNote3MigrationService::testInspectAbsoluteImageFolder() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  QJsonObject overrides;
#ifdef Q_OS_WIN
  overrides[QStringLiteral("image_folder")] = QStringLiteral("C:/absolute/images");
#else
  overrides[QStringLiteral("image_folder")] = QStringLiteral("/absolute/images");
#endif
  writeNotebookConfig(dir.path(), makeConfigJson(overrides));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("image_folder")));
}

void TestVNote3MigrationService::testInspectAbsoluteAttachmentFolder() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  QJsonObject overrides;
#ifdef Q_OS_WIN
  overrides[QStringLiteral("attachment_folder")] = QStringLiteral("D:/abs/attachments");
#else
  overrides[QStringLiteral("attachment_folder")] = QStringLiteral("/abs/attachments");
#endif
  writeNotebookConfig(dir.path(), makeConfigJson(overrides));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY(!result.valid);
  QVERIFY(result.errorMessage.contains(QStringLiteral("attachment_folder")));
}

void TestVNote3MigrationService::testInspectValidNotebook() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());
  writeNotebookConfig(dir.path(), makeConfigJson());

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY2(result.valid, qPrintable(result.errorMessage));
  QVERIFY(result.errorMessage.isEmpty());

  // Check extracted fields.
  QCOMPARE(result.notebookName, QStringLiteral("My Notebook"));
  QCOMPARE(result.notebookDescription, QStringLiteral("A test notebook"));

  // Check warnings — fixed ordered list with at least 3 entries.
  QVERIFY(result.warnings.size() >= 3);
  QVERIFY(result.warnings[0].contains(QStringLiteral("History")));
  QVERIFY(result.warnings[1].contains(QStringLiteral("Per-node metadata")));
  QVERIFY(result.warnings[2].contains(QStringLiteral("Recycle bin")));
}

void TestVNote3MigrationService::testInspectValidNotebookMinimalFields() {
  TempDirFixture dir;
  QVERIFY(dir.isValid());

  // Minimal valid config: version=3, config_mgr="vx.vnotex", no image/attachment folders.
  QJsonObject jobj;
  jobj[QStringLiteral("version")] = 3;
  jobj[QStringLiteral("config_mgr")] = QStringLiteral("vx.vnotex");
  jobj[QStringLiteral("name")] = QStringLiteral("Minimal");
  writeNotebookConfig(dir.path(), QJsonDocument(jobj).toJson(QJsonDocument::Compact));

  auto result = m_service->inspectSourceNotebook(dir.path());
  QVERIFY2(result.valid, qPrintable(result.errorMessage));
  QCOMPARE(result.notebookName, QStringLiteral("Minimal"));
  QVERIFY(result.notebookDescription.isEmpty());
  QVERIFY(result.warnings.size() >= 3);
}

// --- Helper: build a synthetic VNote3 source tree ---

void TestVNote3MigrationService::buildSyntheticSourceTree(const QString &p_basePath) {
  QDir base(p_basePath);

  // Create vx_notebook/vx_notebook.json (the legacy config).
  writeNotebookConfig(p_basePath, makeConfigJson());

  // Create nested user folders with markdown notes.
  base.mkpath(QStringLiteral("notes/sub"));
  {
    QFile f(base.filePath(QStringLiteral("notes/hello.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Hello\nWorld");
    f.close();
  }
  {
    QFile f(base.filePath(QStringLiteral("notes/sub/deep.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Deep\nNested note");
    f.close();
  }

  // Create a root-level markdown note.
  {
    QFile f(base.filePath(QStringLiteral("readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Root readme");
    f.close();
  }

  // Create folder-local vx.json files with proper file/folder listings.
  {
    QJsonObject readmeEntry;
    readmeEntry[QStringLiteral("name")] = QStringLiteral("readme.md");
    QJsonArray rootFiles;
    rootFiles.append(readmeEntry);
    QJsonObject notesFolder;
    notesFolder[QStringLiteral("name")] = QStringLiteral("notes");
    QJsonArray rootFolders;
    rootFolders.append(notesFolder);
    QJsonObject vxImagesFolder;
    vxImagesFolder[QStringLiteral("name")] = QStringLiteral("vx_images");
    rootFolders.append(vxImagesFolder);
    QJsonObject vxAttachFolder;
    vxAttachFolder[QStringLiteral("name")] = QStringLiteral("vx_attachments");
    rootFolders.append(vxAttachFolder);
    QJsonObject rootVx;
    rootVx[QStringLiteral("files")] = rootFiles;
    rootVx[QStringLiteral("folders")] = rootFolders;
    writeVxJson(p_basePath, rootVx);
  }
  {
    QJsonObject helloEntry;
    helloEntry[QStringLiteral("name")] = QStringLiteral("hello.md");
    QJsonArray notesFiles;
    notesFiles.append(helloEntry);
    QJsonObject subFolder;
    subFolder[QStringLiteral("name")] = QStringLiteral("sub");
    QJsonArray notesFolders;
    notesFolders.append(subFolder);
    QJsonObject notesVx;
    notesVx[QStringLiteral("files")] = notesFiles;
    notesVx[QStringLiteral("folders")] = notesFolders;
    writeVxJson(base.filePath(QStringLiteral("notes")), notesVx);
  }
  {
    QJsonObject deepEntry;
    deepEntry[QStringLiteral("name")] = QStringLiteral("deep.md");
    QJsonArray subFiles;
    subFiles.append(deepEntry);
    QJsonObject subVx;
    subVx[QStringLiteral("files")] = subFiles;
    subVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("notes/sub")), subVx);
  }

  // Create vx_images folder with a sample image (should be preserved).
  base.mkpath(QStringLiteral("vx_images"));
  {
    QFile f(base.filePath(QStringLiteral("vx_images/diagram.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png data");
    f.close();
  }
  // vx_images/vx.json lists diagram.png.
  {
    QJsonObject diagramEntry;
    diagramEntry[QStringLiteral("name")] = QStringLiteral("diagram.png");
    QJsonArray imgFiles;
    imgFiles.append(diagramEntry);
    QJsonObject imgVx;
    imgVx[QStringLiteral("files")] = imgFiles;
    imgVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("vx_images")), imgVx);
  }

  // Create vx_attachments folder with a sample attachment (should be preserved).
  base.mkpath(QStringLiteral("vx_attachments"));
  {
    QFile f(base.filePath(QStringLiteral("vx_attachments/doc.pdf")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake pdf data");
    f.close();
  }
  // vx_attachments/vx.json lists doc.pdf.
  {
    QJsonObject docEntry;
    docEntry[QStringLiteral("name")] = QStringLiteral("doc.pdf");
    QJsonArray attFiles;
    attFiles.append(docEntry);
    QJsonObject attVx;
    attVx[QStringLiteral("files")] = attFiles;
    attVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("vx_attachments")), attVx);
  }

  // Create vx_recycle_bin with content (should be excluded).
  base.mkpath(QStringLiteral("vx_recycle_bin"));
  {
    QFile f(base.filePath(QStringLiteral("vx_recycle_bin/deleted.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Deleted note");
    f.close();
  }
}

bool TestVNote3MigrationService::pathExistsInNotebook(const QString &p_notebookRoot,
                                                      const QString &p_relPath) {
  QString fullPath = p_notebookRoot + QStringLiteral("/") + p_relPath;
  return QFileInfo::exists(fullPath);
}

void TestVNote3MigrationService::writeVxJson(const QString &p_dirPath,
                                             const QJsonObject &p_content) {
  QFile f(p_dirPath + QStringLiteral("/vx.json"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument(p_content).toJson(QJsonDocument::Compact));
  f.close();
}

QString TestVNote3MigrationService::findFixture(const QString &p_relPath) {
  // QFINDTESTDATA searches relative to the source file directory (via
  // QT_TESTCASE_SOURCEDIR set automatically by Qt6 CMake integration).
  return QFINDTESTDATA(p_relPath);
}

// --- Helper: create a synthetic VNote3 source with specific names ---

void TestVNote3MigrationService::createSyntheticVNote3Source(const QString &p_basePath) {
  QDir base(p_basePath);

  // Create vx_notebook/vx_notebook.json with specific name/description.
  QJsonObject overrides;
  overrides[QStringLiteral("name")] = QStringLiteral("SyntheticNB");
  overrides[QStringLiteral("description")] = QStringLiteral("SyntheticDesc");
  writeNotebookConfig(p_basePath, makeConfigJson(overrides));

  // Create vx_notebook/notebook.db (legacy DB — should be excluded).
  {
    QFile f(base.filePath(QStringLiteral("vx_notebook/notebook.db")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake sqlite db");
    f.close();
  }

  // Create notes/hello.md.
  base.mkpath(QStringLiteral("notes"));
  {
    QFile f(base.filePath(QStringLiteral("notes/hello.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Hello\nThis is hello.md");
    f.close();
  }

  // Create notes/subfolder/world.md.
  base.mkpath(QStringLiteral("notes/subfolder"));
  {
    QFile f(base.filePath(QStringLiteral("notes/subfolder/world.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# World\nThis is world.md");
    f.close();
  }

  // Create vx_images/photo.png (should be preserved as ordinary content).
  base.mkpath(QStringLiteral("vx_images"));
  {
    QFile f(base.filePath(QStringLiteral("vx_images/photo.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png data");
    f.close();
  }

  // Create vx_attachments/doc.pdf (should be preserved as ordinary content).
  base.mkpath(QStringLiteral("vx_attachments"));
  {
    QFile f(base.filePath(QStringLiteral("vx_attachments/doc.pdf")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake pdf data");
    f.close();
  }

  // Create vx.json files with proper file/folder listings.
  // Root vx.json: "notes", "vx_images", "vx_attachments" folders.
  {
    QJsonObject notesFolder;
    notesFolder[QStringLiteral("name")] = QStringLiteral("notes");
    QJsonObject vxImagesFolder;
    vxImagesFolder[QStringLiteral("name")] = QStringLiteral("vx_images");
    QJsonObject vxAttachFolder;
    vxAttachFolder[QStringLiteral("name")] = QStringLiteral("vx_attachments");
    QJsonArray rootFolders;
    rootFolders.append(notesFolder);
    rootFolders.append(vxImagesFolder);
    rootFolders.append(vxAttachFolder);
    QJsonObject rootVx;
    rootVx[QStringLiteral("files")] = QJsonArray();
    rootVx[QStringLiteral("folders")] = rootFolders;
    writeVxJson(p_basePath, rootVx);
  }

  // notes/vx.json: hello.md + subfolder.
  {
    QJsonObject helloEntry;
    helloEntry[QStringLiteral("name")] = QStringLiteral("hello.md");
    QJsonArray notesFiles;
    notesFiles.append(helloEntry);
    QJsonObject subfolderEntry;
    subfolderEntry[QStringLiteral("name")] = QStringLiteral("subfolder");
    QJsonArray notesFolders;
    notesFolders.append(subfolderEntry);
    QJsonObject notesVx;
    notesVx[QStringLiteral("files")] = notesFiles;
    notesVx[QStringLiteral("folders")] = notesFolders;
    writeVxJson(base.filePath(QStringLiteral("notes")), notesVx);
  }

  // notes/subfolder/vx.json: world.md.
  {
    QJsonObject worldEntry;
    worldEntry[QStringLiteral("name")] = QStringLiteral("world.md");
    QJsonArray subFiles;
    subFiles.append(worldEntry);
    QJsonObject subVx;
    subVx[QStringLiteral("files")] = subFiles;
    subVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("notes/subfolder")), subVx);
  }

  // vx_images/vx.json: photo.png.
  {
    QJsonObject photoEntry;
    photoEntry[QStringLiteral("name")] = QStringLiteral("photo.png");
    QJsonArray imgFiles;
    imgFiles.append(photoEntry);
    QJsonObject imgVx;
    imgVx[QStringLiteral("files")] = imgFiles;
    imgVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("vx_images")), imgVx);
  }

  // vx_attachments/vx.json: doc.pdf.
  {
    QJsonObject docEntry;
    docEntry[QStringLiteral("name")] = QStringLiteral("doc.pdf");
    QJsonArray attFiles;
    attFiles.append(docEntry);
    QJsonObject attVx;
    attVx[QStringLiteral("files")] = attFiles;
    attVx[QStringLiteral("folders")] = QJsonArray();
    writeVxJson(base.filePath(QStringLiteral("vx_attachments")), attVx);
  }

  // Create vx_recycle_bin/deleted.md (recycle bin — should be excluded).
  base.mkpath(QStringLiteral("vx_recycle_bin"));
  {
    QFile f(base.filePath(QStringLiteral("vx_recycle_bin/deleted.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Deleted note");
    f.close();
  }
}

// --- convertNotebook scenarios ---

void TestVNote3MigrationService::testConvertHappyPath() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("happy_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.destinationPath, destPath);
  QCOMPARE(result.notebookName, QStringLiteral("My Notebook"));

  // User files should exist.
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("notes/hello.md")));
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("notes/sub/deep.md")));
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("readme.md")));
}

void TestVNote3MigrationService::testConvertExcludesVxNotebook() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("excl_vxnb_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // The source's vx_notebook must NOT be imported as user content.
  // vxcore creates its own vx_notebook/ metadata in the destination, which is expected.
  // Verify the source's vx_notebook.json was not imported alongside user content at root.
  QVERIFY(!pathExistsInNotebook(destPath, QStringLiteral("vx_notebook.json")));
  // Verify no vx_notebook folder was imported into vxcore's content metadata tree.
  QVERIFY(!pathExistsInNotebook(destPath, QStringLiteral("vx_notebook/contents/vx_notebook")));
}

void TestVNote3MigrationService::testConvertExcludesVxJson() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("excl_vxjson_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Source vx.json files must NOT be imported as user content.
  // Note: vxcore creates its own folder config files (also named vx.json)
  // inside managed folders, so we only verify that the root-level legacy
  // vx.json is absent from user content.
  QVERIFY2(!QFileInfo::exists(destPath + QStringLiteral("/vx.json")),
           "Root-level legacy vx.json should not exist in destination user content");
}

void TestVNote3MigrationService::testConvertExcludesVxRecycleBin() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("excl_recycle_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // vx_recycle_bin directory and its contents must NOT exist.
  QVERIFY(!pathExistsInNotebook(destPath, QStringLiteral("vx_recycle_bin")));
  QVERIFY(!pathExistsInNotebook(destPath, QStringLiteral("vx_recycle_bin/deleted.md")));
}

void TestVNote3MigrationService::testConvertPreservesVxImages() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("preserve_images_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // vx_images folder and its content should exist.
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("vx_images/diagram.png")));
}

void TestVNote3MigrationService::testConvertPreservesVxAttachments() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("preserve_attach_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // vx_attachments folder and its content should exist.
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("vx_attachments/doc.pdf")));
}

void TestVNote3MigrationService::testConvertInvalidSourceFails() {
  TempDirFixture emptyDir;
  QVERIFY(emptyDir.isValid());

  QString destPath = m_tempDir.filePath(QStringLiteral("invalid_src_dest"));
  auto result = m_service->convertNotebook(emptyDir.path(), destPath);
  QVERIFY(!result.success);
  QVERIFY(result.errorMessage.contains(QStringLiteral("inspection failed")));
}

// --- Comprehensive conversion verification ---

void TestVNote3MigrationService::testConvertPreservesStructureAndContent() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  createSyntheticVNote3Source(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("struct_content_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.destinationPath, destPath);

  // --- Preserved user content files ---
  QVERIFY2(pathExistsInNotebook(destPath, QStringLiteral("notes/hello.md")),
           "notes/hello.md should exist in destination");
  QVERIFY2(pathExistsInNotebook(destPath, QStringLiteral("notes/subfolder/world.md")),
           "notes/subfolder/world.md should exist in destination");

  // vx_images and vx_attachments preserved as ordinary content folders.
  QVERIFY2(pathExistsInNotebook(destPath, QStringLiteral("vx_images/photo.png")),
           "vx_images/photo.png should be preserved as ordinary content");
  QVERIFY2(pathExistsInNotebook(destPath, QStringLiteral("vx_attachments/doc.pdf")),
           "vx_attachments/doc.pdf should be preserved as ordinary content");

  // --- Excluded legacy metadata artifacts ---
  // vx_notebook/ as user content must not exist (vxcore creates its own metadata there).
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral("vx_notebook/vx_notebook.json")),
           "Legacy vx_notebook/vx_notebook.json should not be imported as user content");
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral("vx_notebook/notebook.db")),
           "Legacy vx_notebook/notebook.db should not be imported as user content");

  // Legacy vx.json files should not be copied as user content.
  // Note: vxcore creates its own folder config files (also named vx.json)
  // inside managed folders, so we only verify that the root-level legacy
  // vx.json is absent from user content (outside vx_notebook/).
  QVERIFY2(!QFileInfo::exists(destPath + QStringLiteral("/vx.json")),
           "Root-level legacy vx.json should not exist in destination user content");

  // vx_recycle_bin excluded entirely.
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral("vx_recycle_bin")),
           "vx_recycle_bin/ directory should not exist in destination");
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral("vx_recycle_bin/deleted.md")),
           "vx_recycle_bin/deleted.md should not exist in destination");
}

void TestVNote3MigrationService::testConvertPreservesNotebookNameAndDescription() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  createSyntheticVNote3Source(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("name_desc_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Result should carry the name from source.
  QCOMPARE(result.notebookName, QStringLiteral("SyntheticNB"));

  // Open destination notebook to query config via NotebookCoreService.
  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook for config verification");

  QJsonObject config = m_notebookService->getNotebookConfig(nbId);
  QVERIFY(!config.isEmpty());
  QCOMPARE(config[QStringLiteral("name")].toString(), QStringLiteral("SyntheticNB"));
  QCOMPARE(config[QStringLiteral("description")].toString(), QStringLiteral("SyntheticDesc"));

  // Close notebook after assertions.
  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertSourceImmutable() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  createSyntheticVNote3Source(sourceDir.path());

  // Snapshot all files in source tree before conversion.
  QMap<QString, QByteArray> originalFiles;
  QDirIterator it(sourceDir.path(), QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    QFile f(it.filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    originalFiles[it.filePath()] = f.readAll();
    f.close();
  }
  QVERIFY(!originalFiles.isEmpty());

  // Perform successful conversion.
  QString destPath = m_tempDir.filePath(QStringLiteral("immutable_success_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Verify every original file still exists with identical content.
  for (auto jt = originalFiles.constBegin(); jt != originalFiles.constEnd(); ++jt) {
    QVERIFY2(QFile::exists(jt.key()),
             qPrintable(QStringLiteral("Source file missing after conversion: %1").arg(jt.key())));
    QFile f(jt.key());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), jt.value());
    f.close();
  }

  // Verify no extra files were added to source.
  QDirIterator it2(sourceDir.path(), QDir::Files, QDirIterator::Subdirectories);
  int count = 0;
  while (it2.hasNext()) {
    it2.next();
    count++;
  }
  QCOMPARE(count, originalFiles.size());
}

// --- Rollback scenarios ---

void TestVNote3MigrationService::testRollbackOnForcedFailure() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  QString destPath = m_tempDir.filePath(QStringLiteral("rollback_dest"));

  // Activate failpoint — force failure right after notebook creation.
  vnote3MigrationService_setFailAfterCreate(true);

  auto result = m_service->convertNotebook(sourceDir.path(), destPath);

  // Reset failpoint immediately.
  vnote3MigrationService_setFailAfterCreate(false);

  // Conversion must report failure.
  QVERIFY(!result.success);

  // Destination directory must have been cleaned up.
  QVERIFY2(
      !QDir(destPath).exists(),
      qPrintable(
          QStringLiteral("Destination dir should not exist after rollback: %1").arg(destPath)));
}

void TestVNote3MigrationService::testSourceImmutableAfterFailure() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  buildSyntheticSourceTree(sourceDir.path());

  // Record all files in source tree before conversion.
  QMap<QString, QByteArray> originalFiles;
  QDirIterator it(sourceDir.path(), QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    QFile f(it.filePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    originalFiles[it.filePath()] = f.readAll();
    f.close();
  }
  QVERIFY(!originalFiles.isEmpty());

  QString destPath = m_tempDir.filePath(QStringLiteral("immutable_dest"));

  // Activate failpoint.
  vnote3MigrationService_setFailAfterCreate(true);

  m_service->convertNotebook(sourceDir.path(), destPath);

  // Reset failpoint immediately.
  vnote3MigrationService_setFailAfterCreate(false);

  // Verify every original file still exists with identical content.
  for (auto jt = originalFiles.constBegin(); jt != originalFiles.constEnd(); ++jt) {
    QVERIFY2(QFile::exists(jt.key()),
             qPrintable(QStringLiteral("Source file missing after failure: %1").arg(jt.key())));
    QFile f(jt.key());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), jt.value());
    f.close();
  }

  // Verify no extra files were added to source.
  QDirIterator it2(sourceDir.path(), QDir::Files, QDirIterator::Subdirectories);
  int count = 0;
  while (it2.hasNext()) {
    it2.next();
    count++;
  }
  QCOMPARE(count, originalFiles.size());
}

// --- T12: Metadata round-trip tests ---

void TestVNote3MigrationService::testConvertPreservesTags() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("tagged.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Tagged note");
    f.close();
  }

  QJsonArray tagsArr;
  tagsArr.append(QStringLiteral("work"));
  tagsArr.append(QStringLiteral("important"));
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("tagged.md");
  fileEntry[QStringLiteral("tags")] = tagsArr;
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("tags_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject fileInfo =
      m_notebookService->getFileInfo(nbId, QStringLiteral("tagged.md"));
  QVERIFY2(!fileInfo.isEmpty(), "getFileInfo returned empty for tagged.md");

  QJsonArray resultTags = fileInfo[QStringLiteral("tags")].toArray();
  QStringList tagList;
  for (const QJsonValue &v : resultTags) {
    tagList.append(v.toString());
  }
  QVERIFY2(tagList.contains(QStringLiteral("work")),
           qPrintable(QStringLiteral("Tag 'work' missing. Got: %1")
                          .arg(tagList.join(QStringLiteral(", ")))));
  QVERIFY2(tagList.contains(QStringLiteral("important")),
           qPrintable(QStringLiteral("Tag 'important' missing. Got: %1")
                          .arg(tagList.join(QStringLiteral(", ")))));

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertPreservesTimestamps() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("timed.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Timed note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("timed.md");
  fileEntry[QStringLiteral("created_time")] =
      QStringLiteral("2024-01-15T10:30:00Z");
  fileEntry[QStringLiteral("modified_time")] =
      QStringLiteral("2024-06-20T14:45:00Z");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("timestamps_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject fileInfo =
      m_notebookService->getFileInfo(nbId, QStringLiteral("timed.md"));
  QVERIFY2(!fileInfo.isEmpty(), "getFileInfo returned empty for timed.md");

  const qint64 expectedCreated =
      QDateTime::fromString(QStringLiteral("2024-01-15T10:30:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();
  const qint64 expectedModified =
      QDateTime::fromString(QStringLiteral("2024-06-20T14:45:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();

  const qint64 actualCreated =
      fileInfo[QStringLiteral("createdUtc")].toVariant().toLongLong();
  const qint64 actualModified =
      fileInfo[QStringLiteral("modifiedUtc")].toVariant().toLongLong();

  QCOMPARE(actualCreated, expectedCreated);
  QCOMPARE(actualModified, expectedModified);

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertPreservesVisualMetadata() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("colored.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Colored note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("colored.md");
  fileEntry[QStringLiteral("name_color")] = QStringLiteral("#FF0000");
  fileEntry[QStringLiteral("background_color")] = QStringLiteral("#00FF00");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("visual_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject metadata =
      m_notebookService->getFileMetadata(nbId, QStringLiteral("colored.md"));
  QVERIFY2(!metadata.isEmpty(),
           "getFileMetadata returned empty for colored.md");
  QCOMPARE(metadata[QStringLiteral("textColor")].toString(),
           QStringLiteral("#FF0000"));
  QCOMPARE(metadata[QStringLiteral("backgroundColor")].toString(),
           QStringLiteral("#00FF00"));

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertMigratesAttachments() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("noted.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Note with attachments");
    f.close();
  }

  // Source attachment dir: vx_attachments/434314329027059/report.pdf
  base.mkpath(QStringLiteral("vx_attachments/434314329027059"));
  {
    QFile f(base.filePath(
        QStringLiteral("vx_attachments/434314329027059/report.pdf")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake pdf data for attachment test");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("noted.md");
  fileEntry[QStringLiteral("attachment_folder")] =
      QStringLiteral("434314329027059");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("attach_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  // Verify attachment listed in metadata.
  QJsonArray attachments =
      m_notebookService->listAttachments(nbId, QStringLiteral("noted.md"));
  QStringList attachNames;
  for (const QJsonValue &v : attachments) {
    attachNames.append(v.toString());
  }
  QVERIFY2(
      attachNames.contains(QStringLiteral("report.pdf")),
      qPrintable(QStringLiteral("Attachment 'report.pdf' missing. Got: %1")
                     .arg(attachNames.join(QStringLiteral(", ")))));

  // Verify file exists on disk in dest.
  QString attachDir = m_notebookService->getAttachmentsFolder(
      nbId, QStringLiteral("noted.md"));
  QVERIFY2(!attachDir.isEmpty(), "getAttachmentsFolder returned empty");
  QVERIFY2(QFile::exists(attachDir + QStringLiteral("/report.pdf")),
           qPrintable(QStringLiteral("report.pdf not on disk at: %1")
                          .arg(attachDir)));

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertPreservesFolderTimestamps() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  base.mkpath(QStringLiteral("docs"));
  {
    QFile f(base.filePath(QStringLiteral("docs/readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Docs readme");
    f.close();
  }

  // Root vx.json lists "docs" folder.
  QJsonObject folderEntry;
  folderEntry[QStringLiteral("name")] = QStringLiteral("docs");
  QJsonArray foldersArr;
  foldersArr.append(folderEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = QJsonArray();
  rootVx[QStringLiteral("folders")] = foldersArr;
  writeVxJson(sourceDir.path(), rootVx);

  // docs/vx.json has folder-level timestamps.
  QJsonObject docsVx;
  docsVx[QStringLiteral("created_time")] =
      QStringLiteral("2023-03-10T08:00:00Z");
  docsVx[QStringLiteral("modified_time")] =
      QStringLiteral("2025-01-05T16:30:00Z");
  docsVx[QStringLiteral("files")] = QJsonArray();
  docsVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(base.filePath(QStringLiteral("docs")), docsVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("folder_ts_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject folderConfig =
      m_notebookService->getFolderConfig(nbId, QStringLiteral("docs"));
  QVERIFY2(!folderConfig.isEmpty(),
           "getFolderConfig returned empty for docs");

  const qint64 expectedCreated =
      QDateTime::fromString(QStringLiteral("2023-03-10T08:00:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();

  const qint64 actualCreated =
      folderConfig[QStringLiteral("createdUtc")].toVariant().toLongLong();
  const qint64 actualModified =
      folderConfig[QStringLiteral("modifiedUtc")].toVariant().toLongLong();

  // createdUtc is preserved from VNote 3 source.
  QCOMPARE(actualCreated, expectedCreated);

  // modifiedUtc is preserved from VNote 3 source.
  const qint64 expectedModified =
      QDateTime::fromString(QStringLiteral("2025-01-05T16:30:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();
  QVERIFY2(actualModified > 0, "Folder modifiedUtc should be non-zero");
  QCOMPARE(actualModified, expectedModified);

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertRealXpayNotebook() {
  const QString xpayPath = QStringLiteral(
      "C:/Users/tanle/OneDrive - Microsoft/notebooks/xpay");
  if (!QDir(xpayPath).exists()) {
    QSKIP("Real xpay notebook not found");
  }

  QString destPath = m_tempDir.filePath(QStringLiteral("xpay_dest"));
  auto result = m_service->convertNotebook(xpayPath, destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted xpay notebook");

  // Verify notebook has content.
  QJsonObject children =
      m_notebookService->listFolderChildren(nbId, QString());
  QJsonArray files = children[QStringLiteral("files")].toArray();
  QJsonArray folders = children[QStringLiteral("folders")].toArray();
  QVERIFY2(files.size() + folders.size() > 0,
           "Converted xpay notebook has no content at root");

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

// --- T13: Edge case tests ---

void TestVNote3MigrationService::testConvertMissingVxJsonGraceful() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // Subfolder with a file but NO vx.json inside.
  base.mkpath(QStringLiteral("nojson"));
  {
    QFile f(base.filePath(QStringLiteral("nojson/inner.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Inner note without vx.json");
    f.close();
  }

  // Root vx.json lists "nojson" as a folder.
  QJsonObject folderEntry;
  folderEntry[QStringLiteral("name")] = QStringLiteral("nojson");
  QJsonArray foldersArr;
  foldersArr.append(folderEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = QJsonArray();
  rootVx[QStringLiteral("folders")] = foldersArr;
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("nojson_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Folder is created because root vx.json lists it.
  QVERIFY2(QDir(destPath + QStringLiteral("/nojson")).exists() ||
               pathExistsInNotebook(destPath, QStringLiteral("nojson")),
           "Folder 'nojson' should be created");

  // inner.md is copied by supplementary copy (all files are preserved).
  QVERIFY2(pathExistsInNotebook(destPath, QStringLiteral("nojson/inner.md")),
           "inner.md should be copied by supplementary copy");
}

void TestVNote3MigrationService::testConvertEmptyTagsSkipped() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("notags.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# No tags note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("notags.md");
  fileEntry[QStringLiteral("tags")] = QJsonArray(); // Empty array.
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("emptytags_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // No tag-related warnings.
  for (const QString &w : result.warnings) {
    QVERIFY2(!w.contains(QStringLiteral("tag"), Qt::CaseInsensitive),
             qPrintable(
                 QStringLiteral("Unexpected tag warning: %1").arg(w)));
  }

  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("notags.md")));
}

void TestVNote3MigrationService::testConvertInvalidTimestampDegrades() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("badtime.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Bad timestamp note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("badtime.md");
  fileEntry[QStringLiteral("created_time")] = QStringLiteral("not-a-date");
  fileEntry[QStringLiteral("modified_time")] =
      QStringLiteral("2024-06-20T14:45:00Z");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("badtime_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Should have a warning about malformed created_time.
  bool foundWarning = false;
  for (const QString &w : result.warnings) {
    if (w.contains(QStringLiteral("malformed")) &&
        w.contains(QStringLiteral("created_time"))) {
      foundWarning = true;
      break;
    }
  }
  QVERIFY2(foundWarning,
           "Expected warning about malformed created_time");

  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("badtime.md")));
}

void TestVNote3MigrationService::testConvertEmptyAttachmentFolderSkipped() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("noattach.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# No attachment note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("noattach.md");
  fileEntry[QStringLiteral("attachment_folder")] = QStringLiteral("");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("emptyattach_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // No attachment-related warnings.
  for (const QString &w : result.warnings) {
    QVERIFY2(
        !w.contains(QStringLiteral("attachment"), Qt::CaseInsensitive),
        qPrintable(
            QStringLiteral("Unexpected attachment warning: %1").arg(w)));
  }

  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("noattach.md")));
}

void TestVNote3MigrationService::testConvertMissingAttachmentDirDegrades() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("missingattach.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Missing attachment dir note");
    f.close();
  }

  // References attachment_folder but directory doesn't exist on disk.
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("missingattach.md");
  fileEntry[QStringLiteral("attachment_folder")] = QStringLiteral("99999");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("missingattachdir_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Warning about missing attachment source dir.
  bool foundWarning = false;
  for (const QString &w : result.warnings) {
    if (w.contains(QStringLiteral("attachment")) &&
        w.contains(QStringLiteral("not found"))) {
      foundWarning = true;
      break;
    }
  }
  QVERIFY2(foundWarning,
           "Expected warning about missing attachment source directory");

  QVERIFY(
      pathExistsInNotebook(destPath, QStringLiteral("missingattach.md")));
}

void TestVNote3MigrationService::testConvertVxImagesAtMultipleLevels() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // Root-level vx_images.
  base.mkpath(QStringLiteral("vx_images"));
  {
    QFile f(base.filePath(QStringLiteral("vx_images/root_img.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("root image data");
    f.close();
  }

  // Subfolder with its own vx_images.
  base.mkpath(QStringLiteral("sub/vx_images"));
  {
    QFile f(base.filePath(QStringLiteral("sub/vx_images/sub_img.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("sub image data");
    f.close();
  }
  {
    QFile f(base.filePath(QStringLiteral("sub/note.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Sub note");
    f.close();
  }

  // Root vx.json lists "sub" folder.
  QJsonObject folderEntry;
  folderEntry[QStringLiteral("name")] = QStringLiteral("sub");
  QJsonArray foldersArr;
  foldersArr.append(folderEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = QJsonArray();
  rootVx[QStringLiteral("folders")] = foldersArr;
  writeVxJson(sourceDir.path(), rootVx);

  // sub/vx.json lists note.md.
  QJsonArray subFiles;
  QJsonObject noteEntry;
  noteEntry[QStringLiteral("name")] = QStringLiteral("note.md");
  subFiles.append(noteEntry);
  QJsonObject subVx;
  subVx[QStringLiteral("files")] = subFiles;
  subVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(base.filePath(QStringLiteral("sub")), subVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("multilevel_images_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QVERIFY2(pathExistsInNotebook(
               destPath, QStringLiteral("vx_images/root_img.png")),
           "Root-level vx_images/root_img.png should be copied");
  QVERIFY2(pathExistsInNotebook(
               destPath, QStringLiteral("sub/vx_images/sub_img.png")),
           "Subfolder vx_images/sub_img.png should be copied");
}

void TestVNote3MigrationService::testConvertDegradedWarningsCollected() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // File 1: bad timestamps.
  {
    QFile f(base.filePath(QStringLiteral("bad_ts.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Bad timestamp");
    f.close();
  }

  // File 2: missing attachment dir.
  {
    QFile f(base.filePath(QStringLiteral("bad_attach.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Missing attachment dir");
    f.close();
  }

  QJsonObject file1;
  file1[QStringLiteral("name")] = QStringLiteral("bad_ts.md");
  file1[QStringLiteral("created_time")] = QStringLiteral("garbage-date");
  file1[QStringLiteral("modified_time")] = QStringLiteral("also-garbage");

  QJsonObject file2;
  file2[QStringLiteral("name")] = QStringLiteral("bad_attach.md");
  file2[QStringLiteral("attachment_folder")] =
      QStringLiteral("nonexistent_777");

  QJsonArray filesArr;
  filesArr.append(file1);
  filesArr.append(file2);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("degraded_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Multiple warnings should be collected.
  QVERIFY2(
      result.warnings.size() >= 3,
      qPrintable(QStringLiteral("Expected >= 3 warnings, got %1: %2")
                     .arg(result.warnings.size())
                     .arg(result.warnings.join(QStringLiteral("; ")))));

  bool hasTimestampWarning = false;
  bool hasAttachmentWarning = false;
  for (const QString &w : result.warnings) {
    if (w.contains(QStringLiteral("malformed"))) {
      hasTimestampWarning = true;
    }
    if (w.contains(QStringLiteral("attachment")) &&
        w.contains(QStringLiteral("not found"))) {
      hasAttachmentWarning = true;
    }
  }
  QVERIFY2(hasTimestampWarning,
           "Expected at least one timestamp malformed warning");
  QVERIFY2(hasAttachmentWarning,
           "Expected at least one attachment missing warning");

  // Both files still imported despite warnings.
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("bad_ts.md")));
  QVERIFY(
      pathExistsInNotebook(destPath, QStringLiteral("bad_attach.md")));
}

void TestVNote3MigrationService::testConvertGitDirectoryNotImported() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  writeNotebookConfig(sourceDir.path(), makeConfigJson());

  // Create a .git directory with content.
  base.mkpath(QStringLiteral(".git"));
  {
    QFile f(base.filePath(QStringLiteral(".git/config")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[core]\n\trepositoryformatversion = 0\n");
    f.close();
  }

  // Create a regular file listed in vx.json.
  {
    QFile f(base.filePath(QStringLiteral("readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Readme");
    f.close();
  }

  // Root vx.json lists only readme.md — .git is NOT listed.
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("readme.md");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("git_excluded_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // .git directory and its contents must NOT exist in destination.
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral(".git")),
           ".git directory should not be imported");
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral(".git/config")),
           ".git/config should not be imported");

  // Regular file should exist.
  QVERIFY(pathExistsInNotebook(destPath, QStringLiteral("readme.md")));
}

void TestVNote3MigrationService::testProgressSignalEmitted() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  writeNotebookConfig(sourceDir.path(), makeConfigJson());

  // Create 2 files and 1 folder = 3 import items.
  base.mkpath(QStringLiteral("notes"));
  {
    QFile f(base.filePath(QStringLiteral("readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Readme");
    f.close();
  }
  {
    QFile f(base.filePath(QStringLiteral("notes/hello.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Hello");
    f.close();
  }

  // Root vx.json: 1 file + 1 folder = 2 items at root level.
  QJsonObject readmeEntry;
  readmeEntry[QStringLiteral("name")] = QStringLiteral("readme.md");
  QJsonArray rootFiles;
  rootFiles.append(readmeEntry);
  QJsonObject notesFolder;
  notesFolder[QStringLiteral("name")] = QStringLiteral("notes");
  QJsonArray rootFolders;
  rootFolders.append(notesFolder);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = rootFiles;
  rootVx[QStringLiteral("folders")] = rootFolders;
  writeVxJson(sourceDir.path(), rootVx);

  // notes/vx.json: 1 file = 1 item. Total: 2 + 1 = 3 items.
  QJsonObject helloEntry;
  helloEntry[QStringLiteral("name")] = QStringLiteral("hello.md");
  QJsonArray notesFiles;
  notesFiles.append(helloEntry);
  QJsonObject notesVx;
  notesVx[QStringLiteral("files")] = notesFiles;
  notesVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(base.filePath(QStringLiteral("notes")), notesVx);

  // Connect to progressUpdated signal.
  QVector<std::tuple<int, int, QString>> emissions;
  connect(m_service, &VNote3MigrationService::progressUpdated,
          [&emissions](int p_val, int p_maximum, const QString &p_message) {
            emissions.append(std::make_tuple(p_val, p_maximum, p_message));
          });

  QString destPath = m_tempDir.filePath(QStringLiteral("progress_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Disconnect to avoid stale connections in other tests.
  disconnect(m_service, &VNote3MigrationService::progressUpdated, nullptr, nullptr);

  // Should have 3 emissions (1 file + 1 folder + 1 file in subfolder).
  QVERIFY2(emissions.size() == 3,
           qPrintable(QStringLiteral("Expected 3 progress emissions, got %1")
                          .arg(emissions.size())));

  // All emissions should have maximum = 3.
  for (const auto &e : emissions) {
    QCOMPARE(std::get<1>(e), 3);
  }

  // Values should be monotonically increasing: 1, 2, 3.
  for (int i = 0; i < emissions.size(); ++i) {
    QCOMPARE(std::get<0>(emissions[i]), i + 1);
  }

  // Final value should equal maximum.
  QCOMPARE(std::get<0>(emissions.last()), 3);
}

// --- T-TagGraph: Tag graph migration tests ---

void TestVNote3MigrationService::testConvertPreservesTagGraph() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  cfgOvr[QStringLiteral("tag_graph")] = QStringLiteral("parent>child");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("note.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Note with hierarchy tags");
    f.close();
  }

  QJsonArray tagsArr;
  tagsArr.append(QStringLiteral("parent"));
  tagsArr.append(QStringLiteral("child"));
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("note.md");
  fileEntry[QStringLiteral("tags")] = tagsArr;
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("taggraph_happy_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool foundChild = false;
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    if (tagObj[QStringLiteral("name")].toString() == QStringLiteral("child")) {
      foundChild = true;
      QCOMPARE(tagObj[QStringLiteral("parent")].toString(),
               QStringLiteral("parent"));
    }
  }
  QVERIFY2(foundChild, "Tag 'child' not found in listTags output");

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertEmptyTagGraphNoOp() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  // No tagGraph key in config.
  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("flat.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Flat tags note");
    f.close();
  }

  QJsonArray tagsArr;
  tagsArr.append(QStringLiteral("alpha"));
  tagsArr.append(QStringLiteral("beta"));
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("flat.md");
  fileEntry[QStringLiteral("tags")] = tagsArr;
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("taggraph_empty_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  // All tags should be root-level (no parent).
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    QString name = tagObj[QStringLiteral("name")].toString();
    if (name == QStringLiteral("alpha") || name == QStringLiteral("beta")) {
      QVERIFY2(!tagObj.contains(QStringLiteral("parent")) ||
                   tagObj[QStringLiteral("parent")].toString().isEmpty(),
               qPrintable(QStringLiteral("Tag '%1' unexpectedly has parent '%2'")
                              .arg(name, tagObj[QStringLiteral("parent")].toString())));
    }
  }

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertTagGraphOrphanTagsDegrades() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  cfgOvr[QStringLiteral("tag_graph")] = QStringLiteral("ghost1>ghost2");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // File with no tags referencing ghost1/ghost2.
  {
    QFile f(base.filePath(QStringLiteral("unrelated.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Unrelated note");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("unrelated.md");
  fileEntry[QStringLiteral("tags")] = QJsonArray();
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("taggraph_orphan_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  // Both orphan tags should be pre-created with correct hierarchy.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool foundGhost1 = false;
  bool foundGhost2 = false;
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    QString name = tagObj[QStringLiteral("name")].toString();
    if (name == QStringLiteral("ghost1")) {
      foundGhost1 = true;
    }
    if (name == QStringLiteral("ghost2")) {
      foundGhost2 = true;
      QCOMPARE(tagObj[QStringLiteral("parent")].toString(),
               QStringLiteral("ghost1"));
    }
  }
  QVERIFY2(foundGhost1, "Orphan tag 'ghost1' not pre-created");
  QVERIFY2(foundGhost2, "Orphan tag 'ghost2' not pre-created");

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertMalformedTagGraphPairsSkipped() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  cfgOvr[QStringLiteral("tag_graph")] = QStringLiteral("valid>child;;>bad;also>;good>pair");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("tagged2.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Note with mixed tags");
    f.close();
  }

  QJsonArray tagsArr;
  tagsArr.append(QStringLiteral("valid"));
  tagsArr.append(QStringLiteral("child"));
  tagsArr.append(QStringLiteral("good"));
  tagsArr.append(QStringLiteral("pair"));
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("tagged2.md");
  fileEntry[QStringLiteral("tags")] = tagsArr;
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("taggraph_malformed_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  // Valid pairs: valid>child and good>pair should be applied.
  QJsonArray tags = m_tagCoreService->listTags(nbId);
  bool foundChild = false;
  bool foundPair = false;
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    QString name = tagObj[QStringLiteral("name")].toString();
    if (name == QStringLiteral("child")) {
      foundChild = true;
      QCOMPARE(tagObj[QStringLiteral("parent")].toString(),
               QStringLiteral("valid"));
    }
    if (name == QStringLiteral("pair")) {
      foundPair = true;
      QCOMPARE(tagObj[QStringLiteral("parent")].toString(),
               QStringLiteral("good"));
    }
  }
  QVERIFY2(foundChild, "Tag 'child' not found with parent 'valid'");
  QVERIFY2(foundPair, "Tag 'pair' not found with parent 'good'");

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertPreservesMultiLevelTagGraph() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  cfgOvr[QStringLiteral("tag_graph")] =
      QStringLiteral("tag1>tag11;tag11>tag111;tag5>tag55;tag55>ok");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  {
    QFile f(base.filePath(QStringLiteral("multilevel.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Multi-level tag hierarchy note");
    f.close();
  }

  QJsonArray tagsArr;
  tagsArr.append(QStringLiteral("tag1"));
  tagsArr.append(QStringLiteral("tag11"));
  tagsArr.append(QStringLiteral("tag111"));
  tagsArr.append(QStringLiteral("tag5"));
  tagsArr.append(QStringLiteral("tag55"));
  tagsArr.append(QStringLiteral("ok"));
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("multilevel.md");
  fileEntry[QStringLiteral("tags")] = tagsArr;
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("taggraph_multilevel_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonArray tags = m_tagCoreService->listTags(nbId);

  // Build a name->parent map for easy verification.
  QHash<QString, QString> parentMap;
  for (const auto &tagVal : tags) {
    QJsonObject tagObj = tagVal.toObject();
    QString name = tagObj[QStringLiteral("name")].toString();
    QString parent = tagObj[QStringLiteral("parent")].toString();
    parentMap[name] = parent;
  }

  // Root-level tags: tag1 and tag5 have no parent.
  QVERIFY2(parentMap.contains(QStringLiteral("tag1")), "tag1 not found");
  QVERIFY2(parentMap[QStringLiteral("tag1")].isEmpty(),
            "tag1 should be root-level (no parent)");

  QVERIFY2(parentMap.contains(QStringLiteral("tag5")), "tag5 not found");
  QVERIFY2(parentMap[QStringLiteral("tag5")].isEmpty(),
            "tag5 should be root-level (no parent)");

  // Level 2: tag11 -> parent tag1, tag55 -> parent tag5.
  QVERIFY2(parentMap.contains(QStringLiteral("tag11")), "tag11 not found");
  QCOMPARE(parentMap[QStringLiteral("tag11")], QStringLiteral("tag1"));

  QVERIFY2(parentMap.contains(QStringLiteral("tag55")), "tag55 not found");
  QCOMPARE(parentMap[QStringLiteral("tag55")], QStringLiteral("tag5"));

  // Level 3: tag111 -> parent tag11, ok -> parent tag55.
  QVERIFY2(parentMap.contains(QStringLiteral("tag111")), "tag111 not found");
  QCOMPARE(parentMap[QStringLiteral("tag111")], QStringLiteral("tag11"));

  QVERIFY2(parentMap.contains(QStringLiteral("ok")), "ok not found");
  QCOMPARE(parentMap[QStringLiteral("ok")], QStringLiteral("tag55"));

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertFolderWith17FilesAndSubfolder() {
  // Regression test: real-world VNote3 notebook with 17 files (Chinese filenames),
  // 3 files with attachment_folder, and 1 subfolder.
  // Uses on-disk fixture from tests/data/ instead of hardcoded JSON.
  const QString fixturePath =
      findFixture(QStringLiteral("../data/vnote3_notebooks/database_notebook"));
  if (fixturePath.isEmpty()) {
    QSKIP("Test fixture 'database_notebook' not found");
  }

  // Copy fixture to a temp dir so the source tree stays clean.
  TempDirFixture workDir;
  QVERIFY(workDir.isValid());
  const QString sourcePath = workDir.copyFrom(fixturePath, QStringLiteral("source"));
  QVERIFY2(QDir(sourcePath).exists(), "Failed to copy fixture");

  // Run conversion.
  const QString destPath = m_tempDir.filePath(QStringLiteral("legacy17_dest"));
  auto result = m_service->convertNotebook(sourcePath, destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Verify all 17 files are migrated on disk.
  const QStringList expectedFiles = {
      QStringLiteral("cache.md"),
      QStringLiteral("DB2.md"),
      QStringLiteral("hbase.md"),
      QStringLiteral("hive.md"),
      QStringLiteral("mysql.md"),
      QStringLiteral("oracle.md"),
      QStringLiteral("sqlserver.md"),
      // Chinese filenames — use UTF-8 escape sequences for portability.
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe9\x85\x8d\xe7\xbd\xae.md"),           // 数据库配置.md
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xa4\x84\xe7\x90\x86.md"),                       // 数据处理.md
      QStringLiteral("\xe5\x9b\xbd\xe4\xba\xa7\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93.md"),           // 国产数据库.md
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe7\x8e\xaf\xe5\xa2\x83\xe8\xae\xb0\xe5\xbd\x95.md"), // 数据库环境记录.md
      QStringLiteral("SQL\xe8\xaf\xad\xe6\xb3\x95.md"),                                             // SQL语法.md
      QStringLiteral("\xe7\xbb\x91\xe5\xae\x9a\xe5\x8f\x98\xe9\x87\x8f.md"),                       // 绑定变量.md
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe7\xac\x94\xe8\xae\xb0.md"),           // 数据库笔记.md
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe5\x9f\xba\xe7\xa1\x80.md"),           // 数据库基础.md
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe5\xae\xa1\xe8\xae\xa1\xe8\xaf\x84\xe6\xb5\x8b.md"), // 数据库审计评测.md
      QStringLiteral("\xe8\xa7\x92\xe8\x89\xb2\xe4\xb8\x8e\xe6\x9d\x83\xe9\x99\x90.md"),           // 角色与权限.md
  };

  for (const QString &name : expectedFiles) {
    QVERIFY2(pathExistsInNotebook(destPath, name),
             qPrintable(QStringLiteral("File not migrated: %1").arg(name)));
  }

  // Open notebook and verify via NotebookCoreService.
  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject children = m_notebookService->listFolderChildren(nbId, QString());
  QJsonArray files = children[QStringLiteral("files")].toArray();
  QJsonArray folders = children[QStringLiteral("folders")].toArray();

  // Should have exactly 17 files and 1 folder at root.
  QCOMPARE(files.size(), 17);
  QCOMPARE(folders.size(), 1);

  // Verify the subfolder name.
  const QString expectedSubfolder =
      QStringLiteral("\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe9\x83\xa8\xe7\xbd\xb2"); // 数据库部署
  QCOMPARE(folders[0].toObject()[QStringLiteral("name")].toString(), expectedSubfolder);

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testConvertMigratesAttachmentsInChineseFolder() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  // Use UTF-8 escape sequences for Chinese folder name (养生)
  const QString chineseFolder = QStringLiteral("\xe5\x85\xbb\xe7\x94\x9f");

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // Create the Chinese-named subfolder and a .md file inside it.
  base.mkpath(chineseFolder);
  {
    QFile f(base.filePath(chineseFolder + QStringLiteral("/noted.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Note with attachments in Chinese folder");
    f.close();
  }

  // Source attachment dir inside the Chinese folder:
  // 养生/vx_attachments/434314329027059/report.pdf
  base.mkpath(chineseFolder + QStringLiteral("/vx_attachments/434314329027059"));
  {
    QFile f(base.filePath(
        chineseFolder + QStringLiteral("/vx_attachments/434314329027059/report.pdf")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake pdf data for Chinese folder attachment test");
    f.close();
  }

  // Root vx.json lists the Chinese folder
  QJsonObject folderEntry;
  folderEntry[QStringLiteral("name")] = chineseFolder;
  QJsonArray foldersArr;
  foldersArr.append(folderEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = QJsonArray();
  rootVx[QStringLiteral("folders")] = foldersArr;
  writeVxJson(sourceDir.path(), rootVx);

  // Chinese folder's vx.json lists the file with attachment_folder
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("noted.md");
  fileEntry[QStringLiteral("attachment_folder")] = QStringLiteral("434314329027059");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject subVx;
  subVx[QStringLiteral("files")] = filesArr;
  subVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(base.filePath(chineseFolder), subVx);

  QString destPath = m_tempDir.filePath(QStringLiteral("chinese_attach_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  // Build the relative file path as the migration service would
  const QString relFilePath = chineseFolder + QStringLiteral("/noted.md");

  // Verify attachment listed in metadata.
  QJsonArray attachments = m_notebookService->listAttachments(nbId, relFilePath);
  QStringList attachNames;
  for (const QJsonValue &v : attachments) {
    attachNames.append(v.toString());
  }
  QVERIFY2(
      attachNames.contains(QStringLiteral("report.pdf")),
      qPrintable(QStringLiteral("Attachment 'report.pdf' missing. Got: %1")
                     .arg(attachNames.join(QStringLiteral(", ")))));

  // Verify the attachment folder path contains the Chinese folder name (not garbled).
  QString attachDir = m_notebookService->getAttachmentsFolder(nbId, relFilePath);
  QVERIFY2(!attachDir.isEmpty(), "getAttachmentsFolder returned empty");
  QVERIFY2(attachDir.contains(chineseFolder),
           qPrintable(QStringLiteral(
               "Attachment dir should contain Chinese folder name '%1' but got: %2")
                          .arg(chineseFolder, attachDir)));

  // Verify file exists on disk in dest.
  QVERIFY2(QFile::exists(attachDir + QStringLiteral("/report.pdf")),
           qPrintable(QStringLiteral("report.pdf not on disk at: %1").arg(attachDir)));

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

void TestVNote3MigrationService::testCopyRemainingCopiesUnindexedImageFolder() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  // Config with empty image_folder override (so _v_images is NOT the configured folder).
  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // Create a regular file listed in vx.json (valid notebook).
  {
    QFile f(base.filePath(QStringLiteral("note.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Note");
    f.close();
  }

  // Create _v_images folder with a file NOT listed in vx.json.
  base.mkpath(QStringLiteral("_v_images"));
  {
    QFile f(base.filePath(QStringLiteral("_v_images/photo.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png data");
    f.close();
  }

  // Root vx.json lists only note.md — _v_images is NOT listed.
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("note.md");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("copy_remaining_images_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Supplementary copy should have copied the unindexed _v_images folder.
  QVERIFY2(pathExistsInNotebook(
               destPath, QStringLiteral("_v_images/photo.png")),
           "Unindexed _v_images/photo.png should be copied by supplementary copy");
}

void TestVNote3MigrationService::testCopyRemainingCopiesUnindexedFiles() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  writeNotebookConfig(sourceDir.path(), makeConfigJson());

  // Create a file listed in vx.json.
  {
    QFile f(base.filePath(QStringLiteral("indexed.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Indexed");
    f.close();
  }

  // Create an unindexed file NOT in vx.json.
  {
    QFile f(base.filePath(QStringLiteral("random_notes.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("some random notes");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("indexed.md");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("copy_remaining_files_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Supplementary copy should have copied the unindexed file.
  QVERIFY2(pathExistsInNotebook(
               destPath, QStringLiteral("random_notes.txt")),
           "Unindexed random_notes.txt should be copied by supplementary copy");
}

void TestVNote3MigrationService::testCopyRemainingSkipsExclusions() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  writeNotebookConfig(sourceDir.path(), makeConfigJson());

  // Create a regular file listed in vx.json.
  {
    QFile f(base.filePath(QStringLiteral("readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Readme");
    f.close();
  }

  // Create vx_recycle_bin directory (should be excluded).
  base.mkpath(QStringLiteral("vx_recycle_bin"));
  {
    QFile f(base.filePath(QStringLiteral("vx_recycle_bin/trash.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Trash");
    f.close();
  }

  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("readme.md");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("copy_remaining_exclusions_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // vx_recycle_bin must NOT exist in destination.
  QVERIFY2(!pathExistsInNotebook(destPath, QStringLiteral("vx_recycle_bin")),
           "vx_recycle_bin directory should not be copied");
  QVERIFY2(!pathExistsInNotebook(
               destPath, QStringLiteral("vx_recycle_bin/trash.md")),
           "vx_recycle_bin/trash.md should not be copied");
}

void TestVNote3MigrationService::testCopyRemainingDoesNotOverwriteImportedFiles() {
  TempDirFixture sourceDir;
  QVERIFY(sourceDir.isValid());
  QDir base(sourceDir.path());

  QJsonObject cfgOvr;
  cfgOvr[QStringLiteral("image_folder")] = QStringLiteral("vx_images");
  cfgOvr[QStringLiteral("attachment_folder")] = QStringLiteral("vx_attachments");
  writeNotebookConfig(sourceDir.path(), makeConfigJson(cfgOvr));

  // Create readme.md with content.
  {
    QFile f(base.filePath(QStringLiteral("readme.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# Readme with timestamps");
    f.close();
  }

  // vx.json lists readme.md with timestamps.
  QJsonObject fileEntry;
  fileEntry[QStringLiteral("name")] = QStringLiteral("readme.md");
  fileEntry[QStringLiteral("created_time")] =
      QStringLiteral("2024-03-10T08:00:00Z");
  fileEntry[QStringLiteral("modified_time")] =
      QStringLiteral("2024-07-15T16:30:00Z");
  QJsonArray filesArr;
  filesArr.append(fileEntry);
  QJsonObject rootVx;
  rootVx[QStringLiteral("files")] = filesArr;
  rootVx[QStringLiteral("folders")] = QJsonArray();
  writeVxJson(sourceDir.path(), rootVx);

  QString destPath =
      m_tempDir.filePath(QStringLiteral("copy_remaining_no_overwrite_dest"));
  auto result = m_service->convertNotebook(sourceDir.path(), destPath);
  QVERIFY2(result.success, qPrintable(result.errorMessage));

  // Open notebook and verify timestamps are preserved (not clobbered).
  QString nbId = m_notebookService->openNotebook(destPath);
  QVERIFY2(!nbId.isEmpty(), "Failed to open converted notebook");

  QJsonObject fileInfo =
      m_notebookService->getFileInfo(nbId, QStringLiteral("readme.md"));
  QVERIFY2(!fileInfo.isEmpty(), "getFileInfo returned empty for readme.md");

  const qint64 expectedCreated =
      QDateTime::fromString(QStringLiteral("2024-03-10T08:00:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();
  const qint64 expectedModified =
      QDateTime::fromString(QStringLiteral("2024-07-15T16:30:00Z"),
                            Qt::ISODate)
          .toMSecsSinceEpoch();

  const qint64 actualCreated =
      fileInfo[QStringLiteral("createdUtc")].toVariant().toLongLong();
  const qint64 actualModified =
      fileInfo[QStringLiteral("modifiedUtc")].toVariant().toLongLong();

  QCOMPARE(actualCreated, expectedCreated);
  QCOMPARE(actualModified, expectedModified);

  QVERIFY(m_notebookService->closeNotebook(nbId));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVNote3MigrationService)
#include "test_vnote3migrationservice.moc"
