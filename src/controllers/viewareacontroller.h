#ifndef VIEWAREACONTROLLER_H
#define VIEWAREACONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <core/fileopensettings.h>
#include <core/global.h>

namespace vnotex {

class ServiceLocator;
class Buffer2;

// Controller for the view area. Handles business logic and service interactions.
// Does NOT know about ViewArea2 or any widget type -- communicates with the view
// layer purely via signals carrying IDs.
//
// IDs used:
//   WorkspaceId -- QString, mirrors WorkspaceCoreService IDs (vxcore workspace GUIDs).
//   WindowId    -- ID (quint64), assigned by ViewArea2 when a ViewWindow2 is created.
//                  InvalidViewWindowId (0) means no window.
//
// Ownership: Created and owned by ViewArea2 (composite widget pattern).
//
// Signal flow:
//   User action  ->  ViewArea2  ->  ViewAreaController slot (carries IDs, not pointers)
//   Controller decision  ->  signal (carries IDs)  ->  ViewArea2 (resolves to pointer, updates GUI)
class ViewAreaController : public QObject {
  Q_OBJECT

public:
  static constexpr ID InvalidViewWindowId = 0;

  explicit ViewAreaController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~ViewAreaController() override;

  // ============ Open/Close ============

  // Open a buffer in the current workspace (split).
  // Resolves file type via FileTypeCoreService, fires before-hook,
  // ensures a current workspace exists, then emits openBufferRequested
  // for the view to create the ViewWindow2 via factory.
  void openBuffer(const Buffer2 &p_buffer,
                  const FileOpenSettings &p_settings = FileOpenSettings());

  // Called by the view after it has successfully created a ViewWindow2
  // in response to openBufferRequested.
  // @p_windowId: the ID the view assigned to the new window.
  // Fires the after-open hook and updates current window/workspace tracking.
  void onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                          const FileOpenSettings &p_settings);

  // Called by the view after a window was successfully destroyed.
  // Updates tracking state and emits windowsChanged.
  void onViewWindowClosed(ID p_windowId, const QString &p_bufferId,
                          const QString &p_workspaceId);

  // Request to close the view window identified by p_windowId.
  // Fires the before-close hook (cancellable). On success, emits
  // closeViewWindowRequested so the view can call aboutToClose and destroy it.
  // @p_force: skip unsaved-changes check.
  // Returns false if a hook cancelled the close.
  bool closeViewWindow(ID p_windowId, bool p_force);

  // Close all view windows in the given workspaces.
  // Returns true if all closed (false means user cancelled).
  bool closeAll(const QVector<QString> &p_workspaceIds, bool p_force);

  // ============ Split Operations ============

  // Split the workspace p_workspaceId in the given direction.
  // Fires hooks, allocates a new workspace via WorkspaceCoreService,
  // then emits splitRequested for the view to create the new split widget.
  void splitViewSplit(const QString &p_workspaceId, Direction p_direction);

  // Remove the split for p_workspaceId.
  // Closes all windows in it first; fires hooks; emits removeViewSplitRequested.
  // @p_totalSplitCount: current number of splits (cannot remove last one).
  // Returns true if removed.
  bool removeViewSplit(const QString &p_workspaceId, bool p_force, int p_totalSplitCount);

  // Maximize the split for p_workspaceId.
  void maximizeViewSplit(const QString &p_workspaceId);

  // Distribute all splits evenly.
  void distributeViewSplits();

  // Move a view window to an adjacent workspace.
  // @p_srcWorkspaceId: source workspace.
  // @p_windowId:       window to move.
  // @p_direction:      direction of movement.
  // @p_dstWorkspaceId: resolved destination workspace.
  void moveViewWindowOneSplit(const QString &p_srcWorkspaceId, ID p_windowId,
                              Direction p_direction, const QString &p_dstWorkspaceId);

  // ============ Current State ============

  // Get the ID of the current active window (InvalidViewWindowId if none).
  ID getCurrentWindowId() const;

  // Get the ID of the current active workspace (empty if none).
  QString getCurrentWorkspaceId() const;

  // Called by the view when a split gains focus (workspace ID of that split).
  void setCurrentViewSplit(const QString &p_workspaceId, bool p_focus);

  // Called by the view when the active tab changes within a split.
  void setCurrentViewWindow(ID p_windowId);

  // Focus the current split.
  void focus();

  // ============ Session ============

  // Save layout.
  // @p_widgetTree: pre-serialized splitter/workspace tree from ViewArea2.
  // Returns combined JSON (adds currentWorkspaceId from WorkspaceCoreService).
  QJsonObject saveLayout(const QJsonObject &p_widgetTree) const;

  // Load layout: emits loadLayoutRequested for the view to reconstruct widgets.
  void loadLayout(const QJsonObject &p_layout);

  // Subscribe to file-open hooks. Called once during initialization.
  void subscribeToHooks();

signals:
  // ============ Signals to View (ViewArea2 connects to these) ============

  // Request to add the first view split (when area is empty).
  void addFirstViewSplitRequested(const QString &p_workspaceId);

  // Request to split an existing workspace.
  // @p_workspaceId:    the workspace to split adjacent to.
  // @p_direction:      direction of the new split.
  // @p_newWorkspaceId: workspace ID for the new split.
  void splitRequested(const QString &p_workspaceId, Direction p_direction,
                      const QString &p_newWorkspaceId);

  // Request to remove the split for the given workspace from the layout.
  void removeViewSplitRequested(const QString &p_workspaceId);

  // Request to maximize the split for the given workspace.
  void maximizeViewSplitRequested(const QString &p_workspaceId);

  // Request to distribute all splits evenly.
  void distributeViewSplitsRequested();

  // Request to open a buffer -- the view should create a ViewWindow2 via factory.
  // On success, the view calls onViewWindowOpened() with the assigned window ID.
  void openBufferRequested(const Buffer2 &p_buffer, const QString &p_fileType,
                           const QString &p_workspaceId, const FileOpenSettings &p_settings);

  // Request to close the view window p_windowId.
  // View calls aboutToClose, destroys it, then calls onViewWindowClosed.
  void closeViewWindowRequested(ID p_windowId, bool p_force);

  // Request to set the current active workspace.
  void setCurrentViewSplitRequested(const QString &p_workspaceId, bool p_focus);

  // Request to move a view window from one workspace to another.
  void moveViewWindowToSplitRequested(ID p_windowId, const QString &p_srcWorkspaceId,
                                      const QString &p_dstWorkspaceId);

  // Request to focus the split for the given workspace.
  void focusViewSplitRequested(const QString &p_workspaceId);

  // Emitted when the current view window changes.
  void currentViewWindowChanged();

  // Emitted when any window/split is added/removed/changed.
  void windowsChanged();

  // Emitted when the count of splits changes.
  void viewSplitsCountChanged();

  // Request to load a full layout tree from JSON.
  void loadLayoutRequested(const QJsonObject &p_layout);

private:
  // Emit currentViewWindowChanged if the active window has changed.
  void checkCurrentViewWindowChange(const QString &p_workspaceId);

  // Handle FileAfterOpen hook: open a ViewWindow2 for the newly opened buffer.
  void onFileAfterOpen(const QVariantMap &p_args);

  ServiceLocator &m_services;
  ID m_currentWindowId = InvalidViewWindowId;
  QString m_currentWorkspaceId;
};

} // namespace vnotex

#endif // VIEWAREACONTROLLER_H