#include "dockwidgethelper.h"

#include <QApplication>
#include <QBitArray>
#include <QDockWidget>
#include <QHelpEvent>
#include <QLabel>
#include <QShortcut>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QToolTip>

#include <core/configmgr.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/widgetconfig.h>
#include <gui/utils/iconutils.h>
#include <utils/widgetutils.h>

#include <core/servicelocator.h>
#include <gui/services/themeservice.h>

#include "mainwindow2.h"
#include "propertydefs.h"

using namespace vnotex;

namespace {
int dockRotationAngle(Qt::DockWidgetArea p_area) {
  switch (p_area) {
  case Qt::LeftDockWidgetArea:
    return 90;
  case Qt::RightDockWidgetArea:
    return 270;
  default:
    return -1;
  }
}
} // namespace

DockWidgetHelper::NavigationItemInfo::NavigationItemInfo(QTabBar *p_tabBar, int p_tabIndex,
                                                         int p_dockType)
    : m_tabBar(p_tabBar), m_tabIndex(p_tabIndex), m_dockType(p_dockType) {}

DockWidgetHelper::NavigationItemInfo::NavigationItemInfo(int p_dockType) : m_dockType(p_dockType) {}

DockWidgetHelper::DockWidgetHelper(MainWindow2 *p_mainWindow, ServiceLocator &p_services)
    : QObject(p_mainWindow), NavigationMode(NavigationMode::Type::DoubleKeys, p_mainWindow,
                                            p_services.get<ThemeService>()),
      m_mainWindow(p_mainWindow), m_services(p_services) {}

QString DockWidgetHelper::iconFileName(DockType p_dockType) {
  switch (p_dockType) {
  case DockType::NavigationDock:
    return "navigation_dock.svg";
  case DockType::OutlineDock:
    return "outline_dock.svg";
  case DockType::HistoryDock:
    return "history_dock.svg";
  case DockType::TagDock:
    return "tag_dock.svg";
  case DockType::SearchDock:
    return "search_dock.svg";
  case DockType::SnippetDock:
    return "snippet_dock.svg";
  case DockType::LocationListDock:
    return "location_list_dock.svg";
  case DockType::ConsoleDock:
    return "console_dock.svg";
  default:
    return QString();
  }
}

void DockWidgetHelper::setupDocks() {
  m_mainWindow->setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::West);
  m_mainWindow->setTabPosition(Qt::RightDockWidgetArea, QTabWidget::East);
  m_mainWindow->setTabPosition(Qt::TopDockWidgetArea, QTabWidget::North);
  m_mainWindow->setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
  m_mainWindow->setDockNestingEnabled(true);

  m_dockIcons.clear();
  m_dockIcons.resize(DockType::MaxDock);

  for (auto *dock : m_docks) {
    if (dock) {
      m_mainWindow->removeDockWidget(dock);
      dock->deleteLater();
    }
  }
  m_docks.clear();
  m_docks.resize(DockType::MaxDock);

  // The order of m_docks should be identical with enum DockType.
  QVector<int> tabifiedDockIndex;

  if (setupDock(DockType::NavigationDock, tr("Notebooks"), QStringLiteral("NavigationDock.vnotex"),
                Qt::LeftDockWidgetArea, Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea, true)) {
    tabifiedDockIndex.append(DockType::NavigationDock);
  }

  if (setupDock(DockType::HistoryDock, tr("History"), QStringLiteral("HistoryDock.vnotex"),
                Qt::LeftDockWidgetArea, Qt::AllDockWidgetAreas, false)) {
    tabifiedDockIndex.append(DockType::HistoryDock);
  }

  if (setupDock(DockType::TagDock, tr("Tags"), QStringLiteral("TagDock.vnotex"),
                Qt::LeftDockWidgetArea, Qt::AllDockWidgetAreas, true)) {
    tabifiedDockIndex.append(DockType::TagDock);
  }

  if (setupDock(DockType::SearchDock, tr("Search"), QStringLiteral("SearchDock.vnotex"),
                Qt::LeftDockWidgetArea, Qt::AllDockWidgetAreas, false)) {
    tabifiedDockIndex.append(DockType::SearchDock);
  }

  if (setupDock(DockType::SnippetDock, tr("Snippets"), QStringLiteral("SnippetDock.vnotex"),
                Qt::LeftDockWidgetArea, Qt::AllDockWidgetAreas, true)) {
    tabifiedDockIndex.append(DockType::SnippetDock);
  }

  setupDock(DockType::OutlineDock, tr("Outline"), QStringLiteral("OutlineDock.vnotex"),
            Qt::RightDockWidgetArea, Qt::AllDockWidgetAreas, true);

  setupDock(DockType::ConsoleDock, tr("Console"), QStringLiteral("ConsoleDock.vnotex"),
            Qt::BottomDockWidgetArea, Qt::AllDockWidgetAreas, false);

  setupDock(DockType::LocationListDock, tr("Location List"),
            QStringLiteral("LocationListDock.vnotex"), Qt::BottomDockWidgetArea,
            Qt::AllDockWidgetAreas, false);

  setupShortcuts();

  for (int i = 1; i < tabifiedDockIndex.size(); ++i) {
    m_mainWindow->tabifyDockWidget(m_docks[tabifiedDockIndex[i - 1]],
                                   m_docks[tabifiedDockIndex[i]]);
  }

  // Make Notebooks the active tab by default.
  // This will be overridden by restoreState() if saved state exists.
  if (m_docks[DockType::NavigationDock]) {
    m_docks[DockType::NavigationDock]->raise();
  }
}

static void addWidgetToDock(QDockWidget *p_dock, QWidget *p_widget) {
  p_dock->setWidget(p_widget);
  p_dock->setFocusProxy(p_widget);
}

bool DockWidgetHelper::setupDock(DockType p_dockType, const QString &p_title,
                                 const QString &p_objectName, Qt::DockWidgetArea p_area,
                                 Qt::DockWidgetAreas p_allowedAreas, bool p_visible) {
  auto *widget = m_mainWindow->getDockWidget(p_dockType);
  if (!widget) {
    return false;
  }

  auto dock = createDockWidget(p_dockType, p_title, m_mainWindow);
  m_docks[p_dockType] = dock;
  dock->setObjectName(p_objectName);
  dock->setAllowedAreas(p_allowedAreas);
  addWidgetToDock(dock, widget);
  m_mainWindow->addDockWidget(p_area, dock);
  dock->setVisible(p_visible);
  return true;
}

QDockWidget *DockWidgetHelper::createDockWidget(DockType p_dockType, const QString &p_title,
                                                QWidget *p_parent) {
  auto dock = new QDockWidget(p_title, p_parent);
  dock->setToolTip(p_title);
  dock->setProperty(PropertyDefs::c_dockWidgetIndex, p_dockType);
  dock->setProperty(PropertyDefs::c_dockWidgetTitle, p_title);
  return dock;
}

void DockWidgetHelper::activateDock(DockType p_dockType) {
  Q_ASSERT(p_dockType < DockType::MaxDock);
  activateDock(getDock(p_dockType));
}

void DockWidgetHelper::activateDock(QDockWidget *p_dock) {
  bool needUpdateTabBar = !p_dock->isVisible();

  p_dock->show();
  Q_FOREACH (QTabBar *tabBar,
             m_mainWindow->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
    bool found = false;
    for (int i = 0; i < tabBar->count(); ++i) {
      if (p_dock == reinterpret_cast<QWidget *>(tabBar->tabData(i).toULongLong())) {
        tabBar->setCurrentIndex(i);
        found = true;
        break;
      }
    }

    if (found) {
      break;
    }
  }

  p_dock->setFocus();

  if (needUpdateTabBar) {
    updateDockWidgetTabBar();
  }
}

const QVector<QDockWidget *> &DockWidgetHelper::getDocks() const { return m_docks; }

QDockWidget *DockWidgetHelper::getDock(DockType p_dockType) const {
  Q_ASSERT(p_dockType < DockType::MaxDock);
  return m_docks[p_dockType];
}

void DockWidgetHelper::setupShortcuts() {
  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

  // Map each DockType to its CoreConfig::Shortcut.  ConsoleDock has no shortcut.
  auto shortcutForDock = [](DockType p_type) -> int {
    switch (p_type) {
    case DockType::NavigationDock:
      return CoreConfig::Shortcut::NavigationDock;
    case DockType::HistoryDock:
      return CoreConfig::Shortcut::HistoryDock;
    case DockType::TagDock:
      return CoreConfig::Shortcut::TagDock;
    case DockType::SearchDock:
      return CoreConfig::Shortcut::SearchDock;
    case DockType::SnippetDock:
      return CoreConfig::Shortcut::SnippetDock;
    case DockType::OutlineDock:
      return CoreConfig::Shortcut::OutlineDock;
    case DockType::LocationListDock:
      return CoreConfig::Shortcut::LocationListDock;
    default:
      return -1; // No shortcut defined (e.g. ConsoleDock).
    }
  };

  for (int i = 0; i < DockType::MaxDock; ++i) {
    if (!m_docks[i]) {
      continue;
    }
    int sc = shortcutForDock(static_cast<DockType>(i));
    if (sc < 0) {
      continue;
    }
    auto keys = coreConfig.getShortcut(static_cast<CoreConfig::Shortcut>(sc));
    if (!keys.isEmpty()) {
      setupDockActivateShortcut(m_docks[i], keys);
    }
  }
}

void DockWidgetHelper::setupDockActivateShortcut(QDockWidget *p_dock, const QString &p_keys) {
  auto shortcut = WidgetUtils::createShortcut(p_keys, m_mainWindow);
  if (shortcut) {
    p_dock->setToolTip(QStringLiteral("%1\t%2").arg(
        p_dock->windowTitle(), QKeySequence(p_keys).toString(QKeySequence::NativeText)));
    connect(shortcut, &QShortcut::activated, this, [this, p_dock]() { activateDock(p_dock); });
  }
}

void DockWidgetHelper::postSetup() {
  updateDockWidgetTabBar();

  for (const auto dock : m_docks) {
    if (!dock) {
      continue;
    }
    connect(dock, &QDockWidget::dockLocationChanged, this,
            &DockWidgetHelper::updateDockWidgetTabBar);
    connect(dock, &QDockWidget::topLevelChanged, this, &DockWidgetHelper::updateDockWidgetTabBar);
    connect(dock, &QDockWidget::visibilityChanged, this, &DockWidgetHelper::updateDockWidgetTabBar);
  }
}

void DockWidgetHelper::updateDockWidgetTabBar() {
  QBitArray tabifiedDocks(m_docks.size(), false);
  Q_FOREACH (QTabBar *tabBar,
             m_mainWindow->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (!m_tabBarsMonitored.contains(tabBar)) {
      m_tabBarsMonitored.insert(tabBar);
      tabBar->installEventFilter(this);
      // Clean up when tab bar is destroyed to avoid dangling pointers.
      connect(tabBar, &QObject::destroyed, this,
              [this, tabBar]() { m_tabBarsMonitored.remove(tabBar); });
    }

    tabBar->setDrawBase(false);

    const int sz = ConfigMgr::getInst().getCoreConfig().getDocksTabBarIconSize();
    tabBar->setIconSize(QSize(sz, sz));

    auto tabShape = tabBar->shape();
    bool iconOnly = tabShape == QTabBar::RoundedWest || tabShape == QTabBar::RoundedEast ||
                    tabShape == QTabBar::TriangularWest || tabShape == QTabBar::TriangularEast;
    const int cnt = tabBar->count();
    if (cnt == 1) {
      iconOnly = false;
    }

    bool isSideBar =
        iconOnly && (tabShape == QTabBar::RoundedWest || tabShape == QTabBar::TriangularWest);
    if (tabBar->property(PropertyDefs::c_mainWindowSideBar).toBool() != isSideBar) {
      WidgetUtils::setPropertyDynamically(tabBar, PropertyDefs::c_mainWindowSideBar, isSideBar);
    }

    for (int i = 0; i < cnt; ++i) {
      auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(i).toULongLong());
      if (!dock) {
        continue;
      }
      int dockIdx = dock->property(PropertyDefs::c_dockWidgetIndex).toInt();
      tabifiedDocks.setBit(dockIdx);
      tabBar->setTabIcon(i, getDockIcon(static_cast<DockType>(dockIdx), isSideBar));
      if (dock->property(PropertyDefs::c_mainWindowSideBar).toBool() != isSideBar) {
        WidgetUtils::setPropertyDynamically(dock, PropertyDefs::c_mainWindowSideBar, isSideBar);
      }
    }
  }

  emit m_mainWindow->layoutChanged();
}

bool DockWidgetHelper::eventFilter(QObject *p_obj, QEvent *p_event) {
  // Handle ToolTip on QTabBar.
  auto tabBar = qobject_cast<QTabBar *>(p_obj);
  if (!tabBar) {
    return QObject::eventFilter(p_obj, p_event);
  }

  if (p_event->type() == QEvent::ToolTip) {
    // The QTabBar of the tabified dock widgets does not show tooltip due to Qt's internal
    // implementation.
    auto helpEve = static_cast<QHelpEvent *>(p_event);
    int idx = tabBar->tabAt(helpEve->pos());
    bool done = false;
    if (idx > -1) {
      auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(idx).toULongLong());
      if (dock) {
        done = true;
        QToolTip::showText(helpEve->globalPos(),
                           dock->property(PropertyDefs::c_dockWidgetTitle).toString());
      }
    }

    if (!done) {
      QToolTip::hideText();
      p_event->ignore();
    }

    return true;
  }

  return QObject::eventFilter(p_obj, p_event);
}

QStringList DockWidgetHelper::getVisibleDocks() const {
  QStringList visibleDocks;
  for (const auto dock : m_docks) {
    if (dock && dock->isVisible()) {
      visibleDocks.push_back(dock->objectName());
    }
  }
  return visibleDocks;
}

QStringList DockWidgetHelper::hideDocks() {
  const auto &keepDocks =
      m_services.get<ConfigMgr2>()->getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();
  QStringList visibleDocks;
  for (const auto dock : m_docks) {
    if (!dock) {
      continue;
    }
    const auto objName = dock->objectName();
    if (dock->isVisible()) {
      visibleDocks.push_back(objName);
    }

    if (dock->isFloating() || keepDocks.contains(objName)) {
      continue;
    }

    dock->setVisible(false);
  }

  return visibleDocks;
}

void DockWidgetHelper::restoreDocks(const QStringList &p_visibleDocks) {
  const auto &keepDocks =
      m_services.get<ConfigMgr2>()->getWidgetConfig().getMainWindowKeepDocksExpandingContentArea();
  bool hasVisible = false;
  for (const auto dock : m_docks) {
    if (!dock) {
      continue;
    }
    const auto objName = dock->objectName();
    if (dock->isFloating() || keepDocks.contains(objName)) {
      continue;
    }

    const bool visible = p_visibleDocks.contains(objName);
    hasVisible = hasVisible || visible;

    dock->setVisible(visible);
  }

  if (!hasVisible) {
    // At least make one visible.
    getDock(DockType::NavigationDock)->setVisible(true);
  }

  updateDockWidgetTabBar();
}

QVector<void *> DockWidgetHelper::getVisibleNavigationItems() {
  m_navigationItems.clear();

  QBitArray tabifiedDocks(m_docks.size(), false);
  Q_FOREACH (QTabBar *tabBar,
             m_mainWindow->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (!tabBar->isVisible()) {
      continue;
    }

    const int cnt = tabBar->count();
    for (int i = 0; i < cnt; ++i) {
      auto dock = reinterpret_cast<QDockWidget *>(tabBar->tabData(i).toULongLong());
      if (!dock) {
        continue;
      }
      int dockIdx = dock->property(PropertyDefs::c_dockWidgetIndex).toInt();
      tabifiedDocks.setBit(dockIdx);

      m_navigationItems.push_back(NavigationItemInfo(tabBar, i, dockIdx));
    }
  }

  // Non-tabified docks.
  for (int i = 0; i < m_docks.size(); ++i) {
    if (!m_docks[i]) {
      continue;
    }
    if (!tabifiedDocks[i] && m_docks[i]->isVisible()) {
      m_navigationItems.push_back(NavigationItemInfo(i));
    }
  }

  QVector<void *> items;
  for (auto &item : m_navigationItems) {
    items.push_back(&item);
  }
  return items;
}

const QIcon &DockWidgetHelper::getDockIcon(DockType p_dockType, bool p_isSideBar) {
  auto *themeService = m_mainWindow->getServiceLocator().get<ThemeService>();
  Q_ASSERT(themeService);

  const auto fg = themeService->paletteColor("widgets#mainwindow#dockwidget_tabbar#icon#fg");
  const auto selectedFg =
      themeService->paletteColor("widgets#mainwindow#dockwidget_tabbar#icon#selected#fg");
  auto sideBarFg = themeService->paletteColor("widgets#mainwindow#side_bar#icon#fg");
  auto sideBarSelectedFg =
      themeService->paletteColor("widgets#mainwindow#side_bar#icon#selected#fg");

  if (sideBarFg.isEmpty()) {
    sideBarFg = fg;
  }
  if (sideBarSelectedFg.isEmpty()) {
    sideBarSelectedFg = selectedFg;
  }

  const auto area = m_mainWindow->dockWidgetArea(m_docks[p_dockType]);
  const int newAngle = dockRotationAngle(area);
  if ((m_dockIcons[p_dockType].m_rotationAngle != newAngle ||
       m_dockIcons[p_dockType].m_isSideBar != p_isSideBar) &&
      area != Qt::NoDockWidgetArea) {
    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(p_isSideBar ? sideBarFg : fg, QIcon::Normal));
    // FIXME: the Selected Mode is not used by the selected tab of a QTabBar.
    colors.push_back(
        IconUtils::OverriddenColor(p_isSideBar ? sideBarSelectedFg : selectedFg, QIcon::Selected));

    auto iconFile = themeService->getIconFile(iconFileName(p_dockType));
    m_dockIcons[p_dockType].m_icon = IconUtils::fetchIcon(iconFile, colors, newAngle);
    m_dockIcons[p_dockType].m_rotationAngle = newAngle;
    m_dockIcons[p_dockType].m_isSideBar = p_isSideBar;
  }

  return m_dockIcons[p_dockType].m_icon;
}

void DockWidgetHelper::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) {
  Q_UNUSED(p_idx);
  auto info = static_cast<NavigationItemInfo *>(p_item);
  if (info->m_tabBar) {
    auto pos = info->m_tabBar->tabRect(info->m_tabIndex).topLeft();
    pos = info->m_tabBar->mapToGlobal(pos);
    p_label->move(m_mainWindow->mapFromGlobal(pos));
  } else {
    Q_ASSERT(m_docks[info->m_dockType]);
    p_label->setParent(m_docks[info->m_dockType]);
    p_label->move(0, 0);
  }
}

void DockWidgetHelper::handleTargetHit(void *p_item) {
  auto info = static_cast<NavigationItemInfo *>(p_item);
  activateDock(static_cast<DockType>(info->m_dockType));
}

void DockWidgetHelper::clearNavigation() {
  NavigationMode::clearNavigation();

  m_navigationItems.clear();
}
