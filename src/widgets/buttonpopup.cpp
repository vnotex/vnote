#include "buttonpopup.h"

#include <QKeyEvent>
#include <QWidgetAction>

#include <utils/widgetutils.h>

using namespace vnotex;

ButtonPopup::ButtonPopup(QToolButton *p_btn, QWidget *p_parent) : QMenu(p_parent), m_button(p_btn) {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  // Qt::Popup on macOS does not work well with input method.
  setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
  setWindowModality(Qt::ApplicationModal);
#endif
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
