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
    QScopedPointer<QProcess> process(new QProcess());
    process->start(p_program, p_args);
    return startProcess(process.data(),
                        p_in,
                        p_exitCode,
                        p_out,
                        p_err);
}

int VProcessUtils::startProcess(QProcess *p_process,
                                const QByteArray &p_in,
                                int &p_exitCode,
                                QByteArray &p_out,
                                QByteArray &p_err)
{
    int ret = 0;
    if (!p_in.isEmpty()) {
        if (p_process->write(p_in) == -1) {
            p_process->closeWriteChannel();
            qWarning() << "fail to write to QProcess:" << p_process->errorString();
            return -1;
        } else {
            p_process->closeWriteChannel();
        }
    }

    bool finished = false;
    bool started = false;
    while (true) {
        QProcess::ProcessError err = p_process->error();
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
            if (p_process->state() == QProcess::NotRunning) {
                finished = true;
            }
        } else {
            if (p_process->state() != QProcess::NotRunning) {
                started = true;
            }
        }

        if (p_process->waitForFinished(500)) {
            // Finished.
            finished = true;
        }

        if (finished) {
            QProcess::ExitStatus sta = p_process->exitStatus();
            if (sta == QProcess::CrashExit) {
                ret = -1;
                break;
            }

            p_exitCode = p_process->exitCode();
            break;
        }
    }

    p_out = p_process->readAllStandardOutput();
    p_err = p_process->readAllStandardError();

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

int VProcessUtils::startProcess(const QString &p_cmd,
                                const QByteArray &p_in,
                                int &p_exitCode,
                                QByteArray &p_out,
                                QByteArray &p_err)
{
    QScopedPointer<QProcess> process(new QProcess());
    process->start(p_cmd);
    return startProcess(process.data(),
                        p_in,
                        p_exitCode,
                        p_out,
                        p_err);
}
