#ifndef VIEWAREACONTROLLER_H
#define VIEWAREACONTROLLER_H

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include <controllers/viewareaview.h>
#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/nodeidentifier.h>
#include <unitedentry/iviewwindownavigator.h>

class QTimer;

namespace vnotex {

// Record of a closed tab, used for "Open Last Closed File" (Ctrl+Shift+T).
struct ClosedTabRecord {
  NodeIdentifier nodeId;
  ViewWindowMode mode = ViewWindowMode::Read;
  int cursorPosition = -1;
};

class ServiceLocator;
class Buffer2;
class BufferService;
class HookContext;
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
class ViewAreaController : public QObject, public IViewWindowNavigator {
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
  void openWidgetContent(IViewWindowContent *p_content, const QStringList &p_pathSegments = {},
                         const QString &p_fragment = QString());

  // Called by the view after it has successfully created a ViewWindow2
  // in response to m_view->openBuffer().
  // @p_windowId: the ID the view assigned to the new window.
  // @p_detachedWorkspaceId: when non-empty, the buffer was opened into a
  //   detached (--detached-view) workspace; register it there and do NOT retarget
  //   the main window's current window/split. When empty, normal behavior using
  //   the current workspace.
  // Fires the after-open hook and updates current window/workspace tracking.
  void onViewWindowOpened(ID p_windowId, const Buffer2 &p_buffer,
                          const FileOpenSettings &p_settings,
                          const QString &p_detachedWorkspaceId = QString());

  // Called by the view after a window was successfully destroyed.
  // Updates tracking state, records closed tab for reopen, and emits windowsChanged.
  void onViewWindowClosed(ID p_windowId, const QString &p_bufferId, const QString &p_workspaceId,
                          const ClosedTabRecord &p_closedTab = ClosedTabRecord());

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

  // Reopen the most recently closed file tab.
  // Pops the top entry from the closed-tab stack and opens it via BufferService.
  // Does nothing if the stack is empty.
  void openLastClosedFile();

  // ============ Split Operations ============

  // Split the workspace p_workspaceId in the given direction.
  // Fires hooks, allocates a new workspace via WorkspaceCoreService,
  // then calls m_view->split() for the view to create the new split widget.
  void splitViewSplit(const QString &p_workspaceId, Direction p_direction,
                      bool p_openCurrentBuffer = true);

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

  // Detach a view window into a new top-level DetachedWindow. Creates a fresh
  // vxcore workspace to host it, registers the buffer in the new workspace
  // before removing it from the source (to avoid vxcore orphan auto-close), then
  // asks the view to build the DetachedWindow and transfer the window. Fires the
  // ViewSplit create + ViewWindow move hooks; returns false (rolling back the new
  // workspace) if a hook cancels or the transfer fails.
  bool detachViewWindow(const QString &p_srcWorkspaceId, ID p_windowId, const QString &p_bufferId);

  // Tear down a detached workspace after its windows have been moved back into a
  // main split (reattach) or after its last tab was closed. Transfers buffer
  // registration from the detached workspace to p_targetWorkspaceId (skipped when
  // empty), deletes the detached workspace in vxcore, and drops its wrapper.
  void reattachDetachedWorkspace(const QString &p_detachedWorkspaceId,
                                 const QString &p_targetWorkspaceId,
                                 const QStringList &p_bufferIds);

  // Create a new workspace with the given name and switch to it.
  void newWorkspace(const QString &p_currentWorkspaceId, const QString &p_name);

  // Rename the workspace to p_newName.
  void renameWorkspace(const QString &p_workspaceId, const QString &p_newName);

  // Generate a default workspace name like "Workspace 1", "Workspace 2", etc.
  // Finds the next available number by scanning existing workspace names.
  QString generateWorkspaceName() const;

  // Clear the cached per-batch CLI detached workspace so the next
  // --detached-view invocation opens into a fresh detached window. Called by
  // MainWindow2 between queued detached batches (deterministic, no timer race).
  void resetCliDetachedBatch();

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

  // Remove every workspace except p_keepWorkspaceId. Reuses removeWorkspace for
  // each; aborts if the user cancels an unsaved-changes prompt on any workspace.
  void removeOtherWorkspaces(const QString &p_keepWorkspaceId);

  // Switch the given split to a different workspace.
  void switchWorkspace(const QString &p_currentWorkspaceId, const QString &p_targetWorkspaceId);

  // Update the buffer order in a workspace (e.g., after tab drag-to-reorder).
  void setBufferOrder(const QString &p_workspaceId, const QStringList &p_bufferIds);

  // ============ Current State ============

  // Get the ID of the current active window (InvalidViewWindowId if none).
  ID getCurrentWindowId() const;

  // Get the ID of the current active workspace (empty if none).
  QString getCurrentWorkspaceId() const;

  // ============ IViewWindowNavigator ============

  // Enumerate open windows grouped by workspace (known this session), in
  // workspace insertion order; only workspaces with >=1 window are returned.
  QVector<OpenWindowEntry> listOpenWindows() const override;

  // Focus a specific window by workspace + buffer id, surfacing a hidden
  // workspace first if needed. No-op when the target cannot be resolved.
  void focusWindow(const QString &p_workspaceId, const QString &p_bufferId) override;

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

  // Open a vx:// URL. Parses authority/path/fragment and opens the appropriate content.
  // Currently supports vx://settings/... for deep-linking into settings pages.
  void openVxUrl(const QUrl &p_url);

  // Force-emit currentViewWindowChanged (e.g. after session restore settles).
  void notifyCurrentViewWindowChanged();

  // Request a quick note. If no quick note schemes are configured, open the
  // quick access settings page; otherwise emit quickNoteRequested(). Shared by
  // the main window toolbar action and the tab bar double-click gesture.
  void requestQuickNote();

signals:
  // ============ Notification Signals (external consumers) ============

  // Emitted when a quick note should be created (schemes are configured).
  void quickNoteRequested();

  // Emitted when the current view window changes.
  void currentViewWindowChanged();

  // Emitted when any window/split is added/removed/changed.
  void windowsChanged();

  // Emitted when the count of splits changes.
  void viewSplitsCountChanged();

  // Emitted when a tab context menu requests to locate a node in the notebook explorer.
  void locateNodeRequested(const NodeIdentifier &p_nodeId);

private:
  // Move a buffer's vxcore workspace registration from source to destination.
  // Enforces the add-before-remove ordering (addBuffer(dst) BEFORE
  // removeBuffer(src)) so the buffer is never momentarily orphaned — vxcore
  // auto-closes a buffer that belongs to no workspace. Returns true when the
  // buffer is safely registered in the destination (or there was nothing to
  // propagate); returns false (leaving the source registration intact) when the
  // destination addBuffer failed, so callers must not tear down the source.
  bool transferBufferRegistration(const QString &p_srcWorkspaceId, const QString &p_dstWorkspaceId,
                                  const QString &p_bufferId);

  // Emit currentViewWindowChanged if the active window has changed.
  void checkCurrentViewWindowChange(const QString &p_workspaceId);

  // Handle FileAfterOpen hook: open a ViewWindow2 for the newly opened buffer.
  void onFileAfterOpen(const FileOpenEvent &p_event);

  // Lazily create (once per synchronous open batch) the shared detached workspace
  // used to host detached file opens (the --detached-view CLI flag and the
  // notebook explorer's "Open as Detached" menu entry), host it in a
  // DetachedWindow, and return its id. The cached id is cleared via a
  // QTimer::singleShot(0) after the batch finishes so a subsequent invocation
  // gets a fresh detached window.
  QString ensureCliDetachedWorkspace();

  // Handle NodeAfterRename hook: update buffer paths and tab titles.
  void onNodeAfterRename(const NodeRenameEvent &p_event);

  // Handle NodeAfterDelete hook: close view windows for deleted files/folders.
  void onNodeAfterDelete(const NodeOperationEvent &p_event);

  // Handle NodeAfterMove hook: refresh cached NodeIdentifier on open view windows.
  void onNodeAfterMove(const NodeMoveEvent &p_event);

  // Handle ConfigEditorChanged hook: notify all windows.
  void onEditorConfigChanged();

  // Handle NotebookBeforeClose hook: check for dirty buffers and cancel if any.
  void onNotebookBeforeClose(HookContext &p_ctx, const NotebookCloseEvent &p_event);

  // Handle NotebookAfterClose hook: close all tabs for that notebook.
  void onNotebookAfterClose(const NotebookCloseEvent &p_event);

  // Open a single buffer during session restore.
  // Resolves file type and calls m_view->openBuffer() for the view to create the ViewWindow2.
  void openRestoredBuffer(BufferService *p_bufferSvc, const QString &p_workspaceId,
                          const QString &p_bufferId, bool p_focus,
                          ViewWindowMode p_mode = ViewWindowMode::Read, int p_lineNumber = -1);

  // Buffer IDs collected during NotebookBeforeClose for use in NotebookAfterClose.
  QStringList m_pendingNotebookCloseBufferIds;

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

  // Stack of recently closed tabs for "Open Last Closed File" (Ctrl+Shift+T).
  // Most recent close is at the back (push_back / pop_back).
  static constexpr int c_maxClosedTabRecords = 20;
  QVector<ClosedTabRecord> m_closedTabStack;

  // ============ External File Change Detection ============

  void onFileCheckTimerTick();
  void onAppStateChanged(Qt::ApplicationState p_state);
  void checkAllBuffersForExternalChanges();
  void checkActiveBufferForExternalChanges();

  // Owns all WorkspaceWrapper instances. Each workspace known to the controller
  // has an entry here. Hidden workspaces cache their ViewWindows in the wrapper.
  QMap<QString, WorkspaceWrapper *> m_workspaces;

  // Workspace IDs that are hosted in DetachedWindows (out of the main splitter
  // tree). Used to suppress the generic empty-workspace auto-remove path in
  // onViewWindowClosed for detached workspaces, which are torn down by ViewArea2.
  QSet<QString> m_detachedWorkspaceIds;

  // Shared detached workspace id for the current synchronous detached-open batch
  // (--detached-view CLI flag or the explorer's "Open as Detached"). Empty
  // between batches; set by ensureCliDetachedWorkspace() and reset
  // to empty via a queued singleShot once the batch completes (or synchronously
  // by resetCliDetachedBatch() between queued batches at startup).
  QString m_cliDetachedWorkspaceId;

  // Timer for periodic external file change polling (active buffer only).
  QTimer *m_fileCheckTimer = nullptr;

  // Reentrancy guard for file change checks.
  bool m_fileCheckInProgress = false;
};

} // namespace vnotex

#endif // VIEWAREACONTROLLER_H
