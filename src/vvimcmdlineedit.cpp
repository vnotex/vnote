#include "vvimcmdlineedit.h"

#include <QInputMethod>
#include <QGuiApplication>

#include "vpalette.h"

extern VPalette *g_palette;

VVimCmdLineEdit::VVimCmdLineEdit(QWidget *p_parent)
    : VLineEdit(p_parent), m_type(VVim::CommandLineType::Invalid),
      m_registerPending(false), m_enableInputMethod(true)
{
    // When user delete all the text, cancel command input.
    connect(this, &VVimCmdLineEdit::textChanged,
            this, [this](const QString &p_text){
                if (p_text.isEmpty()) {
                    emit commandCancelled();
                } else {
                    emit commandChanged(m_type, p_text.right(p_text.size() - 1));
                }
            });

    connect(this, &VVimCmdLineEdit::textEdited,
            this, [this](const QString &p_text){
                if (p_text.size() < 2) {
                    m_userLastInput.clear();
                } else {
                    m_userLastInput = p_text.right(p_text.size() - 1);
                }
            });

    m_originStyleSheet = styleSheet();
}

QString VVimCmdLineEdit::getCommand() const
{
    QString tx = text();
    if (tx.size() < 2) {
        return "";
    } else {
        return tx.right(tx.size() - 1);
    }
}

QString VVimCmdLineEdit::commandLineTypeLeader(VVim::CommandLineType p_type)
{
    QString leader;
    switch (p_type) {
    case VVim::CommandLineType::Command:
        leader = ":";
        break;

    case VVim::CommandLineType::SearchForward:
        leader = "/";
        break;

    case VVim::CommandLineType::SearchBackward:
        leader = "?";
        break;

    case VVim::CommandLineType::Invalid:
        leader.clear();
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    return leader;
}

void VVimCmdLineEdit::reset(VVim::CommandLineType p_type)
{
    m_type = p_type;
    m_userLastInput.clear();
    setCommand("");
    show();
    setFocus();
    setInputMethodEnabled(p_type != VVim::CommandLineType::Command);
}

void VVimCmdLineEdit::setInputMethodEnabled(bool p_enabled)
{
    if (m_enableInputMethod != p_enabled) {
        m_enableInputMethod = p_enabled;

        QInputMethod *im = QGuiApplication::inputMethod();
        im->reset();
        // Ask input method to query current state, which will call inputMethodQuery().
        im->update(Qt::ImEnabled);
    }
}

QVariant VVimCmdLineEdit::inputMethodQuery(Qt::InputMethodQuery p_query) const
{
    if (p_query == Qt::ImEnabled) {
        return m_enableInputMethod;
    }

    return VLineEdit::inputMethodQuery(p_query);
}

// See if @p_modifiers is Control which is different on macOs and Windows.
static bool isControlModifier(int p_modifiers)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return p_modifiers == Qt::MetaModifier;
#else
    return p_modifiers == Qt::ControlModifier;
#endif
}

void VVimCmdLineEdit::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    int modifiers = p_event->modifiers();

    if (m_registerPending) {
        // Ctrl and Shift may be sent out first.
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Meta) {
            goto exit;
        }

        // Expecting a register name.
        emit requestRegister(key, modifiers);

        p_event->accept();
        setRegisterPending(false);
        return;
    }

    if ((key == Qt::Key_Return && modifiers == Qt::NoModifier)
        || (key == Qt::Key_Enter && modifiers == Qt::KeypadModifier)) {
        // Enter, complete the command line input.
        p_event->accept();
        emit commandFinished(m_type, getCommand());
        return;
    } else if (key == Qt::Key_Escape
               || (key == Qt::Key_BracketLeft && isControlModifier(modifiers))) {
        // Exit command line input.
        setText(commandLineTypeLeader(m_type));
        p_event->accept();
        emit commandCancelled();
        return;
    }

    switch (key) {
    case Qt::Key_U:
        if (isControlModifier(modifiers)) {
            // Ctrl+U, delete all user input.
            setText(commandLineTypeLeader(m_type));
            p_event->accept();
            return;
        }

        break;

    case Qt::Key_N:
        if (!isControlModifier(modifiers)) {
            break;
        }
        // Ctrl+N, request next command.
        // Fall through.
    case Qt::Key_Down:
    {
        emit requestNextCommand(m_type, getCommand());
        p_event->accept();
        return;
    }

    case Qt::Key_P:
        if (!isControlModifier(modifiers)) {
            break;
        }
        // Ctrl+P, request previous command.
        // Fall through.
    case Qt::Key_Up:
    {
        emit requestPreviousCommand(m_type, getCommand());
        p_event->accept();
        return;
    }

    case Qt::Key_R:
    {
        if (isControlModifier(modifiers)) {
            // Ctrl+R, insert the content of a register.
            setRegisterPending(true);
            p_event->accept();
            return;
        }
    }

    default:
        break;
    }

exit:
    VLineEdit::keyPressEvent(p_event);
}

void VVimCmdLineEdit::focusOutEvent(QFocusEvent *p_event)
{
    if (p_event->reason() != Qt::ActiveWindowFocusReason) {
        emit commandCancelled();
    }

    VLineEdit::focusOutEvent(p_event);
}

void VVimCmdLineEdit::setCommand(const QString &p_cmd)
{
    setText(commandLineTypeLeader(m_type) + p_cmd);
}

void VVimCmdLineEdit::restoreUserLastInput()
{
    setCommand(m_userLastInput);
}

void VVimCmdLineEdit::setRegisterPending(bool p_pending)
{
    if (p_pending && !m_registerPending) {
        // Going to pending.
        setStyleSheet(QString("background: %1;").arg(g_palette->color("vim_indicator_cmd_edit_pending_bg")));
    } else if (!p_pending && m_registerPending) {
        // Leaving pending.
        setStyleSheet(m_originStyleSheet);
    }

    m_registerPending = p_pending;
}
