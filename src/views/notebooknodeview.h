#ifndef NOTEBOOKNODEVIEW_H
#define NOTEBOOKNODEVIEW_H

#include <QList>
#include <QSharedPointer>
#include <QTreeView>

namespace vnotex {

class Node;
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

  // Selection helpers
  Node *currentNode() const;
  QList<Node *> selectedNodes() const;

  // Select and navigate to a node
  void selectNode(Node *p_node);
  void expandToNode(Node *p_node);
  void scrollToNode(Node *p_node);

  // Expand/collapse helpers
  void expandAll();
  void collapseAll();

signals:
  // Emitted when a node is activated (double-click or Enter)
  void nodeActivated(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

  // Emitted when selection changes
  void nodeSelectionChanged(const QList<Node *> &p_nodes);

  // Emitted when context menu is requested
  void contextMenuRequested(Node *p_node, const QPoint &p_globalPos);

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
  // Get Node from model index
  Node *nodeFromIndex(const QModelIndex &p_index) const;

  // Get model index from Node
  QModelIndex indexFromNode(Node *p_node) const;

  // Setup initial configuration
  void setupView();

  NotebookNodeController *m_controller = nullptr;
};

} // namespace vnotex

#endif // NOTEBOOKNODEVIEW_H
