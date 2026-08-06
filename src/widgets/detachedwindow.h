#ifndef DETACHEDWINDOW_H
#define DETACHEDWINDOW_H

#include <QString>
#include <QWidget>

namespace vnotex {

class ServiceLocator;
class ViewSplit2;

// A top-level window that hosts a single ViewSplit2 mapped to a detached vxcore
// workspace. Created by ViewArea2 when a ViewWindow2 is detached from the main
// split area. The layout reserves room on the left for a future Outline panel
// (not implemented yet); the ViewSplit2 is the main pane.
//
// DetachedWindow is a pure View: it performs no business logic. Workspace
// creation/deletion and window transfer are orchestrated by ViewAreaController
// and ViewArea2. On close it emits reattachRequested() so ViewArea2 can pull the
// remaining windows back into the main area and tear down the workspace.
class DetachedWindow : public QWidget {
  Q_OBJECT
public:
  explicit DetachedWindow(ServiceLocator &p_services, const QString &p_workspaceId,
                          QWidget *p_parent = nullptr);

  // The hosted split (mapped 1:1 to the detached workspace).
  ViewSplit2 *getViewSplit() const;

  // The detached vxcore workspace ID this window hosts.
  const QString &getWorkspaceId() const;

  // When true, closeEvent will NOT emit reattachRequested(). Set by ViewArea2
  // when it is the one driving the teardown (e.g. app-quit reattach), to avoid
  // re-entrant reattach handling.
  void setReattaching(bool p_reattaching);

signals:
  // Emitted from closeEvent when the user closes the window. ViewArea2 reattaches
  // the remaining tab(s) into the main area and destroys the detached workspace.
  void reattachRequested(DetachedWindow *p_window);

protected:
  void closeEvent(QCloseEvent *p_event) override;

private:
  // Apply the hosted split's "Stay on Top" pin to this top-level window.
  // Affects this window only.
  void setStayOnTop(bool p_enabled);

  ServiceLocator &m_services;

  QString m_workspaceId;

  ViewSplit2 *m_viewSplit = nullptr;

  bool m_reattaching = false;
};

} // namespace vnotex

#endif // DETACHEDWINDOW_H
