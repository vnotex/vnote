#ifndef NOTEBOOKNODECONTROLLER_H
#define NOTEBOOKNODECONTROLLER_H

#include <QList>
#include <QObject>
#include <QSharedPointer>

#include <functional>

#include <core/fileopensettings.h>
#include <models/inodelistmodel.h>
#include <nodeinfo.h>

class QMenu;

namespace vnotex {

class NotebookNodeModel;
class NotebookNodeView;
class ServiceLocator;
class Event;

// Controller class that handles all user actions on notebook nodes.
// Acts as the bridge between View and Model, implementing business logic.
// Uses NodeIdentifier for all node references - no more Node* pointers.
class NotebookNodeController : public QObject {
  Q_OBJECT

public:
  explicit NotebookNodeController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~NotebookNodeController() override;

  // Set the model this controller operates on
  void setModel(INodeListModel *p_model);
  INodeListModel *model() const;

  // Set the view this controller manages
  void setView(NotebookNodeView *p_view);
  NotebookNodeView *view() const;

  // Context menu creation
  QMenu *createContextMenu(const NodeIdentifier &p_nodeId, QWidget *p_parent = nullptr);

  // Context menu for external (unindexed) nodes
  QMenu *createExternalNodeContextMenu(const NodeIdentifier &p_nodeId, QWidget *p_parent = nullptr);

  // Node operations using NodeIdentifier
  void newNote(const NodeIdentifier &p_parentId);
  void newFolder(const NodeIdentifier &p_parentId);
  void openNode(const NodeIdentifier &p_nodeId);
  void openNodeWithDefaultApp(const NodeIdentifier &p_nodeId);
  void deleteNodes(const QList<NodeIdentifier> &p_nodeIds);
  void removeNodesFromNotebook(const QList<NodeIdentifier> &p_nodeIds);
  void copyNodes(const QList<NodeIdentifier> &p_nodeIds);
  void cutNodes(const QList<NodeIdentifier> &p_nodeIds);
  void pasteNodes(const NodeIdentifier &p_targetFolderId);
  void duplicateNode(const NodeIdentifier &p_nodeId);
  void renameNode(const NodeIdentifier &p_nodeId);
  void markNode(const NodeIdentifier &p_nodeId);
  // T9 (notebook-explorer-drag-reorder): virtual so the GUI drag-drop test
  // (tests/gui/test_notebook_node_view_reorder.cpp) can intercept the call
  // with a recording subclass and assert the view dispatched correctly.
  virtual void moveNodes(const QList<NodeIdentifier> &p_nodeIds,
                         const NodeIdentifier &p_targetFolderId);

  // Import/Export
  void exportNode(const NodeIdentifier &p_nodeId);

  // Import external (unindexed) node into notebook index
  void importExternalNode(const NodeIdentifier &p_nodeId);

  // Properties and info
  void showNodeProperties(const NodeIdentifier &p_nodeId);
  void copyNodePath(const NodeIdentifier &p_nodeId);
  void locateNodeInFileManager(const NodeIdentifier &p_nodeId);

  // Sorting and reload
  void sortNodes(const NodeIdentifier &p_parentId);

  // T7 (notebook-explorer-drag-reorder): persist a drag-reorder of a folder's
  // children. Validates inputs (all dragged ids share parentId; type-consistent
  // — folder names for folder children, file names for file children), then
  // dispatches to NotebookCoreService::reorderFolderChildren. Returns
  // immediately; caller relies on the nodesReordered (success) /
  // errorOccurred (failure) signals.
  //
  // p_orderedFolderIds: empty = "do not reorder folders"; non-empty =
  //   permutation of folder children of p_parentId (validated by service).
  // p_orderedFileIds:   same semantics for file children.
  // If BOTH lists are empty, the call is a hard no-op (no service call, no
  // signal).
  //
  // T9 (notebook-explorer-drag-reorder): virtual so the GUI drag-drop test
  // (tests/gui/test_notebook_node_view_reorder.cpp) can intercept the call
  // with a recording subclass and assert the view dispatched correctly.
  virtual void reorderNodes(const NodeIdentifier &p_parentId,
                            const QList<NodeIdentifier> &p_orderedFolderIds,
                            const QList<NodeIdentifier> &p_orderedFileIds);
  void reloadNode(const NodeIdentifier &p_nodeId);
  void reloadAll();

  // Pin/Tag operations
  void pinNodeToQuickAccess(const NodeIdentifier &p_nodeId);
  void manageNodeTags(const NodeIdentifier &p_nodeId);

  // Multi-node operations (list overloads)
  void openNodes(const QList<NodeIdentifier> &p_ids);
  void openNodesWithCommand(const QList<NodeIdentifier> &p_ids, const QString &p_commandTemplate);
  void duplicateNodes(const QList<NodeIdentifier> &p_ids);
  void copyNodePaths(const QList<NodeIdentifier> &p_ids);
  void pinNodesToQuickAccess(const QList<NodeIdentifier> &p_ids);
  void reloadNodes(const QList<NodeIdentifier> &p_ids);
  void markNodes(const QList<NodeIdentifier> &p_ids);

  // Check if clipboard has nodes to paste
  bool canPaste() const;

  // Share clipboard with another controller (for two-column view)
  void shareClipboardWith(NotebookNodeController *p_other);

  // Check if single-click activation is enabled from config
  bool isSingleClickActivationEnabled() const;

  // Set callback to get selected nodes (for views that don't inherit NotebookNodeView)
  using SelectedNodesCallback = std::function<QList<NodeIdentifier>()>;
  void setSelectedNodesCallback(SelectedNodesCallback p_callback);

public slots:
  // Slots for handling dialog results from View
  void handleNewNoteResult(const NodeIdentifier &p_parentId, const NodeIdentifier &p_newNodeId);
  void handleNewFolderResult(const NodeIdentifier &p_parentId, const NodeIdentifier &p_newNodeId);
  void handleRenameResult(const NodeIdentifier &p_nodeId, const QString &p_newName);
  void handleMarkResult(const NodeIdentifier &p_nodeId, const QString &p_textColor,
                        const QString &p_bgColor);
  void handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds);
  void handleImportFiles(const NodeIdentifier &p_targetFolderId, const QStringList &p_files);
  void handleImportFolder(const NodeIdentifier &p_targetFolderId, const QString &p_folderPath);

signals:
  // Signals to notify external components (e.g., BufferMgr)
  void nodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);
  void nodeAboutToMove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToRemove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToReload(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

  // GUI request signals (View should connect to these and handle GUI)
  void newNoteRequested(const NodeIdentifier &p_parentId);
  void newFolderRequested(const NodeIdentifier &p_parentId);
  void renameRequested(const NodeIdentifier &p_nodeId, const QString &p_currentName);
  void deleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void removeFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds);
  void propertiesRequested(const NodeIdentifier &p_nodeId);
  void errorOccurred(const QString &p_title, const QString &p_message);
  void infoMessage(const QString &p_title, const QString &p_message);
  void exportNodeRequested(const NodeIdentifier &p_nodeId);

  // T8 (notebook-explorer-drag-reorder): emitted when the user invokes the
  // "Sort..." action (context menu or keyboard). The View (NotebookExplorer2)
  // owns SortDialog2 and, on user confirmation, calls reorderNodes() with the
  // chosen order. Controllers MUST NOT show QDialog (per
  // src/controllers/AGENTS.md); this signal is the same architectural pattern
  // as newNoteRequested / newFolderRequested.
  void sortRequested(const NodeIdentifier &p_parentId);

  // Signal emitted when nodes are pasted (for cross-panel refresh in two-column view)
  void nodesPasted(const NodeIdentifier &p_targetFolderId);

  void markRequested(const QList<NodeIdentifier> &p_ids);

  void ignoreRequested(const NodeIdentifier &p_nodeId);

  void manageTagsRequested(const NodeIdentifier &p_nodeId);

  // T7 (notebook-explorer-drag-reorder): emitted after the service confirms
  // a successful folder-children reorder. The View should reload the folder
  // identified by p_parentId so the new order is visible.
  void nodesReordered(const NodeIdentifier &p_parentId);

public:
  // Get the parent folder path for a node
  NodeIdentifier getParentFolder(const NodeIdentifier &p_nodeId) const;

private:
  // Resolve selection following Qt right-click convention:
  // - If clicked node is in current selection, return full selection
  // - Otherwise, return clicked node only
  QList<NodeIdentifier> resolveSelection(const NodeIdentifier &p_clickedId) const;
  // Deduplicate descendant nodes - remove any node whose relativePath is a descendant
  // of another node in the list (same notebookId only). Preserves input order.
  QList<NodeIdentifier> dedupeDescendants(const QList<NodeIdentifier> &p_ids) const;
  // Check if clicked node represents a single effective selection (for action enable/disable).
  // Returns true if resolveSelection(p_clickedId).size() == 1.
  bool isSingleEffectiveSelection(const NodeIdentifier &p_clickedId) const;

  // Add actions to context menu based on node type
  void addNewActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addOpenActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addEditActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addCopyMoveActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addImportExportActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addInfoActions(QMenu *p_menu, const NodeIdentifier &p_nodeId);
  void addMiscActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addOpenWithSubmenu(QMenu *p_parentMenu, const NodeIdentifier &p_nodeId);
  void closeOrphanedBuffer(const NodeIdentifier &p_nodeId);

  // Notify BufferMgr before node operations
  void notifyBeforeNodeOperation(const NodeIdentifier &p_nodeId, const QString &p_operation);

  // Get current notebook ID from model
  QString currentNotebookId() const;

  // Build absolute path from NodeIdentifier
  QString buildAbsolutePath(const NodeIdentifier &p_nodeId) const;

  // Get NodeInfo for a node from model
  NodeInfo getNodeInfo(const NodeIdentifier &p_nodeId) const;

  // T7 (notebook-explorer-drag-reorder): translates
  // NotebookCoreService::reorderCompleted into either nodesReordered (success)
  // or errorOccurred (failure). Connected once in the constructor.
  void onReorderCompleted(const QString &p_notebookId, const QString &p_folderRelPath,
                          bool p_success, const QString &p_errorMessage);

  ServiceLocator &m_services;
  INodeListModel *m_model = nullptr;
  NotebookNodeView *m_view = nullptr;
  SelectedNodesCallback m_selectedNodesCallback;

  // Clipboard state (shared between controllers via shared pointer)
  struct ClipboardState {
    QList<NodeIdentifier> nodes;
    bool isCut = false;
  };
  QSharedPointer<ClipboardState> m_clipboard;
};

} // namespace vnotex

#endif // NOTEBOOKNODECONTROLLER_H
