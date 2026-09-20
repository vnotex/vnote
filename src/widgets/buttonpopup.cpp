#include "buttonpopup.h"

#include <QKeyEvent>
#include <QScreen>
#include <QToolButton>
#include <QWidgetAction>

using namespace vnotex;

ButtonPopup::ButtonPopup(QToolButton *p_btn, QWidget *p_parent, Alignment p_alignment)
    : QMenu(p_parent), m_button(p_btn), m_alignment(p_alignment) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  // Qt::Popup on macOS does not work well with input method.
  setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
  setWindowModality(Qt::ApplicationModal);
#endif
}

void ButtonPopup::showEvent(QShowEvent *p_event) {
  QMenu::showEvent(p_event);

  // Qt already places menus vertically and handles hidden toolbar buttons in overflow menus.
  if (m_alignment == Alignment::Right && m_button && m_button->isVisible()) {
    const auto available = m_button->screen()->availableGeometry();
    const int preferredX = m_button->mapToGlobal(QPoint(m_button->width(), 0)).x() - width();
    const int maxX = qMax(available.x(), available.x() + available.width() - width());
    move(qBound(available.x(), preferredX, maxX), y());
  }
}

void ButtonPopup::keyPressEvent(QKeyEvent *p_event) {
  const int key = p_event->key();
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    // Swallow Enter/Return key here to avoid hiding the popup.
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
