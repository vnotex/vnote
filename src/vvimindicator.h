#ifndef VVIMINDICATOR_H
#define VVIMINDICATOR_H

#include <QWidget>
#include <QLineEdit>
#include <QPointer>
#include "utils/vvim.h"

class QLabel;
class VButtonWithWidget;
class QKeyEvent;
class QFocusEvent;
class VEditTab;

class VVimCmdLineEdit : public QLineEdit
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

class VVimIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit VVimIndicator(QWidget *p_parent = 0);

    // Update indicator according to @p_vim.
    void update(const VVim *p_vim, const VEditTab *p_editTab);

private slots:
    void updateRegistersTree(QWidget *p_widget);

    void updateMarksTree(QWidget *p_widget);

    // Vim request to trigger command line.
    void triggerCommandLine(VVim::CommandLineType p_type);

private:
    void setupUI();

    QString modeToString(VimMode p_mode) const;

    // Command line input.
    VVimCmdLineEdit *m_cmdLineEdit;

    // Indicate the mode.
    QLabel *m_modeLabel;

    // Indicate the registers.
    VButtonWithWidget *m_regBtn;

    // Indicate the marks.
    VButtonWithWidget *m_markBtn;

    // Indicate the pending keys.
    QLabel *m_keyLabel;

    QPointer<VVim> m_vim;
    QPointer<VEditTab> m_editTab;
};

#endif // VVIMINDICATOR_H
