#include "task.h"

#include <QJsonDocument>
#include <QVersionNumber>
#include <QDebug>
#include <QJsonValue>
#include <QJsonArray>
#include <QAction>
#include <QRegularExpression>
#include <QInputDialog>
#include <QTextCodec>
#include <QRandomGenerator>

#include "utils/fileutils.h"
#include "utils/pathutils.h"
#include "vnotex.h"
#include "exception.h"
#include "taskhelper.h"
#include "notebook/notebook.h"
#include "shellexecution.h"


using namespace vnotex;

QString Task::s_latestVersion = "0.1.3";
TaskVariableMgr Task::s_vars;

Task *Task::fromFile(const QString &p_file, 
                     const QJsonDocument &p_json,
                     const QString &p_locale, 
                     QObject *p_parent)
{
    auto task = new Task(p_locale, p_file, p_parent);
    return fromJson(task, p_json.object());
}

Task* Task::fromJson(Task *p_task,
                     const QJsonObject &p_obj)
{
    if (p_obj.contains("version")) {
        p_task->dto.version = p_obj["version"].toString();
    }
    
    auto version = QVersionNumber::fromString(p_task->getVersion());
    if (version < QVersionNumber(1, 0, 0)) {
        return fromJsonV0(p_task, p_obj);
    }
    qWarning() << "Unknow Task Version" << version << p_task->dto._source;
    return p_task;
}

Task *Task::fromJsonV0(Task *p_task,
                       const QJsonObject &p_obj,
                       bool mergeTasks)
{
    if (p_obj.contains("type")) {
        p_task->dto.type = p_obj["type"].toString();
    }
    
    if (p_obj.contains("icon")) {
        QString path = p_obj["icon"].toString();
        QDir iconPath(path);
        if (iconPath.isRelative()) {
            QDir taskDir(p_task->dto._source);
            taskDir.cdUp();
            path = QDir(taskDir.filePath(path)).absolutePath();
        }
        if (QFile::exists(path)) {
            p_task->dto.icon = path;
        } else {
            qWarning() << "task icon not exists" << path;
        }
    }
    
    if (p_obj.contains("shortcut")) {
        p_task->dto.shortcut = p_obj["shortcut"].toString();
    }
    
    if (p_obj.contains("type")) {
        p_task->dto.type = p_obj["type"].toString();
    }
    
    if (p_obj.contains("command")) {
        p_task->dto.command = getLocaleString(p_obj["command"], p_task->m_locale);
    }
    
    if (p_obj.contains("args")) {
        p_task->dto.args = getLocaleStringList(p_obj["args"], p_task->m_locale);
    }
    
    if (p_obj.contains("label")) {
        p_task->dto.label = getLocaleString(p_obj["label"], p_task->m_locale);
    } else if (p_task->dto.label.isNull() && !p_task->dto.command.isNull()) {
        p_task->dto.label = p_task->dto.command;
    }
    
    if (p_obj.contains("options")) {
        auto options = p_obj["options"].toObject();
        
        if (options.contains("cwd")) {
            p_task->dto.options.cwd = options["cwd"].toString();
        }
        
        if (options.contains("env")) {
            p_task->dto.options.env.clear();
            auto env = options["env"].toObject();
            for (auto i = env.begin(); i != env.end(); i++) {
                auto key = i.key();
                auto value = getLocaleString(i.value(), p_task->m_locale);
                p_task->dto.options.env.insert(key, value);
            }
        }
        
        if (options.contains("shell") && p_task->getType() == "shell") {
            auto shell = options["shell"].toObject();
            
            if (shell.contains("executable")) {
                p_task->dto.options.shell.executable = shell["executable"].toString();
            }
            
            if (shell.contains("args")) {
                p_task->dto.options.shell.args.clear();
                
                for (auto arg : shell["args"].toArray()) {
                    p_task->dto.options.shell.args << arg.toString();
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
            p_task->m_tasks.append(fromJson(t, task.toObject()));
        }
    }
    
    if (p_obj.contains("inputs")) {
        p_task->dto.inputs.clear();
        auto inputs = p_obj["inputs"].toArray();
        
        for (const auto &input : inputs) {
            auto in = input.toObject();
            InputDTO i;
            if (in.contains("id")) {
                i.id = in["id"].toString();
            } else {
                qWarning() << "Input configuration not contain id";
            }
            
            if (in.contains("type")) {
                i.type = in["type"].toString();
            } else {
                i.type = "promptString";
            }
            
            if (in.contains("description")) {
                i.description = getLocaleString(in["description"], p_task->m_locale);
            }
            
            if (in.contains("default")) {
                i.default_ = getLocaleString(in["default"], p_task->m_locale);
            }
            
            if (i.type == "promptString" && in.contains("password")) {
                i.password = in["password"].toBool();
            } else {
                i.password = false;
            }
            
            if (i.type == "pickString" && in.contains("options")) {
                i.options = getLocaleStringList(in["options"], p_task->m_locale);
            }
            
            if (i.type == "pickString" && !i.default_.isNull() && !i.options.contains(i.default_)) {
                qWarning() << "default must be one of the option values";
            }
            
            p_task->dto.inputs << i;
        }
    }
    
    if (p_obj.contains("messages")) {
        p_task->dto.messages.clear();
        auto messages = p_obj["messages"].toArray();
        
        for (const auto &message : messages) {
            auto msg = message.toObject();
            MessageDTO m;
            if (msg.contains("id")) {
                m.id = msg["id"].toString();
            } else {
                qWarning() << "Message configuration not contain id";
            }
            
            if (msg.contains("type")) {
                m.type = msg["type"].toString();
            } else {
                m.type = "information";
            }
            
            if (msg.contains("title")) {
                m.title = getLocaleString(msg["title"], p_task->m_locale);
            }
            
            if (msg.contains("text")) {
                m.text = getLocaleString(msg["text"], p_task->m_locale);
            }
            
            if (msg.contains("detailedText")) {
                m.detailedText = getLocaleString(msg["detailedText"], p_task->m_locale);
            }
            
            if (msg.contains("buttons")) {
                auto buttons = msg["buttons"].toArray();
                for (auto button : buttons) {
                    auto btn = button.toObject();
                    ButtonDTO b;
                    b.text = getLocaleString(btn["text"], p_task->m_locale);
                    m.buttons << b;
                }
            }
            p_task->dto.messages << m;
        }
    }
    
    // OS-specific task configuration
#if defined (Q_OS_WIN)
#define OS_SPEC "windows"
#endif
#if defined (Q_OS_MACOS)
#define OS_SPEC "osx"
#endif
#if defined (Q_OS_LINUX)
#define OS_SPEC "linux"
#endif
    if (p_obj.contains(OS_SPEC)) {
        auto os = p_obj[OS_SPEC].toObject();
        fromJsonV0(p_task, os, true);
    }
#undef OS_SPEC

    return p_task;
}

QString Task::getVersion() const
{
    return dto.version;
}

QString Task::getType() const
{
    return dto.type;
}

QString Task::getCommand() const
{
    return s_vars.evaluate(dto.command, this);
}

QStringList Task::getArgs() const
{
    return s_vars.evaluate(dto.args, this);
}

QString Task::getLabel() const
{
    return dto.label;
}

QString Task::getIcon() const
{
    return dto.icon;
}

QString Task::getShortcut() const
{
    return dto.shortcut;
}

QString Task::getOptionsCwd() const
{
    auto cwd = dto.options.cwd;
    if (!cwd.isNull()) {
        return s_vars.evaluate(cwd, this);
    }
    auto notebook = TaskHelper::getCurrentNotebook();
    if (notebook) cwd = notebook->getRootFolderAbsolutePath();
    if (!cwd.isNull()) {
        return cwd;
    }
    cwd = TaskHelper::getCurrentFile();
    if (!cwd.isNull()) {
        return QFileInfo(cwd).dir().absolutePath();
    }
    return QFileInfo(dto._source).dir().absolutePath();
}

const QMap<QString, QString> &Task::getOptionsEnv() const
{
    return dto.options.env;
}

QString Task::getOptionsShellExecutable() const
{
    return dto.options.shell.executable;
}

QStringList Task::getOptionsShellArgs() const
{
    if (dto.options.shell.args.isEmpty()) {
        return ShellExecution::defaultShellArguments(dto.options.shell.executable);
    } else {
        return s_vars.evaluate(dto.options.shell.args, this);
    }
}    

const QVector<Task *> &Task::getTasks() const
{
    return m_tasks;
}

const QVector<InputDTO> &Task::getInputs() const
{
    return dto.inputs;
}

InputDTO Task::getInput(const QString &p_id) const
{
    for (auto i : dto.inputs) {
        if (i.id == p_id) {
            return i;
        }
    }
    qDebug() << getLabel();
    qWarning() << "input" << p_id << "not found";
    throw "Input variable can not found";
}

MessageDTO Task::getMessage(const QString &p_id) const
{
    for (auto msg : dto.messages) {
        if (msg.id == p_id) {
            return msg;
        }
    }
    qDebug() << getLabel();
    qWarning() << "message" << p_id << "not found";
    throw "Message can not found";
}

QString Task::getFile() const
{
    return dto._source;
}

Task::Task(const QString &p_locale, 
           const QString &p_file,
           QObject *p_parent)
    : QObject(p_parent)
{   
    dto._source = p_file;
    dto.version = s_latestVersion;
    dto.type = "shell";
    dto.options.shell.executable = ShellExecution::defaultShell();
    
    // inherit configuration
    m_parent = qobject_cast<Task*>(p_parent);
    if (m_parent) {
        dto.version = m_parent->dto.version;
        dto.type = m_parent->dto.type;
        dto.command = m_parent->dto.command;
        dto.args = m_parent->dto.args;
        dto.options.cwd = m_parent->dto.options.cwd;
        dto.options.env = m_parent->dto.options.env;
        dto.options.shell.executable = m_parent->dto.options.shell.executable;
        dto.options.shell.args = m_parent->dto.options.shell.args;
        // not inherit label/inputs/tasks
    } else {
        dto.label = QFileInfo(p_file).baseName();
    }
    
    if (!p_locale.isNull()) {
        m_locale = p_locale;
    }
}

QProcess *Task::setupProcess() const
{
    // Set process property
    auto command = getCommand();
    if (command.isEmpty()) return nullptr;
    auto process = new QProcess(this->parent());
    process->setWorkingDirectory(getOptionsCwd());
    
    auto options_env = getOptionsEnv();
    if (!options_env.isEmpty()) {
        auto env = QProcessEnvironment::systemEnvironment();
        for (auto i = options_env.begin(); i != options_env.end(); i++) {
            env.insert(i.key(), i.value());
        }
        process->setProcessEnvironment(env);
    }
    
    auto args = getArgs();
    auto type = getType();
    
    // set program and args
    if (type == "shell") {
        ShellExecution::setupProcess(process,
                                     command,
                                     args,
                                     getOptionsShellExecutable(),
                                     getOptionsShellArgs());
    } else if (getType() == "process") {
        process->setProgram(command);
        process->setArguments(args);
    }

    // connect signal and slot
    connect(process, &QProcess::started,
            this, [this]() {
        emit showOutput(tr("[Task %1 started]\n").arg(getLabel()));
    });
    connect(process, &QProcess::readyReadStandardOutput,
            this, [process, this]() {
        auto text = textDecode(process->readAllStandardOutput());
        text = TaskHelper::handleCommand(text, process, this);
        emit showOutput(text);
    });
    connect(process, &QProcess::readyReadStandardError,
            this, [process, this]() {
        auto text = process->readAllStandardError();
        emit showOutput(textDecode(text));
    });
    connect(process, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
        emit showOutput(tr("[Task %1 error occurred with code %2]\n").arg(getLabel(), QString::number(error)));
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode) {
        emit showOutput(tr("\n[Task %1 finished with exit code %2]\n")
                        .arg(getLabel(), QString::number(exitCode)));
        process->deleteLater();
    });
    return process;
}

void Task::run() const
{
    QProcess *process;
    try {
        process = setupProcess();
    }  catch (const char *msg) {
        qDebug() << msg;
        return ;
    }
    if (process) {
        // start process
        qInfo() << "run task" << process->program() << process->arguments();
        process->start();
    }
}

TaskDTO Task::getDTO() const
{
    return dto;
}

QString Task::textDecode(const QByteArray &p_text)
{
    static QByteArrayList codecNames = {
        "UTF-8",
        "System",
        "UTF-16",
        "GB18030"
    };
    for (auto name : codecNames) {
        auto text = textDecode(p_text, name);
        if (!text.isNull()) return text;
    }
    return p_text;
}

QString Task::textDecode(const QByteArray &p_text, const QByteArray &name)
{
    auto codec = QTextCodec::codecForName(name);
    if (codec) {
        QTextCodec::ConverterState state;
        auto text = codec->toUnicode(p_text.data(), p_text.size(), &state);
        if (state.invalidChars > 0) return QString();
        return text;
    }
    return QString();
}


bool Task::isValidTaskFile(const QString &p_file, 
                           QJsonDocument &p_json)
{
    QFile file(p_file);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QJsonParseError error;
    p_json = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (p_json.isNull()) {
        qDebug() << "load task" << p_file << "failed: " << error.errorString()
                 << "at offset" << error.offset;
        return false;
    }
    
    return true;
}

QString Task::getLocaleString(const QJsonValue &p_value, const QString &p_locale)
{
    if (p_value.isObject()) {
        auto obj = p_value.toObject();
        if (obj.contains(p_locale)) {
            return obj.value(p_locale).toString();
        } else {
            qWarning() << "current locale" << p_locale << "not found";
            if (!obj.isEmpty()){
                return obj.begin().value().toString();
            } else {
                return QString();
            }
        }
    } else {
        return p_value.toString();
    }
}

QStringList Task::getLocaleStringList(const QJsonValue &p_value, const QString &p_locale)
{
    QStringList list;
    for (auto value : p_value.toArray()) {
        list << getLocaleString(value, p_locale);
    }
    return list;
}

QStringList Task::getStringList(const QJsonValue &p_value)
{
    QStringList list;
    for (auto value : p_value.toArray()) {
        list << value.toString();
    }
    return list;
}
