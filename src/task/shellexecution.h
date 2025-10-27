#ifndef SHELLEXECUTION_H
#define SHELLEXECUTION_H

#include <QStringList>

class QProcess;

namespace vnotex {
class ShellExecution {
public:
  ShellExecution() = delete;

  static void setupProcess(QProcess *p_process, const QString &p_program,
                           const QStringList &p_args = QStringList(),
                           const QString &p_shellExec = QString(),
                           const QStringList &p_shellArgs = QStringList());

  static QString defaultShell();

  static QStringList defaultShellArguments(const QString &p_shell);

private:
  static QString shellBasename(const QString &p_shell);

  static QString quoteSpace(const QString &p_arg);

  static QStringList quoteSpaces(const QStringList &p_args);
};
} // namespace vnotex

#endif // SHELLEXECUTION_H
