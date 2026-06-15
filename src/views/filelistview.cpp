#include "filelistview.h"

#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>

#include <QUrl>

#include <QAbstractProxyModel>

#include <controllers/notebooknodecontroller.h>
#include <core/fileopenparameters.h>
#include <core/global.h>
#include <models/inodelistmodel.h>
#include <models/notebooknodeproxymodel.h>

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

} // anonymous namespace

FileListView::FileListView(QWidget *p_parent) : QListView(p_parent) { setupView(); }

FileListView::~FileListView() {}

void FileListView::setupView() {
  // Enable drag and drop
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDefaultDropAction(Qt::MoveAction);

  // Selection mode
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setSelectionBehavior(QAbstractItemView::SelectRows);

  // List view specific settings
  setViewMode(QListView::ListMode);
  setFlow(QListView::TopToBottom);
  setWrapping(false);
  setUniformItemSizes(true);
  setSpacing(0);

  // Set object name for QSS styling
  setObjectName(QStringLiteral("FileListView"));

  // Disable default edit triggers - we control editing explicitly via edit() calls
  setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Connect activated signal
  connect(this, &QListView::activated, this, &FileListView::onItemActivated);
}

void FileListView::setController(NotebookNodeController *p_controller) {
  if (m_controller == p_controller) {
    return;
  }

  // T10 (drag-reorder): drop any prior nodesReordered subscription before
  // rebinding. Without this, repeated setController calls would stack up
  // lambdas that all call reloadNode on every reorder.
  if (m_reorderConnection) {
    disconnect(m_reorderConnection);
    m_reorderConnection = QMetaObject::Connection();
  }

  m_controller = p_controller;

  if (m_controller) {
    // T10 (drag-reorder): when the controller confirms a reorder, ask it to
    // reload the affected parent so the view picks up the new on-disk order.
    // The controller owns the model and knows how to refresh it; the view
    // stays decoupled from vxcore.
    m_reorderConnection = connect(m_controller, &NotebookNodeController::nodesReordered, this,
                                  [this](const NodeIdentifier &p_parentId) {
                                    if (m_controller) {
                                      m_controller->reloadNode(p_parentId);
                                    }
                                  });
  }
}

NotebookNodeController *FileListView::controller() const { return m_controller; }

NodeIdentifier FileListView::currentNodeId() const {
  if (!selectionModel() || !selectionModel()->hasSelection()) {
    return NodeIdentifier();
  }
  QModelIndex currentIdx = currentIndex();
  return nodeIdFromIndex(currentIdx);
}

QList<NodeIdentifier> FileListView::selectedNodeIds() const {
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

void FileListView::selectNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    clearSelection();
    return;
  }

  // Ensure root children are loaded (may be empty after notebook/display-root switch).
  if (model()->canFetchMore(QModelIndex())) {
    model()->fetchMore(QModelIndex());
  }

  QModelIndex idx = indexFromNodeId(p_nodeId);
  if (idx.isValid()) {
    selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    setCurrentIndex(idx);
  }
}

void FileListView::scrollToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Ensure root children are loaded (may be empty after notebook/display-root switch).
  if (model()->canFetchMore(QModelIndex())) {
    model()->fetchMore(QModelIndex());
  }

  QModelIndex idx = indexFromNodeId(p_nodeId);
  if (idx.isValid()) {
    scrollTo(idx, QAbstractItemView::EnsureVisible);
  }
}

NodeIdentifier FileListView::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeIdentifier();
  }
  // Read role directly — works with any model including proxy models
  // (proxy models forward data() calls to source model automatically)
  QVariant v = p_index.data(INodeListModel::NodeIdentifierRole);
  if (v.isValid()) {
    return v.value<NodeIdentifier>();
  }
  return NodeIdentifier();
}

NodeInfo FileListView::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeInfo();
  }
  QVariant v = p_index.data(INodeListModel::NodeInfoRole);
  if (v.isValid()) {
    return v.value<NodeInfo>();
  }
  return NodeInfo();
}

QModelIndex FileListView::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid()) {
    return QModelIndex();
  }
  QAbstractItemModel *m = model();
  if (!m) {
    return QModelIndex();
  }
  for (int i = 0; i < m->rowCount(); ++i) {
    QModelIndex idx = m->index(i, 0);
    QVariant v = idx.data(INodeListModel::NodeIdentifierRole);
    if (v.isValid() && v.value<NodeIdentifier>() == p_nodeId) {
      return idx;
    }
  }
  return QModelIndex();
}

QObject *FileListView::resolveDropSource(QDropEvent *p_event) const {
  return p_event ? p_event->source() : nullptr;
}

bool FileListView::isReorderAllowed() const {
  auto *proxy = qobject_cast<NotebookNodeProxyModel *>(model());
  if (!proxy) {
    return false;
  }
  if (proxy->viewOrder() != ViewOrder::OrderedByConfiguration) {
    return false;
  }
  if (!proxy->nameFilter().isEmpty()) {
    return false;
  }
  return true;
}

QList<NodeIdentifier> FileListView::currentFileOrderInView() const {
  QList<NodeIdentifier> result;
  QAbstractItemModel *m = model();
  if (!m) {
    return result;
  }
  const int count = m->rowCount();
  result.reserve(count);
  for (int i = 0; i < count; ++i) {
    QModelIndex idx = m->index(i, 0);
    NodeIdentifier id = nodeIdFromIndex(idx);
    if (id.isValid()) {
      result.append(id);
    }
  }
  return result;
}

QList<NodeIdentifier>
FileListView::computeReorderedFileIds(const QList<NodeIdentifier> &p_dragged,
                                      const NodeIdentifier &p_anchor,
                                      QAbstractItemView::DropIndicatorPosition p_pos) const {
  QList<NodeIdentifier> currentOrder = currentFileOrderInView();

  // Drop the dragged items from their current positions so we can re-insert
  // them as one contiguous block at the anchor.
  QSet<NodeIdentifier> draggedSet(p_dragged.cbegin(), p_dragged.cend());
  QList<NodeIdentifier> withoutDragged;
  withoutDragged.reserve(currentOrder.size());
  for (const auto &id : currentOrder) {
    if (!draggedSet.contains(id)) {
      withoutDragged.append(id);
    }
  }

  // Resolve the anchor inside the remaining list. If the anchor itself was
  // dragged (Qt sometimes nominates one of the selected rows as anchor) or
  // the index can't be matched, fall back to appending at the end — there is
  // nothing meaningful to anchor against.
  int anchorRow = -1;
  if (p_anchor.isValid()) {
    for (int i = 0; i < withoutDragged.size(); ++i) {
      if (withoutDragged[i] == p_anchor) {
        anchorRow = i;
        break;
      }
    }
  }

  int insertAt;
  if (anchorRow < 0) {
    insertAt = withoutDragged.size();
  } else if (p_pos == QAbstractItemView::BelowItem) {
    insertAt = anchorRow + 1;
  } else {
    // AboveItem (and any non-Below caller that reached this helper).
    insertAt = anchorRow;
  }

  QList<NodeIdentifier> result = withoutDragged;
  for (int i = 0; i < p_dragged.size(); ++i) {
    result.insert(insertAt + i, p_dragged[i]);
  }
  return result;
}

void FileListView::mousePressEvent(QMouseEvent *p_event) {
  QListView::mousePressEvent(p_event);

  // Handle single-click activation if enabled
  if (p_event->button() == Qt::LeftButton && isSingleClickActivationEnabled()) {
    QModelIndex idx = indexAt(p_event->pos());
    if (idx.isValid()) {
      NodeInfo nodeInfo = nodeInfoFromIndex(idx);
      if (nodeInfo.isValid() && !nodeInfo.isFolder && m_controller) {
        m_controller->openNode(nodeInfo.id);
      }
    }
  }
}

void FileListView::mouseDoubleClickEvent(QMouseEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  if (!idx.isValid()) {
    QListView::mouseDoubleClickEvent(p_event);
    return;
  }

  NodeInfo nodeInfo = nodeInfoFromIndex(idx);
  if (nodeInfo.isValid()) {
    // For files, activate (open)
    if (!nodeInfo.isFolder && m_controller) {
      m_controller->openNode(nodeInfo.id);
      p_event->accept();
      return;
    }
  }

  QListView::mouseDoubleClickEvent(p_event);
}

void FileListView::keyPressEvent(QKeyEvent *p_event) {
  switch (p_event->key()) {
  case Qt::Key_Return:
  case Qt::Key_Enter: {
    NodeInfo nodeInfo = nodeInfoFromIndex(currentIndex());
    if (nodeInfo.isValid() && !nodeInfo.isFolder && m_controller) {
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

  QListView::keyPressEvent(p_event);
}

void FileListView::contextMenuEvent(QContextMenuEvent *p_event) {
  QModelIndex idx = indexAt(p_event->pos());
  NodeIdentifier nodeId = nodeIdFromIndex(idx);

  emit contextMenuRequested(nodeId, p_event->globalPos());
  p_event->accept();
}

void FileListView::dragEnterEvent(QDragEnterEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();

  // Resolve the drop target notebook for read-only gating (same resolution as
  // dragMoveEvent / dropEvent: display root with notebook-root fallback).
  auto *nodeListModel = dynamic_cast<INodeListModel *>(model());
  if (!nodeListModel) {
    auto *proxy = qobject_cast<QAbstractProxyModel *>(model());
    if (proxy) {
      nodeListModel = dynamic_cast<INodeListModel *>(proxy->sourceModel());
    }
  }
  NodeIdentifier targetId;
  if (nodeListModel) {
    targetId = nodeListModel->getDisplayRoot();
    if (!targetId.isValid()) {
      targetId.notebookId = nodeListModel->getNotebookId();
      targetId.relativePath = QString();
    }
  }
  const bool targetReadOnly = m_controller && m_controller->isNotebookReadOnly(targetId.notebookId);

  if (mimeData->hasFormat("application/x-vnotex-node-identifier")) {
    // Read-only gating: block MOVE with read-only source (move-out) or
    // read-only target (move-in / reorder), and COPY into a read-only target
    // (copy-in). COPY-out (read-only source -> writable target) is allowed.
    Qt::DropAction proposedAction = Qt::MoveAction;
    if (p_event->keyboardModifiers() & Qt::ControlModifier) {
      proposedAction = Qt::CopyAction;
    }
    bool blocked = false;
    if (proposedAction == Qt::MoveAction) {
      blocked = targetReadOnly ||
                (m_controller && m_controller->isSelectionReadOnly(decodeNodeMimeData(mimeData)));
    } else {
      blocked = targetReadOnly;
    }
    if (blocked) {
      p_event->ignore();
      return;
    }
    p_event->acceptProposedAction();
  } else if (mimeData->hasUrls()) {
    // External file drop: block import INTO a read-only target.
    if (targetReadOnly) {
      p_event->ignore();
      return;
    }
    p_event->acceptProposedAction();
  } else {
    p_event->ignore();
  }
}

void FileListView::dragMoveEvent(QDragMoveEvent *p_event) {
  // Let QListView compute dropIndicatorPosition() FIRST so our same-view
  // reorder branch can read a correct AboveItem / BelowItem / OnItem value
  // for the current cursor pos. Without this, the very first dragMoveEvent
  // would see a stale (default) indicator and silently reject the drop.
  QListView::dragMoveEvent(p_event);

  // Check if model supports drag & drop via INodeListModel interface
  auto *nodeListModel = dynamic_cast<INodeListModel *>(model());
  if (!nodeListModel) {
    // Check source model if proxy
    auto *proxy = qobject_cast<QAbstractProxyModel *>(model());
    if (proxy) {
      nodeListModel = dynamic_cast<INodeListModel *>(proxy->sourceModel());
    }
  }

  if (nodeListModel && nodeListModel->supportsDragDrop()) {
    // In FileListView, target is the current display root folder
    NodeIdentifier targetId = nodeListModel->getDisplayRoot();
    if (!targetId.isValid()) {
      // Fall back to notebook root
      targetId.notebookId = nodeListModel->getNotebookId();
      targetId.relativePath = QString();
    }

    // Determine proposed action: Ctrl = Copy, otherwise Move
    Qt::DropAction proposedAction = Qt::MoveAction;
    if (p_event->keyboardModifiers() & Qt::ControlModifier) {
      proposedAction = Qt::CopyAction;
    }

    // Read-only gating (mirror dropEvent): block URL import into a read-only
    // target, MOVE with read-only source (move-out) or read-only target
    // (move-in / reorder), and COPY into a read-only target (copy-in).
    // COPY-out is allowed. Ignore so the cursor shows "forbidden".
    {
      const bool targetReadOnly =
          m_controller && m_controller->isNotebookReadOnly(targetId.notebookId);
      const QMimeData *dragMime = p_event->mimeData();
      bool blocked = false;
      if (dragMime->hasFormat(c_nodeMimeType)) {
        if (proposedAction == Qt::MoveAction) {
          blocked =
              targetReadOnly ||
              (m_controller && m_controller->isSelectionReadOnly(decodeNodeMimeData(dragMime)));
        } else {
          blocked = targetReadOnly;
        }
      } else if (dragMime->hasUrls()) {
        blocked = targetReadOnly;
      }
      if (blocked) {
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->ignore();
        setDropIndicatorShown(false);
        return;
      }
    }

    // T10 (drag-reorder): lifted same-parent guard. The original guard at
    // this site unconditionally rejected any same-parent move. We now allow
    // it through if and only if every gate passes (same view, AboveItem /
    // BelowItem indicator, ByConfig view order, no name filter active). All
    // other same-parent drops still reject silently so the user never sees a
    // forbidden cursor + no-op outcome.
    if (proposedAction == Qt::MoveAction && targetId.isValid()) {
      const QMimeData *mimeData = p_event->mimeData();
      if (mimeData->hasFormat(c_nodeMimeType)) {
        // Decode dragged nodes (cache locally, decode once)
        QList<NodeIdentifier> draggedNodes = decodeNodeMimeData(mimeData);

        if (!draggedNodes.isEmpty()) {
          // Check if ALL nodes have the same parent as target
          bool allSameParent = true;
          for (const NodeIdentifier &n : draggedNodes) {
            NodeIdentifier nodeParent =
                m_controller ? m_controller->getParentFolder(n) : NodeIdentifier();
            // Field-wise comparison: notebookId AND relativePath
            if (nodeParent.notebookId != targetId.notebookId ||
                nodeParent.relativePath != targetId.relativePath) {
              allSameParent = false;
              break;
            }
          }

          if (allSameParent) {
            // Same-parent path: only accept if the drop is a real reorder.
            const bool sameView = (resolveDropSource(p_event) == this);
            const QAbstractItemView::DropIndicatorPosition pos = dropIndicatorPosition();
            const bool isAnchor =
                (pos == QAbstractItemView::AboveItem || pos == QAbstractItemView::BelowItem);

            if (sameView && isAnchor && isReorderAllowed()) {
              p_event->acceptProposedAction();
              setDropIndicatorShown(true);
              return;
            }

            // Same parent, but reorder is not eligible: reject silently so
            // the indicator vanishes and the OS shows the forbidden cursor.
            p_event->setDropAction(Qt::IgnoreAction);
            p_event->ignore();
            setDropIndicatorShown(false);
            return;
          }
        }
      }
    }
  }

  // Cross-parent / non-move / non-node-mime: keep the base class's accept
  // state and ensure the indicator stays visible for visual feedback.
  p_event->acceptProposedAction();
  setDropIndicatorShown(true);
}

void FileListView::dropEvent(QDropEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();

  // Check if model supports drag & drop via INodeListModel interface
  auto *nodeListModel = dynamic_cast<INodeListModel *>(model());
  if (!nodeListModel) {
    // Check source model if proxy
    auto *proxy = qobject_cast<QAbstractProxyModel *>(model());
    if (proxy) {
      nodeListModel = dynamic_cast<INodeListModel *>(proxy->sourceModel());
    }
  }
  if (!nodeListModel || !nodeListModel->supportsDragDrop()) {
    p_event->ignore();
    return;
  }

  // In FileListView, target is the current display root folder
  NodeIdentifier targetId = nodeListModel->getDisplayRoot();
  if (!targetId.isValid()) {
    // Fall back to notebook root
    targetId.notebookId = nodeListModel->getNotebookId();
    targetId.relativePath = QString();
  }

  if (!targetId.isValid()) {
    p_event->ignore();
    return;
  }

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

    // Perform operation via controller
    if (action == Qt::CopyAction) {
      // Read-only gating: block COPY INTO a read-only target (copy-in).
      // Copy-out (read-only source -> writable target) is allowed.
      if (m_controller->isNotebookReadOnly(targetId.notebookId)) {
        p_event->ignore();
        return;
      }
      m_controller->copyNodes(draggedNodes);
      m_controller->pasteNodes(targetId);
      p_event->setDropAction(action);
      p_event->accept();
      return;
    }

    // MOVE branch.
    // Read-only gating: block move-out (read-only source) and move-in / reorder
    // (read-only target). Silent.
    if (m_controller->isSelectionReadOnly(draggedNodes) ||
        m_controller->isNotebookReadOnly(targetId.notebookId)) {
      p_event->ignore();
      return;
    }

    // T10 (drag-reorder): same-parent + same-view + Above/Below indicator +
    // OrderedByConfiguration + empty filter routes to reorderNodes. Every
    // other branch falls back to the legacy "filter and move" path which
    // preserves cross-pane move-into-folder behavior.
    bool allSameParent = true;
    for (const NodeIdentifier &n : draggedNodes) {
      NodeIdentifier nodeParent = m_controller->getParentFolder(n);
      // Field-wise comparison: notebookId AND relativePath
      if (nodeParent.notebookId != targetId.notebookId ||
          nodeParent.relativePath != targetId.relativePath) {
        allSameParent = false;
        break;
      }
    }

    const bool sameView = (resolveDropSource(p_event) == this);
    const QAbstractItemView::DropIndicatorPosition pos = dropIndicatorPosition();
    const bool isAnchor =
        (pos == QAbstractItemView::AboveItem || pos == QAbstractItemView::BelowItem);

    if (allSameParent && sameView && isAnchor && isReorderAllowed()) {
      // Resolve the anchor index under the drop point. dropIndicatorPosition
      // is reported against indexAt(eventPos); use the same source-of-truth
      // here so the anchor and the indicator stay in lock-step.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      QModelIndex anchorIdx = indexAt(p_event->position().toPoint());
#else
      QModelIndex anchorIdx = indexAt(p_event->pos());
#endif
      NodeIdentifier anchorId = nodeIdFromIndex(anchorIdx);

      QList<NodeIdentifier> newOrder = computeReorderedFileIds(draggedNodes, anchorId, pos);

      // If the recomputed order is identical to the current order (e.g.,
      // dropping above the next sibling = no-op), short-circuit so the
      // controller and service are not woken up.
      if (newOrder == currentFileOrderInView()) {
        p_event->setDropAction(Qt::IgnoreAction);
        p_event->accept();
        return;
      }

      // FileListView only shows files, so the dragged nodes are files. The
      // folder list is therefore empty. The controller will validate the
      // permutation against the service.
      m_controller->reorderNodes(targetId, /*orderedFolderIds=*/{},
                                 /*orderedFileIds=*/newOrder);
      p_event->acceptProposedAction();
      return;
    }

    if (allSameParent) {
      // Same parent but no reorder routing: drop is a same-folder move that
      // would be a no-op. Preserve the legacy "silent reject" behavior.
      p_event->setDropAction(Qt::IgnoreAction);
      p_event->accept();
      return;
    }

    // Cross-parent MOVE: filter out nodes already at target parent (defense
    // in depth — the same-parent block above already covered the all-same
    // case, but a mixed selection from another pane may include a few that
    // happen to live at displayRoot).
    QList<NodeIdentifier> filteredNodes;
    for (const NodeIdentifier &n : draggedNodes) {
      NodeIdentifier nodeParent = m_controller->getParentFolder(n);
      // Field-wise comparison: notebookId AND relativePath
      if (nodeParent.notebookId != targetId.notebookId ||
          nodeParent.relativePath != targetId.relativePath) {
        filteredNodes.append(n);
      }
    }

    // If all nodes were filtered out (already at target), noop
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
    // Read-only gating: block external file import INTO a read-only target.
    if (m_controller && m_controller->isNotebookReadOnly(targetId.notebookId)) {
      p_event->ignore();
      return;
    }

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

void FileListView::selectionChanged(const QItemSelection &p_selected,
                                    const QItemSelection &p_deselected) {
  QListView::selectionChanged(p_selected, p_deselected);

  QList<NodeIdentifier> nodeIds = selectedNodeIds();
  emit nodeSelectionChanged(nodeIds);
}

void FileListView::onItemActivated(const QModelIndex &p_index) {
  if (!m_controller) {
    return;
  }

  NodeInfo nodeInfo = nodeInfoFromIndex(p_index);
  if (nodeInfo.isValid() && !nodeInfo.isFolder) {
    m_controller->openNode(nodeInfo.id);
  }
}

bool FileListView::isSingleClickActivationEnabled() const {
  if (m_controller) {
    return m_controller->isSingleClickActivationEnabled();
  }
  return false;
}
