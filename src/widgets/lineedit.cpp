#include "lineedit.h"

#include <QKeyEvent>
#include <QCoreApplication>

#include <utils/widgetutils.h>

using namespace vnotex;

LineEdit::LineEdit(QWidget *p_parent)
    : QLineEdit(p_parent)
{
}

LineEdit::LineEdit(const QString &p_contents, QWidget *p_parent)
    : QLineEdit(p_contents, p_parent)
{
}

void LineEdit::keyPressEvent(QKeyEvent *p_event)
{
    // Note that QKeyEvent starts with isAccepted() == true, so you do not
    // need to call QKeyEvent::accept() - just do not call the base class
    // implementation if you act upon the key.
    bool accepted = false;
    int modifiers = p_event->modifiers();
    switch (p_event->key()) {
    case Qt::Key_BracketLeft:
    {
        if (WidgetUtils::isViControlModifier(modifiers)) {
            auto escEvent = new QKeyEvent(QEvent::KeyPress,
                                          Qt::Key_Escape,
                                          Qt::NoModifier);
            QCoreApplication::postEvent(this, escEvent);
            accepted = true;
        }

        break;
    }

    case Qt::Key_H:
    {
        // Backspace.
        if (WidgetUtils::isViControlModifier(modifiers)) {
            backspace();
            accepted = true;
        }

        break;
    }

    case Qt::Key_W:
    {
        // Delete one word backward.
        if (WidgetUtils::isViControlModifier(modifiers)) {
            if (!hasSelectedText()) {
                cursorWordBackward(true);
            }

            backspace();
            accepted = true;
        }

        break;
    }

    case Qt::Key_U:
    {
        if (WidgetUtils::isViControlModifier(modifiers)) {
            if (hasSelectedText()) {
                backspace();
            } else {
                int pos = cursorPosition();
                if (pos > 0) {
                    cursorBackward(true, pos);
                    backspace();
                }
            }

            accepted = true;
        }

        break;
    }

    default:
        break;
    }

    if (!accepted) {
        QLineEdit::keyPressEvent(p_event);
    }
}

void LineEdit::selectBaseName(QLineEdit *p_lineEdit)
{
    auto name = p_lineEdit->text();
    int dotIndex = name.lastIndexOf('.');
    p_lineEdit->setSelection(0, (dotIndex == -1) ? name.size() : dotIndex);
}
