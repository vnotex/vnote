// T9 (notebook-explorer-drag-reorder): NotebookNodeView drag-drop dispatch.
//
// Exercises the new dropIndicatorPosition()-aware dispatch in
// NotebookNodeView::dragMoveEvent / dropEvent. Each sub-test simulates a
// drag-drop sequence on a real QTreeView (NOT GUILESS — needs QApplication +
// widget exposure for visualRect()) and asserts what the recording controller
// observed.
//
// Mock strategy:
//   - StubModel: flat NotebookNodeModel subclass with children at root, so we
//     control geometry + node identities without a real vxcore notebook.
//   - RecordingController: subclass NotebookNodeController, override the
//     virtual reorderNodes / moveNodes to record call arguments without
//     touching services.
//   - TestableView: NotebookNodeView subclass that promotes the protected
//     drag/drop event handlers to public so the test can drive them directly.
//
// Drop indicator semantics (QAbstractItemView::position):
//   - margin = qBound(2, height/5.5, 12)
//   - pos near top of rect → AboveItem
//   - pos near bottom of rect → BelowItem
//   - pos in middle of rect → OnItem
//   - pos outside any rect → OnViewport
//
// To exercise specific indicator positions deterministically we synthesize
// drag events whose position is at the top/bottom edge of the visualRect of
// the chosen anchor item.

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPoint>
#include <QSignalSpy>
#include <QStringList>
#include <QVector>
#include <QtTest>

#include <vxcore/vxcore.h>

#include <controllers/notebooknodecontroller.h>
#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>
#include <views/notebooknodeview.h>

namespace tests {

namespace {

const QString c_nodeMimeType = QStringLiteral("application/x-vnotex-node-identifier");
const QString c_notebookId = QStringLiteral("test-nb");

// Encode a list of NodeIdentifier into the vnotex node mime payload.
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
vnotex::NodeInfo makeRootNode(const QString &p_name, bool p_isFolder) {
  vnotex::NodeInfo info;
  info.name = p_name;
  info.isFolder = p_isFolder;
  info.id.notebookId = c_notebookId;
  info.id.relativePath = p_name; // parent = "" (root)
  return info;
}

// Build a NodeIdentifier with a custom relativePath (used for cross-parent
// regression tests where the source isn't in our model at all).
vnotex::NodeIdentifier makeId(const QString &p_relativePath) {
  vnotex::NodeIdentifier id;
  id.notebookId = c_notebookId;
  id.relativePath = p_relativePath;
  return id;
}

} // namespace

// Stub source model. Same trick used by tests/models/test_notebook_node_proxy_model_order.cpp:
// subclass NotebookNodeModel so the proxy's qobject_cast succeeds, then
// re-implement the Qt model interface plus the INodeListModel accessors over
// an in-memory vector of children at the model root.
class StubModel : public vnotex::NotebookNodeModel {
public:
  explicit StubModel(vnotex::ServiceLocator &p_services, QObject *p_parent = nullptr)
      : vnotex::NotebookNodeModel(p_services, p_parent) {
    setNotebookId(c_notebookId);
  }

  void setStubChildren(const QVector<vnotex::NodeInfo> &p_children) {
    beginResetModel();
    m_children = p_children;
    endResetModel();
  }

  // --- QAbstractItemModel ---
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
      // Drops onto the empty area (root) must also be permitted.
      f |= Qt::ItemIsDropEnabled;
    }
    return f;
  }

  bool hasChildren(const QModelIndex &p_parent = QModelIndex()) const override {
    return !p_parent.isValid() && !m_children.isEmpty();
  }

  bool canFetchMore(const QModelIndex & /*p_parent*/) const override { return false; }
  void fetchMore(const QModelIndex & /*p_parent*/) override {}

  // --- INodeListModel / NotebookNodeModel overrides ---
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

// Testable view. Promotes the protected drag/drop handlers to public so the
// test can invoke them with synthesized events.
class TestableView : public vnotex::NotebookNodeView {
public:
  using vnotex::NotebookNodeView::dragEnterEvent;
  using vnotex::NotebookNodeView::dragMoveEvent;
  using vnotex::NotebookNodeView::dropEvent;
  using vnotex::NotebookNodeView::NotebookNodeView;
};

class TestNotebookNodeViewReorder : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // T9a: same-parent file above sibling file in ByConfig mode → reorder
  // dispatched with the expected new ordered file list.
  void T9a_sameParentFileAboveFileInByConfig_dispatchesReorder();

  // T9b: same setup as T9a but proxy is in OrderedByName mode → reject
  // silently (no reorder, no error, no move).
  void T9b_byNameMode_rejectsReorder();

  // T9c: drag folder onto file anchor (cross-type within same parent) →
  // reject (no reorder dispatched).
  void T9c_folderDraggedToFileAnchor_rejectsReorder();

  // T9d: same setup as T9a but proxy filter is active → reject silently.
  void T9d_filterActive_rejectsReorder();

  // T9e: drag file from a different parent onto a folder anchor (OnItem) →
  // existing moveNodes path used (regression check).
  void T9e_crossParentOnFolder_callsMove();

  // T9f: drag with drop position over the empty viewport area → reject for
  // reorder (no anchor).
  void T9f_dropOnViewport_rejectsReorder();

  // T9g: cross-view drag (mime data references a node from a different
  // parent that isn't even in our model) → no reorder dispatched. Verifies
  // the dispatch gate uses the dragged node's parent, not the visible item.
  void T9g_crossViewSource_doesNotReorder();

private:
  // Helpers for event synthesis.
  void enterDrag(QMimeData *p_mime);
  void doDragMove(QMimeData *p_mime, const QPoint &p_pos,
                  Qt::KeyboardModifiers p_modifiers = Qt::NoModifier);
  void doDrop(QMimeData *p_mime, const QPoint &p_pos,
              Qt::KeyboardModifiers p_modifiers = Qt::NoModifier);
  QPoint pointInsideRow(const QModelIndex &p_idx, double p_yFraction) const;
  QPoint pointOnViewport() const;

  VxCoreContextHandle m_ctx = nullptr;
  vnotex::ServiceLocator *m_services = nullptr;
  StubModel *m_source = nullptr;
  vnotex::NotebookNodeProxyModel *m_proxy = nullptr;
  RecordingController *m_controller = nullptr;
  TestableView *m_view = nullptr;
};

void TestNotebookNodeViewReorder::initTestCase() {
  vxcore_set_test_mode(1);
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);
}

void TestNotebookNodeViewReorder::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestNotebookNodeViewReorder::init() {
  m_services = new vnotex::ServiceLocator();

  m_source = new StubModel(*m_services);

  m_proxy = new vnotex::NotebookNodeProxyModel();
  m_proxy->setSourceModel(m_source);
  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  m_proxy->sort(0, Qt::AscendingOrder);

  m_controller = new RecordingController(*m_services);

  m_view = new TestableView();
  m_view->setModel(m_proxy);
  m_view->setController(m_controller);
  m_view->resize(400, 600);
  m_view->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_view));
}

void TestNotebookNodeViewReorder::cleanup() {
  delete m_view;
  m_view = nullptr;
  delete m_controller;
  m_controller = nullptr;
  delete m_proxy;
  m_proxy = nullptr;
  delete m_source;
  m_source = nullptr;
  delete m_services;
  m_services = nullptr;
}

void TestNotebookNodeViewReorder::enterDrag(QMimeData *p_mime) {
  // Make sure the model is laid out so visualRect returns real geometry.
  QApplication::processEvents();
  QDragEnterEvent enterEvent(QPoint(10, 10), Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton,
                             Qt::NoModifier);
  m_view->dragEnterEvent(&enterEvent);
}

void TestNotebookNodeViewReorder::doDragMove(QMimeData *p_mime, const QPoint &p_pos,
                                             Qt::KeyboardModifiers p_modifiers) {
  QDragMoveEvent moveEvent(p_pos, Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton,
                           p_modifiers);
  m_view->dragMoveEvent(&moveEvent);
}

void TestNotebookNodeViewReorder::doDrop(QMimeData *p_mime, const QPoint &p_pos,
                                         Qt::KeyboardModifiers p_modifiers) {
  QDropEvent dropEvent(p_pos, Qt::CopyAction | Qt::MoveAction, p_mime, Qt::NoButton, p_modifiers);
  m_view->dropEvent(&dropEvent);
}

QPoint TestNotebookNodeViewReorder::pointInsideRow(const QModelIndex &p_idx,
                                                   double p_yFraction) const {
  QRect r = m_view->visualRect(p_idx);
  Q_ASSERT(r.isValid() && !r.isEmpty());
  // Translate viewport coordinates to view coordinates (visualRect returns
  // viewport coords; dragMoveEvent receives pos in viewport coords too).
  int x = r.center().x();
  int y = r.top() + static_cast<int>(p_yFraction * r.height());
  // Clamp inside the row so we don't accidentally hit a neighbor.
  if (y < r.top()) {
    y = r.top();
  }
  if (y > r.bottom()) {
    y = r.bottom();
  }
  return QPoint(x, y);
}

QPoint TestNotebookNodeViewReorder::pointOnViewport() const {
  // Far below any item — guaranteed to map to an invalid index → OnViewport.
  return QPoint(10, m_view->viewport()->height() - 5);
}

// ============================================================================
// T9a: drag file above sibling file in ByConfig → reorder dispatched
// ============================================================================
void TestNotebookNodeViewReorder::T9a_sameParentFileAboveFileInByConfig_dispatchesReorder() {
  // Setup: three files at notebook root in order [a.md, b.md, c.md].
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("a.md"), /*folder=*/false)
           << makeRootNode(QStringLiteral("b.md"), /*folder=*/false)
           << makeRootNode(QStringLiteral("c.md"), /*folder=*/false);
  m_source->setStubChildren(children);
  QApplication::processEvents();

  // Drag c.md and drop it ABOVE a.md → expected new order [c.md, a.md, b.md].
  QList<vnotex::NodeIdentifier> dragged = {children.at(2).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QModelIndex anchorIdx = m_proxy->index(0, 0); // a.md
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(anchorIdx, 0.05); // top → AboveItem
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  QCOMPARE(m_controller->reorderCallCount, 1);
  QCOMPARE(m_controller->moveCallCount, 0);
  QCOMPARE(m_controller->lastReorderParent.notebookId, c_notebookId);
  QCOMPARE(m_controller->lastReorderParent.relativePath, QString()); // root
  QCOMPARE(m_controller->lastFolderIds.size(), 0);                   // no folders reordered
  QCOMPARE(m_controller->lastFileIds.size(), 3);
  QCOMPARE(m_controller->lastFileIds.at(0).relativePath, QStringLiteral("c.md"));
  QCOMPARE(m_controller->lastFileIds.at(1).relativePath, QStringLiteral("a.md"));
  QCOMPARE(m_controller->lastFileIds.at(2).relativePath, QStringLiteral("b.md"));
}

// ============================================================================
// T9b: drag in ByName mode → reject (no service call)
// ============================================================================
void TestNotebookNodeViewReorder::T9b_byNameMode_rejectsReorder() {
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("a.md"), false)
           << makeRootNode(QStringLiteral("b.md"), false);
  m_source->setStubChildren(children);

  // Flip proxy to ByName — reorder must be silently rejected.
  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByName);
  m_proxy->sort(0, Qt::AscendingOrder);
  QApplication::processEvents();

  // a.md comes first in ByName (already alphabetical). Drag b.md above a.md.
  QList<vnotex::NodeIdentifier> dragged = {children.at(1).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QModelIndex anchorIdx = m_proxy->index(0, 0); // a.md
  QPoint pos = pointInsideRow(anchorIdx, 0.05); // AboveItem
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  QCOMPARE(m_controller->reorderCallCount, 0);
  QCOMPARE(m_controller->moveCallCount, 0);
}

// ============================================================================
// T9c: drag folder among files (cross-type with anchor) → reject
// ============================================================================
void TestNotebookNodeViewReorder::T9c_folderDraggedToFileAnchor_rejectsReorder() {
  // Folder-first: folder1 then files. Drag the folder onto a file anchor.
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("folder1"), /*folder=*/true)
           << makeRootNode(QStringLiteral("a.md"), false)
           << makeRootNode(QStringLiteral("b.md"), false);
  m_source->setStubChildren(children);
  QApplication::processEvents();

  // In ByConfig + folder-first, proxy rows are: [folder1, a.md, b.md].
  QModelIndex folderIdx = m_proxy->index(0, 0);
  QModelIndex fileIdx = m_proxy->index(1, 0); // a.md
  QVERIFY(folderIdx.isValid());
  QVERIFY(fileIdx.isValid());

  // Drag folder1 above a.md → cross-type, must reject reorder.
  QList<vnotex::NodeIdentifier> dragged = {children.at(0).id}; // folder1
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QPoint pos = pointInsideRow(fileIdx, 0.05); // AboveItem on file anchor
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  QCOMPARE(m_controller->reorderCallCount, 0);
  QCOMPARE(m_controller->moveCallCount, 0);
}

// ============================================================================
// T9d: drag with filter active → reject
// ============================================================================
void TestNotebookNodeViewReorder::T9d_filterActive_rejectsReorder() {
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("alpha.md"), false)
           << makeRootNode(QStringLiteral("beta.md"), false)
           << makeRootNode(QStringLiteral("gamma.md"), false);
  m_source->setStubChildren(children);

  // Activate filter — "*.md" matches everything visible but proxy still
  // reports a non-empty nameFilter, so reorder must reject.
  m_proxy->setNameFilter(QStringLiteral("*.md"));
  QApplication::processEvents();

  QList<vnotex::NodeIdentifier> dragged = {children.at(2).id}; // gamma.md
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QModelIndex anchorIdx = m_proxy->index(0, 0); // alpha.md
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(anchorIdx, 0.05); // AboveItem
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  QCOMPARE(m_controller->reorderCallCount, 0);
  QCOMPARE(m_controller->moveCallCount, 0);
}

// ============================================================================
// T9e: drag file from a different parent onto a folder anchor → moveNodes
// (regression check — existing cross-parent move path).
// ============================================================================
void TestNotebookNodeViewReorder::T9e_crossParentOnFolder_callsMove() {
  // Stub model contains one folder at root. Drag a file from a foreign
  // parent path (not in the model) and drop it OnItem on the folder.
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("dest_folder"), /*folder=*/true);
  m_source->setStubChildren(children);
  QApplication::processEvents();

  // Source file lives under "other/foo.md" — its parent is "other", which
  // does NOT match the folder anchor's path → cross-parent, move path.
  QList<vnotex::NodeIdentifier> dragged = {makeId(QStringLiteral("other/foo.md"))};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QModelIndex folderIdx = m_proxy->index(0, 0);
  QVERIFY(folderIdx.isValid());
  QPoint pos = pointInsideRow(folderIdx, 0.5); // center → OnItem
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  QCOMPARE(m_controller->reorderCallCount, 0);
  QCOMPARE(m_controller->moveCallCount, 1);
  QCOMPARE(m_controller->lastMoveIds.size(), 1);
  QCOMPARE(m_controller->lastMoveIds.at(0).relativePath, QStringLiteral("other/foo.md"));
  QCOMPARE(m_controller->lastMoveTarget.relativePath, QStringLiteral("dest_folder"));
}

// ============================================================================
// T9f: drop on empty viewport area → reject for reorder
// ============================================================================
void TestNotebookNodeViewReorder::T9f_dropOnViewport_rejectsReorder() {
  // One file at root. Drop on empty viewport space below the row.
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("only.md"), false);
  m_source->setStubChildren(children);
  QApplication::processEvents();

  QList<vnotex::NodeIdentifier> dragged = {children.at(0).id};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  QPoint pos = pointOnViewport(); // far below the single row → OnViewport
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  // Same-parent (root) + OnViewport → reject reorder. moveNodes also must
  // NOT fire because the source's parent IS the target (root).
  QCOMPARE(m_controller->reorderCallCount, 0);
  QCOMPARE(m_controller->moveCallCount, 0);
}

// ============================================================================
// T9g: cross-view drag (foreign source not in the visible model) → no reorder
// dispatched. Existing cross-parent move path is used because allSameParent
// resolves to false based on the dragged node's parent string.
// ============================================================================
void TestNotebookNodeViewReorder::T9g_crossViewSource_doesNotReorder() {
  // Our view shows files at root.
  QVector<vnotex::NodeInfo> children;
  children << makeRootNode(QStringLiteral("local1.md"), false)
           << makeRootNode(QStringLiteral("local2.md"), false);
  m_source->setStubChildren(children);
  QApplication::processEvents();

  // Source is from a different parent path — simulates a foreign view's
  // node being dragged into ours.
  QList<vnotex::NodeIdentifier> dragged = {makeId(QStringLiteral("foreign_dir/elsewhere.md"))};
  QMimeData *mime = makeNodeMimeData(dragged);
  enterDrag(mime);

  // Drop ABOVE local1.md (would be reorder if same parent — but it's not).
  QModelIndex anchorIdx = m_proxy->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QPoint pos = pointInsideRow(anchorIdx, 0.05);
  doDragMove(mime, pos);
  doDrop(mime, pos);

  delete mime;

  // Reorder must NOT be dispatched. The cross-parent move path may fire (if
  // the existing logic decides this is a "move into the file's parent"
  // case) or may be a no-op; we assert ONLY the reorder gate is closed.
  QCOMPARE(m_controller->reorderCallCount, 0);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookNodeViewReorder)
#include "test_notebook_node_view_reorder.moc"
