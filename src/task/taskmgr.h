#ifndef TASKMGR_H
#define TASKMGR_H

#include <QObject>
#include <core/noncopyable.h>

#include <QSharedPointer>
#include <QVector>

#include "task.h"
#include "taskvariablemgr.h"

namespace vnotex {
class TaskMgr : public QObject, private Noncopyable {
  Q_OBJECT
public:
  explicit TaskMgr(QObject *p_parent = nullptr);

  // It will be invoked after MainWindow show.
  void init();

  void reload();

  const QVector<QSharedPointer<Task>> &getAppTasks() const;

  const QVector<QSharedPointer<Task>> &getUserTasks() const;

  const QVector<QSharedPointer<Task>> &getNotebookTasks() const;

  static QString getNotebookTaskFolder();

  const TaskVariableMgr &getVariableMgr() const;

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

  QVector<QSharedPointer<Task>> m_appTasks;

  QVector<QSharedPointer<Task>> m_userTasks;

  QVector<QSharedPointer<Task>> m_notebookTasks;

  TaskVariableMgr m_variableMgr;
};
} // namespace vnotex

#endif // TASKMGR_H
