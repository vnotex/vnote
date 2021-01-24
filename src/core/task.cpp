#include "task.h"

#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QAction>

#include <widgets/mainwindow.h>
#include <widgets/viewarea.h>
#include <widgets/viewwindow.h>

#include "utils/fileutils.h"
#include "utils/pathutils.h"
#include "taskmgr.h"
#include "vnotex.h"
#include "notebookmgr.h"

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
    auto process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::started,
            this, &Task::started);
    connect(process, &QProcess::errorOccurred,
            this, &Task::errorOccurred);
    connect(process, &QProcess::readyReadStandardOutput,
            this, [process]() {
        qDebug() << process->readAllStandardOutput();
    });
    connect(process, &QProcess::started,
            this, [this]() {
        qDebug() << "task" << label() << "started";
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process]() {
        qDebug() << "task" << label() << "finished";
        process->deleteLater();
    });
    m_command = replaceVariables(m_command);
    qDebug() << "run task" << m_command;
    process->start("cmd /c " + m_command);
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

QString Task::replaceVariables(QString p_cmd)
{
    const auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto notebookFolder = notebookMgr.getCurrentNotebookFolder();

    auto win = VNoteX::getInst().getMainWindow()->getViewArea()->getCurrentViewWindow();
    QString file;
    if (win && win->getBuffer()) {
        file = win->getBuffer()->getPath();
    }
    auto fileInfo = QFileInfo(file);
    auto fileBasename = fileInfo.fileName();
    auto fileBasenameNoExtension = fileInfo.baseName();
    auto fileDirname = fileInfo.dir().absolutePath();
    auto fileExtname = "." + fileInfo.suffix();
    
    p_cmd.replace("${notebookFolder}", notebookFolder);
    p_cmd.replace("${file}", file);
    p_cmd.replace("${fileBasename}", fileBasename);
    p_cmd.replace("${fileBasenameNoExtension}", fileBasenameNoExtension);
    p_cmd.replace("${fileDirname}", fileDirname);
    p_cmd.replace("${fileExtname}", fileExtname);
    return p_cmd;
}

Task::Task(QObject *p_parent)
    : QObject(p_parent)
{
}
