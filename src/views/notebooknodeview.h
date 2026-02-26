#ifndef NOTEBOOKNODEVIEW_H
#define NOTEBOOKNODEVIEW_H

#include <QList>
#include <QSharedPointer>
#include <QTreeView>

#include <nodeinfo.h>

namespace vnotex {


class NotebookNodeController;
struct FileOpenParameters;

// A QTreeView subclass for displaying notebook nodes.
// Handles selection, navigation, and forwards events to controller.
class NotebookNodeView : public QTreeView {
  Q_OBJECT

public:
  explicit NotebookNodeView(QWidget *p_parent = nullptr);
  ~NotebookNodeView() override;

  // Set the controller that handles user actions
  void setController(NotebookNodeController *p_controller);
  NotebookNodeController *controller() const;

  // Selection helpers (using NodeIdentifier instead of Node*)
  NodeIdentifier currentNodeId() const;
  QList<NodeIdentifier> selectedNodeIds() const;

  // Select and navigate to a node
  void selectNode(const NodeIdentifier &p_nodeId);
  void expandToNode(const NodeIdentifier &p_nodeId);
  void scrollToNode(const NodeIdentifier &p_nodeId);

  // Expand/collapse helpers
  void expandAll();
  void collapseAll();

signals:
  // Emitted when a node is activated (double-click or Enter)
  void nodeActivated(const NodeIdentifier &p_nodeId, const QSharedPointer<FileOpenParameters> &p_paras);

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
  void onItemExpanded(const QModelIndex &p_index);

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

#endif // NOTEBOOKNODEVIEW_H
