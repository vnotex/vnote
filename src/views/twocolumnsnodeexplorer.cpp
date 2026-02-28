#include "twocolumnsnodeexplorer.h"

#include <QSplitter>
#include <QVBoxLayout>

#include <core/configmgr2.h>
#include <core/servicelocator.h>
#include <core/widgetconfig.h>
#include <controllers/notebooknodecontroller.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>
#include <views/notebooknodeview.h>
#include <views/notebooknodedelegate.h>

using namespace vnotex;

TwoColumnsNodeExplorer::TwoColumnsNodeExplorer(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services) {
  setupUI();
}

TwoColumnsNodeExplorer::~TwoColumnsNodeExplorer() = default;

void TwoColumnsNodeExplorer::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_splitter = new QSplitter(Qt::Horizontal, this);

  // === Folder panel (left) ===
  m_folderModel = new NotebookNodeModel(m_services, this);
  m_folderProxyModel = new NotebookNodeProxyModel(this);
  m_folderProxyModel->setSourceModel(m_folderModel);
  m_folderProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowFolders);

  // Apply view order from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  ViewOrder viewOrder = static_cast<ViewOrder>(widgetConfig.getNodeExplorerViewOrder());
  m_folderProxyModel->setViewOrder(viewOrder);
  m_folderProxyModel->sort(0);

  m_folderView = new NotebookNodeView(this);
  m_folderView->setModel(m_folderProxyModel);

  m_folderDelegate = new NotebookNodeDelegate(m_services, this);
  m_folderView->setItemDelegate(m_folderDelegate);

  m_folderController = new NotebookNodeController(m_services, this);
  m_folderController->setModel(m_folderModel);
  m_folderController->setView(m_folderView);
  m_folderView->setController(m_folderController);

  m_splitter->addWidget(m_folderView);

  // === File panel (right) ===
  m_fileModel = new NotebookNodeModel(m_services, this);
  m_fileProxyModel = new NotebookNodeProxyModel(this);
  m_fileProxyModel->setSourceModel(m_fileModel);
  m_fileProxyModel->setFilterFlags(NotebookNodeProxyModel::ShowNotes);
  m_fileProxyModel->setRecursiveFilteringEnabled(false); // Only show direct children
  m_fileProxyModel->setViewOrder(viewOrder);
  m_fileProxyModel->sort(0);

  m_fileView = new NotebookNodeView(this);
  m_fileView->setModel(m_fileProxyModel);

  m_fileDelegate = new NotebookNodeDelegate(m_services, this);
  m_fileView->setItemDelegate(m_fileDelegate);

  m_fileController = new NotebookNodeController(m_services, this);
  m_fileController->setModel(m_fileModel);
  m_fileController->setView(m_fileView);
  m_fileView->setController(m_fileController);

  m_splitter->addWidget(m_fileView);

  // Set initial splitter sizes
  m_splitter->setSizes(QList<int>() << 200 << 300);

  layout->addWidget(m_splitter);

  // === Connect signals ===

  // Folder selection updates file view
  connect(m_folderView, &NotebookNodeView::nodeSelectionChanged, this,
          &TwoColumnsNodeExplorer::onFolderSelectionChanged);

  // Forward activation signals from both views
  connect(m_folderView, &NotebookNodeView::nodeActivated, this,
          &TwoColumnsNodeExplorer::nodeActivated);
  connect(m_fileView, &NotebookNodeView::nodeActivated, this,
          &TwoColumnsNodeExplorer::nodeActivated);

  // Context menu signals (differentiate source)
  connect(m_folderView, &NotebookNodeView::contextMenuRequested, this,
          &TwoColumnsNodeExplorer::onFolderContextMenu);
  connect(m_fileView, &NotebookNodeView::contextMenuRequested, this,
          &TwoColumnsNodeExplorer::onFileContextMenu);
}

void TwoColumnsNodeExplorer::setNotebookId(const QString &p_notebookId) {
  if (m_notebookId == p_notebookId) {
    return;
  }

  m_notebookId = p_notebookId;

  m_folderModel->setNotebookId(p_notebookId);

  m_fileModel->setNotebookId(p_notebookId);
  // Show root folder's files initially
  if (!p_notebookId.isEmpty()) {
    NodeIdentifier rootId;
    rootId.notebookId = p_notebookId;
    rootId.relativePath = QString();
    m_fileModel->setDisplayRoot(rootId);
  }
}

QString TwoColumnsNodeExplorer::getNotebookId() const { return m_notebookId; }

NodeIdentifier TwoColumnsNodeExplorer::currentNodeId() const {
  // Prefer file selection over folder selection
  if (m_fileView) {
    NodeIdentifier fileNodeId = m_fileView->currentNodeId();
    if (fileNodeId.isValid()) {
      return fileNodeId;
    }
  }
  return m_folderView ? m_folderView->currentNodeId() : NodeIdentifier();
}

QList<NodeIdentifier> TwoColumnsNodeExplorer::selectedNodeIds() const {
  QList<NodeIdentifier> result;

  // Combine selections from both views, preferring file view
  if (m_fileView && m_fileView->hasFocus()) {
    result = m_fileView->selectedNodeIds();
  } else if (m_folderView) {
    result = m_folderView->selectedNodeIds();
  }

  return result;
}

void TwoColumnsNodeExplorer::selectNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Get node info to check if it's a folder
  NodeInfo info;
  if (m_folderModel) {
    QModelIndex idx = m_folderModel->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      info = m_folderModel->nodeInfoFromIndex(idx);
    }
  }

  if (info.isFolder) {
    m_folderView->selectNode(p_nodeId);
  } else {
    // Select parent folder in folder view, then select file
    NodeIdentifier parentId;
    parentId.notebookId = p_nodeId.notebookId;
    parentId.relativePath = p_nodeId.parentPath();

    if (parentId.isValid()) {
      m_folderView->expandToNode(parentId);
      m_folderView->selectNode(parentId);
    }
    m_fileView->selectNode(p_nodeId);
  }
}

void TwoColumnsNodeExplorer::expandToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Expand in folder view (folders only)
  NodeInfo info;
  if (m_folderModel) {
    QModelIndex idx = m_folderModel->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      info = m_folderModel->nodeInfoFromIndex(idx);
    }
  }

  if (info.isFolder) {
    m_folderView->expandToNode(p_nodeId);
  } else {
    // Expand to parent folder
    NodeIdentifier parentId;
    parentId.notebookId = p_nodeId.notebookId;
    parentId.relativePath = p_nodeId.parentPath();
    if (parentId.isValid()) {
      m_folderView->expandToNode(parentId);
    }
  }
}

void TwoColumnsNodeExplorer::scrollToNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo info;
  if (m_folderModel) {
    QModelIndex idx = m_folderModel->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      info = m_folderModel->nodeInfoFromIndex(idx);
    }
  }

  if (info.isFolder) {
    m_folderView->scrollToNode(p_nodeId);
  } else {
    m_fileView->scrollToNode(p_nodeId);
  }
}

void TwoColumnsNodeExplorer::expandAll() {
  if (m_folderView) {
    m_folderView->expandAll();
  }
}

void TwoColumnsNodeExplorer::collapseAll() {
  if (m_folderView) {
    m_folderView->collapseAll();
  }
}

NodeIdentifier TwoColumnsNodeExplorer::currentFolderId() const {
  // Get the currently displayed folder (from file model's display root)
  if (m_fileModel) {
    return m_fileModel->getDisplayRoot();
  }
  return NodeIdentifier();
}

void TwoColumnsNodeExplorer::setViewOrder(ViewOrder p_order) {
  if (m_folderProxyModel) {
    m_folderProxyModel->setViewOrder(p_order);
    m_folderProxyModel->sort(0);
  }
  if (m_fileProxyModel) {
    m_fileProxyModel->setViewOrder(p_order);
    m_fileProxyModel->sort(0);
  }
}

NotebookNodeController *TwoColumnsNodeExplorer::folderController() const {
  return m_folderController;
}

NotebookNodeController *TwoColumnsNodeExplorer::fileController() const {
  return m_fileController;
}

void TwoColumnsNodeExplorer::reloadNode(const NodeIdentifier &p_nodeId, bool p_isFolder) {
  if (p_isFolder) {
    if (m_folderModel) {
      m_folderModel->reloadNode(p_nodeId);
    }
  } else {
    if (m_fileModel) {
      m_fileModel->reloadNode(p_nodeId);
    }
  }
}

QByteArray TwoColumnsNodeExplorer::saveSplitterState() const {
  return m_splitter ? m_splitter->saveState() : QByteArray();
}

void TwoColumnsNodeExplorer::restoreSplitterState(const QByteArray &p_data) {
  if (m_splitter && !p_data.isEmpty()) {
    m_splitter->restoreState(p_data);
  }
}

void TwoColumnsNodeExplorer::onFolderSelectionChanged(const QList<NodeIdentifier> &p_nodeIds) {
  if (!m_fileModel || m_notebookId.isEmpty()) {
    return;
  }

  NodeIdentifier folderId;
  if (p_nodeIds.isEmpty()) {
    // No folder selected - show root folder content
    folderId.notebookId = m_notebookId;
    folderId.relativePath = QString();
  } else {
    folderId = p_nodeIds.first();
    if (!folderId.isValid()) {
      return;
    }
  }

  // Ensure file model has notebook set
  if (m_fileModel->getNotebookId() != m_notebookId) {
    m_fileModel->setNotebookId(m_notebookId);
  }

  // Set display root to show this folder's children
  m_fileModel->setDisplayRoot(folderId);
}

void TwoColumnsNodeExplorer::onFolderContextMenu(const NodeIdentifier &p_nodeId,
                                                  const QPoint &p_globalPos) {
  emit contextMenuRequested(p_nodeId, p_globalPos, false);
}

void TwoColumnsNodeExplorer::onFileContextMenu(const NodeIdentifier &p_nodeId,
                                                const QPoint &p_globalPos) {
  // If no specific node was clicked, use the current display root
  NodeIdentifier effectiveNodeId = p_nodeId;
  if (!effectiveNodeId.isValid() && m_fileModel) {
    effectiveNodeId = m_fileModel->getDisplayRoot();
  }

  emit contextMenuRequested(effectiveNodeId, p_globalPos, true);
}
