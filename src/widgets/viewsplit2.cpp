#include "viewsplit2.h"

#include <QMouseEvent>
#include <QTabBar>

#include "viewwindow2.h"
#include <core/servicelocator.h>

using namespace vnotex;

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
}

void ViewSplit2::setupTabBar() {
  auto bar = tabBar();
  bar->setDrawBase(false);

  // Middle click to close tab.
  bar->installEventFilter(this);
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

void ViewSplit2::setActive(bool p_active) {
  m_active = p_active;
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
  if (p_object == tabBar()) {
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
