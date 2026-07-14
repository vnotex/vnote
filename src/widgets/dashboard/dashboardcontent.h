#ifndef DASHBOARDCONTENT_H
#define DASHBOARDCONTENT_H

#include <QWidget>

#include <core/iviewwindowcontent.h>

class QToolBar;
class QAction;

namespace vnotex {

class ServiceLocator;
class DashboardBoard;

// IViewWindowContent hosted in a WidgetViewWindow2 at vx://home. Wraps a
// DashboardBoard. The board self-manages its layout persistence, so this
// content is never "dirty": save() is a no-op and isDirty() is always false.
class DashboardContent : public QWidget, public IViewWindowContent {
  Q_OBJECT
public:
  explicit DashboardContent(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const override;
  QIcon icon() const override;
  QString virtualAddress() const override;
  void setupToolBar(QToolBar *p_toolBar) override;
  bool isDirty() const override;
  bool save() override;
  void reset() override;
  bool canClose(bool p_force) override;
  QWidget *contentWidget() override;
  void handleThemeChanged() override;

signals:
  // Required by WidgetViewWindow2 (connected via string-based SIGNAL()).
  void contentChanged();

private:
  // Re-tint the toolbar buttons from the current theme. Safe no-ops until the
  // toolbar has been built (m_addAct / m_lockAct still null).
  void refreshAddStickerIcon();
  void updateLockAction();

  ServiceLocator &m_services;
  DashboardBoard *m_board = nullptr;

  // Toolbar actions, owned by the toolbar. Held so their theme-colored icons
  // (and the lock action's checked/text state) can be refreshed on demand.
  QAction *m_addAct = nullptr;
  QAction *m_lockAct = nullptr;
};

} // namespace vnotex

#endif // DASHBOARDCONTENT_H
