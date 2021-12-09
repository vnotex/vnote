#include "taskvariablemgr.h"

#include <QRegularExpression>
#include <QInputDialog>
#include <QApplication>
#include <QRandomGenerator>
#include <QTimeZone>

#include "vnotex.h"
#include "task.h"
#include "taskhelper.h"
#include "shellexecution.h"
#include "configmgr.h"
#include "mainconfig.h"
#include "notebook/notebook.h"
#include <widgets/mainwindow.h>
#include <widgets/dialogs/selectdialog.h>

using namespace vnotex;


TaskVariable::TaskVariable(TaskVariable::Type p_type, 
                           const QString &p_name, 
                           TaskVariable::Func p_func)
    : m_type(p_type), m_name(p_name), m_func(p_func)
{
    
}


TaskVariableMgr::TaskVariableMgr()
    : m_initialized(false)
{
    
}

void TaskVariableMgr::refresh()
{
    init();
}

QString TaskVariableMgr::evaluate(const QString &p_text, 
                                   const Task *p_task) const
{
    auto text = p_text;
    auto eval = [&text](const QString &p_name, std::function<QString()> p_func) {
        auto reg = QRegularExpression(QString(R"(\$\{[\t ]*%1[\t ]*\})").arg(p_name));
        if (text.contains(reg)) {
            text.replace(reg, p_func());
        }
    };
    
    // current notebook variables
    {
        eval("notebookFolder", []() { 
            auto notebook = TaskHelper::getCurrentNotebook();
            if (notebook) {
                return TaskHelper::normalPath(notebook->getRootFolderAbsolutePath()); 
            } else return QString();
        });
        eval("notebookFolderBasename", []() { 
            auto notebook = TaskHelper::getCurrentNotebook();
            if (notebook) {
                auto folder = notebook->getRootFolderAbsolutePath();
                return QDir(folder).dirName(); 
            } else return QString();
        });
        eval("notebookName", []() { 
            auto notebook = TaskHelper::getCurrentNotebook();
            if (notebook) {
                return notebook->getName(); 
            } else return QString();
        });
        eval("notebookName", []() { 
            auto notebook = TaskHelper::getCurrentNotebook();
            if (notebook) {
                return notebook->getDescription(); 
            } else return QString();
        });
    }
    
    // current file variables
    {
        eval("file", []() { 
            return TaskHelper::normalPath(TaskHelper::getCurrentFile()); 
        });
        eval("fileNotebookFolder", []() {
            auto file = TaskHelper::getCurrentFile();
            return TaskHelper::normalPath(TaskHelper::getFileNotebookFolder(file)); 
        });
        eval("relativeFile", []() {
            auto file = TaskHelper::getCurrentFile();
            auto folder = TaskHelper::getFileNotebookFolder(file);
            return QDir(folder).relativeFilePath(file); 
        });
        eval("fileBasename", []() {
            auto file = TaskHelper::getCurrentFile();
            return QFileInfo(file).fileName();
        });
        eval("fileBasename", []() {
            auto file = TaskHelper::getCurrentFile();
            return QFileInfo(file).completeBaseName();
        });
        eval("fileDirname", []() {
            auto file = TaskHelper::getCurrentFile();
            return TaskHelper::normalPath(QFileInfo(file).dir().absolutePath());
        });
        eval("fileExtname", []() {
            auto file = TaskHelper::getCurrentFile();
            return QFileInfo(file).suffix();
        });
        eval("selectedText", []() {
            return TaskHelper::getSelectedText();
        });
        eval("cwd", [p_task]() { 
            return TaskHelper::normalPath(p_task->getOptionsCwd()); 
        });
        eval("taskFile", [p_task]() { 
            return TaskHelper::normalPath(p_task->getFile()); 
        });
        eval("taskDirname", [p_task]() { 
            return TaskHelper::normalPath(QFileInfo(p_task->getFile()).dir().absolutePath());
        });
        eval("execPath", []() { 
            return TaskHelper::normalPath(qApp->applicationFilePath()); 
        });
        eval("pathSeparator", []() { 
            return TaskHelper::getPathSeparator(); 
        });
        eval("notebookTaskFolder", []() { 
            return TaskHelper::normalPath(TaskMgr::getNotebookTaskFolder());
        });
        eval("userTaskFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getUserTaskFolder());
        });
        eval("appTaskFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getAppTaskFolder());
        });
        eval("userThemeFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getUserThemeFolder());
        });
        eval("appThemeFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getAppThemeFolder());
        });
        eval("userDocsFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getUserDocsFolder());
        });
        eval("appDocsFolder", []() { 
            return TaskHelper::normalPath(ConfigMgr::getInst().getAppDocsFolder());
        });
    }
    
    // Magic variables
    {
        auto cDT = QDateTime::currentDateTime();
        for(auto s : {
            "d", "dd", "ddd", "dddd", "M", "MM", "MMM", "MMMM", 
            "yy", "yyyy", "h", "hh", "H", "HH", "m", "mm", 
            "s", "ss", "z", "zzz", "AP", "A", "ap", "a"
        }) eval(QString("magic:%1").arg(s), [s]() {
            return QDateTime::currentDateTime().toString(s);
        }); 
        eval("magic:random", []() {
            return QString::number(QRandomGenerator::global()->generate());
        });
        eval("magic:random_d", []() {
            return QString::number(QRandomGenerator::global()->generate());
        });
        eval("magic:date", []() {
            return QDate::currentDate().toString("yyyy-MM-dd");
        });
        eval("magic:da", []() {
            return QDate::currentDate().toString("yyyyMMdd");
        });
        eval("magic:time", []() {
            return QTime::currentTime().toString("hh:mm:ss");
        });
        eval("magic:datetime", []() {
            return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        });
        eval("magic:dt", []() {
            return QDateTime::currentDateTime().toString("yyyyMMdd hh:mm:ss");
        });
        eval("magic:note", []() {
            auto file = TaskHelper::getCurrentFile();
            return QFileInfo(file).fileName();
        });
        eval("magic:no", []() {
            auto file = TaskHelper::getCurrentFile();
            return QFileInfo(file).completeBaseName();
        });
        eval("magic:t", []() {
            auto dt = QDateTime::currentDateTime();
            return dt.timeZone().displayName(dt, QTimeZone::ShortName);
        });
        eval("magic:w", []() {
            return QString::number(QDate::currentDate().weekNumber());
        });
        eval("magic:att", []() {
            // TODO
            return QString();
        });
    }
    
    // environment variables
    do {
        QMap<QString, QString> map;
        auto list = TaskHelper::getAllSpecialVariables("env", p_text);
        list.erase(std::unique(list.begin(), list.end()), list.end());
        if (list.isEmpty()) break;
        for (const auto &name : list) {
            auto value = QProcessEnvironment::systemEnvironment().value(name);
            map.insert(name, value);
        }
        text = TaskHelper::replaceAllSepcialVariables("env", text, map);
    } while(0);
    
    // config variables
    do {
        const auto config_obj = ConfigMgr::getInst().getConfig().toJson();
        QMap<QString, QString> map;
        auto list = TaskHelper::getAllSpecialVariables("config", p_text);
        if (list.isEmpty()) break;
        list.erase(std::unique(list.begin(), list.end()), list.end());
        for (const auto &name : list) {
            auto value = TaskHelper::evaluateJsonExpr(config_obj, name);
            qDebug() << "insert" << name << value;
            map.insert(name, value);
        }
        text = TaskHelper::replaceAllSepcialVariables("config", text, map);
    } while(0);
    
    // input variables
    text = evaluateInputVariables(text, p_task);
    // shell variables
    text = evaluateShellVariables(text, p_task);
    return text;
}

QStringList TaskVariableMgr::evaluate(const QStringList &p_list, 
                                      const Task *p_task) const
{
    QStringList list;
    for (const auto &s : p_list) {
        list << evaluate(s, p_task);
    }
    return list;
}

void TaskVariableMgr::init()
{
    if (m_initialized) return ;
    m_initialized = true;
    
    add(TaskVariable::FunctionBased,
        "file",
        [](const TaskVariable *, const Task *) {
         return QString();
    });
}

void TaskVariableMgr::add(TaskVariable::Type p_type, 
                          const QString &p_name, 
                          TaskVariable::Func p_func)
{
    m_predefs.insert(p_name, TaskVariable(p_type, p_name, p_func));
}

QString TaskVariableMgr::evaluateInputVariables(const QString &p_text, 
                                                const Task *p_task) const
{
    QMap<QString, QString> map;
    auto list = TaskHelper::getAllSpecialVariables("input", p_text);
    list.erase(std::unique(list.begin(), list.end()), list.end());
    if (list.isEmpty()) return p_text;
    for (const auto &id : list) {
        auto input = p_task->getInput(id);
        QString text;
        auto mainwin = VNoteX::getInst().getMainWindow();
        if (input.type == "promptString") {
            auto desc = evaluate(input.description, p_task);
            auto defaultText = evaluate(input.default_, p_task);
            QInputDialog dialog(mainwin);
            dialog.setInputMode(QInputDialog::TextInput);
            if (input.password) dialog.setTextEchoMode(QLineEdit::Password);
            else dialog.setTextEchoMode(QLineEdit::Normal);
            dialog.setWindowTitle(p_task->getLabel());
            dialog.setLabelText(desc);
            dialog.setTextValue(defaultText);
            if (dialog.exec() == QDialog::Accepted) {
                text = dialog.textValue();
            } else {
                throw "TaskCancle";
            }
        } else if (input.type == "pickString") {
            // TODO: select description
            SelectDialog dialog(p_task->getLabel(), input.description, mainwin);
            for (int i = 0; i < input.options.size(); i++) {
                dialog.addSelection(input.options.at(i), i);
            }
    
            if (dialog.exec() == QDialog::Accepted) {
                int selection = dialog.getSelection();
                text = input.options.at(selection);
            } else {
                throw "TaskCancle";
            }
        }
        map.insert(input.id, text);
    }
    return TaskHelper::replaceAllSepcialVariables("input", p_text, map);
}

QString TaskVariableMgr::evaluateShellVariables(const QString &p_text, 
                                                const Task *p_task) const
{
    QMap<QString, QString> map;
    auto list = TaskHelper::getAllSpecialVariables("shell", p_text);
    list.erase(std::unique(list.begin(), list.end()), list.end());
    if (list.isEmpty()) return p_text;
    for (const auto &cmd : list) {
        QProcess process;
        process.setWorkingDirectory(p_task->getOptionsCwd());
        ShellExecution::setupProcess(&process, cmd);
        process.start();
        if (!process.waitForStarted(1000) || !process.waitForFinished(1000)) {
            throw "Shell variable execution timeout";
        }
        auto res = process.readAllStandardOutput().trimmed();
        map.insert(cmd, res);
    }
    return TaskHelper::replaceAllSepcialVariables("shell", p_text, map);
}

