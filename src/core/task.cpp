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

#include "utils/fileutils.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include <widgets/mainwindow.h>
#include <widgets/viewarea.h>
#include <widgets/viewwindow.h>
#include <widgets/dialogs/selectdialog.h>

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
        p_task->m_command = getLocaleString(p_obj["command"], p_task->m_locale);
    }
    
    if (p_obj.contains("args")) {
        p_task->m_args = getLocaleStringList(p_obj["args"], p_task->m_locale);
    }
    
    if (p_obj.contains("label")) {
        p_task->m_label = getLocaleString(p_obj["label"], p_task->m_locale);
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
            p_task->m_tasks.append(fromJson(t, task.toObject()));
        }
    }
    
    if (p_obj.contains("inputs")) {
        p_task->m_inputs.clear();
        auto inputs = p_obj["inputs"].toArray();
        
        for (const auto &input : inputs) {
            auto in = input.toObject();
            Input i;
            if (in.contains("id")) {
                i.m_id = in["id"].toString();
            } else {
                qWarning() << "Input configuration not contain id";
            }
            
            if (in.contains("type")) {
                i.m_type = in["type"].toString();
            } else {
                i.m_type = "promptString";
            }
            
            if (in.contains("description")) {
                i.m_description = getLocaleString(in["description"], p_task->m_locale);
            }
            
            if (in.contains("default")) {
                i.m_default = getLocaleString(in["default"], p_task->m_locale);
            }
            
            if (i.m_type == "promptString" && in.contains("password")) {
                i.m_password = in["password"].toBool();
            } else {
                i.m_password = false;
            }
            
            if (i.m_type == "pickString" && in.contains("options")) {
                i.m_options = getLocaleStringList(in["options"], p_task->m_locale);
            }
            
            if (i.m_type == "pickString" && !i.m_default.isNull() && !i.m_options.contains(i.m_default)) {
                qWarning() << "default must be one of the option values";
            }
            
            p_task->m_inputs << i;
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

const QVector<Task::Input> &Task::getInputs() const
{
    return m_inputs;
}

Task::Input Task::getInput(const QString &p_id) const
{
    for (auto i : m_inputs) {
        if (i.m_id == p_id) {
            return i;
        }
    }
    qDebug() << getLabel();
    qWarning() << "input" << p_id << "not found";
    throw "Input variable can not found";
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
    m_options_shell_executable = "PowerShell.exe";
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
        // not inherit label/inputs/tasks
    } else {
        m_label = QFileInfo(p_file).baseName();
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
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(getOptionsCwd());
    
    auto args = getArgs();
    auto shell = getShell();
    auto type = getType();
    
    // set program and args
    if (type == "shell") {
        // space quote
        if (!command.isEmpty() && !args.isEmpty()) {
            QString chars = "\"";
            args = spaceQuote(args, chars);
        }
        QStringList allArgs;
        process->setProgram(getOptionsShellExecutable());
        allArgs << getOptionsShellArgs();
        allArgs << command;
        allArgs << args;
        process->setArguments(allArgs);
    } else if (getType() == "process") {
        process->setProgram(command);
        process->setArguments(args);
    }

    // connect signal and slot
    connect(process, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
        emit showOutput(tr("[Task %1 error occurred with code %2]").arg(getLabel(), QString::number(error)));
    });
    connect(process, &QProcess::readyReadStandardOutput,
            this, [process, this]() {
        auto text = process->readAllStandardOutput();
        emit showOutput(textDecode(text));
    });
    connect(process, &QProcess::started,
            this, [this]() {
        emit showOutput(tr("[Task %1 started]\n").arg(getLabel()));
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
        qDebug() << "run task" << process->program() << process->arguments();
        process->start();
    }
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

QString Task::replaceVariables(const QString &p_text) const
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
    cmd.replace("${notebookFolderBasename}", QDir(notebookFolder).dirName());
    cmd.replace("${notebookName}", QDir(notebookFolder).dirName());
    cmd.replace("${file}", normalPath(file));
    cmd.replace("${fileBasename}", fileBasename);
    cmd.replace("${fileBasenameNoExtension}", fileBasenameNoExtension);
    cmd.replace("${fileDirname}", normalPath(fileDirname));
    cmd.replace("${fileExtname}", fileExtname);
    
    // Magic variables
    cmd.replace("${magic:datetime}", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    
    cmd = replaceInputVariables(cmd);
    return cmd;
}

QStringList Task::replaceVariables(const QStringList &p_list) const
{
    QStringList list;
    for (auto s : p_list) {
        s = replaceVariables(s);
        if (!s.isEmpty()) list << s;
    }
    return list;
}

QStringList Task::getAllInputVariables(const QString &p_text) const
{
    QStringList list;
    QRegularExpression re(R"(\$\{input:(.*?)\})");
    auto it = re.globalMatch(p_text);
    while (it.hasNext()) {
        auto match = it.next();
        auto input = getInput(match.captured(1));
        list << input.m_id;
    }
    list.erase(std::unique(list.begin(), list.end()), list.end());
    return list;
}

QMap<QString, QString> Task::evaluateInputVariables(const QString &p_text) const
{
    QMap<QString, QString> map;
    auto list = getAllInputVariables(p_text);
    for (const auto &id : list) {
        auto input = getInput(id);
        QString text;
        auto mainwin = VNoteX::getInst().getMainWindow();
        if (input.m_type == "promptString") {
            auto desc = replaceVariables(input.m_description);
            auto defaultText = replaceVariables(input.m_default);
            QInputDialog dialog(mainwin);
            dialog.setInputMode(QInputDialog::TextInput);
            if (input.m_password) dialog.setTextEchoMode(QLineEdit::Password);
            else dialog.setTextEchoMode(QLineEdit::Normal);
            dialog.setWindowTitle(getLabel());
            dialog.setLabelText(desc);
            dialog.setTextValue(defaultText);
            if (dialog.exec() == QDialog::Accepted) {
                text = dialog.textValue();
            } else {
                throw "TaskCancle";
            }
        } else if (input.m_type == "pickString") {
            // TODO: select description
            SelectDialog dialog(getLabel(), mainwin);
            for (int i = 0; i < input.m_options.size(); i++) {
                qDebug() << "addSelection" << input.m_options.at(i);
                dialog.addSelection(input.m_options.at(i), i);
            }
    
            if (dialog.exec() == QDialog::Accepted) {
                int selection = dialog.getSelection();
                text = input.m_options.at(selection);
            } else {
                throw "TaskCancle";
            }
        }
        map.insert(input.m_id, text);
    }
    return map;
}

QString Task::replaceInputVariables(const QString &p_text) const
{
    auto map = evaluateInputVariables(p_text);
    auto text = p_text;
    for (auto i = map.begin(); i != map.end(); i++) {
        text.replace(QString("${input:%1}").arg(i.key()), i.value());
    }
    return text;
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

QString Task::getLocaleString(const QJsonValue &p_value, const QString &p_locale)
{
    if (p_value.isObject()) {
        auto obj = p_value.toObject();
        if (obj.contains(p_locale)) {
            return obj.value(p_locale).toString();
        } else{
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

QAction *Task::runAction(Task *p_task)
{
    auto label = p_task->getLabel().replace("&", "&&");
    auto act = new QAction(label, p_task);
    connect(act, &QAction::triggered,
            p_task, &Task::run);
    return act;
}
