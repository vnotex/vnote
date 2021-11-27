#include "shellexecution.h"

#include <QFileInfo>

using namespace vnotex;

ShellExecution::ShellExecution()
{
    auto shell = defaultShell();
    setShellExecutable(shell);
    setShellArguments(defaultShellArguments(shell));
}

void ShellExecution::setProgram(const QString &p_prog)
{
    m_program = p_prog;
}

void ShellExecution::setArguments(const QStringList &p_args)
{
    m_args = p_args;
}

void ShellExecution::setShellExecutable(const QString &p_exec)
{
    m_shell_executable = p_exec;
}

void ShellExecution::setShellArguments(const QStringList &p_args)
{
    m_shell_args = p_args;
}

void ShellExecution::setupProcess(QProcess *p_process, 
                                  const QString &p_program,
                                  const QStringList &p_args, 
                                  const QString &p_shell_exec, 
                                  const QStringList &p_shell_args)
{
    auto shell_exec = p_shell_exec.isNull() ? defaultShell() : p_shell_exec;
    auto shell_args = p_shell_args.isEmpty() ? defaultShellArguments(shell_exec)
                                             : p_shell_args;
    p_process->setProgram(shell_exec);
    auto args = p_args;
    
    auto shell = shellBasename(shell_exec);
    if (!p_program.isEmpty() && !p_args.isEmpty()) {
        args = shellQuote(args, shell_exec);
    }
    QStringList allArgs(shell_args);
    if (shell == "bash") {
        allArgs << (QStringList() << p_program << args).join(' ');
    } else {
        allArgs << p_program << args;
    }
    p_process->setArguments(allArgs);
}

QString ShellExecution::shellBasename(const QString &p_shell)
{
    return QFileInfo(p_shell).baseName().toLower();    
}

QString ShellExecution::defaultShell()
{
#ifdef Q_OS_WIN
    return "PowerShell.exe";
#else
    return "/bin/bash";
#endif
}

QStringList ShellExecution::defaultShellArguments(const QString &p_shell)
{
    auto shell = shellBasename(p_shell);
    if (shell == "cmd") {
        return {"/C"};
    } else if (shell == "powershell" || p_shell == "pwsh") {
        return {"-Command"};
    } else if (shell == "bash") {
        return {"-c"};
    }
    return {};
}

QString ShellExecution::shellQuote(const QString &p_text, const QString &)
{
    if (p_text.contains(' ')) {
        return QString("\"%1\"").arg(p_text);
    }
    return p_text;
}

QStringList ShellExecution::shellQuote(const QStringList &p_list, const QString &p_shell)
{
    auto shell = shellBasename(p_shell);
    QStringList list;
    for (const auto &s : p_list) {
        list << shellQuote(s, shell);
    }
    return list;
}
