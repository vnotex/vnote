#include "taskvariablemgr.h"

#include <QApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimeZone>

#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <utils/pathutils.h>
#include <vxcore/notebook_json_keys.h>

#include "itaskcontext.h"
#include "shellexecution.h"
#include "task.h"
#include "taskservice.h"

using namespace vnotex;

TaskVariable::TaskVariable(const QString &p_name, const Func &p_func)
    : m_name(p_name), m_func(p_func) {}

QString TaskVariable::evaluate(Task *p_task, const QString &p_value) const {
  return m_func(p_task, p_value);
}

const QString TaskVariableMgr::c_variableSymbolRegExp =
    QString(R"(\$\{([^${}:]+)(?::([^${}:]+))?\})");

TaskVariableMgr::TaskVariableMgr(TaskService *p_taskService) : m_taskService(p_taskService) {}

ITaskContext *TaskVariableMgr::context() const {
  return m_taskService ? m_taskService->taskContext() : nullptr;
}

void TaskVariableMgr::init() { initVariables(); }

void TaskVariableMgr::initVariables() {
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

void TaskVariableMgr::initNotebookVariables() {
  addVariable("notebookFolder", [this](Task *, const QString &) {
    const auto root = m_taskService ? m_taskService->currentNotebookRootFolder() : QString();
    return root.isEmpty() ? QString() : PathUtils::cleanPath(root);
  });
  addVariable("notebookFolderName", [this](Task *, const QString &) {
    const auto root = m_taskService ? m_taskService->currentNotebookRootFolder() : QString();
    return root.isEmpty() ? QString() : PathUtils::dirName(root);
  });
  addVariable("notebookName", [this](Task *, const QString &) {
    auto *ctx = context();
    auto *nbService = m_taskService ? m_taskService->notebookService() : nullptr;
    if (ctx && nbService) {
      const auto id = ctx->currentNotebookId();
      if (!id.isEmpty()) {
        return nbService->getNotebookConfig(id)
            .value(QLatin1String(vxcore::kJsonKeyName))
            .toString();
      }
    }
    return QString();
  });
  addVariable("notebookDescription", [this](Task *, const QString &) {
    auto *ctx = context();
    auto *nbService = m_taskService ? m_taskService->notebookService() : nullptr;
    if (ctx && nbService) {
      const auto id = ctx->currentNotebookId();
      if (!id.isEmpty()) {
        return nbService->getNotebookConfig(id)
            .value(QLatin1String(vxcore::kJsonKeyDescription))
            .toString();
      }
    }
    return QString();
  });
}

void TaskVariableMgr::initBufferVariables() {
  addVariable("buffer", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return PathUtils::cleanPath(path);
      }
    }
    return QString();
  });
  addVariable("bufferNotebookFolder", [this](Task *, const QString &) {
    auto *ctx = context();
    auto *nbService = m_taskService ? m_taskService->notebookService() : nullptr;
    if (ctx && nbService) {
      const auto id = ctx->currentBufferNotebookId();
      if (!id.isEmpty()) {
        const auto root = nbService->getNotebookConfig(id)
                              .value(QLatin1String(vxcore::kJsonKeyRootFolder))
                              .toString();
        if (!root.isEmpty()) {
          return PathUtils::cleanPath(root);
        }
      }
    }
    return QString();
  });
  addVariable("bufferRelativePath", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto relPath = ctx->currentBufferRelativePath();
      if (!relPath.isEmpty()) {
        return PathUtils::cleanPath(relPath);
      }
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return PathUtils::cleanPath(path);
      }
    }
    return QString();
  });
  addVariable("bufferName", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return PathUtils::fileName(path);
      }
    }
    return QString();
  });
  addVariable("bufferBaseName", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return QFileInfo(path).completeBaseName();
      }
    }
    return QString();
  });
  addVariable("bufferDir", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return PathUtils::parentDirPath(path);
      }
    }
    return QString();
  });
  addVariable("bufferExt", [this](Task *, const QString &) {
    auto *ctx = context();
    if (ctx) {
      const auto path = ctx->currentBufferPath();
      if (!path.isEmpty()) {
        return QFileInfo(path).suffix();
      }
    }
    return QString();
  });
  addVariable("selectedText", [this](Task *, const QString &) {
    auto *ctx = context();
    return ctx ? ctx->selectedText() : QString();
  });
}

void TaskVariableMgr::initTaskVariables() {
  addVariable("cwd", [](Task *task, const QString &) {
    return PathUtils::cleanPath(task->getOptionsCwd());
  });
  addVariable("taskFile",
              [](Task *task, const QString &) { return PathUtils::cleanPath(task->getFile()); });
  addVariable("taskDir", [](Task *task, const QString &) {
    return PathUtils::parentDirPath(task->getFile());
  });
  addVariable("exeFile", [](Task *, const QString &) {
    return PathUtils::cleanPath(qApp->applicationFilePath());
  });
  addVariable("pathSeparator", [](Task *, const QString &) { return QDir::separator(); });
  addVariable("notebookTaskFolder", [this](Task *, const QString &) {
    const auto folder = m_taskService ? m_taskService->getNotebookTaskFolder() : QString();
    return folder.isEmpty() ? QString() : PathUtils::cleanPath(folder);
  });
  addVariable("taskFolder", [this](Task *, const QString &) {
    auto *configMgr = m_taskService ? m_taskService->configMgr() : nullptr;
    if (configMgr) {
      return PathUtils::cleanPath(configMgr->getConfigDataFolder(ConfigMgr2::Tasks));
    }
    return QString();
  });
  addVariable("themeFolder", [this](Task *, const QString &) {
    auto *configMgr = m_taskService ? m_taskService->configMgr() : nullptr;
    if (configMgr) {
      return PathUtils::cleanPath(configMgr->getConfigDataFolder(ConfigMgr2::Themes));
    }
    return QString();
  });
}

void TaskVariableMgr::initMagicVariables() {
  addVariable("magic", [this](Task *, const QString &val) {
    if (val.isEmpty()) {
      return QString();
    }

    auto *snippetService = m_taskService ? m_taskService->snippetService() : nullptr;
    if (!snippetService) {
      qWarning() << "cannot resolve ${magic:} variable: no snippet service";
      return QString();
    }
    // The new SnippetCoreService only exposes applySnippetBySymbol(content),
    // which resolves %name% symbols in arbitrary text. Legacy per-buffer
    // override injection (SnippetMgr::generateOverrides) is not available here;
    // pass the snippet symbol directly.
    return snippetService->applySnippetBySymbol(QLatin1Char('%') + val + QLatin1Char('%'));
  });
}

void TaskVariableMgr::initEnvironmentVariables() {
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

void TaskVariableMgr::initConfigVariables() {
  // ${config:main.core.shortcuts.FullScreen}.
  addVariable("config", [this](Task *, const QString &val) {
    if (val.isEmpty()) {
      return QString();
    }
    auto *configMgr = m_taskService ? m_taskService->configMgr() : nullptr;
    if (!configMgr) {
      return QString();
    }
    auto jsonVal = configMgr->parseAndReadConfig(val);
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

void TaskVariableMgr::initInputVariables() {
  // ${input:inputId}.
  addVariable("input", [this](Task *task, const QString &val) {
    if (val.isEmpty()) {
      Exception::throwOne(Exception::Type::InvalidArgument,
                          QStringLiteral("task (%1) with empty input id").arg(task->getLabel()));
    }

    auto input = task->findInput(val);
    if (!input) {
      Exception::throwOne(
          Exception::Type::InvalidArgument,
          QStringLiteral("task (%1) with invalid input id (%2)").arg(task->getLabel(), val));
    }

    if (input->type == "promptString") {
      auto *ctx = context();
      if (!ctx) {
        // No UI context yet: cannot prompt, cancel the task.
        task->setCancelled(true);
        return QString();
      }
      const auto desc = evaluate(task, input->description);
      const auto defaultText = evaluate(task, input->default_);
      bool cancelled = false;
      const auto result =
          ctx->promptString(task->getLabel(), desc, defaultText, input->password, cancelled);
      if (cancelled) {
        task->setCancelled(true);
        return QString();
      }
      return result;
    } else if (input->type == "pickString") {
      // TODO: migrate pickString to a UI-provided selection dialog. Until then,
      // cancel the task (matches the prior stubbed behavior).
      task->setCancelled(true);
      return QString();
    } else {
      Exception::throwOne(Exception::Type::InvalidArgument,
                          QStringLiteral("task (%1) with invalid input type (%2)(%3)")
                              .arg(task->getLabel(), input->id, input->type));
    }

    return QString();
  });
}

void TaskVariableMgr::initShellVariables() {
  // ${shell:command}.
  addVariable("shell", [this](Task *task, const QString &val) {
    QProcess process;
    process.setWorkingDirectory(task->getOptionsCwd());
    ShellExecution::setupProcess(&process, val);
    process.start();
    const int timeout = 1000;
    if (!process.waitForStarted(timeout) || !process.waitForFinished(timeout)) {
      Exception::throwOne(Exception::Type::InvalidArgument,
                          QStringLiteral("task (%1) failed to fetch shell variable (%2)")
                              .arg(task->getLabel(), val));
    }
    return Task::decodeText(process.readAllStandardOutput());
  });
}

void TaskVariableMgr::addVariable(const QString &p_name, const TaskVariable::Func &p_func) {
  Q_ASSERT(!m_variables.contains(p_name));

  m_variables.insert(p_name, TaskVariable(p_name, p_func));
}

QString TaskVariableMgr::evaluate(Task *p_task, const QString &p_text) const {
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

QStringList TaskVariableMgr::evaluate(Task *p_task, const QStringList &p_texts) const {
  QStringList strs;
  for (const auto &str : p_texts) {
    strs << evaluate(p_task, str);
  }
  return strs;
}

const TaskVariable *TaskVariableMgr::findVariable(const QString &p_name) const {
  auto it = m_variables.find(p_name);
  if (it != m_variables.end()) {
    return &(it.value());
  }

  return nullptr;
}

void TaskVariableMgr::overrideVariable(const QString &p_name, const TaskVariable::Func &p_func) {
  m_variables.insert(p_name, TaskVariable(p_name, p_func));
}
