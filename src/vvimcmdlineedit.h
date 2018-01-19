#ifndef VVIMCMDLINEEDIT_H
#define VVIMCMDLINEEDIT_H

#include "vlineedit.h"
#include "utils/vvim.h"

class QKeyEvent;
class QFocusEvent;

class VVimCmdLineEdit : public VLineEdit
{
    Q_OBJECT

public:
    explicit VVimCmdLineEdit(QWidget *p_parent = 0);

    void reset(VVim::CommandLineType p_type);

    // Set the command to @p_cmd with leader unchanged.
    void setCommand(const QString &p_cmd);

    // Get the command.
    QString getCommand() const;

    void restoreUserLastInput();

    QVariant inputMethodQuery(Qt::InputMethodQuery p_query) const Q_DECL_OVERRIDE;

signals:
    // User has finished the input and the command is ready to execute.
    void commandFinished(VVim::CommandLineType p_type, const QString &p_cmd);

    // User cancelled the input.
    void commandCancelled();

    // User request the next command in the history.
    void requestNextCommand(VVim::CommandLineType p_type, const QString &p_cmd);

    // User request the previous command in the history.
    void requestPreviousCommand(VVim::CommandLineType p_type, const QString &p_cmd);

    // Emit when the input text changed.
    void commandChanged(VVim::CommandLineType p_type, const QString &p_cmd);

    // Emit when expecting to read a register.
    void requestRegister(int p_key, int p_modifiers);

protected:
    void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    void focusOutEvent(QFocusEvent *p_event) Q_DECL_OVERRIDE;

private:
    // Return the leader of @p_type.
    QString commandLineTypeLeader(VVim::CommandLineType p_type);

    void setRegisterPending(bool p_pending);

    void setInputMethodEnabled(bool p_enabled);

    VVim::CommandLineType m_type;

    // The latest command user input.
    QString m_userLastInput;

    // Whether we are expecting a register name to read.
    bool m_registerPending;

    QString m_originStyleSheet;

    // Whether enable input method.
    bool m_enableInputMethod;
};
#endif // VVIMCMDLINEEDIT_H
