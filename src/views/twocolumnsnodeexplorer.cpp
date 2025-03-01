#include "twocolumnsnodeexplorer.h"

#include <QMenu>
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
#include <views/filenodedelegate.h>
#include <views/filelistview.h>

using namespace vnotex;

TwoColumnsNodeExplorer::TwoColumnsNodeExplorer(ServiceLocator &p_services, QWidget *p_parent)
    : INodeExplorer(p_parent), m_services(p_services) {
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

  m_fileView = new FileListView(this);
  m_fileView->setModel(m_fileProxyModel);

  m_fileDelegate = new FileNodeDelegate(m_services, this);
  m_fileView->setItemDelegate(m_fileDelegate);

  m_fileController = new NotebookNodeController(m_services, this);
  m_fileController->setModel(m_fileModel);
  // Note: FileListView is not a NotebookNodeView, so we don't call setView()
  m_fileView->setController(m_fileController);

  // Share clipboard between folder and file controllers
  m_folderController->shareClipboardWith(m_fileController);

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
  connect(m_fileView, &FileListView::nodeActivated, this,
          &TwoColumnsNodeExplorer::nodeActivated);

  // Context menu signals (differentiate source)
  connect(m_folderView, &NotebookNodeView::contextMenuRequested, this,
          &TwoColumnsNodeExplorer::onFolderContextMenu);
  connect(m_fileView, &FileListView::contextMenuRequested, this,
          &TwoColumnsNodeExplorer::onFileContextMenu);

  // Forward controller signals from both controllers
  connectControllerSignals(m_folderController);
  connectControllerSignals(m_fileController);
}

void TwoColumnsNodeExplorer::connectControllerSignals(NotebookNodeController *p_controller) {
  if (!p_controller) {
    return;
  }

  // Node lifecycle signals
  connect(p_controller, &NotebookNodeController::fileActivated, this,
          &TwoColumnsNodeExplorer::fileActivated);
  connect(p_controller, &NotebookNodeController::nodeAboutToMove, this,
          &TwoColumnsNodeExplorer::nodeAboutToMove);
  connect(p_controller, &NotebookNodeController::nodeAboutToRemove, this,
          &TwoColumnsNodeExplorer::nodeAboutToRemove);
  connect(p_controller, &NotebookNodeController::nodeAboutToReload, this,
          &TwoColumnsNodeExplorer::nodeAboutToReload);
  connect(p_controller, &NotebookNodeController::closeFileRequested, this,
          &TwoColumnsNodeExplorer::closeFileRequested);

  // GUI request signals
  connect(p_controller, &NotebookNodeController::newNoteRequested, this,
          &TwoColumnsNodeExplorer::newNoteRequested);
  connect(p_controller, &NotebookNodeController::newFolderRequested, this,
          &TwoColumnsNodeExplorer::newFolderRequested);
  connect(p_controller, &NotebookNodeController::renameRequested, this,
          &TwoColumnsNodeExplorer::renameRequested);
  connect(p_controller, &NotebookNodeController::deleteRequested, this,
          &TwoColumnsNodeExplorer::deleteRequested);
  connect(p_controller, &NotebookNodeController::removeFromNotebookRequested, this,
          &TwoColumnsNodeExplorer::removeFromNotebookRequested);
  connect(p_controller, &NotebookNodeController::propertiesRequested, this,
          &TwoColumnsNodeExplorer::propertiesRequested);

  // Status signals
  connect(p_controller, &NotebookNodeController::errorOccurred, this,
          &TwoColumnsNodeExplorer::errorOccurred);
  connect(p_controller, &NotebookNodeController::infoMessage, this,
          &TwoColumnsNodeExplorer::infoMessage);

  // Cross-panel refresh for paste operations
  connect(p_controller, &NotebookNodeController::nodesPasted, this,
          [this](const NodeIdentifier &p_targetFolderId) {
            // Refresh file model if the target folder is currently displayed
            if (m_fileModel && m_fileModel->getDisplayRoot() == p_targetFolderId) {
              m_fileModel->reloadNode(p_targetFolderId);
            }
            // Also reload folder model to show any new subfolders
            if (m_folderModel) {
              m_folderModel->reloadNode(p_targetFolderId);
            }
          });
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
  NodeInfo info = getNodeInfo(p_nodeId);

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
  NodeInfo info = getNodeInfo(p_nodeId);

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

  NodeInfo info = getNodeInfo(p_nodeId);

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

QMenu *TwoColumnsNodeExplorer::createContextMenu(const NodeIdentifier &p_nodeId,
                                                  QWidget *p_parent) {
  // Delegate to the 3-param version; use folder controller by default
  // (determines from node info whether it's a folder or file)
  NodeInfo info = getNodeInfo(p_nodeId);
  return createContextMenu(p_nodeId, !info.isFolder, p_parent);
}

QMenu *TwoColumnsNodeExplorer::createContextMenu(const NodeIdentifier &p_nodeId,
                                                  bool p_isFromFileView,
                                                  QWidget *p_parent) {
  NotebookNodeController *controller = p_isFromFileView ? m_fileController : m_folderController;
  if (!controller) {
    return nullptr;
  }

  // If no specific node was clicked in file view, use the current display root
  NodeIdentifier effectiveNodeId = p_nodeId;
  if (!effectiveNodeId.isValid() && p_isFromFileView && m_fileModel) {
    effectiveNodeId = m_fileModel->getDisplayRoot();
  }

  return controller->createContextMenu(effectiveNodeId, p_parent);
}

NodeInfo TwoColumnsNodeExplorer::getNodeInfo(const NodeIdentifier &p_nodeId) const {
  NodeInfo info;

  // Try folder model first
  if (m_folderModel) {
    QModelIndex idx = m_folderModel->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      info = m_folderModel->nodeInfoFromIndex(idx);
      if (info.isValid()) {
        return info;
      }
    }
  }

  // Try file model
  if (m_fileModel) {
    QModelIndex idx = m_fileModel->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      info = m_fileModel->nodeInfoFromIndex(idx);
    }
  }

  return info;
}

NotebookNodeController *TwoColumnsNodeExplorer::controllerForNode(
    const NodeIdentifier &p_nodeId) const {
  NodeInfo info = getNodeInfo(p_nodeId);
  return info.isFolder ? m_folderController : m_fileController;
}

void TwoColumnsNodeExplorer::handleRenameResult(const NodeIdentifier &p_nodeId,
                                                 const QString &p_newName) {
  NotebookNodeController *controller = controllerForNode(p_nodeId);
  if (controller) {
    controller->handleRenameResult(p_nodeId, p_newName);
  }
}

void TwoColumnsNodeExplorer::handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds,
                                                    bool p_permanent) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // Determine controller from first node
  NotebookNodeController *controller = controllerForNode(p_nodeIds.first());
  if (controller) {
    controller->handleDeleteConfirmed(p_nodeIds, p_permanent);
  }
}

void TwoColumnsNodeExplorer::handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // Determine controller from first node
  NotebookNodeController *controller = controllerForNode(p_nodeIds.first());
  if (controller) {
    controller->handleRemoveConfirmed(p_nodeIds);
  }
}

void TwoColumnsNodeExplorer::reloadNode(const NodeIdentifier &p_nodeId) {
  // Auto-detect folder vs file based on node info
  NodeInfo info = getNodeInfo(p_nodeId);
  reloadNode(p_nodeId, info.isFolder);
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
