#ifndef VPROCESSUTILS_H
#define VPROCESSUTILS_H

#include <QStringList>
#include <QByteArray>

class QProcess;

class VProcessUtils
{
public:
    // p_exitCode: exit code of the program, valid only when returning 0.
    // 0: execute successfully;
    // -1: crashed;
    // -2: fail to start.
    static int startProcess(const QString &p_program,
                            const QStringList &p_args,
                            int &p_exitCode,
                            QByteArray &p_out,
                            QByteArray &p_err);

    static int startProcess(const QString &p_program,
                            const QStringList &p_args,
                            const QByteArray &p_in,
                            int &p_exitCode,
                            QByteArray &p_out,
                            QByteArray &p_err);

    static int startProcess(const QString &p_cmd,
                            const QByteArray &p_in,
                            int &p_exitCode,
                            QByteArray &p_out,
                            QByteArray &p_err);

private:
    VProcessUtils() {}

    static int startProcess(QProcess *p_process,
                            const QByteArray &p_in,
                            int &p_exitCode,
                            QByteArray &p_out,
                            QByteArray &p_err);
};

#endif // VPROCESSUTILS_H
