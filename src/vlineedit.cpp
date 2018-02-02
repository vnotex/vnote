#include "vlineedit.h"

#include <QKeyEvent>

#include "utils/vutils.h"

VLineEdit::VLineEdit(QWidget *p_parent)
    : QLineEdit(p_parent), m_ctrlKEnabled(true)
{
}

VLineEdit::VLineEdit(const QString &p_contents, QWidget *p_parent)
    : QLineEdit(p_contents, p_parent), m_ctrlKEnabled(true)
{
}

void VLineEdit::keyPressEvent(QKeyEvent *p_event)
{
    // Note that QKeyEvent starts with isAccepted() == true, so you do not
    // need to call QKeyEvent::accept() - just do not call the base class
    // implementation if you act upon the key.

    bool accept = false;
    int modifiers = p_event->modifiers();
    switch (p_event->key()) {
    case Qt::Key_H:
        // Backspace.
        if (VUtils::isControlModifierForVim(modifiers)) {
            backspace();
            accept = true;
        }

        break;

    case Qt::Key_W:
        // Delete one word backward.
        if (VUtils::isControlModifierForVim(modifiers)) {
            if (!hasSelectedText()) {
                cursorWordBackward(true);
            }

            backspace();
            accept = true;
        }

        break;

    case Qt::Key_U:
    {
        if (VUtils::isControlModifierForVim(modifiers)) {
            if (hasSelectedText()) {
                backspace();
            } else {
                int pos = cursorPosition();
                if (pos > 0) {
                    cursorBackward(true, pos);
                    backspace();
                }
            }

            accept = true;
        }

        break;
    }

    case Qt::Key_K:
    {
        if (VUtils::isControlModifierForVim(modifiers) && !m_ctrlKEnabled) {
            QWidget::keyPressEvent(p_event);
            accept = true;
        }

        break;
    }

    default:
        break;
    }

    if (!accept) {
        QLineEdit::keyPressEvent(p_event);
    }
}
