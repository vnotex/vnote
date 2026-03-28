#include "filelistview.h"


#include <QContextMenuEvent>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>

#include <QUrl>

#include <QAbstractProxyModel>

#include <controllers/notebooknodecontroller.h>
#include <core/fileopenparameters.h>
#include <models/inodelistmodel.h>

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

void FileListView::mousePressEvent(QMouseEvent *p_event) {
  QListView::mousePressEvent(p_event);

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

void FileListView::selectionChanged(const QItemSelection &p_selected,
                                    const QItemSelection &p_deselected) {
  QListView::selectionChanged(p_selected, p_deselected);

  QList<NodeIdentifier> nodeIds = selectedNodeIds();
  emit nodeSelectionChanged(nodeIds);
}

void FileListView::onItemActivated(const QModelIndex &p_index) {
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
