#ifndef TASKTREEMODEL_H
#define TASKTREEMODEL_H

#include <QAbstractItemModel>
#include <QVector>

namespace vnotex {
class TaskService;
class Task;

// Tree model exposing file-based tasks grouped under two fixed roots:
// "App Tasks" and "Notebook Tasks". Children of a group are the top-level
// tasks from TaskService::getAppTasks() / getNotebookTasks(); grandchildren are
// each task's Task::getChildren() sub-tasks. Group rows are labels only and map
// to a null Task*.
class TaskTreeModel : public QAbstractItemModel {
  Q_OBJECT
public:
  explicit TaskTreeModel(TaskService *p_taskService, QObject *p_parent = nullptr);
  ~TaskTreeModel() override;

  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_index) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;

  // Returns the Task* for a task/sub-task row, or nullptr for group rows and
  // invalid indexes.
  Task *taskForIndex(const QModelIndex &p_index) const;

  // True if the index is a top-level task row (direct child of a group), i.e.
  // one that owns its own .json file. Sub-task rows and group rows return false.
  bool isTopLevelTask(const QModelIndex &p_index) const;

  // Re-read tasks from TaskService and reset the model.
  void refresh();

private:
  struct Item {
    ~Item() { qDeleteAll(m_children); }

    enum Kind { Group, TaskItem } m_kind = Group;
    QString m_label;         // Group label (unused for tasks).
    Task *m_task = nullptr;  // Non-null for task/sub-task rows.
    Item *m_parent = nullptr;
    QVector<Item *> m_children;
  };

  void clearItems();

  Item *itemForIndex(const QModelIndex &p_index) const;

  TaskService *m_taskService = nullptr;

  // Two fixed group roots owned by the model.
  QVector<Item *> m_roots;
};
} // namespace vnotex

#endif // TASKTREEMODEL_H
