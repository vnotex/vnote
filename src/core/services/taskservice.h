#ifndef TASKSERVICE_H
#define TASKSERVICE_H

#include <QObject>
#include <core/noncopyable.h>

#include <QHash>
#include <QSharedPointer>
#include <QVector>

#include "task.h"
#include "taskvariablemgr.h"

class QProcess;

namespace vnotex {
class ConfigMgr2;
class NotebookCoreService;
class SnippetCoreService;
class ITaskContext;

// Service layer replacement for the legacy TaskMgr. Plain QObject (tasks are
// not vxcore-backed). Dependencies are injected via the constructor; no
// singleton lookups. The ITaskContext may be nullptr until UI wiring lands,
// in which case all context-derived variables resolve to empty.
class TaskService : public QObject, private Noncopyable {
  Q_OBJECT
public:
  TaskService(ConfigMgr2 *p_configMgr, NotebookCoreService *p_notebookService,
              SnippetCoreService *p_snippetService, ITaskContext *p_context,
              QObject *p_parent = nullptr);

  // Initializes variables only. Task files are loaded on the first request,
  // after startup has installed bundled data. Call once after registration.
  void init();

  // Inject (or replace) the live editor/notebook context used to resolve
  // context-derived task variables (${notebook*}, ${buffer*}, ${input:*},
  // ${selectedText}). Safe to call after init(): TaskVariableMgr resolves the
  // context lazily at evaluate-time, so no re-init is required. Replacing the
  // context invalidates notebook tasks and emits tasksUpdated().
  void setTaskContext(ITaskContext *p_context);

  // Immediately reload both scopes from disk and emit tasksUpdated().
  void reload();

  // Runs the given task, keeping the owning root task alive for the duration of
  // its process even if reload()/reloadNotebookTasks() replaces the loaded task
  // lists in the meantime. Prevents a reload or notebook switch from destroying
  // a Task (and its QProcess) mid-execution.
  void runTask(Task *p_task);

  // Invalidate notebook-scoped tasks after the current notebook changes.
  // Emits tasksUpdated(); the next request reloads from the current notebook.
  void reloadNotebookTasks();

  // Load each scope once on demand, including empty results. These getters
  // never emit tasksUpdated(), so they are safe during a model refresh.
  const QVector<QSharedPointer<Task>> &getAppTasks();

  const QVector<QSharedPointer<Task>> &getNotebookTasks();

  // Absolute path to the current notebook's task folder, or "" if there is no
  // current notebook (or no context).
  QString getNotebookTaskFolder() const;

  // Absolute path of the app (global) tasks folder, or "" if unavailable. This
  // is the single source of truth shared by task loading and the controller's
  // create/open operations.
  QString getAppTaskFolder() const;

  const TaskVariableMgr &getVariableMgr() const;

  // Injected dependency accessors (used by Task / TaskVariableMgr).
  ConfigMgr2 *configMgr() const;
  NotebookCoreService *notebookService() const;
  SnippetCoreService *snippetService() const;
  ITaskContext *taskContext() const;

  // Absolute root folder path of the current notebook, or "" if none.
  QString currentNotebookRootFolder() const;

  // Absolute path of the current buffer's file, or "" if none.
  QString currentBufferPath() const;

signals:
  void tasksUpdated();

  void taskOutputRequested(const QString &p_text) const;

private:
  void loadAllTasks();

  void loadNotebookTasks();

  void loadGlobalTasks();

  void loadTasksFromFolder(QVector<QSharedPointer<Task>> &p_tasks, const QString &p_folder);

  // Return nullptr if not a valid task.
  QSharedPointer<Task> loadTask(const QString &p_taskFile);

  ConfigMgr2 *m_configMgr = nullptr;

  NotebookCoreService *m_notebookService = nullptr;

  SnippetCoreService *m_snippetService = nullptr;

  ITaskContext *m_context = nullptr;

  bool m_appTasksLoaded = false;
  QVector<QSharedPointer<Task>> m_appTasks;

  bool m_notebookTasksLoaded = false;
  QVector<QSharedPointer<Task>> m_notebookTasks;

  // Root tasks kept alive while one of their (sub)tasks is executing, keyed by
  // the specific QProcess of each run, so a concurrent reload does not destroy
  // a running Task/QProcess and concurrent runs of the same task are isolated.
  QHash<QProcess *, QSharedPointer<Task>> m_runningTasks;

  TaskVariableMgr m_variableMgr;
};
} // namespace vnotex

#endif // TASKSERVICE_H
