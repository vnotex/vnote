#include "taskservice.h"

#include <QDebug>
#include <QDir>

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

void TaskService::init() {
  m_variableMgr.init();

  // Load all tasks. Notebook-scoped tasks are (re)loaded by the UI via
  // reloadNotebookTasks() when the current notebook changes.
  loadAllTasks();
}

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
} // namespace

void TaskService::runTask(Task *p_task) {
  if (!p_task) {
    return;
  }

  // Find the owning root QSharedPointer so we can keep it alive during
  // execution (a sub-task's raw pointer is owned by its top-level task).
  QSharedPointer<Task> root;
  for (const auto &lists : {&m_appTasks, &m_notebookTasks}) {
    for (const auto &sp : *lists) {
      if (subtreeContains(sp.data(), p_task)) {
        root = sp;
        break;
      }
    }
    if (root) {
      break;
    }
  }

  QProcess *process = p_task->runStarted();
  if (!process) {
    // No process was started (empty command / cancelled input); nothing to
    // keep alive.
    return;
  }

  if (root) {
    // Keep the root alive for THIS run only, keyed by its process. Isolating by
    // process (rather than the aggregate Task::finished()) makes concurrent
    // runs of the same task independent: each completion releases only its own
    // reference.
    m_runningTasks.insert(process, root);
    auto conn = QSharedPointer<QMetaObject::Connection>::create();
    *conn = connect(p_task, &Task::finished, this, [this, process, conn](QProcess *p_finished) {
      if (p_finished != process) {
        return;
      }
      m_runningTasks.remove(process);
      QObject::disconnect(*conn);
    });
  }
}

void TaskService::setTaskContext(ITaskContext *p_context) { m_context = p_context; }

void TaskService::reloadNotebookTasks() {
  loadNotebookTasks();
  emit tasksUpdated();
}

const QVector<QSharedPointer<Task>> &TaskService::getAppTasks() const { return m_appTasks; }

const QVector<QSharedPointer<Task>> &TaskService::getNotebookTasks() const {
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
  const auto config = m_notebookService->getNotebookConfig(notebookId);
  // Only bundled notebooks have a per-notebook config folder (vx_notebook).
  // Raw notebooks have no notebook-root config path, so there is no task
  // folder to derive.
  if (config.value(QLatin1String(vxcore::kJsonKeyType)).toString() != QStringLiteral("bundled")) {
    return QString();
  }
  const auto rootFolder = config.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  if (rootFolder.isEmpty()) {
    return QString();
  }
  // Bundled notebooks store per-notebook config under <root>/vx_notebook; the
  // task folder lives at <root>/vx_notebook/tasks (mirrors the legacy
  // Notebook::getConfigFolderAbsolutePath() + "tasks").
  const auto configFolder =
      PathUtils::concatenateFilePath(rootFolder, QStringLiteral("vx_notebook"));
  return PathUtils::concatenateFilePath(configFolder, QStringLiteral("tasks"));
}

void TaskService::loadAllTasks() {
  loadGlobalTasks();

  loadNotebookTasks();

  emit tasksUpdated();
}

void TaskService::loadNotebookTasks() {
  loadTasksFromFolder(m_notebookTasks, getNotebookTaskFolder());
}

void TaskService::loadGlobalTasks() { loadTasksFromFolder(m_appTasks, getAppTaskFolder()); }

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
