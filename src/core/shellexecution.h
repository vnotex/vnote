#ifndef SHELLEXECUTION_H
#define SHELLEXECUTION_H

#include <QProcess>

namespace vnotex {


class ShellExecution : public QProcess
{
    Q_OBJECT
public:
    ShellExecution();
    void setProgram(const QString &p_prog);
    void setArguments(const QStringList &p_args);
    void setShellExecutable(const QString &p_exec);
    void setShellArguments(const QStringList &p_args);
    
    static void setupProcess(QProcess *p_process,
                             const QString &p_program,
                             const QStringList &p_args = QStringList(),
                             const QString &p_shell_exec = QString(),
                             const QStringList &p_shell_args = QStringList());
    static QString defaultShell();
    static QStringList defaultShellArguments(const QString &p_shell);
    static QString shellQuote(const QString &p_text,
                              const QString &p_shell);
    static QStringList shellQuote(const QStringList &p_list,
                                  const QString &p_shell);
    
private:
    QString m_shell_executable;
    QStringList m_shell_args;
    QString m_program;
    QStringList m_args;
    
    static QString shellBasename(const QString &p_shell);
};

}

#endif // SHELLEXECUTION_H
