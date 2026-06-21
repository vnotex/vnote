// Qt-layer integration coverage for the "missing files on disk" feature.
//
// This is the SHARED Qt test file for the missing-nodes-on-disk feature. T7
// seeds the buffer-open surfacing path; later tasks (T8/T9/T11) EXTEND this
// same target with their own subtests.
//
// T7 scope: verify the OPTIONAL `VxCoreError *p_outErr` out-param threaded
// through BufferCoreService::openBuffer and BufferService::openBuffer surfaces
// VXCORE_ERR_NODE_NOT_EXISTS when a bundled, indexed node's CONTENT has been
// deleted from disk (the node's metadata / vx.json survives, so vxcore still
// resolves the node type and the reactive missing-content gate fires).

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/notebooknodecontroller.h>
#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffercoreservice.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <models/inodelistmodel.h>
#include <models/notebooknodemodel.h>
#include <temp_dir_fixture.h>
#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestMissingNodesQt : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // BufferCoreService::openBuffer out-param.
  void testCoreOpenBufferOutParamSuccess();
  void testCoreOpenBufferOutParamMissingNode();
  void testCoreOpenBufferTwoArgBackwardCompat();

  // BufferService::openBuffer out-param (hook-aware wrapper).
  void testServiceOpenBufferOutParamSuccess();
  void testServiceOpenBufferOutParamMissingNode();
  void testServiceOpenBufferTwoArgBackwardCompat();

  // NotebookCoreService::getFileInfo out-param (T8).
  void testGetFileInfoMissingNode();

  // NotebookCoreService::listFolderChildren per-child exists flag (T8).
  void testListFolderChildrenMarksMissing();

  // NotebookCoreService::listFolderChildren folder-level missing code (T8).
  void testListFolderChildrenMissingFolder();

  // NotebookNodeModel IsMissingRole + missingNodesDetected signal (T9).
  void testModelIsMissingRolePopulated();
  void testModelIsMissingClearsOnReappear();
  void testModelEmitsMissingNodesDetected();

  // NotebookNodeController phantom detection + batch removal (T11).
  void testControllerPreflightEmitsAndSuppressesActivation();
  void testControllerModelSignalReemits();
  void testControllerSuppression();
  void testControllerRevalidateSkipsReappeared();
  void testControllerFolderUnindexCascades();

private:
  // Create a fresh file in the notebook, then delete its on-disk content while
  // leaving the index/metadata intact (so the node becomes a "phantom").
  // Returns the relative path of the created file.
  QString makePhantomFile(const QString &p_fileName);

  // Same as makePhantomFile but the file is created inside p_parentPath (a
  // notebook-relative folder). Returns the relative path of the created file
  // (e.g. "f/a.md"), or an empty string on any failure.
  QString makePhantomFileIn(const QString &p_parentPath, const QString &p_fileName);

  // Create a folder (with one indexed child file), then delete the folder's
  // on-disk content directory recursively while leaving the index/metadata
  // intact (so the folder becomes a "phantom"). Returns the folder's relative
  // path, or an empty string on any failure.
  QString makePhantomFolder(const QString &p_folderName);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferCoreService *m_coreService = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferService = nullptr;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestMissingNodesQt::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // QSignalSpy needs these registered to capture the missingNodesDetected
  // payload (a QList<NodeIdentifier>).
  qRegisterMetaType<vnotex::NodeIdentifier>("vnotex::NodeIdentifier");
  qRegisterMetaType<QList<vnotex::NodeIdentifier>>("QList<vnotex::NodeIdentifier>");

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_coreService = new BufferCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::AutoSave, this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("missing_nodes_qt_nb"));
  QString configJson =
      QStringLiteral(R"({"name": "Missing Nodes Qt", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestMissingNodesQt::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_coreService;
  m_coreService = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestMissingNodesQt::cleanup() {
  // Close all open buffers between tests so reopen never dedups to a cached one.
  const QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    const QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

QString TestMissingNodesQt::makePhantomFile(const QString &p_fileName) {
  return makePhantomFileIn(QString(), p_fileName);
}

QString TestMissingNodesQt::makePhantomFileIn(const QString &p_parentPath,
                                              const QString &p_fileName) {
  const QString fileId = m_notebookService->createFile(m_notebookId, p_parentPath, p_fileName);
  if (fileId.isEmpty()) {
    return QString();
  }

  const QString relPath =
      p_parentPath.isEmpty() ? p_fileName : (p_parentPath + QLatin1Char('/') + p_fileName);

  // Resolve the absolute ON-DISK content path (<root>/<relativePath>). The
  // node metadata lives elsewhere (<root>/vx_notebook/contents/<rel>/vx.json)
  // and is intentionally left intact so vxcore still resolves the node type.
  const QString contentPath = m_coreService->getResolvedPath(m_notebookId, relPath);
  if (contentPath.isEmpty() || !QFile::exists(contentPath)) {
    return QString();
  }
  if (!QFile::remove(contentPath)) {
    return QString();
  }
  if (QFile::exists(contentPath)) {
    return QString();
  }
  return relPath;
}

QString TestMissingNodesQt::makePhantomFolder(const QString &p_folderName) {
  const QString folderId = m_notebookService->createFolder(m_notebookId, QString(), p_folderName);
  if (folderId.isEmpty()) {
    return QString();
  }

  // Give the folder an indexed child so its metadata is non-trivial; the child
  // content is deleted along with the folder's content directory below.
  const QString childId =
      m_notebookService->createFile(m_notebookId, p_folderName, QStringLiteral("child.md"));
  if (childId.isEmpty()) {
    return QString();
  }

  // Delete ONLY the folder's on-disk content directory. The folder index lives
  // at <root>/vx_notebook/contents/<folder>/vx.json and survives, so vxcore
  // still resolves the folder type and the reactive missing-content gate fires.
  const QString contentPath = m_coreService->getResolvedPath(m_notebookId, p_folderName);
  if (contentPath.isEmpty() || !QDir(contentPath).exists()) {
    return QString();
  }
  if (!QDir(contentPath).removeRecursively()) {
    return QString();
  }
  if (QDir(contentPath).exists()) {
    return QString();
  }
  return p_folderName;
}

// ============ BufferCoreService::openBuffer ============

void TestMissingNodesQt::testCoreOpenBufferOutParamSuccess() {
  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("core_ok.md"));
  QVERIFY(!fileId.isEmpty());

  VxCoreError outErr = VXCORE_ERR_UNKNOWN;
  const QString bufferId =
      m_coreService->openBuffer(m_notebookId, QStringLiteral("core_ok.md"), &outErr);
  QVERIFY(!bufferId.isEmpty());
  QCOMPARE(outErr, VXCORE_OK);
}

void TestMissingNodesQt::testCoreOpenBufferOutParamMissingNode() {
  const QString rel = makePhantomFile(QStringLiteral("core_phantom.md"));
  QVERIFY2(!rel.isEmpty(), "Failed to create phantom file (delete on-disk content)");

  VxCoreError outErr = VXCORE_OK;
  const QString bufferId = m_coreService->openBuffer(m_notebookId, rel, &outErr);
  QVERIFY2(bufferId.isEmpty(), "Buffer open must FAIL for a node whose content is gone");
  QCOMPARE(outErr, VXCORE_ERR_NODE_NOT_EXISTS);
}

void TestMissingNodesQt::testCoreOpenBufferTwoArgBackwardCompat() {
  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("core_compat.md"));
  QVERIFY(!fileId.isEmpty());

  // Existing two-arg callers must still compile and behave as before.
  const QString bufferId =
      m_coreService->openBuffer(m_notebookId, QStringLiteral("core_compat.md"));
  QVERIFY(!bufferId.isEmpty());
}

// ============ BufferService::openBuffer (hook-aware) ============

void TestMissingNodesQt::testServiceOpenBufferOutParamSuccess() {
  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("svc_ok.md"));
  QVERIFY(!fileId.isEmpty());

  VxCoreError outErr = VXCORE_ERR_UNKNOWN;
  Buffer2 buf = m_bufferService->openBuffer(
      NodeIdentifier{m_notebookId, QStringLiteral("svc_ok.md")}, FileOpenSettings(), &outErr);
  QVERIFY(buf.isValid());
  QCOMPARE(outErr, VXCORE_OK);
}

void TestMissingNodesQt::testServiceOpenBufferOutParamMissingNode() {
  const QString rel = makePhantomFile(QStringLiteral("svc_phantom.md"));
  QVERIFY2(!rel.isEmpty(), "Failed to create phantom file (delete on-disk content)");

  VxCoreError outErr = VXCORE_OK;
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, rel}, FileOpenSettings(), &outErr);
  QVERIFY2(!buf.isValid(), "BufferService must return an invalid Buffer2 for a missing node");
  QCOMPARE(outErr, VXCORE_ERR_NODE_NOT_EXISTS);
}

void TestMissingNodesQt::testServiceOpenBufferTwoArgBackwardCompat() {
  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("svc_compat.md"));
  QVERIFY(!fileId.isEmpty());

  // Existing one/two-arg callers must still compile and behave as before.
  Buffer2 buf =
      m_bufferService->openBuffer(NodeIdentifier{m_notebookId, QStringLiteral("svc_compat.md")});
  QVERIFY(buf.isValid());
}

// ============ NotebookCoreService::getFileInfo (T8) ============

void TestMissingNodesQt::testGetFileInfoMissingNode() {
  // An intact file -> VXCORE_OK and a non-empty config object.
  const QString fileId =
      m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("info_ok.md"));
  QVERIFY(!fileId.isEmpty());

  VxCoreError okErr = VXCORE_ERR_UNKNOWN;
  const QJsonObject okInfo =
      m_notebookService->getFileInfo(m_notebookId, QStringLiteral("info_ok.md"), &okErr);
  QCOMPARE(okErr, VXCORE_OK);
  QVERIFY(!okInfo.isEmpty());

  // A bundled file whose on-disk content was deleted (metadata survives) ->
  // VXCORE_ERR_NODE_NOT_EXISTS and an empty object.
  const QString rel = makePhantomFile(QStringLiteral("info_phantom.md"));
  QVERIFY2(!rel.isEmpty(), "Failed to create phantom file (delete on-disk content)");

  VxCoreError missErr = VXCORE_OK;
  const QJsonObject missInfo = m_notebookService->getFileInfo(m_notebookId, rel, &missErr);
  QCOMPARE(missErr, VXCORE_ERR_NODE_NOT_EXISTS);
  QVERIFY(missInfo.isEmpty());
}

// ============ NotebookCoreService::listFolderChildren (T8) ============

void TestMissingNodesQt::testListFolderChildrenMarksMissing() {
  const QString folderId =
      m_notebookService->createFolder(m_notebookId, QString(), QStringLiteral("f"));
  QVERIFY(!folderId.isEmpty());

  // "f/a.md": phantom (content deleted); "f/b.md": intact.
  const QString relA = makePhantomFileIn(QStringLiteral("f"), QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child a.md");
  const QString bId =
      m_notebookService->createFile(m_notebookId, QStringLiteral("f"), QStringLiteral("b.md"));
  QVERIFY(!bId.isEmpty());

  VxCoreError err = VXCORE_ERR_UNKNOWN;
  const QJsonObject children =
      m_notebookService->listFolderChildren(m_notebookId, QStringLiteral("f"), &err);
  QCOMPARE(err, VXCORE_OK);

  const QJsonArray files = children.value(QStringLiteral("files")).toArray();
  bool sawA = false;
  bool sawB = false;
  for (const QJsonValue &v : files) {
    const QJsonObject child = v.toObject();
    const QString name = child.value(QStringLiteral("name")).toString();
    const QString existsKey = QString::fromUtf8(vxcore::kJsonKeyNodeExists);
    if (name == QStringLiteral("a.md")) {
      sawA = true;
      QVERIFY2(child.contains(existsKey), "phantom child missing the 'exists' field");
      QCOMPARE(child.value(existsKey).toBool(), false);
    } else if (name == QStringLiteral("b.md")) {
      sawB = true;
      QVERIFY2(child.contains(existsKey), "healthy child missing the 'exists' field");
      QCOMPARE(child.value(existsKey).toBool(), true);
    }
  }
  QVERIFY2(sawA, "phantom child a.md absent from listing (must be MARKED, not filtered)");
  QVERIFY2(sawB, "healthy child b.md absent from listing");
}

void TestMissingNodesQt::testListFolderChildrenMissingFolder() {
  const QString rel = makePhantomFolder(QStringLiteral("g"));
  QVERIFY2(!rel.isEmpty(), "Failed to create phantom folder (delete on-disk content dir)");

  VxCoreError err = VXCORE_OK;
  const QJsonObject children = m_notebookService->listFolderChildren(m_notebookId, rel, &err);
  QCOMPARE(err, VXCORE_ERR_NODE_NOT_EXISTS);
  QVERIFY(children.isEmpty());
}

// ============ NotebookNodeModel IsMissingRole + signal (T9) ============

void TestMissingNodesQt::testModelIsMissingRolePopulated() {
  const QString folder = QStringLiteral("m_role");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());

  // "m_role/a.md": phantom (content deleted); "m_role/b.md": intact.
  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child a.md");
  const QString bId = m_notebookService->createFile(m_notebookId, folder, QStringLiteral("b.md"));
  QVERIFY(!bId.isEmpty());

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);

  // Expand the root so child folders gain valid indices; this also prefetches
  // each child folder's children (populating their NodeInfo, incl. isMissing).
  model.fetchMore(QModelIndex());

  const QModelIndex aIdx = model.indexFromNodeId(NodeIdentifier{m_notebookId, relA});
  QVERIFY2(aIdx.isValid(), "phantom child index not found in model");
  QCOMPARE(model.data(aIdx, INodeListModel::IsMissingRole).toBool(), true);

  const QModelIndex bIdx =
      model.indexFromNodeId(NodeIdentifier{m_notebookId, folder + QStringLiteral("/b.md")});
  QVERIFY2(bIdx.isValid(), "healthy child index not found in model");
  QCOMPARE(model.data(bIdx, INodeListModel::IsMissingRole).toBool(), false);
}

void TestMissingNodesQt::testModelIsMissingClearsOnReappear() {
  const QString folder = QStringLiteral("m_reappear");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());

  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child a.md");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);

  // Prime the hierarchy (root + prefetched folder children).
  model.fetchMore(QModelIndex());
  QModelIndex aIdx = model.indexFromNodeId(NodeIdentifier{m_notebookId, relA});
  QVERIFY(aIdx.isValid());
  QCOMPARE(model.data(aIdx, INodeListModel::IsMissingRole).toBool(), true);

  // Recreate the child's on-disk content. The index/metadata never went away,
  // so the next listing's transient "exists" flag flips back to true.
  const QString contentPath = m_coreService->getResolvedPath(m_notebookId, relA);
  QVERIFY(!contentPath.isEmpty());
  {
    QFile f(contentPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# back\n");
    f.close();
  }
  QVERIFY(QFile::exists(contentPath));

  model.reloadNode(NodeIdentifier{m_notebookId, folder});
  aIdx = model.indexFromNodeId(NodeIdentifier{m_notebookId, relA});
  QVERIFY(aIdx.isValid());
  QCOMPARE(model.data(aIdx, INodeListModel::IsMissingRole).toBool(), false);
}

void TestMissingNodesQt::testModelEmitsMissingNodesDetected() {
  const QString folder = QStringLiteral("m_emit");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());
  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child a.md");

  const QString phantomFolder = makePhantomFolder(QStringLiteral("m_emit_folder"));
  QVERIFY2(!phantomFolder.isEmpty(), "Failed to create phantom folder");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);

  // Prime root first (this initial listing may itself emit for the accumulated
  // root-level phantoms; attach the spy AFTER so we observe only our triggers).
  model.fetchMore(QModelIndex());

  QSignalSpy spy(&model, &NotebookNodeModel::missingNodesDetected);
  QVERIFY(spy.isValid());

  // (1) Re-listing a folder with a phantom child emits with that child's id.
  model.reloadNode(NodeIdentifier{m_notebookId, folder});
  QCOMPARE(spy.count(), 1);
  {
    const auto ids = spy.takeFirst().at(0).value<QList<NodeIdentifier>>();
    bool sawChild = false;
    for (const NodeIdentifier &id : ids) {
      if (id.relativePath == relA) {
        sawChild = true;
      }
    }
    QVERIFY2(sawChild, "phantom child id absent from missingNodesDetected payload");
  }

  // (2) Listing a deleted folder emits with the folder's own id.
  spy.clear();
  model.reloadNode(NodeIdentifier{m_notebookId, phantomFolder});
  QCOMPARE(spy.count(), 1);
  {
    const auto ids = spy.takeFirst().at(0).value<QList<NodeIdentifier>>();
    bool sawFolder = false;
    for (const NodeIdentifier &id : ids) {
      if (id.relativePath == phantomFolder) {
        sawFolder = true;
      }
    }
    QVERIFY2(sawFolder, "phantom folder id absent from missingNodesDetected payload");
  }
}

// ============ NotebookNodeController phantom detection + removal (T11) ============

void TestMissingNodesQt::testControllerPreflightEmitsAndSuppressesActivation() {
  const QString folder = QStringLiteral("pf");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());

  QVERIFY(!m_notebookService->createFile(m_notebookId, folder, QStringLiteral("ok.md")).isEmpty());
  const QString okRel = folder + QStringLiteral("/ok.md");
  const QString badRel = makePhantomFileIn(folder, QStringLiteral("bad.md"));
  QVERIFY2(!badRel.isEmpty(), "Failed to create phantom file");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);
  NotebookNodeController controller(services);
  controller.setModel(&model);

  // Prime the hierarchy so getNodeInfo() resolves the file nodes (and so the
  // controller's model-signal wiring is primed before we observe).
  model.fetchMore(QModelIndex());

  int activatedCount = 0;
  QList<NodeIdentifier> activatedIds;
  int removalEmits = 0;
  QList<NodeIdentifier> removalIds;
  QObject::connect(&controller, &NotebookNodeController::nodeActivated, &controller,
                   [&](const NodeIdentifier &id, const FileOpenSettings &) {
                     ++activatedCount;
                     activatedIds.append(id);
                   });
  QObject::connect(&controller, &NotebookNodeController::missingNodeRemovalRequested, &controller,
                   [&](const QList<NodeIdentifier> &ids) {
                     ++removalEmits;
                     removalIds = ids;
                   });

  // (1) Opening a PHANTOM file: removal requested, NO activation.
  controller.openNode(NodeIdentifier{m_notebookId, badRel});
  QCOMPARE(removalEmits, 1);
  QCOMPARE(removalIds.size(), 1);
  QCOMPARE(removalIds.first().relativePath, badRel);
  QCOMPARE(activatedCount, 0);

  // (2) Opening an INTACT file: activation, NO new removal request.
  controller.openNode(NodeIdentifier{m_notebookId, okRel});
  QCOMPARE(activatedCount, 1);
  QCOMPARE(activatedIds.first().relativePath, okRel);
  QCOMPARE(removalEmits, 1); // unchanged
}

void TestMissingNodesQt::testControllerModelSignalReemits() {
  const QString folder = QStringLiteral("ms");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());
  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);
  NotebookNodeController controller(services);
  controller.setModel(&model);
  model.fetchMore(QModelIndex());

  int removalEmits = 0;
  QList<NodeIdentifier> removalIds;
  QObject::connect(&controller, &NotebookNodeController::missingNodeRemovalRequested, &controller,
                   [&](const QList<NodeIdentifier> &ids) {
                     ++removalEmits;
                     removalIds = ids;
                   });

  // The model's missingNodesDetected (emitted during the folder re-list) must
  // be re-surfaced by the controller as missingNodeRemovalRequested.
  model.reloadNode(NodeIdentifier{m_notebookId, folder});
  QCOMPARE(removalEmits, 1);
  bool sawChild = false;
  for (const NodeIdentifier &id : removalIds) {
    if (id.relativePath == relA) {
      sawChild = true;
    }
  }
  QVERIFY2(sawChild, "phantom child id absent from missingNodeRemovalRequested payload");
}

void TestMissingNodesQt::testControllerSuppression() {
  const QString folder = QStringLiteral("sup");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());
  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  QVERIFY2(!relA.isEmpty(), "Failed to create phantom child");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);
  NotebookNodeController controller(services);
  controller.setModel(&model);
  model.fetchMore(QModelIndex());

  // Decline (suppress) the only phantom child BEFORE detection fires.
  controller.suppressMissingNodes({NodeIdentifier{m_notebookId, relA}});

  int removalEmits = 0;
  QList<NodeIdentifier> removalIds;
  QObject::connect(&controller, &NotebookNodeController::missingNodeRemovalRequested, &controller,
                   [&](const QList<NodeIdentifier> &ids) {
                     ++removalEmits;
                     removalIds = ids;
                   });

  model.reloadNode(NodeIdentifier{m_notebookId, folder});
  // Suppressed -> the controller must NOT re-emit for relA.
  for (const NodeIdentifier &id : removalIds) {
    QVERIFY2(id.relativePath != relA, "suppressed id must not be re-emitted");
  }
  QCOMPARE(removalEmits, 0);
}

void TestMissingNodesQt::testControllerRevalidateSkipsReappeared() {
  const QString folder = QStringLiteral("rev");
  QVERIFY(!m_notebookService->createFolder(m_notebookId, QString(), folder).isEmpty());
  const QString relA = makePhantomFileIn(folder, QStringLiteral("a.md"));
  const QString relB = makePhantomFileIn(folder, QStringLiteral("b.md"));
  QVERIFY2(!relA.isEmpty() && !relB.isEmpty(), "Failed to create phantom children");

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);
  NotebookNodeController controller(services);
  controller.setModel(&model);
  model.fetchMore(QModelIndex());

  // A "reappears" on disk between detection and confirmation; B stays missing.
  const QString contentA = m_coreService->getResolvedPath(m_notebookId, relA);
  QVERIFY(!contentA.isEmpty());
  {
    QFile f(contentA);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# back\n");
    f.close();
  }
  QVERIFY(QFile::exists(contentA));

  controller.handleMissingRemovalConfirmed(
      {NodeIdentifier{m_notebookId, relA}, NodeIdentifier{m_notebookId, relB}});

  // A revalidated to VXCORE_OK -> SKIPPED -> still indexed and resolvable.
  VxCoreError errA = VXCORE_ERR_UNKNOWN;
  m_notebookService->getFileInfo(m_notebookId, relA, &errA);
  QCOMPARE(errA, VXCORE_OK);

  // B was unindexed -> dropped from the parent listing; A remains listed.
  VxCoreError listErr = VXCORE_ERR_UNKNOWN;
  const QJsonObject children =
      m_notebookService->listFolderChildren(m_notebookId, folder, &listErr);
  QCOMPARE(listErr, VXCORE_OK);
  const QJsonArray files = children.value(QStringLiteral("files")).toArray();
  bool sawA = false;
  bool sawB = false;
  for (const QJsonValue &v : files) {
    const QString name = v.toObject().value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("a.md")) {
      sawA = true;
    } else if (name == QStringLiteral("b.md")) {
      sawB = true;
    }
  }
  QVERIFY2(sawA, "reappeared file a.md must remain indexed (revalidation skipped it)");
  QVERIFY2(!sawB, "phantom file b.md must be unindexed");
}

void TestMissingNodesQt::testControllerFolderUnindexCascades() {
  // casc/ with an indexed descendant subtree: casc/sub/deep.md.
  QVERIFY(
      !m_notebookService->createFolder(m_notebookId, QString(), QStringLiteral("casc")).isEmpty());
  QVERIFY(
      !m_notebookService->createFolder(m_notebookId, QStringLiteral("casc"), QStringLiteral("sub"))
           .isEmpty());
  QVERIFY(!m_notebookService
               ->createFile(m_notebookId, QStringLiteral("casc/sub"), QStringLiteral("deep.md"))
               .isEmpty());

  // Sanity: the descendant subtree is reachable before removal.
  VxCoreError preErr = VXCORE_ERR_UNKNOWN;
  m_notebookService->listFolderChildren(m_notebookId, QStringLiteral("casc/sub"), &preErr);
  QCOMPARE(preErr, VXCORE_OK);

  // Delete casc's on-disk content directory (casc, casc/sub, deep.md) but keep
  // the metadata so casc becomes a phantom folder.
  const QString contentCasc = m_coreService->getResolvedPath(m_notebookId, QStringLiteral("casc"));
  QVERIFY(!contentCasc.isEmpty() && QDir(contentCasc).exists());
  QVERIFY(QDir(contentCasc).removeRecursively());

  ServiceLocator services;
  services.registerService<NotebookCoreService>(m_notebookService);
  NotebookNodeModel model(services);
  model.setNotebookId(m_notebookId);
  NotebookNodeController controller(services);
  controller.setModel(&model);
  model.fetchMore(QModelIndex());

  // Confirm removal of the missing folder. Unindexing a folder cascades.
  controller.handleMissingRemovalConfirmed({NodeIdentifier{m_notebookId, QStringLiteral("casc")}});

  // casc no longer appears in the root listing...
  VxCoreError rootErr = VXCORE_ERR_UNKNOWN;
  const QJsonObject root = m_notebookService->listFolderChildren(m_notebookId, QString(), &rootErr);
  QCOMPARE(rootErr, VXCORE_OK);
  const QJsonArray folders = root.value(QStringLiteral("folders")).toArray();
  for (const QJsonValue &v : folders) {
    QVERIFY2(v.toObject().value(QStringLiteral("name")).toString() != QStringLiteral("casc"),
             "unindexed folder casc must be gone from the root listing");
  }

  // ...and its subtree is no longer reachable (the unindex cascaded).
  VxCoreError subErr = VXCORE_OK;
  m_notebookService->listFolderChildren(m_notebookId, QStringLiteral("casc/sub"), &subErr);
  QVERIFY2(subErr != VXCORE_OK, "casc/sub must be unreachable after the folder unindex cascades");
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMissingNodesQt)
#include "test_missing_nodes_qt.moc"
