#include "notebooknodeview.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QUrl>

#include <core/fileopenparameters.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>

using namespace vnotex;

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

  // Connect activated signal
  connect(this, &QTreeView::activated, this, &NotebookNodeView::onItemActivated);
}

void NotebookNodeView::setController(NotebookNodeController *p_controller) {
  m_controller = p_controller;
}

NotebookNodeController *NotebookNodeView::controller() const { return m_controller; }

NodeIdentifier NotebookNodeView::currentNodeId() const {
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
      auto paras = QSharedPointer<FileOpenParameters>::create();
      emit nodeActivated(nodeInfo.id, paras);
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
    NodeInfo nodeInfo = nodeInfoFromIndex(currentIndex());
    if (nodeInfo.isValid() && !nodeInfo.isFolder) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      emit nodeActivated(nodeInfo.id, paras);
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
    // Rename - handled by controller
    NodeIdentifier nodeId = currentNodeId();
    if (nodeId.isValid() && m_controller) {
      // Controller will handle rename
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

  if (mimeData->hasFormat("application/x-vnotex-node-identifier")) {
    // Internal node move - let model/controller handle it
    // The actual move is handled by the controller
    QTreeView::dropEvent(p_event);
  } else if (mimeData->hasUrls()) {
    // External file import
    QList<QUrl> urls = mimeData->urls();
    if (!urls.isEmpty() && m_controller) {
      // Controller will handle import
      // For now, just accept the drop
      p_event->acceptProposedAction();
    }
  }
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
    auto paras = QSharedPointer<FileOpenParameters>::create();
    emit nodeActivated(nodeInfo.id, paras);
  }
}
