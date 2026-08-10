// FolderBundleImporter coverage for the SYNCHRONOUS share-bundle import.
//
// Every case drives the REAL round trip: a bundled notebook is packaged by
// FolderSharePackager, and the resulting "*-bundle" directory is imported by
// FolderBundleImporter into a second notebook through the same commit callback
// the controller wires (NotebookCoreService::attachImportedFolder), with the
// same authoritative id oracle (collectNodeIds).
//
// The property under test throughout is VERBATIM preservation: ids, timestamps,
// tags and attachments must survive, and they are checked AFTER the destination
// notebook is closed and reopened — which rebuilds the metadata store from
// vx.json — rather than by byte-comparing JSON, so the test proves the data is
// actually reachable through the public API.
//
// The other half is the failure contract: an id collision, a cancellation, or
// an injected failure at any stage must leave the destination notebook
// byte-identical, with nothing published and no staging tree left behind.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtTest>

#include <core/services/folderbundleimporter.h>
#include <core/services/foldersharepackager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vnotex;

namespace tests {

namespace {

// Create a REAL symbolic link (not a Windows .lnk shortcut). Returns false when
// the platform or user cannot create one, so callers QSKIP rather than fail.
bool makeSymlink(const QString &p_target, const QString &p_link, bool p_targetIsDir) {
#ifdef Q_OS_WIN
  DWORD flags = p_targetIsDir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  flags |= 0x2 /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */;
  return ::CreateSymbolicLinkW(
             reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(p_link).utf16()),
             reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(p_target).utf16()),
             flags) != 0;
#else
  Q_UNUSED(p_targetIsDir);
  return QFile::link(p_target, p_link);
#endif
}

} // namespace

class TestFolderBundleImporter : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testImportsBundleWithIdsAndMetadataIntact();
  void testAttachmentsSurviveReopen();
  void testImportsIntoNestedDestination();
  void testIdCollisionFailsAndWritesNothing();
  void testNameCollisionUniquifiesWithoutTouchingIds();
  void testCancellationMidCopyPublishesNothing();
  void testInjectedFailuresLeaveNotebookUnchanged();
  void testBundleWithoutMetadataDirRejected();
  void testMalformedFolderConfigRejected();
  void testSymlinkedEntryRejected();
  void testIdOracleFailureFailsClosed();
  void testInspectReportsCounts();

private:
  // Source notebook helpers.
  void makeFolder(const QString &p_path);
  void makeFile(const QString &p_folderPath, const QString &p_name, const QByteArray &p_content);

  // Packages @p_relPath from the SOURCE notebook and returns the bundle path.
  QString makeBundle(const QString &p_relPath);

  // Runs the importer against @p_bundlePath into @p_notebookId, wiring the same
  // commit + id-oracle callbacks the controller uses in production.
  FolderBundleImporter::Result import(const QString &p_notebookId, const QString &p_bundlePath,
                                      const QString &p_destRelPath = QStringLiteral("."),
                                      const QString &p_failureInjection = QString());

  QString configPath(const QString &p_notebookPath, const QString &p_relPath) const;
  QJsonObject readConfig(const QString &p_notebookPath, const QString &p_relPath) const;
  void writeConfig(const QString &p_notebookPath, const QString &p_relPath,
                   const QJsonObject &p_config) const;

  // Every file + directory under a root, as '/'-separated relative paths.
  static QStringList snapshotTree(const QString &p_root);

  TempDirFixture *m_tempDir = nullptr;
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebooks = nullptr;

  QString m_sourceId;
  QString m_sourcePath;
  QString m_destId;
  QString m_destPath;

  // When >= 0, the cancel predicate returns true after this many progress ticks.
  int m_cancelAfterTicks = -1;
  int m_progressTicks = 0;
  // When true, the id oracle reports failure (simulating an unreadable tree).
  bool m_breakIdOracle = false;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void TestFolderBundleImporter::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);

  m_notebooks = new NotebookCoreService(m_context, this);

  m_sourcePath = m_tempDir->filePath(QStringLiteral("src-nb"));
  m_sourceId = m_notebooks->createNotebook(m_sourcePath, QStringLiteral(R"({"name": "Source NB"})"),
                                           NotebookType::Bundled);
  QVERIFY(!m_sourceId.isEmpty());

  m_destPath = m_tempDir->filePath(QStringLiteral("dest-nb"));
  m_destId = m_notebooks->createNotebook(m_destPath, QStringLiteral(R"({"name": "Dest NB"})"),
                                         NotebookType::Bundled);
  QVERIFY(!m_destId.isEmpty());

  QVERIFY(QDir().mkpath(m_tempDir->filePath(QStringLiteral("bundles"))));

  m_cancelAfterTicks = -1;
  m_progressTicks = 0;
  m_breakIdOracle = false;
}

void TestFolderBundleImporter::cleanup() {
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
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestFolderBundleImporter::makeFolder(const QString &p_path) {
  QVERIFY2(!m_notebooks->createFolderPath(m_sourceId, p_path).isEmpty(),
           qPrintable(QStringLiteral("createFolderPath failed: %1").arg(p_path)));
}

void TestFolderBundleImporter::makeFile(const QString &p_folderPath, const QString &p_name,
                                        const QByteArray &p_content) {
  QVERIFY2(!m_notebooks->createFile(m_sourceId, p_folderPath, p_name).isEmpty(),
           qPrintable(QStringLiteral("createFile failed: %1").arg(p_name)));

  const QString relPath =
      p_folderPath.isEmpty() ? p_name : p_folderPath + QLatin1Char('/') + p_name;
  QFile file(m_sourcePath + QLatin1Char('/') + relPath);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(p_content);
  file.close();
}

QString TestFolderBundleImporter::makeBundle(const QString &p_relPath) {
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

FolderBundleImporter::Result TestFolderBundleImporter::import(const QString &p_notebookId,
                                                              const QString &p_bundlePath,
                                                              const QString &p_destRelPath,
                                                              const QString &p_failureInjection) {
  const FolderImportPaths paths = m_notebooks->getFolderImportPaths(p_notebookId, p_destRelPath);
  if (!paths.isValid()) {
    FolderBundleImporter::Result failure;
    failure.m_errorMessage = paths.m_errorMessage;
    return failure;
  }

  FolderBundleImporter::Request request;
  request.m_notebookRoot = paths.m_notebookRoot;
  request.m_destContentRoot = paths.m_contentRoot;
  request.m_destMetadataRoot = paths.m_metadataRoot;
  request.m_destRelativePath = p_destRelPath;
  request.m_bundlePath = p_bundlePath;
  request.m_failureInjection = p_failureInjection;

  m_progressTicks = 0;

  FolderBundleImporter::Callbacks callbacks;
  callbacks.m_progress = [this](qint64, qint64) { ++m_progressTicks; };
  callbacks.m_isCancelled = [this]() {
    return m_cancelAfterTicks >= 0 && m_progressTicks > m_cancelAfterTicks;
  };
  callbacks.m_collectNodeIds = [this, p_notebookId](QStringList *p_outIds, QString *p_outError) {
    if (m_breakIdOracle) {
      *p_outError = QStringLiteral("injected oracle failure");
      return false;
    }
    VxCoreError error = VXCORE_OK;
    *p_outIds = m_notebooks->collectNodeIds(p_notebookId, &error);
    if (error != VXCORE_OK) {
      *p_outError = QStringLiteral("collectNodeIds failed");
      return false;
    }
    return true;
  };
  callbacks.m_commit = [this, p_notebookId](const FolderBundleImporter::CommitRequest &p_commit,
                                            QString *p_outError) {
    VxCoreError error = VXCORE_OK;
    const QString folderId =
        m_notebooks->attachImportedFolder(p_notebookId, p_commit.m_destRelativePath,
                                          p_commit.m_folderName, p_commit.m_stagingDir, &error);
    if (error != VXCORE_OK || folderId.isEmpty()) {
      *p_outError = QStringLiteral("attach failed with %1").arg(error);
      return false;
    }
    *p_commit.m_outFolderId = folderId;
    return true;
  };

  return FolderBundleImporter::run(request, callbacks);
}

QString TestFolderBundleImporter::configPath(const QString &p_notebookPath,
                                             const QString &p_relPath) const {
  QString path = p_notebookPath + QStringLiteral("/vx_notebook/contents");
  if (!p_relPath.isEmpty()) {
    path += QLatin1Char('/') + p_relPath;
  }
  return path + QStringLiteral("/vx.json");
}

QJsonObject TestFolderBundleImporter::readConfig(const QString &p_notebookPath,
                                                 const QString &p_relPath) const {
  QFile file(configPath(p_notebookPath, p_relPath));
  if (!file.open(QIODevice::ReadOnly)) {
    return QJsonObject();
  }
  const QByteArray raw = file.readAll();
  file.close();
  return QJsonDocument::fromJson(raw).object();
}

void TestFolderBundleImporter::writeConfig(const QString &p_notebookPath, const QString &p_relPath,
                                           const QJsonObject &p_config) const {
  QFile file(configPath(p_notebookPath, p_relPath));
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(p_config).toJson());
  file.close();
}

QStringList TestFolderBundleImporter::snapshotTree(const QString &p_root) {
  QStringList entries;
  QDirIterator it(p_root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  const QDir root(p_root);
  while (it.hasNext()) {
    entries.append(root.relativeFilePath(it.next()));
  }
  entries.sort();
  return entries;
}

// ---------------------------------------------------------------------------
// Happy path
// ---------------------------------------------------------------------------

void TestFolderBundleImporter::testImportsBundleWithIdsAndMetadataIntact() {
  makeFolder(QStringLiteral("Alpha"));
  makeFolder(QStringLiteral("Alpha/Sub"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  makeFile(QStringLiteral("Alpha/Sub"), QStringLiteral("deep.md"), "# deep\n");

  // Tag the note by editing vx.json directly. Per-file tag STRINGS are what the
  // bundle carries (the notebook's tag hierarchy lives in config.json, which a
  // bundle deliberately omits), so this is exactly the shape under test.
  {
    QJsonObject alpha = readConfig(m_sourcePath, QStringLiteral("Alpha"));
    QJsonArray files = alpha.value(QStringLiteral("files")).toArray();
    QJsonObject note = files.at(0).toObject();
    note[QStringLiteral("tags")] = QJsonArray({QStringLiteral("research")});
    files.replace(0, note);
    alpha[QStringLiteral("files")] = files;
    writeConfig(m_sourcePath, QStringLiteral("Alpha"), alpha);
  }

  const QJsonObject sourceAlpha = readConfig(m_sourcePath, QStringLiteral("Alpha"));
  const QJsonObject sourceSub = readConfig(m_sourcePath, QStringLiteral("Alpha/Sub"));
  QVERIFY(!sourceAlpha.value(QStringLiteral("id")).toString().isEmpty());

  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const FolderBundleImporter::Result result = import(m_destId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Succeeded);
  QCOMPARE(result.m_folderName, QStringLiteral("Alpha"));
  QCOMPARE(result.m_relativePath, QStringLiteral("Alpha"));
  QCOMPARE(result.m_folderId, sourceAlpha.value(QStringLiteral("id")).toString());

  // The staging directory is consumed by the commit, never left behind.
  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/vx_notebook/vx_import/") +
                             QStringLiteral("dangling")));
  const QDir stagingRoot(m_destPath + QStringLiteral("/vx_notebook/vx_import"));
  QVERIFY(!stagingRoot.exists() ||
          stagingRoot.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

  // Content landed.
  QVERIFY(QFileInfo::exists(m_destPath + QStringLiteral("/Alpha/note.md")));
  QVERIFY(QFileInfo::exists(m_destPath + QStringLiteral("/Alpha/Sub/deep.md")));

  // Reopen the destination so the metadata store is rebuilt from vx.json — this
  // is what makes the assertions below about the PUBLIC API meaningful.
  QVERIFY(m_notebooks->closeNotebook(m_destId));
  m_destId = m_notebooks->openNotebook(m_destPath);
  QVERIFY(!m_destId.isEmpty());
  QVERIFY(m_notebooks->rebuildNotebookCache(m_destId));

  const QJsonObject importedAlpha = m_notebooks->getFolderConfig(m_destId, QStringLiteral("Alpha"));
  QCOMPARE(importedAlpha.value(QStringLiteral("id")).toString(),
           sourceAlpha.value(QStringLiteral("id")).toString());
  QCOMPARE(importedAlpha.value(QStringLiteral("createdUtc")).toVariant().toLongLong(),
           sourceAlpha.value(QStringLiteral("createdUtc")).toVariant().toLongLong());
  QCOMPARE(importedAlpha.value(QStringLiteral("modifiedUtc")).toVariant().toLongLong(),
           sourceAlpha.value(QStringLiteral("modifiedUtc")).toVariant().toLongLong());

  const QJsonArray files = importedAlpha.value(QStringLiteral("files")).toArray();
  QCOMPARE(files.size(), 1);
  const QJsonObject note = files.at(0).toObject();
  QCOMPARE(note.value(QStringLiteral("name")).toString(), QStringLiteral("note.md"));
  QCOMPARE(note.value(QStringLiteral("id")).toString(), sourceAlpha.value(QStringLiteral("files"))
                                                            .toArray()
                                                            .at(0)
                                                            .toObject()
                                                            .value(QStringLiteral("id"))
                                                            .toString());
  QCOMPARE(note.value(QStringLiteral("tags")).toArray().size(), 1);
  QCOMPARE(note.value(QStringLiteral("tags")).toArray().at(0).toString(),
           QStringLiteral("research"));

  // The descendant subtree came along with its own ids.
  const QJsonObject importedSub =
      m_notebooks->getFolderConfig(m_destId, QStringLiteral("Alpha/Sub"));
  QCOMPARE(importedSub.value(QStringLiteral("id")).toString(),
           sourceSub.value(QStringLiteral("id")).toString());

  // And the ids are now part of the destination's namespace.
  VxCoreError error = VXCORE_OK;
  const QStringList ids = m_notebooks->collectNodeIds(m_destId, &error);
  QCOMPARE(error, VXCORE_OK);
  QVERIFY(ids.contains(sourceAlpha.value(QStringLiteral("id")).toString()));
  QVERIFY(ids.contains(sourceSub.value(QStringLiteral("id")).toString()));
}

void TestFolderBundleImporter::testAttachmentsSurviveReopen() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");

  // Attachments are dropped by the store-record converter, so they exercise the
  // dedicated attachments write inside the attach transaction.
  QJsonObject alpha = readConfig(m_sourcePath, QStringLiteral("Alpha"));
  QJsonArray files = alpha.value(QStringLiteral("files")).toArray();
  QJsonObject note = files.at(0).toObject();
  note[QStringLiteral("attachments")] = QJsonArray({QStringLiteral("spec.pdf")});
  files.replace(0, note);
  alpha[QStringLiteral("files")] = files;
  writeConfig(m_sourcePath, QStringLiteral("Alpha"), alpha);

  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  QCOMPARE(import(m_destId, bundle).m_status, FolderBundleImporter::Status::Succeeded);

  QVERIFY(m_notebooks->closeNotebook(m_destId));
  m_destId = m_notebooks->openNotebook(m_destPath);
  QVERIFY(!m_destId.isEmpty());
  QVERIFY(m_notebooks->rebuildNotebookCache(m_destId));

  const QJsonArray attachments =
      m_notebooks->listAttachments(m_destId, QStringLiteral("Alpha/note.md"));
  QCOMPARE(attachments.size(), 1);
  QCOMPARE(attachments.at(0).toString(), QStringLiteral("spec.pdf"));
}

void TestFolderBundleImporter::testImportsIntoNestedDestination() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  QVERIFY(!m_notebooks->createFolderPath(m_destId, QStringLiteral("Projects")).isEmpty());

  const FolderBundleImporter::Result result = import(m_destId, bundle, QStringLiteral("Projects"));
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Succeeded);
  QCOMPARE(result.m_relativePath, QStringLiteral("Projects/Alpha"));

  QVERIFY(QFileInfo::exists(m_destPath + QStringLiteral("/Projects/Alpha/note.md")));
  QVERIFY(!QFileInfo::exists(m_destPath + QStringLiteral("/Alpha")));

  const QJsonObject projects = readConfig(m_destPath, QStringLiteral("Projects"));
  QCOMPARE(projects.value(QStringLiteral("folders")).toArray().size(), 1);
}

// ---------------------------------------------------------------------------
// Failure contract
// ---------------------------------------------------------------------------

void TestFolderBundleImporter::testIdCollisionFailsAndWritesNothing() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  // Importing back into the SOURCE notebook is the canonical collision: every
  // id in the bundle is already there. Ids are preserved verbatim, so this must
  // be a hard failure rather than a silent remap or an overwrite.
  const QStringList before = snapshotTree(m_sourcePath);

  const FolderBundleImporter::Result result = import(m_sourceId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());

  QCOMPARE(snapshotTree(m_sourcePath), before);
}

void TestFolderBundleImporter::testNameCollisionUniquifiesWithoutTouchingIds() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const QJsonObject sourceAlpha = readConfig(m_sourcePath, QStringLiteral("Alpha"));

  // A DIFFERENT folder already occupies the name in the destination.
  QVERIFY(!m_notebooks->createFolderPath(m_destId, QStringLiteral("Alpha")).isEmpty());
  const QString existingId =
      readConfig(m_destPath, QStringLiteral("Alpha")).value(QStringLiteral("id")).toString();
  QVERIFY(!existingId.isEmpty());

  const FolderBundleImporter::Result result = import(m_destId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Succeeded);
  QCOMPARE(result.m_folderName, QStringLiteral("Alpha (2)"));

  // The name is the ONLY thing rewritten: the id is untouched.
  const QJsonObject imported = readConfig(m_destPath, QStringLiteral("Alpha (2)"));
  QCOMPARE(imported.value(QStringLiteral("name")).toString(), QStringLiteral("Alpha (2)"));
  QCOMPARE(imported.value(QStringLiteral("id")).toString(),
           sourceAlpha.value(QStringLiteral("id")).toString());

  // And the pre-existing folder is untouched.
  QCOMPARE(readConfig(m_destPath, QStringLiteral("Alpha")).value(QStringLiteral("id")).toString(),
           existingId);
}

void TestFolderBundleImporter::testCancellationMidCopyPublishesNothing() {
  makeFolder(QStringLiteral("Alpha"));
  for (int i = 0; i < 8; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("note%1.md").arg(i),
             QByteArray(64 * 1024, 'x'));
  }
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const QStringList before = snapshotTree(m_destPath);

  m_cancelAfterTicks = 1;
  const FolderBundleImporter::Result result = import(m_destId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Cancelled);

  QCOMPARE(snapshotTree(m_destPath), before);
}

void TestFolderBundleImporter::testInjectedFailuresLeaveNotebookUnchanged() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const QStringList before = snapshotTree(m_destPath);

  for (const QString &stage : {QStringLiteral("copy"), QStringLiteral("verify"),
                               QStringLiteral("publish"), QStringLiteral("attach")}) {
    const FolderBundleImporter::Result result =
        import(m_destId, bundle, QStringLiteral("."), stage);
    QVERIFY2(result.m_status == FolderBundleImporter::Status::Failed,
             qPrintable(QStringLiteral("stage %1 did not fail").arg(stage)));
    QVERIFY2(snapshotTree(m_destPath) == before,
             qPrintable(QStringLiteral("stage %1 mutated the notebook").arg(stage)));
  }
}

void TestFolderBundleImporter::testBundleWithoutMetadataDirRejected() {
  const QString fake = m_tempDir->filePath(QStringLiteral("fake-bundle"));
  QVERIFY(QDir().mkpath(fake + QStringLiteral("/Alpha")));
  QFile file(fake + QStringLiteral("/Alpha/note.md"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("x");
  file.close();

  const QStringList before = snapshotTree(m_destPath);
  const FolderBundleImporter::Result result = import(m_destId, fake);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Failed);
  QVERIFY(result.m_errorMessage.contains(QStringLiteral("vx_notebook")));
  QCOMPARE(snapshotTree(m_destPath), before);

  // The inspection used by the dialog preview agrees.
  QVERIFY(!FolderBundleImporter::inspect(fake).m_valid);
}

void TestFolderBundleImporter::testMalformedFolderConfigRejected() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  // Corrupt the bundle's own metadata AFTER packaging.
  QFile config(bundle + QStringLiteral("/vx_notebook/contents/Alpha/vx.json"));
  QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
  config.write("{ not json");
  config.close();

  const QStringList before = snapshotTree(m_destPath);
  QCOMPARE(import(m_destId, bundle).m_status, FolderBundleImporter::Status::Failed);
  QCOMPARE(snapshotTree(m_destPath), before);
  QVERIFY(!FolderBundleImporter::inspect(bundle).m_valid);
}

void TestFolderBundleImporter::testSymlinkedEntryRejected() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const QString outside = m_tempDir->filePath(QStringLiteral("outside.md"));
  QFile file(outside);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("secret");
  file.close();

  if (!makeSymlink(outside, bundle + QStringLiteral("/Alpha/link.md"), false)) {
    QSKIP("Cannot create a symbolic link on this platform/user.");
  }

  const QStringList before = snapshotTree(m_destPath);
  const FolderBundleImporter::Result result = import(m_destId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Failed);
  QCOMPARE(snapshotTree(m_destPath), before);
}

void TestFolderBundleImporter::testIdOracleFailureFailsClosed() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "# note\n");
  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const QStringList before = snapshotTree(m_destPath);

  // "Could not determine" must never be treated as "no collision".
  m_breakIdOracle = true;
  const FolderBundleImporter::Result result = import(m_destId, bundle);
  QCOMPARE(result.m_status, FolderBundleImporter::Status::Failed);
  QCOMPARE(snapshotTree(m_destPath), before);
}

void TestFolderBundleImporter::testInspectReportsCounts() {
  makeFolder(QStringLiteral("Alpha"));
  makeFolder(QStringLiteral("Alpha/Sub"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  makeFile(QStringLiteral("Alpha"), QStringLiteral("b.md"), "b");
  makeFile(QStringLiteral("Alpha/Sub"), QStringLiteral("c.md"), "c");

  const QString bundle = makeBundle(QStringLiteral("Alpha"));
  QVERIFY(!bundle.isEmpty());

  const FolderBundleImporter::Inspection inspection = FolderBundleImporter::inspect(bundle);
  QVERIFY(inspection.m_valid);
  QCOMPARE(inspection.m_folderName, QStringLiteral("Alpha"));
  QCOMPARE(inspection.m_fileCount, 3);
  QCOMPARE(inspection.m_subfolderCount, 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFolderBundleImporter)
#include "test_folderbundleimporter.moc"
