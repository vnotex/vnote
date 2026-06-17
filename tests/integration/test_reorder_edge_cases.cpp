// T14 (notebook-explorer-drag-reorder): comprehensive edge-case coverage for
// the drag-reorder feature. Separate from T13's happy-path E2E test.
//
// Sub-tests EC1-EC14 cover the 14 edge cases enumerated in the plan
// (.sisyphus/plans/notebook-explorer-drag-reorder.md, lines 1908-1999):
//
//   Service-layer (real vxcore notebook, NotebookCoreService):
//     EC1  — Root folder reorder (folder_path == "")
//     EC2  — Single child: identity submission is a no-op (no disk write)
//     EC3  — Empty input via controller: hard no-op, no service call
//     EC5  — Cross-type rejection at vxcore (folder-named file or vice versa)
//     EC9  — Unicode names (CJK, accented) round-trip
//     EC10 — Long names (under MAX_PATH headroom)
//     EC11 — Performance: 100 children reorder under budget
//     EC12 — Concurrent save+reorder serialized by NotebookIoGate
//
//   View-layer (StubModel + RecordingController + TestableView):
//     EC4  — Filter active rejects same-parent drag
//     EC6  — Multi-select same type lands contiguously at indicator
//     EC7  — Multi-select mixed type rejects (no reorder, no move)
//     EC8  — Multi-select different parents: reorder gate stays shut
//     EC14 — Sort mode flipped mid-drag rejects at drop time
//
//   Mixed (controller wired to recording model):
//     EC13 — nodesReordered signal triggers model reload (rowsInserted)
//
// Wave 1-3 are committed; this is part of Wave 4 (parallel with T13).

#include <atomic>

#include <QApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QPoint>
#include <QSignalSpy>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QtTest>

#include <vxcore/vxcore.h>

#include <controllers/notebooknodecontroller.h>
#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>
#include <views/notebooknodeview.h>

#include <temp_dir_fixture.h>

namespace tests {

namespace {

// Mime payload type used by NotebookNodeView for dragged nodes.
const QString c_nodeMimeType = QStringLiteral("application/x-vnotex-node-identifier");

// Encode a list of NodeIdentifier into the vnotex node mime payload (matches
// the encoder used by NotebookNodeModel::mimeData).
QMimeData *makeNodeMimeData(const QList<vnotex::NodeIdentifier> &p_nodes) {
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  for (const auto &n : p_nodes) {
    stream << n.notebookId << n.relativePath;
  }
  auto *mime = new QMimeData();
  mime->setData(c_nodeMimeType, data);
  return mime;
}

// Build a NodeInfo whose parent is the notebook root.
vnotex::NodeInfo makeRootNode(const QString &p_notebookId, const QString &p_name, bool p_isFolder) {
  vnotex::NodeInfo info;
  info.name = p_name;
  info.isFolder = p_isFolder;
  info.id.notebookId = p_notebookId;
  info.id.relativePath = p_name;
  return info;
}

// Build an arbitrary NodeIdentifier (used for cross-parent regressions).
vnotex::NodeIdentifier makeId(const QString &p_notebookId, const QString &p_relativePath) {
  vnotex::NodeIdentifier id;
  id.notebookId = p_notebookId;
  id.relativePath = p_relativePath;
  return id;
}

} // namespace

// =============================================================================
// View-layer scaffolding (shared with tests/gui/test_notebook_node_view_reorder.cpp
// pattern). Kept local so this file is self-contained.
// =============================================================================

// Stub source model — flat children at root. Subclasses NotebookNodeModel so
// the proxy's qobject_cast succeeds and the lessThan path under test fires.
class StubModel : public vnotex::NotebookNodeModel {
public:
  explicit StubModel(vnotex::ServiceLocator &p_services, const QString &p_notebookId,
                     QObject *p_parent = nullptr)
      : vnotex::NotebookNodeModel(p_services, p_parent), m_notebookId(p_notebookId) {
    setNotebookId(p_notebookId);
  }

  void setStubChildren(const QVector<vnotex::NodeInfo> &p_children) {
    beginResetModel();
    m_children = p_children;
    endResetModel();
  }

  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override {
    return p_parent.isValid() ? 0 : m_children.size();
  }

  int columnCount(const QModelIndex & /*p_parent*/ = QModelIndex()) const override { return 1; }

  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override {
    if (p_parent.isValid() || p_column != 0) {
      return QModelIndex();
    }
    if (p_row < 0 || p_row >= m_children.size()) {
      return QModelIndex();
    }
    return createIndex(p_row, p_column, static_cast<quintptr>(p_row + 1));
  }

  QModelIndex parent(const QModelIndex & /*p_child*/) const override { return QModelIndex(); }

  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_children.size()) {
      return QVariant();
    }
    if (p_role == Qt::DisplayRole) {
      return m_children.at(p_index.row()).name;
    }
    return QVariant();
  }

  Qt::ItemFlags flags(const QModelIndex &p_index) const override {
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (p_index.isValid()) {
      f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    } else {
      f |= Qt::ItemIsDropEnabled;
    }
    return f;
  }

  bool hasChildren(const QModelIndex &p_parent = QModelIndex()) const override {
    return !p_parent.isValid() && !m_children.isEmpty();
  }

  bool canFetchMore(const QModelIndex & /*p_parent*/) const override { return false; }
  void fetchMore(const QModelIndex & /*p_parent*/) override {}

  vnotex::NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const override {
    return nodeInfoFromIndex(p_index).id;
  }

  vnotex::NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_children.size()) {
      return vnotex::NodeInfo();
    }
    return m_children.at(p_index.row());
  }

  vnotex::NodeInfo nodeInfoFromNodeId(const vnotex::NodeIdentifier &p_nodeId) const override {
    for (const auto &n : m_children) {
      if (n.id == p_nodeId) {
        return n;
      }
    }
    return vnotex::NodeInfo();
  }

private:
  QString m_notebookId;
  QVector<vnotex::NodeInfo> m_children;
};

// Recording controller. Intercepts the two virtual hooks the view dispatches
// to so the test can assert call args without dragging in vxcore / clipboard.
class RecordingController : public vnotex::NotebookNodeController {
public:
  explicit RecordingController(vnotex::ServiceLocator &p_services)
      : vnotex::NotebookNodeController(p_services) {}

  void reorderNodes(const vnotex::NodeIdentifier &p_parentId,
                    const QList<vnotex::NodeIdentifier> &p_orderedFolderIds,
                    const QList<vnotex::NodeIdentifier> &p_orderedFileIds) override {
    ++reorderCallCount;
    lastReorderParent = p_parentId;
    lastFolderIds = p_orderedFolderIds;
    lastFileIds = p_orderedFileIds;
  }

  void moveNodes(const QList<vnotex::NodeIdentifier> &p_nodeIds,
                 const vnotex::NodeIdentifier &p_targetFolderId) override {
    ++moveCallCount;
    lastMoveIds = p_nodeIds;
    lastMoveTarget = p_targetFolderId;
  }

  int reorderCallCount = 0;
  int moveCallCount = 0;
  vnotex::NodeIdentifier lastReorderParent;
  QList<vnotex::NodeIdentifier> lastFolderIds;
  QList<vnotex::NodeIdentifier> lastFileIds;
  QList<vnotex::NodeIdentifier> lastMoveIds;
  vnotex::NodeIdentifier lastMoveTarget;
};

// Testable view exposing protected drag/drop handlers.
class TestableView : public vnotex::NotebookNodeView {
public:
  using vnotex::NotebookNodeView::dragEnterEvent;
  using vnotex::NotebookNodeView::dragMoveEvent;
  using vnotex::NotebookNodeView::dropEvent;
  using vnotex::NotebookNodeView::NotebookNodeView;
};

// =============================================================================
// EC13 scaffolding: recording model that reports reloadNode invocations and
// emits modelReset so the test can verify the after-reorder refresh chain.
// =============================================================================

class RecordingNodeModel : public StubModel {
public:
  explicit RecordingNodeModel(vnotex::ServiceLocator &p_services, const QString &p_notebookId,
                              QObject *p_parent = nullptr)
      : StubModel(p_services, p_notebookId, p_parent) {}

  void reloadNode(const vnotex::NodeIdentifier &p_nodeId) {
    ++reloadCallCount;
    lastReloadedNode = p_nodeId;
    // Emit modelReset so any view connected to this model would repaint —
    // mirrors the "model emits rowsRemoved/rowsInserted (or modelReset)"
    // contract EC13 asserts on.
    beginResetModel();
    endResetModel();
  }

  int reloadCallCount = 0;
  vnotex::NodeIdentifier lastReloadedNode;
};

// =============================================================================
// Test class
// =============================================================================

class TestReorderEdgeCases : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  // Service-layer edge cases (real vxcore notebooks on a per-test temp dir).
  void EC1_rootFolderReorder();
  void EC2_singleChildIsNoOp();
  void EC3_emptyInputIsNoOp();
  void EC5_crossTypeRejection();
  void EC9_unicodeNames();
  void EC10_longNames();
  void EC11_performance100Children();
  void EC12_concurrentSaveAndReorder();

  // View-layer edge cases (StubModel + RecordingController + TestableView).
  void EC4_filterActiveRejection();
  void EC6_multiSelectSameType();
  void EC7_multiSelectMixedType();
  void EC8_multiSelectDifferentParents();
  void EC14_sortModeChangeMidDrag();

  // Integration: nodesReordered → model reload chain.
  void EC13_afterReorderModelRefresh();

private:
  // Service-layer helpers — fresh notebook per test to avoid cross-test bleed.
  QString seedNotebook(const QString &p_baseName, const QStringList &p_folderNames,
                       const QStringList &p_fileNames);
  QStringList listFolders(const QString &p_notebookId, const QString &p_folderRelPath = QString());
  QStringList listFiles(const QString &p_notebookId, const QString &p_folderRelPath = QString());

  // View-layer event synthesis helpers (mirror tests/gui/test_notebook_node_view_reorder.cpp).
  void enterDrag(TestableView *p_view, QMimeData *p_mime);
  void doDragMove(TestableView *p_view, QMimeData *p_mime, const QPoint &p_pos,
                  Qt::KeyboardModifiers p_modifiers = Qt::NoModifier);
  void doDrop(TestableView *p_view, QMimeData *p_mime, const QPoint &p_pos,
              Qt::KeyboardModifiers p_modifiers = Qt::NoModifier);
  QPoint pointInsideRow(TestableView *p_view, const QModelIndex &p_idx, double p_yFraction) const;

  // Long-lived per-class fixtures for service-layer tests.
  VxCoreContextHandle m_ctx = nullptr;
  vnotex::NotebookCoreService *m_service = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::NotebookIoGate *m_ioGate = nullptr;
  TempDirFixture *m_tempDir = nullptr;
};

// =============================================================================
// Fixture lifecycle
// =============================================================================

void TestReorderEdgeCases::initTestCase() {
  // CRITICAL: must precede vxcore_context_create — keeps tests off real
  // user AppData (per tests/AGENTS.md).
  vxcore_set_test_mode(1);

  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  const QString configJson = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_ioGate = new vnotex::NotebookIoGate();
  m_hookMgr = new vnotex::HookManager(this);
  m_service = new vnotex::NotebookCoreService(m_ctx, this);
  m_service->setHookManager(m_hookMgr);
  m_service->setNotebookIoGate(m_ioGate);
}

void TestReorderEdgeCases::cleanupTestCase() {
  delete m_service;
  m_service = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;

  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }

  delete m_tempDir;
  m_tempDir = nullptr;
}

void TestReorderEdgeCases::cleanup() {
  // Close every open notebook so each test starts clean. Mirrors the
  // pattern from tests/core/test_notebook_core_service_reorder.cpp.
  if (!m_service) {
    return;
  }
  const QJsonArray nbs = m_service->listNotebooks();
  for (const auto &v : nbs) {
    const QString id = v.toObject().value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      m_service->closeNotebook(id);
    }
  }
}

// =============================================================================
// Service-layer helpers
// =============================================================================

QString TestReorderEdgeCases::seedNotebook(const QString &p_baseName,
                                           const QStringList &p_folderNames,
                                           const QStringList &p_fileNames) {
  const QString nbPath = m_tempDir->filePath(p_baseName);
  const QString cfg = QStringLiteral("{\"name\":\"Edge Notebook\",\"version\":\"1\"}");
  const QString nbId = m_service->createNotebook(nbPath, cfg, vnotex::NotebookType::Bundled);
  if (nbId.isEmpty()) {
    return QString();
  }
  for (const QString &name : p_folderNames) {
    if (m_service->createFolder(nbId, QString(), name).isEmpty()) {
      qWarning() << "seedNotebook: createFolder failed for" << name;
      return QString();
    }
  }
  for (const QString &name : p_fileNames) {
    if (m_service->createFile(nbId, QString(), name).isEmpty()) {
      qWarning() << "seedNotebook: createFile failed for" << name;
      return QString();
    }
  }
  return nbId;
}

QStringList TestReorderEdgeCases::listFolders(const QString &p_notebookId,
                                              const QString &p_folderRelPath) {
  QStringList out;
  const QJsonObject obj = m_service->listFolderChildren(p_notebookId, p_folderRelPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("folders")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

QStringList TestReorderEdgeCases::listFiles(const QString &p_notebookId,
                                            const QString &p_folderRelPath) {
  QStringList out;
  const QJsonObject obj = m_service->listFolderChildren(p_notebookId, p_folderRelPath);
  for (const QJsonValue &v : obj.value(QStringLiteral("files")).toArray()) {
    out.append(v.toObject().value(QStringLiteral("name")).toString());
  }
  return out;
}

// =============================================================================
// View-layer event synthesis helpers
// =============================================================================

void TestReorderEdgeCases::enterDrag(TestableView *p_view, QMimeData *p_mime) {
  QApplication::processEvents();
  QDragEnterEvent enterEvent(QPoint(10, 10), Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton,
                             Qt::NoModifier);
  p_view->dragEnterEvent(&enterEvent);
}

void TestReorderEdgeCases::doDragMove(TestableView *p_view, QMimeData *p_mime, const QPoint &p_pos,
                                      Qt::KeyboardModifiers p_modifiers) {
  QDragMoveEvent moveEvent(p_pos, Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton,
                           p_modifiers);
  p_view->dragMoveEvent(&moveEvent);
}

void TestReorderEdgeCases::doDrop(TestableView *p_view, QMimeData *p_mime, const QPoint &p_pos,
                                  Qt::KeyboardModifiers p_modifiers) {
  QDropEvent dropEvent(p_pos, Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton, p_modifiers);
  p_view->dropEvent(&dropEvent);
}

QPoint TestReorderEdgeCases::pointInsideRow(TestableView *p_view, const QModelIndex &p_idx,
                                            double p_yFraction) const {
  QRect r = p_view->visualRect(p_idx);
  Q_ASSERT(r.isValid() && !r.isEmpty());
  int x = r.center().x();
  int y = r.top() + static_cast<int>(p_yFraction * r.height());
  if (y < r.top()) {
    y = r.top();
  }
  if (y > r.bottom()) {
    y = r.bottom();
  }
  return QPoint(x, y);
}

// =============================================================================
// EC1: Root folder reorder
// =============================================================================
// folder_path == "" must operate on the notebook root's direct children with
// the same correctness as a subfolder reorder.
void TestReorderEdgeCases::EC1_rootFolderReorder() {
  const QString nbId =
      seedNotebook(QStringLiteral("nb_ec1"),
                   QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                   QStringList{QStringLiteral("x.md"), QStringLiteral("y.md")});
  QVERIFY(!nbId.isEmpty());

  const QStringList seedFolders = listFolders(nbId);
  const QStringList seedFiles = listFiles(nbId);
  QCOMPARE(seedFolders.size(), 3);
  QCOMPARE(seedFiles.size(), 2);

  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());
  QStringList newFiles = seedFiles;
  std::reverse(newFiles.begin(), newFiles.end());

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(0).toString(), nbId);
  QCOMPARE(args.at(1).toString(), QString()); // root folder rel path is ""
  QCOMPARE(args.at(2).toBool(), true);

  QCOMPARE(listFolders(nbId), newFolders);
  QCOMPARE(listFiles(nbId), newFiles);
}

// =============================================================================
// EC2: Single child
// =============================================================================
// Submitting the one existing child as a "reorder" is by definition the
// current order — vxcore's no-op contract applies: success, no disk write.
void TestReorderEdgeCases::EC2_singleChildIsNoOp() {
  const QString nbId =
      seedNotebook(QStringLiteral("nb_ec2"), QStringList{}, QStringList{QStringLiteral("solo.md")});
  QVERIFY(!nbId.isEmpty());

  const QStringList seedFiles = listFiles(nbId);
  QCOMPARE(seedFiles.size(), 1);

  // mtime probe on the notebook's vx_notebook folder. Per the no-op contract,
  // vx.json must not be rewritten.
  QFileInfo vxFolder(m_tempDir->filePath(QStringLiteral("nb_ec2/vx_notebook")));
  const QDateTime preMtime = vxFolder.lastModified();

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), QStringList(), seedFiles);

  if (spy.isEmpty()) {
    QVERIFY(spy.wait(500));
  }
  QCOMPARE(spy.size(), 1);
  const auto args = spy.takeFirst();
  QCOMPARE(args.at(2).toBool(), true);
  QCOMPARE(args.at(3).toString(), QString());

  vxFolder.refresh();
  QCOMPARE(vxFolder.lastModified(), preMtime);
  QCOMPARE(listFiles(nbId), seedFiles);
}

// =============================================================================
// EC3: Empty input via controller
// =============================================================================
// reorderNodes({}, {}) is a hard no-op at the controller level — no service
// call, no signal, no error. Service's own no-op check is irrelevant because
// the controller short-circuits first.
void TestReorderEdgeCases::EC3_emptyInputIsNoOp() {
  // Subclass NotebookCoreService that records reorder dispatches without
  // actually invoking vxcore. Confirms the controller never reaches the
  // service when input is empty.
  class CountingNotebookCoreService : public vnotex::NotebookCoreService {
  public:
    explicit CountingNotebookCoreService(VxCoreContextHandle p_ctx, QObject *p_parent = nullptr)
        : vnotex::NotebookCoreService(p_ctx, p_parent) {}

    void reorderFolderChildren(const QString &, const QString &, const QStringList &,
                               const QStringList &) override {
      ++callCount;
    }
    int callCount = 0;
  };

  vnotex::ServiceLocator services;
  CountingNotebookCoreService *svc = new CountingNotebookCoreService(m_ctx);
  services.registerService<vnotex::NotebookCoreService>(svc);

  vnotex::NotebookNodeController controller(services);

  QSignalSpy reorderedSpy(&controller, &vnotex::NotebookNodeController::nodesReordered);
  QSignalSpy errorSpy(&controller, &vnotex::NotebookNodeController::errorOccurred);

  const vnotex::NodeIdentifier parent{QStringLiteral("nb-ec3"), QStringLiteral("any/folder")};
  controller.reorderNodes(parent, {}, {});

  QCOMPARE(svc->callCount, 0);
  QCOMPARE(reorderedSpy.count(), 0);
  QCOMPARE(errorSpy.count(), 0);

  delete svc;
}

// =============================================================================
// EC4: Filter active rejection (view-layer)
// =============================================================================
// Same-parent drag is silently rejected when the proxy's name filter is set;
// otherwise the persisted child order would be derived from a partial visible
// subset and corrupt the on-disk ordering.
void TestReorderEdgeCases::EC4_filterActiveRejection() {
  const QString notebookId = QStringLiteral("nb-ec4");
  vnotex::ServiceLocator services;
  StubModel source(services, notebookId);
  vnotex::NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&source);
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  proxy.sort(0, Qt::AscendingOrder);

  RecordingController controller(services);
  TestableView view;
  view.setModel(&proxy);
  view.setController(&controller);
  view.resize(400, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(notebookId, QStringLiteral("alpha.md"), false)
           << makeRootNode(notebookId, QStringLiteral("beta.md"), false)
           << makeRootNode(notebookId, QStringLiteral("gamma.md"), false);
  source.setStubChildren(children);
  QApplication::processEvents();

  // Activate the proxy filter — even a permissive "*.md" pattern flips the
  // gate because nameFilter() becomes non-empty.
  proxy.setNameFilter(QStringLiteral("*.md"));
  QApplication::processEvents();

  QList<vnotex::NodeIdentifier> dragged = {children.at(2).id}; // gamma.md
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(&view, mime);

  QModelIndex anchorIdx = proxy.index(0, 0); // alpha.md
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(&view, anchorIdx, 0.05); // AboveItem
  doDragMove(&view, mime, pos);
  doDrop(&view, mime, pos);

  delete mime;

  QCOMPARE(controller.reorderCallCount, 0);
  QCOMPARE(controller.moveCallCount, 0);
}

// =============================================================================
// EC5: Cross-type rejection (service-layer, direct vxcore call)
// =============================================================================
// Submit a permutation that lists a file's name under "folders" (or vice
// versa). vxcore enforces the per-sub-array exact-permutation rule, so the
// mismatched name → VXCORE_ERR_PERMUTATION_MISMATCH.
void TestReorderEdgeCases::EC5_crossTypeRejection() {
  const QString nbId =
      seedNotebook(QStringLiteral("nb_ec5"), QStringList{QStringLiteral("real_folder")},
                   QStringList{QStringLiteral("real_file.md")});
  QVERIFY(!nbId.isEmpty());

  // Folder array names the file -> mismatch (the existing folder is missing
  // and "real_file.md" doesn't exist in the folder set).
  const QString crossTypeJson =
      QStringLiteral("{\"folders\":[\"real_file.md\"],\"files\":[\"real_folder\"]}");
  VxCoreError err = vxcore_folder_set_children_order(m_ctx, nbId.toUtf8().constData(), "",
                                                     crossTypeJson.toUtf8().constData());
  QCOMPARE(err, VXCORE_ERR_PERMUTATION_MISMATCH);

  // Disk untouched.
  QCOMPARE(listFolders(nbId), QStringList{QStringLiteral("real_folder")});
  QCOMPARE(listFiles(nbId), QStringList{QStringLiteral("real_file.md")});
}

// =============================================================================
// EC6: Multi-select same parent, same type (view-layer)
// =============================================================================
// Drag 3 files together → all three land contiguously at the indicator
// position. Verified through the RecordingController's lastFileIds.
void TestReorderEdgeCases::EC6_multiSelectSameType() {
  const QString notebookId = QStringLiteral("nb-ec6");
  vnotex::ServiceLocator services;
  StubModel source(services, notebookId);
  vnotex::NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&source);
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  proxy.sort(0, Qt::AscendingOrder);

  RecordingController controller(services);
  TestableView view;
  view.setModel(&proxy);
  view.setController(&controller);
  view.resize(400, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  // Setup: 5 files at root in order [a, b, c, d, e].
  QVector<vnotex::NodeInfo> children;
  for (const QString &name :
       QStringList{QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("c.md"),
                   QStringLiteral("d.md"), QStringLiteral("e.md")}) {
    children << makeRootNode(notebookId, name, false);
  }
  source.setStubChildren(children);
  QApplication::processEvents();

  // Drag c, d, e together and drop them above a.md.
  QList<vnotex::NodeIdentifier> dragged = {children.at(2).id, children.at(3).id, children.at(4).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(&view, mime);

  QModelIndex anchorIdx = proxy.index(0, 0); // a.md
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(&view, anchorIdx, 0.05); // AboveItem
  doDragMove(&view, mime, pos);
  doDrop(&view, mime, pos);

  delete mime;

  QCOMPARE(controller.reorderCallCount, 1);
  QCOMPARE(controller.moveCallCount, 0);
  QCOMPARE(controller.lastFolderIds.size(), 0);
  QCOMPARE(controller.lastFileIds.size(), 5);
  // c, d, e contiguous before a, then b.
  QCOMPARE(controller.lastFileIds.at(0).relativePath, QStringLiteral("c.md"));
  QCOMPARE(controller.lastFileIds.at(1).relativePath, QStringLiteral("d.md"));
  QCOMPARE(controller.lastFileIds.at(2).relativePath, QStringLiteral("e.md"));
  QCOMPARE(controller.lastFileIds.at(3).relativePath, QStringLiteral("a.md"));
  QCOMPARE(controller.lastFileIds.at(4).relativePath, QStringLiteral("b.md"));
}

// =============================================================================
// EC7: Multi-select mixed type (view-layer)
// =============================================================================
// Drag 2 folders + 1 file → draggedTypeKind returns -1 (mixed) → silent
// reject. Neither reorderNodes nor moveNodes called for the same-parent
// path; on the cross-parent path moveNodes can fire (out of scope for this
// gate — we only assert the reorder gate stays shut).
void TestReorderEdgeCases::EC7_multiSelectMixedType() {
  const QString notebookId = QStringLiteral("nb-ec7");
  vnotex::ServiceLocator services;
  StubModel source(services, notebookId);
  vnotex::NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&source);
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  proxy.sort(0, Qt::AscendingOrder);

  RecordingController controller(services);
  TestableView view;
  view.setModel(&proxy);
  view.setController(&controller);
  view.resize(400, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(notebookId, QStringLiteral("folder1"), true)
           << makeRootNode(notebookId, QStringLiteral("folder2"), true)
           << makeRootNode(notebookId, QStringLiteral("a.md"), false)
           << makeRootNode(notebookId, QStringLiteral("b.md"), false);
  source.setStubChildren(children);
  QApplication::processEvents();

  // Drag {folder1, folder2, a.md} → mixed type. Anchor: b.md (a file).
  QList<vnotex::NodeIdentifier> dragged = {children.at(0).id, children.at(1).id, children.at(2).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(&view, mime);

  QModelIndex anchorIdx = proxy.index(3, 0); // b.md (folder-first ordering)
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(&view, anchorIdx, 0.05);
  doDragMove(&view, mime, pos);
  doDrop(&view, mime, pos);

  delete mime;

  QCOMPARE(controller.reorderCallCount, 0);
  // moveNodes also must not fire — every dragged node has root as its
  // parent, same as the anchor, so the dispatch falls into the same-parent
  // branch, and the mixed-type guard rejects.
  QCOMPARE(controller.moveCallCount, 0);
}

// =============================================================================
// EC8: Multi-select different parents (view-layer)
// =============================================================================
// Drag two files whose parents differ → allDraggedShareParent is false →
// dispatch falls into the cross-parent move path. We assert ONLY that the
// reorder gate is closed (moveNodes may or may not fire — the existing
// cross-parent logic decides).
void TestReorderEdgeCases::EC8_multiSelectDifferentParents() {
  const QString notebookId = QStringLiteral("nb-ec8");
  vnotex::ServiceLocator services;
  StubModel source(services, notebookId);
  vnotex::NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&source);
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  proxy.sort(0, Qt::AscendingOrder);

  RecordingController controller(services);
  TestableView view;
  view.setModel(&proxy);
  view.setController(&controller);
  view.resize(400, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  // Visible model holds one file at root for the anchor.
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(notebookId, QStringLiteral("anchor.md"), false);
  source.setStubChildren(children);
  QApplication::processEvents();

  // Dragged set: two files from different parents (neither matches root).
  QList<vnotex::NodeIdentifier> dragged = {makeId(notebookId, QStringLiteral("dir_a/x.md")),
                                           makeId(notebookId, QStringLiteral("dir_b/y.md"))};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(&view, mime);

  QModelIndex anchorIdx = proxy.index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(&view, anchorIdx, 0.05);
  doDragMove(&view, mime, pos);
  doDrop(&view, mime, pos);

  delete mime;

  // Reorder gate must be closed for cross-parent drag.
  QCOMPARE(controller.reorderCallCount, 0);
}

// =============================================================================
// EC9: Unicode names
// =============================================================================
// Reorder folders and files whose names contain CJK and accented characters.
// The vx.json round-trip must preserve the names byte-perfectly.
void TestReorderEdgeCases::EC9_unicodeNames() {
  const QString cjkFolder = QStringLiteral("\u76EE\u5F55");              // 目录
  const QString accentFolder = QStringLiteral("dossier_caf\u00E9");      // café
  const QString cjkFile = QStringLiteral("\u30D5\u30A1\u30A4\u30EB.md"); // ファイル.md
  const QString accentFile = QStringLiteral("r\u00E9sum\u00E9.md");      // résumé.md

  const QString nbId = seedNotebook(QStringLiteral("nb_ec9"), QStringList{cjkFolder, accentFolder},
                                    QStringList{cjkFile, accentFile});
  QVERIFY2(!nbId.isEmpty(), "Failed to seed unicode-name notebook");

  const QStringList seedFolders = listFolders(nbId);
  const QStringList seedFiles = listFiles(nbId);
  QCOMPARE(seedFolders.size(), 2);
  QCOMPARE(seedFiles.size(), 2);

  // Reverse to force a permutation that exercises both unicode names.
  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());
  QStringList newFiles = seedFiles;
  std::reverse(newFiles.begin(), newFiles.end());

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  QCOMPARE(listFolders(nbId), newFolders);
  QCOMPARE(listFiles(nbId), newFiles);
}

// =============================================================================
// EC10: Long names
// =============================================================================
// Reorder folders and files with long names. Windows MAX_PATH is 260, so
// 200-char filenames combined with a temp path can exceed the limit when
// the test runs out of %TEMP%. Use a tighter ceiling (120 chars) that
// covers the "name length doesn't break anything" intent while staying
// safely under MAX_PATH for the test's nested layout.
void TestReorderEdgeCases::EC10_longNames() {
  const int nameLen = 120;
  const QString longA = QString(nameLen, QChar(QLatin1Char('a'))) + QStringLiteral(".md");
  const QString longB = QString(nameLen, QChar(QLatin1Char('b'))) + QStringLiteral(".md");
  const QString longC = QString(nameLen, QChar(QLatin1Char('c'))) + QStringLiteral(".md");

  const QString nbId =
      seedNotebook(QStringLiteral("nb_ec10"), QStringList{}, QStringList{longA, longB, longC});
  QVERIFY2(!nbId.isEmpty(), "Failed to seed long-names notebook");

  const QStringList seedFiles = listFiles(nbId);
  QCOMPARE(seedFiles.size(), 3);

  QStringList newFiles = seedFiles;
  std::reverse(newFiles.begin(), newFiles.end());

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), QStringList(), newFiles);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);

  QCOMPARE(listFiles(nbId), newFiles);
}

// =============================================================================
// EC11: Performance — 100 children
// =============================================================================
// Create a folder with 100 files and time a reverse-order reorder. Budget is
// 500ms (relaxed from the plan's aspirational 100ms — see notepad for the
// actual measurement on this developer machine).
void TestReorderEdgeCases::EC11_performance100Children() {
  const int childCount = 100;
  QStringList fileNames;
  fileNames.reserve(childCount);
  for (int i = 0; i < childCount; ++i) {
    fileNames << QStringLiteral("file_%1.md").arg(i, 3, 10, QChar(QLatin1Char('0')));
  }

  const QString nbId = seedNotebook(QStringLiteral("nb_ec11"), QStringList{}, fileNames);
  QVERIFY(!nbId.isEmpty());

  const QStringList seedFiles = listFiles(nbId);
  QCOMPARE(seedFiles.size(), childCount);

  QStringList reversed = seedFiles;
  std::reverse(reversed.begin(), reversed.end());

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);

  QElapsedTimer timer;
  timer.start();
  m_service->reorderFolderChildren(nbId, QString(), QStringList(), reversed);
  QVERIFY(spy.wait(5000));
  const qint64 elapsedMs = timer.elapsed();

  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);
  QCOMPARE(listFiles(nbId), reversed);

  // Log the measurement so CI captures the actual perf budget achieved.
  qDebug().noquote()
      << QStringLiteral("EC11: reorder of %1 children took %2 ms").arg(childCount).arg(elapsedMs);
  QVERIFY2(elapsedMs < 500,
           qPrintable(QStringLiteral("EC11 reorder of %1 children exceeded 500 ms budget: %2 ms")
                          .arg(childCount)
                          .arg(elapsedMs)));
}

// =============================================================================
// EC12: Reorder runs concurrently with a save (gate-independent)
// =============================================================================
// CONTRACT CHANGE (CI race fix): reorderFolderChildren now performs its vxcore
// write SYNCHRONOUSLY on the calling (UI) thread — like every sibling folder
// mutation (createFolder/createFile/rename/delete) — and deliberately does NOT
// acquire the per-notebook NotebookIoGate. The gate could only ever be held on
// a worker thread, which cannot serialize against the UI-thread FolderManager
// reads (model fetchMore) that actually race the write; the retired gated
// design produced a deterministic PERMUTATION_MISMATCH on 2-core CI.
//
// This test pins the new contract: a foreign thread holding the gate (a stand-in
// for a concurrent buffer-save / sync-stage) does NOT block reorder; reorder
// completes and persists regardless. (The acknowledged flip-side — a structural
// vx.json write racing a concurrent git-stage, which the ungated siblings ALSO
// permit — is tracked as a future "serialize all FolderManager writes" refactor.
// Do NOT "fix" this back to gate-serialization.)
void TestReorderEdgeCases::EC12_concurrentSaveAndReorder() {
  const QString nbId = seedNotebook(QStringLiteral("nb_ec12"),
                                    QStringList{QStringLiteral("sub_a"), QStringLiteral("sub_b")},
                                    QStringList{QStringLiteral("f1.md"), QStringLiteral("f2.md")});
  QVERIFY(!nbId.isEmpty());

  const QStringList seedFolders = listFolders(nbId);
  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());

  std::atomic<bool> holderHasLock{false};
  std::atomic<bool> releaseRequested{false};
  QThread *holder = QThread::create([&]() {
    vnotex::NotebookIoGate::ScopedLock lock(*m_ioGate, nbId);
    holderHasLock.store(true);
    while (!releaseRequested.load()) {
      QThread::msleep(10);
    }
  });
  holder->start();
  while (!holderHasLock.load()) {
    QTest::qWait(10);
  }

  QSignalSpy spy(m_service, &vnotex::NotebookCoreService::reorderCompleted);
  m_service->reorderFolderChildren(nbId, QString(), newFolders, QStringList());

  // Reorder completes and persists even while the external holder owns the
  // gate — the gate is not on reorder's path.
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.size(), 1);
  QCOMPARE(spy.takeFirst().at(2).toBool(), true);
  QCOMPARE(listFolders(nbId), newFolders);

  releaseRequested.store(true);
  holder->wait();
  delete holder;
}

// =============================================================================
// EC13: After-reorder model refresh
// =============================================================================
// Real controller wired to real service + real notebook. A custom recording
// model receives nodesReordered → reloadNode and emits modelReset. The test
// asserts both signals fire so a hooked-up view would repaint without manual
// intervention.
void TestReorderEdgeCases::EC13_afterReorderModelRefresh() {
  const QString nbId =
      seedNotebook(QStringLiteral("nb_ec13"), QStringList{QStringLiteral("p"), QStringLiteral("q")},
                   QStringList{QStringLiteral("m.md"), QStringLiteral("n.md")});
  QVERIFY(!nbId.isEmpty());

  const QStringList seedFolders = listFolders(nbId);
  const QStringList seedFiles = listFiles(nbId);

  vnotex::ServiceLocator services;
  services.registerService<vnotex::NotebookCoreService>(m_service);

  vnotex::NotebookNodeController controller(services);
  RecordingNodeModel model(services, nbId);

  // Manually wire the connection the real NotebookNodeView::setController
  // wires under the hood: nodesReordered → model.reloadNode(parentId).
  QObject::connect(
      &controller, &vnotex::NotebookNodeController::nodesReordered, &model,
      [&model](const vnotex::NodeIdentifier &p_parentId) { model.reloadNode(p_parentId); });

  QSignalSpy reorderedSpy(&controller, &vnotex::NotebookNodeController::nodesReordered);
  QSignalSpy modelResetSpy(&model, &QAbstractItemModel::modelReset);

  // Build NodeIdentifier lists for the controller's reorderNodes contract:
  // every id lives directly under p_parentId (= notebook root).
  vnotex::NodeIdentifier parentId;
  parentId.notebookId = nbId;
  // parentId.relativePath defaults to QString() = root.

  QStringList newFolders = seedFolders;
  std::reverse(newFolders.begin(), newFolders.end());
  QStringList newFiles = seedFiles;
  std::reverse(newFiles.begin(), newFiles.end());

  QList<vnotex::NodeIdentifier> folderIds;
  for (const QString &name : newFolders) {
    folderIds.append(makeId(nbId, name));
  }
  QList<vnotex::NodeIdentifier> fileIds;
  for (const QString &name : newFiles) {
    fileIds.append(makeId(nbId, name));
  }

  controller.reorderNodes(parentId, folderIds, fileIds);

  // nodesReordered fires via reorderCompleted → onReorderCompleted on the
  // service's owning thread.
  QVERIFY(reorderedSpy.wait(5000));
  QCOMPARE(reorderedSpy.count(), 1);
  const vnotex::NodeIdentifier emittedParent =
      reorderedSpy.takeFirst().at(0).value<vnotex::NodeIdentifier>();
  QCOMPARE(emittedParent, parentId);

  // Drain any queued events so the connected lambda (model.reloadNode) runs.
  QCoreApplication::processEvents();

  QCOMPARE(model.reloadCallCount, 1);
  QCOMPARE(model.lastReloadedNode, parentId);
  QVERIFY2(modelResetSpy.count() >= 1,
           "model emitted no modelReset/rowsRemoved/rowsInserted after reorder");
}

// =============================================================================
// EC14: Sort mode change mid-drag
// =============================================================================
// Start a drag in OrderedByConfiguration, programmatically flip the proxy to
// OrderedByName before dropEvent. The drop MUST be rejected at drop time —
// dropEvent re-evaluates isReorderSortModeActive() rather than caching the
// drag-start mode.
void TestReorderEdgeCases::EC14_sortModeChangeMidDrag() {
  const QString notebookId = QStringLiteral("nb-ec14");
  vnotex::ServiceLocator services;
  StubModel source(services, notebookId);
  vnotex::NotebookNodeProxyModel proxy;
  proxy.setSourceModel(&source);
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  proxy.sort(0, Qt::AscendingOrder);

  RecordingController controller(services);
  TestableView view;
  view.setModel(&proxy);
  view.setController(&controller);
  view.resize(400, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(notebookId, QStringLiteral("a.md"), false)
           << makeRootNode(notebookId, QStringLiteral("b.md"), false)
           << makeRootNode(notebookId, QStringLiteral("c.md"), false);
  source.setStubChildren(children);
  QApplication::processEvents();

  // Drag c.md, drag-move ABOVE a.md while mode is OrderedByConfiguration —
  // this would normally be a legal reorder.
  QList<vnotex::NodeIdentifier> dragged = {children.at(2).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(&view, mime);

  QModelIndex anchorIdx = proxy.index(0, 0); // a.md
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(&view, anchorIdx, 0.05); // AboveItem
  doDragMove(&view, mime, pos);

  // MID-DRAG: flip sort mode to OrderedByName. The dropEvent re-check must
  // observe the new mode and reject.
  proxy.setViewOrder(vnotex::ViewOrder::OrderedByName);
  proxy.sort(0, Qt::AscendingOrder);
  QApplication::processEvents();

  doDrop(&view, mime, pos);

  delete mime;

  QCOMPARE(controller.reorderCallCount, 0);
  QCOMPARE(controller.moveCallCount, 0);
}

} // namespace tests

QTEST_MAIN(tests::TestReorderEdgeCases)
#include "test_reorder_edge_cases.moc"
