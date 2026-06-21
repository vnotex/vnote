#ifndef TWOCOLUMNSNODEEXPLORER_H
#define TWOCOLUMNSNODEEXPLORER_H

#include "inodeexplorer.h"

class QSplitter;
class QMenu;

namespace vnotex {

class NotebookNodeModel;
class NotebookNodeProxyModel;
class NotebookNodeView;
class NotebookNodeDelegate;
class FileNodeDelegate;
class FileListView;
class NotebookNodeController;
class ServiceLocator;
struct FileOpenParameters;
class Event;

// TwoColumnsNodeExplorer encapsulates a two-panel view:
// - Left panel: folder tree (shows only folders)
// - Right panel: file list (shows files in selected folder)
//
// It provides a unified interface similar to NotebookNodeView for easy
// integration with NotebookExplorer2.
class TwoColumnsNodeExplorer : public INodeExplorer {
  Q_OBJECT

public:
  explicit TwoColumnsNodeExplorer(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TwoColumnsNodeExplorer() override;

  // Set the notebook to display
  void setNotebookId(const QString &p_notebookId) override;
  QString getNotebookId() const override;

  // Selection helpers (unified interface)
  NodeIdentifier currentNodeId() const override;
  QList<NodeIdentifier> selectedNodeIds() const override;

  // Navigation (unified interface)
  void selectNode(const NodeIdentifier &p_nodeId) override;
  void expandToNode(const NodeIdentifier &p_nodeId) override;
  void scrollToNode(const NodeIdentifier &p_nodeId) override;

  // Expand/collapse helpers
  void expandAll() override;
  void collapseAll() override;

  // Get the currently selected folder (for new note/folder operations)
  NodeIdentifier currentFolderId() const override;

  // View order
  void setViewOrder(ViewOrder p_order) override;

  // External nodes visibility
  void setExternalNodesVisible(bool p_visible) override;

  // Node info lookup - checks both models
  NodeInfo getNodeInfo(const NodeIdentifier &p_nodeId) const override;

  // Operation handlers - delegates to appropriate controller
  void handleRenameResult(const NodeIdentifier &p_nodeId, const QString &p_newName) override;
  void handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent) override;
  void handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) override;
  void handleMissingRemovalConfirmed(const QList<NodeIdentifier> &p_nodeIds) override;
  void suppressMissingNodes(const QList<NodeIdentifier> &p_nodeIds) override;
  void handleMarkResult(const NodeIdentifier &p_nodeId, const QString &p_textColor,
                        const QString &p_bgColor) override;

  // Reload a node in the appropriate model
  // INodeExplorer interface - auto-detects folder vs file
  void reloadNode(const NodeIdentifier &p_nodeId) override;
  void startInlineRename(const NodeIdentifier &p_nodeId) override;
  // Extended version for explicit folder/file specification
  void reloadNode(const NodeIdentifier &p_nodeId, bool p_isFolder);

  // === Reorder ===
  // T11 (notebook-explorer-drag-reorder): bridge method required by
  // INodeExplorer. The full forwarding + signal mirror is added in T12
  // (sortRequested wiring through both panes). This implementation builds
  // the NodeIdentifier lists and dispatches to the folder pane's controller.
  void requestReorderNodes(const NodeIdentifier &p_parentId,
                           const QStringList &p_orderedFolderNames,
                           const QStringList &p_orderedFileNames) override;

  // === State capture/restore ===
  NodeExplorerState captureState() const override;
  void applyState(const NodeExplorerState &p_state) override;

  // Splitter state for session save/restore
  QByteArray saveSplitterState() const;
  void restoreSplitterState(const QByteArray &p_data);

  // Note: INodeExplorer signals are inherited:
  // - nodeActivated, contextMenuRequested, fileActivated, nodeAboutToMove, nodeAboutToRemove,
  //   nodeAboutToReload, closeFileRequested, newNoteRequested, newFolderRequested,
  //   renameRequested, deleteRequested, removeFromNotebookRequested, propertiesRequested,
  //   errorOccurred, infoMessage
  //
  // This class uses contextMenuRequested with bool parameter to indicate which panel.
  // The INodeExplorer::contextMenuRequested(nodeId, globalPos) is NOT emitted directly;
  // callers should connect to the 3-param version.

signals:
  void exportNodeRequested(const NodeIdentifier &p_nodeId);

  // T12 (notebook-explorer-drag-reorder): forwarded from
  // NotebookNodeController::sortRequested. Wired for BOTH the folder pane
  // controller AND the file pane controller via connectControllerSignals,
  // so either pane's "Sort..." action surfaces here. NotebookExplorer2
  // connects this to onSortRequested (shared with CombinedNodeExplorer) and
  // owns SortDialog2 — controllers MUST NOT show QDialog
  // (src/controllers/AGENTS.md).
  void sortRequested(const NodeIdentifier &p_parentId);

private slots:
  void onFolderSelectionChanged(const QList<NodeIdentifier> &p_nodeIds);
  void onFolderContextMenu(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);
  void onFileContextMenu(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

private:
  void setupUI();
  void connectControllerSignals(NotebookNodeController *p_controller);

  // Helper to determine which controller to use for a node
  NotebookNodeController *controllerForNode(const NodeIdentifier &p_nodeId) const;

  ServiceLocator &m_services;
  QString m_notebookId;

  // Folder panel (left)
  NotebookNodeModel *m_folderModel = nullptr;
  NotebookNodeProxyModel *m_folderProxyModel = nullptr;
  NotebookNodeView *m_folderView = nullptr;
  NotebookNodeDelegate *m_folderDelegate = nullptr;
  NotebookNodeController *m_folderController = nullptr;

  // File panel (right)
  NotebookNodeModel *m_fileModel = nullptr;
  NotebookNodeProxyModel *m_fileProxyModel = nullptr;
  FileListView *m_fileView = nullptr;
  FileNodeDelegate *m_fileDelegate = nullptr;
  NotebookNodeController *m_fileController = nullptr;

  // Layout
  QSplitter *m_splitter = nullptr;
};

} // namespace vnotex

#endif // TWOCOLUMNSNODEEXPLORER_H
