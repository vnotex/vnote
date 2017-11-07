#include "vimnavigationforwidget.h"

#include <QWidget>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QDebug>

#include "vutils.h"

VimNavigationForWidget::VimNavigationForWidget()
{
}

bool VimNavigationForWidget::injectKeyPressEventForVim(QWidget *p_widget,
                                                       QKeyEvent *p_event,
                                                       QWidget *p_escWidget)
{
    Q_ASSERT(p_widget);

    bool ret = false;
    int key = p_event->key();
    int modifiers = p_event->modifiers();
    if (!p_escWidget) {
        p_escWidget = p_widget;
    }

    switch (key) {
    case Qt::Key_BracketLeft:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            QKeyEvent *escEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape,
                                                Qt::NoModifier);
            QCoreApplication::postEvent(p_escWidget, escEvent);
            ret = true;
        }

        break;
    }

    case Qt::Key_J:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            QKeyEvent *downEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down,
                                                 Qt::NoModifier);
            QCoreApplication::postEvent(p_widget, downEvent);
            ret = true;
        }

        break;
    }

    case Qt::Key_K:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            QKeyEvent *upEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up,
                                               Qt::NoModifier);
            QCoreApplication::postEvent(p_widget, upEvent);
            ret = true;
        }

        break;
    }

    default:
        break;
    }

    return ret;
}
