#include "buttonpopup.h"

#include <QApplication>
#include <QDialog>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>

using namespace vnotex;

namespace {
bool isOwnedBy(const QObject *p_object, const QObject *p_owner) {
  if (!p_owner) {
    return false;
  }
  for (auto *object = p_object; object; object = object->parent()) {
    if (object == p_owner) {
      return true;
    }
  }
  // Native-rendered children may have no QWidget peer or QObject link to the widget.
  const auto *ownerWidget = qobject_cast<const QWidget *>(p_owner);
  const auto *ownerWindow = ownerWidget ? ownerWidget->windowHandle() : nullptr;
  for (auto *window = qobject_cast<const QWindow *>(p_object); window && ownerWindow;
       window = window->parent() ? window->parent() : window->transientParent()) {
    if (window == ownerWindow) {
      return true;
    }
  }
  return false;
}

QWidget *eventWidget(QObject *p_object) {
  if (auto *widget = qobject_cast<QWidget *>(p_object)) {
    return widget;
  }
  if (auto *window = qobject_cast<QWindow *>(p_object)) {
    return QWidget::find(window->winId());
  }
  return nullptr;
}
} // namespace

ButtonPopup::ButtonPopup(QToolButton *p_btn, QWidget *p_parent, Alignment p_alignment)
    : QMenu(p_parent), m_button(p_btn), m_alignment(p_alignment) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  // Popup grabs do not establish native IME focus. A frameless Tool owns its input
  // context without foreground-activating a menu or making the application modal.
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_ShowWithoutActivating);
#endif
  // This connection precedes subclass callbacks which focus their embedded input.
  connect(this, &QMenu::aboutToShow, this, &ButtonPopup::beginOpening);
}

ButtonPopup::~ButtonPopup() {
  if (qApp) {
    qApp->removeEventFilter(this);
  }
}

QWidget *ButtonPopup::activePopupWidget() {
  if (auto *popup = QApplication::activePopupWidget()) {
    return popup;
  }
  if (auto *focus = QApplication::focusWidget()) {
    if (auto *popup = qobject_cast<ButtonPopup *>(focus->window()); popup && popup->isVisible()) {
      return popup;
    }
  }
  auto *popup = qobject_cast<ButtonPopup *>(QApplication::activeWindow());
  return popup && popup->isVisible() ? popup : nullptr;
}

void ButtonPopup::beginOpening() {
  if (m_transferring) {
    return;
  }
  ++m_openGeneration;
  m_open = true;
  m_restoreFocus = true;
  m_composing = false;
  m_dismissButton = Qt::NoButton;
  m_originWindow = m_button         ? m_button->window()
                   : parentWidget() ? parentWidget()->window()
                                    : QApplication::activeWindow();
  m_originFocus = QApplication::focusWidget();
  // An overflow menu already has widget focus; its host still remembers the editor.
  if (m_originWindow && (!m_originFocus || m_originFocus->window() != m_originWindow)) {
    m_originFocus = m_originWindow->focusWidget();
  }
  m_parentPopup = windowType() == Qt::Tool ? QApplication::activePopupWidget() : nullptr;
  m_transferPending = m_parentPopup && m_parentPopup != this;
  qApp->installEventFilter(this);
}

void ButtonPopup::setVisible(bool p_visible) {
  const bool opening = p_visible && !isVisible();
  const QPointer<ButtonPopup> guard(this);
  QMenu::setVisible(p_visible);
  if (!guard || !opening || !isVisible()) {
    return;
  }

  // Cocoa can deliver queued work after showEvent but before native visibility
  // finishes. Do not retire the causing menu until Qt completes that transition.
  const auto generation = m_openGeneration;
  if (m_transferPending) {
    QTimer::singleShot(0, this, [this, generation]() { transferFromPopup(generation); });
  } else {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QTimer::singleShot(0, this, [this, generation]() { activateOpening(generation); });
#endif
  }
}

void ButtonPopup::showEvent(QShowEvent *p_event) {
  QMenu::showEvent(p_event);
  // Also support callers using QWidget::show() rather than QMenu::popup().
  if (!m_open) {
    beginOpening();
  }

  // Qt already places menus vertically and handles hidden toolbar buttons in overflow menus.
  if (m_alignment == Alignment::Right && m_button && m_button->isVisible()) {
    const auto available = m_button->screen()->availableGeometry();
    const int preferredX = m_button->mapToGlobal(QPoint(m_button->width(), 0)).x() - width();
    const int maxX = qMax(available.x(), available.x() + available.width() - width());
    move(qBound(available.x(), preferredX, maxX), y());
  }
}

void ButtonPopup::activateOpening(quint64 p_generation, QWidget *p_finishedDialog) {
  if (p_generation != m_openGeneration || !m_open || !isVisible() || !isEnabled() ||
      !m_originWindow || !m_originWindow->isVisible() || !m_originWindow->isEnabled() ||
      QGuiApplication::applicationState() != Qt::ApplicationActive) {
    return;
  }
  auto *active = QApplication::activeWindow();
  const bool finishedDialogActive =
      p_finishedDialog && active == p_finishedDialog && !p_finishedDialog->isVisible();
  if (active && active != this && active != m_originWindow && !finishedDialogActive) {
    return; // A newer dialog/window owns activation; never take it back from a queued show.
  }
  if (auto *popup = QApplication::activePopupWidget();
      popup && popup != this && ownsObject(popup)) {
    return; // A child menu opened before our deferred activation.
  }
  if (auto *handle = windowHandle()) {
    handle->requestActivate();
  }
}

void ButtonPopup::transferFromPopup(quint64 p_generation) {
  if (p_generation != m_openGeneration || !m_open || !isVisible() || !m_transferPending) {
    return;
  }
  if (!m_parentPopup || QApplication::activePopupWidget() != m_parentPopup) {
    hide(); // The ancestor was cancelled/replaced before the handoff could run.
    return;
  }
  const auto anchor = pos();
  const QPointer<ButtonPopup> guard(this);
  m_transferring = true;
  // QMenu retains its causing popup until hideEvent. Release the entire ancestor grab
  // chain only after its submenu-opening stack has unwound, then start a root menu.
  while (auto *ancestor = QApplication::activePopupWidget()) {
    if (ancestor == this || !isOwnedBy(ancestor, m_originWindow)) {
      break;
    }
    ancestor->hide();
    if (!guard || p_generation != m_openGeneration) {
      return;
    }
    if (QApplication::activePopupWidget() == ancestor) {
      break;
    }
  }
  hide();
  if (!guard) {
    return;
  }
  m_parentPopup.clear();
  m_transferPending = false;
  if (p_generation == m_openGeneration && m_originWindow && m_originWindow->isVisible() &&
      m_originWindow->isEnabled() && (!parentWidget() || parentWidget()->isVisible()) &&
      !QApplication::activePopupWidget() &&
      QGuiApplication::applicationState() == Qt::ApplicationActive) {
    popup(anchor);
  }
  if (!guard) {
    return;
  }
  m_transferring = false;
  if (!isVisible()) {
    m_open = false;
    ++m_openGeneration;
    qApp->removeEventFilter(this);
    m_originFocus.clear();
    m_originWindow.clear();
  }
}

void ButtonPopup::hideEvent(QHideEvent *p_event) {
  const auto generation = m_openGeneration;
  const QPointer<ButtonPopup> guard(this);
  QMenu::hideEvent(p_event);
  if (!guard || m_transferring || generation != m_openGeneration) {
    return;
  }
  m_open = false;
  m_transferPending = false;
  m_composing = false;
  m_parentPopup.clear();
  const auto closedGeneration = ++m_openGeneration;
  const auto origin = m_originWindow;
  const auto focus = m_originFocus;
  m_originWindow.clear();
  m_originFocus.clear();
  if (m_dismissButton == Qt::NoButton) {
    qApp->removeEventFilter(this);
  } else {
    // Keep only the dismissing gesture's release, not a permanent application filter.
    QTimer::singleShot(QApplication::doubleClickInterval(), this, [this, closedGeneration]() {
      if (m_openGeneration == closedGeneration && !m_open &&
          QGuiApplication::mouseButtons() == Qt::NoButton) {
        m_dismissButton = Qt::NoButton;
        qApp->removeEventFilter(this);
      }
    });
  }
  if (windowType() == Qt::Tool && m_restoreFocus) {
    QTimer::singleShot(0, this, [this, closedGeneration, origin, focus]() {
      if (m_openGeneration != closedGeneration || m_open || !origin || !focus ||
          !origin->isVisible() || !origin->isEnabled() || !focus->isVisible() ||
          !focus->isEnabled() || focus->window() != origin ||
          QGuiApplication::applicationState() != Qt::ApplicationActive ||
          ButtonPopup::activePopupWidget() || QApplication::activeModalWidget()) {
        return;
      }
      auto *active = QApplication::activeWindow();
      if (active && active != origin && active != this) {
        return;
      }
      origin->activateWindow();
      focus->setFocus(Qt::PopupFocusReason);
    });
  }
}

bool ButtonPopup::ownsObject(QObject *p_object) const {
  return isOwnedBy(p_object, this) || isOwnedBy(eventWidget(p_object), this);
}

void ButtonPopup::dismissWhenInactive() {
  if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
    return;
  }
  const auto generation = m_openGeneration;
  QTimer::singleShot(0, this, [this, generation]() {
    if (generation == m_openGeneration && m_open && !m_composing &&
        QGuiApplication::applicationState() != Qt::ApplicationActive) {
      m_restoreFocus = false;
      hide();
    }
  });
}

bool ButtonPopup::eventFilter(QObject *p_object, QEvent *p_event) {
  const auto type = p_event->type();
  if (m_dismissButton != Qt::NoButton && type == QEvent::MouseButtonRelease &&
      static_cast<QMouseEvent *>(p_event)->button() == m_dismissButton) {
    m_dismissButton = Qt::NoButton;
    if (!m_open) {
      qApp->removeEventFilter(this);
    }
    return true;
  }
  if (!m_open) {
    return QMenu::eventFilter(p_object, p_event);
  }
  if (type == QEvent::Show && !p_event->spontaneous() && windowType() == Qt::Tool) {
    if (auto *dialog = qobject_cast<QDialog *>(p_object); dialog && ownsObject(dialog)) {
      const auto generation = m_openGeneration;
      // The panel stays visible while its child dialog closes. Restore native
      // activation after completion, not during the dialog's native hide path.
      connect(
          dialog, &QDialog::finished, this,
          [this, generation, closed = QPointer<QDialog>(dialog)] {
            if ((closed && (closed->isVisible() || !ownsObject(closed))) ||
                QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
              return;
            }
            activateOpening(generation, closed.data());
          },
          static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    }
  }
  if (type == QEvent::InputMethod && ownsObject(p_object)) {
    if (m_transferPending) {
      return true;
    }
    m_composing = !static_cast<QInputMethodEvent *>(p_event)->preeditString().isEmpty();
    if (windowType() == Qt::Tool && !m_composing) {
      dismissWhenInactive();
    }
  }
  if (m_transferPending && ownsObject(p_object) &&
      (type == QEvent::KeyPress || type == QEvent::KeyRelease ||
       type == QEvent::ShortcutOverride)) {
    p_event->accept();
    return true;
  }
  if (windowType() != Qt::Tool) {
    return QMenu::eventFilter(p_object, p_event);
  }
  if (type == QEvent::ApplicationStateChange || type == QEvent::ApplicationDeactivate) {
    dismissWhenInactive();
  } else if ((type == QEvent::Hide || type == QEvent::Close) && p_object != this &&
             isOwnedBy(parentWidget(), p_object)) {
    m_restoreFocus = false;
    hide();
    return false; // aboutToHide may destroy the owner and this popup.
  } else if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
    if (m_transferPending) {
      p_event->accept();
      return true;
    }
    const auto *modal = QApplication::activeModalWidget();
    const auto *widget = eventWidget(p_object);
    const bool inOwnedDialog = modal && isOwnedBy(modal, m_originWindow) &&
                               (isOwnedBy(p_object, modal) || isOwnedBy(widget, modal));
    if ((widget || qobject_cast<QWindow *>(p_object)) && !ownsObject(p_object) && !inOwnedDialog) {
      m_dismissButton = static_cast<QMouseEvent *>(p_event)->button();
      hide();
      p_event->accept();
      return true;
    }
  }
  return QMenu::eventFilter(p_object, p_event);
}

void ButtonPopup::keyPressEvent(QKeyEvent *p_event) {
  const int key = p_event->key();
  if (key == Qt::Key_Return || key == Qt::Key_Enter || (key == Qt::Key_Escape && m_composing)) {
    // The input/IME gets first refusal; an unhandled Return must not hide the popup.
    p_event->accept();
    return;
  }
  QMenu::keyPressEvent(p_event);
}

void ButtonPopup::addWidget(QWidget *p_widget) {
  auto act = new QWidgetAction(this);
  // @act will own @p_widget.
  act->setDefaultWidget(p_widget);
  addAction(act);
}
