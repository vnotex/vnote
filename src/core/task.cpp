#include "task.h"

#include <QJsonDocument>
#include <QVersionNumber>
#include <QDebug>
#include <QJsonValue>
#include <QJsonArray>
#include <QAction>

#include "utils/fileutils.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include <widgets/mainwindow.h>
#include <widgets/viewarea.h>
#include <widgets/viewwindow.h>

using namespace vnotex;

QString Task::s_latestVersion = "0.1.3";

Task *Task::fromFile(const QString &p_file, 
                     const QString &p_locale, 
                     QObject *p_parent)
{
    auto task = new Task(p_locale, p_file, p_parent);
    return fromJson(task, readTaskFile(p_file));
}

Task* Task::fromJson(Task *p_task,
                     const QJsonObject &p_obj)
{
    if (p_obj.contains("version")) {
        p_task->m_version = p_obj["version"].toString();
    }
    
    auto version = QVersionNumber::fromString(p_task->getVersion());
    if (version < QVersionNumber(1, 0, 0)) {
        return fromJsonV0(p_task, p_obj);
    }
    qWarning() << "Unknow Task Version" << version << p_task->m_file;
    return p_task;
}

Task *Task::fromJsonV0(Task *p_task, 
                       const QJsonObject &p_obj,
                       bool mergeTasks)
{
    if (p_obj.contains("type")) {
        p_task->m_type = p_obj["type"].toString();
    }
    
    if (p_obj.contains("command")) {
        p_task->m_command = p_obj["command"].toString();
    }
    
    if (p_obj.contains("args")) {
        p_task->m_args.clear();
        auto args = p_obj["args"].toArray();
        for (auto arg : args) {
            p_task->m_args << arg.toString();
        }
    }
    
    if (p_obj.contains("label")) {
        auto label = p_obj["label"];
        if (label.isObject()) {
            p_task->m_label = label.toObject().value(p_task->m_locale).toString();
        } else {
            p_task->m_label = label.toString();
        }
    } else if (p_task->m_label.isNull() && !p_task->m_command.isNull()) {
        p_task->m_label = p_task->m_command;
    }
    
    if (p_obj.contains("options")) {
        auto options = p_obj["options"].toObject();
        
        if (options.contains("cwd")) {
            p_task->m_options_cwd = options["cwd"].toString();
        }
        
        if (options.contains("env")) {
            p_task->m_options_env.clear();
            auto env = options["env"].toObject();
            for (auto i = env.begin(); i != env.end(); i++) {
                auto key = i.key();
                auto value = i.value().toString();
                p_task->m_options_env.insert(key, value);
            }
        }
        
        if (options.contains("shell") && p_task->getType() == "shell") {
            auto shell = options["shell"].toObject();
            
            if (shell.contains("executable")) {
                p_task->m_options_shell_executable = shell["executable"].toString();
            }
            
            if (shell.contains("args")) {
                p_task->m_options_shell_args.clear();
                
                for (auto arg : shell["args"].toArray()) {
                    p_task->m_options_shell_args << arg.toString();
                }
            }
        }
    }
    
    if (p_obj.contains("tasks")) {
        if (!mergeTasks) p_task->m_tasks.clear();
        auto tasks = p_obj["tasks"].toArray();
        
        for (const auto &task : tasks) {
            auto t = new Task(p_task->m_locale,
                              p_task->getFile(),
                              p_task);
            p_task->m_tasks.append(fromJsonV0(t, task.toObject()));
        }
    }
    
    // OS-specific task configuration
    QString os_str;
#if defined (Q_OS_WIN)
    os_str = "windows";
#endif
#if defined (Q_OS_MACOS)
    os_str = "osx";
#endif
#if defined (Q_OS_LINUX)
    os_str = "linux";
#endif
    if (p_obj.contains(os_str)) {
        auto os = p_obj[os_str].toObject();
        fromJsonV0(p_task, os, true);
    }

    return p_task;
}

QString Task::getVersion() const
{
    return m_version;
}

QString Task::getType() const
{
    return m_type;
}

QString Task::getCommand() const
{
    return replaceVariables(m_command);
}

QStringList Task::getArgs() const
{
    return replaceVariables(m_args);
}

QString Task::getLabel() const
{
    return m_label;
}

QString Task::getOptionsCwd() const
{
    auto cwd = m_options_cwd;
    if (!cwd.isNull()) {
        return replaceVariables(cwd);
    }
    cwd = getCurrentNotebookFolder();
    if (!cwd.isNull()) {
        return cwd;
    }
    cwd = getCurrentFile();
    if (!cwd.isNull()) {
        return QFileInfo(cwd).dir().absolutePath();
    }
    return QFileInfo(m_file).dir().absolutePath();
}

const QMap<QString, QString> &Task::getOptionsEnv() const
{
    return m_options_env;
}

QString Task::getShell() const
{
    return QFileInfo(m_options_shell_executable).baseName().toLower();
}

QString Task::getOptionsShellExecutable() const
{
    return m_options_shell_executable;
}

QStringList Task::getOptionsShellArgs() const
{
    if (m_options_shell_args.isEmpty()) {
        return defaultShellArgs(getShell());
    } else {
        return replaceVariables(m_options_shell_args);
    }
}    

const QVector<Task *> &Task::getTasks() const
{
    return m_tasks;
}

QString Task::getFile() const
{
    return m_file;
}

Task::Task(const QString &p_locale, 
           const QString &p_file,
           QObject *p_parent)
    : QObject(p_parent)
{
    m_file = p_file;
    m_version = s_latestVersion;
    m_type = "shell";
#ifdef Q_OS_WIN
    m_options_shell_executable = "cmd.exe";
#else
    m_options_shell_executable = "/bin/bash";
#endif
    
    // inherit configuration
    m_parent = qobject_cast<Task*>(p_parent);
    if (m_parent) {
        m_version = m_parent->m_version;
        m_type = m_parent->m_type;
        m_command = m_parent->m_command;
        m_args = m_parent->m_args;
        m_options_cwd = m_parent->m_options_cwd;
        m_options_env = m_parent->m_options_env;
        m_options_shell_executable = m_parent->m_options_shell_executable;
        m_options_shell_args = m_parent->m_options_shell_args;
        // not inherit label and tasks
    } else {
        m_label = QFileInfo(p_file).baseName();
    }
    
    if (!p_locale.isNull()) {
        m_locale = p_locale;
    }
}

void Task::run() const
{
    // Set process property
    auto command = getCommand();
    if (command.isEmpty()) return ;
    auto process = new QProcess(this->parent());
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(getOptionsCwd());
    
    // space quote
    auto args = getArgs();
    auto shell = getShell();
    if (!command.isEmpty() && !args.isEmpty()) {
        QString chars = "\"";
        if (shell == "powershell") chars = "\\\"";
        command = spaceQuote(command, chars);
        args = spaceQuote(args, chars);
    }
    
    // set program and args
    auto type = getType();
    if (type == "shell") {
        auto cmd = QString("%1 %2").arg(getOptionsShellExecutable(),
                                        getOptionsShellArgs().join(' '));
        if (shell == "powershell") {
            auto cmd_in = QString("%1 %2").arg(command, args.join(' '));
            // escape powershell special character
            cmd_in.replace("\"", "\\\"");
            cmd += QString(" \'%1\'").arg(cmd_in);
        } else {
            cmd += QString(" %1 %2").arg(command, args.join(' '));
        }
        process->setProgram(cmd);
    } else if (getType() == "process") {
        process->setProgram(command);
        process->setArguments(args);
    }

    // connect signal and slot
    connect(process, &QProcess::errorOccurred,
            this, [](QProcess::ProcessError error) {
        qDebug() << "error" << error;
    });
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
    
    // start process
    qDebug() << "run task" << process->program() << process->arguments();
    process->start();
}


QJsonObject Task::readTaskFile(const QString &p_file)
{
    auto bytes = FileUtils::readFile(p_file);
    return QJsonDocument::fromJson(bytes).object();
}

QStringList Task::defaultShellArgs(const QString &p_shell)
{
    if (p_shell == "cmd") {
        return {"/C"};
    } else if (p_shell == "powershell") {
        return {"-Command"};
    } else if (p_shell == "bash") {
        return {"-c"};
    }
    return {};
}

QString Task::normalPath(const QString &p_path)
{
    auto path = p_path;
#if defined (Q_OS_WIN)
    path.replace('/', '\\');
#endif
    return path;    
}

QString Task::spaceQuote(const QString &p_text, const QString &p_chars)
{
    if (p_text.contains(' ')) return p_chars + p_text + p_chars;
    return p_text;
}

QStringList Task::spaceQuote(const QStringList &p_list, const QString &p_chars)
{
    QStringList list;
    for (auto s : p_list) {
        list << spaceQuote(s, p_chars);
    }
    return list;
}

QString Task::replaceVariables(const QString &p_text)
{
    auto cmd = p_text;
    auto notebookFolder = getCurrentNotebookFolder();
    auto file = getCurrentFile();
    auto fileInfo = QFileInfo(file);
    auto fileBasename = fileInfo.fileName();
    auto fileBasenameNoExtension = fileInfo.baseName();
    auto fileDirname = fileInfo.dir().absolutePath();
    auto fileExtname = "." + fileInfo.suffix();
    
    cmd.replace("${notebookFolder}", notebookFolder);
    cmd.replace("${file}", normalPath(file));
    cmd.replace("${fileBasename}", fileBasename);
    cmd.replace("${fileBasenameNoExtension}", fileBasenameNoExtension);
    cmd.replace("${fileDirname}", normalPath(fileDirname));
    cmd.replace("${fileExtname}", fileExtname);
    return cmd;
}

QStringList Task::replaceVariables(const QStringList &p_list)
{
    QStringList list;
    for (auto s : p_list) {
        s = replaceVariables(s);
        if (!s.isEmpty()) list << s;
    }
    return list;
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

bool Task::isValidTaskFile(const QString &p_file)
{
    QFile file(p_file);
    if (!file.exists()) {
        qWarning() << "task file does not exist: " << p_file;
        return false;
    }
    
    return true;
}

QAction *Task::runAction(Task *p_task)
{
    auto label = p_task->getLabel().replace("&", "&&");
    auto act = new QAction(label, p_task);
    connect(act, &QAction::triggered,
            p_task, &Task::run);
    return act;
}
