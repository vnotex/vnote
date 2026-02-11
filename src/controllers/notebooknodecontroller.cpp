#include "notebooknodecontroller.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>

#include <core/events.h>
#include <core/fileopenparameters.h>
#include <core/notebook/node.h>
#include <core/notebook/notebook.h>
#include <core/vnotex.h>
#include <models/notebooknodemodel.h>
#include <utils/pathutils.h>
#include <views/notebooknodeview.h>
#include <widgets/dialogs/nodeinfowidget.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

NotebookNodeController::NotebookNodeController(QObject *p_parent) : QObject(p_parent) {}

NotebookNodeController::~NotebookNodeController() {}

void NotebookNodeController::setModel(NotebookNodeModel *p_model) { m_model = p_model; }

NotebookNodeModel *NotebookNodeController::model() const { return m_model; }

void NotebookNodeController::setView(NotebookNodeView *p_view) { m_view = p_view; }

NotebookNodeView *NotebookNodeController::view() const { return m_view; }

QSharedPointer<Notebook> NotebookNodeController::currentNotebook() const {
  if (m_model) {
    return m_model->getNotebook();
  }
  return nullptr;
}

QMenu *NotebookNodeController::createContextMenu(Node *p_node, QWidget *p_parent) {
  QMenu *menu = new QMenu(p_parent);

  addNewActions(menu, p_node);
  menu->addSeparator();
  addOpenActions(menu, p_node);
  menu->addSeparator();
  addEditActions(menu, p_node);
  menu->addSeparator();
  addCopyMoveActions(menu, p_node);
  menu->addSeparator();
  addImportExportActions(menu, p_node);
  menu->addSeparator();
  addInfoActions(menu, p_node);
  menu->addSeparator();
  addMiscActions(menu, p_node);

  return menu;
}

void NotebookNodeController::addNewActions(QMenu *p_menu, Node *p_node) {
  // Determine target folder for new items
  Node *targetFolder = p_node;
  if (p_node && !p_node->isContainer()) {
    targetFolder = p_node->getParent();
  }

  auto *newNoteAction = p_menu->addAction(tr("New &Note"));
  connect(newNoteAction, &QAction::triggered, this, [this, targetFolder]() {
    newNote(targetFolder);
  });

  auto *newFolderAction = p_menu->addAction(tr("New &Folder"));
  connect(newFolderAction, &QAction::triggered, this, [this, targetFolder]() {
    newFolder(targetFolder);
  });
}

void NotebookNodeController::addOpenActions(QMenu *p_menu, Node *p_node) {
  if (!p_node) {
    return;
  }

  auto *openAction = p_menu->addAction(tr("&Open"));
  connect(openAction, &QAction::triggered, this, [this, p_node]() { openNode(p_node); });

  if (p_node->hasContent()) {
    auto *openWithAction = p_menu->addAction(tr("Open With Default App"));
    connect(openWithAction, &QAction::triggered, this,
            [this, p_node]() { openNodeWithDefaultApp(p_node); });
  }
}

void NotebookNodeController::addEditActions(QMenu *p_menu, Node *p_node) {
  if (!p_node) {
    return;
  }

  if (!p_node->isReadOnly()) {
    auto *renameAction = p_menu->addAction(tr("&Rename"));
    connect(renameAction, &QAction::triggered, this, [this, p_node]() { renameNode(p_node); });

    auto *deleteAction = p_menu->addAction(tr("&Delete"));
    connect(deleteAction, &QAction::triggered, this, [this, p_node]() {
      deleteNodes(QList<Node *>() << p_node);
    });

    auto *removeAction = p_menu->addAction(tr("Remove From Notebook"));
    removeAction->setToolTip(tr("Remove from notebook but keep files on disk"));
    connect(removeAction, &QAction::triggered, this, [this, p_node]() {
      removeNodesFromNotebook(QList<Node *>() << p_node);
    });
  }
}

void NotebookNodeController::addCopyMoveActions(QMenu *p_menu, Node *p_node) {
  if (!p_node) {
    return;
  }

  auto *copyAction = p_menu->addAction(tr("&Copy"));
  connect(copyAction, &QAction::triggered, this,
          [this, p_node]() { copyNodes(QList<Node *>() << p_node); });

  if (!p_node->isReadOnly()) {
    auto *cutAction = p_menu->addAction(tr("Cu&t"));
    connect(cutAction, &QAction::triggered, this,
            [this, p_node]() { cutNodes(QList<Node *>() << p_node); });
  }

  // Paste only available on containers
  if (p_node->isContainer() && canPaste()) {
    auto *pasteAction = p_menu->addAction(tr("&Paste"));
    connect(pasteAction, &QAction::triggered, this, [this, p_node]() { pasteNodes(p_node); });
  }

  auto *duplicateAction = p_menu->addAction(tr("D&uplicate"));
  connect(duplicateAction, &QAction::triggered, this, [this, p_node]() { duplicateNode(p_node); });
}

void NotebookNodeController::addImportExportActions(QMenu *p_menu, Node *p_node) {
  // Import actions - only on containers
  Node *targetFolder = p_node;
  if (p_node && !p_node->isContainer()) {
    targetFolder = p_node->getParent();
  }

  if (targetFolder) {
    auto *importFilesAction = p_menu->addAction(tr("Import &Files"));
    connect(importFilesAction, &QAction::triggered, this,
            [this, targetFolder]() { importFiles(targetFolder); });

    auto *importFolderAction = p_menu->addAction(tr("Import F&older"));
    connect(importFolderAction, &QAction::triggered, this,
            [this, targetFolder]() { importFolder(targetFolder); });
  }

  if (p_node) {
    auto *exportAction = p_menu->addAction(tr("&Export"));
    connect(exportAction, &QAction::triggered, this, [this, p_node]() { exportNode(p_node); });
  }
}

void NotebookNodeController::addInfoActions(QMenu *p_menu, Node *p_node) {
  if (!p_node) {
    return;
  }

  auto *copyPathAction = p_menu->addAction(tr("Copy &Path"));
  connect(copyPathAction, &QAction::triggered, this, [this, p_node]() { copyNodePath(p_node); });

  auto *locateAction = p_menu->addAction(tr("Open &Location"));
  connect(locateAction, &QAction::triggered, this,
          [this, p_node]() { locateNodeInFileManager(p_node); });

  auto *propertiesAction = p_menu->addAction(tr("P&roperties"));
  connect(propertiesAction, &QAction::triggered, this,
          [this, p_node]() { showNodeProperties(p_node); });
}

void NotebookNodeController::addMiscActions(QMenu *p_menu, Node *p_node) {
  if (p_node && p_node->isContainer()) {
    auto *sortAction = p_menu->addAction(tr("&Sort"));
    connect(sortAction, &QAction::triggered, this, [this, p_node]() { sortNodes(p_node); });
  }

  auto *reloadAction = p_menu->addAction(tr("Re&load"));
  connect(reloadAction, &QAction::triggered, this, [this, p_node]() { reloadNode(p_node); });

  if (p_node) {
    auto *pinAction = p_menu->addAction(tr("Pin to &Quick Access"));
    connect(pinAction, &QAction::triggered, this,
            [this, p_node]() { pinNodeToQuickAccess(p_node); });

    auto *tagAction = p_menu->addAction(tr("Manage &Tags"));
    connect(tagAction, &QAction::triggered, this, [this, p_node]() { manageNodeTags(p_node); });
  }
}

void NotebookNodeController::newNote(Node *p_parentNode) {
  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  Node *targetFolder = p_parentNode;
  if (!targetFolder) {
    targetFolder = notebook->getRootNode().data();
  }

  if (!targetFolder->isContainer()) {
    targetFolder = targetFolder->getParent();
  }

  bool ok;
  QString name = QInputDialog::getText(m_view, tr("New Note"), tr("Note name:"),
                                       QLineEdit::Normal, tr("new_note.md"), &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  try {
    auto newNode = notebook->newNode(targetFolder, Node::Flag::Content, name);
    if (newNode && m_model) {
      m_model->reloadNode(targetFolder);
      if (m_view) {
        m_view->selectNode(newNode.data());
      }
      // Open the new note
      openNode(newNode.data());
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to create note: %1").arg(e.what()));
  }
}

void NotebookNodeController::newFolder(Node *p_parentNode) {
  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  Node *targetFolder = p_parentNode;
  if (!targetFolder) {
    targetFolder = notebook->getRootNode().data();
  }

  if (!targetFolder->isContainer()) {
    targetFolder = targetFolder->getParent();
  }

  bool ok;
  QString name = QInputDialog::getText(m_view, tr("New Folder"), tr("Folder name:"),
                                       QLineEdit::Normal, tr("new_folder"), &ok);
  if (!ok || name.isEmpty()) {
    return;
  }

  try {
    auto newNode = notebook->newNode(targetFolder, Node::Flag::Container, name);
    if (newNode && m_model) {
      m_model->reloadNode(targetFolder);
      if (m_view) {
        m_view->selectNode(newNode.data());
      }
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to create folder: %1").arg(e.what()));
  }
}

void NotebookNodeController::openNode(Node *p_node) {
  if (!p_node) {
    return;
  }

  auto paras = QSharedPointer<FileOpenParameters>::create();
  emit nodeActivated(p_node, paras);
}

void NotebookNodeController::openNodeWithDefaultApp(Node *p_node) {
  if (!p_node || !p_node->hasContent()) {
    return;
  }

  QString path = p_node->fetchAbsolutePath();
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void NotebookNodeController::deleteNodes(const QList<Node *> &p_nodes) {
  if (p_nodes.isEmpty()) {
    return;
  }

  if (!confirmDelete(p_nodes, false)) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  for (Node *node : p_nodes) {
    notifyBeforeNodeOperation(node, QStringLiteral("delete"));
    try {
      notebook->moveNodeToRecycleBin(node);
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"), tr("Failed to delete: %1").arg(e.what()));
    }
  }

  if (m_model) {
    m_model->reload();
  }
}

void NotebookNodeController::removeNodesFromNotebook(const QList<Node *> &p_nodes) {
  if (p_nodes.isEmpty()) {
    return;
  }

  int ret = QMessageBox::question(
      m_view, tr("Remove from Notebook"),
      tr("Remove %n node(s) from notebook? Files will be kept on disk.", "", p_nodes.size()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  if (ret != QMessageBox::Yes) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  for (Node *node : p_nodes) {
    notifyBeforeNodeOperation(node, QStringLiteral("remove"));
    try {
      notebook->removeNode(node, false, true); // configOnly = true
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"), tr("Failed to remove: %1").arg(e.what()));
    }
  }

  if (m_model) {
    m_model->reload();
  }
}

void NotebookNodeController::copyNodes(const QList<Node *> &p_nodes) {
  m_clipboardNodes = p_nodes;
  m_isCut = false;
}

void NotebookNodeController::cutNodes(const QList<Node *> &p_nodes) {
  m_clipboardNodes = p_nodes;
  m_isCut = true;
}

void NotebookNodeController::pasteNodes(Node *p_targetFolder) {
  if (m_clipboardNodes.isEmpty() || !p_targetFolder || !p_targetFolder->isContainer()) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  for (Node *node : m_clipboardNodes) {
    if (m_isCut) {
      notifyBeforeNodeOperation(node, QStringLiteral("move"));
    }
    try {
      auto srcNode = node->sharedFromThis();
      notebook->copyNodeAsChildOf(srcNode, p_targetFolder, m_isCut);
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"),
                            tr("Failed to paste: %1").arg(e.what()));
    }
  }

  if (m_isCut) {
    m_clipboardNodes.clear();
    m_isCut = false;
  }

  if (m_model) {
    m_model->reloadNode(p_targetFolder);
  }
}

void NotebookNodeController::duplicateNode(Node *p_node) {
  if (!p_node) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  Node *parent = p_node->getParent();
  if (!parent) {
    return;
  }

  try {
    auto srcNode = p_node->sharedFromThis();
    auto newNode = notebook->copyNodeAsChildOf(srcNode, parent, false);
    if (newNode && m_model) {
      m_model->reloadNode(parent);
      if (m_view) {
        m_view->selectNode(newNode.data());
      }
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to duplicate: %1").arg(e.what()));
  }
}

void NotebookNodeController::renameNode(Node *p_node) {
  if (!p_node || p_node->isReadOnly()) {
    return;
  }

  bool ok;
  QString newName = QInputDialog::getText(m_view, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                          p_node->getName(), &ok);
  if (!ok || newName.isEmpty() || newName == p_node->getName()) {
    return;
  }

  if (!p_node->canRename(newName)) {
    QMessageBox::warning(m_view, tr("Rename"),
                         tr("Cannot rename to '%1'. Name may already exist or is invalid.")
                             .arg(newName));
    return;
  }

  notifyBeforeNodeOperation(p_node, QStringLiteral("rename"));

  try {
    p_node->updateName(newName);
    if (m_model) {
      m_model->nodeDataChanged(p_node);
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to rename: %1").arg(e.what()));
  }
}

void NotebookNodeController::moveNodes(const QList<Node *> &p_nodes, Node *p_targetFolder) {
  cutNodes(p_nodes);
  pasteNodes(p_targetFolder);
}

void NotebookNodeController::importFiles(Node *p_targetFolder) {
  if (!p_targetFolder || !p_targetFolder->isContainer()) {
    return;
  }

  QStringList files = QFileDialog::getOpenFileNames(m_view, tr("Import Files"),
                                                    QString(), tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  for (const QString &file : files) {
    try {
      notebook->copyAsNode(p_targetFolder, Node::Flag::Content, file);
    } catch (const std::exception &e) {
      QMessageBox::critical(m_view, tr("Error"),
                            tr("Failed to import '%1': %2").arg(file, e.what()));
    }
  }

  if (m_model) {
    m_model->reloadNode(p_targetFolder);
  }
}

void NotebookNodeController::importFolder(Node *p_targetFolder) {
  if (!p_targetFolder || !p_targetFolder->isContainer()) {
    return;
  }

  QString folder =
      QFileDialog::getExistingDirectory(m_view, tr("Import Folder"), QString());
  if (folder.isEmpty()) {
    return;
  }

  auto notebook = currentNotebook();
  if (!notebook) {
    return;
  }

  try {
    notebook->copyAsNode(p_targetFolder, Node::Flag::Container, folder);
    if (m_model) {
      m_model->reloadNode(p_targetFolder);
    }
  } catch (const std::exception &e) {
    QMessageBox::critical(m_view, tr("Error"), tr("Failed to import folder: %1").arg(e.what()));
  }
}

void NotebookNodeController::exportNode(Node *p_node) {
  // TODO: Implement export functionality
  // This would typically open an export dialog
  Q_UNUSED(p_node);
  QMessageBox::information(m_view, tr("Export"), tr("Export functionality not yet implemented."));
}

void NotebookNodeController::showNodeProperties(Node *p_node) {
  if (!p_node) {
    return;
  }

  // Create a simple properties dialog
  QString info;
  info += tr("Name: %1\n").arg(p_node->getName());
  info += tr("Path: %1\n").arg(p_node->fetchAbsolutePath());
  info += tr("Type: %1\n").arg(p_node->isContainer() ? tr("Folder") : tr("Note"));
  info += tr("Created: %1\n").arg(p_node->getCreatedTimeUtc().toLocalTime().toString());
  info += tr("Modified: %1\n").arg(p_node->getModifiedTimeUtc().toLocalTime().toString());

  if (p_node->isContainer()) {
    info += tr("Children: %1\n").arg(p_node->getChildrenCount());
  }

  QStringList tags = p_node->getTags();
  if (!tags.isEmpty()) {
    info += tr("Tags: %1\n").arg(tags.join(", "));
  }

  QMessageBox::information(m_view, tr("Properties"), info);
}

void NotebookNodeController::copyNodePath(Node *p_node) {
  if (!p_node) {
    return;
  }

  QString path = p_node->fetchAbsolutePath();
  QApplication::clipboard()->setText(path);
}

void NotebookNodeController::locateNodeInFileManager(Node *p_node) {
  if (!p_node) {
    return;
  }

  QString path = p_node->fetchAbsolutePath();
  // Open the parent directory in file manager
  QString dirPath = p_node->isContainer() ? path : PathUtils::parentDirPath(path);
  QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void NotebookNodeController::sortNodes(Node *p_parentNode) {
  // TODO: Implement sort dialog
  Q_UNUSED(p_parentNode);
  QMessageBox::information(m_view, tr("Sort"), tr("Sort functionality not yet implemented."));
}

void NotebookNodeController::reloadNode(Node *p_node) {
  if (p_node) {
    auto event = QSharedPointer<Event>::create();
    emit nodeAboutToReload(p_node, event);

    if (m_model) {
      m_model->reloadNode(p_node);
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

void NotebookNodeController::pinNodeToQuickAccess(Node *p_node) {
  // TODO: Implement quick access pinning
  Q_UNUSED(p_node);
  QMessageBox::information(m_view, tr("Quick Access"),
                           tr("Quick Access functionality not yet implemented."));
}

void NotebookNodeController::manageNodeTags(Node *p_node) {
  // TODO: Implement tag management dialog
  Q_UNUSED(p_node);
  QMessageBox::information(m_view, tr("Tags"), tr("Tag management not yet implemented."));
}

bool NotebookNodeController::canPaste() const { return !m_clipboardNodes.isEmpty(); }

bool NotebookNodeController::confirmDelete(const QList<Node *> &p_nodes, bool p_permanent) {
  QString title = p_permanent ? tr("Delete Permanently") : tr("Delete");
  QString message;

  if (p_permanent) {
    message = tr("Permanently delete %n node(s)? This cannot be undone.", "", p_nodes.size());
  } else {
    message = tr("Move %n node(s) to recycle bin?", "", p_nodes.size());
  }

  int ret =
      QMessageBox::question(m_view, title, message, QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::No);

  return ret == QMessageBox::Yes;
}

void NotebookNodeController::notifyBeforeNodeOperation(Node *p_node, const QString &p_operation) {
  if (!p_node) {
    return;
  }

  auto event = QSharedPointer<Event>::create();

  if (p_operation == QStringLiteral("delete") || p_operation == QStringLiteral("remove")) {
    emit nodeAboutToRemove(p_node, event);
  } else if (p_operation == QStringLiteral("move") || p_operation == QStringLiteral("rename")) {
    emit nodeAboutToMove(p_node, event);
  }

  // Also request to close the file
  QString path = p_node->fetchAbsolutePath();
  emit closeFileRequested(path, event);
}
