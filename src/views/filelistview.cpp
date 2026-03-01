#include "filelistview.h"


#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>

#include <QUrl>

#include <controllers/notebooknodecontroller.h>
#include <core/fileopenparameters.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>

using namespace vnotex;

FileListView::FileListView(QWidget *p_parent) : QListView(p_parent) {
  setupView();
}


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
  m_controller = p_controller;
}

NotebookNodeController *FileListView::controller() const { return m_controller; }

NodeIdentifier FileListView::currentNodeId() const {
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

  QModelIndex idx = indexFromNodeId(p_nodeId);
  if (idx.isValid()) {
    selectionModel()->select(idx,
                             QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    setCurrentIndex(idx);
  }
}

void FileListView::scrollToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
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

NodeInfo FileListView::nodeInfoFromIndex(const QModelIndex &p_index) const {
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

QModelIndex FileListView::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
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

void FileListView::mousePressEvent(QMouseEvent *p_event) {
  QListView::mousePressEvent(p_event);

  // Handle single-click activation if enabled
  if (p_event->button() == Qt::LeftButton && isSingleClickActivationEnabled()) {
    QModelIndex idx = indexAt(p_event->pos());
    if (idx.isValid()) {
      NodeInfo nodeInfo = nodeInfoFromIndex(idx);
      if (nodeInfo.isValid() && !nodeInfo.isFolder) {
        auto paras = QSharedPointer<FileOpenParameters>::create();
        emit nodeActivated(nodeInfo.id, paras);
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
    if (!nodeInfo.isFolder) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      emit nodeActivated(nodeInfo.id, paras);
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

  if (mimeData->hasFormat("application/x-vnotex-node-identifier")) {
    p_event->acceptProposedAction();
  } else if (mimeData->hasUrls()) {
    // External file drop
    p_event->acceptProposedAction();
  } else {
    p_event->ignore();
  }
}

void FileListView::dragMoveEvent(QDragMoveEvent *p_event) {
  // In file list view, we accept drops anywhere (files go into current folder)
  p_event->acceptProposedAction();
  setDropIndicatorShown(true);

  QListView::dragMoveEvent(p_event);
}

void FileListView::dropEvent(QDropEvent *p_event) {
  const QMimeData *mimeData = p_event->mimeData();

  if (mimeData->hasFormat("application/x-vnotex-node-identifier")) {
    // Internal node move - let model/controller handle it
    QListView::dropEvent(p_event);
  } else if (mimeData->hasUrls()) {
    // External file import
    QList<QUrl> urls = mimeData->urls();
    if (!urls.isEmpty() && m_controller) {
      // Controller will handle import
      p_event->acceptProposedAction();
    }
  }
}

void FileListView::selectionChanged(const QItemSelection &p_selected,
                                    const QItemSelection &p_deselected) {
  QListView::selectionChanged(p_selected, p_deselected);

  QList<NodeIdentifier> nodeIds = selectedNodeIds();
  emit nodeSelectionChanged(nodeIds);
}

void FileListView::onItemActivated(const QModelIndex &p_index) {
  NodeInfo nodeInfo = nodeInfoFromIndex(p_index);
  if (nodeInfo.isValid() && !nodeInfo.isFolder) {
    auto paras = QSharedPointer<FileOpenParameters>::create();
    emit nodeActivated(nodeInfo.id, paras);
  }
}



bool FileListView::isSingleClickActivationEnabled() const {
  if (m_controller) {
    return m_controller->isSingleClickActivationEnabled();
  }
  return false;
}
