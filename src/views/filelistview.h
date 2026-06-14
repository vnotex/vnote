#ifndef FILELISTVIEW_H
#define FILELISTVIEW_H

#include <QAbstractItemView>
#include <QList>
#include <QListView>
#include <QMetaObject>
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
  // Emitted when selection changes
  void nodeSelectionChanged(const QList<NodeIdentifier> &p_nodeIds);

  // Emitted when context menu is requested
  void contextMenuRequested(const NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

protected:
  // Event handlers
  void mousePressEvent(QMouseEvent *p_event) override;
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

  // T10 test seam: returns the QObject originating this drop event. Tests may
  // override to inject a synthetic source so cross-view drag detection can be
  // exercised without spinning up a real QDragManager session. Production
  // implementation forwards to QDropEvent::source(). MUST stay protected so
  // subclasses (and only subclasses) can override.
  virtual QObject *resolveDropSource(QDropEvent *p_event) const;

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

  // Check if single-click activation is enabled from config
  bool isSingleClickActivationEnabled() const;

  // T10 (drag-reorder): true when the proxy is in OrderedByConfiguration mode
  // AND no name filter is active. Drag-reorder requires both conditions so
  // the displayed order matches the on-disk order vxcore will be asked to
  // mutate. Returns false if the model is not a NotebookNodeProxyModel.
  bool isReorderAllowed() const;

  // T10 (drag-reorder): current file order as displayed by the view. Iterates
  // every top-level proxy row and collects NodeIdentifiers. Order matches
  // what the user sees, so it is the correct starting point for computing
  // the new permutation after a drop.
  QList<NodeIdentifier> currentFileOrderInView() const;

  // T10 (drag-reorder): compute the new ordered file id list after dropping
  // p_dragged at p_pos relative to p_anchor. Removes p_dragged from their
  // current positions, then re-inserts them as a contiguous block at the
  // anchor (Above = before anchor, Below = after anchor). If the anchor is
  // itself in p_dragged or otherwise unresolvable, dragged items are
  // appended at the end.
  QList<NodeIdentifier>
  computeReorderedFileIds(const QList<NodeIdentifier> &p_dragged, const NodeIdentifier &p_anchor,
                          QAbstractItemView::DropIndicatorPosition p_pos) const;

  NotebookNodeController *m_controller = nullptr;

  // T10 (drag-reorder): tracks the per-controller connection set up in
  // setController() so we can disconnect cleanly when the controller is
  // reassigned. Without this, repeated setController calls would accumulate
  // lambda connections that each call reloadNode on every reorder.
  QMetaObject::Connection m_reorderConnection;
};

} // namespace vnotex

#endif // FILELISTVIEW_H
