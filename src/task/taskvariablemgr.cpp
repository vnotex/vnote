#include "taskvariablemgr.h"

#include <QRegularExpression>
#include <QInputDialog>
#include <QApplication>
#include <QRandomGenerator>
#include <QTimeZone>
#include <QProcess>

#include <core/vnotex.h>
#include <core/notebookmgr.h>
#include <core/configmgr.h>
#include <core/mainconfig.h>
#include <core/exception.h>
#include <widgets/mainwindow.h>
#include <widgets/dialogs/selectdialog.h>
#include <notebook/notebook.h>
#include <notebook/node.h>
#include <utils/pathutils.h>
#include <buffer/buffer.h>
#include <widgets/viewwindow.h>
#include <widgets/viewarea.h>
#include <snippet/snippetmgr.h>

#include "task.h"
#include "taskmgr.h"
#include "shellexecution.h"

using namespace vnotex;


TaskVariable::TaskVariable(const QString &p_name, const Func &p_func)
    : m_name(p_name),
      m_func(p_func)
{
}

QString TaskVariable::evaluate(Task *p_task, const QString &p_value) const
{
    return m_func(p_task, p_value);
}


const QString TaskVariableMgr::c_variableSymbolRegExp = QString(R"(\$\{([^${}:]+)(?::([^${}:]+))?\})");

TaskVariableMgr::TaskVariableMgr(TaskMgr *p_taskMgr)
    : m_taskMgr(p_taskMgr)
{
}

void TaskVariableMgr::init()
{
    initVariables();
}

void TaskVariableMgr::initVariables()
{
    m_variables.clear();

    m_needUpdateSystemEnvironment = true;

    initNotebookVariables();

    initBufferVariables();

    initTaskVariables();

    initMagicVariables();

    initEnvironmentVariables();

    initConfigVariables();

    initInputVariables();

    initShellVariables();
}

void TaskVariableMgr::initNotebookVariables()
{
    addVariable("notebookFolder", [](Task *, const QString &) {
        auto notebook = TaskVariableMgr::getCurrentNotebook();
        if (notebook) {
            return PathUtils::cleanPath(notebook->getRootFolderAbsolutePath());
        } else {
            return QString();
        }
    });
    addVariable("notebookFolderName", [](Task *, const QString &) {
        auto notebook = TaskVariableMgr::getCurrentNotebook();
        if (notebook) {
            return PathUtils::dirName(notebook->getRootFolderPath());
        } else {
            return QString();
        }
    });
    addVariable("notebookName", [](Task *, const QString &) {
        auto notebook = TaskVariableMgr::getCurrentNotebook();
        if (notebook) {
            return notebook->getName();
        } else {
            return QString();
        }
    });
    addVariable("notebookDescription", [](Task *, const QString &) {
        auto notebook = TaskVariableMgr::getCurrentNotebook();
        if (notebook) {
            return notebook->getDescription();
        } else {
            return QString();
        }
    });
}

void TaskVariableMgr::initBufferVariables()
{
    addVariable("buffer", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            return PathUtils::cleanPath(buffer->getPath());
        }
        return QString();
    });
    addVariable("bufferNotebookFolder", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            auto node = buffer->getNode();
            if (node) {
                return PathUtils::cleanPath(node->getNotebook()->getRootFolderAbsolutePath());
            }
        }
        return QString();
    });
    addVariable("bufferRelativePath", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            auto node = buffer->getNode();
            if (node) {
                return PathUtils::cleanPath(node->fetchPath());
            } else {
                return PathUtils::cleanPath(buffer->getPath());
            }
        }
        return QString();
    });
    addVariable("bufferName", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            return PathUtils::fileName(buffer->getPath());
        }
        return QString();
    });
    addVariable("bufferBaseName", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            return QFileInfo(buffer->getPath()).completeBaseName();
        }
        return QString();
    });
    addVariable("bufferDir", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            return PathUtils::parentDirPath(buffer->getPath());
        }
        return QString();
    });
    addVariable("bufferExt", [](Task *, const QString &) {
        auto buffer = getCurrentBuffer();
        if (buffer) {
            return QFileInfo(buffer->getPath()).suffix();
        }
        return QString();
    });
    addVariable("selectedText", [](Task *, const QString &) {
        auto win = getCurrentViewWindow();
        if (win) {
            return win->selectedText();
        }
        return QString();
    });
}

void TaskVariableMgr::initTaskVariables()
{
    addVariable("cwd", [](Task *task, const QString &) {
        return PathUtils::cleanPath(task->getOptionsCwd());
    });
    addVariable("taskFile", [](Task *task, const QString &) {
        return PathUtils::cleanPath(task->getFile());
    });
    addVariable("taskDir", [](Task *task, const QString &) {
        return PathUtils::parentDirPath(task->getFile());
    });
    addVariable("exeFile", [](Task *, const QString &) {
        return PathUtils::cleanPath(qApp->applicationFilePath());
    });
    addVariable("pathSeparator", [](Task *, const QString &) {
        return QDir::separator();
    });
    addVariable("notebookTaskFolder", [this](Task *, const QString &) {
        return PathUtils::cleanPath(m_taskMgr->getNotebookTaskFolder());
    });
    addVariable("userTaskFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getUserTaskFolder());
    });
    addVariable("appTaskFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getAppTaskFolder());
    });
    addVariable("userThemeFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getUserThemeFolder());
    });
    addVariable("appThemeFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getAppThemeFolder());
    });
    addVariable("userDocsFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getUserDocsFolder());
    });
    addVariable("appDocsFolder", [](Task *, const QString &) {
        return PathUtils::cleanPath(ConfigMgr::getInst().getAppDocsFolder());
    });
}

void TaskVariableMgr::initMagicVariables()
{
    addVariable("magic", [](Task *, const QString &val) {
        if (val.isEmpty()) {
            return QString();
        }

        auto overrides = SnippetMgr::generateOverrides(getCurrentBuffer());
        return SnippetMgr::getInst().applySnippetBySymbol(SnippetMgr::generateSnippetSymbol(val), overrides);
    });
}

void TaskVariableMgr::initEnvironmentVariables()
{
    addVariable("env", [this](Task *, const QString &val) {
        if (val.isEmpty()) {
            return QString();
        }
        if (m_needUpdateSystemEnvironment) {
            m_needUpdateSystemEnvironment = false;
            m_systemEnv = QProcessEnvironment::systemEnvironment();
        }
        return m_systemEnv.value(val);
    });
}

void TaskVariableMgr::initConfigVariables()
{
    // ${config:main.core.shortcuts.FullScreen}.
    addVariable("config", [](Task *, const QString &val) {
        if (val.isEmpty()) {
            return QString();
        }
        auto jsonVal = ConfigMgr::getInst().parseAndReadConfig(val);
        switch (jsonVal.type()) {
        case QJsonValue::Bool:
            return jsonVal.toBool() ? QStringLiteral("1") : QStringLiteral("0");
        break;

        case QJsonValue::Double:
            return QString::number(jsonVal.toDouble());
        break;

        case QJsonValue::String:
            return jsonVal.toString();
        break;

        default:
            return QString();
        }
    });
}

void TaskVariableMgr::initInputVariables()
{
    // ${input:inputId}.
    addVariable("input", [this](Task *task, const QString &val) {
        if (val.isEmpty()) {
            Exception::throwOne(Exception::Type::InvalidArgument,
                                QString("task (%1) with empty input id").arg(task->getLabel()));
        }

        auto input = task->findInput(val);
        if (!input) {
            Exception::throwOne(Exception::Type::InvalidArgument,
                                QString("task (%1) with invalid input id (%2)").arg(task->getLabel(), val));
        }

        if (input->type == "promptString") {
            const auto desc = evaluate(task, input->description);
            const auto defaultText = evaluate(task, input->default_);
            QInputDialog dialog(VNoteX::getInst().getMainWindow());
            dialog.setInputMode(QInputDialog::TextInput);
            dialog.setTextEchoMode(input->password ? QLineEdit::Password : QLineEdit::Normal);
            dialog.setWindowTitle(task->getLabel());
            dialog.setLabelText(desc);
            dialog.setTextValue(defaultText);
            if (dialog.exec() == QDialog::Accepted) {
                return dialog.textValue();
            } else {
                task->setCancelled(true);
                return QString();
            }
        } else if (input->type == "pickString") {
            const auto desc = evaluate(task, input->description);
            SelectDialog dialog(task->getLabel(), desc, VNoteX::getInst().getMainWindow());
            for (int i = 0; i < input->options.size(); i++) {
                dialog.addSelection(input->options.at(i), i);
            }

            if (dialog.exec() == QDialog::Accepted) {
                int selection = dialog.getSelection();
                return input->options.at(selection);
            } else {
                task->setCancelled(true);
                return QString();
            }
        } else {
            Exception::throwOne(Exception::Type::InvalidArgument,
                                QString("task (%1) with invalid input type (%2)(%3)").arg(task->getLabel(), input->id, input->type));
        }

        return QString();
    });
}

void TaskVariableMgr::initShellVariables()
{
    // ${shell:command}.
    addVariable("shell", [this](Task *task, const QString &val) {
        QProcess process;
        process.setWorkingDirectory(task->getOptionsCwd());
        ShellExecution::setupProcess(&process, val);
        process.start();
        const int timeout = 1000;
        if (!process.waitForStarted(timeout) || !process.waitForFinished(timeout)) {
            Exception::throwOne(Exception::Type::InvalidArgument,
                                QString("task (%1) failed to fetch shell variable (%2)").arg(task->getLabel(), val));
        }
        return Task::decodeText(process.readAllStandardOutput());
    });
}

void TaskVariableMgr::addVariable(const QString &p_name, const TaskVariable::Func &p_func)
{
    Q_ASSERT(!m_variables.contains(p_name));

    m_variables.insert(p_name, TaskVariable(p_name, p_func));
}

const ViewWindow *TaskVariableMgr::getCurrentViewWindow()
{
    return VNoteX::getInst().getMainWindow()->getViewArea()->getCurrentViewWindow();
}

Buffer *TaskVariableMgr::getCurrentBuffer()
{
    auto win = getCurrentViewWindow();
    if (win) {
        return win->getBuffer();
    }
    return nullptr;
}

QSharedPointer<Notebook> TaskVariableMgr::getCurrentNotebook()
{
    return VNoteX::getInst().getNotebookMgr().getCurrentNotebook();
}

QString TaskVariableMgr::evaluate(Task *p_task, const QString &p_text) const
{
    QString content(p_text);

    int maxTimesAtSamePos = 100;

    QRegularExpression regExp(c_variableSymbolRegExp);
    int pos = 0;
    while (pos < content.size()) {
        QRegularExpressionMatch match;
        int idx = content.indexOf(regExp, pos, &match);
        if (idx == -1) {
            break;
        }

        const auto varName = match.captured(1).trimmed();
        const auto varValue = match.captured(2).trimmed();
        auto var = findVariable(varName);
        if (!var) {
            // Skip it.
            pos = idx + match.capturedLength(0);
            continue;
        }

        const auto afterText = var->evaluate(p_task, varValue);
        content.replace(idx, match.capturedLength(0), afterText);

        // @afterText may still contains variable symbol.
        if (pos == idx) {
            if (--maxTimesAtSamePos == 0) {
                break;
            }
        } else {
            maxTimesAtSamePos = 100;
        }
        pos = idx;
    }

    return content;
}

QStringList TaskVariableMgr::evaluate(Task *p_task, const QStringList &p_texts) const
{
    QStringList strs;
    for (const auto &str : p_texts) {
        strs << evaluate(p_task, str);
    }
    return strs;
}

const TaskVariable *TaskVariableMgr::findVariable(const QString &p_name) const
{
    auto it = m_variables.find(p_name);
    if (it != m_variables.end()) {
        return &(it.value());
    }

    return nullptr;
}

void TaskVariableMgr::overrideVariable(const QString &p_name, const TaskVariable::Func &p_func)
{
    m_variables.insert(p_name, TaskVariable(p_name, p_func));
}
