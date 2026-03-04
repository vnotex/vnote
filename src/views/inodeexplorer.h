#ifndef INODEEXPLORER_H
#define INODEEXPLORER_H

#include <QList>
#include <QSharedPointer>
#include <QWidget>

#include <core/global.h>
#include <nodeinfo.h>

class QMenu;

namespace vnotex {

struct FileOpenParameters;
class Event;

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

  // === Reload ===
  virtual void reloadNode(const NodeIdentifier &p_nodeId) = 0;

  // === Inline rename ===
  // Start inline editing for a node (triggers edit mode on the view)
  virtual void startInlineRename(const NodeIdentifier &p_nodeId) = 0;

  // === External files visibility ===
  virtual void setExternalNodesVisible(bool p_visible) = 0;

signals:
  // === Activation signals ===
  void nodeActivated(const NodeIdentifier &p_nodeId,
                     const QSharedPointer<FileOpenParameters> &p_paras);

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

  // === Status signals ===
  void errorOccurred(const QString &p_title, const QString &p_message);
  void infoMessage(const QString &p_title, const QString &p_message);
};

} // namespace vnotex

#endif // INODEEXPLORER_H
