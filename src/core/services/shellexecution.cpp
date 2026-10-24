#include "shellexecution.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

using namespace vnotex;

bool ShellExecution::setupProcess(QProcess *p_process, const QString &p_program,
                                  const QStringList &p_args, const QString &p_shellExec,
                                  const QStringList &p_shellArgs) {
  const auto shellExec = p_shellExec.isNull() ? defaultShell() : p_shellExec;
  const auto shell = shellBasename(shellExec);
  const bool powershell = shell == "powershell" || shell == "pwsh";
  if (!p_args.isEmpty() && shell != "bash" && !powershell) {
    return false;
  }
  for (const auto &arg : p_args) {
    if (arg.contains(QChar::Null)) {
      return false;
    }
  }

  auto allArgs = p_shellArgs.isEmpty() ? defaultShellArguments(shellExec) : p_shellArgs;
  if (p_args.isEmpty()) {
    allArgs << p_program;
  } else if (shell == "bash") {
    // Values never become part of the -c script, even inside malformed command quotes.
    allArgs << (p_program + QStringLiteral(" \"$@\"")) << QStringLiteral("vnote-task");
    allArgs << p_args;
  } else {
    // Windows PowerShell has no -CommandWithArgs. Expand per-process environment
    // values as strings, not source text; preserve both inherited and configured env.
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(p_process->processEnvironment());
    auto command = p_program;
    for (qsizetype i = 0; i < p_args.size(); ++i) {
      const auto name = QStringLiteral("VNOTE_TASK_ARGUMENT_%1").arg(i);
      env.insert(name, p_args.at(i));
      command += QStringLiteral(" ([string]${env:%1})").arg(name);
    }
    p_process->setProcessEnvironment(env);
    allArgs << command;
  }
  p_process->setProgram(shellExec);
  p_process->setArguments(allArgs);
  return true;
}

QString ShellExecution::shellBasename(const QString &p_shell) {
  return QFileInfo(p_shell).baseName().toLower();
}

QString ShellExecution::defaultShell() {
#ifdef Q_OS_WIN
  return QStringLiteral("PowerShell.exe");
#else
  return QStringLiteral("/bin/bash");
#endif
}

QStringList ShellExecution::defaultShellArguments(const QString &p_shell) {
  auto shell = shellBasename(p_shell);
  if (shell == "cmd") {
    return {"/C"};
  } else if (shell == "powershell" || shell == "pwsh") {
    return {"-Command"};
  } else if (shell == "bash") {
    return {"-c"};
  }
  return {};
}
