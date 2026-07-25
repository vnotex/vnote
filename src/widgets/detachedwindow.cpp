#include "detachedwindow.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QSplitter>

#include "viewsplit2.h"

using namespace vnotex;

DetachedWindow::DetachedWindow(ServiceLocator &p_services, const QString &p_workspaceId,
                               QWidget *p_parent)
    : QWidget(p_parent), m_services(p_services), m_workspaceId(p_workspaceId) {
  setAttribute(Qt::WA_DeleteOnClose, false);
  setWindowTitle(tr("VNote"));

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // A horizontal splitter reserves room on the left for a future Outline panel.
  // For now it only holds the ViewSplit2 main pane.
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setChildrenCollapsible(false);

  m_viewSplit = new ViewSplit2(m_services, m_workspaceId, nullptr, /*p_detached=*/true);
  splitter->addWidget(m_viewSplit);

  layout->addWidget(splitter);

  resize(800, 600);
}

ViewSplit2 *DetachedWindow::getViewSplit() const { return m_viewSplit; }

const QString &DetachedWindow::getWorkspaceId() const { return m_workspaceId; }

void DetachedWindow::setReattaching(bool p_reattaching) { m_reattaching = p_reattaching; }

void DetachedWindow::closeEvent(QCloseEvent *p_event) {
  if (!m_reattaching) {
    // Guard against re-entrancy: ViewArea2's handler moves the windows out and
    // schedules this window for deletion synchronously.
    m_reattaching = true;
    emit reattachRequested(this);
  }
  QWidget::closeEvent(p_event);
}
