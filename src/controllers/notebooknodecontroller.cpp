#include "notebooknodecontroller.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QUrl>

#include <core/events.h>
#include <core/fileopensettings.h>
#include <core/servicelocator.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <models/notebooknodemodel.h>
#include <utils/pathutils.h>
#include <views/notebooknodeview.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services),
      m_clipboard(new ClipboardState()) {
}

NotebookNodeController::~NotebookNodeController() {}

void NotebookNodeController::setModel(INodeListModel *p_model) { m_model = p_model; }

INodeListModel *NotebookNodeController::model() const { return m_model; }

void NotebookNodeController::setView(NotebookNodeView *p_view) { m_view = p_view; }

NotebookNodeView *NotebookNodeController::view() const { return m_view; }

QString NotebookNodeController::currentNotebookId() const {
  if (m_model) {
    return m_model->getNotebookId();
  }
  return QString();
}

QString NotebookNodeController::buildAbsolutePath(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid()) {
    return QString();
  }

  // Get notebook root path from service
  auto *notebookService = m_services.get<NotebookCoreService>();
  QJsonObject config = notebookService->getNotebookConfig(p_nodeId.notebookId);
  QString rootPath = config.value(QStringLiteral("rootFolder")).toString();

  if (rootPath.isEmpty()) {
    return QString();
  }

  if (p_nodeId.relativePath.isEmpty()) {
    return rootPath;
  }

  return PathUtils::concatenateFilePath(rootPath, p_nodeId.relativePath);
}

NodeInfo NotebookNodeController::getNodeInfo(const NodeIdentifier &p_nodeId) const {
  if (m_model && p_nodeId.isValid()) {
    QModelIndex idx = m_model->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      return m_model->nodeInfoFromIndex(idx);
    }
  }
  return NodeInfo();
}

NodeIdentifier NotebookNodeController::getParentFolder(const NodeIdentifier &p_nodeId) const {
  NodeIdentifier parentId;
  parentId.notebookId = p_nodeId.notebookId;
  parentId.relativePath = p_nodeId.parentPath();
  return parentId;
}

bool NotebookNodeController::isSingleClickActivationEnabled() const {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    return configMgr->getWidgetConfig().isNodeExplorerSingleClickActivation();
  }
  return false;
}

void NotebookNodeController::setSelectedNodesCallback(SelectedNodesCallback p_callback) {
  m_selectedNodesCallback = p_callback;
}

QMenu *NotebookNodeController::createContextMenu(const NodeIdentifier &p_nodeId,
                                                  QWidget *p_parent) {
  // Get node info to determine if it's a folder or external
  NodeInfo nodeInfo = getNodeInfo(p_nodeId);

  // External nodes have a different context menu
  if (nodeInfo.isValid() && nodeInfo.isExternal) {
    return createExternalNodeContextMenu(p_nodeId, p_parent);
  }

  QMenu *menu = new QMenu(p_parent);

  bool isFolder = nodeInfo.isValid() ? nodeInfo.isFolder : true; // Default to folder for root

  addNewActions(menu, p_nodeId, isFolder);
  menu->addSeparator();
  addOpenActions(menu, p_nodeId, isFolder);
  menu->addSeparator();
  addEditActions(menu, p_nodeId, isFolder);
  menu->addSeparator();
  addCopyMoveActions(menu, p_nodeId, isFolder);
  menu->addSeparator();
  addImportExportActions(menu, p_nodeId, isFolder);
  menu->addSeparator();
  addInfoActions(menu, p_nodeId);
  menu->addSeparator();
  addMiscActions(menu, p_nodeId, isFolder);

  return menu;
}

QMenu *NotebookNodeController::createExternalNodeContextMenu(const NodeIdentifier &p_nodeId,
                                                              QWidget *p_parent) {
  QMenu *menu = new QMenu(p_parent);

  // Get node info to determine if it's a folder
  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  bool isFolder = nodeInfo.isValid() ? nodeInfo.isFolder : true;

  // Open actions - only for files (external folders cannot be expanded)
  if (!isFolder) {
    auto *openAction = menu->addAction(tr("&Open"));
    connect(openAction, &QAction::triggered, this, [this, p_nodeId]() { openNode(p_nodeId); });

    auto *openWithAction = menu->addAction(tr("Open With Default App"));
    connect(openWithAction, &QAction::triggered, this,
            [this, p_nodeId]() { openNodeWithDefaultApp(p_nodeId); });

    menu->addSeparator();
  }

  // Import to Index action
  auto *importAction = menu->addAction(tr("&Import to Index"));
  importAction->setToolTip(tr("Add this external item to the notebook index"));
  connect(importAction, &QAction::triggered, this,
          [this, p_nodeId]() { importExternalNode(p_nodeId); });

  menu->addSeparator();

  // Open Location action
  auto *locateAction = menu->addAction(tr("Open &Location"));
  connect(locateAction, &QAction::triggered, this,
          [this, p_nodeId]() { locateNodeInFileManager(p_nodeId); });

  return menu;
}

void NotebookNodeController::addNewActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                           bool p_isFolder) {
  // Determine target folder for new items
  NodeIdentifier targetFolder = p_isFolder ? p_nodeId : getParentFolder(p_nodeId);

  auto *newNoteAction = p_menu->addAction(tr("New &Note"));
  connect(newNoteAction, &QAction::triggered, this,
          [this, targetFolder]() { newNote(targetFolder); });

  auto *newFolderAction = p_menu->addAction(tr("New &Folder"));
  connect(newFolderAction, &QAction::triggered, this,
          [this, targetFolder]() { newFolder(targetFolder); });
}

void NotebookNodeController::addOpenActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  if (!p_nodeId.isValid()) {
    return;
  }

  if (!p_isFolder) {
    auto *openAction = p_menu->addAction(tr("&Open"));
    connect(openAction, &QAction::triggered, this, [this, p_nodeId]() { openNode(p_nodeId); });

    auto *openWithAction = p_menu->addAction(tr("Open With Default App"));
    connect(openWithAction, &QAction::triggered, this,
            [this, p_nodeId]() { openNodeWithDefaultApp(p_nodeId); });
  }
}

void NotebookNodeController::addEditActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  Q_UNUSED(p_isFolder);
  if (!p_nodeId.isValid()) {
    return;
  }

  auto *renameAction = p_menu->addAction(tr("&Rename"));
  connect(renameAction, &QAction::triggered, this, [this, p_nodeId]() { renameNode(p_nodeId); });

  auto *deleteAction = p_menu->addAction(tr("&Delete"));
  connect(deleteAction, &QAction::triggered, this, [this, p_nodeId]() {
    deleteNodes(QList<NodeIdentifier>() << p_nodeId);
  });

  auto *removeAction = p_menu->addAction(tr("Remove From Notebook"));
  removeAction->setToolTip(tr("Remove from notebook but keep files on disk"));
  connect(removeAction, &QAction::triggered, this, [this, p_nodeId]() {
    removeNodesFromNotebook(QList<NodeIdentifier>() << p_nodeId);
  });
}

void NotebookNodeController::addCopyMoveActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                                bool p_isFolder) {
  if (p_nodeId.isValid()) {
    auto *copyAction = p_menu->addAction(tr("&Copy"));
    connect(copyAction, &QAction::triggered, this, [this, p_nodeId]() {
      // Use all selected nodes: try view first, then callback, finally fall back to context menu node
      QList<NodeIdentifier> nodeIds;
      if (m_view) {
        nodeIds = m_view->selectedNodeIds();
      } else if (m_selectedNodesCallback) {
        nodeIds = m_selectedNodesCallback();
      }
      if (nodeIds.isEmpty()) {
        nodeIds << p_nodeId;
      }
      copyNodes(nodeIds);
    });

    auto *cutAction = p_menu->addAction(tr("Cu&t"));
    connect(cutAction, &QAction::triggered, this, [this, p_nodeId]() {
      // Use all selected nodes: try view first, then callback, finally fall back to context menu node
      QList<NodeIdentifier> nodeIds;
      if (m_view) {
        nodeIds = m_view->selectedNodeIds();
      } else if (m_selectedNodesCallback) {
        nodeIds = m_selectedNodesCallback();
      }
      if (nodeIds.isEmpty()) {
        nodeIds << p_nodeId;
      }
      cutNodes(nodeIds);
    });
  }

  // Paste is available on any folder target, including root (invalid nodeId).
  if (p_isFolder && canPaste()) {
    NodeIdentifier targetFolder = p_nodeId;
    if (!targetFolder.isValid()) {
      targetFolder.notebookId = currentNotebookId();
      targetFolder.relativePath = QString(); // root
    }
    if (!targetFolder.notebookId.isEmpty()) {
      auto *pasteAction = p_menu->addAction(tr("&Paste"));
      connect(pasteAction, &QAction::triggered, this,
              [this, targetFolder]() { pasteNodes(targetFolder); });
    }
  }

  if (p_nodeId.isValid()) {
    auto *duplicateAction = p_menu->addAction(tr("D&uplicate"));
    connect(duplicateAction, &QAction::triggered, this,
            [this, p_nodeId]() { duplicateNode(p_nodeId); });
  }
}

void NotebookNodeController::addImportExportActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                                    bool p_isFolder) {
  Q_UNUSED(p_isFolder);
  // Import actions removed from context menu - available via toolbar only

  if (p_nodeId.isValid()) {
    auto *exportAction = p_menu->addAction(tr("&Export"));
    connect(exportAction, &QAction::triggered, this,
            [this, p_nodeId]() { exportNode(p_nodeId); });
  }
}

void NotebookNodeController::addInfoActions(QMenu *p_menu, const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  auto *copyPathAction = p_menu->addAction(tr("Copy &Path"));
  connect(copyPathAction, &QAction::triggered, this,
          [this, p_nodeId]() { copyNodePath(p_nodeId); });

  auto *locateAction = p_menu->addAction(tr("Open &Location"));
  connect(locateAction, &QAction::triggered, this,
          [this, p_nodeId]() { locateNodeInFileManager(p_nodeId); });

  auto *propertiesAction = p_menu->addAction(tr("P&roperties"));
  connect(propertiesAction, &QAction::triggered, this,
          [this, p_nodeId]() { showNodeProperties(p_nodeId); });

  auto *tagAction = p_menu->addAction(tr("Manage &Tags"));
  connect(tagAction, &QAction::triggered, this,
          [this, p_nodeId]() { manageNodeTags(p_nodeId); });
}

void NotebookNodeController::addMiscActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  auto *reloadAction = p_menu->addAction(tr("Re&load"));
  connect(reloadAction, &QAction::triggered, this,
          [this, p_nodeId]() { reloadNode(p_nodeId); });

  if (p_nodeId.isValid()) {
    auto *pinAction = p_menu->addAction(tr("Pin to &Quick Access"));
    connect(pinAction, &QAction::triggered, this,
            [this, p_nodeId]() { pinNodeToQuickAccess(p_nodeId); });
  }
}

void NotebookNodeController::newNote(const NodeIdentifier &p_parentId) {
  QString notebookId = p_parentId.isValid() ? p_parentId.notebookId : currentNotebookId();
  if (notebookId.isEmpty()) {
    return;
  }

  NodeIdentifier parentId = p_parentId;
  if (!parentId.isValid()) {
    parentId.notebookId = notebookId;
    parentId.relativePath = QString(); // root
  }

  emit newNoteRequested(parentId);
}

void NotebookNodeController::newFolder(const NodeIdentifier &p_parentId) {
  QString notebookId = p_parentId.isValid() ? p_parentId.notebookId : currentNotebookId();
  if (notebookId.isEmpty()) {
    return;
  }

  NodeIdentifier parentId = p_parentId;
  if (!parentId.isValid()) {
    parentId.notebookId = notebookId;
    parentId.relativePath = QString();
  }

  emit newFolderRequested(parentId);
}

void NotebookNodeController::openNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);

  // Handle external nodes
  if (nodeInfo.isValid() && nodeInfo.isExternal && !nodeInfo.isFolder) {
    auto *configMgr = m_services.get<ConfigMgr2>();
    bool autoImport =
        configMgr && configMgr->getWidgetConfig().getNodeExplorerAutoImportExternalFilesEnabled();

    if (autoImport) {
      // Import the external node first, then open
      auto *notebookService = m_services.get<NotebookCoreService>();
      if (notebookService &&
          notebookService->indexNode(p_nodeId.notebookId, p_nodeId.relativePath)) {
        // Reload parent to update view
        if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
          NodeIdentifier parentId = getParentFolder(p_nodeId);
          nbModel->reloadNode(parentId);
        }
      }
      // Import failed, fall through to open as external file
    }
  }

  FileOpenSettings settings;
  settings.m_mode = m_services.get<ConfigMgr2>()->getCoreConfig().getDefaultOpenMode();
  emit nodeActivated(p_nodeId, settings);
}

void NotebookNodeController::openNodeWithDefaultApp(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (nodeInfo.isFolder) {
    return;
  }

  QString path = buildAbsolutePath(p_nodeId);
  if (!path.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  }
}

void NotebookNodeController::deleteNodes(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // Emit signal to request delete confirmation from view
  emit deleteRequested(p_nodeIds, false);
}

void NotebookNodeController::removeNodesFromNotebook(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  emit removeFromNotebookRequested(p_nodeIds);
}

void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &p_nodeIds) {
  m_clipboard->nodes = p_nodeIds;
  m_clipboard->isCut = false;
}

void NotebookNodeController::cutNodes(const QList<NodeIdentifier> &p_nodeIds) {
  m_clipboard->nodes = p_nodeIds;
  m_clipboard->isCut = true;
}

void NotebookNodeController::pasteNodes(const NodeIdentifier &p_targetFolderId) {
  if (m_clipboard->nodes.isEmpty() || !p_targetFolderId.isValid()) {
    return;
  }
  auto *notebookService = m_services.get<NotebookCoreService>();

  NodeInfo targetInfo = getNodeInfo(p_targetFolderId);
  // Check if target is a folder:
  // 1. Root folder (empty relativePath) is always a folder
  // 2. Valid nodeInfo with isFolder=true
  // 3. For invalid nodeInfo (cross-panel paste), query service to verify it's a folder
  bool isTargetFolder = p_targetFolderId.relativePath.isEmpty() || targetInfo.isFolder;
  if (!isTargetFolder && !targetInfo.isValid()) {
    // Target not in model - check if it's a folder via service
    QJsonObject folderConfig = notebookService->getFolderConfig(
        p_targetFolderId.notebookId, p_targetFolderId.relativePath);
    isTargetFolder = !folderConfig.isEmpty();
  }
  if (!isTargetFolder) {
    return;
  }

  // Collect source parent folders for cut operations (need to refresh after move)
  QSet<QString> sourceParentPaths;

  // Track first successfully pasted node for selection
  NodeIdentifier firstPastedNode;

  for (const NodeIdentifier &nodeId : m_clipboard->nodes) {
    if (m_clipboard->isCut) {
      notifyBeforeNodeOperation(nodeId, QStringLiteral("move"));
      // Remember source parent for later refresh
      sourceParentPaths.insert(nodeId.parentPath());
    }
    NodeInfo nodeInfo = getNodeInfo(nodeId);
    // If source node not in our model cache (cross-panel paste), query service
    bool isFolder = nodeInfo.isFolder;
    QString nodeName = nodeInfo.name;
    if (!nodeInfo.isValid()) {
      // Cross-panel paste: node not in our model cache, query service
      // Try folder config first, then file info
      QJsonObject folderConfig = notebookService->getFolderConfig(nodeId.notebookId, nodeId.relativePath);
      if (!folderConfig.isEmpty()) {
        isFolder = true;
        nodeName = folderConfig.value(QStringLiteral("name")).toString();
      } else {
        QJsonObject fileInfo = notebookService->getFileInfo(nodeId.notebookId, nodeId.relativePath);
        if (!fileInfo.isEmpty()) {
          isFolder = false;
          nodeName = fileInfo.value(QStringLiteral("name")).toString();
        } else {
          emit errorOccurred(tr("Error"), tr("Node not found: %1").arg(nodeId.relativePath));
          continue;
        }
      }
    }
    bool success = false;
    if (m_clipboard->isCut) {
      // Move operation
      if (isFolder) {
        success = notebookService->moveFolder(nodeId.notebookId, nodeId.relativePath,
                                              p_targetFolderId.relativePath);
      } else {
        success = notebookService->moveFile(nodeId.notebookId, nodeId.relativePath,
                                            p_targetFolderId.relativePath);
      }
      if (!success) {
        emit errorOccurred(tr("Error"), tr("Failed to move %1.").arg(nodeName));
      } else if (!firstPastedNode.isValid()) {
        // Remember first successfully pasted node
        firstPastedNode.notebookId = p_targetFolderId.notebookId;
        if (p_targetFolderId.relativePath.isEmpty()) {
          firstPastedNode.relativePath = nodeName;
        } else {
          firstPastedNode.relativePath = p_targetFolderId.relativePath + QStringLiteral("/") + nodeName;
        }
      }
    } else {
      // Copy operation - get available name first to avoid conflicts
      QString targetName = notebookService->getAvailableName(
          nodeId.notebookId, p_targetFolderId.relativePath, nodeName);
      if (targetName.isEmpty()) {
        emit errorOccurred(tr("Error"), tr("Failed to get available name for %1.").arg(nodeName));
        continue;
      }
      QString result;
      if (isFolder) {
        result = notebookService->copyFolder(nodeId.notebookId, nodeId.relativePath,
                                             p_targetFolderId.relativePath, targetName);
        if (result.isEmpty()) {
          emit errorOccurred(tr("Error"), tr("Failed to copy folder."));
        }
      } else {
        result = notebookService->copyFile(nodeId.notebookId, nodeId.relativePath,
                                           p_targetFolderId.relativePath, targetName);
        if (result.isEmpty()) {
          emit errorOccurred(tr("Error"), tr("Failed to copy file."));
        }
      }
      success = !result.isEmpty();

      // Remember first successfully pasted node
      if (success && !firstPastedNode.isValid()) {
        firstPastedNode.notebookId = p_targetFolderId.notebookId;
        if (p_targetFolderId.relativePath.isEmpty()) {
          firstPastedNode.relativePath = targetName;
        } else {
          firstPastedNode.relativePath = p_targetFolderId.relativePath + QStringLiteral("/") + targetName;
        }
      }
    }
  }


  // For cut operations, refresh source parent folders
  if (m_clipboard->isCut) {
    auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model);
    if (nbModel) {
      for (const QString &parentPath : sourceParentPaths) {
        NodeIdentifier parentId;
        parentId.notebookId = p_targetFolderId.notebookId;
        parentId.relativePath = parentPath;
        nbModel->reloadNode(parentId);
      }
    }
    m_clipboard->nodes.clear();
    m_clipboard->isCut = false;
  }

  // Refresh target folder
  if (auto *nbModel2 = dynamic_cast<NotebookNodeModel *>(m_model)) {
    nbModel2->reloadNode(p_targetFolderId);
  }

  // Notify that paste completed (for cross-panel refresh)
  emit nodesPasted(p_targetFolderId);

  // Select and expand to first pasted node
  if (firstPastedNode.isValid() && m_view) {
    m_view->expandToNode(firstPastedNode);
    m_view->selectNode(firstPastedNode);
  }
}

void NotebookNodeController::duplicateNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeIdentifier parentId = getParentFolder(p_nodeId);
  if (!parentId.notebookId.isEmpty()) {
    // Set clipboard and paste to same folder
    copyNodes(QList<NodeIdentifier>() << p_nodeId);
    pasteNodes(parentId);
    m_clipboard->nodes.clear();
  }
}

void NotebookNodeController::renameNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (!nodeInfo.isValid()) {
    return;
  }

  emit renameRequested(p_nodeId, nodeInfo.name);
}

void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &p_nodeIds,
                                       const NodeIdentifier &p_targetFolderId) {
  cutNodes(p_nodeIds);
  pasteNodes(p_targetFolderId);
}


void NotebookNodeController::exportNode(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  emit infoMessage(tr("Export"), tr("Export functionality not yet implemented."));
}

void NotebookNodeController::importExternalNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Verify this is an external node
  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (!nodeInfo.isValid() || !nodeInfo.isExternal) {
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    emit errorOccurred(tr("Error"), tr("NotebookService not available."));
    return;
  }

  bool success = notebookService->indexNode(p_nodeId.notebookId, p_nodeId.relativePath);
  if (!success) {
    emit errorOccurred(tr("Error"), tr("Failed to import external node to index."));
    return;
  }

  // Reload parent folder to update the view
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    NodeIdentifier parentId = getParentFolder(p_nodeId);
    nbModel->reloadNode(parentId);
  }
}

void NotebookNodeController::showNodeProperties(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  emit propertiesRequested(p_nodeId);
}

void NotebookNodeController::copyNodePath(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  QString path = buildAbsolutePath(p_nodeId);
  if (!path.isEmpty()) {
    QApplication::clipboard()->setText(path);
  }
}

void NotebookNodeController::locateNodeInFileManager(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  QString path = buildAbsolutePath(p_nodeId);
  if (path.isEmpty()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  QString dirPath = nodeInfo.isFolder ? path : PathUtils::parentDirPath(path);
  QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void NotebookNodeController::sortNodes(const NodeIdentifier &p_parentId) {
  Q_UNUSED(p_parentId);
  emit infoMessage(tr("Sort"), tr("Sort functionality not yet implemented."));
}

void NotebookNodeController::reloadNode(const NodeIdentifier &p_nodeId) {
  if (p_nodeId.isValid()) {
    auto event = QSharedPointer<Event>::create();
    emit nodeAboutToReload(p_nodeId, event);

    if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
      nbModel->reloadNode(p_nodeId);
    }
  } else {
    reloadAll();
  }
}

void NotebookNodeController::reloadAll() {
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    nbModel->reload();
  }
}

void NotebookNodeController::pinNodeToQuickAccess(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  emit infoMessage(tr("Quick Access"), tr("Quick Access functionality not yet implemented."));
}

void NotebookNodeController::manageNodeTags(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  emit infoMessage(tr("Tags"), tr("Tag management not yet implemented."));
}

bool NotebookNodeController::canPaste() const { return !m_clipboard->nodes.isEmpty(); }

void NotebookNodeController::shareClipboardWith(NotebookNodeController *p_other) {
  if (p_other && p_other != this) {
    // Share our clipboard with the other controller
    p_other->m_clipboard = m_clipboard;
  }
}


void NotebookNodeController::notifyBeforeNodeOperation(const NodeIdentifier &p_nodeId,
                                                       const QString &p_operation) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Hooks (NodeBeforeDelete, NodeBeforeMove, FileBeforeClose) are now fired
  // by the service layer (NotebookCoreService) — not by the controller.
  // The controller only emits Qt signals for UI coordination.

  auto event = QSharedPointer<Event>::create();

  if (p_operation == QStringLiteral("delete") || p_operation == QStringLiteral("remove")) {
    emit nodeAboutToRemove(p_nodeId, event);
  } else if (p_operation == QStringLiteral("move") || p_operation == QStringLiteral("rename")) {
    emit nodeAboutToMove(p_nodeId, event);
  }

  // Also request to close the file (Qt signal for UI coordination).
  QString path = buildAbsolutePath(p_nodeId);
  if (!path.isEmpty()) {
    emit closeFileRequested(path, event);
  }
}

// --- Handler implementations for dialog results from View ---

void NotebookNodeController::handleNewNoteResult(const NodeIdentifier &p_parentId,
                                                  const NodeIdentifier &p_newNodeId) {
  Q_UNUSED(p_parentId);
  // Model reload is handled by the view layer after dialog closes
  // This slot is available for additional post-creation logic if needed
  if (p_newNodeId.isValid() && m_model) {
    // Optionally select the new node or perform other actions
  }
}

void NotebookNodeController::handleNewFolderResult(const NodeIdentifier &p_parentId,
                                                    const NodeIdentifier &p_newNodeId) {
  Q_UNUSED(p_parentId);
  // Model reload is handled by the view layer after dialog closes
  // This slot is available for additional post-creation logic if needed
  if (p_newNodeId.isValid() && m_model) {
    // Optionally select the new node or perform other actions
  }
}

void NotebookNodeController::handleRenameResult(const NodeIdentifier &p_nodeId,
                                                 const QString &p_newName) {
  if (!p_nodeId.isValid() || p_newName.isEmpty()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (!nodeInfo.isValid() || nodeInfo.name == p_newName) {
    return;
  }

  // NodeBeforeRename hook is now fired by NotebookCoreService::renameFile/renameFolder.
  // The service returns false if cancelled by a plugin.

  // Check if the buffer is open and dirty. Prompt user before proceeding.
  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    QJsonArray buffers = bufferSvc->listBuffers();
    for (int i = 0; i < buffers.size(); ++i) {
      QJsonObject bufObj = buffers[i].toObject();
      QString nbId = bufObj.value(QStringLiteral("notebookId")).toString();
      QString filePath = bufObj.value(QStringLiteral("filePath")).toString();
      if (nbId == p_nodeId.notebookId && filePath == p_nodeId.relativePath) {
        QString bufferId = bufObj.value(QStringLiteral("id")).toString();
        Buffer2 buf = bufferSvc->getBufferHandle(bufferId);
        if (buf.isValid() && buf.isModified()) {
          int ret = MessageBoxHelper::questionSaveDiscardCancel(
              MessageBoxHelper::Question,
              tr("The file \"%1\" has unsaved changes.").arg(nodeInfo.name),
              tr("Save before renaming, discard changes, or cancel?"),
              QString(),
              nullptr);
          if (ret == QMessageBox::Cancel) {
            return; // Abort rename.
          }
          if (ret == QMessageBox::Save) {
            buf.save();
          }
          // Discard: proceed without saving — on-disk content gets renamed as-is.
        }
        break;
      }
    }
  }

  notifyBeforeNodeOperation(p_nodeId, QStringLiteral("rename"));

  auto *notebookService = m_services.get<NotebookCoreService>();
  bool success;
  if (nodeInfo.isFolder) {
    success = notebookService->renameFolder(p_nodeId.notebookId, p_nodeId.relativePath, p_newName);
  } else {
    success = notebookService->renameFile(p_nodeId.notebookId, p_nodeId.relativePath, p_newName);
  }
  if (!success) {
    emit errorOccurred(tr("Error"), tr("Failed to rename %1.").arg(nodeInfo.name));
    return;
  }

  // NodeAfterRename hook is fired by NotebookCoreService::renameFile/renameFolder.

  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    NodeIdentifier parentId = getParentFolder(p_nodeId);
    nbModel->reloadNode(parentId);
  }
}

void NotebookNodeController::handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds,
                                                    bool p_permanent) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  QSet<NodeIdentifier> parentsToReload;

  for (const NodeIdentifier &nodeId : p_nodeIds) {
    notifyBeforeNodeOperation(nodeId, QStringLiteral("delete"));

    NodeInfo nodeInfo = getNodeInfo(nodeId);
    NodeIdentifier parentId = getParentFolder(nodeId);
    parentsToReload.insert(parentId);

    bool success;
    if (nodeInfo.isFolder) {
      // Note: vxcore deleteFolder handles recycle bin for bundled notebooks
      // p_permanent flag is currently ignored - delete always uses vxcore default behavior
      Q_UNUSED(p_permanent);
      success = notebookService->deleteFolder(nodeId.notebookId, nodeId.relativePath);
    } else {
      // Note: vxcore deleteFile handles recycle bin for bundled notebooks
      success = notebookService->deleteFile(nodeId.notebookId, nodeId.relativePath);
    }
    if (!success) {
      emit errorOccurred(tr("Error"), tr("Failed to delete %1.").arg(nodeInfo.name));
    }
  }
  // Reload all affected parent folders
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    for (const NodeIdentifier &parentId : parentsToReload) {
      nbModel->reloadNode(parentId);
    }
  }
}

void NotebookNodeController::handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  QSet<NodeIdentifier> parentsToReload;

  for (const NodeIdentifier &nodeId : p_nodeIds) {
    notifyBeforeNodeOperation(nodeId, QStringLiteral("remove"));

    NodeIdentifier parentId = getParentFolder(nodeId);
    parentsToReload.insert(parentId);
    // Use unindexNode to remove from metadata without deleting files on disk
    if (!notebookService->unindexNode(nodeId.notebookId, nodeId.relativePath)) {
      emit errorOccurred(tr("Error"), tr("Failed to remove from notebook."));
    }
  }
  // Reload all affected parent folders
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    for (const NodeIdentifier &parentId : parentsToReload) {
      nbModel->reloadNode(parentId);
    }
  }
}

void NotebookNodeController::handleImportFiles(const NodeIdentifier &p_targetFolderId,
                                                const QStringList &p_files) {
  if (!p_targetFolderId.isValid() || p_files.isEmpty()) {
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    emit errorOccurred(tr("Import"), tr("NotebookService not available."));
    return;
  }

  int successCount = 0;
  int failCount = 0;

  for (const QString &filePath : p_files) {
    QString fileId = notebookService->importFile(
        p_targetFolderId.notebookId, p_targetFolderId.relativePath, filePath);
    if (fileId.isEmpty()) {
      ++failCount;
    } else {
      ++successCount;
    }
  }

  // Reload the target folder to show imported files
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    if (successCount > 0) {
      nbModel->reloadNode(p_targetFolderId);
    }
  }

  // Report results
  if (failCount > 0) {
    emit infoMessage(tr("Import"),
                    tr("Imported %1 file(s), %2 failed.").arg(successCount).arg(failCount));
  } else if (successCount > 0) {
    emit infoMessage(tr("Import"), tr("Successfully imported %1 file(s).").arg(successCount));
  }
}

void NotebookNodeController::handleImportFolder(const NodeIdentifier &p_targetFolderId,
                                                 const QString &p_folderPath) {
  if (!p_targetFolderId.isValid() || p_folderPath.isEmpty()) {
    return;
  }

  // TODO: Implement folder import via NotebookService when available
  emit infoMessage(tr("Import"), tr("Folder import not yet implemented in new architecture."));
}
