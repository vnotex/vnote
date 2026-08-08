// FolderShareController end-to-end coverage for the SYNCHRONOUS share flow.
//
// The controller resolves the storage roots through vxcore, saves every open
// modified note under the folder, and drives FolderSharePackager — all on the
// calling thread. These cases cover the whole feature except the two dialogs,
// which belong to NotebookExplorer2:
//
//   * the produced package layout, flattening, and byte-for-byte metadata;
//   * unindexed / hidden / empty content;
//   * "-bundle", "-bundle (2)" collision naming and Unicode names;
//   * every refusal (raw notebook, root, reserved basename, destination inside
//     the notebook, orphan metadata, malformed records, missing content,
//     symlinked roots and ancestors) publishing NO bundle and leaving no temp;
//   * source mutation and staged corruption detected before publish;
//   * cancellation publishing nothing;
//   * the save barrier making modified notes durable under every auto-save
//     policy, and failing the share when a note cannot be saved.

#include <QAtomicInt>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <QtTest>

#include <controllers/foldersharecontroller.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/foldersharepackager.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vnotex;

namespace tests {

namespace {

// Create a REAL symbolic link (not a Windows .lnk shortcut, which is what
// QFile::link produces there). Returns false when the platform or the current
// user cannot create one — on Windows this needs Developer Mode or the
// SeCreateSymbolicLink privilege, so callers QSKIP rather than fail.
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

class TestFolderShareController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testSharesNestedFolderFlattened();
  void testCopiesMetadataByteForByte();
  void testCopiesUnindexedHiddenAndEmptyContent();
  void testNoManifestOrNotebookConfig();
  void testUnicodeNamesAndCollisionNaming();
  void testCustomAssetsAndTaggedFolderProceed();
  void testEmptyAttachmentsOmittedIsValid();
  void testNonStringAttachmentEntryRejected();
  void testRejectsRawNotebook();
  void testRejectsNotebookRoot();
  void testRejectsReservedBasename();
  void testRejectsDestinationInsideNotebook();
  void testRejectsOrphanSelectedMetadata();
  void testRejectsMalformedFileRecord();
  void testRejectsMissingIndexedContent();
  void testRejectsSymlinkInsideSource();
  void testRejectsSymlinkedContentRoot();
  void testRejectsSymlinkedAncestor();
  void testAcceptsDestinationReachedThroughSymlink();
  void testSourceMutationDuringCopyFails();
  void testInjectedFailuresLeaveNoBundleOrTemp();
  void testCancellationPublishesNothing();
  void testProgressIsMonotonicAndBounded();
  void testModifiedNoteIsSavedUnderEveryPolicy();
  void testPendingAutoSaveIsDrainedBeforeSnapshot();
  void testReadOnlyNotebookWithModifiedNoteFails();
  void testSaveCancelledByHookFailsTheShare();
  void testGateHeldByAnotherActorFailsTheShare();
  void testInMemoryEditDuringCopyFailsTheShare();
  void testNoteOpenedDuringCopyFailsTheShare();
  void testDuplicateMetadataIdRejected();
  void testFractionalTimestampRejected();

private:
  void makeFolder(const QString &p_path);
  void makeFile(const QString &p_folderPath, const QString &p_name, const QByteArray &p_content);
  QString destination() const { return m_tempDir->filePath(QStringLiteral("dest")); }

  FolderSharePackager::Result share(const QString &p_relPath,
                                    const QString &p_destination = QString());

  static QByteArray readAll(const QString &p_path);
  // Directories under the destination that are NOT the published bundle.
  static QStringList leftoverEntries(const QString &p_destination, const QString &p_bundlePath);

  QString configPath(const QString &p_relPath) const;
  QJsonObject readConfig(const QString &p_relPath) const;
  void writeConfig(const QString &p_relPath, const QJsonObject &p_config) const;

  TempDirFixture *m_tempDir = nullptr;
  VxCoreContextHandle m_context = nullptr;
  ServiceLocator m_services;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebooks = nullptr;
  // Owned here (rather than by BufferService) so a test can hold the gate from
  // another thread and observe the share back off.
  NotebookIoGate *m_gate = nullptr;
  BufferService *m_buffers = nullptr;
  FolderShareController *m_controller = nullptr;

  QString m_notebookId;
  QString m_notebookPath;

  // Recorded by the callbacks of the most recent share().
  QVector<QPair<qint64, qint64>> m_progressTicks;
  QStringList m_labels;
  // When set, the cancel predicate returns true once this many progress ticks
  // have been observed.
  int m_cancelAfterTicks = -1;
  // Invoked on every progress tick, so a test can mutate the source mid-copy.
  std::function<void()> m_onProgress;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void TestFolderShareController::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context);

  m_hookMgr = new HookManager(this);
  m_notebooks = new NotebookCoreService(m_context, this);
  m_notebooks->setHookManager(m_hookMgr);
  m_gate = new NotebookIoGate();
  m_buffers = new BufferService(m_context, m_hookMgr, m_gate, AutoSavePolicy::None, this);

  m_services.registerService<NotebookCoreService>(m_notebooks);
  m_services.registerService<BufferService>(m_buffers);
  m_controller = new FolderShareController(m_services, this);

  m_notebookPath = m_tempDir->filePath(QStringLiteral("nb"));
  m_notebookId = m_notebooks->createNotebook(
      m_notebookPath, QStringLiteral(R"({"name": "Share NB"})"), NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
  QVERIFY(QDir().mkpath(destination()));

  m_progressTicks.clear();
  m_labels.clear();
  m_cancelAfterTicks = -1;
  m_onProgress = nullptr;
}

void TestFolderShareController::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  if (m_buffers) {
    m_buffers->shutdown(3000);
  }
  if (!m_notebookId.isEmpty() && m_notebooks) {
    m_notebooks->closeNotebook(m_notebookId);
  }
  delete m_buffers;
  m_buffers = nullptr;
  // The gate must outlive the save queue's workers.
  delete m_gate;
  m_gate = nullptr;
  delete m_notebooks;
  m_notebooks = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestFolderShareController::makeFolder(const QString &p_path) {
  QVERIFY2(!m_notebooks->createFolderPath(m_notebookId, p_path).isEmpty(),
           qPrintable(QStringLiteral("createFolderPath failed: %1").arg(p_path)));
}

void TestFolderShareController::makeFile(const QString &p_folderPath, const QString &p_name,
                                         const QByteArray &p_content) {
  QVERIFY2(!m_notebooks->createFile(m_notebookId, p_folderPath, p_name).isEmpty(),
           qPrintable(QStringLiteral("createFile failed: %1").arg(p_name)));

  const QString relPath =
      p_folderPath.isEmpty() ? p_name : p_folderPath + QLatin1Char('/') + p_name;
  QFile file(m_notebookPath + QLatin1Char('/') + relPath);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(p_content);
  file.close();
}

QString TestFolderShareController::configPath(const QString &p_relPath) const {
  QString path = m_notebookPath + QStringLiteral("/vx_notebook/contents");
  if (!p_relPath.isEmpty()) {
    path += QLatin1Char('/') + p_relPath;
  }
  return path + QStringLiteral("/vx.json");
}

QJsonObject TestFolderShareController::readConfig(const QString &p_relPath) const {
  QFile file(configPath(p_relPath));
  if (!file.open(QIODevice::ReadOnly)) {
    return QJsonObject();
  }
  const QByteArray raw = file.readAll();
  file.close();
  return QJsonDocument::fromJson(raw).object();
}

void TestFolderShareController::writeConfig(const QString &p_relPath,
                                            const QJsonObject &p_config) const {
  QFile file(configPath(p_relPath));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  file.write(QJsonDocument(p_config).toJson(QJsonDocument::Indented));
  file.close();
}

QByteArray TestFolderShareController::readAll(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}

QStringList TestFolderShareController::leftoverEntries(const QString &p_destination,
                                                       const QString &p_bundlePath) {
  QStringList result;
  const QFileInfoList entries =
      QDir(p_destination)
          .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  for (const QFileInfo &info : entries) {
    if (!p_bundlePath.isEmpty() &&
        QDir::cleanPath(info.absoluteFilePath()) == QDir::cleanPath(p_bundlePath)) {
      continue;
    }
    result.append(info.fileName());
  }
  result.sort();
  return result;
}

FolderSharePackager::Result TestFolderShareController::share(const QString &p_relPath,
                                                             const QString &p_destination) {
  m_progressTicks.clear();
  m_labels.clear();

  FolderShareController::Callbacks callbacks;
  callbacks.m_labelChanged = [this](const QString &p_label) { m_labels.append(p_label); };
  callbacks.m_progress = [this](qint64 p_done, qint64 p_total) {
    m_progressTicks.append(qMakePair(p_done, p_total));
    if (m_onProgress) {
      m_onProgress();
    }
  };
  callbacks.m_isCancelled = [this]() {
    return m_cancelAfterTicks >= 0 && m_progressTicks.size() > m_cancelAfterTicks;
  };

  return m_controller->shareFolder(NodeIdentifier{m_notebookId, p_relPath},
                                   p_destination.isEmpty() ? destination() : p_destination,
                                   callbacks);
}

// ---------------------------------------------------------------------------
// Happy paths
// ---------------------------------------------------------------------------

void TestFolderShareController::testSharesNestedFolderFlattened() {
  makeFolder(QStringLiteral("Projects"));
  makeFolder(QStringLiteral("Projects/Alpha"));
  makeFolder(QStringLiteral("Projects/Alpha/Sub"));
  makeFile(QStringLiteral("Projects/Alpha"), QStringLiteral("note.md"), "hello alpha");
  makeFile(QStringLiteral("Projects/Alpha/Sub"), QStringLiteral("deep.md"), "deep");
  // A sibling that MUST NOT appear in the bundle.
  makeFile(QStringLiteral("Projects"), QStringLiteral("sibling.md"), "sibling");

  const auto result = share(QStringLiteral("Projects/Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
  const QString bundle = result.m_bundlePath;

  QCOMPARE(QFileInfo(bundle).fileName(), QStringLiteral("Alpha-bundle"));

  // Flattened: top-level "Alpha", no "Projects" anywhere.
  QVERIFY(QFileInfo(bundle + QStringLiteral("/Alpha")).isDir());
  QVERIFY(!QFileInfo::exists(bundle + QStringLiteral("/Projects")));
  QCOMPARE(readAll(bundle + QStringLiteral("/Alpha/note.md")), QByteArray("hello alpha"));
  QCOMPARE(readAll(bundle + QStringLiteral("/Alpha/Sub/deep.md")), QByteArray("deep"));
  QVERIFY(!QFileInfo::exists(bundle + QStringLiteral("/Alpha/sibling.md")));

  // Metadata mirrors the same flattened shape.
  QVERIFY(QFileInfo::exists(bundle + QStringLiteral("/vx_notebook/contents/Alpha/vx.json")));
  QVERIFY(QFileInfo::exists(bundle + QStringLiteral("/vx_notebook/contents/Alpha/Sub/vx.json")));
  QVERIFY(!QFileInfo::exists(bundle + QStringLiteral("/vx_notebook/contents/Projects")));

  QCOMPARE(leftoverEntries(destination(), bundle), QStringList());
}

void TestFolderShareController::testCopiesMetadataByteForByte() {
  makeFolder(QStringLiteral("Alpha"));
  makeFolder(QStringLiteral("Alpha/Nested"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  makeFile(QStringLiteral("Alpha"), QStringLiteral("b.md"), "b");
  makeFile(QStringLiteral("Alpha/Nested"), QStringLiteral("c.md"), "c");

  // Non-default metadata: tags, a custom metadata object, and an attachment.
  QVERIFY(m_notebooks->createTag(m_notebookId, QStringLiteral("research")));
  QVERIFY(m_notebooks->updateFileTags(m_notebookId, QStringLiteral("Alpha/a.md"),
                                      QStringList{QStringLiteral("research")}));
  QVERIFY(m_notebooks->updateFileAttachments(m_notebookId, QStringLiteral("Alpha/b.md"),
                                             QStringList{QStringLiteral("doc.pdf")}));
  QVERIFY(m_notebooks->updateFolderMetadata(m_notebookId, QStringLiteral("Alpha"),
                                            QStringLiteral(R"({"color":"blue"})")));

  const QByteArray alphaConfig = readAll(configPath(QStringLiteral("Alpha")));
  const QByteArray nestedConfig = readAll(configPath(QStringLiteral("Alpha/Nested")));
  QVERIFY(!alphaConfig.isEmpty());
  QVERIFY(!nestedConfig.isEmpty());

  const auto result = share(QStringLiteral("Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
  const QString bundle = result.m_bundlePath;

  // Byte-for-byte: ids, timestamps, tags, attachments and child order survive.
  QCOMPARE(readAll(bundle + QStringLiteral("/vx_notebook/contents/Alpha/vx.json")), alphaConfig);
  QCOMPARE(readAll(bundle + QStringLiteral("/vx_notebook/contents/Alpha/Nested/vx.json")),
           nestedConfig);

  const QJsonObject copied =
      QJsonDocument::fromJson(
          readAll(bundle + QStringLiteral("/vx_notebook/contents/Alpha/vx.json")))
          .object();
  const QString sourceId =
      readConfig(QStringLiteral("Alpha")).value(QStringLiteral("id")).toString();
  QVERIFY(!sourceId.isEmpty());
  QCOMPARE(copied.value(QStringLiteral("id")).toString(), sourceId);
}

void TestFolderShareController::testCopiesUnindexedHiddenAndEmptyContent() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("indexed.md"), "indexed");

  // Unindexed file, hidden file, and an empty directory — none of which the
  // metadata traversal alone would find.
  const QString alphaDir = m_notebookPath + QStringLiteral("/Alpha");
  QFile unindexed(alphaDir + QStringLiteral("/unindexed.txt"));
  QVERIFY(unindexed.open(QIODevice::WriteOnly));
  unindexed.write("not in vx.json");
  unindexed.close();

  QFile hidden(alphaDir + QStringLiteral("/.hidden"));
  QVERIFY(hidden.open(QIODevice::WriteOnly));
  hidden.write("hidden bytes");
  hidden.close();

  QVERIFY(QDir().mkpath(alphaDir + QStringLiteral("/EmptyDir")));

  const auto result = share(QStringLiteral("Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
  const QString bundle = result.m_bundlePath;

  QCOMPARE(readAll(bundle + QStringLiteral("/Alpha/unindexed.txt")), QByteArray("not in vx.json"));
  QCOMPARE(readAll(bundle + QStringLiteral("/Alpha/.hidden")), QByteArray("hidden bytes"));
  QVERIFY(QFileInfo(bundle + QStringLiteral("/Alpha/EmptyDir")).isDir());
}

void TestFolderShareController::testNoManifestOrNotebookConfig() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  const auto result = share(QStringLiteral("Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
  const QString bundle = result.m_bundlePath;

  QVERIFY(!QFileInfo::exists(bundle + QStringLiteral("/vx_notebook/config.json")));
  QVERIFY(!QFileInfo::exists(bundle + QStringLiteral("/manifest.json")));

  // The package root holds exactly the content folder plus the metadata dir.
  QStringList topLevel;
  for (const QFileInfo &info :
       QDir(bundle).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
    topLevel.append(info.fileName());
  }
  topLevel.sort();
  QCOMPARE(topLevel, QStringList({QStringLiteral("Alpha"), QStringLiteral("vx_notebook")}));
}

void TestFolderShareController::testUnicodeNamesAndCollisionNaming() {
  const QString folder = QString::fromUtf8("\xE9\xA1\xB9\xE7\x9B\xAE"); // 项目
  makeFolder(folder);
  makeFile(folder, QString::fromUtf8("\xE7\xAC\x94\xE8\xAE\xB0.md"), "unicode note");

  const auto first = share(folder);
  QVERIFY2(first.succeeded(), qPrintable(first.m_errorMessage));
  QCOMPARE(QFileInfo(first.m_bundlePath).fileName(), folder + QStringLiteral("-bundle"));
  QCOMPARE(readAll(first.m_bundlePath + QLatin1Char('/') + folder + QStringLiteral("/") +
                   QString::fromUtf8("\xE7\xAC\x94\xE8\xAE\xB0.md")),
           QByteArray("unicode note"));

  // A second share of the same folder must not clobber the first.
  const auto second = share(folder);
  QVERIFY2(second.succeeded(), qPrintable(second.m_errorMessage));
  QCOMPARE(QFileInfo(second.m_bundlePath).fileName(), folder + QStringLiteral("-bundle (2)"));
  QVERIFY(QFileInfo(first.m_bundlePath).isDir());

  const auto third = share(folder);
  QVERIFY2(third.succeeded(), qPrintable(third.m_errorMessage));
  QCOMPARE(QFileInfo(third.m_bundlePath).fileName(), folder + QStringLiteral("-bundle (3)"));
}

void TestFolderShareController::testCustomAssetsAndTaggedFolderProceed() {
  // A custom assets folder is NOT recorded in the bundle (no config.json), but
  // its physical bytes are copied as ordinary content, and the share proceeds
  // without any warning.
  QVERIFY(m_notebooks->updateNotebookConfig(m_notebookId,
                                            QStringLiteral(R"({"assetsFolder":"my_assets"})")));

  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  QVERIFY(m_notebooks->createTag(m_notebookId, QStringLiteral("important")));
  QVERIFY(m_notebooks->updateFileTags(m_notebookId, QStringLiteral("Alpha/a.md"),
                                      QStringList{QStringLiteral("important")}));

  const QString assetsDir = m_notebookPath + QStringLiteral("/Alpha/my_assets");
  QVERIFY(QDir().mkpath(assetsDir));
  QFile asset(assetsDir + QStringLiteral("/image.png"));
  QVERIFY(asset.open(QIODevice::WriteOnly));
  asset.write("PNGDATA");
  asset.close();

  const auto result = share(QStringLiteral("Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));

  QCOMPARE(readAll(result.m_bundlePath + QStringLiteral("/Alpha/my_assets/image.png")),
           QByteArray("PNGDATA"));
  QVERIFY(!QFileInfo::exists(result.m_bundlePath + QStringLiteral("/vx_notebook/config.json")));

  // The file tag NAME survives inside vx.json even though the notebook-level
  // tag hierarchy is deliberately not carried.
  QVERIFY(readAll(result.m_bundlePath + QStringLiteral("/vx_notebook/contents/Alpha/vx.json"))
              .contains("important"));
}

void TestFolderShareController::testEmptyAttachmentsOmittedIsValid() {
  // The canonical serializer OMITS "attachments" when empty. The strict
  // validator must accept that (it is the shape of every ordinary file).
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("plain.md"), "plain");

  const QJsonArray files =
      readConfig(QStringLiteral("Alpha")).value(QStringLiteral("files")).toArray();
  QCOMPARE(files.size(), 1);
  QVERIFY(!files.at(0).toObject().contains(QStringLiteral("attachments")));

  const auto result = share(QStringLiteral("Alpha"));
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
}

void TestFolderShareController::testNonStringAttachmentEntryRejected() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  QJsonObject config = readConfig(QStringLiteral("Alpha"));
  QJsonArray files = config.value(QStringLiteral("files")).toArray();
  QJsonObject record = files.at(0).toObject();
  record[QStringLiteral("attachments")] = QJsonArray({QJsonValue(42)});
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);

  auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());

  // A non-ARRAY attachments value is equally invalid.
  record[QStringLiteral("attachments")] = QJsonValue(QStringLiteral("doc.pdf"));
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);

  result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

void TestFolderShareController::testRejectsRawNotebook() {
  const QString rawPath = m_tempDir->filePath(QStringLiteral("raw_nb"));
  QVERIFY(QDir().mkpath(rawPath + QStringLiteral("/Alpha")));
  const QString rawId =
      m_notebooks->createNotebook(rawPath, QStringLiteral(R"({"name": "Raw"})"), NotebookType::Raw);
  QVERIFY(!rawId.isEmpty());

  FolderShareController::Callbacks callbacks;
  const auto result = m_controller->shareFolder(NodeIdentifier{rawId, QStringLiteral("Alpha")},
                                                destination(), callbacks);
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());

  m_notebooks->closeNotebook(rawId);
}

void TestFolderShareController::testRejectsNotebookRoot() {
  makeFolder(QStringLiteral("Alpha"));

  QCOMPARE(share(QString()).m_status, FolderSharePackager::Status::Failed);
  QCOMPARE(share(QStringLiteral(".")).m_status, FolderSharePackager::Status::Failed);
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testRejectsReservedBasename() {
  // A NESTED folder named vx_notebook is legal inside a notebook, but it cannot
  // occupy the package root beside the bundle's own metadata directory.
  makeFolder(QStringLiteral("Projects"));
  makeFolder(QStringLiteral("Projects/vx_notebook"));
  makeFile(QStringLiteral("Projects/vx_notebook"), QStringLiteral("a.md"), "a");

  const auto result = share(QStringLiteral("Projects/vx_notebook"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(result.m_errorMessage.contains(QStringLiteral("vx_notebook")));
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testRejectsDestinationInsideNotebook() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  QCOMPARE(share(QStringLiteral("Alpha"), m_notebookPath + QStringLiteral("/Alpha")).m_status,
           FolderSharePackager::Status::Failed);
  // The notebook root itself is equally refused.
  QCOMPARE(share(QStringLiteral("Alpha"), m_notebookPath).m_status,
           FolderSharePackager::Status::Failed);
}

void TestFolderShareController::testRejectsOrphanSelectedMetadata() {
  makeFolder(QStringLiteral("Projects"));
  makeFolder(QStringLiteral("Projects/Alpha"));
  makeFile(QStringLiteral("Projects/Alpha"), QStringLiteral("a.md"), "a");

  // Drop Projects from the ROOT config while leaving Projects/vx.json (which
  // still lists Alpha) and both physical directories intact. Checking only the
  // immediate parent edge would wrongly accept this.
  m_notebooks->closeNotebook(m_notebookId);
  QJsonObject root = readConfig(QString());
  root[QStringLiteral("folders")] = QJsonArray();
  writeConfig(QString(), root);
  m_notebookId = m_notebooks->openNotebook(m_notebookPath);
  QVERIFY(!m_notebookId.isEmpty());

  QCOMPARE(share(QStringLiteral("Projects/Alpha")).m_status, FolderSharePackager::Status::Failed);
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testRejectsMalformedFileRecord() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  QJsonObject config = readConfig(QStringLiteral("Alpha"));
  QJsonArray files = config.value(QStringLiteral("files")).toArray();
  QJsonObject record = files.at(0).toObject();

  // Missing id.
  record.remove(QStringLiteral("id"));
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);
  QCOMPARE(share(QStringLiteral("Alpha")).m_status, FolderSharePackager::Status::Failed);

  // Wrong type for a timestamp.
  record[QStringLiteral("id")] = QStringLiteral("some-id");
  record[QStringLiteral("createdUtc")] = QStringLiteral("not-a-number");
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);
  QCOMPARE(share(QStringLiteral("Alpha")).m_status, FolderSharePackager::Status::Failed);

  // Traversing child name.
  record[QStringLiteral("createdUtc")] = 1;
  record[QStringLiteral("name")] = QStringLiteral("../escape.md");
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);
  QCOMPARE(share(QStringLiteral("Alpha")).m_status, FolderSharePackager::Status::Failed);

  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testRejectsMissingIndexedContent() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("gone.md"), "gone");

  // Delete only the CONTENT; the record in vx.json stays.
  QVERIFY(QFile::remove(m_notebookPath + QStringLiteral("/Alpha/gone.md")));

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testRejectsSymlinkInsideSource() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("real.md"), "real");

  const QString outsideTarget = m_tempDir->filePath(QStringLiteral("outside.md"));
  QFile target(outsideTarget);
  QVERIFY(target.open(QIODevice::WriteOnly));
  target.write("outside");
  target.close();

  if (!makeSymlink(outsideTarget, m_notebookPath + QStringLiteral("/Alpha/link.md"), false)) {
    QSKIP("This platform/user cannot create symbolic links");
  }

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

// The SELECTED folder itself may be a junction pointing outside the notebook.
// is_directory() follows it, so the explicit link checks are what must refuse;
// without them the packager would copy an external tree.
void TestFolderShareController::testRejectsSymlinkedContentRoot() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  const QString outside = m_tempDir->filePath(QStringLiteral("outside_tree"));
  QVERIFY(QDir().mkpath(outside));
  QFile secret(outside + QStringLiteral("/secret.txt"));
  QVERIFY(secret.open(QIODevice::WriteOnly));
  secret.write("secret");
  secret.close();

  const QString alphaDir = m_notebookPath + QStringLiteral("/Alpha");
  QDir(alphaDir).removeRecursively();
  if (!makeSymlink(outside, alphaDir, true)) {
    QSKIP("This platform/user cannot create symbolic links");
  }

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

// Same hole one level up: the selected folder is fine, but an ANCESTOR is a
// junction, so the resolved path lives outside the notebook.
void TestFolderShareController::testRejectsSymlinkedAncestor() {
  makeFolder(QStringLiteral("Projects"));
  makeFolder(QStringLiteral("Projects/Alpha"));
  makeFile(QStringLiteral("Projects/Alpha"), QStringLiteral("a.md"), "a");

  const QString outside = m_tempDir->filePath(QStringLiteral("outside_projects"));
  QVERIFY(QDir().mkpath(outside + QStringLiteral("/Alpha")));
  QFile planted(outside + QStringLiteral("/Alpha/planted.md"));
  QVERIFY(planted.open(QIODevice::WriteOnly));
  planted.write("planted");
  planted.close();

  const QString projectsDir = m_notebookPath + QStringLiteral("/Projects");
  QDir(projectsDir).removeRecursively();
  if (!makeSymlink(outside, projectsDir, true)) {
    QSKIP("This platform/user cannot create symbolic links");
  }

  const auto result = share(QStringLiteral("Projects/Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testAcceptsDestinationReachedThroughSymlink() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  const QString realDest = m_tempDir->filePath(QStringLiteral("realdest"));
  QVERIFY(QDir().mkpath(realDest));
  const QString linkedDest = m_tempDir->filePath(QStringLiteral("linkeddest"));
  if (!makeSymlink(realDest, linkedDest, true)) {
    QSKIP("This platform/user cannot create symbolic links");
  }

  const auto result = share(QStringLiteral("Alpha"), linkedDest);
  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));
  // The bundle materializes in the RESOLVED directory.
  QVERIFY(QFileInfo(realDest + QStringLiteral("/Alpha-bundle")).isDir());
}

// ---------------------------------------------------------------------------
// External races, failures, cancellation
// ---------------------------------------------------------------------------

void TestFolderShareController::testSourceMutationDuringCopyFails() {
  makeFolder(QStringLiteral("Alpha"));
  const QByteArray big(2 * 1024 * 1024, 'x');
  for (int i = 0; i < 3; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("big%1.bin").arg(i), big);
  }
  // An UNINDEXED file: ordinary metadata traversal would miss it entirely,
  // which is why the inventory covers unindexed content too.
  const QString victim = m_notebookPath + QStringLiteral("/Alpha/victim.bin");
  QFile file(victim);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(QByteArray(1024, 'a'));
  file.close();

  // Mutate the source from inside the copy, via the progress callback.
  bool mutated = false;
  m_onProgress = [&]() {
    if (mutated) {
      return;
    }
    QFile rewrite(victim);
    if (rewrite.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      rewrite.write(QByteArray(2048, 'b'));
      rewrite.close();
      mutated = true;
    }
  };

  const auto result = share(QStringLiteral("Alpha"));
  m_onProgress = nullptr;
  QVERIFY(mutated);

  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testInjectedFailuresLeaveNoBundleOrTemp() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  for (const QString &stage :
       {QStringLiteral("copy"), QStringLiteral("verify"), QStringLiteral("publish")}) {
    m_controller->testSetFailureInjection(stage);

    const auto result = share(QStringLiteral("Alpha"));
    QVERIFY2(result.m_status == FolderSharePackager::Status::Failed, qPrintable(stage));
    QVERIFY2(!result.m_errorMessage.isEmpty(), qPrintable(stage));
    // No final bundle AND no hidden temp sibling survived.
    QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
  }

  m_controller->testSetFailureInjection(QString());
  QVERIFY(share(QStringLiteral("Alpha")).succeeded());
}

void TestFolderShareController::testCancellationPublishesNothing() {
  makeFolder(QStringLiteral("Alpha"));
  const QByteArray big(2 * 1024 * 1024, 'z');
  for (int i = 0; i < 4; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("big%1.bin").arg(i), big);
  }

  // Cancel as soon as the copy reports its first tick.
  m_cancelAfterTicks = 0;
  const auto result = share(QStringLiteral("Alpha"));
  m_cancelAfterTicks = -1;

  QCOMPARE(result.m_status, FolderSharePackager::Status::Cancelled);
  QVERIFY(result.m_bundlePath.isEmpty());
  // Cancellation publishes nothing and leaves no temp tree behind.
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

void TestFolderShareController::testProgressIsMonotonicAndBounded() {
  makeFolder(QStringLiteral("Alpha"));
  const QByteArray big(1024 * 1024, 'p');
  for (int i = 0; i < 3; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("big%1.bin").arg(i), big);
  }

  QVERIFY(share(QStringLiteral("Alpha")).succeeded());

  QVERIFY(!m_progressTicks.isEmpty());
  qint64 previous = -1;
  for (const auto &tick : m_progressTicks) {
    QVERIFY(tick.first >= 0);
    QVERIFY(tick.second > 0);
    QVERIFY2(tick.first <= tick.second, "progress must never exceed its total");
    QVERIFY2(tick.first >= previous, "progress must be monotonic");
    previous = tick.first;
  }

  // The caller saw a label for every phase it needs to render.
  QVERIFY(
      m_labels.contains(FolderShareController::phaseLabel(FolderSharePackager::Phase::Copying)));
  QVERIFY(
      m_labels.contains(FolderShareController::phaseLabel(FolderSharePackager::Phase::Verifying)));
}

// ---------------------------------------------------------------------------
// Save barrier
// ---------------------------------------------------------------------------

void TestFolderShareController::testModifiedNoteIsSavedUnderEveryPolicy() {
  const QVector<AutoSavePolicy> policies{AutoSavePolicy::None, AutoSavePolicy::AutoSave,
                                         AutoSavePolicy::BackupFile};
  int index = 0;
  for (AutoSavePolicy policy : policies) {
    m_buffers->setAutoSavePolicy(policy);

    const QString folder = QStringLiteral("Policy%1").arg(index++);
    makeFolder(folder);
    makeFile(folder, QStringLiteral("note.md"), "on disk");

    const QString relPath = folder + QStringLiteral("/note.md");
    Buffer2 buffer = m_buffers->openBuffer(NodeIdentifier{m_notebookId, relPath});
    QVERIFY(buffer.isValid());

    // An "editor" holding text the vxcore buffer does not have yet. Under
    // AutoSavePolicy::None this would NEVER reach the disk on its own.
    const QString editorText =
        QStringLiteral("edited under policy %1").arg(static_cast<int>(policy));
    m_buffers->registerActiveWriter(buffer.id(), 0x99, [editorText]() { return editorText; });
    m_buffers->markDirty(buffer.id());

    const auto result = share(folder);
    QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));

    // Both the real notebook file and the bundle carry the edited content.
    QCOMPARE(readAll(m_notebookPath + QLatin1Char('/') + relPath), editorText.toUtf8());
    QCOMPARE(readAll(result.m_bundlePath + QLatin1Char('/') + folder + QStringLiteral("/note.md")),
             editorText.toUtf8());
    QVERIFY(!m_buffers->isModified(buffer.id()));

    m_buffers->unregisterActiveWriter(buffer.id(), 0x99);
    m_buffers->closeBuffer(buffer.id());
  }
}

// A read-only notebook with an unsaved edit cannot be shared with up-to-date
// content, so the share must FAIL rather than silently bundle stale bytes.
void TestFolderShareController::testReadOnlyNotebookWithModifiedNoteFails() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "on disk");

  Buffer2 buffer =
      m_buffers->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("Alpha/note.md")});
  QVERIFY(buffer.isValid());
  QVERIFY(m_buffers->setContentRaw(buffer.id(), QByteArray("edited in memory")));
  QVERIFY(m_buffers->isModified(buffer.id()));

  QCOMPARE(vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), true),
           VXCORE_OK);

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
  // The on-disk bytes were never touched.
  QCOMPARE(readAll(m_notebookPath + QStringLiteral("/Alpha/note.md")), QByteArray("on disk"));

  vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  m_buffers->closeBuffer(buffer.id());
}

// A plugin cancelling vnote.file.before_save means the note cannot be made
// durable; publishing stale content would be worse than failing.
void TestFolderShareController::testSaveCancelledByHookFailsTheShare() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "on disk");

  Buffer2 buffer =
      m_buffers->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("Alpha/note.md")});
  QVERIFY(buffer.isValid());
  QVERIFY(m_buffers->setContentRaw(buffer.id(), QByteArray("edited in memory")));
  QVERIFY(m_buffers->isModified(buffer.id()));

  const int hookId = m_hookMgr->addAction<BufferEvent>(
      HookNames::FileBeforeSave, [](HookContext &p_ctx, const BufferEvent &) { p_ctx.cancel(); },
      10);

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
  QCOMPARE(readAll(m_notebookPath + QStringLiteral("/Alpha/note.md")), QByteArray("on disk"));

  m_hookMgr->removeAction(hookId);
  m_buffers->closeBuffer(buffer.id());
}

// An async auto-save the queue ALREADY holds owns an OLDER snapshot. If the
// barrier installed the editor's newer text before that worker ran, the worker
// would overwrite it and then leave the buffer unmodified — so the durability
// check would pass while the disk held stale bytes. The barrier must drain
// FIRST.
void TestFolderShareController::testPendingAutoSaveIsDrainedBeforeSnapshot() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "on disk");

  const QString relPath = QStringLiteral("Alpha/note.md");
  Buffer2 buffer = m_buffers->openBuffer(NodeIdentifier{m_notebookId, relPath});
  QVERIFY(buffer.isValid());

  // Queue an async save carrying the OLD text, exactly as an auto-save tick
  // would, then immediately hand the editor NEWER text.
  //
  // The gate is held from another thread first, so the queued worker is
  // GUARANTEED to still be pending when the share starts — that is what makes
  // this test actually exercise the drain-first path rather than racing a fast
  // worker that already finished.
  QAtomicInt release(0);
  QSemaphore acquired;
  NotebookIoGate *gate = m_gate;
  QFuture<void> holder = QtConcurrent::run([gate, this, &release, &acquired]() {
    NotebookIoGate::ScopedLock lock(*gate, m_notebookId);
    acquired.release();
    while (release.loadAcquire() == 0) {
      QThread::msleep(10);
    }
  });
  QVERIFY2(acquired.tryAcquire(1, 10000), "the holder thread never took the gate");

  m_buffers->setAutoSavePolicy(AutoSavePolicy::AutoSave);
  QString editorText = QStringLiteral("stale auto-saved text");
  m_buffers->registerActiveWriter(buffer.id(), 0x77, [&editorText]() { return editorText; });
  m_buffers->markDirty(buffer.id());
  m_buffers->syncNow(buffer.id()); // enqueues the stale snapshot
  // The worker is now parked on the gate, so the save really is outstanding.
  QVERIFY(m_buffers->isSaveQueueBusy(buffer.id()));

  editorText = QStringLiteral("the newest editor text");
  m_buffers->markDirty(buffer.id());

  // Let the stale worker run only once the share has begun draining.
  bool released = false;
  m_onProgress = [&]() {
    if (!released) {
      release.storeRelease(1);
      released = true;
    }
  };
  // The drain happens before any progress tick, so release it from a timer too.
  QTimer::singleShot(0, [&release]() { release.storeRelease(1); });

  const auto result = share(QStringLiteral("Alpha"));
  m_onProgress = nullptr;
  release.storeRelease(1);
  holder.waitForFinished();

  QVERIFY2(result.succeeded(), qPrintable(result.m_errorMessage));

  // The NEWEST text won, on disk and in the bundle. Before the drain-first fix
  // the stale worker snapshot could land last and be published.
  QCOMPARE(readAll(m_notebookPath + QLatin1Char('/') + relPath),
           QByteArray("the newest editor text"));
  QCOMPARE(readAll(result.m_bundlePath + QStringLiteral("/Alpha/note.md")),
           QByteArray("the newest editor text"));

  m_buffers->unregisterActiveWriter(buffer.id(), 0x77);
  m_buffers->closeBuffer(buffer.id());
}

// The share-triggered save must serialize against sync staging through
// NotebookIoGate. When another actor holds the gate the share must fail with a
// "try again" rather than write into someone else's staging window — and it
// must NOT block the GUI indefinitely waiting for it.
void TestFolderShareController::testGateHeldByAnotherActorFailsTheShare() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "on disk");

  Buffer2 buffer =
      m_buffers->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("Alpha/note.md")});
  QVERIFY(buffer.isValid());
  QVERIFY(m_buffers->setContentRaw(buffer.id(), QByteArray("edited in memory")));
  QVERIFY(m_buffers->isModified(buffer.id()));

  // Hold the gate for this notebook on another thread, as a sync stage would,
  // and do not proceed until the holder confirms it actually has it.
  QAtomicInt release(0);
  QSemaphore acquired;
  NotebookIoGate *gate = m_gate;
  QVERIFY(gate);
  QFuture<void> holder = QtConcurrent::run([gate, this, &release, &acquired]() {
    NotebookIoGate::ScopedLock lock(*gate, m_notebookId);
    acquired.release();
    while (release.loadAcquire() == 0) {
      QThread::msleep(10);
    }
  });
  QVERIFY2(acquired.tryAcquire(1, 10000), "the holder thread never took the gate");

  QElapsedTimer timer;
  timer.start();
  const auto result = share(QStringLiteral("Alpha"));
  const qint64 elapsed = timer.elapsed();

  release.storeRelease(1);
  holder.waitForFinished();

  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  // Bounded: it gave up rather than blocking the GUI forever.
  QVERIFY2(elapsed < 20000, qPrintable(QStringLiteral("waited %1ms on the gate").arg(elapsed)));
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
  // Nothing was written behind the gate holder's back.
  QCOMPARE(readAll(m_notebookPath + QStringLiteral("/Alpha/note.md")), QByteArray("on disk"));

  m_buffers->closeBuffer(buffer.id());
}

// The packager's progress callbacks pump the event loop, so a timer or queued
// signal can edit an open note AFTER the save barrier ran. Under
// AutoSavePolicy::None such an edit never reaches disk, so re-hashing the
// source cannot see it — only the controller's final precondition can.
void TestFolderShareController::testInMemoryEditDuringCopyFailsTheShare() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("note.md"), "on disk");
  // Enough bytes that the copy reports progress at least once.
  const QByteArray big(2 * 1024 * 1024, 'x');
  for (int i = 0; i < 2; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("big%1.bin").arg(i), big);
  }

  m_buffers->setAutoSavePolicy(AutoSavePolicy::None);
  Buffer2 buffer =
      m_buffers->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("Alpha/note.md")});
  QVERIFY(buffer.isValid());

  // Edit the note IN MEMORY from a progress tick, i.e. exactly when the modal
  // dialog would be pumping events.
  bool edited = false;
  m_onProgress = [&]() {
    if (edited) {
      return;
    }
    m_buffers->setContentRaw(buffer.id(), QByteArray("edited while copying"));
    m_buffers->markDirty(buffer.id());
    edited = true;
  };

  const auto result = share(QStringLiteral("Alpha"));
  m_onProgress = nullptr;
  QVERIFY(edited);

  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  // Nothing published: the bundle would have carried the pre-edit bytes.
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());

  m_buffers->closeBuffer(buffer.id());
}

// A note OPENED into the subtree after the barrier was never made durable, so
// its content may exist only in memory.
void TestFolderShareController::testNoteOpenedDuringCopyFailsTheShare() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("late.md"), "on disk");
  const QByteArray big(2 * 1024 * 1024, 'y');
  for (int i = 0; i < 2; ++i) {
    makeFile(QStringLiteral("Alpha"), QStringLiteral("big%1.bin").arg(i), big);
  }

  Buffer2 late;
  bool opened = false;
  m_onProgress = [&]() {
    if (opened) {
      return;
    }
    late = m_buffers->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("Alpha/late.md")});
    opened = late.isValid();
  };

  const auto result = share(QStringLiteral("Alpha"));
  m_onProgress = nullptr;
  QVERIFY(opened);

  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(!result.m_errorMessage.isEmpty());
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());

  m_buffers->closeBuffer(late.id());
}

void TestFolderShareController::testDuplicateMetadataIdRejected() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");
  makeFile(QStringLiteral("Alpha"), QStringLiteral("b.md"), "b");

  QJsonObject config = readConfig(QStringLiteral("Alpha"));
  QJsonArray files = config.value(QStringLiteral("files")).toArray();
  QCOMPARE(files.size(), 2);
  QJsonObject first = files.at(0).toObject();
  QJsonObject second = files.at(1).toObject();
  second[QStringLiteral("id")] = first.value(QStringLiteral("id"));
  files.replace(1, second);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);

  const auto result = share(QStringLiteral("Alpha"));
  QCOMPARE(result.m_status, FolderSharePackager::Status::Failed);
  QVERIFY(result.m_errorMessage.contains(QStringLiteral("Duplicate id")));
  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

// vxcore serializes timestamps as int64_t; a fractional value is not something
// the importer could round-trip.
void TestFolderShareController::testFractionalTimestampRejected() {
  makeFolder(QStringLiteral("Alpha"));
  makeFile(QStringLiteral("Alpha"), QStringLiteral("a.md"), "a");

  QJsonObject config = readConfig(QStringLiteral("Alpha"));
  QJsonArray files = config.value(QStringLiteral("files")).toArray();
  QJsonObject record = files.at(0).toObject();
  record[QStringLiteral("modifiedUtc")] = 1.5;
  files.replace(0, record);
  config[QStringLiteral("files")] = files;
  writeConfig(QStringLiteral("Alpha"), config);

  QCOMPARE(share(QStringLiteral("Alpha")).m_status, FolderSharePackager::Status::Failed);

  // The same rule applies to the FOLDER record.
  config = readConfig(QStringLiteral("Alpha"));
  config[QStringLiteral("createdUtc")] = 2.25;
  writeConfig(QStringLiteral("Alpha"), config);
  QCOMPARE(share(QStringLiteral("Alpha")).m_status, FolderSharePackager::Status::Failed);

  QCOMPARE(leftoverEntries(destination(), QString()), QStringList());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestFolderShareController)
#include "test_foldersharecontroller.moc"
