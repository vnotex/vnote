#ifndef INODEEXPLORER_H
#define INODEEXPLORER_H

#include <QDataStream>
#include <QList>
#include <QSharedPointer>
#include <QWidget>

#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/nodeidentifier.h>
#include <nodeinfo.h>

class QMenu;

namespace vnotex {

class Event;
class NavigationMode;

// Serializable snapshot of a node explorer's expansion, selection, and display state.
// Plain value type — no QObject, no heap allocation.
struct NodeExplorerState {
  // Expanded folder IDs in parent-before-child order.
  QList<NodeIdentifier> expandedFolders;

  // Currently focused node.
  NodeIdentifier currentNodeId;

  // For TwoColumns mode: the folder currently displayed in the file panel.
  // Empty/default for Combined mode.
  NodeIdentifier displayRootId;
};

inline QDataStream &operator<<(QDataStream &p_stream, const NodeExplorerState &p_state) {
  p_stream << p_state.expandedFolders << p_state.currentNodeId << p_state.displayRootId;
  return p_stream;
}

inline QDataStream &operator>>(QDataStream &p_stream, NodeExplorerState &p_state) {
  p_stream >> p_state.expandedFolders >> p_state.currentNodeId >> p_state.displayRootId;
  return p_stream;
}

// INodeExplorer is the abstract base class for node explorer widgets.
// Both CombinedNodeExplorer and TwoColumnsNodeExplorer implement this interface,
// allowing NotebookExplorer2 to work with them polymorphically.
class INodeExplorer : public QWidget {
  Q_OBJECT

public:
  explicit INodeExplorer(QWidget *p_parent = nullptr) : QWidget(p_parent) {}
  ~INodeExplorer() override = default;

  // === Notebook management ===
  virtual void setNotebookId(const QString &p_notebookId) = 0;
  virtual QString getNotebookId() const = 0;

  // === Selection ===
  virtual NodeIdentifier currentNodeId() const = 0;
  virtual QList<NodeIdentifier> selectedNodeIds() const = 0;

  // === Navigation ===
  virtual void selectNode(const NodeIdentifier &p_nodeId) = 0;
  virtual void expandToNode(const NodeIdentifier &p_nodeId) = 0;
  virtual void scrollToNode(const NodeIdentifier &p_nodeId) = 0;

  // === Expand/collapse ===
  virtual void expandAll() = 0;
  virtual void collapseAll() = 0;

  // === Folder operations ===
  // Returns the currently selected folder, or parent folder if a file is selected
  virtual NodeIdentifier currentFolderId() const = 0;

  // === View order ===
  virtual void setViewOrder(ViewOrder p_order) = 0;

  // === Node info ===
  virtual NodeInfo getNodeInfo(const NodeIdentifier &p_nodeId) const = 0;

  // === Operation handlers ===
  virtual void handleRenameResult(const NodeIdentifier &p_nodeId, const QString &p_newName) = 0;
  virtual void handleDeleteConfirmed(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent) = 0;
  virtual void handleRemoveConfirmed(const QList<NodeIdentifier> &p_nodeIds) = 0;
  virtual void handleMarkResult(const NodeIdentifier &p_nodeId, const QString &p_textColor,
                                const QString &p_bgColor) = 0;

  // === Reload ===
  virtual void reloadNode(const NodeIdentifier &p_nodeId) = 0;

  // === Inline rename ===
  // Start inline editing for a node (triggers edit mode on the view)
  virtual void startInlineRename(const NodeIdentifier &p_nodeId) = 0;

  // === External files visibility ===
  virtual void setExternalNodesVisible(bool p_visible) = 0;

  // Navigation mode wrapper (override in subclasses that support navigation)
  virtual NavigationMode *getNavigationModeWrapper() const { return nullptr; }

  // === State capture/restore ===
  virtual NodeExplorerState captureState() const = 0;
  virtual void applyState(const NodeExplorerState &p_state) = 0;

signals:
  // === Activation signals ===
  void nodeActivated(const NodeIdentifier &p_nodeId, const FileOpenSettings &p_settings);

  // === Node lifecycle signals ===
  void nodeAboutToMove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToRemove(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void nodeAboutToReload(const NodeIdentifier &p_nodeId, const QSharedPointer<Event> &p_event);
  void closeFileRequested(const QString &p_filePath, const QSharedPointer<Event> &p_event);

  // === GUI request signals ===
  void newNoteRequested(const NodeIdentifier &p_parentId);
  void newFolderRequested(const NodeIdentifier &p_parentId);
  void renameRequested(const NodeIdentifier &p_nodeId, const QString &p_currentName);
  void deleteRequested(const QList<NodeIdentifier> &p_nodeIds, bool p_permanent);
  void removeFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds);
  void propertiesRequested(const NodeIdentifier &p_nodeId);
  void markRequested(const NodeIdentifier &p_nodeId);

  // === Status signals ===
  void errorOccurred(const QString &p_title, const QString &p_message);
  void infoMessage(const QString &p_title, const QString &p_message);
};

} // namespace vnotex

#endif // INODEEXPLORER_H
