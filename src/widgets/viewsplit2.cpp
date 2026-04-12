#include "viewsplit2.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>

#include "propertydefs.h"
#include "viewwindow2.h"
#include "widgetsfactory.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/workspacecoreservice.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <gui/utils/widgetutils.h>
#include <utils/clipboardutils.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

QIcon ViewSplit2::s_windowListIcon;
QIcon ViewSplit2::s_windowListActiveIcon;
QIcon ViewSplit2::s_menuIcon;
QIcon ViewSplit2::s_menuActiveIcon;

const QString ViewSplit2::c_activeActionButtonForegroundName =
    QStringLiteral("widgets#viewsplit#action_button#active#fg");

const QString ViewSplit2::c_actionButtonForegroundName =
    QStringLiteral("widgets#viewsplit#action_button#fg");

ViewSplit2::ViewSplit2(ServiceLocator &p_services, const QString &p_workspaceId, QWidget *p_parent)
    : QTabWidget(p_parent), m_services(p_services), m_workspaceId(p_workspaceId) {
  setupUI();
  setupShortcuts();
}

ViewSplit2::~ViewSplit2() {}

void ViewSplit2::setupUI() {
  // QTabWidget properties.
  setUsesScrollButtons(true);
  setElideMode(Qt::ElideNone);
  setTabsClosable(true);
  setMovable(true);
  setDocumentMode(true);

  setupTabBar();
  setupCornerWidget();

  connect(this, &QTabWidget::tabCloseRequested, this, &ViewSplit2::closeTab);

  connect(this, &QTabWidget::tabBarClicked, this, [this](int p_idx) {
    Q_UNUSED(p_idx);
    focusCurrentViewWindow();
    emit focused(this);
  });

  connect(this, &QTabWidget::currentChanged, this, [this](int p_idx) {
    Q_UNUSED(p_idx);
    focusCurrentViewWindow();
    emit currentViewWindowChanged(getCurrentViewWindow());
  });

  connect(tabBar(), &QTabBar::tabMoved, this, [this](int p_from, int p_to) {
    QStringList ids;
    for (int i = 0; i < count(); ++i) {
      auto *win = getViewWindow(i);
      if (win) {
        ids.append(win->getBuffer().id());
      }
    }
    qInfo() << "ViewSplit2::tabMoved: from:" << p_from << "to:" << p_to << "new order:" << ids;
    emit bufferOrderChanged(this, ids);
  });
}

void ViewSplit2::setupTabBar() {
  auto bar = tabBar();
  bar->setDrawBase(false);

  // Right-click context menu on tabs.
  bar->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(bar, &QTabBar::customContextMenuRequested, this, [this](const QPoint &p_pos) {
    int idx = tabBar()->tabAt(p_pos);
    if (idx != -1) {
      createTabContextMenu(idx, tabBar()->mapToGlobal(p_pos));
    }
  });

  // Middle click to close tab.
  bar->installEventFilter(this);
}

void ViewSplit2::initIcons() {
  if (!s_windowListIcon.isNull()) {
    return;
  }

  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return;
  }

  const QString windowListIconName(QStringLiteral("split_window_list.svg"));
  const QString menuIconName(QStringLiteral("split_menu.svg"));
  const QString fg = themeService->paletteColor(c_actionButtonForegroundName);
  const QString activeFg = themeService->paletteColor(c_activeActionButtonForegroundName);

  s_windowListIcon = IconUtils::fetchIcon(themeService->getIconFile(windowListIconName), fg);
  s_windowListActiveIcon =
      IconUtils::fetchIcon(themeService->getIconFile(windowListIconName), activeFg);

  s_menuIcon = IconUtils::fetchIcon(themeService->getIconFile(menuIconName), fg);
  s_menuActiveIcon = IconUtils::fetchIcon(themeService->getIconFile(menuIconName), activeFg);
}

void ViewSplit2::setupCornerWidget() {
  initIcons();

  // Container.
  auto widget = new QWidget(this);
  widget->setProperty(PropertyDefs::c_viewSplitCornerWidget, true);
  auto layout = new QHBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);

  // Window list button.
  {
    m_windowListButton = new QToolButton(this);
    m_windowListButton->setPopupMode(QToolButton::InstantPopup);
    m_windowListButton->setProperty(PropertyDefs::c_actionToolButton, true);

    auto act = new QAction(s_windowListIcon, tr("Open Windows"), m_windowListButton);
    m_windowListButton->setDefaultAction(act);

    auto menu = WidgetsFactory::createMenu(m_windowListButton);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() { updateWindowList(menu); });
    connect(menu, &QMenu::triggered, this, [this](QAction *p_act) {
      int idx = p_act->data().toInt();
      setCurrentViewWindow(getViewWindow(idx));
    });
    m_windowListButton->setMenu(menu);

    layout->addWidget(m_windowListButton);
  }

  // Menu button.
  {
    m_menuButton = new QToolButton(this);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setProperty(PropertyDefs::c_actionToolButton, true);

    auto act = new QAction(s_menuIcon, tr("Menu"), m_menuButton);
    m_menuButton->setDefaultAction(act);

    auto menu = WidgetsFactory::createMenu(m_menuButton);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() { updateMenu(menu); });
    m_menuButton->setMenu(menu);

    layout->addWidget(m_menuButton);
  }

  widget->installEventFilter(this);

  setCornerWidget(widget, Qt::TopRightCorner);
}

void ViewSplit2::updateWindowList(QMenu *p_menu) {
  p_menu->clear();

  if (!m_windowListActionGroup) {
    m_windowListActionGroup = new QActionGroup(p_menu);
  } else {
    WidgetUtils::clearActionGroup(m_windowListActionGroup);
  }

  auto currentWin = getCurrentViewWindow();
  int cnt = getViewWindowCount();
  if (cnt == 0) {
    auto act = p_menu->addAction(tr("No Window To Show"));
    act->setEnabled(false);
    return;
  }

  for (int i = 0; i < cnt; ++i) {
    auto window = getViewWindow(i);

    auto act = new QAction(window->getName(), m_windowListActionGroup);
    act->setToolTip(window->getTitle());
    act->setData(i);
    act->setCheckable(true);
    p_menu->addAction(act);

    if (currentWin == window) {
      act->setChecked(true);
    }
  }
}

void ViewSplit2::setVisibleWorkspaceIdsFunc(const VisibleWorkspaceIdsFunc &p_func) {
  m_visibleWorkspaceIdsFunc = p_func;
}

void ViewSplit2::updateMenu(QMenu *p_menu) {
  p_menu->clear();

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto *coreConfig = configMgr ? &configMgr->getCoreConfig() : nullptr;

  // Workspaces section.
  {
    p_menu->addSection(tr("Workspaces"));

    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      auto workspaces = wsSvc->listWorkspaces();
      auto currentWsId = m_workspaceId;

      // Get workspaces visible in other splits to disable them.
      QStringList visibleIds;
      if (m_visibleWorkspaceIdsFunc) {
        visibleIds = m_visibleWorkspaceIdsFunc();
      }

      for (int i = 0; i < workspaces.size(); ++i) {
        auto wsObj = workspaces[i].toObject();
        auto wsId = wsObj.value(QStringLiteral("id")).toString();
        auto wsName = wsObj.value(QStringLiteral("name")).toString();
        if (wsName.isEmpty()) {
          wsName = tr("Workspace %1").arg(i);
        }

        auto act = p_menu->addAction(wsName);
        act->setCheckable(true);
        if (wsId == currentWsId) {
          act->setChecked(true);
        } else if (visibleIds.contains(wsId)) {
          // Workspace is shown in another split — disable to prevent duplication.
          act->setEnabled(false);
        }
        connect(act, &QAction::triggered, this,
                [this, wsId]() { emit switchWorkspaceRequested(this, wsId); });
      }
    }

    p_menu->addSeparator();

    auto *newWsAct =
        p_menu->addAction(tr("New Workspace"), [this]() { emit newWorkspaceRequested(this); });
    if (coreConfig) {
      WidgetUtils::addActionShortcutText(
          newWsAct, coreConfig->getShortcut(CoreConfig::Shortcut::NewWorkspace));
    }

    p_menu->addAction(tr("Rename Workspace"), [this]() { emit renameWorkspaceRequested(this); });

    p_menu->addAction(tr("Remove Workspace"), [this]() { emit removeWorkspaceRequested(this); });
  }

  // Splits section.
  {
    p_menu->addSection(tr("Split"));

    // Determine if multi-split actions should be enabled.
    bool multiSplit = false;
    if (m_visibleWorkspaceIdsFunc) {
      multiSplit = m_visibleWorkspaceIdsFunc().size() > 1;
    }

    auto *verticalAct = p_menu->addAction(
        tr("Vertical Split"), [this]() { emit splitRequested(this, Direction::Right); });

    auto *horizontalAct = p_menu->addAction(
        tr("Horizontal Split"), [this]() { emit splitRequested(this, Direction::Down); });

    auto *maximizeAct =
        p_menu->addAction(tr("Maximize Split"), [this]() { emit maximizeSplitRequested(this); });
    maximizeAct->setEnabled(multiSplit);

    auto *distributeAct =
        p_menu->addAction(tr("Distribute Splits"), [this]() { emit distributeSplitsRequested(); });
    distributeAct->setEnabled(multiSplit);

    if (coreConfig) {
      WidgetUtils::addActionShortcutText(
          verticalAct, coreConfig->getShortcut(CoreConfig::Shortcut::VerticalSplit));
      WidgetUtils::addActionShortcutText(
          horizontalAct, coreConfig->getShortcut(CoreConfig::Shortcut::HorizontalSplit));
      WidgetUtils::addActionShortcutText(
          maximizeAct, coreConfig->getShortcut(CoreConfig::Shortcut::MaximizeSplit));
      WidgetUtils::addActionShortcutText(
          distributeAct, coreConfig->getShortcut(CoreConfig::Shortcut::DistributeSplits));
    }

    p_menu->addSeparator();

    auto *removeSplitAct =
        p_menu->addAction(tr("Remove Split"), [this]() { emit removeSplitRequested(this); });
    removeSplitAct->setEnabled(multiSplit);

    auto *removeSplitAndWsAct = p_menu->addAction(tr("Remove Split and Workspace"), [this]() {
      emit removeSplitAndWorkspaceRequested(this);
    });
    removeSplitAndWsAct->setEnabled(multiSplit);

    if (coreConfig) {
      WidgetUtils::addActionShortcutText(
          removeSplitAndWsAct,
          coreConfig->getShortcut(CoreConfig::Shortcut::RemoveSplitAndWorkspace));
    }
  }
}

void ViewSplit2::closeTab(int p_idx) {
  auto win = getViewWindow(p_idx);
  if (win) {
    emit viewWindowCloseRequested(win);
  }
}

void ViewSplit2::addViewWindow(ViewWindow2 *p_win) {
  int idx = addTab(p_win, p_win->getIcon(), p_win->getTitle());
  QString tooltip = p_win->getBuffer().resolvedPath();
  setTabToolTip(idx, tooltip.isEmpty() ? p_win->getTitle() : tooltip);

  p_win->setVisible(true);

  connect(p_win, &ViewWindow2::focused, this, [this](ViewWindow2 *) { emit focused(this); });

  connect(p_win, &ViewWindow2::statusChanged, this, [this]() {
    auto win = qobject_cast<ViewWindow2 *>(sender());
    if (!win)
      return;
    int idx = indexOf(win);
    if (idx != -1) {
      setTabIcon(idx, win->getIcon());
      setTabText(idx, win->getTitle());
      QString tooltip = win->getBuffer().resolvedPath();
      setTabToolTip(idx, tooltip.isEmpty() ? win->getTitle() : tooltip);
    }
  });

  connect(p_win, &ViewWindow2::nameChanged, this, [this]() {
    auto win = qobject_cast<ViewWindow2 *>(sender());
    if (!win)
      return;
    int idx = indexOf(win);
    if (idx != -1) {
      setTabText(idx, win->getTitle());
      QString tooltip = win->getBuffer().resolvedPath();
      setTabToolTip(idx, tooltip.isEmpty() ? win->getTitle() : tooltip);
    }
  });
}

void ViewSplit2::takeViewWindow(ViewWindow2 *p_win) {
  int idx = indexOf(p_win);
  Q_ASSERT(idx != -1);
  removeTab(idx);

  disconnect(p_win, nullptr, this, nullptr);

  if (m_currentViewWindow == p_win) {
    m_currentViewWindow = nullptr;
  }
  if (m_lastViewWindow == p_win) {
    m_lastViewWindow = nullptr;
  }

  p_win->setVisible(false);
  p_win->setParent(nullptr);
}

ViewWindow2 *ViewSplit2::getCurrentViewWindow() const {
  return qobject_cast<ViewWindow2 *>(currentWidget());
}

void ViewSplit2::setCurrentViewWindow(ViewWindow2 *p_win) {
  if (!p_win) {
    return;
  }
  setCurrentWidget(p_win);
}

void ViewSplit2::setCurrentViewWindow(int p_idx) {
  auto win = getViewWindow(p_idx);
  setCurrentViewWindow(win);
}

QVector<ViewWindow2 *> ViewSplit2::getAllViewWindows() const {
  QVector<ViewWindow2 *> wins;
  int cnt = getViewWindowCount();
  wins.reserve(cnt);
  for (int i = 0; i < cnt; ++i) {
    wins.push_back(getViewWindow(i));
  }
  return wins;
}

int ViewSplit2::getViewWindowCount() const { return count(); }

const QString &ViewSplit2::getWorkspaceId() const { return m_workspaceId; }

void ViewSplit2::setWorkspaceId(const QString &p_workspaceId) { m_workspaceId = p_workspaceId; }

void ViewSplit2::setActive(bool p_active) {
  m_active = p_active;

  if (m_windowListButton && m_menuButton) {
    if (p_active) {
      m_windowListButton->defaultAction()->setIcon(s_windowListActiveIcon);
      m_menuButton->defaultAction()->setIcon(s_menuActiveIcon);
    } else {
      m_windowListButton->defaultAction()->setIcon(s_windowListIcon);
      m_menuButton->defaultAction()->setIcon(s_menuIcon);
    }
  }
}

bool ViewSplit2::isActive() const { return m_active; }

void ViewSplit2::focus() { focusCurrentViewWindow(); }

ViewWindow2 *ViewSplit2::getViewWindow(int p_idx) const {
  return qobject_cast<ViewWindow2 *>(widget(p_idx));
}

void ViewSplit2::focusCurrentViewWindow() {
  auto win = getCurrentViewWindow();
  if (win) {
    win->setFocus();
  } else {
    setFocus();
  }

  m_lastViewWindow = m_currentViewWindow;
  m_currentViewWindow = win;
}

void ViewSplit2::alternateTab() {
  if (!m_lastViewWindow) {
    return;
  }

  // It is fine even when m_lastViewWindow is a wild pointer. The implementation will just
  // compare its value without dereferencing it.
  if (-1 != indexOf(m_lastViewWindow)) {
    setCurrentViewWindow(m_lastViewWindow);
  } else {
    m_lastViewWindow = nullptr;
  }
}

void ViewSplit2::activateNextTab(bool p_backward) {
  int idx = currentIndex();
  if (idx == -1 || count() == 1) {
    return;
  }

  if (p_backward) {
    --idx;
    if (idx < 0) {
      idx = count() - 1;
    }
  } else {
    ++idx;
    if (idx >= count()) {
      idx = 0;
    }
  }

  setCurrentViewWindow(idx);
}

void ViewSplit2::mousePressEvent(QMouseEvent *p_event) {
  QTabWidget::mousePressEvent(p_event);
  emit focused(this);
}

bool ViewSplit2::eventFilter(QObject *p_object, QEvent *p_event) {
  if (p_object == cornerWidget()) {
    if (p_event->type() == QEvent::Resize) {
      auto resizeEve = static_cast<QResizeEvent *>(p_event);
      int height = resizeEve->size().height();
      if (height > 0) {
        // Make corner widget visible even when there is no tab.
        cornerWidget()->setMinimumHeight(height);
      }
    }
  } else if (p_object == tabBar()) {
    if (p_event->type() == QEvent::MouseButtonRelease) {
      auto mouseEve = static_cast<QMouseEvent *>(p_event);
      if (mouseEve->button() == Qt::MiddleButton) {
        int idx = tabBar()->tabAt(mouseEve->pos());
        if (idx != -1) {
          closeTab(idx);
          // closeTab may trigger a chain that removes this split (via auto-remove
          // of empty workspace). Return true to stop event processing and avoid
          // accessing potentially invalidated state.
          return true;
        }
      }
    } else if (p_event->type() == QEvent::MouseButtonDblClick) {
      auto mouseEve = static_cast<QMouseEvent *>(p_event);
      if (mouseEve->button() == Qt::LeftButton) {
        int idx = tabBar()->tabAt(mouseEve->pos());
        if (idx != -1) {
          closeTab(idx);
          // closeTab may trigger removal of this split. Return immediately.
          return true;
        }
      }
    }
  }

  return QTabWidget::eventFilter(p_object, p_event);
}

void ViewSplit2::createTabContextMenu(int p_tabIndex, const QPoint &p_globalPos) {
  auto *win = getViewWindow(p_tabIndex);
  if (!win) {
    return;
  }

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto *coreConfig = configMgr ? &configMgr->getCoreConfig() : nullptr;

  QMenu menu(this);

  // ---- Close Actions ----
  auto *closeTabAct =
      menu.addAction(tr("Close Tab"), [this, p_tabIndex]() { closeTab(p_tabIndex); });

  auto *closeAllAct = menu.addAction(tr("Close All Tabs"), [this, p_tabIndex]() {
    emit closeTabsRequested(this, p_tabIndex, CloseTabMode::All);
  });

  auto *closeOtherAct = menu.addAction(tr("Close Other Tabs"), [this, p_tabIndex]() {
    emit closeTabsRequested(this, p_tabIndex, CloseTabMode::Others);
  });

  auto *closeLeftAct = menu.addAction(tr("Close Tabs To The Left"), [this, p_tabIndex]() {
    emit closeTabsRequested(this, p_tabIndex, CloseTabMode::ToTheLeft);
  });
  closeLeftAct->setEnabled(p_tabIndex > 0);

  auto *closeRightAct = menu.addAction(tr("Close Tabs To The Right"), [this, p_tabIndex]() {
    emit closeTabsRequested(this, p_tabIndex, CloseTabMode::ToTheRight);
  });
  closeRightAct->setEnabled(p_tabIndex < count() - 1);

  if (coreConfig) {
    WidgetUtils::addActionShortcutText(closeTabAct,
                                       coreConfig->getShortcut(CoreConfig::Shortcut::CloseTab));
    WidgetUtils::addActionShortcutText(closeAllAct,
                                       coreConfig->getShortcut(CoreConfig::Shortcut::CloseAllTabs));
    WidgetUtils::addActionShortcutText(
        closeOtherAct, coreConfig->getShortcut(CoreConfig::Shortcut::CloseOtherTabs));
    WidgetUtils::addActionShortcutText(
        closeLeftAct, coreConfig->getShortcut(CoreConfig::Shortcut::CloseTabsToTheLeft));
    WidgetUtils::addActionShortcutText(
        closeRightAct, coreConfig->getShortcut(CoreConfig::Shortcut::CloseTabsToTheRight));
  }

  menu.addSeparator();

  // ---- File Actions ----
  // Resolve the absolute path (notebook file or external file).
  const auto &nodeId = win->getNodeId();
  QString absPath = win->getBuffer().resolvedPath();

  auto *copyPathAct = menu.addAction(tr("Copy Path"), [absPath]() {
    if (!absPath.isEmpty()) {
      ClipboardUtils::setTextToClipboard(absPath);
    }
  });
  copyPathAct->setEnabled(!absPath.isEmpty());

  auto *openLocAct = menu.addAction(tr("Open Location"), [absPath]() {
    if (!absPath.isEmpty()) {
      QString dirPath = PathUtils::parentDirPath(absPath);
      WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(dirPath));
    }
  });
  openLocAct->setEnabled(!absPath.isEmpty());

  if (nodeId.isValid()) {
    auto *locateAct =
        menu.addAction(tr("Locate Node"), [this, nodeId]() { emit locateNodeRequested(nodeId); });
    if (coreConfig) {
      WidgetUtils::addActionShortcutText(locateAct,
                                         coreConfig->getShortcut(CoreConfig::Shortcut::LocateNode));
    }
  }

  menu.addSeparator();

  // ---- Move Actions ----
  auto *moveLeftAct = menu.addAction(tr("Move One Split Left"), [this, win]() {
    emit moveViewWindowOneSplitRequested(this, win, Direction::Left);
  });

  auto *moveRightAct = menu.addAction(tr("Move One Split Right"), [this, win]() {
    emit moveViewWindowOneSplitRequested(this, win, Direction::Right);
  });

  auto *moveUpAct = menu.addAction(tr("Move One Split Up"), [this, win]() {
    emit moveViewWindowOneSplitRequested(this, win, Direction::Up);
  });

  auto *moveDownAct = menu.addAction(tr("Move One Split Down"), [this, win]() {
    emit moveViewWindowOneSplitRequested(this, win, Direction::Down);
  });

  if (coreConfig) {
    WidgetUtils::addActionShortcutText(
        moveLeftAct, coreConfig->getShortcut(CoreConfig::Shortcut::MoveOneSplitLeft));
    WidgetUtils::addActionShortcutText(
        moveRightAct, coreConfig->getShortcut(CoreConfig::Shortcut::MoveOneSplitRight));
    WidgetUtils::addActionShortcutText(
        moveUpAct, coreConfig->getShortcut(CoreConfig::Shortcut::MoveOneSplitUp));
    WidgetUtils::addActionShortcutText(
        moveDownAct, coreConfig->getShortcut(CoreConfig::Shortcut::MoveOneSplitDown));
  }

  menu.exec(p_globalPos);
}

QVector<ViewSplit2::TabNavigationInfo> ViewSplit2::getNavigationModeInfo() const {
  QVector<TabNavigationInfo> infos;
  auto bar = tabBar();
  for (int i = 0; i < bar->count(); ++i) {
    QPoint tl = bar->tabRect(i).topLeft();
    if (tl.x() < 0 || tl.x() >= bar->width()) {
      continue;
    }

    TabNavigationInfo info;
    info.m_topLeft = bar->mapToParent(tl);
    info.m_viewWindow = getViewWindow(i);
    infos.append(info);
  }

  return infos;
}

void ViewSplit2::setupShortcuts() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  const auto &coreConfig = configMgr->getCoreConfig();

  // NewWorkspace.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::NewWorkspace),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit newWorkspaceRequested(this); });
    }
  }

  // VerticalSplit.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::VerticalSplit),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit splitRequested(this, Direction::Right); });
    }
  }

  // HorizontalSplit.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::HorizontalSplit),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit splitRequested(this, Direction::Down); });
    }
  }

  // MaximizeSplit.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::MaximizeSplit),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit maximizeSplitRequested(this); });
    }
  }

  // DistributeSplits.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::DistributeSplits),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit distributeSplitsRequested(); });
    }
  }

  // RemoveSplitAndWorkspace.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::RemoveSplitAndWorkspace), this,
        Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { emit removeSplitAndWorkspaceRequested(this); });
    }
  }

  // ActivateTab1-9.
  {
    const CoreConfig::Shortcut tabShortcuts[] = {
        CoreConfig::Shortcut::ActivateTab1, CoreConfig::Shortcut::ActivateTab2,
        CoreConfig::Shortcut::ActivateTab3, CoreConfig::Shortcut::ActivateTab4,
        CoreConfig::Shortcut::ActivateTab5, CoreConfig::Shortcut::ActivateTab6,
        CoreConfig::Shortcut::ActivateTab7, CoreConfig::Shortcut::ActivateTab8,
        CoreConfig::Shortcut::ActivateTab9};
    for (int i = 0; i < 9; ++i) {
      auto shortcut = WidgetUtils::createShortcut(coreConfig.getShortcut(tabShortcuts[i]), this,
                                                  Qt::WidgetWithChildrenShortcut);
      if (shortcut) {
        connect(shortcut, &QShortcut::activated, this, [this, i]() { setCurrentViewWindow(i); });
      }
    }
  }

  // AlternateTab.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::AlternateTab),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, &ViewSplit2::alternateTab);
    }
  }

  // ActivateNextTab.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::ActivateNextTab),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() { activateNextTab(false); });
    }
  }

  // ActivatePreviousTab.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::ActivatePreviousTab), this,
        Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() { activateNextTab(true); });
    }
  }

  // MoveOneSplitLeft.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitLeft),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          emit moveViewWindowOneSplitRequested(this, win, Direction::Left);
        }
      });
    }
  }

  // MoveOneSplitDown.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitDown),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          emit moveViewWindowOneSplitRequested(this, win, Direction::Down);
        }
      });
    }
  }

  // MoveOneSplitUp.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitUp),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          emit moveViewWindowOneSplitRequested(this, win, Direction::Up);
        }
      });
    }
  }

  // MoveOneSplitRight.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::MoveOneSplitRight),
                                    this, Qt::WidgetWithChildrenShortcut);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          emit moveViewWindowOneSplitRequested(this, win, Direction::Right);
        }
      });
    }
  }
}
