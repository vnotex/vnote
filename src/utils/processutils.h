#ifndef PROCESSUTILS_H
#define PROCESSUTILS_H

#include <functional>

#include <QStringList>
#include <QByteArray>

class QProcess;

namespace vnotex
{
    class ProcessUtils
    {
    public:
        enum State
        {
            Succeeded,
            Crashed,
            FailedToStart,
            FailedToWrite
        };

        ProcessUtils() = delete;

        static State start(const QString &p_program,
                           const QStringList &p_args,
                           const QByteArray &p_stdIn,
                           int &p_exitCodeOnSuccess,
                           QByteArray &p_stdOut,
                           QByteArray &p_stdErr);

        static State start(const QString &p_program,
                           const QStringList &p_args,
                           const std::function<void(const QString &)> &p_logger,
                           const bool &p_askedToStop);

        // Copied from QProcess code.
        static QStringList parseCombinedArgString(const QString &p_args);

        static QString combineArgString(const QStringList &p_args);

    private:
        static State handleProcess(QProcess *p_process,
                                   const QByteArray &p_stdIn,
                                   int &p_exitCodeOnSuccess,
                                   QByteArray &p_stdOut,
                                   QByteArray &p_stdErr);
    };
}

#endif // PROCESSUTILS_H
