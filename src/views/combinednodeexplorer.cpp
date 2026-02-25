#include "combinednodeexplorer.h"

#include <QMenu>
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

CombinedNodeExplorer::CombinedNodeExplorer(ServiceLocator &p_services, QWidget *p_parent)
    : INodeExplorer(p_parent), m_services(p_services) {
  setupUI();
}

CombinedNodeExplorer::~CombinedNodeExplorer() = default;

void CombinedNodeExplorer::setupUI() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  // Create MVC components
  m_model = new NotebookNodeModel(m_services, this);
  m_proxyModel = new NotebookNodeProxyModel(this);
  m_proxyModel->setSourceModel(m_model);
  m_proxyModel->setFilterFlags(NotebookNodeProxyModel::ShowAll);

  // Apply view order from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  ViewOrder viewOrder = static_cast<ViewOrder>(widgetConfig.getNodeExplorerViewOrder());
  m_proxyModel->setViewOrder(viewOrder);
  m_proxyModel->sort(0);

  m_view = new NotebookNodeView(this);
  m_view->setModel(m_proxyModel);

  m_delegate = new NotebookNodeDelegate(m_services, this);
  m_view->setItemDelegate(m_delegate);

  m_controller = new NotebookNodeController(m_services, this);
  m_controller->setModel(m_model);
  m_controller->setView(m_view);
  m_view->setController(m_controller);

  layout->addWidget(m_view);

  // === Connect signals ===

  // Forward activation signals
  connect(m_view, &NotebookNodeView::nodeActivated, this,
          &CombinedNodeExplorer::nodeActivated);

  // Context menu signal
  connect(m_view, &NotebookNodeView::contextMenuRequested, this,
          QOverload<const NodeIdentifier &, const QPoint &>::of(
              &CombinedNodeExplorer::contextMenuRequested));

  // Forward controller signals
  connect(m_controller, &NotebookNodeController::nodeActivated, this,
          &CombinedNodeExplorer::nodeActivated);
  connect(m_controller, &NotebookNodeController::fileActivated, this,
          &CombinedNodeExplorer::fileActivated);
  connect(m_controller, &NotebookNodeController::nodeAboutToMove, this,
          &CombinedNodeExplorer::nodeAboutToMove);
  connect(m_controller, &NotebookNodeController::nodeAboutToRemove, this,
          &CombinedNodeExplorer::nodeAboutToRemove);
  connect(m_controller, &NotebookNodeController::nodeAboutToReload, this,
          &CombinedNodeExplorer::nodeAboutToReload);
  connect(m_controller, &NotebookNodeController::closeFileRequested, this,
          &CombinedNodeExplorer::closeFileRequested);

  // GUI request signals
  connect(m_controller, &NotebookNodeController::newNoteRequested, this,
          &CombinedNodeExplorer::newNoteRequested);
  connect(m_controller, &NotebookNodeController::newFolderRequested, this,
          &CombinedNodeExplorer::newFolderRequested);
  connect(m_controller, &NotebookNodeController::renameRequested, this,
          &CombinedNodeExplorer::renameRequested);
  connect(m_controller, &NotebookNodeController::deleteRequested, this,
          &CombinedNodeExplorer::deleteRequested);
  connect(m_controller, &NotebookNodeController::removeFromNotebookRequested, this,
          &CombinedNodeExplorer::removeFromNotebookRequested);
  connect(m_controller, &NotebookNodeController::propertiesRequested, this,
          &CombinedNodeExplorer::propertiesRequested);

  // Status signals
  connect(m_controller, &NotebookNodeController::errorOccurred, this,
          &CombinedNodeExplorer::errorOccurred);
  connect(m_controller, &NotebookNodeController::infoMessage, this,
          &CombinedNodeExplorer::infoMessage);
}

void CombinedNodeExplorer::setNotebookId(const QString &p_notebookId) {
  if (m_notebookId == p_notebookId) {
    return;
  }

  m_notebookId = p_notebookId;
  m_model->setNotebookId(p_notebookId);
}

QString CombinedNodeExplorer::getNotebookId() const { return m_notebookId; }

NodeIdentifier CombinedNodeExplorer::currentNodeId() const {
  return m_view ? m_view->currentNodeId() : NodeIdentifier();
}

QList<NodeIdentifier> CombinedNodeExplorer::selectedNodeIds() const {
  return m_view ? m_view->selectedNodeIds() : QList<NodeIdentifier>();
}

void CombinedNodeExplorer::selectNode(const NodeIdentifier &p_nodeId) {
  if (m_view && p_nodeId.isValid()) {
    m_view->selectNode(p_nodeId);
  }
}

void CombinedNodeExplorer::expandToNode(const NodeIdentifier &p_nodeId) {
  if (m_view && p_nodeId.isValid()) {
    m_view->expandToNode(p_nodeId);
  }
}

void CombinedNodeExplorer::scrollToNode(const NodeIdentifier &p_nodeId) {
  if (m_view && p_nodeId.isValid()) {
    m_view->scrollToNode(p_nodeId);
  }
}

void CombinedNodeExplorer::expandAll() {
  if (m_view) {
    m_view->expandAll();
  }
}

void CombinedNodeExplorer::collapseAll() {
  if (m_view) {
    m_view->collapseAll();
  }
}

NodeIdentifier CombinedNodeExplorer::currentFolderId() const {
  NodeIdentifier nodeId = currentNodeId();
  if (!nodeId.isValid()) {
    // Return notebook root if no selection
    if (!m_notebookId.isEmpty()) {
      NodeIdentifier rootId;
      rootId.notebookId = m_notebookId;
      rootId.relativePath = QString();
      return rootId;
    }
    return NodeIdentifier();
  }

  // Check if it's a folder
  NodeInfo info = getNodeInfo(nodeId);
  if (info.isFolder) {
    return nodeId;
  }

  // Return parent folder for file nodes
  NodeIdentifier parentId;
  parentId.notebookId = nodeId.notebookId;
  parentId.relativePath = nodeId.parentPath();
  return parentId;
}

void CombinedNodeExplorer::setViewOrder(ViewOrder p_order) {
  if (m_proxyModel) {
    m_proxyModel->setViewOrder(p_order);
    m_proxyModel->sort(0);
  }
}

QMenu *CombinedNodeExplorer::createContextMenu(const NodeIdentifier &p_nodeId,
                                                QWidget *p_parent) {
  if (!m_controller) {
    return nullptr;
  }
  return m_controller->createContextMenu(p_nodeId, p_parent);
}

NodeInfo CombinedNodeExplorer::getNodeInfo(const NodeIdentifier &p_nodeId) const {
  if (m_model) {
    QModelIndex idx = m_model->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      return m_model->nodeInfoFromIndex(idx);
    }
  }
  return NodeInfo();
}

void CombinedNodeExplorer::handleRenameResult(const NodeIdentifier &p_nodeId,
                                               const QString &p_newName) {
  if (m_controller) {
    m_controller->handleRenameResult(p_nodeId, p_newName);
  }
}

void CombinedNodeExplorer::handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds,
                                                  bool p_permanent) {
  if (m_controller) {
    m_controller->handleDeleteConfirmed(p_nodeIds, p_permanent);
  }
}

void CombinedNodeExplorer::handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) {
  if (m_controller) {
    m_controller->handleRemoveConfirmed(p_nodeIds);
  }
}

void CombinedNodeExplorer::reloadNode(const NodeIdentifier &p_nodeId) {
  if (m_model) {
    m_model->reloadNode(p_nodeId);
  }
}
