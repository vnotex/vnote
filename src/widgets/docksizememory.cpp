#include "docksizememory.h"

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>

using namespace vnotex;

DockSizeMemory::DockSizeMemory(QMainWindow *p_window, QDockWidget *p_dock)
    : QObject(p_dock), m_window(p_window), m_dock(p_dock) {
  m_restoreTimer.setSingleShot(true);
  connect(&m_restoreTimer, &QTimer::timeout, this, [this]() {
    if (!m_dock->isVisible() || !isSeparateDock()) {
      return;
    }
    // The host's content and Qt's layout must exist before requesting the saved split.
    restoreSize(Qt::Horizontal);
    restoreSize(Qt::Vertical);
    m_restoreOnShow = false;
  });
  m_dock->installEventFilter(this);
}

bool DockSizeMemory::isSeparateDock() const {
  return !m_dock->isFloating() && m_window->dockWidgetArea(m_dock) != Qt::NoDockWidgetArea &&
         m_window->tabifiedDockWidgets(m_dock).isEmpty();
}

void DockSizeMemory::restoreSize(Qt::Orientation p_orientation) {
  const bool horizontal = p_orientation == Qt::Horizontal;
  const auto geometry = m_dock->geometry();
  const int requested = horizontal ? m_size.width() : m_size.height();
  QList<QDockWidget *> docks{m_dock};
  QList<int> sizes{requested};
  int peersSize = 0;
  for (auto *peer : m_window->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (peer == m_dock || !peer->isVisible() || peer->isFloating() ||
        m_window->dockWidgetArea(peer) != m_window->dockWidgetArea(m_dock)) {
      continue;
    }
    const auto rect = peer->geometry();
    // Only peers in the same row/column participate. Inactive dock tabs are offscreen.
    const bool sameSpan = horizontal
                              ? rect.top() == geometry.top() && rect.bottom() == geometry.bottom()
                              : rect.left() == geometry.left() && rect.right() == geometry.right();
    if (!sameSpan || !m_window->rect().intersects(rect)) {
      continue;
    }
    const int size = horizontal ? peer->width() : peer->height();
    docks.append(peer);
    sizes.append(size);
    peersSize += size;
  }
  if (peersSize > 0) {
    // resizeDocks uses relative weights: keep the row/column total unchanged or Qt
    // redistributes the spare space back into the very dock we are restoring.
    const int current = horizontal ? m_dock->width() : m_dock->height();
    const int remaining = qMax(0, current + peersSize - requested);
    for (int i = 1; i < sizes.size(); ++i) {
      sizes[i] = qMax(1, int(qint64(sizes[i]) * remaining / peersSize));
    }
  }
  m_window->resizeDocks(docks, sizes, p_orientation);
}

QSize DockSizeMemory::getSize() const {
  return !m_restoreOnShow && m_dock->isVisible() && isSeparateDock() ? m_dock->size() : m_size;
}

void DockSizeMemory::setSize(const QSize &p_size) {
  m_restoreTimer.stop();
  m_size = p_size.isEmpty() ? QSize() : p_size;
  m_restoreOnShow = !m_size.isEmpty();
}

bool DockSizeMemory::eventFilter(QObject *p_object, QEvent *p_event) {
  if (p_event->type() == QEvent::Hide && m_dock->isHidden()) {
    // Parent hide (including minimize-to-tray) is not a change of dock visibility.
    // Nor may a rapid show/hide replace loaded memory with pre-layout geometry.
    if (!m_restoreOnShow && m_window->isVisible() && isSeparateDock()) {
      m_size = m_dock->size();
    }
    m_restoreTimer.stop();
    m_restoreOnShow = !m_size.isEmpty();
  } else if (p_event->type() == QEvent::Show && m_restoreOnShow) {
    m_restoreTimer.start(0);
  }
  return QObject::eventFilter(p_object, p_event);
}
