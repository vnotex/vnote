#ifndef VIEWAREAVIEW_H
#define VIEWAREAVIEW_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/nodeidentifier.h>

namespace vnotex {

class Buffer2;
class IViewWindowContent;

// Abstract interface for the view layer of the ViewArea MVC.
// The controller calls these methods to command the view.
// ViewArea2 implements this interface.
class ViewAreaView {
public:
  virtual ~ViewAreaView() = default;

  // Split management
  virtual void addFirstViewSplit(const QString &p_workspaceId) = 0;
  virtual void split(const QString &p_workspaceId, Direction p_direction,
                     const QString &p_newWorkspaceId) = 0;
  virtual void removeViewSplit(const QString &p_workspaceId) = 0;
  virtual void maximizeViewSplit(const QString &p_workspaceId) = 0;
  virtual void distributeViewSplits() = 0;

  // Buffer/window management
  virtual void openBuffer(const Buffer2 &p_buffer, const QString &p_fileType,
                          const QString &p_workspaceId, const FileOpenSettings &p_settings) = 0;

  // Open a widget-based content (not file-backed) as a tab.
  // The view layer creates WidgetViewWindow2 internally.
  // @p_content: ownership transferred to the view.
  // @p_buffer: virtual buffer handle for this content.
  // @p_workspaceId: target workspace.
  virtual void openWidgetContent(IViewWindowContent *p_content, const Buffer2 &p_buffer,
                                 const QString &p_workspaceId) = 0;

  // Close the view window. Returns true if closed, false if cancelled by user
  // (e.g., unsaved changes dialog). Callers must handle false for abort semantics.
  virtual bool closeViewWindow(ID p_windowId, bool p_force) = 0;
  // Apply file open settings (scroll, highlight) to an already-created window.
  virtual void applyFileOpenSettings(ID p_windowId, const FileOpenSettings &p_settings) = 0;

  // Focus/navigation
  virtual void setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) = 0;
  virtual void focusViewSplit(const QString &p_workspaceId) = 0;

  // Window transfer
  virtual void moveViewWindowToSplit(ID p_windowId, const QString &p_srcWorkspaceId,
                                     const QString &p_dstWorkspaceId) = 0;

  // Workspace switching
  virtual void switchWorkspace(const QString &p_currentWorkspaceId,
                               const QString &p_newWorkspaceId) = 0;

  // Reparenting primitives for workspace switch (controller orchestrates these)
  virtual QVector<QObject *> takeViewWindowsFromSplit(const QString &p_workspaceId,
                                                      int *p_outCurrentIndex) = 0;
  virtual void placeViewWindowsInSplit(const QString &p_workspaceId,
                                       const QVector<QObject *> &p_windows, int p_currentIndex) = 0;
  virtual void updateSplitWorkspaceId(const QString &p_oldWorkspaceId,
                                      const QString &p_newWorkspaceId) = 0;

  // Session
  virtual void loadLayout(const QJsonObject &p_layout) = 0;
  virtual void setCurrentBuffer(const QString &p_workspaceId, const QString &p_bufferId,
                                bool p_focus) = 0;

  // Query methods (controller needs widget-layer state)
  virtual int getViewSplitCount() const = 0;
  virtual QStringList getVisibleWorkspaceIds() const = 0;

  // Get all tracked view window IDs for a given workspace.
  // Returns IDs in tab order. Used by controller for iterative close.
  virtual QVector<ID> getViewWindowIdsForWorkspace(const QString &p_workspaceId) const = 0;

  // Find the view window ID for a buffer in the given workspace.
  // Returns InvalidViewWindowId if the buffer is not open in that workspace.
  virtual ID findWindowIdByBufferId(const QString &p_workspaceId,
                                    const QString &p_bufferId) const = 0;

  // Get the buffer ID of the current (active) tab in the given workspace.
  // Returns empty string if the workspace has no tabs.
  virtual QString getCurrentBufferIdForWorkspace(const QString &p_workspaceId) const = 0;

  // Notify all visible view windows whose node matches p_oldNodeId that
  // the file has been renamed. The view iterates its windows internally.
  virtual void onNodeRenamed(const NodeIdentifier &p_oldNodeId,
                             const NodeIdentifier &p_newNodeId) = 0;

  // Notify all view windows that editor configuration has changed.
  // The view iterates all windows and calls handleEditorConfigChange().
  virtual void notifyEditorConfigChanged() = 0;
};

} // namespace vnotex

#endif // VIEWAREAVIEW_H
