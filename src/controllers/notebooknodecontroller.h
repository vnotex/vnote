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
  void moveNodes(const QList<NodeIdentifier> &p_nodeIds, const NodeIdentifier &p_targetFolderId);

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
  void reloadNode(const NodeIdentifier &p_nodeId);
  void reloadAll();

  // Pin/Tag operations
  void pinNodeToQuickAccess(const NodeIdentifier &p_nodeId);
  void manageNodeTags(const NodeIdentifier &p_nodeId);

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

  // Signal emitted when nodes are pasted (for cross-panel refresh in two-column view)
  void nodesPasted(const NodeIdentifier &p_targetFolderId);

  void markRequested(const NodeIdentifier &p_nodeId);

  void ignoreRequested(const NodeIdentifier &p_nodeId);

public:
  // Get the parent folder path for a node
  NodeIdentifier getParentFolder(const NodeIdentifier &p_nodeId) const;

private:
  // Add actions to context menu based on node type
  void addNewActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addOpenActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addEditActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addCopyMoveActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addImportExportActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);
  void addInfoActions(QMenu *p_menu, const NodeIdentifier &p_nodeId);
  void addMiscActions(QMenu *p_menu, const NodeIdentifier &p_nodeId, bool p_isFolder);

  // Notify BufferMgr before node operations
  void notifyBeforeNodeOperation(const NodeIdentifier &p_nodeId, const QString &p_operation);

  // Get current notebook ID from model
  QString currentNotebookId() const;

  // Build absolute path from NodeIdentifier
  QString buildAbsolutePath(const NodeIdentifier &p_nodeId) const;

  // Get NodeInfo for a node from model
  NodeInfo getNodeInfo(const NodeIdentifier &p_nodeId) const;

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
