#ifndef VIEWAREACONTROLLER_H
#define VIEWAREACONTROLLER_H

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <controllers/viewareaview.h>
#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/hookevents.h>
#include <core/nodeidentifier.h>

namespace vnotex {

class ServiceLocator;
class Buffer2;
class BufferService;
class IViewWindowContent;
class WorkspaceWrapper;

// Controller for the view area. Handles business logic and service interactions.
// Does NOT know about ViewArea2 or any widget type -- communicates with the view
// layer via the ViewAreaView interface (direct method calls carrying IDs).
//
// IDs used:
//   WorkspaceId -- QString, mirrors WorkspaceCoreService IDs (vxcore workspace GUIDs).
//   WindowId    -- ID (quint64), assigned by ViewArea2 when a ViewWindow2 is created.
//                  InvalidViewWindowId (0) means no window.
//
// Ownership: Created and owned by ViewArea2 (composite widget pattern).
//
// Communication flow:
//   User action  ->  ViewArea2  ->  ViewAreaController method (carries IDs, not pointers)
//   Controller decision  ->  m_view->method() (carries IDs)  ->  ViewArea2 (resolves to pointer,
//   updates GUI)
class ViewAreaController : public QObject {
  Q_OBJECT

public:
  static constexpr ID InvalidViewWindowId = 0;

  explicit ViewAreaController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~ViewAreaController() override;

  // Set the view interface. Called once by ViewArea2 during setup.
  void setView(ViewAreaView *p_view);

  // ============ Open/Close ============

  // Open a buffer in the current workspace (split).
  // Resolves file type via FileTypeCoreService, fires before-hook,
  // ensures a current workspace exists, then calls m_view->openBuffer()
  // for the view to create the ViewWindow2 via factory.
  void openBuffer(const Buffer2 &p_buffer, const FileOpenSettings &p_settings = FileOpenSettings());

  // Open a widget-based content (e.g., Settings) as a tab in the view area.
  // Creates a virtual buffer, checks for duplicate tabs across all workspaces,
  // and delegates to the view layer to create WidgetViewWindow2.
  // @p_content: ownership transferred. Deleted if a duplicate tab is found.
  void openWidgetContent(IViewWindowContent *p_content);

  // Called by the view after it has successfully created a ViewWindow2
  // in response to m_view->openBuffer().
  // @p_windowId: the ID the view assigned to the new window.
  // Fires the after-open hook and updates current window/workspace tracking.
  void onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                          const FileOpenSettings &p_settings);

  // Called by the view after a window was successfully destroyed.
  // Updates tracking state and emits windowsChanged.
  void onViewWindowClosed(ID p_windowId, const QString &p_bufferId, const QString &p_workspaceId);

  // Request to close the view window identified by p_windowId.
  // Fires the before-close hook (cancellable). On success, calls
  // m_view->closeViewWindow() so the view can call aboutToClose and destroy it.
  // @p_force: skip unsaved-changes check.
  // Returns false if a hook cancelled the close.
  bool closeViewWindow(ID p_windowId, bool p_force);

  // Close all view windows in the given workspaces.
  // Returns true if all closed (false means user cancelled).
  bool closeAll(const QVector<QString> &p_workspaceIds, bool p_force);

  // Close all buffers (visible and hidden) for app quit.
  // Phase 1: Close all visible workspace windows via closeAll().
  // Phase 2: Close hidden workspace windows via aboutToClose().
  // Returns false if user cancelled any save prompt.
  // Note: Non-transactional — if cancel happens in Phase 2, visible windows
  // from Phase 1 are already closed. This is acceptable per spec.
  bool closeAllBuffersForQuit();

  // Close multiple tabs in a workspace based on mode, relative to a reference tab index.
  // @p_workspaceId: workspace containing the tabs.
  // @p_referenceTabIndex: the right-clicked tab index (for Others/Left/Right reference).
  // @p_mode: which tabs to close (All, Others, ToTheLeft, ToTheRight).
  void closeTabs(const QString &p_workspaceId, int p_referenceTabIndex, CloseTabMode p_mode);

  // ============ Split Operations ============

  // Split the workspace p_workspaceId in the given direction.
  // Fires hooks, allocates a new workspace via WorkspaceCoreService,
  // then calls m_view->split() for the view to create the new split widget.
  void splitViewSplit(const QString &p_workspaceId, Direction p_direction);

  // Remove the split for p_workspaceId.
  // @p_keepWorkspace: if true, hide-only mode (workspace becomes hidden, split removed).
  //                   if false, full removal (workspace removed, then split removed).
  // @p_force: skip unsaved-changes check.
  // Closes all windows in it first; fires hooks; calls m_view->removeViewSplit().
  // Returns true if removed.
  bool removeViewSplit(const QString &p_workspaceId, bool p_keepWorkspace, bool p_force);

  // Maximize the split for p_workspaceId.
  void maximizeViewSplit(const QString &p_workspaceId);

  // Distribute all splits evenly.
  void distributeViewSplits();

  // Move a view window to an adjacent workspace.
  // @p_srcWorkspaceId: source workspace.
  // @p_windowId:       window to move.
  // @p_direction:      direction of movement.
  // @p_dstWorkspaceId: resolved destination workspace.
  // @p_bufferId:       buffer ID of the window being moved.
  void moveViewWindowOneSplit(const QString &p_srcWorkspaceId, ID p_windowId, Direction p_direction,
                              const QString &p_dstWorkspaceId, const QString &p_bufferId);

  // Create a new workspace with the given name and switch to it.
  void newWorkspace(const QString &p_currentWorkspaceId, const QString &p_name);

  // Rename the workspace to p_newName.
  void renameWorkspace(const QString &p_workspaceId, const QString &p_newName);

  // Generate a default workspace name like "Workspace 1", "Workspace 2", etc.
  // Finds the next available number by scanning existing workspace names.
  QString generateWorkspaceName() const;

  // Get the display name of a workspace by ID. Returns empty string if not found.
  QString getWorkspaceName(const QString &p_workspaceId) const;

  // Remove the workspace for the given split.
  // Closes its buffers one-by-one (if not in other workspaces), deletes the workspace,
  // then switches to a hidden workspace. If none available, removes the split.
  // @p_force: skip unsaved-changes check.
  // Returns true if workspace was removed, false if user cancelled.
  // Note: p_workspaceId is passed by value because switchWorkspace may modify
  // the split's workspace ID during this call.
  bool removeWorkspace(QString p_workspaceId, bool p_force);

  // Switch the given split to a different workspace.
  void switchWorkspace(const QString &p_currentWorkspaceId, const QString &p_targetWorkspaceId);

  // Update the buffer order in a workspace (e.g., after tab drag-to-reorder).
  void setBufferOrder(const QString &p_workspaceId, const QStringList &p_bufferIds);

  // ============ Current State ============

  // Get the ID of the current active window (InvalidViewWindowId if none).
  ID getCurrentWindowId() const;

  // Get the ID of the current active workspace (empty if none).
  QString getCurrentWorkspaceId() const;

  // Called by the view when a split gains focus (workspace ID of that split).
  void setCurrentViewSplit(const QString &p_workspaceId, bool p_focus);

  // Called by the view when the active tab changes within a split.
  // @p_bufferId: buffer ID of the newly active window (to update vxcore current buffer).
  void setCurrentViewWindow(ID p_windowId, const QString &p_bufferId = QString());

  // Focus the current split.
  void focus();

  // ============ Session ============

  // Whether view area operations should propagate state changes to vxcore.
  // Set to false during session restore and shutdown.
  void setShouldPropagateToCore(bool p_enabled);
  bool shouldPropagateToCore() const;

  // Restore buffers from vxcore workspace state.
  // Call after loadLayout() has created the splitter tree.
  // @p_layoutWorkspaceIds: workspace IDs present in the splitter layout.
  //   Only workspaces in this set will have their buffers restored.
  void restoreSession(const QStringList &p_layoutWorkspaceIds);

  // Save layout.
  // @p_widgetTree: pre-serialized splitter/workspace tree from ViewArea2.
  // Returns JSON containing the splitter tree geometry only.
  // Note: currentWorkspaceId is persisted by vxcore, not here.
  QJsonObject saveLayout(const QJsonObject &p_widgetTree) const;

  // Load layout: calls m_view->loadLayout() for the view to reconstruct widgets.
  void loadLayout(const QJsonObject &p_layout);

  // Subscribe to file-open hooks. Called once during initialization.
  void subscribeToHooks();

  // Force-emit currentViewWindowChanged (e.g. after session restore settles).
  void notifyCurrentViewWindowChanged();

signals:
  // ============ Notification Signals (external consumers) ============

  // Emitted when the current view window changes.
  void currentViewWindowChanged();

  // Emitted when any window/split is added/removed/changed.
  void windowsChanged();

  // Emitted when the count of splits changes.
  void viewSplitsCountChanged();

  // Emitted when a tab context menu requests to locate a node in the notebook explorer.
  void locateNodeRequested(const NodeIdentifier &p_nodeId);

private:
  // Emit currentViewWindowChanged if the active window has changed.
  void checkCurrentViewWindowChange(const QString &p_workspaceId);

  // Handle FileAfterOpen hook: open a ViewWindow2 for the newly opened buffer.
  void onFileAfterOpen(const FileOpenEvent &p_event);

  // Handle NodeAfterRename hook: update buffer paths and tab titles.
  void onNodeAfterRename(const NodeRenameEvent &p_event);

  // Handle ConfigEditorChanged hook: notify all windows.
  void onEditorConfigChanged();

  // Open a single buffer during session restore.
  // Resolves file type and calls m_view->openBuffer() for the view to create the ViewWindow2.
  void openRestoredBuffer(BufferService *p_bufferSvc, const QString &p_workspaceId,
                          const QString &p_bufferId, bool p_focus,
                          ViewWindowMode p_mode = ViewWindowMode::Read, int p_lineNumber = -1);

  ServiceLocator &m_services;
  ViewAreaView *m_view = nullptr;
  ID m_currentWindowId = InvalidViewWindowId;
  QString m_currentWorkspaceId;

  // Whether view area operations should propagate state changes to vxcore.
  // false during session restore (rebuilding UI from vxcore state).
  // true during normal operation (user actions update vxcore).
  bool m_shouldPropagateToCore = true;

  // When true, onViewWindowClosed() will not auto-remove empty workspaces.
  // Set during bulk close operations (removeWorkspace, closeAll) where
  // the caller manages workspace lifecycle explicitly.
  bool m_suppressAutoRemove = false;

  // Owns all WorkspaceWrapper instances. Each workspace known to the controller
  // has an entry here. Hidden workspaces cache their ViewWindows in the wrapper.
  QMap<QString, WorkspaceWrapper *> m_workspaces;
};

} // namespace vnotex

#endif // VIEWAREACONTROLLER_H
