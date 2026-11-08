#include "taskservice.h"

#include <QDebug>
#include <QDir>
#include <QJsonObject>
#include <QProcess>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/services/notebookcoreservice.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <vxcore/notebook_json_keys.h>

#include "itaskcontext.h"

using namespace vnotex;

TaskService::TaskService(ConfigMgr2 *p_configMgr, NotebookCoreService *p_notebookService,
                         SnippetCoreService *p_snippetService, ITaskContext *p_context,
                         QObject *p_parent)
    : QObject(p_parent), m_configMgr(p_configMgr), m_notebookService(p_notebookService),
      m_snippetService(p_snippetService), m_context(p_context), m_variableMgr(this) {}

void TaskService::init() { m_variableMgr.init(); }

void TaskService::reload() { loadAllTasks(); }

namespace {
// True if p_task is p_root or a descendant of p_root.
bool subtreeContains(Task *p_root, Task *p_task) {
  if (p_root == p_task) {
    return true;
  }
  for (auto *child : p_root->getChildren()) {
    if (subtreeContains(child, p_task)) {
      return true;
    }
  }
  return false;
}

QString notebookTaskFolder(const QJsonObject &p_config) {
  // Only bundled notebooks have a per-notebook config folder (vx_notebook).
  // Raw notebooks have no notebook-root config path, so there is no task
  // folder to derive.
  if (p_config.value(QLatin1String(vxcore::kJsonKeyType)).toString() != QStringLiteral("bundled")) {
    return QString();
  }
  const auto rootFolder = p_config.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  if (rootFolder.isEmpty()) {
    return QString();
  }
  const auto configFolder =
      PathUtils::concatenateFilePath(rootFolder, QStringLiteral("vx_notebook"));
  return PathUtils::concatenateFilePath(configFolder, QStringLiteral("tasks"));
}
} // namespace

QProcess *TaskService::runTask(Task *p_task) {
  if (!p_task) {
    return nullptr;
  }

  // Retain the registered root before any modal prompt or variable evaluation.
  // A sub-task's raw pointer is owned by its top-level task.
  QSharedPointer<Task> root;
  bool notebookTask = false;
  for (const auto *tasks : {&m_appTasks, &m_notebookTasks}) {
    for (const auto &candidate : *tasks) {
      if (subtreeContains(candidate.data(), p_task)) {
        root = candidate;
        notebookTask = tasks == &m_notebookTasks;
        break;
      }
    }
    if (root) {
      break;
    }
  }
  if (!root) {
    return nullptr;
  }

  if (notebookTask) {
    auto *context = m_context;
    const auto notebookId = m_notebookTasksNotebookId;
    const auto taskFolder = m_notebookTasksFolder;
    if (!context || !m_notebookService || notebookId.isEmpty() || taskFolder.isEmpty()) {
      return nullptr;
    }

    // Neither task JSON nor the execution-time selection identifies the origin
    // of a cached task. Check the original cache and context before trusting it.
    const auto matchesOrigin = [this, context, &notebookId, &taskFolder, &root]() {
      return m_context == context && m_notebookTasksNotebookId == notebookId &&
             m_notebookTasksFolder == taskFolder && m_notebookTasks.contains(root) &&
             context->currentNotebookId() == notebookId;
    };
    if (!matchesOrigin()) {
      return nullptr;
    }
    const auto config = m_notebookService->getNotebookConfig(notebookId);
    if (notebookTaskFolder(config) != taskFolder) {
      // Missing live metadata, a closed notebook, or a changed task folder.
      return nullptr;
    }

    if (!m_notebookService->isNotebookTrusted(notebookId)) {
      if (!context->confirmNotebookTrust(
              config.value(QLatin1String(vxcore::kJsonKeyName)).toString(),
              config.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString())) {
        return nullptr;
      }
      // The modal event loop may have reloaded tasks, switched/closed the
      // notebook, or replaced the context. Never persist a stale approval.
      if (!matchesOrigin() || getNotebookTaskFolder() != taskFolder) {
        return nullptr;
      }
      if (!m_notebookService->setNotebookTrusted(notebookId, true)) {
        emit taskOutputRequested(
            tr("Task (%1) was not run because notebook trust could not be saved.\n")
                .arg(p_task->getLabel()));
        return nullptr;
      }
    }
  }

  QProcess *process = p_task->runStarted();
  if (!process) {
    // No process was started (empty command / cancelled input).
    return nullptr;
  }

  // Isolate concurrent runs by process, not the aggregate Task::finished().
  m_runningTasks.insert(process, root);
  auto conn = QSharedPointer<QMetaObject::Connection>::create();
  *conn = connect(
      p_task, &Task::finished, this,
      [this, process, conn](QProcess *p_finished) {
        if (p_finished != process) {
          return;
        }
        m_runningTasks.remove(process);
        QObject::disconnect(*conn);
      },
      // The last root reference must outlive Task/QProcess signal delivery.
      Qt::QueuedConnection);
  return process;
}

void TaskService::setTaskContext(ITaskContext *p_context) {
  if (m_context == p_context) {
    return;
  }
  m_context = p_context;
  reloadNotebookTasks();
}

void TaskService::reloadNotebookTasks() {
  m_notebookTasksLoaded = false;
  m_notebookTasks.clear();
  m_notebookTasksNotebookId.clear();
  m_notebookTasksFolder.clear();
  emit tasksUpdated();
}

const QVector<QSharedPointer<Task>> &TaskService::getAppTasks() {
  if (!m_appTasksLoaded) {
    loadGlobalTasks();
  }
  return m_appTasks;
}

const QVector<QSharedPointer<Task>> &TaskService::getNotebookTasks() {
  if (!m_notebookTasksLoaded) {
    loadNotebookTasks();
  }
  return m_notebookTasks;
}

ConfigMgr2 *TaskService::configMgr() const { return m_configMgr; }

NotebookCoreService *TaskService::notebookService() const { return m_notebookService; }

SnippetCoreService *TaskService::snippetService() const { return m_snippetService; }

ITaskContext *TaskService::taskContext() const { return m_context; }

QString TaskService::currentNotebookRootFolder() const {
  if (!m_context || !m_notebookService) {
    return QString();
  }
  const auto notebookId = m_context->currentNotebookId();
  if (notebookId.isEmpty()) {
    return QString();
  }
  const auto config = m_notebookService->getNotebookConfig(notebookId);
  return config.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
}

QString TaskService::currentBufferPath() const {
  return m_context ? m_context->currentBufferPath() : QString();
}

QString TaskService::getNotebookTaskFolder() const {
  if (!m_context || !m_notebookService) {
    return QString();
  }
  const auto notebookId = m_context->currentNotebookId();
  if (notebookId.isEmpty()) {
    return QString();
  }
  return notebookTaskFolder(m_notebookService->getNotebookConfig(notebookId));
}

void TaskService::loadAllTasks() {
  loadGlobalTasks();

  loadNotebookTasks();

  emit tasksUpdated();
}

void TaskService::loadNotebookTasks() {
  m_notebookTasksNotebookId = m_context ? m_context->currentNotebookId() : QString();
  m_notebookTasksFolder = getNotebookTaskFolder();
  loadTasksFromFolder(m_notebookTasks, m_notebookTasksFolder);
  m_notebookTasksLoaded = true;
}

void TaskService::loadGlobalTasks() {
  loadTasksFromFolder(m_appTasks, getAppTaskFolder());
  m_appTasksLoaded = true;
}

QString TaskService::getAppTaskFolder() const {
  return m_configMgr ? m_configMgr->getConfigDataFolder(ConfigMgr2::Tasks) : QString();
}

void TaskService::loadTasksFromFolder(QVector<QSharedPointer<Task>> &p_tasks,
                                      const QString &p_folder) {
  qDebug() << "load tasks from folder" << p_folder;
  p_tasks.clear();

  if (p_folder.isEmpty()) {
    return;
  }

  const auto taskFiles = FileUtils2::entryListRecursively(p_folder, {"*.json"}, QDir::Files);
  for (const auto &file : taskFiles) {
    auto task = loadTask(file);
    if (task) {
      qDebug() << "loaded task" << task->getLabel();
      connect(task.data(), &Task::outputRequested, this, &TaskService::taskOutputRequested);
      p_tasks.append(task);
    }
  }
}

QSharedPointer<Task> TaskService::loadTask(const QString &p_taskFile) {
  QString localeStr;
  if (m_configMgr) {
    localeStr = m_configMgr->getCoreConfig().getLocaleToUse();
  }
  auto task = Task::fromFile(p_taskFile, localeStr, this);
  return task;
}

const TaskVariableMgr &TaskService::getVariableMgr() const { return m_variableMgr; }
