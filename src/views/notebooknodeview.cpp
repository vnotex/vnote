#include "notebooknodeview.h"

#include <QContextMenuEvent>
#include <QDataStream>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QQueue>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QUrl>

#include <controllers/notebooknodecontroller.h>
#include <core/fileopenparameters.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>
#include <qmenu.h>

using namespace vnotex;

namespace {

// Internal MIME type for node identifiers
const QString c_nodeMimeType = QStringLiteral("application/x-vnotex-node-identifier");

// Decode NodeIdentifier list from MIME data
QList<NodeIdentifier> decodeNodeMimeData(const QMimeData *p_mimeData) {
  QList<NodeIdentifier> nodes;
  if (!p_mimeData || !p_mimeData->hasFormat(c_nodeMimeType)) {
    return nodes;
  }

  QByteArray encodedData = p_mimeData->data(c_nodeMimeType);
  QDataStream stream(&encodedData, QIODevice::ReadOnly);

  while (!stream.atEnd()) {
    QString notebookId, relativePath;
    stream >> notebookId >> relativePath;
    NodeIdentifier nodeId;
    nodeId.notebookId = notebookId;
    nodeId.relativePath = relativePath;
    if (nodeId.isValid()) {
      nodes.append(nodeId);
    }
  }
  return nodes;
}

// Check if target is same as or descendant of any source node (circular drop prevention)
bool isDropOnSelfOrDescendant(const QList<NodeIdentifier> &p_sources,
                              const NodeIdentifier &p_target) {
  for (const NodeIdentifier &source : p_sources) {
    if (source.notebookId != p_target.notebookId) {
      continue;
    }
    // Check if target is the same as source
    if (source.relativePath == p_target.relativePath) {
      return true;
    }
    // Check if target is a descendant of source
    if (!source.relativePath.isEmpty() &&
        p_target.relativePath.startsWith(source.relativePath + QLatin1Char('/'))) {
      return true;
    }
  }
  return false;
}

// === T9 helpers (notebook-explorer-drag-reorder) ===

// True when the view is currently in OrderedByConfiguration mode (the only
// sort mode under which drag-reorder is allowed). If the model is not a
// NotebookNodeProxyModel (e.g. tests using a bare source model) we
// conservatively treat it as ByConfig — there is no other order to violate.
bool isReorderSortModeActive(QAbstractItemModel *p_model) {
  if (auto *proxy = qobject_cast<NotebookNodeProxyModel *>(p_model)) {
    return proxy->viewOrder() == ViewOrder::OrderedByConfiguration;
  }
  return true;
}

// True when the proxy's name filter is non-empty. With a filter active the
// visible rows are a subset of the on-disk order, so a drag-reorder would
// re-order a partial view, corrupting the persisted child order.
bool isProxyFilterActive(QAbstractItemModel *p_model) {
  if (auto *proxy = qobject_cast<NotebookNodeProxyModel *>(p_model)) {
    return !proxy->nameFilter().isEmpty();
  }
  return false;
}

// 0 = all folders, 1 = all files, -1 = mixed/unknown. The check goes through
// the source NotebookNodeModel; if any dragged node is missing from the
// source's cache (e.g. cross-view drag from FileListView whose nodes the
// tree view has never seen) we return -1 so the dispatch path conservatively
// rejects.
int draggedTypeKind(NotebookNodeModel *p_nodeModel, const QList<NodeIdentifier> &p_draggedNodes) {
  if (!p_nodeModel || p_draggedNodes.isEmpty()) {
    return -1;
  }
  bool hasFolder = false;
  bool hasFile = false;
  for (const NodeIdentifier &id : p_draggedNodes) {
    NodeInfo info = p_nodeModel->nodeInfoFromNodeId(id);
    if (!info.isValid()) {
      return -1;
    }
    if (info.isFolder) {
      hasFolder = true;
    } else {
      hasFile = true;
    }
    if (hasFolder && hasFile) {
      return -1;
    }
  }
  return hasFolder ? 0 : 1;
}

// True when every dragged node shares the target as its parent. Mirrors the
// loop that previously rejected same-parent drops outright — preserved here
// because the reorder dispatch is gated by the same "all-same-parent"
// predicate, just with a different decision (accept reorder instead of
// reject) when the other conditions line up.
bool allDraggedShareParent(NotebookNodeController *p_controller,
                           const QList<NodeIdentifier> &p_draggedNodes,
                           const NodeIdentifier &p_targetId) {
  for (const NodeIdentifier &n : p_draggedNodes) {
    NodeIdentifier nodeParent = p_controller ? p_controller->getParentFolder(n) : NodeIdentifier();
    if (nodeParent.notebookId != p_targetId.notebookId ||
        nodeParent.relativePath != p_targetId.relativePath) {
      return false;
    }
  }
  return true;
}

} // anonymous namespace

NotebookNodeView::NotebookNodeView(QWidget *p_parent) : QTreeView(p_parent) { setupView(); }

NotebookNodeView::~NotebookNodeView() {}

void NotebookNodeView::setupView() {
  // Enable drag and drop
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDefaultDropAction(Qt::MoveAction);

  // Selection mode
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  // Appearance
  setHeaderHidden(true);
  setIndentation(16);
  setUniformRowHeights(true);
  setAnimated(true);

  // Enable lazy loading
  setAutoExpandDelay(500);

  // Disable default edit triggers - we control editing explicitly via edit() calls
  setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Connect activated signal
  connect(this, &QTreeView::activated, this, &NotebookNodeView::onItemActivated);

  // Connect expanded signal to prefetch grandchildren
  connect(this, &QTreeView::expanded, this, &NotebookNodeView::onItemExpanded);
}

void NotebookNodeView::setController(NotebookNodeController *p_controller) {
  // T9 (notebook-explorer-drag-reorder): swap-safe connection lifecycle.
  // Disconnect any prior nodesReordered handler before re-pointing m_controller
  // so callers that hot-swap controllers don't end up with a stale receiver.
  if (m_reorderedConn) {
    disconnect(m_reorderedConn);
    m_reorderedConn = QMetaObject::Connection();
  }

  m_controller = p_controller;

  if (m_controller) {
    // Mirror the refresh strategy used after moveNodes/pasteNodes (controller
    // calls NotebookNodeModel::reloadNode(parentId) — see
    // notebooknodecontroller.cpp:792, :1116, etc.). For reorder the controller
    // does NOT touch the model itself (it only emits nodesReordered after the
    // worker thread finishes), so the view owns the refresh hook here.
    m_reorderedConn = connect(m_controller, &NotebookNodeController::nodesReordered, this,
                              [this](const NodeIdentifier &p_parentId) {
                                auto *proxy = qobject_cast<NotebookNodeProxyModel *>(model());
                                auto *nodeModel = qobject_cast<NotebookNodeModel *>(
                                    proxy ? proxy->sourceModel() : model());
                                if (nodeModel) {
                                  nodeModel->reloadNode(p_parentId);
                                }
                              });
  }
}

NotebookNodeController *NotebookNodeView::controller() const { return m_controller; }

NodeIdentifier NotebookNodeView::currentNodeId() const {
  if (!selectionModel() || !selectionModel()->hasSelection()) {
    return NodeIdentifier();
  }
  QModelIndex currentIdx = currentIndex();
  return nodeIdFromIndex(currentIdx);
}

QList<NodeIdentifier> NotebookNodeView::selectedNodeIds() const {
  QList<NodeIdentifier> nodeIds;
  QModelIndexList selected = selectedIndexes();

  for (const QModelIndex &idx : selected) {
    if (idx.column() == 0) { // Only process first column
      NodeIdentifier nodeId = nodeIdFromIndex(idx);
      if (nodeId.isValid()) {
        nodeIds.append(nodeId);
      }
    }
  }

  return nodeIds;
}

void NotebookNodeView::selectNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    clearSelection();
    return;
  }

  QModelIndex idx = indexFromNodeId(p_nodeId);
  if (!idx.isValid()) {
    // Target index is not yet materialized — walk the ancestor chain and
    // fetchMore() at each step so the proxy/source caches are populated
    // before retrying. Mirrors the proven pattern in expandToNode() above,
    // but only fetches (no expand) since selection does not require the
    // ancestor rows to be visually expanded.
    if (model()) {
      // Ensure root children are loaded (may be empty after notebook/display-root switch).
      if (model()->canFetchMore(QModelIndex())) {
        model()->fetchMore(QModelIndex());
      }

      // Walk strict ancestors only (NOT the target itself).
      QStringList components = p_nodeId.relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
      QString currentPath;
      for (int i = 0; i < components.size() - 1; ++i) {
        if (!currentPath.isEmpty()) {
          currentPath += QLatin1Char('/');
        }
        currentPath += components[i];

        NodeIdentifier parentId;
        parentId.notebookId = p_nodeId.notebookId;
        parentId.relativePath = currentPath;

        QModelIndex parentIdx = indexFromNodeId(parentId);
        if (parentIdx.isValid() && model()->canFetchMore(parentIdx)) {
          model()->fetchMore(parentIdx);
        }
      }
    }

    // Retry once after the walk.
    idx = indexFromNodeId(p_nodeId);
    if (!idx.isValid()) {
      qWarning() << "NotebookNodeView::selectNode: failed to resolve" << p_nodeId.notebookId
                 << p_nodeId.relativePath;
      return;
    }
  }

  selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  setCurrentIndex(idx);
}

void NotebookNodeView::expandToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Build path from root to node by parsing the relativePath
  QString path = p_nodeId.relativePath;
  if (path.isEmpty()) {
    return; // Root node, nothing to expand
  }

  // Ensure root children are loaded (may be empty after notebook/display-root switch).
  if (model()->canFetchMore(QModelIndex())) {
    model()->fetchMore(QModelIndex());
  }

  // Split path into components and expand each parent
  QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString currentPath;

  for (int i = 0; i < components.size() - 1; ++i) {
    if (!currentPath.isEmpty()) {
      currentPath += QLatin1Char('/');
    }
    currentPath += components[i];

    NodeIdentifier parentId;
    parentId.notebookId = p_nodeId.notebookId;
    parentId.relativePath = currentPath;

    QModelIndex idx = indexFromNodeId(parentId);
    if (idx.isValid()) {
      expand(idx);
    }
  }
}

void NotebookNodeView::scrollToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  QModelIndex idx = indexFromNodeId(p_nodeId);
  if (idx.isValid()) {
    scrollTo(idx, QAbstractItemView::EnsureVisible);
  }
}

void NotebookNodeView::expandAll() { QTreeView::expandAll(); }

void NotebookNodeView::collapseAll() { QTreeView::collapseAll(); }

QList<NodeIdentifier> NotebookNodeView::getExpandedFolders() const {
  QList<NodeIdentifier> result;
  if (!model()) {
    return result;
  }

  // BFS traversal ensures parent-before-child order.
  QQueue<QModelIndex> queue;
  int rootRows = model()->rowCount(QModelIndex());
  for (int i = 0; i < rootRows; ++i) {
    queue.enqueue(model()->index(i, 0, QModelIndex()));
  }

  while (!queue.isEmpty()) {
    QModelIndex idx = queue.dequeue();
    if (!idx.isValid()) {
      continue;
    }

    if (isExpanded(idx)) {
      NodeInfo info = nodeInfoFromIndex(idx);
      if (info.isValid() && info.isFolder) {
        result.append(info.id);
      }

      // Enqueue children for further traversal.
      int childCount = model()->rowCount(idx);
      for (int i = 0; i < childCount; ++i) {
        queue.enqueue(model()->index(i, 0, idx));
      }
    }
  }

  return result;
}

void NotebookNodeView::replayExpandedFolders(const QList<NodeIdentifier> &p_folders) {
  if (!model() || p_folders.isEmpty()) {
    return;
  }

  // Ensure root children are loaded (may be empty after notebook/display-root switch).
  if (model()->canFetchMore(QModelIndex())) {
    model()->fetchMore(QModelIndex());
  }

  for (const NodeIdentifier &folderId : p_folders) {
    if (!folderId.isValid()) {
      continue;
    }

    QModelIndex idx = indexFromNodeId(folderId);
    if (!idx.isValid()) {
      continue; // Stale/missing ID — skip gracefully.
    }

    // Fetch children before expanding so the tree has data to show.
    if (model()->canFetchMore(idx)) {
      model()->fetchMore(idx);
    }

    expand(idx);
  }
}

NodeIdentifier NotebookNodeView::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeIdentifier();
  }

  // Handle proxy model
  QModelIndex sourceIdx = p_index;
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  if (proxyModel) {
    sourceIdx = proxyModel->mapToSource(p_index);
  }

  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (nodeModel) {
    return nodeModel->nodeIdFromIndex(sourceIdx);
  }

  return NodeIdentifier();
}

NodeInfo NotebookNodeView::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeInfo();
  }

  // Handle proxy model
  QModelIndex sourceIdx = p_index;
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  if (proxyModel) {
    sourceIdx = proxyModel->mapToSource(p_index);
  }

  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (nodeModel) {
    NodeInfo result = nodeModel->nodeInfoFromIndex(sourceIdx);
    return result;
  }

  return NodeInfo();
}

QModelIndex NotebookNodeView::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid()) {
    return QModelIndex();
  }

  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());

  if (!nodeModel) {
    return QModelIndex();
  }

  QModelIndex sourceIdx = nodeModel->indexFromNodeId(p_nodeId);
  if (proxyModel) {
    return proxyModel->mapFromSource(sourceIdx);
  }

  return sourceIdx;
}

void NotebookNodeView::mousePressEvent(QMouseEvent *p_event) {
  QTreeView::mousePressEvent(p_event);

  // Handle single-click activation if enabled
  if (p_event->button() == Qt::LeftButton && isSingleClickActivationEnabled()) {
    QModelIndex idx = indexAt(p_event->pos());
    if (idx.isValid()) {
      NodeInfo nodeInfo = nodeInfoFromIndex(idx);
      if (nodeInfo.isValid() && !nodeInfo.isFolder) {
        m_controller->openNode(nodeInfo.id);
      }
    }
  }
}

void NotebookNodeView::mouseDoubleClickEvent(QMouseEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  if (!idx.isValid()) {
    QTreeView::mouseDoubleClickEvent(p_event);
    return;
  }

  NodeInfo nodeInfo = nodeInfoFromIndex(idx);
  if (nodeInfo.isValid()) {
    // For containers, toggle expand/collapse (default behavior)
    // For content nodes, activate
    if (!nodeInfo.isFolder) {
      m_controller->openNode(nodeInfo.id);
      p_event->accept();
      return;
    }
  }

  QTreeView::mouseDoubleClickEvent(p_event);
}

void NotebookNodeView::keyPressEvent(QKeyEvent *p_event) {
  switch (p_event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter: {
    // Don't activate while inline editing (e.g., rename).
    // The Enter commits the edit; let QTreeView handle it.
    if (state() == QAbstractItemView::EditingState) {
      break;
    }
    NodeInfo nodeInfo = nodeInfoFromIndex(currentIndex());
    if (nodeInfo.isValid() && !nodeInfo.isFolder) {
      m_controller->openNode(nodeInfo.id);
      p_event->accept();
      return;
    }
    break;
  }
  case Qt::Key_Delete: {
    // Handled by controller via context menu or shortcut
    break;
  }
  case Qt::Key_F2: {
    // Rename - trigger inline edit
    QModelIndex idx = currentIndex();
    if (idx.isValid()) {
      edit(idx);
      p_event->accept();
      return;
    }
    break;
  }
  default:
    break;
  }

  QTreeView::keyPressEvent(p_event);
}

void NotebookNodeView::contextMenuEvent(QContextMenuEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  NodeIdentifier nodeId = nodeIdFromIndex(idx);

  emit contextMenuRequested(nodeId, p_event->globalPos());

  p_event->accept();
}

void NotebookNodeView::dragEnterEvent(QDragEnterEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();

  if (mimeData->hasFormat("application/x-vnotex-node-identifier")) {
    p_event->acceptProposedAction();
  } else if (mimeData->hasUrls()) {
    // External file drop
    p_event->acceptProposedAction();
  } else {
    p_event->ignore();
  }
}

void NotebookNodeView::dragMoveEvent(QDragMoveEvent *p_event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QPoint dropPos = p_event->position().toPoint();
#else
  const QPoint dropPos = p_event->pos();
#endif

  QModelIndex idx = indexAt(dropPos);
  NodeInfo targetInfo = nodeInfoFromIndex(idx);
  NodeIdentifier targetId = nodeIdFromIndex(idx);

  // Resolve target NodeIdentifier (mirrors dropEvent logic). Drops on empty
  // viewport area fall back to the notebook root.
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (!targetId.isValid()) {
    if (nodeModel) {
      targetId.notebookId = nodeModel->getNotebookId();
      targetId.relativePath = QString(); // root
    }
  }

  // If the cursor sits on a FILE row, the move/copy semantics are "drop into
  // this file's parent folder", so rewrite the target. Reorder semantics use
  // the original file index as the ANCHOR; we preserve idx + targetInfo for
  // that purpose and only rewrite targetId.
  if (targetId.isValid() && !targetId.relativePath.isEmpty() && !targetInfo.isFolder) {
    if (m_controller) {
      targetId = m_controller->getParentFolder(targetId);
    }
  }

  // Let QTreeView compute the drop indicator position FIRST. Reading it
  // before this point would yield stale data from the previous drag-move.
  QTreeView::dragMoveEvent(p_event);
  const DropIndicatorPosition indicatorPos = dropIndicatorPosition();

  // === Dispatch decision ===
  // Reorder is only meaningful for MOVE-action drags carrying our internal
  // mime payload. Copy drags and external URL drops keep their previous
  // accept/ignore decisions (set by the existing logic below).
  Qt::DropAction proposedAction = Qt::MoveAction;
  if (p_event->keyboardModifiers() & Qt::ControlModifier) {
    proposedAction = Qt::CopyAction;
  }

  const QMimeData *mimeData = p_event->mimeData();
  const bool isNodeDrag = proposedAction == Qt::MoveAction && targetId.isValid() && mimeData &&
                          mimeData->hasFormat(c_nodeMimeType);

  if (isNodeDrag) {
    QList<NodeIdentifier> draggedNodes = decodeNodeMimeData(mimeData);
    if (!draggedNodes.isEmpty()) {
      const bool sameParent = allDraggedShareParent(m_controller, draggedNodes, targetId);

      if (sameParent) {
        // === Candidate for the new reorder path ===
        // Per the plan-locked decision (Wave 3, T9), check sort mode +
        // filter at BOTH dragMove and drop time — the toolbar could flip
        // either mid-drag.
        if (!isReorderSortModeActive(model()) || isProxyFilterActive(model())) {
          p_event->setDropAction(Qt::IgnoreAction);
          p_event->ignore();
          setDropIndicatorShown(false);
          return;
        }
        // OnItem / OnViewport on the same parent are no-ops (move-into-own-
        // parent / no anchor). Silent reject preserves the pre-T9 semantics
        // for OnItem and adds the OnViewport guard.
        if (indicatorPos == OnItem || indicatorPos == OnViewport) {
          p_event->setDropAction(Qt::IgnoreAction);
          p_event->ignore();
          setDropIndicatorShown(false);
          return;
        }
        // Mixed-type drag: must reject. Folders and files reorder
        // independently; mixing them would force the dispatch to touch
        // both sub-arrays of vx.json, which is out of scope for T9.
        const int draggedKind = draggedTypeKind(nodeModel, draggedNodes);
        if (draggedKind < 0) {
          p_event->setDropAction(Qt::IgnoreAction);
          p_event->ignore();
          setDropIndicatorShown(false);
          return;
        }
        // Anchor type must match dragged type — you can't reorder a file
        // relative to a folder anchor (or vice versa) because the folder
        // section always sorts above the file section under folder-first.
        NodeInfo anchorInfo = nodeInfoFromIndex(idx);
        if (!anchorInfo.isValid()) {
          // No anchor (shouldn't happen for AboveItem/BelowItem with a
          // valid indicator, but defensively reject).
          p_event->setDropAction(Qt::IgnoreAction);
          p_event->ignore();
          setDropIndicatorShown(false);
          return;
        }
        const bool draggedIsFolder = (draggedKind == 0);
        if (anchorInfo.isFolder != draggedIsFolder) {
          p_event->setDropAction(Qt::IgnoreAction);
          p_event->ignore();
          setDropIndicatorShown(false);
          return;
        }
        // All checks pass — accept reorder. The drop will be persisted by
        // dropEvent via the controller.
        p_event->setDropAction(Qt::MoveAction);
        p_event->acceptProposedAction();
        setDropIndicatorShown(true);
        return;
      }
      // sameParent == false → fall through to the existing cross-parent
      // accept-if-folder logic below.
    }
  }

  // === Existing cross-parent / non-reorder accept logic (preserved) ===
  // Only accept drops on containers.
  if (targetInfo.isValid() && targetInfo.isFolder) {
    p_event->acceptProposedAction();
    setDropIndicatorShown(true);
  } else if (!targetInfo.isValid()) {
    // Dropping on root (empty area)
    p_event->acceptProposedAction();
    setDropIndicatorShown(true);
  } else {
    p_event->ignore();
  }
}

void NotebookNodeView::dropEvent(QDropEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QPoint dropPos = p_event->position().toPoint();
#else
  const QPoint dropPos = p_event->pos();
#endif
  QModelIndex idx = indexAt(dropPos);
  NodeIdentifier targetId = nodeIdFromIndex(idx);
  NodeInfo targetInfo = nodeInfoFromIndex(idx);

  // If dropping on empty area or invalid index, target is notebook root
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (!targetId.isValid()) {
    if (nodeModel) {
      targetId.notebookId = nodeModel->getNotebookId();
      targetId.relativePath = QString(); // root
    }
  }

  // If target is a file (not folder), use its parent folder as the drop
  // target. Keep idx + targetInfo for anchor lookup in the reorder path.
  if (targetId.isValid() && !targetId.relativePath.isEmpty() && !targetInfo.isFolder) {
    if (m_controller) {
      targetId = m_controller->getParentFolder(targetId);
    }
    if (!targetId.isValid()) {
      p_event->ignore();
      return;
    }
  }

  // T9: re-read indicator at drop time. dragMoveEvent already updated it,
  // but we re-read here so this method is correct even if invoked
  // out-of-band by tests.
  const DropIndicatorPosition indicatorPos = dropIndicatorPosition();

  if (mimeData->hasFormat(c_nodeMimeType)) {
    // Decode dragged nodes
    QList<NodeIdentifier> draggedNodes = decodeNodeMimeData(mimeData);

    if (draggedNodes.isEmpty() || !m_controller) {
      p_event->ignore();
      return;
    }

    // Prevent dropping folder into itself or descendant
    if (isDropOnSelfOrDescendant(draggedNodes, targetId)) {
      p_event->ignore();
      return;
    }

    // Determine action: Ctrl = Copy, otherwise Move
    Qt::DropAction action = Qt::MoveAction;
    if (p_event->keyboardModifiers() & Qt::ControlModifier) {
      action = Qt::CopyAction;
    }

    if (action == Qt::CopyAction) {
      // Copy semantics are unchanged: copy + paste into target folder.
      m_controller->copyNodes(draggedNodes);
      m_controller->pasteNodes(targetId);
      p_event->setDropAction(action);
      p_event->accept();
      return;
    }

    // === MOVE branch ===
    const bool sameParent = allDraggedShareParent(m_controller, draggedNodes, targetId);

    if (sameParent) {
      // === T9 same-parent reorder dispatch ===
      // Re-check sort mode + filter — they could have flipped between
      // dragMoveEvent and dropEvent via the toolbar.
      if (!isReorderSortModeActive(model()) || isProxyFilterActive(model())) {
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->accept();
        return;
      }
      if (indicatorPos == OnItem || indicatorPos == OnViewport) {
        // OnItem on same parent would be a move-into-own-parent no-op;
        // OnViewport has no anchor. Both are silent rejects.
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->accept();
        return;
      }
      const int draggedKind = draggedTypeKind(nodeModel, draggedNodes);
      if (draggedKind < 0) {
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->accept();
        return;
      }
      const bool draggedIsFolder = (draggedKind == 0);
      NodeInfo anchorInfo = nodeInfoFromIndex(idx);
      if (!anchorInfo.isValid() || anchorInfo.isFolder != draggedIsFolder) {
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->accept();
        return;
      }

      // Build the new ordered list for the dragged-type sub-array. Per the
      // plan: enumerate proxy children of parentId, filter to dragged type,
      // remove dragged ids, insert as contiguous block at the indicator
      // position relative to the anchor.
      QModelIndex parentIdx;
      if (!targetId.relativePath.isEmpty()) {
        parentIdx = indexFromNodeId(targetId);
      }
      // parentIdx invalid = proxy root.

      QList<NodeIdentifier> ordered;
      const int rowCount = model()->rowCount(parentIdx);
      for (int i = 0; i < rowCount; ++i) {
        QModelIndex childIdx = model()->index(i, 0, parentIdx);
        NodeInfo childInfo = nodeInfoFromIndex(childIdx);
        if (!childInfo.isValid()) {
          continue;
        }
        if (childInfo.isFolder == draggedIsFolder) {
          ordered.append(childInfo.id);
        }
      }

      QSet<NodeIdentifier> draggedSet;
      for (const auto &n : draggedNodes) {
        draggedSet.insert(n);
      }
      QList<NodeIdentifier> cleaned;
      cleaned.reserve(ordered.size());
      for (const auto &id : ordered) {
        if (!draggedSet.contains(id)) {
          cleaned.append(id);
        }
      }

      int anchorPos = -1;
      for (int i = 0; i < cleaned.size(); ++i) {
        if (cleaned.at(i) == anchorInfo.id) {
          anchorPos = i;
          break;
        }
      }
      int insertPos = (anchorPos < 0) ? cleaned.size()
                                      : (indicatorPos == AboveItem ? anchorPos : anchorPos + 1);

      for (int i = 0; i < draggedNodes.size(); ++i) {
        cleaned.insert(insertPos + i, draggedNodes.at(i));
      }

      // Split into the right sub-array. Pass the OTHER list empty so the
      // service leaves that section untouched (per the contract documented
      // on NotebookNodeController::reorderNodes).
      if (draggedIsFolder) {
        m_controller->reorderNodes(targetId, cleaned, QList<NodeIdentifier>{});
      } else {
        m_controller->reorderNodes(targetId, QList<NodeIdentifier>{}, cleaned);
      }
      p_event->setDropAction(Qt::MoveAction);
      p_event->acceptProposedAction();
      return;
    }

    // === Cross-parent: existing move path ===
    QList<NodeIdentifier> filteredNodes;
    for (const NodeIdentifier &n : draggedNodes) {
      NodeIdentifier nodeParent = m_controller->getParentFolder(n);
      // Field-wise comparison: notebookId AND relativePath
      if (nodeParent.notebookId != targetId.notebookId ||
          nodeParent.relativePath != targetId.relativePath) {
        filteredNodes.append(n);
      }
    }

    if (filteredNodes.isEmpty()) {
      p_event->setDropAction(Qt::IgnoreAction);
      p_event->accept();
      return;
    }

    m_controller->moveNodes(filteredNodes, targetId);
    p_event->setDropAction(action);
    p_event->accept();
    return;
  }

  if (mimeData->hasUrls()) {
    // External file import
    QStringList filePaths;
    for (const QUrl &url : mimeData->urls()) {
      if (url.isLocalFile()) {
        filePaths.append(url.toLocalFile());
      }
    }

    if (!filePaths.isEmpty() && m_controller) {
      m_controller->handleImportFiles(targetId, filePaths);
      p_event->acceptProposedAction();
      return;
    }
  }

  p_event->ignore();
}

void NotebookNodeView::selectionChanged(const QItemSelection &p_selected,
                                        const QItemSelection &p_deselected) {
  QTreeView::selectionChanged(p_selected, p_deselected);

  QList<NodeIdentifier> nodeIds = selectedNodeIds();
  emit nodeSelectionChanged(nodeIds);
}

void NotebookNodeView::onItemActivated(const QModelIndex &p_index) {
  NodeInfo nodeInfo = nodeInfoFromIndex(p_index);
  if (nodeInfo.isValid() && !nodeInfo.isFolder) {
    m_controller->openNode(nodeInfo.id);
  }
}

void NotebookNodeView::onItemExpanded(const QModelIndex &p_index) {
  if (!p_index.isValid()) {
    return;
  }

  // Map to source index if using proxy model
  QModelIndex sourceIdx = p_index;
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  if (proxyModel) {
    sourceIdx = proxyModel->mapToSource(p_index);
  }

  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (nodeModel) {
    nodeModel->prefetchChildrenOfChildren(sourceIdx);
  }
}

bool NotebookNodeView::isSingleClickActivationEnabled() const {
  if (m_controller) {
    return m_controller->isSingleClickActivationEnabled();
  }
  return false;
}
