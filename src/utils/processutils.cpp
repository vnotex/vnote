#include "processutils.h"

#include <QProcess>
#include <QScopedPointer>
#include <QDebug>

#include "utils.h"

using namespace vnotex;

ProcessUtils::State ProcessUtils::start(const QString &p_program,
                                        const QStringList &p_args,
                                        const QByteArray &p_stdIn,
                                        int &p_exitCodeOnSuccess,
                                        QByteArray &p_stdOut,
                                        QByteArray &p_stdErr)
{
    QScopedPointer<QProcess> proc(new QProcess());
    proc->start(p_program, p_args);
    return handleProcess(proc.data(), p_stdIn, p_exitCodeOnSuccess, p_stdOut, p_stdErr);
}

ProcessUtils::State ProcessUtils::handleProcess(QProcess *p_process,
                                                const QByteArray &p_stdIn,
                                                int &p_exitCodeOnSuccess,
                                                QByteArray &p_stdOut,
                                                QByteArray &p_stdErr)
{
    if (!p_process->waitForStarted()) {
        return State::FailedToStart;
    }

    if (!p_stdIn.isEmpty()) {
        if (p_process->write(p_stdIn) == -1) {
            p_process->closeWriteChannel();
            qWarning() << "failed to write to stdin of QProcess" << p_process->errorString();
            return State::FailedToWrite;
        } else {
            p_process->closeWriteChannel();
        }
    }

    p_process->waitForFinished();

    State state = State::Succeeded;
    if (p_process->exitStatus() == QProcess::CrashExit) {
        state = State::Crashed;
    } else {
        p_exitCodeOnSuccess = p_process->exitCode();
    }

    p_stdOut = p_process->readAllStandardOutput();
    p_stdErr = p_process->readAllStandardError();
    return state;
}

QStringList ProcessUtils::parseCombinedArgString(const QString &p_args)
{
    QStringList args;
    QString tmp;
    int quoteCount = 0;
    bool inQuote = false;

    // Handle quoting.
    // Tokens can be surrounded by double quotes "hello world".
    // Three consecutive double quotes represent the quote character itself.
    for (int i = 0; i < p_args.size(); ++i) {
        if (p_args.at(i) == QLatin1Char('"')) {
            ++quoteCount;
            if (quoteCount == 3) {
                // Third consecutive quote.
                quoteCount = 0;
                tmp += p_args.at(i);
            }

            continue;
        }

        if (quoteCount) {
            if (quoteCount == 1) {
                inQuote = !inQuote;
            }

            quoteCount = 0;
        }

        if (!inQuote && p_args.at(i).isSpace()) {
            if (!tmp.isEmpty()) {
                args += tmp;
                tmp.clear();
            }
        } else {
            tmp += p_args.at(i);
        }
    }

    if (!tmp.isEmpty()) {
        args += tmp;
    }

    return args;
}

QString ProcessUtils::combineArgString(const QStringList &p_args)
{
    QString argStr;
    for (const auto &arg : p_args) {
        QString tmp(arg);
        tmp.replace("\"", "\"\"\"");
        if (tmp.contains(' ')) {
            tmp = '"' + tmp + '"';
        }

        if (argStr.isEmpty()) {
            argStr = tmp;
        } else {
            argStr = argStr + ' ' + tmp;
        }
    }

    return argStr;
}

ProcessUtils::State ProcessUtils::start(const QString &p_program,
                                        const QStringList &p_args,
                                        const std::function<void(const QString &)> &p_logger,
                                        const bool &p_askedToStop)
{
    QProcess proc;
    proc.start(p_program, p_args);

    if (!proc.waitForStarted()) {
        return State::FailedToStart;
    }

    while (proc.state() != QProcess::NotRunning) {
        Utils::sleepWait(100);

        auto outBa = proc.readAllStandardOutput();
        auto errBa = proc.readAllStandardError();
        QString msg;
        if (!outBa.isEmpty()) {
            msg += QString::fromLocal8Bit(outBa);
        }
        if (!errBa.isEmpty()) {
            msg += QString::fromLocal8Bit(errBa);
        }
        if (!msg.isEmpty()) {
            p_logger(msg);
        }

        if (p_askedToStop) {
            break;
        }
    }

    return proc.exitStatus() == QProcess::NormalExit ? State::Succeeded : State::Crashed;
}
