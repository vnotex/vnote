#include "notebooknodeview.h"

#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QQueue>
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
  m_controller = p_controller;
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
  if (idx.isValid()) {
    selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    setCurrentIndex(idx);
  }
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
    return nodeModel->nodeInfoFromIndex(sourceIdx);
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
  QModelIndex idx = indexAt(p_event->position().toPoint());
  NodeInfo targetInfo = nodeInfoFromIndex(idx);

  // Only accept drops on containers
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

  QTreeView::dragMoveEvent(p_event);
}

void NotebookNodeView::dropEvent(QDropEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();
  QModelIndex idx = indexAt(p_event->position().toPoint());
  NodeIdentifier targetId = nodeIdFromIndex(idx);

  // If dropping on empty area or invalid index, target is notebook root
  if (!targetId.isValid()) {
    auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
    auto *nodeModel =
        qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
    if (nodeModel) {
      targetId.notebookId = nodeModel->getNotebookId();
      targetId.relativePath = QString(); // root
    }
  }

  // If target is a file (not folder), use its parent folder as target
  NodeInfo targetInfo = nodeInfoFromIndex(idx);
  if (targetId.isValid() && !targetId.relativePath.isEmpty() && !targetInfo.isFolder) {
    if (m_controller) {
      targetId = m_controller->getParentFolder(targetId);
    }
    if (!targetId.isValid()) {
      p_event->ignore();
      return;
    }
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
    if (p_event->modifiers() & Qt::ControlModifier) {
      action = Qt::CopyAction;
    }

    // Perform operation via controller
    if (action == Qt::CopyAction) {
      m_controller->copyNodes(draggedNodes);
      m_controller->pasteNodes(targetId);
    } else {
      m_controller->moveNodes(draggedNodes, targetId);
    }

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
