#include "presentationtoolbareffect.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QToolBar>

using namespace vnotex;

namespace {

// Menus and combo/extension popups are windows, so QWidget::window() alone
// loses their relationship to the toolbar. QObject ancestry preserves it.
bool isOwnedBy(const QObject *p_object, const QObject *p_owner) {
  for (auto *object = p_object; object; object = object->parent()) {
    if (object == p_owner) {
      return true;
    }
  }
  return false;
}

} // namespace

PresentationToolBarEffect::PresentationToolBarEffect(QToolBar *p_toolBar, QObject *p_parent)
    : QObject(p_parent), m_toolBar(p_toolBar) {
  m_opacityTimer.setSingleShot(true);
  m_opacityTimer.setInterval(0);
  connect(&m_opacityTimer, &QTimer::timeout, this, &PresentationToolBarEffect::updateOpacity);
  if (p_toolBar) {
    connect(p_toolBar, &QObject::destroyed, this, [this]() {
      m_toolBar.clear();
      setActive(false);
    });
  }
}

PresentationToolBarEffect::~PresentationToolBarEffect() { setActive(false); }

void PresentationToolBarEffect::setActive(bool p_on) {
  if (p_on && m_toolBar) {
    if (!m_opacityEffect) {
      m_opacityEffect = new QGraphicsOpacityEffect(m_toolBar);
      m_toolBar->setGraphicsEffect(m_opacityEffect);
    }
    if (!m_active) {
      m_active = true;
      qApp->installEventFilter(this);
      m_focusConnection =
          connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
            if (!m_opacityTimer.isActive()) {
              m_opacityTimer.start();
            }
          });
    }
    updateOpacity();
  } else {
    m_active = false;
    m_opacityTimer.stop();
    if (qApp) {
      qApp->removeEventFilter(this);
    }
    disconnect(m_focusConnection);
    m_focusConnection = {};
    if (m_toolBar && m_opacityEffect && m_toolBar->graphicsEffect() == m_opacityEffect) {
      m_toolBar->setGraphicsEffect(nullptr);
    }
    m_opacityEffect.clear();
  }
}

bool PresentationToolBarEffect::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (m_active && m_toolBar) {
    switch (p_event->type()) {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Close:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::ActivationChange:
    case QEvent::ApplicationStateChange:
      // Evaluate after Qt has updated hover, popup and activation bookkeeping.
      // Child-to-child moves and popup handoffs share one pending evaluation.
      if (!m_opacityTimer.isActive()) {
        m_opacityTimer.start();
      }
      break;
    default:
      break;
    }
  }
  return QObject::eventFilter(p_obj, p_event);
}

void PresentationToolBarEffect::updateOpacity() {
  if (!m_active || !m_toolBar || !m_opacityEffect) {
    return;
  }

  const auto *window = m_toolBar->window();
  const auto *popup = QApplication::activePopupWidget();
  const bool ownsPopup = isOwnedBy(popup, m_toolBar);
  const bool windowActive = QApplication::activeWindow() == window && window->isActiveWindow();
  const bool engaged =
      m_toolBar->underMouse() || isOwnedBy(QApplication::focusWidget(), m_toolBar) || ownsPopup;
  const qreal opacity = windowActive && (!popup || ownsPopup) && engaged ? 1.0 : 0.1;
  if (m_opacityEffect->opacity() != opacity) {
    m_opacityEffect->setOpacity(opacity);
  }
}
