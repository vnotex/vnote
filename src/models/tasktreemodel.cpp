#include "tasktreemodel.h"

#include <functional>

#include <QFileInfo>
#include <QIcon>

#include <core/services/task.h>
#include <core/services/taskservice.h>

using namespace vnotex;

TaskTreeModel::TaskTreeModel(TaskService *p_taskService, QObject *p_parent)
    : QAbstractItemModel(p_parent), m_taskService(p_taskService) {
  refresh();
}

TaskTreeModel::~TaskTreeModel() { clearItems(); }

void TaskTreeModel::clearItems() {
  qDeleteAll(m_roots);
  m_roots.clear();
}

void TaskTreeModel::refresh() {
  beginResetModel();

  clearItems();

  // Recursively builds an Item for p_task under p_parent, including any nested
  // sub-tasks (Task::getChildren()).
  std::function<Item *(Item *, Task *)> addTaskItem = [&addTaskItem](Item *p_parent, Task *p_task) {
    auto *item = new Item();
    item->m_kind = Item::TaskItem;
    item->m_task = p_task;
    item->m_parent = p_parent;
    p_parent->m_children.append(item);
    const auto &children = p_task->getChildren();
    for (auto *child : children) {
      addTaskItem(item, child);
    }
    return item;
  };

  auto *appGroup = new Item();
  appGroup->m_kind = Item::Group;
  appGroup->m_label = tr("App Tasks");
  m_roots.append(appGroup);

  auto *notebookGroup = new Item();
  notebookGroup->m_kind = Item::Group;
  notebookGroup->m_label = tr("Notebook Tasks");
  m_roots.append(notebookGroup);

  if (m_taskService) {
    for (const auto &task : m_taskService->getAppTasks()) {
      addTaskItem(appGroup, task.data());
    }
    for (const auto &task : m_taskService->getNotebookTasks()) {
      addTaskItem(notebookGroup, task.data());
    }
  }

  endResetModel();
}

TaskTreeModel::Item *TaskTreeModel::itemForIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return nullptr;
  }
  return static_cast<Item *>(p_index.internalPointer());
}

QModelIndex TaskTreeModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (p_column != 0 || p_row < 0) {
    return QModelIndex();
  }

  if (!p_parent.isValid()) {
    if (p_row >= m_roots.size()) {
      return QModelIndex();
    }
    return createIndex(p_row, p_column, m_roots.at(p_row));
  }

  auto *parentItem = itemForIndex(p_parent);
  if (!parentItem || p_row >= parentItem->m_children.size()) {
    return QModelIndex();
  }
  return createIndex(p_row, p_column, parentItem->m_children.at(p_row));
}

QModelIndex TaskTreeModel::parent(const QModelIndex &p_index) const {
  auto *item = itemForIndex(p_index);
  if (!item || !item->m_parent) {
    return QModelIndex();
  }

  auto *parentItem = item->m_parent;
  auto *grandParent = parentItem->m_parent;
  int row = 0;
  if (grandParent) {
    row = grandParent->m_children.indexOf(parentItem);
  } else {
    row = m_roots.indexOf(parentItem);
  }
  if (row < 0) {
    return QModelIndex();
  }
  return createIndex(row, 0, parentItem);
}

int TaskTreeModel::rowCount(const QModelIndex &p_parent) const {
  if (!p_parent.isValid()) {
    return m_roots.size();
  }
  auto *item = itemForIndex(p_parent);
  return item ? item->m_children.size() : 0;
}

int TaskTreeModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1;
}

QVariant TaskTreeModel::data(const QModelIndex &p_index, int p_role) const {
  auto *item = itemForIndex(p_index);
  if (!item) {
    return QVariant();
  }

  switch (p_role) {
  case Qt::DisplayRole:
    if (item->m_kind == Item::Group) {
      return item->m_label;
    }
    return item->m_task ? item->m_task->getLabel() : QString();
  case Qt::DecorationRole:
    if (item->m_kind == Item::TaskItem && item->m_task) {
      const auto &iconPath = item->m_task->getIcon();
      if (!iconPath.isEmpty() && QFileInfo::exists(iconPath)) {
        return QIcon(iconPath);
      }
    }
    return QVariant();
  case Qt::ToolTipRole:
    if (item->m_kind == Item::TaskItem && item->m_task) {
      return item->m_task->getFile();
    }
    return QVariant();
  default:
    return QVariant();
  }
}

Task *TaskTreeModel::taskForIndex(const QModelIndex &p_index) const {
  auto *item = itemForIndex(p_index);
  return item ? item->m_task : nullptr;
}

bool TaskTreeModel::isTopLevelTask(const QModelIndex &p_index) const {
  auto *item = itemForIndex(p_index);
  return item && item->m_kind == Item::TaskItem && item->m_parent &&
         item->m_parent->m_kind == Item::Group;
}
