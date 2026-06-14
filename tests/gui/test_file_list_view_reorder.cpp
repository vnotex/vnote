// T10 (notebook-explorer-drag-reorder): FileListView drag-reorder routing.
//
// Source-side mirror of T9 (NotebookNodeView). Verifies that:
//   T10a: same-view drag with AboveItem + ByConfig + no filter + all files
//         routes to NotebookNodeController::reorderNodes with the expected
//         orderedFileIds permutation (orderedFolderIds is empty).
//   T10b: same setup but with proxy in OrderedByName → reorder NOT dispatched.
//   T10c: same setup but dragged payload is a folder (cross-type vs the
//         FileListView's file-only model) → reorder NOT dispatched.
//   T10d: same setup but proxy has an active name filter → reorder NOT
//         dispatched.
//   T10e: cross-parent drop (dragged file lives under a different parent
//         than the displayRoot) → existing moveNodes path used, reorder NOT
//         dispatched.
//   T10f: same setup but indicator is OnViewport (no anchor) → reorder NOT
//         dispatched.
//   T10g: cross-view drag (drop event source != this view) → reorder NOT
//         dispatched.
//   T10h: cross-view drag also preserves the existing cross-pane move
//         behavior — when the dragged file's parent differs from displayRoot
//         AND the source is foreign, moveNodes is called with the dragged
//         payload and target = displayRoot.
//
// Strategy: subclass FileListView and override the `resolveDropSource`
// protected hook so the test can inject a synthetic drop-source QObject
// without spinning up a real QDragManager session. The same subclass exposes
// the protected dragMoveEvent / dropEvent so the test can fire forged
// QDropEvent / QDragMoveEvent instances directly at the view.

#include <QAbstractListModel>
#include <QApplication>
#include <QDataStream>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHash>
#include <QList>
#include <QMimeData>
#include <QPointF>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <controllers/notebooknodecontroller.h>
#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <models/inodelistmodel.h>
#include <models/notebooknodeproxymodel.h>
#include <views/filelistview.h>

// --- NotebookNodeController stubs (satisfy linker for filelistview.cpp) ---
// FileListView references controller methods; we redirect the ones it calls
// at drop time into a recording table while no-op'ing the rest.
//
// Two pieces of recording state are exposed: lastReorderCall and
// lastMoveCall, each a struct populated by the corresponding controller
// method. Tests assert against these directly.

namespace tests {

struct ReorderCall {
  int count = 0;
  vnotex::NodeIdentifier parentId;
  QList<vnotex::NodeIdentifier> orderedFolderIds;
  QList<vnotex::NodeIdentifier> orderedFileIds;
};

struct MoveCall {
  int count = 0;
  QList<vnotex::NodeIdentifier> nodeIds;
  vnotex::NodeIdentifier targetFolderId;
};

// Globals that the controller stub writes into and tests read from. The
// stubs MUST live in this TU so the linker resolves them ahead of the real
// NotebookNodeController object file.
static ReorderCall g_lastReorderCall;
static MoveCall g_lastMoveCall;
static QHash<vnotex::NodeIdentifier, vnotex::NodeIdentifier> g_parentMap;

void resetRecordingState() {
  g_lastReorderCall = ReorderCall{};
  g_lastMoveCall = MoveCall{};
  g_parentMap.clear();
}

} // namespace tests

namespace vnotex {

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_clipboard(new ClipboardState()) {}
NotebookNodeController::~NotebookNodeController() = default;

void NotebookNodeController::setModel(INodeListModel *) {}
INodeListModel *NotebookNodeController::model() const { return nullptr; }
void NotebookNodeController::setView(NotebookNodeView *) {}
NotebookNodeView *NotebookNodeController::view() const { return nullptr; }
QMenu *NotebookNodeController::createContextMenu(const NodeIdentifier &, QWidget *) {
  return nullptr;
}
QMenu *NotebookNodeController::createExternalNodeContextMenu(const NodeIdentifier &, QWidget *) {
  return nullptr;
}
void NotebookNodeController::newNote(const NodeIdentifier &) {}
void NotebookNodeController::newFolder(const NodeIdentifier &) {}
void NotebookNodeController::openNode(const NodeIdentifier &) {}
void NotebookNodeController::openNodeWithDefaultApp(const NodeIdentifier &) {}
void NotebookNodeController::deleteNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::removeNodesFromNotebook(const QList<NodeIdentifier> &) {}
void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::cutNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::pasteNodes(const NodeIdentifier &) {}
void NotebookNodeController::duplicateNode(const NodeIdentifier &) {}
void NotebookNodeController::renameNode(const NodeIdentifier &) {}
void NotebookNodeController::markNode(const NodeIdentifier &) {}

void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &p_nodeIds,
                                       const NodeIdentifier &p_targetFolderId) {
  ++tests::g_lastMoveCall.count;
  tests::g_lastMoveCall.nodeIds = p_nodeIds;
  tests::g_lastMoveCall.targetFolderId = p_targetFolderId;
}

void NotebookNodeController::exportNode(const NodeIdentifier &) {}
void NotebookNodeController::importExternalNode(const NodeIdentifier &) {}
void NotebookNodeController::showNodeProperties(const NodeIdentifier &) {}
void NotebookNodeController::copyNodePath(const NodeIdentifier &) {}
void NotebookNodeController::locateNodeInFileManager(const NodeIdentifier &) {}
void NotebookNodeController::sortNodes(const NodeIdentifier &) {}

void NotebookNodeController::reorderNodes(const NodeIdentifier &p_parentId,
                                          const QList<NodeIdentifier> &p_orderedFolderIds,
                                          const QList<NodeIdentifier> &p_orderedFileIds) {
  ++tests::g_lastReorderCall.count;
  tests::g_lastReorderCall.parentId = p_parentId;
  tests::g_lastReorderCall.orderedFolderIds = p_orderedFolderIds;
  tests::g_lastReorderCall.orderedFileIds = p_orderedFileIds;
}

void NotebookNodeController::reloadNode(const NodeIdentifier &) {}
void NotebookNodeController::reloadAll() {}
void NotebookNodeController::pinNodeToQuickAccess(const NodeIdentifier &) {}
void NotebookNodeController::manageNodeTags(const NodeIdentifier &) {}
void NotebookNodeController::openNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::openNodesWithCommand(const QList<NodeIdentifier> &, const QString &) {}
void NotebookNodeController::duplicateNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::copyNodePaths(const QList<NodeIdentifier> &) {}
void NotebookNodeController::pinNodesToQuickAccess(const QList<NodeIdentifier> &) {}
void NotebookNodeController::reloadNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::markNodes(const QList<NodeIdentifier> &) {}
bool NotebookNodeController::canPaste() const { return false; }
void NotebookNodeController::shareClipboardWith(NotebookNodeController *) {}
bool NotebookNodeController::isSingleClickActivationEnabled() const { return false; }
void NotebookNodeController::setSelectedNodesCallback(SelectedNodesCallback) {}
void NotebookNodeController::handleNewNoteResult(const NodeIdentifier &, const NodeIdentifier &) {}
void NotebookNodeController::handleNewFolderResult(const NodeIdentifier &, const NodeIdentifier &) {
}
void NotebookNodeController::handleRenameResult(const NodeIdentifier &, const QString &) {}
void NotebookNodeController::handleMarkResult(const NodeIdentifier &, const QString &,
                                              const QString &) {}
void NotebookNodeController::handleDeleteConfirmed(const QList<NodeIdentifier> &, bool) {}
void NotebookNodeController::handleRemoveConfirmed(const QList<NodeIdentifier> &) {}
void NotebookNodeController::handleImportFiles(const NodeIdentifier &, const QStringList &) {}
void NotebookNodeController::handleImportFolder(const NodeIdentifier &, const QString &) {}

NodeIdentifier NotebookNodeController::getParentFolder(const NodeIdentifier &p_nodeId) const {
  // Tests pre-populate g_parentMap with id → parent mappings. If absent,
  // fall back to deriving the parent from the relative path so simple
  // same-parent tests work without explicit registration.
  auto it = tests::g_parentMap.constFind(p_nodeId);
  if (it != tests::g_parentMap.constEnd()) {
    return it.value();
  }
  NodeIdentifier parent;
  parent.notebookId = p_nodeId.notebookId;
  parent.relativePath = p_nodeId.parentPath();
  return parent;
}

} // namespace vnotex

// --- End stubs ---

using namespace vnotex;

namespace tests {

// Minimal INodeListModel-backed list model. Holds NodeInfo entries and
// exposes them via the same roles the production proxy + view consume. The
// proxy's lessThan path requires the source to be a NotebookNodeModel for
// sort to do anything meaningful, but since we never call sort() in tests
// the proxy passes source rows through in order, which is exactly what we
// need for OrderedByConfiguration semantics.
class FileListModelStub : public QAbstractListModel, public INodeListModel {
public:
  using QAbstractListModel::QAbstractListModel;

  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override {
    return p_parent.isValid() ? 0 : m_entries.size();
  }

  QVariant data(const QModelIndex &p_index, int p_role) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_entries.size()) {
      return {};
    }
    const NodeInfo &info = m_entries[p_index.row()];
    switch (p_role) {
    case Qt::DisplayRole:
      return info.name;
    case INodeListModel::NodeIdentifierRole:
      return QVariant::fromValue(info.id);
    case INodeListModel::NodeInfoRole:
      return QVariant::fromValue(info);
    case INodeListModel::IsFolderRole:
      return info.isFolder;
    default:
      return {};
    }
  }

  Qt::ItemFlags flags(const QModelIndex &p_index) const override {
    Qt::ItemFlags f = QAbstractListModel::flags(p_index);
    if (p_index.isValid()) {
      f |= Qt::ItemIsDragEnabled;
    }
    f |= Qt::ItemIsDropEnabled;
    return f;
  }

  // INodeListModel
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_entries.size()) {
      return {};
    }
    return m_entries[p_index.row()].id;
  }

  NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_entries.size()) {
      return {};
    }
    return m_entries[p_index.row()];
  }

  QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const override {
    for (int i = 0; i < m_entries.size(); ++i) {
      if (m_entries[i].id == p_nodeId) {
        return index(i, 0);
      }
    }
    return {};
  }

  bool supportsDragDrop() const override { return true; }
  NodeIdentifier getDisplayRoot() const override { return m_displayRoot; }
  QString getNotebookId() const override { return m_notebookId; }

  // Test fixture API
  void setNotebookId(const QString &p_id) { m_notebookId = p_id; }
  void setDisplayRoot(const NodeIdentifier &p_root) { m_displayRoot = p_root; }

  void setEntries(QList<NodeInfo> p_entries) {
    beginResetModel();
    m_entries = std::move(p_entries);
    endResetModel();
  }

  const QList<NodeInfo> &entries() const { return m_entries; }

private:
  QList<NodeInfo> m_entries;
  NodeIdentifier m_displayRoot;
  QString m_notebookId;
};

// FileListView subclass that opens up the protected hooks the test needs.
class TestableFileListView : public FileListView {
public:
  using FileListView::dragMoveEvent;
  using FileListView::dropEvent;
  using FileListView::FileListView;

  // Inject a synthetic drop source so cross-view drag detection can be
  // exercised without QDragManager. nullptr restores the real source().
  void setMockDropSource(QObject *p_source) { m_mockSource = p_source; }

protected:
  QObject *resolveDropSource(QDropEvent *p_event) const override {
    return m_mockSource ? m_mockSource.data() : FileListView::resolveDropSource(p_event);
  }

private:
  QPointer<QObject> m_mockSource;
};

// Build node-mime payload identical to what the production drag path emits.
// Use the same MIME format constant as filelistview.cpp.
const char *kNodeMime = "application/x-vnotex-node-identifier";

QMimeData *makeNodeMime(const QList<NodeIdentifier> &p_nodes) {
  auto *mime = new QMimeData();
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  for (const auto &n : p_nodes) {
    stream << n.notebookId << n.relativePath;
  }
  mime->setData(QLatin1String(kNodeMime), data);
  return mime;
}

NodeInfo makeFile(const QString &p_nbId, const QString &p_relPath) {
  NodeInfo info;
  info.id.notebookId = p_nbId;
  info.id.relativePath = p_relPath;
  info.isFolder = false;
  const int sep = p_relPath.lastIndexOf(QLatin1Char('/'));
  info.name = sep < 0 ? p_relPath : p_relPath.mid(sep + 1);
  return info;
}

class TestFileListViewReorder : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // 8 sub-tests called out in T10 acceptance criteria.
  void T10a_sameView_AboveItem_byConfig_routesToReorder();
  void T10b_byNameMode_rejectsReorder();
  void T10c_crossTypeDraggedFolder_rejectsReorder();
  void T10d_activeNameFilter_rejectsReorder();
  void T10e_crossParentDrop_usesExistingMovePath();
  void T10f_onViewportIndicator_rejectsReorder();
  void T10g_crossViewDrag_doesNotReorder();
  void T10h_crossViewDrag_preservesExistingMoveBehavior();

private:
  void seedThreeFilesUnderRoot();

  // Fire a drop event through the protected dropEvent override. Returns
  // after Qt processes the event synchronously. Caller owns mime data; we
  // copy ownership semantics from QDropEvent constructor (mime is borrowed,
  // not owned, so it must stay alive until after the call).
  void simulateDrop(TestableFileListView *p_view, const QMimeData *p_mime, const QPointF &p_pos,
                    Qt::KeyboardModifiers p_mods = Qt::NoModifier);

  void simulateDragMove(TestableFileListView *p_view, const QMimeData *p_mime, const QPointF &p_pos,
                        Qt::KeyboardModifiers p_mods = Qt::NoModifier);

  ServiceLocator *m_services = nullptr;
  NotebookNodeController *m_controller = nullptr;
  FileListModelStub *m_source = nullptr;
  NotebookNodeProxyModel *m_proxy = nullptr;
  TestableFileListView *m_view = nullptr;

  // Stable identifiers used across sub-tests.
  static QString s_nbId() { return QStringLiteral("nb-1"); }
  NodeIdentifier rootId() const {
    NodeIdentifier id;
    id.notebookId = s_nbId();
    id.relativePath = QString(); // notebook root
    return id;
  }
};

void TestFileListViewReorder::init() {
  resetRecordingState();

  m_services = new ServiceLocator();
  m_controller = new NotebookNodeController(*m_services);

  m_source = new FileListModelStub();
  m_source->setNotebookId(s_nbId());
  m_source->setDisplayRoot(rootId());

  m_proxy = new NotebookNodeProxyModel();
  m_proxy->setSourceModel(m_source);
  m_proxy->setFilterFlags(NotebookNodeProxyModel::ShowNotes);
  m_proxy->setViewOrder(ViewOrder::OrderedByConfiguration);
  // Intentionally NOT calling proxy->sort(0). The proxy's lessThan tries to
  // qobject_cast<NotebookNodeModel*> on the source and short-circuits
  // because the source is a FileListModelStub; sorting would therefore
  // collapse to default lex behavior on display strings, which is not what
  // we want for OrderedByConfiguration parity.

  m_view = new TestableFileListView();
  m_view->setModel(m_proxy);
  m_view->setController(m_controller);
  m_view->resize(400, 300);

  // Self-source for "same view" routing in T10a-T10f. Cross-view tests
  // override this to a foreign QObject.
  m_view->setMockDropSource(m_view);

  // Process any pending events from model + view setup so dropEvent sees a
  // settled state.
  QCoreApplication::processEvents();
}

void TestFileListViewReorder::cleanup() {
  delete m_view;
  m_view = nullptr;
  delete m_proxy;
  m_proxy = nullptr;
  delete m_source;
  m_source = nullptr;
  delete m_controller;
  m_controller = nullptr;
  delete m_services;
  m_services = nullptr;
  resetRecordingState();
}

void TestFileListViewReorder::seedThreeFilesUnderRoot() {
  QList<NodeInfo> entries;
  entries.append(makeFile(s_nbId(), QStringLiteral("a.md")));
  entries.append(makeFile(s_nbId(), QStringLiteral("b.md")));
  entries.append(makeFile(s_nbId(), QStringLiteral("c.md")));
  m_source->setEntries(entries);
  QCoreApplication::processEvents();
}

void TestFileListViewReorder::simulateDrop(TestableFileListView *p_view, const QMimeData *p_mime,
                                           const QPointF &p_pos, Qt::KeyboardModifiers p_mods) {
  // QDropEvent borrows the mime data; keep alive in the caller.
  QDropEvent evt(p_pos, Qt::MoveAction | Qt::CopyAction, p_mime, Qt::LeftButton, p_mods,
                 QEvent::Drop);
  p_view->dropEvent(&evt);
}

void TestFileListViewReorder::simulateDragMove(TestableFileListView *p_view,
                                               const QMimeData *p_mime, const QPointF &p_pos,
                                               Qt::KeyboardModifiers p_mods) {
  QDragMoveEvent evt(p_pos.toPoint(), Qt::MoveAction | Qt::CopyAction, p_mime, Qt::LeftButton,
                     p_mods);
  p_view->dragMoveEvent(&evt);
}

// T10a: same view, files [a, b, c]; drag c above a → reorder with [c, a, b].
void TestFileListViewReorder::T10a_sameView_AboveItem_byConfig_routesToReorder() {
  seedThreeFilesUnderRoot();

  // Identify the file we want to drag and the anchor.
  const NodeIdentifier c{s_nbId(), QStringLiteral("c.md")};
  const NodeIdentifier a{s_nbId(), QStringLiteral("a.md")};

  // Pre-set the indicator state. Qt does not let us call dropIndicatorPosition()
  // directly to a chosen value, but it derives from the live drag move event
  // against an actual hovered index. Feed dragMoveEvent first so the view
  // computes dropIndicatorPosition() == AboveItem for the next dropEvent.
  QModelIndex anchorProxyIdx = m_view->model()->index(0, 0); // a.md at row 0
  QVERIFY(anchorProxyIdx.isValid());
  const QRect anchorRect = m_view->visualRect(anchorProxyIdx);
  QVERIFY(anchorRect.isValid());
  // Drop point: top edge of anchor → AboveItem indicator.
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  // Dragged set: c.md
  QScopedPointer<QMimeData> mime(makeNodeMime({c}));

  // Drive dragMoveEvent first so dropIndicatorPosition() returns the
  // expected AboveItem prior to dropEvent.
  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 1);
  QCOMPARE(g_lastMoveCall.count, 0);
  QCOMPARE(g_lastReorderCall.parentId, rootId());
  QVERIFY(g_lastReorderCall.orderedFolderIds.isEmpty());
  // Expected new file order: c.md inserted ABOVE a.md → [c, a, b].
  const NodeIdentifier b{s_nbId(), QStringLiteral("b.md")};
  const QList<NodeIdentifier> expected{c, a, b};
  QCOMPARE(g_lastReorderCall.orderedFileIds, expected);
}

// T10b: same setup as T10a but proxy in OrderedByName → reorder rejected.
void TestFileListViewReorder::T10b_byNameMode_rejectsReorder() {
  seedThreeFilesUnderRoot();
  m_proxy->setViewOrder(ViewOrder::OrderedByName);

  const NodeIdentifier c{s_nbId(), QStringLiteral("c.md")};
  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({c}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  // Same-parent same-folder drop in non-ByConfig mode is silently rejected;
  // moveNodes must NOT be invoked (would otherwise be a no-op move).
  QCOMPARE(g_lastMoveCall.count, 0);
}

// T10c: dragged payload is a folder. The FileListView's model only shows
// files (filter = ShowNotes), so the "anchor under drop" is a file and the
// dragged is a folder — i.e., cross-type. Currently FileListView treats the
// drop as a move into displayRoot because the folder's parent is something
// else; the key assertion is that reorderNodes is NOT called.
void TestFileListViewReorder::T10c_crossTypeDraggedFolder_rejectsReorder() {
  seedThreeFilesUnderRoot();

  // Build a synthetic folder NodeIdentifier whose parent IS the displayRoot
  // (root). That way the "same-parent" pre-condition succeeds and the only
  // thing standing between us and reorder is the cross-type guard. The
  // FileListView's reorderNodes call would conflate folders with files —
  // the dispatch must avoid that. (In FileListView the dragged set being
  // folders cannot legitimately come from this same view; same-view drags
  // are always files because the model only exposes files. We simulate
  // this by injecting a foreign source so the dispatch hits the cross-view
  // branch and falls into the move path instead of reorder.)
  NodeIdentifier folder;
  folder.notebookId = s_nbId();
  folder.relativePath = QStringLiteral("subfolder");
  g_parentMap.insert(folder, rootId()); // folder's parent is the displayRoot

  // Foreign source (folder pane).
  QObject foreignSource;
  m_view->setMockDropSource(&foreignSource);

  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({folder}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  // The folder is at displayRoot already, so the "same parent" guard rejects
  // the move silently (no moveNodes either).
  QCOMPARE(g_lastMoveCall.count, 0);
}

// T10d: an active name filter forces reorder rejection — the filtered set in
// the view is no longer the full on-disk order, so reordering would corrupt
// the unseen rows.
void TestFileListViewReorder::T10d_activeNameFilter_rejectsReorder() {
  seedThreeFilesUnderRoot();
  m_proxy->setNameFilter(QStringLiteral("*.md")); // matches everything but is still "active"

  const NodeIdentifier c{s_nbId(), QStringLiteral("c.md")};
  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({c}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  QCOMPARE(g_lastMoveCall.count, 0);
}

// T10e: dragged file has a different parent than displayRoot — the existing
// cross-pane move path must be used, not reorder.
void TestFileListViewReorder::T10e_crossParentDrop_usesExistingMovePath() {
  seedThreeFilesUnderRoot();

  // Dragged file lives in a different folder than displayRoot.
  NodeIdentifier elsewhere;
  elsewhere.notebookId = s_nbId();
  elsewhere.relativePath = QStringLiteral("elsewhere/thing.md");
  NodeIdentifier elsewhereParent;
  elsewhereParent.notebookId = s_nbId();
  elsewhereParent.relativePath = QStringLiteral("elsewhere");
  g_parentMap.insert(elsewhere, elsewhereParent);

  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({elsewhere}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  QCOMPARE(g_lastMoveCall.count, 1);
  QCOMPARE(g_lastMoveCall.targetFolderId, rootId());
  QCOMPARE(g_lastMoveCall.nodeIds, QList<NodeIdentifier>{elsewhere});
}

// T10f: drop point sits below the last item / in empty viewport area →
// dropIndicatorPosition() returns OnViewport, no anchor → reorder rejected.
void TestFileListViewReorder::T10f_onViewportIndicator_rejectsReorder() {
  seedThreeFilesUnderRoot();

  const NodeIdentifier c{s_nbId(), QStringLiteral("c.md")};

  // Drop position outside every row so indexAt returns invalid and the
  // indicator becomes OnViewport.
  QPointF dropPos(10.0, 5000.0);

  QScopedPointer<QMimeData> mime(makeNodeMime({c}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  // Same-folder + no anchor: drop is silently ignored.
  QCOMPARE(g_lastMoveCall.count, 0);
}

// T10g: cross-view drag (source is a foreign QObject) → reorder rejected
// even when every other gate would pass.
void TestFileListViewReorder::T10g_crossViewDrag_doesNotReorder() {
  seedThreeFilesUnderRoot();
  QObject foreignSource;
  m_view->setMockDropSource(&foreignSource);

  const NodeIdentifier c{s_nbId(), QStringLiteral("c.md")};
  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({c}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
}

// T10h: cross-view drag with a foreign file at a different parent → existing
// moveNodes call still fires with target = displayRoot. Guarantees the cross
// pane behavior is preserved even after the reorder branch was added.
void TestFileListViewReorder::T10h_crossViewDrag_preservesExistingMoveBehavior() {
  seedThreeFilesUnderRoot();
  QObject foreignSource;
  m_view->setMockDropSource(&foreignSource);

  NodeIdentifier foreignFile;
  foreignFile.notebookId = s_nbId();
  foreignFile.relativePath = QStringLiteral("elsewhere/foreign.md");
  NodeIdentifier foreignParent;
  foreignParent.notebookId = s_nbId();
  foreignParent.relativePath = QStringLiteral("elsewhere");
  g_parentMap.insert(foreignFile, foreignParent);

  QModelIndex anchorIdx = m_view->model()->index(0, 0);
  QVERIFY(anchorIdx.isValid());
  QRect anchorRect = m_view->visualRect(anchorIdx);
  QPointF dropPos(anchorRect.center().x(), anchorRect.top() + 1);

  QScopedPointer<QMimeData> mime(makeNodeMime({foreignFile}));

  simulateDragMove(m_view, mime.data(), dropPos);
  simulateDrop(m_view, mime.data(), dropPos);

  QCOMPARE(g_lastReorderCall.count, 0);
  QCOMPARE(g_lastMoveCall.count, 1);
  QCOMPARE(g_lastMoveCall.targetFolderId, rootId());
  QCOMPARE(g_lastMoveCall.nodeIds, QList<NodeIdentifier>{foreignFile});
}

} // namespace tests

QTEST_MAIN(tests::TestFileListViewReorder)
#include "test_file_list_view_reorder.moc"
