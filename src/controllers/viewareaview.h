#ifndef VIEWAREAVIEW_H
#define VIEWAREAVIEW_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <core/fileopensettings.h>
#include <core/global.h>

namespace vnotex {

class Buffer2;

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
                          const QString &p_workspaceId,
                          const FileOpenSettings &p_settings) = 0;
  virtual void closeViewWindow(ID p_windowId, bool p_force) = 0;

  // Focus/navigation
  virtual void setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) = 0;
  virtual void focusViewSplit(const QString &p_workspaceId) = 0;

  // Window transfer
  virtual void moveViewWindowToSplit(ID p_windowId,
                                     const QString &p_srcWorkspaceId,
                                     const QString &p_dstWorkspaceId) = 0;

  // Workspace switching
  virtual void switchWorkspace(const QString &p_currentWorkspaceId,
                               const QString &p_newWorkspaceId) = 0;

  // Session
  virtual void loadLayout(const QJsonObject &p_layout) = 0;
  virtual void setCurrentBuffer(const QString &p_workspaceId,
                                const QString &p_bufferId, bool p_focus) = 0;

  // Query methods (controller needs widget-layer state)
  virtual int getViewSplitCount() const = 0;
  virtual QStringList getVisibleWorkspaceIds() const = 0;
};

} // namespace vnotex

#endif // VIEWAREAVIEW_H
