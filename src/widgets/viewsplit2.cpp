#include "viewsplit2.h"

#include <QAction>
#include <QActionGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTabBar>
#include <QToolButton>

#include "propertydefs.h"
#include "viewwindow2.h"
#include "widgetsfactory.h"
#include <core/servicelocator.h>
#include <core/services/workspacecoreservice.h>
#include <gui/services/themeservice.h>
#include <utils/iconutils.h>
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

ViewSplit2::ViewSplit2(ServiceLocator &p_services, const QString &p_workspaceId,
                       QWidget *p_parent)
    : QTabWidget(p_parent), m_services(p_services), m_workspaceId(p_workspaceId) {
  setupUI();
}

ViewSplit2::~ViewSplit2() {
}

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
    qInfo() << "ViewSplit2::tabMoved: from:" << p_from << "to:" << p_to
            << "new order:" << ids;
    emit bufferOrderChanged(this, ids);
  });
}

void ViewSplit2::setupTabBar() {
  auto bar = tabBar();
  bar->setDrawBase(false);

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
        connect(act, &QAction::triggered, this, [this, wsId]() {
          emit switchWorkspaceRequested(this, wsId);
        });
      }
    }

    p_menu->addSeparator();

    p_menu->addAction(tr("New Workspace"), [this]() { emit newWorkspaceRequested(this); });

    p_menu->addAction(tr("Remove Workspace"), [this]() { emit removeWorkspaceRequested(this); });
  }

  // Splits section.
  {
    p_menu->addSection(tr("Split"));

    p_menu->addAction(tr("Vertical Split"), [this]() {
      emit splitRequested(this, Direction::Right);
    });

    p_menu->addAction(tr("Horizontal Split"), [this]() {
      emit splitRequested(this, Direction::Down);
    });

    p_menu->addAction(tr("Maximize Split"), [this]() {
      emit maximizeSplitRequested(this);
    });

    p_menu->addAction(tr("Distribute Splits"), [this]() {
      emit distributeSplitsRequested();
    });

    // TODO(context-menu): Add context menu entries for "Close Split" (hide-only)
    // and "Close Split and Workspace" (full removal). See legacy ViewSplit for reference.
    // Currently only "Remove Split" (hide-only) is exposed.
    p_menu->addAction(tr("Remove Split"), [this]() {
      emit removeSplitRequested(this);
    });
  }
}

void ViewSplit2::closeTab(int p_idx) {
  auto win = getViewWindow(p_idx);
  if (win) {
    emit viewWindowCloseRequested(win);
  }
}

void ViewSplit2::addViewWindow(ViewWindow2 *p_win) {
  int idx = addTab(p_win, p_win->getIcon(), p_win->getName());
  setTabToolTip(idx, p_win->getTitle());

  p_win->setVisible(true);

  connect(p_win, &ViewWindow2::focused, this, [this](ViewWindow2 *) {
    emit focused(this);
  });

  connect(p_win, &ViewWindow2::statusChanged, this, [this]() {
    auto win = qobject_cast<ViewWindow2 *>(sender());
    if (!win) return;
    int idx = indexOf(win);
    if (idx != -1) {
      setTabIcon(idx, win->getIcon());
    }
  });

  connect(p_win, &ViewWindow2::nameChanged, this, [this]() {
    auto win = qobject_cast<ViewWindow2 *>(sender());
    if (!win) return;
    int idx = indexOf(win);
    if (idx != -1) {
      setTabText(idx, win->getName());
      setTabToolTip(idx, win->getTitle());
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

int ViewSplit2::getViewWindowCount() const {
  return count();
}

const QString &ViewSplit2::getWorkspaceId() const {
  return m_workspaceId;
}

void ViewSplit2::setWorkspaceId(const QString &p_workspaceId) {
  m_workspaceId = p_workspaceId;
}

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

bool ViewSplit2::isActive() const {
  return m_active;
}

void ViewSplit2::focus() {
  focusCurrentViewWindow();
}

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
        }
      }
    }
  }

  return QTabWidget::eventFilter(p_object, p_event);
}
