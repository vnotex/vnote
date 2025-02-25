#include "notebooknodecontroller.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>

#include <core/events.h>
#include <core/fileopenparameters.h>
#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
#include <models/notebooknodemodel.h>
#include <utils/pathutils.h>
#include <views/notebooknodeview.h>

using namespace vnotex;

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

NotebookNodeController::~NotebookNodeController() {}

void NotebookNodeController::setModel(NotebookNodeModel *p_model) { m_model = p_model; }

NotebookNodeModel *NotebookNodeController::model() const { return m_model; }

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
  auto *notebookService = m_services.get<NotebookService>();
  QJsonObject config = notebookService->getNotebookConfig(p_nodeId.notebookId);
  QString rootPath = config.value(QStringLiteral("root_folder")).toString();

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

QMenu *NotebookNodeController::createContextMenu(const NodeIdentifier &p_nodeId,
                                                  QWidget *p_parent) {
  QMenu *menu = new QMenu(p_parent);

  // Get node info to determine if it's a folder
  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
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

  auto *openAction = p_menu->addAction(tr("&Open"));
  connect(openAction, &QAction::triggered, this, [this, p_nodeId]() { openNode(p_nodeId); });

  if (!p_isFolder) {
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
  if (!p_nodeId.isValid()) {
    return;
  }

  auto *copyAction = p_menu->addAction(tr("&Copy"));
  connect(copyAction, &QAction::triggered, this,
          [this, p_nodeId]() { copyNodes(QList<NodeIdentifier>() << p_nodeId); });

  auto *cutAction = p_menu->addAction(tr("Cu&t"));
  connect(cutAction, &QAction::triggered, this,
          [this, p_nodeId]() { cutNodes(QList<NodeIdentifier>() << p_nodeId); });

  // Paste only available on containers
  if (p_isFolder && canPaste()) {
    auto *pasteAction = p_menu->addAction(tr("&Paste"));
    connect(pasteAction, &QAction::triggered, this,
            [this, p_nodeId]() { pasteNodes(p_nodeId); });
  }

  auto *duplicateAction = p_menu->addAction(tr("D&uplicate"));
  connect(duplicateAction, &QAction::triggered, this,
          [this, p_nodeId]() { duplicateNode(p_nodeId); });
}

void NotebookNodeController::addImportExportActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                                    bool p_isFolder) {
  // Import actions - only on containers
  NodeIdentifier targetFolder = p_isFolder ? p_nodeId : getParentFolder(p_nodeId);

  if (targetFolder.isValid()) {
    auto *importFilesAction = p_menu->addAction(tr("Import &Files"));
    connect(importFilesAction, &QAction::triggered, this,
            [this, targetFolder]() { importFiles(targetFolder); });

    auto *importFolderAction = p_menu->addAction(tr("Import F&older"));
    connect(importFolderAction, &QAction::triggered, this,
            [this, targetFolder]() { importFolder(targetFolder); });
  }

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
}

void NotebookNodeController::addMiscActions(QMenu *p_menu, const NodeIdentifier &p_nodeId,
                                            bool p_isFolder) {
  if (p_isFolder) {
    auto *sortAction = p_menu->addAction(tr("&Sort"));
    connect(sortAction, &QAction::triggered, this,
            [this, p_nodeId]() { sortNodes(p_nodeId); });
  }

  auto *reloadAction = p_menu->addAction(tr("Re&load"));
  connect(reloadAction, &QAction::triggered, this,
          [this, p_nodeId]() { reloadNode(p_nodeId); });

  if (p_nodeId.isValid()) {
    auto *pinAction = p_menu->addAction(tr("Pin to &Quick Access"));
    connect(pinAction, &QAction::triggered, this,
            [this, p_nodeId]() { pinNodeToQuickAccess(p_nodeId); });

    auto *tagAction = p_menu->addAction(tr("Manage &Tags"));
    connect(tagAction, &QAction::triggered, this,
            [this, p_nodeId]() { manageNodeTags(p_nodeId); });
  }
}

void NotebookNodeController::newNote(const NodeIdentifier &p_parentId) {
  QString notebookId = p_parentId.isValid() ? p_parentId.notebookId : currentNotebookId();
  if (notebookId.isEmpty()) {
    return;
  }

  QString folderPath = p_parentId.relativePath;

  bool ok;
  QString name = QInputDialog::getText(m_view, tr("New Note"), tr("Note name:"), QLineEdit::Normal,
                                       tr("new_note.md"), &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  try {
    auto *notebookService = m_services.get<NotebookService>();
    QString newFilePath = notebookService->createFile(notebookId, folderPath, name);

    if (!newFilePath.isEmpty() && m_model) {
      m_model->reloadNode(p_parentId);

      // Select the new note
      NodeIdentifier newNodeId;
      newNodeId.notebookId = notebookId;
      newNodeId.relativePath = newFilePath;

      if (m_view) {
        m_view->selectNode(newNodeId);
      }

      // Open the new note
      openNode(newNodeId);
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to create note: %1").arg(e.what()));
  }
}

void NotebookNodeController::newFolder(const NodeIdentifier &p_parentId) {
  QString notebookId = p_parentId.isValid() ? p_parentId.notebookId : currentNotebookId();
  if (notebookId.isEmpty()) {
    return;
  }

  QString parentPath = p_parentId.relativePath;

  bool ok;
  QString name = QInputDialog::getText(m_view, tr("New Folder"), tr("Folder name:"),
                                       QLineEdit::Normal, tr("new_folder"), &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  try {
    auto *notebookService = m_services.get<NotebookService>();
    QString newFolderPath = notebookService->createFolder(notebookId, parentPath, name);

    if (!newFolderPath.isEmpty() && m_model) {
      m_model->reloadNode(p_parentId);

      // Select the new folder
      NodeIdentifier newNodeId;
      newNodeId.notebookId = notebookId;
      newNodeId.relativePath = newFolderPath;

      if (m_view) {
        m_view->selectNode(newNodeId);
      }
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to create folder: %1").arg(e.what()));
  }
}

void NotebookNodeController::openNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  auto paras = QSharedPointer<FileOpenParameters>::create();
  emit nodeActivated(p_nodeId, paras);
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

  if (!confirmDelete(p_nodeIds, false)) {
    return;
  }

  auto *notebookService = m_services.get<NotebookService>();

  for (const NodeIdentifier &nodeId : p_nodeIds) {
    notifyBeforeNodeOperation(nodeId, QStringLiteral("delete"));
    try {
      NodeInfo nodeInfo = getNodeInfo(nodeId);
      if (nodeInfo.isFolder) {
        notebookService->deleteFolder(nodeId.notebookId, nodeId.relativePath);
      } else {
        notebookService->deleteFile(nodeId.notebookId, nodeId.relativePath);
      }
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"), tr("Failed to delete: %1").arg(e.what()));
    }
  }

  if (m_model) {
    m_model->reload();
  }
}

void NotebookNodeController::removeNodesFromNotebook(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  int ret = QMessageBox::question(
      m_view, tr("Remove from Notebook"),
      tr("Remove %n node(s) from notebook? Files will be kept on disk.", "", p_nodeIds.size()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (ret != QMessageBox::Yes) {
    return;
  }

  // TODO: Implement configOnly removal via NotebookService
  // For now, just show not implemented
  QMessageBox::information(m_view, tr("Remove"),
                           tr("Remove from notebook (config only) not yet implemented."));
}

void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &p_nodeIds) {
  m_clipboardNodes = p_nodeIds;
  m_isCut = false;
}

void NotebookNodeController::cutNodes(const QList<NodeIdentifier> &p_nodeIds) {
  m_clipboardNodes = p_nodeIds;
  m_isCut = true;
}

void NotebookNodeController::pasteNodes(const NodeIdentifier &p_targetFolderId) {
  if (m_clipboardNodes.isEmpty() || !p_targetFolderId.isValid()) {
    return;
  }

  NodeInfo targetInfo = getNodeInfo(p_targetFolderId);
  if (!targetInfo.isFolder) {
    return;
  }

  auto *notebookService = m_services.get<NotebookService>();

  for (const NodeIdentifier &nodeId : m_clipboardNodes) {
    if (m_isCut) {
      notifyBeforeNodeOperation(nodeId, QStringLiteral("move"));
    }
    try {
      NodeInfo nodeInfo = getNodeInfo(nodeId);
      if (m_isCut) {
        // Move operation
        if (nodeInfo.isFolder) {
          notebookService->moveFolder(nodeId.notebookId, nodeId.relativePath,
                                     p_targetFolderId.relativePath);
        } else {
          notebookService->moveFile(nodeId.notebookId, nodeId.relativePath,
                                   p_targetFolderId.relativePath);
        }
      } else {
        // Copy operation
        if (nodeInfo.isFolder) {
          notebookService->copyFolder(nodeId.notebookId, nodeId.relativePath,
                                     p_targetFolderId.relativePath, nodeInfo.name);
        } else {
          notebookService->copyFile(nodeId.notebookId, nodeId.relativePath,
                                   p_targetFolderId.relativePath, nodeInfo.name);
        }
      }
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"), tr("Failed to paste: %1").arg(e.what()));
    }
  }

  if (m_isCut) {
    m_clipboardNodes.clear();
    m_isCut = false;
  }

  if (m_model) {
    m_model->reloadNode(p_targetFolderId);
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
    m_clipboardNodes.clear();
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

  bool ok;
  QString newName =
      QInputDialog::getText(m_view, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                            nodeInfo.name, &ok);
  if (!ok || newName.isEmpty() || newName == nodeInfo.name) {
    return;
  }

  notifyBeforeNodeOperation(p_nodeId, QStringLiteral("rename"));

  try {
    auto *notebookService = m_services.get<NotebookService>();
    if (nodeInfo.isFolder) {
      notebookService->renameFolder(p_nodeId.notebookId, p_nodeId.relativePath, newName);
    } else {
      notebookService->renameFile(p_nodeId.notebookId, p_nodeId.relativePath, newName);
    }

    if (m_model) {
      NodeIdentifier parentId = getParentFolder(p_nodeId);
      m_model->reloadNode(parentId);
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to rename: %1").arg(e.what()));
  }
}

void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &p_nodeIds,
                                       const NodeIdentifier &p_targetFolderId) {
  cutNodes(p_nodeIds);
  pasteNodes(p_targetFolderId);
}

void NotebookNodeController::importFiles(const NodeIdentifier &p_targetFolderId) {
  if (!p_targetFolderId.isValid()) {
    return;
  }

  QStringList files =
      QFileDialog::getOpenFileNames(m_view, tr("Import Files"), QString(), tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  // TODO: Implement file import via NotebookService
  QMessageBox::information(m_view, tr("Import"),
                           tr("File import not yet implemented in new architecture."));
}

void NotebookNodeController::importFolder(const NodeIdentifier &p_targetFolderId) {
  if (!p_targetFolderId.isValid()) {
    return;
  }

  QString folder = QFileDialog::getExistingDirectory(m_view, tr("Import Folder"), QString());
  if (folder.isEmpty()) {
    return;
  }

  // TODO: Implement folder import via NotebookService
  QMessageBox::information(m_view, tr("Import"),
                           tr("Folder import not yet implemented in new architecture."));
}

void NotebookNodeController::exportNode(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  QMessageBox::information(m_view, tr("Export"), tr("Export functionality not yet implemented."));
}

void NotebookNodeController::showNodeProperties(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  NodeInfo nodeInfo = getNodeInfo(p_nodeId);
  if (!nodeInfo.isValid()) {
    return;
  }

  QString info;
  info += tr("Name: %1\n").arg(nodeInfo.name);
  info += tr("Path: %1\n").arg(buildAbsolutePath(p_nodeId));
  info += tr("Type: %1\n").arg(nodeInfo.isFolder ? tr("Folder") : tr("Note"));
  info += tr("Created: %1\n").arg(nodeInfo.createdTimeUtc.toLocalTime().toString());
  info += tr("Modified: %1\n").arg(nodeInfo.modifiedTimeUtc.toLocalTime().toString());

  if (nodeInfo.isFolder) {
    info += tr("Children: %1\n").arg(nodeInfo.childCount);
  }

  if (!nodeInfo.tags.isEmpty()) {
    info += tr("Tags: %1\n").arg(nodeInfo.tags.join(", "));
  }

  QMessageBox::information(m_view, tr("Properties"), info);
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
  QMessageBox::information(m_view, tr("Sort"), tr("Sort functionality not yet implemented."));
}

void NotebookNodeController::reloadNode(const NodeIdentifier &p_nodeId) {
  if (p_nodeId.isValid()) {
    auto event = QSharedPointer<Event>::create();
    emit nodeAboutToReload(p_nodeId, event);

    if (m_model) {
      m_model->reloadNode(p_nodeId);
    }
  } else {
    reloadAll();
  }
}

void NotebookNodeController::reloadAll() {
  if (m_model) {
    m_model->reload();
  }
}

void NotebookNodeController::pinNodeToQuickAccess(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  QMessageBox::information(m_view, tr("Quick Access"),
                           tr("Quick Access functionality not yet implemented."));
}

void NotebookNodeController::manageNodeTags(const NodeIdentifier &p_nodeId) {
  Q_UNUSED(p_nodeId);
  QMessageBox::information(m_view, tr("Tags"), tr("Tag management not yet implemented."));
}

bool NotebookNodeController::canPaste() const { return !m_clipboardNodes.isEmpty(); }

bool NotebookNodeController::confirmDelete(const QList<NodeIdentifier> &p_nodeIds,
                                           bool p_permanent) {
  QString title = p_permanent ? tr("Delete Permanently") : tr("Delete");
  QString message;

  if (p_permanent) {
    message = tr("Permanently delete %n node(s)? This cannot be undone.", "", p_nodeIds.size());
  } else {
    message = tr("Move %n node(s) to recycle bin?", "", p_nodeIds.size());
  }

  int ret = QMessageBox::question(m_view, title, message, QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No);

  return ret == QMessageBox::Yes;
}

void NotebookNodeController::notifyBeforeNodeOperation(const NodeIdentifier &p_nodeId,
                                                       const QString &p_operation) {
  if (!p_nodeId.isValid()) {
    return;
  }

  auto event = QSharedPointer<Event>::create();

  if (p_operation == QStringLiteral("delete") || p_operation == QStringLiteral("remove")) {
    emit nodeAboutToRemove(p_nodeId, event);
  } else if (p_operation == QStringLiteral("move") || p_operation == QStringLiteral("rename")) {
    emit nodeAboutToMove(p_nodeId, event);
  }

  // Also request to close the file
  QString path = buildAbsolutePath(p_nodeId);
  if (!path.isEmpty()) {
    emit closeFileRequested(path, event);
  }
}
