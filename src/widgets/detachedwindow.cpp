#include "detachedwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPointer>
#include <QSplitter>

#include "viewsplit2.h"

using namespace vnotex;

DetachedWindow::DetachedWindow(ServiceLocator &p_services, const QString &p_workspaceId,
                               QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_workspaceId(p_workspaceId) {
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle(qApp->applicationDisplayName());

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // A horizontal splitter reserves room on the left for a future Outline panel.
  // For now it only holds the ViewSplit2 main pane.
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setChildrenCollapsible(false);

  m_viewSplit = new ViewSplit2(m_services, m_workspaceId, nullptr, /*p_detached=*/true);
  connect(m_viewSplit, &ViewSplit2::stayOnTopToggled, this, &DetachedWindow::setStayOnTop);
  splitter->addWidget(m_viewSplit);

  layout->addWidget(splitter);

  resize(800, 600);
}

ViewSplit2 *DetachedWindow::getViewSplit() const { return m_viewSplit; }

const QString &DetachedWindow::getWorkspaceId() const { return m_workspaceId; }

void DetachedWindow::setStayOnTop(bool p_enabled) {
  // This window uses the system title bar (no qwindowkit), so mutating the window
  // flags is safe. It may hide/recreate the native window, hence the re-show.
  const bool visible = isVisible();
  const Qt::WindowFlags flags = windowFlags();
  if (p_enabled) {
    setWindowFlags(flags | Qt::WindowStaysOnTopHint);
  } else {
    setWindowFlags(flags & ~Qt::WindowStaysOnTopHint);
  }
  if (visible) {
    show();
  }
}

void DetachedWindow::closeEvent(QCloseEvent *p_event) {
  // Only the owner may tear down the host; Cancel must not hide it.
  p_event->ignore();
  if (m_closeInProgress) {
    return;
  }

  m_closeInProgress = true;
  QPointer<DetachedWindow> guard(this);
  emit closeRequested(this);
  if (guard) {
    guard->m_closeInProgress = false;
  }
}
