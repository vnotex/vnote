#include "graphhelper.h"

#include <QDebug>
#include <QFileInfo>

#include <utils/processutils.h>

using namespace vnotex;

#define TaskIdProperty "GraphTaskId"
#define TaskTimeStampProperty "GraphTaskTimeStamp"

GraphHelper::GraphHelper()
    : m_cache(100, CacheItem())
{
}

QStringList GraphHelper::getArgsToUse(const QStringList &p_args)
{
    if (p_args.isEmpty()) {
        return QStringList();
    }

    if (p_args[0] == "-c") {
        // Combine all the arguments except the first one.
        QStringList args;
        args << p_args[0];

        QString subCmd;
        for (int i = 1; i < p_args.size(); ++i) {
            subCmd += " " + p_args[i];
        }
        args << subCmd;

        return args;
    } else {
        return p_args;
    }
}

void GraphHelper::process(quint64 p_id,
                          TimeStamp p_timeStamp,
                          const QString &p_format,
                          const QString &p_text,
                          const ResultCallback &p_callback)
{
    Task task;
    task.m_id = p_id;
    task.m_timeStamp = p_timeStamp;
    task.m_format = p_format;
    task.m_text = p_text;
    task.m_callback = p_callback;

    m_tasks.enqueue(task);

    processOneTask();
}

void GraphHelper::processOneTask()
{
    if (m_taskOngoing || m_tasks.isEmpty()) {
        return;
    }

    m_taskOngoing = true;

    const auto &task = m_tasks.head();

    const auto &cachedData = m_cache.get(task.m_text);
    if (!cachedData.isNull() && cachedData.m_format == task.m_format) {
        finishOneTask(cachedData.m_data);
        return;
    }

    if (!m_programValid) {
        qWarning() << "program to execute for rendering is not valid" << m_program;
        finishOneTask(QString());
        return;
    }

    // Will be released in finishOneTask.
    QProcess *process = new QProcess();
    process->setProperty(TaskIdProperty, task.m_id);
    process->setProperty(TaskTimeStampProperty, task.m_timeStamp);
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
                         finishOneTask(process, exitCode, exitStatus);
                     });

    if (m_overriddenCommand.isEmpty()) {
        Q_ASSERT(!m_program.isEmpty());
        QStringList args(m_args);
        args << getFormatArgs(task.m_format);
        process->start(m_program, getArgsToUse(args));
    } else {
        auto cmd = getCommandToUse(m_overriddenCommand, task.m_format);
        process->start(cmd);
    }

    if (process->write(task.m_text.toUtf8()) == -1) {
        qWarning() << "Graph task" << task.m_id << "failed to write to process stdin:" << process->errorString();
    }

    process->closeWriteChannel();
}

void GraphHelper::finishOneTask(QProcess *p_process, int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    Q_ASSERT(m_taskOngoing && !m_tasks.isEmpty());

    const auto task = m_tasks.dequeue();

    const quint64 id = p_process->property(TaskIdProperty).toULongLong();
    const quint64 timeStamp = p_process->property(TaskTimeStampProperty).toULongLong();
    Q_ASSERT(task.m_id == id && task.m_timeStamp == timeStamp);

    qDebug() << "Graph task" << id << timeStamp << "finished";

    bool failed = true;
    if (p_exitStatus == QProcess::NormalExit) {
        if (p_exitCode < 0) {
            qWarning() << "Graph task" << id << "failed:" << p_exitCode;
        } else {
            failed = false;
            const auto outBa = p_process->readAllStandardOutput();
            QString data;
            if (task.m_format == QStringLiteral("svg")) {
                data = QString::fromLocal8Bit(outBa);
                task.m_callback(id, timeStamp, task.m_format, data);
            } else {
                data = QString::fromLocal8Bit(outBa.toBase64());
                task.m_callback(id, timeStamp, task.m_format, data);
            }

            CacheItem item;
            item.m_format = task.m_format;
            item.m_data = data;
            m_cache.set(task.m_text, item);
        }
    } else {
        qWarning() << "Graph task" << id << "failed to start" << p_exitCode << p_exitStatus;
    }

    const QByteArray errBa = p_process->readAllStandardError();
    if (!errBa.isEmpty()) {
        QString errStr(QString::fromLocal8Bit(errBa));
        if (failed) {
            qWarning() << "Graph task" << id << "stderr:" << errStr;
        } else {
            qDebug() << "Graph task" << id << "stderr:" << errStr;
        }
    }

    if (failed) {
        task.m_callback(id, task.m_timeStamp, task.m_format, QString());
    }

    p_process->deleteLater();

    m_taskOngoing = false;
    processOneTask();
}

void GraphHelper::finishOneTask(const QString &p_data)
{
    Q_ASSERT(m_taskOngoing && !m_tasks.isEmpty());

    const auto task = m_tasks.dequeue();

    qDebug() << "Graph task" << task.m_id << task.m_timeStamp << "finished by cache" << p_data.size();

    task.m_callback(task.m_id, task.m_timeStamp, task.m_format, p_data);

    m_taskOngoing = false;
    processOneTask();
}

QString GraphHelper::getCommandToUse(const QString &p_command, const QString &p_format)
{
    auto cmd(p_command);
    cmd.replace("%1", p_format);
    return cmd;
}

void GraphHelper::clearCache()
{
    m_cache.clear();
}

void GraphHelper::checkValidProgram()
{
    m_programValid = true;
    if (m_overriddenCommand.isEmpty()) {
        if (m_program.isEmpty()) {
            m_programValid = false;
        } else {
            QFileInfo finfo(m_program);
            m_programValid = !finfo.isAbsolute() || finfo.isExecutable();
        }
    }
}
