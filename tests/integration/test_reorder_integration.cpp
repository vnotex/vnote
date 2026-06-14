// T13 (notebook-explorer-drag-reorder): full E2E integration test for the
// reorder chain. No mocks: real NotebookCoreService, real vxcore, real disk
// writes. Six sub-tests cover the contract end-to-end:
//
//   1. testRootReorderUpdatesVxJson
//        - Reorder root folder children + files, wait for reorderCompleted,
//          read root vx.json from disk, verify both arrays.
//   2. testProxyByConfigShowsDiskOrder
//        - With proxy in OrderedByConfiguration, verify the source row order
//          (folders first, then files) matches the reordered disk order
//          (regression for the T3 fix).
//   3. testProxyByNameSortsAlphabetically
//        - With proxy in OrderedByName, verify alphabetical sort works
//          regardless of disk order.
//   4. testReorderPersistsAcrossRestart
//        - Reorder, destroy vxcore context, recreate vxcore context, reopen
//          notebook from the same disk path, verify the disk order survives.
//   5. testSubfolderReorderDoesNotTouchRoot
//        - Reorder children inside a subfolder; verify subfolder vx.json
//          updated, root vx.json mtime unchanged.
//   6. testReorderEmitsDirtyEventViaVxcore
//        - Subscribe to folder.config_changed via vxcore_on_event; verify
//          reorder fires both folder.config_changed and folder.children_reordered.
//
// NOT GUILESS — uses QApplication so QtConcurrent and the GUI-affine signal
// queue function correctly. Auto-cleanup via QTemporaryDir (TempDirFixture);
// vxcore_set_test_mode(1) protects real user AppData.

#include <atomic>

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QtTest>

#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include <temp_dir_fixture.h>

using namespace vnotex;

namespace tests {

namespace {

// Path inside a bundled notebook where the root folder's vx.json lives.
// Per libs/vxcore/src/core/bundled_folder_manager.cpp GetConfigPath:
//   <notebook_root>/vx_notebook/contents/[<folder>/]vx.json
QString rootVxJsonPath(const QString &p_notebookPath) {
  return p_notebookPath + QStringLiteral("/vx_notebook/contents/vx.json");
}

QString subfolderVxJsonPath(const QString &p_notebookPath, const QString &p_subfolder) {
  return p_notebookPath + QStringLiteral("/vx_notebook/contents/") + p_subfolder +
         QStringLiteral("/vx.json");
}

// Read + parse vx.json. Returns an empty object on failure.
QJsonObject readVxJson(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QJsonObject();
  }
  const QByteArray bytes = f.readAll();
  f.close();
  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError) {
    return QJsonObject();
  }
  return doc.object();
}

// Extract the "folders" array as a QStringList. vx.json schema:
//   "folders": ["sub_a", "sub_b", ...]   (array of strings)
QStringList foldersFromVxJson(const QJsonObject &p_obj) {
  QStringList out;
  for (const QJsonValue &v : p_obj.value(QStringLiteral("folders")).toArray()) {
    out.append(v.toString());
  }
  return out;
}

// Extract the "files" array as a QStringList of names. vx.json schema:
//   "files": [{"name": "file_a.md", ...}, ...]   (array of objects)
QStringList filesFromVxJson(const QJsonObject &p_obj) {
  QStringList out;
  for (const QJsonValue &v : p_obj.value(QStringLiteral("files")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

// Wrap NotebookCoreService::listFolderChildren into name-only QStringLists.
QStringList listChildFolders(NotebookCoreService &p_svc, const QString &p_notebookId,
                             const QString &p_folderPath = QString()) {
  QStringList out;
  const QJsonObject obj = p_svc.listFolderChildren(p_notebookId, p_folderPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("folders")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

QStringList listChildFiles(NotebookCoreService &p_svc, const QString &p_notebookId,
                           const QString &p_folderPath = QString()) {
  QStringList out;
  const QJsonObject obj = p_svc.listFolderChildren(p_notebookId, p_folderPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("files")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

} // namespace

class TestReorderIntegration : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testRootReorderUpdatesVxJson();
  void testProxyByConfigShowsDiskOrder();
  void testProxyByNameSortsAlphabetically();
  void testReorderPersistsAcrossRestart();
  void testSubfolderReorderDoesNotTouchRoot();
  void testReorderEmitsDirtyEventViaVxcore();

private:
  // Bring up a fresh vxcore context + the three services needed for reorder
  // (HookManager, NotebookIoGate, NotebookCoreService). Caller owns the
  // returned objects via the out-params; teardownContext() releases them in
  // the correct order.
  void setupContext();
  void teardownContext();

  // Seed the canonical T13 fixture inside the running context's m_tempDir:
  //   <baseName>/
  //     folder_b/  folder_a/  folder_c/
  //     file_c.md  file_a.md  file_b.md
  // Returns notebookId on success or empty string on failure.
  QString seedT13Notebook(const QString &p_baseName);

  TempDirFixture *m_tempDir = nullptr;
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  NotebookCoreService *m_service = nullptr;
};

void TestReorderIntegration::initTestCase() {
  // CRITICAL: must enable test mode BEFORE any vxcore_context_create() so the
  // notebook lives under %TEMP%\vxcore_test* instead of real user AppData.
  // Without this the test would corrupt the developer's real VNote config.
  vxcore_set_test_mode(1);
}

void TestReorderIntegration::cleanupTestCase() {
  // Per-test fixtures torn down in each test via teardownContext().
}

void TestReorderIntegration::setupContext() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_ioGate = new NotebookIoGate();
  m_hookMgr = new HookManager();
  m_service = new NotebookCoreService(m_context);
  m_service->setHookManager(m_hookMgr);
  m_service->setNotebookIoGate(m_ioGate);
}

void TestReorderIntegration::teardownContext() {
  delete m_service;
  m_service = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
  delete m_tempDir;
  m_tempDir = nullptr;
}

QString TestReorderIntegration::seedT13Notebook(const QString &p_baseName) {
  const QString nbPath = m_tempDir->filePath(p_baseName);
  const QString cfg = QStringLiteral("{\"name\":\"T13 Reorder\",\"version\":\"1\"}");
  const QString nbId = m_service->createNotebook(nbPath, cfg, NotebookType::Bundled);
  if (nbId.isEmpty()) {
    return QString();
  }

  // Creation order is deliberately NOT alphabetical so the post-reorder
  // alphabetical state is a distinct transition that the test asserts on.
  for (const QString &name : QStringList{QStringLiteral("folder_b"), QStringLiteral("folder_a"),
                                         QStringLiteral("folder_c")}) {
    if (m_service->createFolder(nbId, QString(), name).isEmpty()) {
      qWarning() << "seedT13Notebook: createFolder failed for" << name;
      return QString();
    }
  }
  for (const QString &name : QStringList{QStringLiteral("file_c.md"), QStringLiteral("file_a.md"),
                                         QStringLiteral("file_b.md")}) {
    if (m_service->createFile(nbId, QString(), name).isEmpty()) {
      qWarning() << "seedT13Notebook: createFile failed for" << name;
      return QString();
    }
  }
  return nbId;
}

// ============================================================================
// Test 1: Root reorder → vx.json on disk reflects new order.
// ============================================================================
void TestReorderIntegration::testRootReorderUpdatesVxJson() {
  setupContext();

  const QString baseName = QStringLiteral("nb_root");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());

  const QStringList newFolders{QStringLiteral("folder_a"), QStringLiteral("folder_b"),
                               QStringLiteral("folder_c")};
  const QStringList newFiles{QStringLiteral("file_a.md"), QStringLiteral("file_b.md"),
                             QStringLiteral("file_c.md")};

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY2(spy.wait(5000), "reorderCompleted signal never fired within 5s");
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), nbId);
  QCOMPARE(args.at(1).toString(), QString());
  QCOMPARE(args.at(2).toBool(), true);
  QCOMPARE(args.at(3).toString(), QString());

  // Disk-content verification — read vx.json via QFile + QJsonDocument.
  const QString vxJson = rootVxJsonPath(m_tempDir->filePath(baseName));
  QVERIFY2(QFile::exists(vxJson),
           qPrintable(QStringLiteral("vx.json not found at %1").arg(vxJson)));
  const QJsonObject obj = readVxJson(vxJson);
  QVERIFY(!obj.isEmpty());
  QCOMPARE(foldersFromVxJson(obj), newFolders);
  QCOMPARE(filesFromVxJson(obj), newFiles);

  // listFolderChildren must agree with disk.
  QCOMPARE(listChildFolders(*m_service, nbId), newFolders);
  QCOMPARE(listChildFiles(*m_service, nbId), newFiles);

  teardownContext();
}

// ============================================================================
// Test 2: Proxy in OrderedByConfiguration mirrors disk order (post-T3 fix).
// ============================================================================
void TestReorderIntegration::testProxyByConfigShowsDiskOrder() {
  setupContext();

  const QString baseName = QStringLiteral("nb_byconfig");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());

  // Reorder to alphabetical so the post-reorder disk order is unambiguously
  // distinct from the creation order seeded by seedT13Notebook.
  const QStringList newFolders{QStringLiteral("folder_a"), QStringLiteral("folder_b"),
                               QStringLiteral("folder_c")};
  const QStringList newFiles{QStringLiteral("file_a.md"), QStringLiteral("file_b.md"),
                             QStringLiteral("file_c.md")};
  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  // Build a real ServiceLocator with the real NotebookCoreService so the
  // model fetches the same on-disk state we just reordered.
  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_service);

  NotebookNodeModel model(services);
  model.setNotebookId(nbId);
  // Trigger the lazy fetch on the (invalid) absolute root so the children
  // populate before we read row counts via the proxy.
  QVERIFY(model.canFetchMore(QModelIndex()));
  model.fetchMore(QModelIndex());

  NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&model);
  proxy.setViewOrder(ViewOrder::OrderedByConfiguration);
  proxy.sort(0);

  // 3 folders + 3 files = 6 rows at the proxy root.
  QCOMPARE(proxy.rowCount(QModelIndex()), 6);

  // The proxy interleaves folders-first then files (see
  // NotebookNodeProxyModel::lessThan), so rows 0..2 are folders, rows 3..5
  // are files. Within each kind, OrderedByConfiguration honors source row
  // order, which IS the disk order from vxcore_folder_list_children.
  QStringList proxyFolders;
  for (int i = 0; i < 3; ++i) {
    proxyFolders.append(proxy.index(i, 0).data(Qt::DisplayRole).toString());
  }
  QStringList proxyFiles;
  for (int i = 3; i < 6; ++i) {
    proxyFiles.append(proxy.index(i, 0).data(Qt::DisplayRole).toString());
  }
  QCOMPARE(proxyFolders, newFolders);
  QCOMPARE(proxyFiles, newFiles);

  teardownContext();
}

// ============================================================================
// Test 3: Proxy in OrderedByName sorts alphabetically (regression for T3).
// ============================================================================
void TestReorderIntegration::testProxyByNameSortsAlphabetically() {
  setupContext();

  const QString baseName = QStringLiteral("nb_byname");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());
  // Note: do NOT reorder; we want to exercise OrderedByName on the original
  // creation order (folder_b, folder_a, folder_c / file_c.md, file_a.md,
  // file_b.md) so the proxy's sort is genuinely doing work.

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_service);

  NotebookNodeModel model(services);
  model.setNotebookId(nbId);
  QVERIFY(model.canFetchMore(QModelIndex()));
  model.fetchMore(QModelIndex());

  NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&model);
  proxy.setViewOrder(ViewOrder::OrderedByName);
  proxy.sort(0);

  QCOMPARE(proxy.rowCount(QModelIndex()), 6);

  // Folders alphabetical, then files alphabetical (kind ordering is enforced
  // before the per-kind name comparison).
  const QStringList expectedFolders{QStringLiteral("folder_a"), QStringLiteral("folder_b"),
                                    QStringLiteral("folder_c")};
  const QStringList expectedFiles{QStringLiteral("file_a.md"), QStringLiteral("file_b.md"),
                                  QStringLiteral("file_c.md")};
  QStringList proxyFolders;
  for (int i = 0; i < 3; ++i) {
    proxyFolders.append(proxy.index(i, 0).data(Qt::DisplayRole).toString());
  }
  QStringList proxyFiles;
  for (int i = 3; i < 6; ++i) {
    proxyFiles.append(proxy.index(i, 0).data(Qt::DisplayRole).toString());
  }
  QCOMPARE(proxyFolders, expectedFolders);
  QCOMPARE(proxyFiles, expectedFiles);

  teardownContext();
}

// ============================================================================
// Test 4: Reorder survives full vxcore context destroy/recreate cycle.
//         This is the critical persistence guarantee — proves the order is
//         on-disk truth, not in-memory state.
// ============================================================================
void TestReorderIntegration::testReorderPersistsAcrossRestart() {
  setupContext();

  const QString baseName = QStringLiteral("nb_restart");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());

  const QStringList newFolders{QStringLiteral("folder_a"), QStringLiteral("folder_b"),
                               QStringLiteral("folder_c")};
  const QStringList newFiles{QStringLiteral("file_a.md"), QStringLiteral("file_b.md"),
                             QStringLiteral("file_c.md")};
  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  // Snapshot the temp-dir path so we can keep using it after teardown. We
  // CANNOT just call teardownContext() (it deletes m_tempDir along with the
  // QTemporaryDir, which would wipe the notebook). Tear down only the
  // service/context layers; reuse the same on-disk path.
  const QString nbPath = m_tempDir->filePath(baseName);

  // Close the notebook + destroy vxcore context. After this the notebook is
  // purely on-disk state.
  QVERIFY(m_service->closeNotebook(nbId));
  delete m_service;
  m_service = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  vxcore_context_destroy(m_context);
  m_context = nullptr;

  // Recreate vxcore context + services. This simulates an application
  // restart — fresh memory, only disk state preserved.
  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_ioGate = new NotebookIoGate();
  m_hookMgr = new HookManager();
  m_service = new NotebookCoreService(m_context);
  m_service->setHookManager(m_hookMgr);
  m_service->setNotebookIoGate(m_ioGate);

  // Re-open the notebook from the same disk path.
  const QString reopenedId = m_service->openNotebook(nbPath);
  QVERIFY2(!reopenedId.isEmpty(),
           qPrintable(QStringLiteral("Failed to reopen notebook at %1").arg(nbPath)));

  // Order must be preserved across the restart.
  QCOMPARE(listChildFolders(*m_service, reopenedId), newFolders);
  QCOMPARE(listChildFiles(*m_service, reopenedId), newFiles);

  // Disk content must still be correct too.
  const QJsonObject obj = readVxJson(rootVxJsonPath(nbPath));
  QVERIFY(!obj.isEmpty());
  QCOMPARE(foldersFromVxJson(obj), newFolders);
  QCOMPARE(filesFromVxJson(obj), newFiles);

  teardownContext();
}

// ============================================================================
// Test 5: Subfolder reorder updates only the subfolder's vx.json.
//         Root vx.json mtime must be unchanged.
// ============================================================================
void TestReorderIntegration::testSubfolderReorderDoesNotTouchRoot() {
  setupContext();

  const QString baseName = QStringLiteral("nb_subfolder");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());

  // Create 3 files inside folder_a/.
  const QString subRel = QStringLiteral("folder_a");
  for (const QString &name :
       QStringList{QStringLiteral("nested_c.md"), QStringLiteral("nested_a.md"),
                   QStringLiteral("nested_b.md")}) {
    QVERIFY2(!m_service->createFile(nbId, subRel, name).isEmpty(),
             qPrintable(QStringLiteral("createFile in subfolder failed: %1").arg(name)));
  }

  // Snapshot root vx.json mtime BEFORE the subfolder reorder so we can prove
  // the root file was untouched. We use the parent directory containing
  // vx.json + a small sleep to make the mtime change detectable if the bug
  // were present (Windows FILETIME granularity is ~10ms but FAT/exFAT can be
  // 2s — temp dir is NTFS in CI so this is safe).
  const QString rootVx = rootVxJsonPath(m_tempDir->filePath(baseName));
  QFileInfo rootInfo(rootVx);
  QVERIFY(rootInfo.exists());
  const QDateTime rootMtimeBefore = rootInfo.lastModified();
  // Small wait so any (incorrect) write would produce a detectable mtime
  // delta. Without this the test could silently pass on filesystems with
  // 1s mtime granularity if both writes happened in the same second.
  QThread::msleep(1100);

  const QStringList newSubFiles{QStringLiteral("nested_a.md"), QStringLiteral("nested_b.md"),
                                QStringLiteral("nested_c.md")};
  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, subRel, QStringList(), newSubFiles);
  QVERIFY(spy.wait(5000));
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), nbId);
  QCOMPARE(args.at(1).toString(), subRel);
  QCOMPARE(args.at(2).toBool(), true);

  // Subfolder vx.json updated.
  const QJsonObject subObj = readVxJson(subfolderVxJsonPath(m_tempDir->filePath(baseName), subRel));
  QVERIFY(!subObj.isEmpty());
  QCOMPARE(filesFromVxJson(subObj), newSubFiles);

  // Root vx.json mtime UNCHANGED.
  QFileInfo rootInfoAfter(rootVx);
  rootInfoAfter.refresh();
  QCOMPARE(rootInfoAfter.lastModified(), rootMtimeBefore);

  // And the root order is also unchanged from creation order.
  QCOMPARE(listChildFolders(*m_service, nbId),
           (QStringList{QStringLiteral("folder_b"), QStringLiteral("folder_a"),
                        QStringLiteral("folder_c")}));

  teardownContext();
}

// ============================================================================
// Test 6: Reorder fires the two events that auto-sync subscribes to:
//   - folder.config_changed (umbrella; SyncManager::mark_dirty)
//   - folder.children_reordered (structural; richer UI consumers)
// vxcore does not expose a "dirty notebook" predicate via the public C ABI
// (verified: grep on libs/vxcore/include for "dirty" returns zero hits), so
// the equivalent contract is verified through the event bus.
// ============================================================================

namespace {
struct EventCounter {
  std::atomic<int> configChanged{0};
  std::atomic<int> childrenReordered{0};
  QString lastNotebookId; // touched only from event thread, then read after wait.
};

void onEventThunk(const char *event_name, const char *json_data, void *userdata) {
  auto *counter = static_cast<EventCounter *>(userdata);
  if (!event_name || !json_data || !counter) {
    return;
  }
  const QByteArray nameBytes(event_name);
  if (nameBytes == "folder.config_changed") {
    counter->configChanged.fetch_add(1);
  } else if (nameBytes == "folder.children_reordered") {
    counter->childrenReordered.fetch_add(1);
  }
  // Capture notebookId from the payload for the most recent event for debug.
  QJsonParseError err{};
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_data), &err);
  if (err.error == QJsonParseError::NoError && doc.isObject()) {
    counter->lastNotebookId = doc.object().value(QStringLiteral("notebookId")).toString();
  }
}
} // namespace

void TestReorderIntegration::testReorderEmitsDirtyEventViaVxcore() {
  setupContext();

  const QString baseName = QStringLiteral("nb_events");
  const QString nbId = seedT13Notebook(baseName);
  QVERIFY(!nbId.isEmpty());

  EventCounter counter;
  QCOMPARE(vxcore_on_event(m_context, "folder.config_changed", &onEventThunk, &counter), VXCORE_OK);
  QCOMPARE(vxcore_on_event(m_context, "folder.children_reordered", &onEventThunk, &counter),
           VXCORE_OK);

  const QStringList newFolders{QStringLiteral("folder_a"), QStringLiteral("folder_b"),
                               QStringLiteral("folder_c")};
  const QStringList newFiles{QStringLiteral("file_a.md"), QStringLiteral("file_b.md"),
                             QStringLiteral("file_c.md")};

  QSignalSpy spy(m_service, &NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  // Both events must have fired at least once. The reorder rewrites the root
  // folder's vx.json exactly once, so we expect counts of 1+ (allowing for
  // future schema changes that could legitimately fan out additional events).
  QVERIFY2(counter.childrenReordered.load() >= 1,
           qPrintable(QStringLiteral("folder.children_reordered did not fire; count=%1")
                          .arg(counter.childrenReordered.load())));
  QVERIFY2(counter.configChanged.load() >= 1,
           qPrintable(QStringLiteral("folder.config_changed did not fire; count=%1")
                          .arg(counter.configChanged.load())));
  QCOMPARE(counter.lastNotebookId, nbId);

  // Unsubscribe to keep the test hygienic — the context destroy in
  // teardownContext() also tears down the bus, but explicit unsubscribe is a
  // good citizenship pattern.
  vxcore_off_event(m_context, "folder.config_changed", &onEventThunk);
  vxcore_off_event(m_context, "folder.children_reordered", &onEventThunk);

  teardownContext();
}

} // namespace tests

QTEST_MAIN(tests::TestReorderIntegration)
#include "test_reorder_integration.moc"
