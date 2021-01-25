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
    if (task->m_label.isNull()) {
        task->m_label = QFileInfo(p_file).baseName();
    }
    if (task->m_options_cwd.isNull()) {
        task->m_options_cwd = QFileInfo(p_file).dir().absolutePath();
    }
    return task;
}

Task *Task::fromJson(const QJsonObject &p_obj, QObject *p_parent)
{
    auto task = new Task(p_parent);
    if (p_obj.contains("type")) {
        task->m_type = p_obj["type"].toString();
    }
    
    task->m_command = p_obj["command"].toString();
    
    task->m_label = p_obj["label"].toString();
    
    if (p_obj.contains("args")) {
        task->m_args.clear();
        auto args = p_obj["args"].toArray();
        for (auto arg : args) {
            task->m_args << arg.toString();
        }
    }
    
    if (p_obj.contains("options")) {
        auto options = p_obj["options"].toObject();
        task->m_options_cwd = options["cwd"].toString();
        if (task->m_type == "shell" && options.contains("shell")) {
            auto shell = options["shell"].toObject();
            if (shell.contains("executable")) {
                task->m_options_shell_executable = shell["executable"].toString();
            }
            if (shell.contains("args")) {
                task->m_options_shell_args.clear();
                for (auto arg : shell["args"].toArray()) {
                    task->m_options_shell_args.append(arg.toString());
                }
            }
        }
    }
    auto subTasks = p_obj["tasks"].toArray();
    for (const auto &subTask : subTasks) {
        task->m_subTasks.append(fromJson(subTask.toObject(), task));
    }
    return task;
}

QString Task::getLabel() const
{
    return m_label;
}

QString Task::getFilePath() const
{
    return m_taskFilePath;
}

QString Task::getCommand() const
{
    return replaceVariables(m_command);
}

QStringList Task::getArgs() const
{
    QStringList args;
    for (auto arg : m_args) {
        arg = replaceVariables(arg);
        if (!arg.isEmpty()) args << arg;
    }
    return args;
}

QString Task::getOptions_cwd() const
{
    auto cwd = getCurrentNotebookFolder();
    if (!cwd.isNull()) {
        return cwd;
    }
    cwd = getCurrentFile();
    if (!cwd.isNull()) {
        return QFileInfo(cwd).dir().absolutePath();
    }
    cwd = m_options_cwd;
    return replaceVariables(cwd);
}

void Task::run()
{
    if (m_command.isEmpty()) return ;
    auto process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(getOptions_cwd());
    if (m_type == "shell") {
        process->setProgram(m_options_shell_executable);
        auto args = m_options_shell_args;
        args << getCommand() << getArgs();
        process->setArguments(args);
    } else if (m_type == "process") {
        process->setProgram(getCommand());
        process->setArguments(getArgs());
    }
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
        qDebug() << "task" << getLabel() << "started";
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process]() {
        qDebug() << "task" << getLabel() << "finished";
        process->deleteLater();
    });
    m_command = replaceVariables(m_command);
    qDebug() << "run task" << process->program() << process->arguments();
    process->start();
}

QStringList Task::defaultShellArgs(const QString &shellType)
{
    auto shell = QFileInfo(shellType).baseName().toLower();
    if (shell == "cmd") {
        return {"/D", "/S", "/C"};
    } else if (shell == "powershell") {
        return {"-Command"};
    } else if (shell == "bash") {
        return {"-c"};
    } else {
        return {};
    }
}

QAction *Task::runAction()
{
    auto label = getLabel().replace("&", "&&");
    auto act = new QAction(label, this);
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

QString Task::replaceVariables(const QString &p_cmd)
{
    auto cmd = p_cmd;
    auto notebookFolder = getCurrentNotebookFolder();
    auto file = getCurrentFile();
    auto fileInfo = QFileInfo(file);
    auto fileBasename = fileInfo.fileName();
    auto fileBasenameNoExtension = fileInfo.baseName();
    auto fileDirname = fileInfo.dir().absolutePath();
    auto fileExtname = "." + fileInfo.suffix();
    
    cmd.replace("${notebookFolder}", notebookFolder);
    cmd.replace("${file}", file);
    cmd.replace("${fileBasename}", fileBasename);
    cmd.replace("${fileBasenameNoExtension}", fileBasenameNoExtension);
    cmd.replace("${fileDirname}", fileDirname);
    cmd.replace("${fileExtname}", fileExtname);
    return cmd;
}

QString Task::getCurrentFile()
{
    auto win = VNoteX::getInst().getMainWindow()->getViewArea()->getCurrentViewWindow();
    QString file;
    if (win && win->getBuffer()) {
        file = win->getBuffer()->getPath();
    }
    return file;
}

QString Task::getCurrentNotebookFolder()
{
    const auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    return notebookMgr.getCurrentNotebookFolder();
}

Task::Task(QObject *p_parent)
    : QObject(p_parent)
{
    m_type = "shell";
#ifdef Q_OS_WIN
    m_options_shell_executable = "PowerShell.exe";
#else
    m_options_shell_executable = "/bin/bash";
#endif
    m_options_shell_args = defaultShellArgs(m_options_shell_executable);
}
