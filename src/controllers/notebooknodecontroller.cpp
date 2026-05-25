#include "notebooknodecontroller.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QUrl>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/events.h>
#include <core/fileopensettings.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/workspacecoreservice.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <models/notebooknodemodel.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>
#include <views/notebooknodeview.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_clipboard(new ClipboardState()) {}

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
    // Direct cache lookup (bypasses fragile indexFromNodeId round-trip).
    NodeInfo directResult = m_model->nodeInfoFromNodeId(p_nodeId);
    if (directResult.isValid()) {
      return directResult;
    }

    // Fallback to index-based lookup.
    QModelIndex idx = m_model->indexFromNodeId(p_nodeId);
    if (idx.isValid()) {
      NodeInfo result = m_model->nodeInfoFromIndex(idx);
      return result;
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

QList<NodeIdentifier>
NotebookNodeController::resolveSelection(const NodeIdentifier &p_clickedId) const {
  QList<NodeIdentifier> sel;
  if (m_selectedNodesCallback) {
    sel = m_selectedNodesCallback();
  } else if (m_view) {
    sel = m_view->selectedNodeIds();
  }
  // Qt convention: right-click outside current selection operates on clicked row only.
  if (sel.isEmpty() || !sel.contains(p_clickedId)) {
    return {p_clickedId};
  }
  return sel;
}

QList<NodeIdentifier>
NotebookNodeController::dedupeDescendants(const QList<NodeIdentifier> &p_ids) const {
  QList<NodeIdentifier> result;
  for (const auto &id : p_ids) {
    // Check if this id is a descendant of any other id in the list
    bool isDescendant = false;
    for (const auto &other : p_ids) {
      // Only compare nodes in the same notebook
      if (id.notebookId != other.notebookId) {
        continue;
      }
      // Skip if comparing with self
      if (id == other) {
        continue;
      }
      // Check if other is a prefix of id (path-segment-aware)
      QString otherPath = QDir::cleanPath(other.relativePath);
      QString idPath = QDir::cleanPath(id.relativePath);
      if (!otherPath.isEmpty() && idPath.startsWith(otherPath + QLatin1Char('/'))) {
        isDescendant = true;
        break;
      }
    }
    if (!isDescendant) {
      result.append(id);
    }
  }
  return result;
}

bool NotebookNodeController::isSingleEffectiveSelection(const NodeIdentifier &p_clickedId) const {
  return resolveSelection(p_clickedId).size() == 1;
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
    openAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

    addOpenWithSubmenu(menu, p_nodeId);

    menu->addSeparator();
  }

  // Import to Index action
  auto *importAction = menu->addAction(tr("&Import to Index"));
  importAction->setToolTip(tr("Add this external item to the notebook index"));
  connect(importAction, &QAction::triggered, this,
          [this, p_nodeId]() { importExternalNode(p_nodeId); });
  importAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  menu->addSeparator();

  // Ignore action - adds this node's name to the ignored list
  auto *ignoreAction = menu->addAction(tr("&Ignore"));
  connect(ignoreAction, &QAction::triggered, this,
          [this, p_nodeId]() { emit ignoreRequested(p_nodeId); });
  ignoreAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  menu->addSeparator();

  // Open Location action
  auto *locateAction = menu->addAction(tr("Open &Location"));
  connect(locateAction, &QAction::triggered, this,
          [this, p_nodeId]() { locateNodeInFileManager(p_nodeId); });
  locateAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  return menu;
}

void NotebookNodeController::addNewActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                           bool p_isFolder) {
  // Determine target folder for new items
  NodeIdentifier targetFolder = p_isFolder ? p_nodeId : getParentFolder(p_nodeId);

  auto *newNoteAction = p_menu->addAction(tr("New &Note"));
  connect(newNoteAction, &QAction::triggered, this,
          [this, targetFolder]() { newNote(targetFolder); });
  newNoteAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  auto *newFolderAction = p_menu->addAction(tr("New &Folder"));
  connect(newFolderAction, &QAction::triggered, this,
          [this, targetFolder]() { newFolder(targetFolder); });
  newFolderAction->setEnabled(isSingleEffectiveSelection(p_nodeId));
}

void NotebookNodeController::addOpenActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  if (!p_nodeId.isValid()) {
    return;
  }

  if (!p_isFolder) {
    auto *openAction = p_menu->addAction(tr("&Open"));
    connect(openAction, &QAction::triggered, this,
            [this, p_nodeId]() { openNodes(resolveSelection(p_nodeId)); });

    addOpenWithSubmenu(p_menu, p_nodeId);
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
  renameAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  {
    auto *notebookService = m_services.get<NotebookCoreService>();
    QJsonObject nbConfig = notebookService->getNotebookConfig(p_nodeId.notebookId);
    QString nbType = nbConfig.value(QStringLiteral("type")).toString();
    bool isBundled = (nbType == QStringLiteral("bundled"));
    if (isBundled) {
      auto *deleteAction = p_menu->addAction(tr("&Delete"));
      connect(deleteAction, &QAction::triggered, this,
              [this, p_nodeId]() { deleteNodes(dedupeDescendants(resolveSelection(p_nodeId))); });

      auto *removeAction = p_menu->addAction(tr("Remove from Notebook"));
      removeAction->setToolTip(tr("Remove from notebook but keep files on disk"));
      connect(removeAction, &QAction::triggered, this, [this, p_nodeId]() {
        removeNodesFromNotebook(dedupeDescendants(resolveSelection(p_nodeId)));
      });
    }
  }
}

void NotebookNodeController::addCopyMoveActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                                bool p_isFolder) {
  if (p_nodeId.isValid()) {
    auto *copyAction = p_menu->addAction(tr("&Copy"));
    connect(copyAction, &QAction::triggered, this,
            [this, p_nodeId]() { copyNodes(dedupeDescendants(resolveSelection(p_nodeId))); });

    auto *cutAction = p_menu->addAction(tr("Cu&t"));
    connect(cutAction, &QAction::triggered, this,
            [this, p_nodeId]() { cutNodes(dedupeDescendants(resolveSelection(p_nodeId))); });
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
      pasteAction->setEnabled(isSingleEffectiveSelection(p_nodeId));
    }
  }

  if (p_nodeId.isValid()) {
    auto *duplicateAction = p_menu->addAction(tr("D&uplicate"));
    connect(duplicateAction, &QAction::triggered, this,
            [this, p_nodeId]() { duplicateNodes(resolveSelection(p_nodeId)); });
  }
}

void NotebookNodeController::addImportExportActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                                    bool p_isFolder) {
  Q_UNUSED(p_isFolder);
  // Import actions removed from context menu - available via toolbar only

  if (p_nodeId.isValid()) {
    auto *exportAction = p_menu->addAction(tr("&Export"));
    connect(exportAction, &QAction::triggered, this, [this, p_nodeId]() { exportNode(p_nodeId); });
    exportAction->setEnabled(isSingleEffectiveSelection(p_nodeId));
  }
}

void NotebookNodeController::addInfoActions(QMenu *p_menu, const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  auto *copyPathAction = p_menu->addAction(tr("Copy &Path"));
  connect(copyPathAction, &QAction::triggered, this,
          [this, p_nodeId]() { copyNodePaths(resolveSelection(p_nodeId)); });

  auto *locateAction = p_menu->addAction(tr("Open &Location"));
  connect(locateAction, &QAction::triggered, this,
          [this, p_nodeId]() { locateNodeInFileManager(p_nodeId); });
  locateAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  auto *propertiesAction = p_menu->addAction(tr("P&roperties"));
  connect(propertiesAction, &QAction::triggered, this,
          [this, p_nodeId]() { showNodeProperties(p_nodeId); });
  propertiesAction->setEnabled(isSingleEffectiveSelection(p_nodeId));

  {
    auto *notebookService = m_services.get<NotebookCoreService>();
    QJsonObject nbConfig = notebookService->getNotebookConfig(p_nodeId.notebookId);
    QString nbType = nbConfig.value(QStringLiteral("type")).toString();
    bool isBundled = (nbType == QStringLiteral("bundled"));
    if (isBundled) {
      auto *tagAction = p_menu->addAction(tr("&Tags"));
      connect(tagAction, &QAction::triggered, this,
              [this, p_nodeId]() { manageNodeTags(p_nodeId); });
      tagAction->setEnabled(isSingleEffectiveSelection(p_nodeId));
    }
  }
}

void NotebookNodeController::addMiscActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  Q_UNUSED(p_isFolder);

  auto *reloadAction = p_menu->addAction(tr("Re&load"));
  connect(reloadAction, &QAction::triggered, this,
          [this, p_nodeId]() { reloadNodes(resolveSelection(p_nodeId)); });

  if (p_nodeId.isValid()) {
    auto *pinAction = p_menu->addAction(tr("Pin to &Quick Access"));
    connect(pinAction, &QAction::triggered, this,
            [this, p_nodeId]() { pinNodesToQuickAccess(resolveSelection(p_nodeId)); });

    auto *notebookService = m_services.get<NotebookCoreService>();
    QJsonObject nbConfig = notebookService->getNotebookConfig(p_nodeId.notebookId);
    QString nbType = nbConfig.value(QStringLiteral("type")).toString();
    bool isBundled = (nbType == QStringLiteral("bundled"));
    if (!p_nodeId.isRoot() && isBundled) {
      auto *markAction = p_menu->addAction(tr("&Mark"));
      connect(markAction, &QAction::triggered, this,
              [this, p_nodeId]() { markNodes(resolveSelection(p_nodeId)); });
    }
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
  // Delegate to list version (single source of truth for open logic).
  openNodes({p_nodeId});
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

void NotebookNodeController::addOpenWithSubmenu(QMenu *p_parentMenu,
                                                const NodeIdentifier &p_nodeId) {
  auto *subMenu = new QMenu(tr("Open With"), p_parentMenu);

  auto *configMgr = m_services.get<ConfigMgr2>();
  if (configMgr) {
    const auto &programs = configMgr->getSessionConfig().getExternalPrograms();
    for (const auto &prog : programs) {
      if (prog.m_name.isEmpty()) {
        continue;
      }
      if (prog.isSystemProgram()) {
        continue;
      }
      QString name = prog.m_name;
      QString command = prog.m_command;
      auto *action = subMenu->addAction(name);
      connect(action, &QAction::triggered, this, [this, p_nodeId, command]() {
        openNodesWithCommand(resolveSelection(p_nodeId), command);
      });
    }
    if (!programs.isEmpty()) {
      subMenu->addSeparator();
    }
  }

  auto *defaultAction = subMenu->addAction(tr("System Default App"));
  connect(defaultAction, &QAction::triggered, this, [this, p_nodeId]() {
    const auto ids = resolveSelection(p_nodeId);
    for (const auto &id : ids) {
      NodeInfo info = getNodeInfo(id);
      if (info.isFolder) {
        continue;
      }
      openNodeWithDefaultApp(id);
      closeOrphanedBuffer(id);
    }
  });

  p_parentMenu->addMenu(subMenu);
}

void NotebookNodeController::closeOrphanedBuffer(const NodeIdentifier &p_nodeId) {
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  // Find buffer matching this node.
  const auto buffers = bufferSvc->listBuffers();
  QString bufferId;
  for (const auto &bufVal : buffers) {
    auto obj = bufVal.toObject();
    if (obj[QStringLiteral("notebookId")].toString() == p_nodeId.notebookId &&
        obj[QStringLiteral("relativePath")].toString() == p_nodeId.relativePath) {
      bufferId = obj[QStringLiteral("id")].toString();
      break;
    }
  }
  if (bufferId.isEmpty()) {
    return;
  }

  // Don't close modified buffers (unsaved changes).
  if (bufferSvc->isModified(bufferId)) {
    return;
  }

  // Check if buffer is in any workspace (displayed in a ViewWindow).
  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    const auto workspaces = wsSvc->listWorkspaces();
    for (const auto &wsVal : workspaces) {
      auto wsObj = wsVal.toObject();
      auto bufferIds = wsObj[QStringLiteral("bufferIds")].toArray();
      for (const auto &id : bufferIds) {
        if (id.toString() == bufferId) {
          return; // Buffer is in a workspace — don't close.
        }
      }
    }
  }

  // Buffer exists but is orphaned — close it.
  bufferSvc->closeBuffer(bufferId);
}

void NotebookNodeController::deleteNodes(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // Query notebook type: raw notebooks have no recycle bin, so delete permanently.
  auto *notebookService = m_services.get<NotebookCoreService>();
  QJsonObject nbConfig = notebookService->getNotebookConfig(p_nodeIds.first().notebookId);
  QString nbType = nbConfig.value(QStringLiteral("type")).toString();
  bool isBundled = (nbType == QStringLiteral("bundled"));

  // Emit signal to request delete confirmation from view
  emit deleteRequested(p_nodeIds, !isBundled);
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
    QJsonObject folderConfig = notebookService->getFolderConfig(p_targetFolderId.notebookId,
                                                                p_targetFolderId.relativePath);
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
      QJsonObject folderConfig =
          notebookService->getFolderConfig(nodeId.notebookId, nodeId.relativePath);
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
          firstPastedNode.relativePath =
              p_targetFolderId.relativePath + QStringLiteral("/") + nodeName;
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
          firstPastedNode.relativePath =
              p_targetFolderId.relativePath + QStringLiteral("/") + targetName;
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
  // Delegate to list overload for single source of truth.
  duplicateNodes(QList<NodeIdentifier>{p_nodeId});
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

void NotebookNodeController::markNode(const NodeIdentifier &p_nodeId) {
  // Delegate to list version (single source of truth).
  markNodes({p_nodeId});
}

void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &p_nodeIds,
                                       const NodeIdentifier &p_targetFolderId) {
  cutNodes(p_nodeIds);
  pasteNodes(p_targetFolderId);
}

void NotebookNodeController::exportNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  emit exportNodeRequested(p_nodeId);
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
  copyNodePaths({p_nodeId});
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
  // Delegate to list version (single source of truth).
  reloadNodes({p_nodeId});
}

void NotebookNodeController::reloadAll() {
  if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
    nbModel->reload();
  }
}

void NotebookNodeController::pinNodeToQuickAccess(const NodeIdentifier &p_nodeId) {
  // Delegate to list version (single source of truth).
  pinNodesToQuickAccess({p_nodeId});
}

void NotebookNodeController::manageNodeTags(const NodeIdentifier &p_nodeId) {
  emit manageTagsRequested(p_nodeId);
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

  // Hooks (NodeBeforeDelete, NodeBeforeMove) are now fired
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
              tr("Save before renaming, discard changes, or cancel?"), QString(), nullptr);
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

void NotebookNodeController::handleMarkResult(const NodeIdentifier &p_nodeId,
                                              const QString &p_textColor,
                                              const QString &p_bgColor) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (!nodeInfo.isValid()) {
    return;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  QJsonObject metadata;
  bool success = false;

  if (nodeInfo.isFolder) {
    metadata = notebookService->getFolderMetadata(p_nodeId.notebookId, p_nodeId.relativePath);
  } else {
    metadata = notebookService->getFileMetadata(p_nodeId.notebookId, p_nodeId.relativePath);
  }

  if (p_textColor.isEmpty()) {
    metadata.remove(QStringLiteral("textColor"));
  } else {
    metadata[QStringLiteral("textColor")] = p_textColor;
  }

  if (p_bgColor.isEmpty()) {
    metadata.remove(QStringLiteral("backgroundColor"));
  } else {
    metadata[QStringLiteral("backgroundColor")] = p_bgColor;
  }

  QJsonDocument doc(metadata);
  QString metadataJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

  if (nodeInfo.isFolder) {
    success = notebookService->updateFolderMetadata(p_nodeId.notebookId, p_nodeId.relativePath,
                                                    metadataJson);
  } else {
    success = notebookService->updateFileMetadata(p_nodeId.notebookId, p_nodeId.relativePath,
                                                  metadataJson);
  }

  if (!success) {
    emit errorOccurred(tr("Error"), tr("Failed to mark %1.").arg(nodeInfo.name));
    return;
  }

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
    QString fileId = notebookService->importFile(p_targetFolderId.notebookId,
                                                 p_targetFolderId.relativePath, filePath);
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

// Multi-node Open: opens each non-folder node in order. Folders are silently skipped
// (the first non-folder note opened becomes the focused tab via downstream BufferMgr behavior).
void NotebookNodeController::openNodes(const QList<NodeIdentifier> &p_ids) {
  int skipped = 0;
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto *notebookService = m_services.get<NotebookCoreService>();

  for (const auto &id : p_ids) {
    if (!id.isValid()) {
      continue;
    }

    NodeInfo nodeInfo = getNodeInfo(id);
    if (nodeInfo.isValid() && nodeInfo.isFolder) {
      ++skipped;
      continue;
    }

    // Handle external nodes: auto-import if enabled, then fall through to open.
    if (nodeInfo.isValid() && nodeInfo.isExternal && !nodeInfo.isFolder) {
      bool autoImport =
          configMgr && configMgr->getWidgetConfig().getNodeExplorerAutoImportExternalFilesEnabled();
      if (autoImport && notebookService &&
          notebookService->indexNode(id.notebookId, id.relativePath)) {
        if (auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model)) {
          NodeIdentifier parentId = getParentFolder(id);
          nbModel->reloadNode(parentId);
        }
      }
    }

    FileOpenSettings settings;
    if (configMgr) {
      settings.m_mode = configMgr->getCoreConfig().getDefaultOpenMode();
    }
    emit nodeActivated(id, settings);
  }

  if (skipped > 0) {
    qDebug() << "openNodes: silently skipped" << skipped << "folder(s) in multi-selection";
  }
}

// Multi-node Open With: launches the external program for each non-folder node.
// Folders are silently skipped.
void NotebookNodeController::openNodesWithCommand(const QList<NodeIdentifier> &p_ids,
                                                  const QString &p_commandTemplate) {
  int skipped = 0;
  for (const auto &id : p_ids) {
    if (!id.isValid()) {
      continue;
    }

    NodeInfo nodeInfo = getNodeInfo(id);
    if (nodeInfo.isValid() && nodeInfo.isFolder) {
      ++skipped;
      continue;
    }

    QString path = buildAbsolutePath(id);
    if (path.isEmpty()) {
      continue;
    }
    SessionConfig::ExternalProgram tempProg;
    tempProg.m_command = p_commandTemplate;
    QString cmd = tempProg.fetchCommand(path);
    if (cmd.isEmpty()) {
      continue;
    }
    ProcessUtils::startDetached(cmd);
    closeOrphanedBuffer(id);
  }

  if (skipped > 0) {
    qDebug() << "openNodesWithCommand: silently skipped" << skipped
             << "folder(s) in multi-selection";
  }
}

void NotebookNodeController::duplicateNodes(const QList<NodeIdentifier> &p_ids) {
  const auto ids = dedupeDescendants(p_ids);
  int failures = 0;
  for (const auto &id : ids) {
    if (!id.isValid()) {
      ++failures;
      continue;
    }
    NodeIdentifier parentId = getParentFolder(id);
    if (parentId.notebookId.isEmpty()) {
      ++failures;
      continue;
    }
    // Duplicate via copy+paste-to-same-folder (auto-suffix handled by pasteNodes).
    copyNodes(QList<NodeIdentifier>{id});
    pasteNodes(parentId);
    m_clipboard->nodes.clear();
  }
  if (failures > 0) {
    qWarning() << "duplicateNodes: failed to duplicate" << failures << "of" << ids.size()
               << "node(s)";
  }
}

void NotebookNodeController::copyNodePaths(const QList<NodeIdentifier> &p_ids) {
  QStringList paths;
  for (const auto &id : p_ids) {
    QString path = buildAbsolutePath(id);
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }

  if (!paths.isEmpty()) {
    QString joinedPaths = paths.join("\n");
    QApplication::clipboard()->setText(joinedPaths);
  }
}

void NotebookNodeController::pinNodesToQuickAccess(const QList<NodeIdentifier> &p_ids) {
  if (p_ids.isEmpty()) {
    return;
  }

  auto *notebookSvc = m_services.get<NotebookCoreService>();
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!notebookSvc || !configMgr) {
    return;
  }

  auto &sessionConfig = configMgr->getSessionConfig();
  auto items = sessionConfig.getQuickAccessItems();

  int pinned = 0;
  int duplicates = 0;
  int failures = 0;

  for (const auto &id : p_ids) {
    QString absolutePath = notebookSvc->buildAbsolutePath(id.notebookId, id.relativePath);
    if (absolutePath.isEmpty()) {
      qWarning() << "pinNodesToQuickAccess: failed to build absolute path for" << id.relativePath;
      ++failures;
      continue;
    }

    // Skip duplicates (compare against current working list).
    bool isDup = false;
    for (const auto &existing : items) {
      if (existing.m_path == absolutePath) {
        isDup = true;
        break;
      }
    }
    if (isDup) {
      ++duplicates;
      continue;
    }

    // Look up UUID from file info.
    QString uuid;
    QJsonObject fileInfo = notebookSvc->getFileInfo(id.notebookId, id.relativePath);
    if (!fileInfo.isEmpty() && fileInfo.contains(QStringLiteral("id"))) {
      uuid = fileInfo[QStringLiteral("id")].toString();
    }

    SessionConfig::QuickAccessItem item;
    item.m_path = absolutePath;
    item.m_openMode = QuickAccessOpenMode::Default;
    item.m_uuid = uuid;
    items.append(item);
    ++pinned;
  }

  if (pinned > 0) {
    sessionConfig.setQuickAccessItems(items);
  }

  if (failures > 0) {
    qWarning() << "pinNodesToQuickAccess: pinned" << pinned << "duplicates" << duplicates
               << "failures" << failures << "of" << p_ids.size();
  }

  if (pinned > 0 && duplicates == 0) {
    emit infoMessage(tr("Quick Access"), tr("Pinned %1 item(s) to Quick Access.").arg(pinned));
  } else if (pinned == 0 && duplicates > 0) {
    emit infoMessage(tr("Quick Access"), tr("Already in Quick Access."));
  } else if (pinned > 0 && duplicates > 0) {
    emit infoMessage(tr("Quick Access"),
                     tr("Pinned %1 item(s); %2 already present.").arg(pinned).arg(duplicates));
  }
}

void NotebookNodeController::reloadNodes(const QList<NodeIdentifier> &p_ids) {
  if (p_ids.isEmpty()) {
    return;
  }

  auto *nbModel = dynamic_cast<NotebookNodeModel *>(m_model);
  for (const auto &id : p_ids) {
    if (id.isValid()) {
      auto event = QSharedPointer<Event>::create();
      emit nodeAboutToReload(id, event);
      if (nbModel) {
        nbModel->reloadNode(id);
      }
    } else {
      reloadAll();
    }
  }
}

void NotebookNodeController::markNodes(const QList<NodeIdentifier> &p_ids) {
  if (p_ids.isEmpty()) {
    return;
  }
  emit markRequested(p_ids);
}
