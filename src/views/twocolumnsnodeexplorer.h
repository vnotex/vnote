#ifndef TWOCOLUMNSNODEEXPLORER_H
#define TWOCOLUMNSNODEEXPLORER_H

#include <QList>
#include <QSharedPointer>
#include <QWidget>

#include <core/global.h>
#include <nodeinfo.h>

class QSplitter;

namespace vnotex {

class NotebookNodeModel;
class NotebookNodeProxyModel;
class NotebookNodeView;
class NotebookNodeDelegate;
class NotebookNodeController;
class ServiceLocator;
struct FileOpenParameters;

// TwoColumnsNodeExplorer encapsulates a two-panel view:
// - Left panel: folder tree (shows only folders)
// - Right panel: file list (shows files in selected folder)
//
// It provides a unified interface similar to NotebookNodeView for easy
// integration with NotebookExplorer2.
class TwoColumnsNodeExplorer : public QWidget {
  Q_OBJECT

public:
  explicit TwoColumnsNodeExplorer(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~TwoColumnsNodeExplorer() override;

  // Set the notebook to display
  void setNotebookId(const QString &p_notebookId);
  QString getNotebookId() const;

  // Selection helpers (unified interface)
  NodeIdentifier currentNodeId() const;
  QList<NodeIdentifier> selectedNodeIds() const;

  // Navigation (unified interface)
  void selectNode(const NodeIdentifier &p_nodeId);
  void expandToNode(const NodeIdentifier &p_nodeId);
  void scrollToNode(const NodeIdentifier &p_nodeId);

  // Expand/collapse helpers
  void expandAll();
  void collapseAll();

  // Get the currently selected folder (for new note/folder operations)
  NodeIdentifier currentFolderId() const;

  // View order
  void setViewOrder(ViewOrder p_order);

  // Access to controllers for signal connections
  NotebookNodeController *folderController() const;
  NotebookNodeController *fileController() const;

  // Reload a node in the appropriate model
  void reloadNode(const NodeIdentifier &p_nodeId, bool p_isFolder);

  // Splitter state for session save/restore
  QByteArray saveSplitterState() const;
  void restoreSplitterState(const QByteArray &p_data);

signals:
  // Emitted when a node is activated (double-click or Enter)
  void nodeActivated(const NodeIdentifier &p_nodeId,
                     const QSharedPointer<FileOpenParameters> &p_paras);

  // Emitted when context menu is requested
  void contextMenuRequested(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos,
                            bool p_isFromFileView);

private slots:
  void onFolderSelectionChanged(const QList<NodeIdentifier> &p_nodeIds);
  void onFolderContextMenu(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);
  void onFileContextMenu(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

private:
  void setupUI();

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
  NotebookNodeView *m_fileView = nullptr;
  NotebookNodeDelegate *m_fileDelegate = nullptr;
  NotebookNodeController *m_fileController = nullptr;

  // Layout
  QSplitter *m_splitter = nullptr;
};

} // namespace vnotex

#endif // TWOCOLUMNSNODEEXPLORER_H
