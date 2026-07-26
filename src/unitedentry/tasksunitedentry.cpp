#include "tasksunitedentry.h"

#include <QIcon>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "entrywidgetfactory.h"
#include "taskentrydelegate.h"
#include <core/servicelocator.h>
#include <core/services/task.h>
#include <core/services/taskservice.h>

using namespace vnotex;

namespace {
// True if @p_task is @p_root or a descendant of @p_root.
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

// True if @p_task is present anywhere in one of the given root task lists.
bool isTaskLive(Task *p_task, const QVector<QSharedPointer<Task>> &p_roots) {
  for (const auto &sp : p_roots) {
    if (sp && subtreeContains(sp.data(), p_task)) {
      return true;
    }
  }
  return false;
}

void collectLeaves(Task *p_task, const QStringList &p_prefix, const QString &p_filter,
                   QVector<TasksUnitedEntry::TaskLeaf> &p_out) {
  if (!p_task) {
    return;
  }

  QStringList path = p_prefix;
  path.append(p_task->getLabel());

  const auto &children = p_task->getChildren();
  if (!children.isEmpty()) {
    // Grouping parent: descend into children.
    for (auto *child : children) {
      collectLeaves(child, path, p_filter, p_out);
    }
    return;
  }

  // Leaf: runnable only if it carries a (raw, unevaluated) command.
  if (p_task->getDTO().command.trimmed().isEmpty()) {
    return;
  }

  if (!p_filter.isEmpty() && !p_task->getLabel().contains(p_filter, Qt::CaseInsensitive)) {
    return;
  }

  TasksUnitedEntry::TaskLeaf leaf;
  leaf.task = p_task;
  leaf.path = path.join(QLatin1Char('/'));
  p_out.append(leaf);
}
} // namespace

TasksUnitedEntry::TasksUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                   QObject *p_parent)
    : IUnitedEntry("task", tr("Run a task"), p_mgr, p_parent), m_services(p_services) {}

QVector<TasksUnitedEntry::TaskLeaf>
TasksUnitedEntry::collectRunnableLeaves(const QVector<QSharedPointer<Task>> &p_roots,
                                        const QString &p_scope, const QString &p_filter) {
  QStringList prefix;
  if (!p_scope.isEmpty()) {
    prefix.append(p_scope);
  }

  QVector<TaskLeaf> leaves;
  for (const auto &root : p_roots) {
    collectLeaves(root.data(), prefix, p_filter, leaves);
  }
  return leaves;
}

void TasksUnitedEntry::initOnFirstProcess() {
  m_tree = EntryWidgetFactory::createTreeWidget(1);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // Two-line rows: task label, then its dimmed scope path.
  m_tree->setItemDelegate(new TaskEntryDelegate(m_services, m_tree.data()));
  connect(m_tree.data(), &QTreeWidget::itemActivated, this,
          &TasksUnitedEntry::handleItemActivated);
}

void TasksUnitedEntry::addLeafItems(const QVector<TaskLeaf> &p_leaves,
                                    QTreeWidgetItem **p_firstItem) {
  for (const auto &leaf : p_leaves) {
    auto *item = new QTreeWidgetItem(m_tree.data());
    item->setText(0, leaf.task->getLabel());
    item->setData(0, TaskEntryDelegate::PathRole, leaf.path);
    item->setToolTip(0, leaf.path);

    const QString iconPath = leaf.task->getIcon();
    if (!iconPath.isEmpty()) {
      item->setIcon(0, QIcon(iconPath));
    }

    const int index = m_rows.size();
    m_rows.append(QPointer<Task>(leaf.task));
    item->setData(0, Qt::UserRole, index);

    if (p_firstItem && !*p_firstItem) {
      *p_firstItem = item;
    }
  }
}

void TasksUnitedEntry::processInternal(
    const QString &p_args,
    const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) {
  setOngoing(true);

  // Reset any previously built rows up front so a stale tree can never be
  // returned by currentPopupWidget() (and keyboard-activated) after an
  // empty-result label is shown.
  if (m_tree) {
    m_tree->clear();
  }
  m_rows.clear();

  auto *ts = m_services.get<TaskService>();
  if (!ts) {
    QSharedPointer<QWidget> label = EntryWidgetFactory::createLabel(tr("No tasks"));
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  const QString filter = p_args.trimmed();

  const auto appLeaves =
      collectRunnableLeaves(ts->getAppTasks(), QStringLiteral("app"), filter);
  const auto notebookLeaves =
      collectRunnableLeaves(ts->getNotebookTasks(), QStringLiteral("notebook"), filter);

  if (appLeaves.isEmpty() && notebookLeaves.isEmpty()) {
    QSharedPointer<QWidget> label = EntryWidgetFactory::createLabel(
        filter.isEmpty() ? tr("No tasks") : tr("No matching tasks"));
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  QTreeWidgetItem *firstItem = nullptr;
  addLeafItems(appLeaves, &firstItem);
  addLeafItems(notebookLeaves, &firstItem);

  if (firstItem) {
    m_tree->setCurrentItem(firstItem);
  }

  p_popupWidgetFunc(m_tree);
  finish();
}

void TasksUnitedEntry::finish() {
  setOngoing(false);
  emit finished();
}

QSharedPointer<QWidget> TasksUnitedEntry::currentPopupWidget() const { return m_tree; }

void TasksUnitedEntry::handleItemActivated(QTreeWidgetItem *p_item, int p_column) {
  Q_UNUSED(p_column);

  if (!p_item) {
    return;
  }

  const QVariant data = p_item->data(0, Qt::UserRole);
  if (!data.isValid()) {
    return;
  }
  const int index = data.toInt();
  if (index < 0 || index >= m_rows.size()) {
    return;
  }

  // QPointer is nulled if the Task was destroyed (e.g. by a reload) while the
  // popup was open, guarding against both dangling pointers and address reuse.
  Task *task = m_rows.at(index).data();
  if (!task) {
    return;
  }

  auto *ts = m_services.get<TaskService>();
  if (!ts) {
    return;
  }

  // Defense in depth: the task must still belong to a live task tree.
  if (!isTaskLive(task, ts->getAppTasks()) && !isTaskLive(task, ts->getNotebookTasks())) {
    return;
  }

  ts->runTask(task);

  emit itemActivated(true, false);
}
