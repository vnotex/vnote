#include "shellexecution.h"

#include <QFileInfo>
#include <QProcess>

using namespace vnotex;

void ShellExecution::setupProcess(QProcess *p_process,
                                  const QString &p_program,
                                  const QStringList &p_args,
                                  const QString &p_shellExec,
                                  const QStringList &p_shellArgs)
{
    auto shellExec = p_shellExec.isNull() ? defaultShell() : p_shellExec;
    auto shellArgs = p_shellArgs.isEmpty() ? defaultShellArguments(shellExec) : p_shellArgs;

    p_process->setProgram(shellExec);

    const auto shell = shellBasename(p_shellExec);
    QStringList allArgs(shellArgs);
    if (shell == "bash") {
        allArgs << (QStringList() << p_program << quoteSpaces(p_args)).join(' ');
    } else {
        allArgs << p_program << p_args;
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
    return QStringLiteral("PowerShell.exe");
#else
    return QStringLiteral("/bin/bash");
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

QString ShellExecution::quoteSpace(const QString &p_arg)
{
    if (p_arg.contains(QLatin1Char(' '))) {
        return QLatin1Char('"') + p_arg + QLatin1Char('"');
    } else {
        return p_arg;
    }
}

QStringList ShellExecution::quoteSpaces(const QStringList &p_args)
{
    QStringList args;
    for (const auto &arg : p_args) {
        args << quoteSpace(arg);
    }
    return args;
}
