#include "taskmgr.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>

#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/notebookmgr.h>
#include <core/vnotex.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

TaskMgr::TaskMgr(QObject *p_parent) : QObject(p_parent), m_variableMgr(this) {}

void TaskMgr::init() {
  m_variableMgr.init();

  // Load all tasks and watch the location.
  loadAllTasks();

  connect(&VNoteX::getInst().getNotebookMgr(), &NotebookMgr::currentNotebookChanged, this,
          [this]() {
            loadNotebookTasks();
            emit tasksUpdated();
          });
}

void TaskMgr::reload() { loadAllTasks(); }

const QVector<QSharedPointer<Task>> &TaskMgr::getAppTasks() const { return m_appTasks; }

const QVector<QSharedPointer<Task>> &TaskMgr::getUserTasks() const { return m_userTasks; }

const QVector<QSharedPointer<Task>> &TaskMgr::getNotebookTasks() const { return m_notebookTasks; }

QString TaskMgr::getNotebookTaskFolder() {
  auto nb = VNoteX::getInst().getNotebookMgr().getCurrentNotebook();
  if (nb) {
    const auto folderPath = nb->getConfigFolderAbsolutePath();
    if (!folderPath.isEmpty()) {
      return PathUtils::concatenateFilePath(folderPath, QStringLiteral("tasks"));
    }
  }
  return QString();
}

void TaskMgr::loadAllTasks() {
  loadGlobalTasks();

  loadNotebookTasks();

  emit tasksUpdated();
}

void TaskMgr::loadNotebookTasks() { loadTasksFromFolder(m_notebookTasks, getNotebookTaskFolder()); }

void TaskMgr::loadGlobalTasks() {
  loadTasksFromFolder(m_appTasks, ConfigMgr::getInst().getAppTaskFolder());
  loadTasksFromFolder(m_userTasks, ConfigMgr::getInst().getUserTaskFolder());
}

void TaskMgr::loadTasksFromFolder(QVector<QSharedPointer<Task>> &p_tasks, const QString &p_folder) {
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
      connect(task.data(), &Task::outputRequested, this, &TaskMgr::taskOutputRequested);
      p_tasks.append(task);
    }
  }
}

QSharedPointer<Task> TaskMgr::loadTask(const QString &p_taskFile) {
  const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
  auto task = Task::fromFile(p_taskFile, localeStr, this);
  return task;
}

const TaskVariableMgr &TaskMgr::getVariableMgr() const { return m_variableMgr; }
