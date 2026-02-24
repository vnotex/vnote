#ifndef DOCKWIDGETHELPER_H
#define DOCKWIDGETHELPER_H

#include <QIcon>
#include <QPair>
#include <QSet>
#include <QVector>

#include "navigationmode.h"

class QDockWidget;
class QTabBar;

namespace vnotex {
class MainWindow2;

// Dock widget helper for MainWindow.
class DockWidgetHelper : public QObject, public NavigationMode {
  Q_OBJECT
public:
  // Index in m_docks.
  enum DockType {
    NavigationDock = 0,
    HistoryDock,
    TagDock,
    SearchDock,
    SnippetDock,
    OutlineDock,
    ConsoleDock,
    LocationListDock,
    MaxDock
  };
  Q_ENUM(DockType)

  explicit DockWidgetHelper(MainWindow2 *p_mainWindow);

  void setupDocks();

  void postSetup();

  void activateDock(DockType p_dockType);

  QDockWidget *getDock(DockType p_dockType) const;

  const QVector<QDockWidget *> &getDocks() const;

  void updateDockWidgetTabBar();

  QStringList getVisibleDocks() const;

  QStringList hideDocks();

  void restoreDocks(const QStringList &p_visibleDocks);

  // NavigationMode.
protected:
  QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

  void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

  void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

  void clearNavigation() Q_DECL_OVERRIDE;

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private:
  struct NavigationItemInfo {
    NavigationItemInfo() = default;

    NavigationItemInfo(QTabBar *p_tabBar, int p_tabIndex, int p_dockType);

    NavigationItemInfo(int p_dockType);

    QTabBar *m_tabBar = nullptr;

    int m_tabIndex = -1;

    int m_dockType = -1;
  };

  struct IconInfo {
    QIcon m_icon;

    int m_rotationAngle = INT_MIN;

    bool m_isSideBar = false;
  };

  bool setupDock(DockType p_dockType, const QString &p_title, const QString &p_objectName,
                 Qt::DockWidgetArea p_area, Qt::DockWidgetAreas p_allowedAreas,
                 bool p_visible);

  QDockWidget *createDockWidget(DockType p_dockType, const QString &p_title, QWidget *p_parent);

  void setupShortcuts();

  void activateDock(QDockWidget *p_dock);

  void setupDockActivateShortcut(QDockWidget *p_dock, const QString &p_keys);

  const QIcon &getDockIcon(DockType p_dockType, bool p_isSideBar);

  static QString iconFileName(DockType p_dockType);

  MainWindow2 *m_mainWindow = nullptr;

  QVector<IconInfo> m_dockIcons;

  QVector<QDockWidget *> m_docks;

  // We need to install event filter to the tabbar of tabified dock widgets.
  QSet<QTabBar *> m_tabBarsMonitored;

  QVector<NavigationItemInfo> m_navigationItems;
};
} // namespace vnotex

#endif // DOCKWIDGETHELPER_H
