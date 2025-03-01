#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QList>
#include <QListView>
#include <QSharedPointer>

#include <nodeinfo.h>

namespace vnotex {

class NotebookNodeController;
struct FileOpenParameters;

// A QListView subclass for displaying files in the two-column explorer.
// Simpler than NotebookNodeView - no tree hierarchy, just a flat list.
class FileListView : public QListView {
  Q_OBJECT

public:
  explicit FileListView(QWidget *p_parent = nullptr);
  ~FileListView() override;

  // Set the controller that handles user actions
  void setController(NotebookNodeController *p_controller);
  NotebookNodeController *controller() const;

  // Selection helpers (using NodeIdentifier instead of Node*)
  NodeIdentifier currentNodeId() const;
  QList<NodeIdentifier> selectedNodeIds() const;

  // Select and navigate to a node
  void selectNode(const NodeIdentifier &p_nodeId);
  void scrollToNode(const NodeIdentifier &p_nodeId);

signals:
  // Emitted when a node is activated (double-click or Enter)
  void nodeActivated(const NodeIdentifier &p_nodeId,
                     const QSharedPointer<FileOpenParameters> &p_paras);

  // Emitted when selection changes
  void nodeSelectionChanged(const QList<NodeIdentifier> &p_nodeIds);

  // Emitted when context menu is requested
  void contextMenuRequested(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

protected:
  // Event handlers
  void mouseDoubleClickEvent(QMouseEvent *p_event) override;
  void keyPressEvent(QKeyEvent *p_event) override;
  void contextMenuEvent(QContextMenuEvent *p_event) override;

  // Drag and drop
  void dragEnterEvent(QDragEnterEvent *p_event) override;
  void dragMoveEvent(QDragMoveEvent *p_event) override;
  void dropEvent(QDropEvent *p_event) override;

  // Selection change notification
  void selectionChanged(const QItemSelection &p_selected,
                        const QItemSelection &p_deselected) override;

private slots:
  void onItemActivated(const QModelIndex &p_index);

private:
  // Get NodeIdentifier from model index
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const;

  // Get NodeInfo from model index (for isFolder checks)
  NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const;

  // Get model index from NodeIdentifier
  QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const;

  // Setup initial configuration
  void setupView();

  NotebookNodeController *m_controller = nullptr;
};

} // namespace vnotex

#endif // FILELISTVIEW_H
