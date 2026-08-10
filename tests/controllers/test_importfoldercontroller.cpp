// ImportFolderController coverage for the "Shared folder from VNote" mode.
//
// The controller is the layer that turns a bundle path plus a destination node
// into a committed import: it resolves the storage roots through vxcore, runs
// FolderBundleImporter, and wires the commit to
// NotebookCoreService::attachImportedFolder under the notebook I/O gate.
//
// Two things are asserted here that the importer's own test cannot cover:
//   * the validateBundle() refusal matrix, which is what drives the dialog's
//     banner and its OK button, and
//   * one end-to-end importBundle(), proving the controller's own wiring (paths,
//     commit callback, id oracle, NodeIdentifier resolution) is correct.
//
// GUILESS: the controller opens no dialogs — the QProgressDialog belongs to
// ImportFolderDialog2.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <controllers/importfoldercontroller.h>
#include <core/servicelocator.h>
#include <core/services/folderbundleimporter.h>
#include <core/services/foldersharepackager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestImportFolderController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testValidateBundleAcceptsAWellFormedBundle();
  void testValidateBundleRejectsAnEmptyPath();
  void testValidateBundleRejectsANonBundleDirectory();
  void testValidateBundleRejectsABundleInsideTheNotebook();
  void testValidateBundleRejectsARawNotebook();
  void testValidateBundleRejectsAReadOnlyNotebook();
  void testValidateBundleRejectsAnUnknownDestinationFolder();

  void testImportBundleEndToEnd();
  void testImportBundleReportsCancellation();
  void testImportBundleFailureLeavesNothingBehind();

private:
  void makeFolder(const QString &p_path);
  void makeFile(const QString &p_folderPath, const QString &p_name, const QByteArray &p_content);
  QString makeBundle(const QString &p_relPath);
  ImportBundleInput bundleInput(const QString &p_bundlePath,
                                const QString &p_parent = QStringLiteral(".")) const;

  TempDirFixture *m_tempDir = nullptr;
  VxCoreContextHandle m_context = nullptr;
  ServiceLocator m_services;
  NotebookCoreService *m_notebooks = nullptr;
  NotebookIoGate *m_gate = nullptr;
  ImportFolderController *m_controller = nullptr;

  QString m_sourceId;
  QString m_sourcePath;
  QString m_destId;
  QString m_destPath;
};

void TestImportFolderController::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);

  m_notebooks = new NotebookCoreService(m_context, this);
  m_gate = new NotebookIoGate();
  m_services.registerService<NotebookCoreService>(m_notebooks);
  m_services.registerService<NotebookIoGate>(m_gate);
  m_controller = new ImportFolderController(m_services, this);

  m_sourcePath = m_tempDir->filePath(QStringLiteral("src-nb"));
  m_sourceId = m_notebooks->createNotebook(m_sourcePath, QStringLiteral(R"({"name": "Source NB"})"),
                                           NotebookType::Bundled);
  QVERIFY(!m_sourceId.isEmpty());

  m_destPath = m_tempDir->filePath(QStringLiteral("dest-nb"));
  m_destId = m_notebooks->createNotebook(m_destPath, QStringLiteral(R"({"name": "Dest NB"})"),
                                         NotebookType::Bundled);
  QVERIFY(!m_destId.isEmpty());

  QVERIFY(QDir().mkpath(m_tempDir->filePath(QStringLiteral("bundles"))));
}

void TestImportFolderController::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  if (m_notebooks) {
    if (!m_sourceId.isEmpty()) {
      m_notebooks->closeNotebook(m_sourceId);
    }
    if (!m_destId.isEmpty()) {
      m_notebooks->closeNotebook(m_destId);
    }
  }
  delete m_notebooks;
  m_notebooks = nullptr;
  delete m_gate;
  m_gate = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestImportFolderController::makeFolder(const QString &p_path) {
  QVERIFY(!m_notebooks->createFolderPath(m_sourceId, p_path).isEmpty());
}

void TestImportFolderController::makeFile(const QString &p_folderPath, const QString &p_name,
                                          const QByteArray &p_content) {
  QVERIFY(!m_notebooks->createFile(m_sourceId, p_folderPath, p_name).isEmpty());
  const QString relPath =
      p_folderPath.isEmpty() ? p_name : p_folderPath + QLatin1Char('/') + p_name;
  QFile file(m_sourcePath + QLatin1Char('/') + relPath);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(p_content);
  file.close();
}

QString TestImportFolderController::makeBundle(const QString &p_relPath) {
  const FolderSharePaths paths = m_notebooks->getFolderSharePaths(m_sourceId, p_relPath);
  if (!paths.isValid()) {
    return QString();
  }

  FolderSharePackager::Request request;
  request.m_notebookRoot = paths.m_notebookRoot;
  request.m_contentRoot = paths.m_contentRoot;
  request.m_metadataRoot = paths.m_metadataRoot;
  request.m_destinationParent = m_tempDir->filePath(QStringLiteral("bundles"));
  request.m_folderName = QFileInfo(paths.m_contentRoot).fileName();

  const FolderSharePackager::Result result =
      FolderSharePackager::run(request, FolderSharePackager::Callbacks());
  return result.succeeded() ? result.m_bundlePath : QString();
}

ImportBundleInput TestImportFolderController::bundleInput(const QString &p_bundlePath,
                                                          const QString &p_parent) const {
  ImportBundleInput input;
  input.notebookId = m_destId;
  input.parentFolderPath = p_parent;
  input.bundlePath = p_bundlePath;
  return input;
}

// ---------------------------------------------------------------------------
// validateBundle
// ---------------------------------------------------------------------------

void TestImportFolderController::testValidateBundleAcceptsAWellFormedBundle() {
  makeFolder(QStringLiteral("Alpha"));
  makeFolder(QStringLiteral("Alpha/Sub"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  makeFile(QStringLiteral("Alpha"), QStringLiteral("b.md"), "b");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const ImportBundleValidationResult result = m_controller->validateBundle(bundleInput(bundle));
  QVERIFY2(result.valid, qPrintable(result.message));
  // The preview the dialog shows comes straight from here.
  QCOMPARE(result.folderName, QStringLiteral("Alpha"));
  QCOMPARE(result.fileCount, 2);
  QCOMPARE(result.subfolderCount, 1);
}

void TestImportFolderController::testValidateBundleRejectsAnEmptyPath() {
  const ImportBundleValidationResult result = m_controller->validateBundle(bundleInput(QString()));
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

void TestImportFolderController::testValidateBundleRejectsANonBundleDirectory() {
  const QString plain = m_tempDir->filePath(QStringLiteral("plain"));
  QVERIFY(QDir().mkpath(plain + QStringLiteral("/Alpha")));

  const ImportBundleValidationResult result = m_controller->validateBundle(bundleInput(plain));
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains(QStringLiteral("vx_notebook")));
}

void TestImportFolderController::testValidateBundleRejectsABundleInsideTheNotebook() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  // Move the bundle inside the DESTINATION notebook: copying a tree onto itself
  // while the notebook is being mutated must be refused up front.
  const QString inside = m_destPath + QStringLiteral("/Alpha-bundle");
  QVERIFY(QDir().rename(bundle, inside));

  const ImportBundleValidationResult result = m_controller->validateBundle(bundleInput(inside));
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains(QStringLiteral("inside this notebook")));
}

void TestImportFolderController::testValidateBundleRejectsARawNotebook() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  // Bundle import is structurally bundled-only: a raw notebook has no
  // vx_notebook/contents/ tree for the metadata half to land in.
  const QString rawPath = m_tempDir->filePath(QStringLiteral("raw-nb"));
  const QString rawId = m_notebooks->createNotebook(
      rawPath, QStringLiteral(R"({"name": "Raw NB"})"), NotebookType::Raw);
  QVERIFY(!rawId.isEmpty());

  ImportBundleInput input = bundleInput(bundle);
  input.notebookId = rawId;
  const ImportBundleValidationResult result = m_controller->validateBundle(input);
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains(QStringLiteral("bundled")));

  m_notebooks->closeNotebook(rawId);
}

void TestImportFolderController::testValidateBundleRejectsAReadOnlyNotebook() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  // No Qt-side setter exists for the read-only flag, so drive the C API.
  QCOMPARE(vxcore_notebook_set_read_only(m_context, m_destId.toUtf8().constData(), 1), VXCORE_OK);

  const ImportBundleValidationResult result = m_controller->validateBundle(bundleInput(bundle));
  QVERIFY(!result.valid);
  QVERIFY(result.message.contains(QStringLiteral("read-only")));
}

void TestImportFolderController::testValidateBundleRejectsAnUnknownDestinationFolder() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const ImportBundleValidationResult result =
      m_controller->validateBundle(bundleInput(bundle, QStringLiteral("NoSuchFolder")));
  QVERIFY(!result.valid);
  QVERIFY(!result.message.isEmpty());
}

// ---------------------------------------------------------------------------
// importBundle
// ---------------------------------------------------------------------------

void TestImportFolderController::testImportBundleEndToEnd() {
  makeFolder(QStringLiteral("Alpha"));
  makeFolder(QStringLiteral("Alpha/Sub"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  makeFile(QStringLiteral("Alpha/Sub"), QStringLiteral("deep.md"), "# deep\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  QStringList labels;
  ImportFolderController::Callbacks callbacks;
  callbacks.m_labelChanged = [&labels](const QString &p_label) { labels.append(p_label); };

  const ImportFolderResult result = m_controller->importBundle(bundleInput(bundle), callbacks);
  QVERIFY2(result.success, qPrintable(result.errorMessage));
  QCOMPARE(result.nodeId.notebookId, m_destId);
  QCOMPARE(result.nodeId.relativePath, QStringLiteral("Alpha"));
  // The caller gets a labelled phase sequence to drive its progress dialog.
  QVERIFY(!labels.isEmpty());

  QVERIFY(QFileInfo::exists(m_destPath + QStringLiteral("/Alpha/note.md")));
  QVERIFY(QFileInfo::exists(m_destPath + QStringLiteral("/Alpha/Sub/deep.md")));

  // The node is reachable through the store, which is what the explorer needs
  // in order to select it after the dialog closes.
  const QJsonObject config = m_notebooks->getFolderConfig(m_destId, QStringLiteral("Alpha"));
  QVERIFY(!config.value(QStringLiteral("id")).toString().isEmpty());

  // The controller is not left busy after a completed run.
  QVERIFY(!m_controller->isBusy());
}

void TestImportFolderController::testImportBundleReportsCancellation() {
  makeFolder(QStringLiteral("Alpha"));
  for (int i = 0; i < 6; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("note%1.md").arg(i),
             QByteArray(64 * 1024, 'x'));
  }
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  ImportFolderController::Callbacks callbacks;
  callbacks.m_isCancelled = []() { return true; };

  const ImportFolderResult result = m_controller->importBundle(bundleInput(bundle), callbacks);
  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/Alpha")));
}

void TestImportFolderController::testImportBundleFailureLeavesNothingBehind() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  m_controller->testSetFailureInjection(QStringLiteral("attach"));
  const ImportFolderResult result =
      m_controller->importBundle(bundleInput(bundle), ImportFolderController::Callbacks());
  QVERIFY(!result.success);

  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/Alpha")));
  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/vx_notebook/contents/Alpha")));
  // The staging area is not left seeded either.
  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/vx_notebook/vx_import")));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestImportFolderController)
#include "test_importfoldercontroller.moc"
