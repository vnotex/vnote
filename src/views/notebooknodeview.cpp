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
#include <core/notebook/node.h>
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

Node *NotebookNodeView::currentNode() const {
  QModelIndex currentIdx = currentIndex();
  return nodeFromIndex(currentIdx);
}

QList<Node *> NotebookNodeView::selectedNodes() const {
  QList<Node *> nodes;
  QModelIndexList selected = selectedIndexes();

  for (const QModelIndex &idx : selected) {
    if (idx.column() == 0) { // Only process first column
      Node *node = nodeFromIndex(idx);
      if (node) {
        nodes.append(node);
      }
    }
  }

  return nodes;
}

void NotebookNodeView::selectNode(Node *p_node) {
  if (!p_node) {
    clearSelection();
    return;
  }

  QModelIndex idx = indexFromNode(p_node);
  if (idx.isValid()) {
    selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    setCurrentIndex(idx);
  }
}

void NotebookNodeView::expandToNode(Node *p_node) {
  if (!p_node) {
    return;
  }

  // Build path from root to node
  QList<Node *> path;
  Node *current = p_node->getParent();
  while (current && !current->isRoot()) {
    path.prepend(current);
    current = current->getParent();
  }

  // Expand each node in path
  for (Node *node : path) {
    QModelIndex idx = indexFromNode(node);
    if (idx.isValid()) {
      expand(idx);
    }
  }
}

void NotebookNodeView::scrollToNode(Node *p_node) {
  if (!p_node) {
    return;
  }

  QModelIndex idx = indexFromNode(p_node);
  if (idx.isValid()) {
    scrollTo(idx, QAbstractItemView::EnsureVisible);
  }
}

void NotebookNodeView::expandAll() { QTreeView::expandAll(); }

void NotebookNodeView::collapseAll() { QTreeView::collapseAll(); }

Node *NotebookNodeView::nodeFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return nullptr;
  }

  // Handle proxy model
  QModelIndex sourceIdx = p_index;
  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  if (proxyModel) {
    sourceIdx = proxyModel->mapToSource(p_index);
  }

  auto *nodeModel = qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());
  if (nodeModel) {
    return nodeModel->nodeFromIndex(sourceIdx);
  }

  return nullptr;
}

QModelIndex NotebookNodeView::indexFromNode(Node *p_node) const {
  if (!p_node) {
    return QModelIndex();
  }

  auto *proxyModel = qobject_cast<NotebookNodeProxyModel *>(model());
  auto *nodeModel =
      qobject_cast<NotebookNodeModel *>(proxyModel ? proxyModel->sourceModel() : model());

  if (!nodeModel) {
    return QModelIndex();
  }

  QModelIndex sourceIdx = nodeModel->indexFromNode(p_node);
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

  Node *node = nodeFromIndex(idx);
  if (node) {
    // For containers, toggle expand/collapse (default behavior)
    // For content nodes, activate
    if (!node->isContainer()) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      emit nodeActivated(node, paras);
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
    Node *node = currentNode();
    if (node && !node->isContainer()) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      emit nodeActivated(node, paras);
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
    Node *node = currentNode();
    if (node && m_controller) {
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
  Node *node = nodeFromIndex(idx);

  emit contextMenuRequested(node, p_event->globalPos());
  p_event->accept();
}

void NotebookNodeView::dragEnterEvent(QDragEnterEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();

  if (mimeData->hasFormat("application/x-vnotex-node")) {
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
  Node *targetNode = nodeFromIndex(idx);

  // Only accept drops on containers
  if (targetNode && targetNode->isContainer()) {
    p_event->acceptProposedAction();
    setDropIndicatorShown(true);
  } else if (!targetNode) {
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
  Node *targetNode = nodeFromIndex(idx);

  if (mimeData->hasFormat("application/x-vnotex-node")) {
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

  QList<Node *> nodes = selectedNodes();
  emit nodeSelectionChanged(nodes);
}

void NotebookNodeView::onItemActivated(const QModelIndex &p_index) {
  Node *node = nodeFromIndex(p_index);
  if (node && !node->isContainer()) {
    auto paras = QSharedPointer<FileOpenParameters>::create();
    emit nodeActivated(node, paras);
  }
}
