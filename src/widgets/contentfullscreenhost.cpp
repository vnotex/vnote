#include "contentfullscreenhost.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLayout>
#include <QScopedValueRollback>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>

using namespace vnotex;

ContentFullScreenHost::ContentFullScreenHost(QObject *p_parent) : QObject(p_parent) {}

ContentFullScreenHost::~ContentFullScreenHost() { exitFullScreen(); }

bool ContentFullScreenHost::isFullScreen() const { return !m_content.isNull(); }

QWidget *ContentFullScreenHost::content() const { return m_content.data(); }

bool ContentFullScreenHost::exitFullScreen() { return setFullScreen(false, nullptr); }

bool ContentFullScreenHost::setFullScreen(bool p_on, QWidget *p_content) {
  if (m_transitionInProgress || p_on == isFullScreen()) {
    return false;
  }

  if (!p_on) {
    restore(false);
    return true;
  }

  if (!p_content || !p_content->parentWidget() || p_content->isWindow() ||
      !p_content->isVisible()) {
    return false;
  }

  const QScopedValueRollback<bool> transition(m_transitionInProgress, true);
  ++m_generation;
  m_content = p_content;
  m_parent = p_content->parentWidget();
  m_parentLayout = m_parent->layout();
  m_parentLayoutEnabled = m_parentLayout && m_parentLayout->isEnabled();
  m_windowFlags = p_content->windowFlags();
  m_windowState = p_content->windowState();
  m_geometry = p_content->geometry();
  m_restoreVisibility = p_content->isVisible();
  m_quitOnClose = p_content->testAttribute(Qt::WA_QuitOnClose);
  m_screen = p_content->screen();
  auto *focus = QApplication::focusWidget();
  if (focus && (focus == p_content || p_content->isAncestorOf(focus)) &&
      focus->window() == p_content->window()) {
    m_focusWidget = focus;
  }

  m_destroyedConnection =
      connect(p_content, &QObject::destroyed, this, [this]() { restore(true); });
  if (auto *app = QCoreApplication::instance()) {
    // Chromium's deeply nested focus widget consumes Escape before it reaches
    // a filter on the promoted root. Scope the application filter below.
    app->installEventFilter(this);
  }

  // QStackedLayout still owns this same tab page and otherwise resizes it when
  // the background split lays out. Do not disable the page's own layout.
  if (m_parentLayout) {
    m_parentLayout->setEnabled(false);
  }
  p_content->setWindowFlags((m_windowFlags & ~Qt::WindowType_Mask) | Qt::Window |
                            Qt::FramelessWindowHint);
  if (m_screen) {
    p_content->setScreen(m_screen);
    p_content->move(m_screen->geometry().topLeft());
  }
  p_content->setAttribute(Qt::WA_QuitOnClose, false);
  p_content->showFullScreen();
  p_content->raise();
  p_content->activateWindow();
  p_content->setFocus(Qt::OtherFocusReason);
  return true;
}

void ContentFullScreenHost::restore(bool p_contentDestroyed) {
  const QScopedValueRollback<bool> transition(m_transitionInProgress, true);
  const QPointer<QWidget> content = p_contentDestroyed ? nullptr : m_content.data();
  const auto parent = m_parent;
  const auto parentLayout = m_parentLayout;
  const auto focus = m_focusWidget;
  const auto flags = m_windowFlags;
  const auto state = m_windowState;
  const auto geometry = m_geometry;
  const bool layoutEnabled = m_parentLayoutEnabled;
  const bool restoreVisibility = m_restoreVisibility;
  const bool quitOnClose = m_quitOnClose;

  // Clear active state before flag changes, layout activation or focus callbacks
  // can reenter. In destroyed(), even a nonnull QPointer may name a dying QWidget.
  m_content.clear();
  m_parent.clear();
  m_parentLayout.clear();
  m_focusWidget.clear();
  m_screen.clear();
  m_restoreVisibility = false;
  disconnect(m_destroyedConnection);
  m_destroyedConnection = {};
  if (auto *app = QCoreApplication::instance()) {
    app->removeEventFilter(this);
  }

  if (content) {
    content->hide();
    content->setWindowFlags(flags);
    content->setWindowState(state);
    content->setAttribute(Qt::WA_QuitOnClose, quitOnClose);
    if (content->parentWidget() == parent) {
      content->setGeometry(geometry);
    }
  }

  if (parentLayout) {
    parentLayout->setEnabled(layoutEnabled);
    if (layoutEnabled) {
      parentLayout->invalidate();
      if (p_contentDestroyed) {
        // QWidget emits destroyed() before its layout item is removed from the
        // parent's layout. Do not lay out the dying page from that signal.
        QTimer::singleShot(0, parentLayout, [parentLayout]() {
          if (parentLayout && parentLayout->isEnabled()) {
            parentLayout->activate();
          }
        });
      } else {
        parentLayout->activate();
      }
    }
  }

  if (!content) {
    return;
  }
  if (content->parentWidget() != parent && content->parentWidget()) {
    auto *layout = content->parentWidget()->layout();
    if (layout && layout->isEnabled()) {
      // A real ownership transfer wins: never move the widget back or apply
      // geometry from its previous split to the new parent.
      layout->invalidate();
      layout->activate();
    }
  }
  if (restoreVisibility) {
    content->show();
    content->window()->activateWindow();
    if (focus && (focus == content || content->isAncestorOf(focus)) &&
        focus->window() == content->window()) {
      focus->setFocus(Qt::OtherFocusReason);
    } else {
      content->setFocus(Qt::OtherFocusReason);
    }
  }
}

bool ContentFullScreenHost::ownsEventTarget(QObject *p_obj) const {
  if (auto *widget = qobject_cast<QWidget *>(p_obj)) {
    // QObject ancestry alone would also capture separate child dialogs.
    return widget == m_content || widget->window() == m_content;
  }
  // Native Chromium QWindow targets have no QWidget parent chain.
  return qobject_cast<QWindow *>(p_obj) && !QApplication::activePopupWidget() &&
         QApplication::activeWindow() == m_content;
}

bool ContentFullScreenHost::hasOwnedPopup() const {
  for (QObject *obj = QApplication::activePopupWidget(); obj; obj = obj->parent()) {
    if (obj == m_content) {
      return true;
    }
  }
  return false;
}

bool ContentFullScreenHost::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (!m_content || m_transitionInProgress) {
    return QObject::eventFilter(p_obj, p_event);
  }

  switch (p_event->type()) {
  case QEvent::KeyPress:
  case QEvent::ShortcutOverride:
    if (static_cast<QKeyEvent *>(p_event)->key() == Qt::Key_Escape && ownsEventTarget(p_obj) &&
        !hasOwnedPopup()) {
      p_event->accept();
      if (p_event->type() == QEvent::KeyPress) {
        emit exitRequested();
      }
      return true;
    }
    break;

  case QEvent::Close:
    if (p_obj == m_content) {
      p_event->ignore();
      emit exitRequested();
      return true;
    }
    break;

  case QEvent::Hide:
    if (p_obj == m_content && !p_event->spontaneous() && m_restoreVisibility) {
      m_restoreVisibility = false;
      const auto generation = m_generation;
      // QWidget destruction also sends Hide before destroyed() and before its
      // QPointer clears. Defer the intent so teardown never restores a dying view.
      QTimer::singleShot(0, this, [this, generation]() {
        if (m_content && m_generation == generation && !m_restoreVisibility) {
          emit exitRequested();
        }
      });
    }
    break;

  case QEvent::ParentChange:
    if (p_obj == m_content && !m_restoreVisibility) {
      // Finish a hide/reparent transfer under its NEW parent before the caller
      // shows the destination tab. A queued Hide must not hide it afterwards.
      emit exitRequested();
    }
    break;

  case QEvent::WindowStateChange:
    if (p_obj == m_content && !m_content->isFullScreen() && !m_content->isMinimized()) {
      const auto generation = m_generation;
      // The platform can deliver this from inside its native state transition.
      // Restoring flags there destroys the QWindow still in use by that callback.
      QTimer::singleShot(0, this, [this, generation]() {
        if (m_content && m_generation == generation && !m_content->isFullScreen() &&
            !m_content->isMinimized()) {
          emit exitRequested();
        }
      });
    }
    break;

  default:
    break;
  }

  return QObject::eventFilter(p_obj, p_event);
}
