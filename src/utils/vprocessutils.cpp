#include "vprocessutils.h"

#include <QProcess>
#include <QScopedPointer>
#include <QDebug>

int VProcessUtils::startProcess(const QString &p_program,
                                const QStringList &p_args,
                                const QByteArray &p_in,
                                int &p_exitCode,
                                QByteArray &p_out,
                                QByteArray &p_err)
{
    int ret = 0;
    QScopedPointer<QProcess> process(new QProcess());
    process->start(p_program, p_args);

    if (!p_in.isEmpty()) {
        if (process->write(p_in) == -1) {
            process->closeWriteChannel();
            qWarning() << "fail to write to QProcess:" << process->errorString();
            return -1;
        } else {
            process->closeWriteChannel();
        }
    }

    bool finished = false;
    bool started = false;
    while (true) {
        QProcess::ProcessError err = process->error();
        if (err == QProcess::FailedToStart
            || err == QProcess::Crashed) {
            if (err == QProcess::FailedToStart) {
                ret = -2;
            } else {
                ret = -1;
            }

            break;
        }

        if (started) {
            if (process->state() == QProcess::NotRunning) {
                finished = true;
            }
        } else {
            if (process->state() != QProcess::NotRunning) {
                started = true;
            }
        }

        if (process->waitForFinished(500)) {
            // Finished.
            finished = true;
        }

        if (finished) {
            QProcess::ExitStatus sta = process->exitStatus();
            if (sta == QProcess::CrashExit) {
                ret = -1;
                break;
            }

            p_exitCode = process->exitCode();
            break;
        }
    }

    p_out = process->readAllStandardOutput();
    p_err = process->readAllStandardError();

    return ret;
}

int VProcessUtils::startProcess(const QString &p_program,
                                const QStringList &p_args,
                                int &p_exitCode,
                                QByteArray &p_out,
                                QByteArray &p_err)
{
    return startProcess(p_program,
                        p_args,
                        QByteArray(),
                        p_exitCode,
                        p_out,
                        p_err);
}
