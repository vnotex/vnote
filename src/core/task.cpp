#include "task.h"

#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAction>

#include "utils/fileutils.h"
#include "utils/pathutils.h"
#include "taskmgr.h"

using namespace vnotex;

Task *Task::fromFile(const QString &p_file, QObject *p_parent)
{
    auto task = fromJson(readTaskFile(p_file), p_parent);
    task->m_taskFilePath = p_file;
    return task;
}

Task *Task::fromJson(const QJsonObject &p_obj, QObject *p_parent)
{
    auto task = new Task(p_parent);
    task->m_command = p_obj["command"].toString();
    task->m_label = p_obj["label"].toString();
    auto subTasks = p_obj["tasks"].toArray();
    for (const auto &subTask : subTasks) {
        task->m_subTasks.append(fromJson(subTask.toObject(), task));
    }
    return task;
}

QString Task::label() const
{
    if (!m_label.isNull()) {
        return m_label;
    }
    if (!m_taskFilePath.isNull()) {
        return QFileInfo(m_taskFilePath).baseName();
    }
    return m_label;
}

QString Task::filePath() const
{
    return m_taskFilePath;
}

void Task::run()
{
    if (m_command.isEmpty()) return ;
    qDebug() << "run task" << label() << m_command;
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::started,
            this, &Task::started);
    connect(m_process, &QProcess::errorOccurred,
            this, &Task::errorOccurred);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, [this]() {
        qDebug() << m_process->readAllStandardOutput();
    });
    m_process->start("cmd /c " + m_command);
}

QAction *Task::runAction()
{
    auto act = new QAction(label(), this);
    connect(act, &QAction::triggered,
            this, &Task::run);
    return act;
}

const QVector<Task *> &Task::subTasks() const
{
    return m_subTasks;
}

bool Task::isValidTaskFile(const QString &p_file)
{
    QFile file(p_file);
    if (!file.exists()) {
        qWarning() << "task file does not exist: " << p_file;
        return false;
    }
    
    return true;
}

QJsonObject Task::readTaskFile(const QString &p_file)
{
    auto bytes = FileUtils::readFile(p_file);
    return QJsonDocument::fromJson(bytes).object();
}

Task::Task(QObject *p_parent)
    : QObject(p_parent)
{
    m_process = new QProcess(this);
}
