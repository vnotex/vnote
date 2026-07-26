#ifndef TASKSUNITEDENTRY_H
#define TASKSUNITEDENTRY_H

#include "iunitedentry.h"

#include <QPointer>
#include <QSharedPointer>
#include <QVector>

#include <functional>

class QTreeWidget;
class QTreeWidgetItem;

namespace vnotex {
class ServiceLocator;
class Task;

// United entry (keyword "task") that shows a flat, filtered list of runnable
// leaf tasks (App + Notebook scoped). Activating a row runs the task via
// TaskService and closes the popup.
class TasksUnitedEntry : public IUnitedEntry {
  Q_OBJECT
public:
  TasksUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                   QObject *p_parent = nullptr);

  QSharedPointer<QWidget> currentPopupWidget() const Q_DECL_OVERRIDE;

  // One row of the flat task list: the runnable leaf task plus its display
  // path, e.g. "app/git/commit" (scope, then the ancestor labels, then the
  // leaf's own label).
  struct TaskLeaf {
    Task *task = nullptr;

    QString path;
  };

  // Recursively flatten @p_roots into the runnable leaf tasks: a task is a
  // runnable leaf when it has no children AND a non-empty (raw, unevaluated)
  // command; a task with children is descended into. @p_scope is the first path
  // segment ("app" / "notebook"). When @p_filter is non-empty, only leaves whose
  // label contains it (case-insensitive) are returned. Exposed as a static so
  // the flatten/filter/path logic is unit-testable without a running
  // TaskService. The unevaluated DTO command is used so this never triggers
  // variable evaluation / input prompts.
  static QVector<TaskLeaf> collectRunnableLeaves(const QVector<QSharedPointer<Task>> &p_roots,
                                                 const QString &p_scope, const QString &p_filter);

protected:
  void initOnFirstProcess() Q_DECL_OVERRIDE;

  void
  processInternal(const QString &p_args,
                  const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
      Q_DECL_OVERRIDE;

private:
  void finish();

  void addLeafItems(const QVector<TaskLeaf> &p_leaves, QTreeWidgetItem **p_firstItem);

  void handleItemActivated(QTreeWidgetItem *p_item, int p_column);

  ServiceLocator &m_services;

  QSharedPointer<QTreeWidget> m_tree;

  // Deletion-aware identity for the rows currently shown in m_tree. Each item
  // stores its index into this vector in Qt::UserRole; a QPointer is nulled
  // when the underlying Task QObject is destroyed (e.g. by a task reload while
  // the popup is open), so a stale row can never resurrect a reused address.
  QVector<QPointer<Task>> m_rows;
};
} // namespace vnotex

#endif // TASKSUNITEDENTRY_H
