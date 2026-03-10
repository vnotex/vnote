#ifndef VIEWAREACONTROLLER_H
#define VIEWAREACONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <core/global.h>

namespace vnotex {

class ServiceLocator;
class ViewSplit2;
class ViewWindow2;
class Buffer2;
class ViewWindowFactory;
class WorkspaceCoreService;
class BufferService;
class HookManager;

// Controller for the view area. Handles business logic and service interactions.
// Does NOT know about ViewArea2 — communicates with the view layer via signals only.
//
// Ownership: Created and owned by ViewArea2 (composite widget pattern).
//
// Signal flow:
//   User action (ViewSplit2 signal) → ViewArea2 → ViewAreaController (slot)
//   Controller decision → signal → ViewArea2 (handles GUI)
class ViewAreaController : public QObject {
  Q_OBJECT

public:
  explicit ViewAreaController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~ViewAreaController() override;

  // ============ Open/Close ============

  // Open a buffer in the current split. Creates a ViewWindow2 via factory.
  // Emits openBufferRequested with the created window.
  void openBuffer(const Buffer2 &p_buffer, const QString &p_fileType);

  // Close a view window. If p_force, skip unsaved-changes check.
  // Returns true if closed.
  bool closeViewWindow(ViewWindow2 *p_win, bool p_force);

  // Close all view windows in the given splits.
  // Returns true if all closed (false means user cancelled).
  bool closeAll(const QVector<ViewSplit2 *> &p_splits, bool p_force);

  // ============ Split Operations ============

  // Split the given split in the given direction.
  void splitViewSplit(ViewSplit2 *p_split, Direction p_direction);

  // Remove a split. If it has windows, close them first.
  // Returns true if removed.
  bool removeViewSplit(ViewSplit2 *p_split, bool p_force, int p_totalSplitCount);

  // Maximize the given split.
  void maximizeViewSplit(ViewSplit2 *p_split);

  // Distribute all splits evenly.
  void distributeViewSplits();

  // Move a view window to the adjacent split in the given direction.
  void moveViewWindowOneSplit(ViewSplit2 *p_split, ViewWindow2 *p_win, Direction p_direction,
                              ViewSplit2 *p_targetSplit);

  // ============ Current State ============

  // Get the current active view window.
  ViewWindow2 *getCurrentViewWindow() const;

  // Set the current view split (called by ViewArea2 when split focus changes).
  void setCurrentViewSplit(ViewSplit2 *p_split, bool p_focus);

  // Set the current view window (called by ViewArea2 when tab changes).
  void setCurrentViewWindow(ViewWindow2 *p_win);

  // Focus the current split.
  void focus();

  // ============ Session ============

  // Save layout: controller serializes workspace-related data.
  // Needs widget tree info from the view — ViewArea2 calls this with its own data.
  QJsonObject saveLayout(const QVector<ViewSplit2 *> &p_splits,
                         const std::function<QJsonObject(const QWidget *)> &p_serializeWidget) const;

  // Load layout: controller deserializes and emits signals for the view to create widgets.
  void loadLayout(const QJsonObject &p_layout);

  // Connect a newly created ViewSplit2's signals to this controller.
  void connectSplitSignals(ViewSplit2 *p_split);

  // Subscribe to file-open hooks. Called once during initialization.
  void subscribeToHooks();

signals:
  // ============ Signals to View (ViewArea2 connects to these) ============

  // Request to add the first view split (when area is empty).
  // @p_workspaceId: workspace ID for the new split.
  void addFirstViewSplitRequested(const QString &p_workspaceId);

  // Request to split an existing split.
  // @p_split: the split to split adjacent to.
  // @p_direction: direction of the new split.
  // @p_workspaceId: workspace ID for the new split.
  void splitRequested(ViewSplit2 *p_split, Direction p_direction, const QString &p_workspaceId);

  // Request to remove a view split from the layout.
  void removeViewSplitRequested(ViewSplit2 *p_split);

  // Request to maximize a view split.
  void maximizeViewSplitRequested(ViewSplit2 *p_split);

  // Request to distribute all splits evenly.
  void distributeViewSplitsRequested();

  // A ViewWindow2 has been created and needs to be added to a split.
  // @p_win: the created ViewWindow2.
  // @p_split: the target split to add it to.
  void viewWindowCreated(ViewWindow2 *p_win, ViewSplit2 *p_split);

  // Request to set the current active split.
  void setCurrentViewSplitRequested(ViewSplit2 *p_split, bool p_focus);

  // Request to move a view window from one split to another.
  // @p_sourceSplit: the split the window is currently in.
  // @p_win: the window to move.
  // @p_targetSplit: the split to move the window to.
  void moveViewWindowToSplit(ViewSplit2 *p_sourceSplit, ViewWindow2 *p_win,
                             ViewSplit2 *p_targetSplit);

  // Request to focus a split.
  void focusViewSplitRequested(ViewSplit2 *p_split);

  // Emitted when the current view window changes.
  void currentViewWindowChanged();

  // Emitted when any window/split is added/removed/changed.
  void windowsChanged();

  // Emitted when the count of splits changes.
  void viewSplitsCountChanged();

  // ============ Session signals ============

  // Request to create a view split during layout loading.
  // @p_workspaceId: workspace ID for the new split.
  // The view should create a ViewSplit2 and call connectSplitSignals.
  void loadLayoutCreateSplitRequested(const QString &p_workspaceId);

  // Request to load a full layout tree from JSON.
  void loadLayoutRequested(const QJsonObject &p_layout);

private:
  // Check if current view window has changed and emit signal.
  void checkCurrentViewWindowChange(ViewSplit2 *p_activeSplit);

  // Handle FileAfterOpen hook: create a ViewWindow2 for the newly opened buffer.
  void onFileAfterOpen(const QVariantMap &p_args);

  ServiceLocator &m_services;
  ViewWindow2 *m_currentWindow = nullptr;
  ViewSplit2 *m_currentSplit = nullptr;
};

} // namespace vnotex

#endif // VIEWAREACONTROLLER_H
