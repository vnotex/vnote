#include "taskservice.h"

#include <QDebug>
#include <QDir>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/services/notebookcoreservice.h>
#include <utils/fileutils.h>
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

void TaskService::loadGlobalTasks() {
  QString folder;
  if (m_configMgr) {
    folder = m_configMgr->getConfigDataFolder(ConfigMgr2::Tasks);
  }
  loadTasksFromFolder(m_appTasks, folder);
}

void TaskService::loadTasksFromFolder(QVector<QSharedPointer<Task>> &p_tasks,
                                      const QString &p_folder) {
  qDebug() << "load tasks from folder" << p_folder;
  p_tasks.clear();

  if (p_folder.isEmpty()) {
    return;
  }

  const auto taskFiles = FileUtils::entryListRecursively(p_folder, {"*.json"}, QDir::Files);
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
