#ifndef VIEWAREA2_H
#define VIEWAREA2_H

#include <QJsonObject>
#include <QWidget>
#include <QVector>

#include <core/global.h>

class QSplitter;
class QVBoxLayout;

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;
class ViewAreaController;

// Composite widget for the view area. Owns both the GUI (splitter tree + ViewSplit2s)
// and the ViewAreaController (business logic). Follows the same composite pattern
// as NotebookExplorer2.
//
// The controller communicates with ViewArea2 via signals only — the controller
// has zero knowledge of ViewArea2.
//
// Layout structure:
//   ViewArea2 (QWidget)
//     └── QVBoxLayout
//           └── top widget: either a single ViewSplit2 or a QSplitter tree
//
// Signal flow:
//   User action (ViewSplit2 signal) → ViewArea2 → ViewAreaController (slot)
//   Controller decision → signal → ViewArea2 (handles GUI)
//
// The splitter tree follows Vim split conventions:
//   - "Vertical split" (Left/Right) → Qt::Horizontal orientation
//   - "Horizontal split" (Up/Down) → Qt::Vertical orientation
class ViewArea2 : public QWidget {
  Q_OBJECT
public:
  explicit ViewArea2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ViewArea2() override;

  // Access the controller.
  ViewAreaController *getController() const;

  // ============ Split Factory ============

  // Create a ViewSplit2 with the given workspace ID. Does NOT add it to the layout.
  // Caller must add it to a splitter or call addFirstViewSplit/splitAt.
  ViewSplit2 *createViewSplit(const QString &p_workspaceId);

  // Create a QSplitter. Does NOT add it to the layout.
  QSplitter *createSplitter(Qt::Orientation p_orientation);

  // ============ Layout Management ============

  // Add the first view split (when area is empty). Sets it as the top widget.
  // Returns the created ViewSplit2.
  ViewSplit2 *addFirstViewSplit(const QString &p_workspaceId);

  // Split an existing ViewSplit2 in the given direction.
  // Creates a new ViewSplit2 and inserts it adjacent to p_split.
  // Returns the new ViewSplit2.
  ViewSplit2 *splitAt(ViewSplit2 *p_split, Direction p_direction, const QString &p_workspaceId);

  // Remove a ViewSplit2 from the layout. Handles splitter unwrapping.
  // Does NOT delete the split — caller is responsible.
  void removeViewSplit(ViewSplit2 *p_split);

  // Maximize a split by giving it maximum space and minimizing siblings.
  void maximizeViewSplit(ViewSplit2 *p_split);

  // Distribute all splits evenly.
  void distributeViewSplits();

  // Find the adjacent split in the given direction from p_split.
  // Returns nullptr if no adjacent split exists.
  ViewSplit2 *findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const;

  // ============ State ============

  // Get all ViewSplit2 instances (in no particular order).
  QVector<ViewSplit2 *> getAllViewSplits() const;

  // Get the current active ViewSplit2 (the one with isActive() == true).
  ViewSplit2 *getCurrentViewSplit() const;

  // Get the count of ViewSplit2 instances.
  int getViewSplitCount() const;

  // ============ Top Widget ============

  // Get the top-level widget (either a ViewSplit2 or QSplitter, or nullptr).
  QWidget *getTopWidget() const;

  // Set the top-level widget (replaces existing). Takes ownership.
  void setTopWidget(QWidget *p_widget);

  // Save the view area layout as JSON.
  // Coordinates between the view's widget tree and the controller's workspace metadata.
  QJsonObject saveLayout() const;

  // Load a previously saved layout from JSON.
  // Delegates to the controller, which emits signals back to reconstruct the view.
  void loadLayout(const QJsonObject &p_layout);

private slots:
  // ============ Controller signal handlers ============

  // Handle controller's addFirstViewSplitRequested signal.
  void onAddFirstViewSplitRequested(const QString &p_workspaceId);

  // Handle controller's splitRequested signal.
  void onSplitRequested(ViewSplit2 *p_split, Direction p_direction, const QString &p_workspaceId);

  // Handle controller's removeViewSplitRequested signal.
  void onRemoveViewSplitRequested(ViewSplit2 *p_split);

  // Handle controller's maximizeViewSplitRequested signal.
  void onMaximizeViewSplitRequested(ViewSplit2 *p_split);

  // Handle controller's distributeViewSplitsRequested signal.
  void onDistributeViewSplitsRequested();

  // Handle controller's viewWindowCreated signal.
  void onViewWindowCreated(ViewWindow2 *p_win, ViewSplit2 *p_split);

  // Handle controller's setCurrentViewSplitRequested signal.
  void onSetCurrentViewSplitRequested(ViewSplit2 *p_split, bool p_focus);

  // Handle controller's focusViewSplitRequested signal.
  void onFocusViewSplitRequested(ViewSplit2 *p_split);

  // Handle controller's moveViewWindowToSplit signal.
  void onMoveViewWindowToSplit(ViewSplit2 *p_sourceSplit, ViewWindow2 *p_win,
                               ViewSplit2 *p_targetSplit);

  // Handle controller's loadLayoutRequested signal.
  void onLoadLayoutRequested(const QJsonObject &p_layout);

  // ============ ViewSplit2 signal handlers (forwarded to controller) ============

  // Handle ViewSplit2's moveViewWindowOneSplitRequested.
  // Resolves the target split, then calls controller.
  void onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                         Direction p_direction);

  // Handle ViewSplit2's removeSplitRequested.
  // Passes the actual split count to the controller.
  void onRemoveSplitRequested(ViewSplit2 *p_split);

private:
  // Setup the controller and wire its signals.
  void setupController();

  // Collect all ViewSplit2 descendants of a widget (recursive).
  void collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const;

  // Recursively distribute splitter sizes evenly.
  static void distributeSplitter(QSplitter *p_splitter);

  // Find parent splitter of a widget. Returns nullptr if widget is top-level.
  QSplitter *findParentSplitter(QWidget *p_widget) const;

  // Unwrap single-child splitters after removal.
  void unwrapSingleChildSplitter(QSplitter *p_splitter);

  // Descend into a widget tree to find the first ViewSplit2.
  static ViewSplit2 *findFirstViewSplit(QWidget *p_widget);

  // Descend into a widget tree to find the last ViewSplit2.
  static ViewSplit2 *findLastViewSplit(QWidget *p_widget);

  // Serialize a widget tree to JSON (for saveLayout).
  static QJsonObject serializeWidget(const QWidget *p_widget);

  // Deserialize a splitter node from JSON (for loadLayout).
  void deserializeSplitterNode(const QJsonObject &p_node, QSplitter *p_splitter);

  ServiceLocator &m_services;
  QVBoxLayout *m_mainLayout = nullptr;
  QWidget *m_topWidget = nullptr;

  // Controller for business logic — owned by this widget.
  ViewAreaController *m_controller = nullptr;
};

} // namespace vnotex

#endif // VIEWAREA2_H
