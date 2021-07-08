#include "floatingwidget.h"

#include <QMenu>

using namespace vnotex;

FloatingWidget::FloatingWidget(QWidget *p_parent)
    : QWidget(p_parent)
{
}

void FloatingWidget::showEvent(QShowEvent *p_event)
{
    QWidget::showEvent(p_event);

    // May fix potential input method issue.
    activateWindow();

    setFocus();
}

void FloatingWidget::finish()
{
    if (m_menu) {
        m_menu->hide();
    }
}

void FloatingWidget::setMenu(QMenu *p_menu)
{
    m_menu = p_menu;
}
