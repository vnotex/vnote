#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/services/notebookcoreservice.h>
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

private:
  void writeNotebookConfig(const QString &p_basePath, const QByteArray &p_content);
  QByteArray makeConfigJson(const QJsonObject &p_overrides = QJsonObject());
  void buildSyntheticSourceTree(const QString &p_basePath);
  void createSyntheticVNote3Source(const QString &p_basePath);
  bool pathExistsInNotebook(const QString &p_notebookRoot, const QString &p_relPath);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
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

  // Create VNote3MigrationService.
  m_service = new VNote3MigrationService(m_notebookService, this);
  QVERIFY(m_service != nullptr);
}

void TestVNote3MigrationService::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;

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
  jobj[QStringLiteral("image_folder")] = QStringLiteral("_v_images");
  jobj[QStringLiteral("attachment_folder")] = QStringLiteral("_v_attachments");
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

  // Create folder-local vx.json files (should be excluded).
  {
    QFile f(base.filePath(QStringLiteral("vx.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
  }
  {
    QFile f(base.filePath(QStringLiteral("notes/vx.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
  }
  {
    QFile f(base.filePath(QStringLiteral("notes/sub/vx.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
  }

  // Create vx_images folder with a sample image (should be preserved).
  base.mkpath(QStringLiteral("vx_images"));
  {
    QFile f(base.filePath(QStringLiteral("vx_images/diagram.png")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake png data");
    f.close();
  }

  // Create vx_attachments folder with a sample attachment (should be preserved).
  base.mkpath(QStringLiteral("vx_attachments"));
  {
    QFile f(base.filePath(QStringLiteral("vx_attachments/doc.pdf")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("fake pdf data");
    f.close();
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

  // Create notes/vx.json (legacy per-folder config — should be excluded).
  {
    QFile f(base.filePath(QStringLiteral("notes/vx.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{}");
    f.close();
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
  // vxcore creates its own vx.json metadata files under vx_notebook/contents/ which is expected.
  // Scan only the user content area (root level, excluding vx_notebook/) for stray vx.json.
  QDirIterator it(destPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    QFileInfo fi = it.fileInfo();
    // Skip vxcore's own metadata tree entirely.
    QString relPath = QDir(destPath).relativeFilePath(fi.absoluteFilePath());
    if (relPath.startsWith(QStringLiteral("vx_notebook"))) {
      continue;
    }
    if (fi.isFile()) {
      QVERIFY2(fi.fileName() != QStringLiteral("vx.json"),
               qPrintable(QStringLiteral("vx.json found at: %1").arg(fi.filePath())));
    }
  }
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

  // notes/vx.json excluded — scan user content area only (skip vxcore metadata).
  QDirIterator it(destPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    QFileInfo fi = it.fileInfo();
    QString relPath = QDir(destPath).relativeFilePath(fi.absoluteFilePath());
    if (relPath.startsWith(QStringLiteral("vx_notebook"))) {
      continue;
    }
    if (fi.isFile()) {
      QVERIFY2(fi.fileName() != QStringLiteral("vx.json"),
               qPrintable(QStringLiteral("vx.json should be excluded: %1").arg(relPath)));
    }
  }

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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestVNote3MigrationService)
#include "test_vnote3migrationservice.moc"
