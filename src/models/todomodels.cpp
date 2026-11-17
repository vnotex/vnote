#include "todomodels.h"

#include <QDataStream>
#include <QIODevice>
#include <QLocale>
#include <QMimeData>
#include <QStringView>

#include <algorithm>
#include <utility>

using namespace vnotex;

namespace {

const QString ListMimeType = QStringLiteral("application/x-vnote-todo-list");
const QString TaskMimeType = QStringLiteral("application/x-vnote-todo-task");
constexpr quint32 DragFormatVersion = 1;

bool matchesView(const TodoItem &p_item, const TodoList &p_list, TodoViewKind p_view,
                 const QString &p_listKey, const QDate &p_today) {
  switch (p_view) {
  case TodoViewKind::List:
    return p_list.m_key == p_listKey;
  case TodoViewKind::MyDay:
    return p_today.isValid() && p_item.m_fields.m_myDay == p_today;
  case TodoViewKind::Important:
    return p_item.m_fields.m_important;
  case TodoViewKind::Planned:
    return p_item.m_fields.m_due.isValid();
  case TodoViewKind::All:
    return true;
  }
  return false;
}

bool matchesFilters(const TodoItem &p_item, const TodoList &p_list, bool p_groupMatches,
                    const TodoProjection &p_projection) {
  const auto &fields = p_item.m_fields;
  if (!p_projection.m_tag.isEmpty() &&
      !fields.m_tags.contains(p_projection.m_tag, Qt::CaseInsensitive)) {
    return false;
  }

  const auto &search = p_projection.m_search;
  if (search.isEmpty() || p_groupMatches || p_list.m_title.contains(search, Qt::CaseInsensitive) ||
      fields.m_title.contains(search, Qt::CaseInsensitive) ||
      fields.m_note.contains(search, Qt::CaseInsensitive)) {
    return true;
  }
  for (const auto &tag : fields.m_tags) {
    if (tag.contains(search, Qt::CaseInsensitive)) {
      return true;
    }
  }
  for (const auto &step : p_item.m_steps) {
    if (step.m_title.contains(search, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

bool hasFilters(const TodoProjection &p_projection) {
  return !p_projection.m_search.isEmpty() || !p_projection.m_tag.isEmpty();
}

QString groupTitle(const TodoGroup &p_group) {
  if (p_group.m_isDefault) {
    return {};
  }
  return QStringView(p_group.m_title).trimmed().isEmpty() ? TodoListModel::tr("Untitled group")
                                                          : p_group.m_title;
}

QString listTitle(const TodoList &p_list) {
  return QStringView(p_list.m_title).trimmed().isEmpty() ? TodoListModel::tr("Untitled list")
                                                         : p_list.m_title;
}

QString itemTitle(const TodoItem &p_item) {
  return QStringView(p_item.m_fields.m_title).trimmed().isEmpty()
             ? TodoTaskModel::tr("Untitled task")
             : p_item.m_fields.m_title;
}

bool readBoolean(const QVariant &p_value, bool *p_result) {
  if (p_value.metaType().id() != QMetaType::Bool) {
    return false;
  }
  *p_result = p_value.toBool();
  return true;
}

} // namespace

TodoListModel::TodoListModel(ServiceLocator &p_services, QObject *p_parent)
    : QAbstractListModel(p_parent) {
  Q_UNUSED(p_services);
  setSnapshot(TodoDocument(), TodoProjection(), false, -1);
}

QModelIndex TodoListModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (p_parent.isValid() || p_column != 0 || p_row < 0 || p_row >= m_rows.size()) {
    return {};
  }
  return createIndex(p_row, p_column, static_cast<quintptr>(m_snapshot));
}

bool TodoListModel::isOwnIndex(const QModelIndex &p_index) const {
  return p_index.isValid() && p_index.model() == this && p_index.column() == 0 &&
         p_index.row() >= 0 && p_index.row() < m_rows.size() &&
         p_index.internalId() == static_cast<quintptr>(m_snapshot);
}

int TodoListModel::rowCount(const QModelIndex &p_parent) const {
  return p_parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TodoListModel::data(const QModelIndex &p_index, int p_role) const {
  if (!isOwnIndex(p_index)) {
    return {};
  }

  const auto &row = m_rows.at(p_index.row());
  const auto *list = listAt(p_index.row());
  const auto *group = groupAt(p_index.row());
  const bool groupHeader = group && !list;
  switch (p_role) {
  case IconRole:
    return list ? list->m_properties.value(QStringLiteral("icon")).toString() : QString();
  case GroupRole:
    return groupHeader;
  case GroupIdRole:
    return group && !group->m_isDefault ? group->m_key : QString();
  case GroupTitleRole:
    return group ? groupTitle(*group) : QString();
  case ExpandedRole:
    return group ? QVariant(row.m_expanded) : QVariant();
  case ListCountRole:
    return groupHeader ? QVariant(group->m_listCount) : QVariant();
  default:
    break;
  }
  if (p_role == IdRole) {
    return list ? list->m_key : groupHeader ? group->m_key : QString();
  }
  if (p_role == ViewKindRole) {
    return groupHeader ? QVariant() : QVariant::fromValue(row.m_view);
  }
  if (p_role == IncompleteCountRole) {
    return row.m_incompleteCount;
  }
  if (p_role != Qt::DisplayRole && p_role != TitleRole && p_role != Qt::AccessibleTextRole &&
      p_role != Qt::ToolTipRole) {
    return {};
  }

  if (groupHeader) {
    const auto title = groupTitle(*group);
    if (p_role == Qt::AccessibleTextRole || p_role == Qt::ToolTipRole) {
      return (row.m_expanded ? tr("%1, %2 lists, %3 incomplete tasks, expanded")
                             : tr("%1, %2 lists, %3 incomplete tasks, collapsed"))
          .arg(title)
          .arg(group->m_listCount)
          .arg(row.m_incompleteCount);
    }
    return title;
  }
  QString title;
  switch (row.m_view) {
  case TodoViewKind::List:
    title = listTitle(*list);
    break;
  case TodoViewKind::MyDay:
    title = tr("My Day");
    break;
  case TodoViewKind::Important:
    title = tr("Important");
    break;
  case TodoViewKind::Planned:
    title = tr("Planned");
    break;
  case TodoViewKind::All:
    title = tr("All Tasks");
    break;
  }
  if (p_role == Qt::AccessibleTextRole || p_role == Qt::ToolTipRole) {
    return tr("%1, %2 incomplete tasks").arg(title).arg(row.m_incompleteCount);
  }
  return title;
}

Qt::ItemFlags TodoListModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return m_editable ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
  }
  if (!isOwnIndex(p_index)) {
    return Qt::NoItemFlags;
  }
  auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
  if (m_editable && groupAt(p_index.row())) {
    result |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
  }
  return result;
}

QHash<int, QByteArray> TodoListModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  roles.insert(IdRole, "id");
  roles.insert(TitleRole, "title");
  roles.insert(ViewKindRole, "viewKind");
  roles.insert(IncompleteCountRole, "incompleteCount");
  roles.insert(GroupRole, "group");
  roles.insert(GroupIdRole, "groupId");
  roles.insert(GroupTitleRole, "groupTitle");
  roles.insert(ExpandedRole, "expanded");
  roles.insert(ListCountRole, "listCount");
  roles.insert(IconRole, "icon");
  return roles;
}

void TodoListModel::setSnapshot(const TodoDocument &p_document, const TodoProjection &p_projection,
                                bool p_editable, int p_revision) {
  beginResetModel();
  m_document = p_document;
  m_editable = p_editable && m_document.isValid();
  m_revision = p_revision;
  ++m_snapshot;
  m_rows.clear();
  m_rows.reserve(
      4 + (m_document.isValid() ? m_document.m_lists.size() + m_document.m_groups.size() : 0));
  m_rows.append({TodoViewKind::MyDay});
  m_rows.append({TodoViewKind::Important});
  m_rows.append({TodoViewKind::Planned});
  m_rows.append({TodoViewKind::All});

  if (m_document.isValid()) {
    const auto &lists = std::as_const(m_document.m_lists);
    const auto appendGroup = [&](int p_groupIndex) {
      const auto &group = m_document.m_groups.at(p_groupIndex);
      const bool expanded =
          group.m_isDefault || !p_projection.m_collapsedGroups.contains(group.m_key);
      const bool groupMatches =
          !group.m_isDefault && !p_projection.m_search.isEmpty() &&
          groupTitle(group).contains(p_projection.m_search, Qt::CaseInsensitive);
      const int groupRow = group.m_isDefault ? -1 : static_cast<int>(m_rows.size());
      if (groupRow >= 0) {
        m_rows.append({TodoViewKind::List, -1, p_groupIndex, 0, expanded});
      }
      for (int listIndex = group.m_firstList; listIndex < group.m_firstList + group.m_listCount;
           ++listIndex) {
        const auto &list = lists.at(listIndex);
        Row row{TodoViewKind::List, listIndex, p_groupIndex, 0, expanded};
        for (const auto &item : list.m_items) {
          if (item.m_fields.m_checked || !matchesFilters(item, list, groupMatches, p_projection)) {
            continue;
          }
          ++row.m_incompleteCount;
          if (groupRow >= 0) {
            ++m_rows[groupRow].m_incompleteCount;
          }
          ++m_rows[3].m_incompleteCount;
          if (matchesView(item, list, TodoViewKind::MyDay, {}, p_projection.m_today)) {
            ++m_rows[0].m_incompleteCount;
          }
          if (item.m_fields.m_important) {
            ++m_rows[1].m_incompleteCount;
          }
          if (item.m_fields.m_due.isValid()) {
            ++m_rows[2].m_incompleteCount;
          }
        }
        m_rows.append(row);
      }
    };
    // Project the internal Default owner first without changing source indexes or order.
    for (int groupIndex = 0; groupIndex < m_document.m_groups.size(); ++groupIndex) {
      if (m_document.m_groups.at(groupIndex).m_isDefault) {
        appendGroup(groupIndex);
        break;
      }
    }
    for (int groupIndex = 0; groupIndex < m_document.m_groups.size(); ++groupIndex) {
      if (!m_document.m_groups.at(groupIndex).m_isDefault) {
        appendGroup(groupIndex);
      }
    }
  }
  endResetModel();
}

void TodoListModel::setGroupExpanded(const QString &p_key, bool p_expanded) {
  const int first = rowForGroup(p_key);
  if (first < 0 || m_rows.at(first).m_expanded == p_expanded) {
    return;
  }
  const int last = first + groupAt(first)->m_listCount;
  for (int row = first; row <= last; ++row) {
    m_rows[row].m_expanded = p_expanded;
  }
  emit dataChanged(index(first), index(last),
                   {ExpandedRole, Qt::AccessibleTextRole, Qt::ToolTipRole});
}

int TodoListModel::rowForKey(const QString &p_key) const {
  return rowForView(TodoViewKind::List, p_key);
}

int TodoListModel::rowForView(TodoViewKind p_view, const QString &p_listKey) const {
  for (int row = 0; row < m_rows.size(); ++row) {
    if (m_rows.at(row).m_view != p_view ||
        (m_rows.at(row).m_group >= 0 && m_rows.at(row).m_list < 0)) {
      continue;
    }
    if (p_view != TodoViewKind::List || listAt(row)->m_key == p_listKey) {
      return row;
    }
  }
  return -1;
}

int TodoListModel::rowForGroup(const QString &p_key) const {
  for (int row = 0; row < m_rows.size(); ++row) {
    if (m_rows.at(row).m_group >= 0 && m_rows.at(row).m_list < 0 && groupAt(row)->m_key == p_key) {
      return row;
    }
  }
  return -1;
}

const TodoGroup *TodoListModel::groupAt(int p_row) const {
  if (p_row < 0 || p_row >= m_rows.size() || m_rows.at(p_row).m_group < 0) {
    return nullptr;
  }
  return &m_document.m_groups.at(m_rows.at(p_row).m_group);
}

const TodoList *TodoListModel::listAt(int p_row) const {
  if (p_row < 0 || p_row >= m_rows.size() || m_rows.at(p_row).m_list < 0) {
    return nullptr;
  }
  return &m_document.m_lists.at(m_rows.at(p_row).m_list);
}

QStringList TodoListModel::mimeTypes() const { return {ListMimeType}; }

QMimeData *TodoListModel::mimeData(const QModelIndexList &p_indexes) const {
  if (!m_editable || p_indexes.size() != 1 || !isOwnIndex(p_indexes.first())) {
    return nullptr;
  }
  const int row = p_indexes.first().row();
  const auto *group = groupAt(row);
  if (!group) {
    return nullptr;
  }
  const auto *list = listAt(row);
  const auto kind = list ? DragKind::List : DragKind::Group;
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << DragFormatVersion << m_dragIdentity << m_snapshot << m_revision
         << static_cast<quint8>(kind) << (list ? list->m_key : group->m_key);
  auto *mime = new QMimeData();
  mime->setData(ListMimeType, bytes);
  return mime;
}

bool TodoListModel::decodeDrop(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                               int p_column, const QModelIndex &p_parent, Drop *p_drop) const {
  if (!m_editable || p_action != Qt::MoveAction || !p_data || !p_data->hasFormat(ListMimeType) ||
      (p_column != -1 && p_column != 0) || p_row < -1 || p_row > m_rows.size() ||
      (p_parent.isValid() && (!isOwnIndex(p_parent) || p_row != -1))) {
    return false;
  }

  QDataStream stream(p_data->data(ListMimeType));
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 version = 0;
  QUuid identity;
  quint64 snapshot = 0;
  int revision = -1;
  quint8 kind = 0;
  QString key;
  stream >> version >> identity >> snapshot >> revision >> kind >> key;
  if (stream.status() != QDataStream::Ok || !stream.atEnd() || version != DragFormatVersion ||
      identity != m_dragIdentity || snapshot != m_snapshot || revision != m_revision ||
      (kind != static_cast<quint8>(DragKind::Group) &&
       kind != static_cast<quint8>(DragKind::List))) {
    return false;
  }
  const auto dragKind = static_cast<DragKind>(kind);
  const int sourceRow = dragKind == DragKind::Group ? rowForGroup(key) : rowForKey(key);
  if (sourceRow < 0) {
    return false;
  }
  const auto &source = m_rows.at(sourceRow);
  const int targetRow = p_parent.isValid() ? p_parent.row() : p_row;
  const bool atEnd = !p_parent.isValid() && (p_row == -1 || p_row == m_rows.size());
  if (!atEnd && !groupAt(targetRow)) {
    return false;
  }

  QString groupKey;
  QString beforeKey;
  bool unchanged = false;
  if (dragKind == DragKind::Group) {
    // Groups can only occupy root gaps before headers or at the end, never nest.
    if (p_parent.isValid() || (!atEnd && listAt(targetRow))) {
      return false;
    }
    if (!atEnd) {
      beforeKey = groupAt(targetRow)->m_key;
    }
    int nextGroup = source.m_group + 1;
    while (nextGroup < m_document.m_groups.size() &&
           m_document.m_groups.at(nextGroup).m_isDefault) {
      ++nextGroup;
    }
    unchanged =
        beforeKey == key || (atEnd ? nextGroup == m_document.m_groups.size()
                                   : nextGroup < m_document.m_groups.size() &&
                                         beforeKey == m_document.m_groups.at(nextGroup).m_key);
  } else {
    const TodoGroup *targetGroup = nullptr;
    int beforeList = -1;
    if (atEnd) {
      // An empty key requests Default, including creating it when it is absent.
      targetGroup = m_document.defaultGroup();
    } else {
      const auto &target = m_rows.at(targetRow);
      targetGroup = groupAt(targetRow);
      if (target.m_list >= 0) {
        // Root gaps and on-item drops both refer to the actual child, even hidden ones.
        beforeList = target.m_list;
      } else if (!p_parent.isValid()) {
        // Header gaps follow presented custom groups, not the hidden Default block.
        int previousGroup = target.m_group - 1;
        while (previousGroup >= 0 && m_document.m_groups.at(previousGroup).m_isDefault) {
          --previousGroup;
        }
        targetGroup =
            previousGroup >= 0 ? &m_document.m_groups.at(previousGroup) : m_document.defaultGroup();
      }
      // An on-header drop appends to that group, unlike the root gap before it.
    }
    if (targetGroup && !targetGroup->m_isDefault) {
      groupKey = targetGroup->m_key;
    }
    if (beforeList >= 0) {
      beforeKey = m_document.m_lists.at(beforeList).m_key;
    }
    if (targetGroup == groupAt(sourceRow)) {
      const int insertion =
          beforeList >= 0 ? beforeList : targetGroup->m_firstList + targetGroup->m_listCount;
      unchanged = insertion == source.m_list || insertion == source.m_list + 1;
    }
  }
  if (p_drop) {
    p_drop->m_kind = dragKind;
    p_drop->m_key = key;
    p_drop->m_groupKey = groupKey;
    p_drop->m_beforeKey = beforeKey;
    p_drop->m_unchanged = unchanged;
  }
  return true;
}

bool TodoListModel::canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                                    int p_column, const QModelIndex &p_parent) const {
  return decodeDrop(p_data, p_action, p_row, p_column, p_parent, nullptr);
}

bool TodoListModel::dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                                 int p_column, const QModelIndex &p_parent) {
  Drop drop;
  if (!decodeDrop(p_data, p_action, p_row, p_column, p_parent, &drop)) {
    return false;
  }
  if (drop.m_unchanged) {
    return true;
  }
  // Local keys remain valid when a direct controller connection resets the snapshot.
  if (drop.m_kind == DragKind::Group) {
    emit groupMoveRequested(drop.m_key, drop.m_beforeKey, m_revision);
  } else {
    emit listMoveRequested(drop.m_key, drop.m_groupKey, drop.m_beforeKey, m_revision);
  }
  return true;
}

Qt::DropActions TodoListModel::supportedDragActions() const {
  return m_editable ? Qt::MoveAction : Qt::IgnoreAction;
}

Qt::DropActions TodoListModel::supportedDropActions() const { return supportedDragActions(); }

TodoTaskModel::TodoTaskModel(ServiceLocator &p_services, QObject *p_parent)
    : QAbstractListModel(p_parent) {
  Q_UNUSED(p_services);
}

QModelIndex TodoTaskModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (p_parent.isValid() || p_column != 0 || p_row < 0 || p_row >= m_rows.size()) {
    return {};
  }
  return createIndex(p_row, p_column, static_cast<quintptr>(m_snapshot));
}

int TodoTaskModel::rowCount(const QModelIndex &p_parent) const {
  return p_parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

bool TodoTaskModel::isOwnIndex(const QModelIndex &p_index) const {
  return p_index.isValid() && p_index.model() == this && p_index.column() == 0 &&
         p_index.row() >= 0 && p_index.row() < m_rows.size() &&
         p_index.internalId() == static_cast<quintptr>(m_snapshot);
}

QVariant TodoTaskModel::data(const QModelIndex &p_index, int p_role) const {
  if (!isOwnIndex(p_index)) {
    return {};
  }
  const auto *item = itemAt(p_index.row());
  if (p_role == SectionRole) {
    return item == nullptr;
  }
  if (p_role == RevisionRole) {
    return m_revision;
  }
  if (!item) {
    switch (p_role) {
    case Qt::DisplayRole:
    case TitleRole:
      return tr("Completed (%1)").arg(m_completedCount);
    case Qt::AccessibleTextRole:
    case Qt::ToolTipRole:
      return m_projection.m_completedExpanded
                 ? tr("Completed, %1 tasks, expanded").arg(m_completedCount)
                 : tr("Completed, %1 tasks, collapsed").arg(m_completedCount);
    case ExpandedRole:
      return m_projection.m_completedExpanded;
    case CompletedCountRole:
      return m_completedCount;
    default:
      return {};
    }
  }

  const auto &row = m_rows.at(p_index.row());
  const auto &list = m_document.m_lists.at(row.m_list);
  const auto &fields = item->m_fields;
  switch (p_role) {
  case Qt::DisplayRole:
  case TitleRole:
    return itemTitle(*item);
  case Qt::CheckStateRole:
    return fields.m_checked ? Qt::Checked : Qt::Unchecked;
  case IdRole:
    return item->m_key;
  case CheckedRole:
    return fields.m_checked;
  case ImportantRole:
    return fields.m_important;
  case DueRole:
    return fields.m_due;
  case MyDayRole:
    return fields.m_myDay;
  case TagsRole:
    return fields.m_tags;
  case NoteRole:
    return fields.m_note;
  case StepsCompletedRole:
    return row.m_stepsCompleted;
  case StepsTotalRole:
    return static_cast<int>(item->m_steps.size());
  case SourceListIdRole:
    return list.m_key;
  case SourceListNameRole:
    return listTitle(list);
  case SourceGroupIdRole:
    return m_document.m_groups.at(list.m_groupIndex).m_key;
  case SourceGroupNameRole:
    return groupTitle(m_document.m_groups.at(list.m_groupIndex));
  case Qt::AccessibleTextRole:
  case Qt::ToolTipRole: {
    QStringList parts{itemTitle(*item), fields.m_checked ? tr("Completed") : tr("Not completed")};
    const auto &group = m_document.m_groups.at(list.m_groupIndex);
    if (!group.m_isDefault) {
      parts.append(tr("Group: %1").arg(groupTitle(group)));
    }
    parts.append(tr("List: %1").arg(listTitle(list)));
    if (fields.m_important) {
      parts.append(tr("Important"));
    }
    if (fields.m_due.isValid()) {
      parts.append(tr("Due: %1").arg(QLocale().toString(fields.m_due, QLocale::ShortFormat)));
    }
    if (fields.m_myDay.isValid()) {
      parts.append(tr("My Day: %1").arg(QLocale().toString(fields.m_myDay, QLocale::ShortFormat)));
    }
    if (!item->m_steps.isEmpty()) {
      parts.append(
          tr("%1 of %2 steps completed").arg(row.m_stepsCompleted).arg(item->m_steps.size()));
    }
    if (!fields.m_tags.isEmpty()) {
      parts.append(tr("Tags: %1").arg(fields.m_tags.join(QStringLiteral(", "))));
    }
    return parts.join(QStringLiteral(", "));
  }
  default:
    return {};
  }
}

bool TodoTaskModel::setData(const QModelIndex &p_index, const QVariant &p_value, int p_role) {
  if (!isOwnIndex(p_index)) {
    return false;
  }
  const auto *item = itemAt(p_index.row());
  bool value = false;
  if (!item) {
    if (p_role != ExpandedRole || !readBoolean(p_value, &value)) {
      return false;
    }
    if (value != m_projection.m_completedExpanded) {
      emit completedExpansionRequested(value);
    }
    return true;
  }
  if (!m_editable) {
    return false;
  }
  if (p_role == Qt::CheckStateRole) {
    if (p_value.metaType().id() != QMetaType::Int &&
        p_value.metaType() != QMetaType::fromType<Qt::CheckState>()) {
      return false;
    }
    const auto state = p_value.toInt();
    if (state != Qt::Checked && state != Qt::Unchecked) {
      return false;
    }
    value = state == Qt::Checked;
  } else if ((p_role != CheckedRole && p_role != ImportantRole) || !readBoolean(p_value, &value)) {
    return false;
  }

  // Copy the key: a direct controller connection can replace this snapshot while emitting.
  const auto key = item->m_key;
  if (p_role == ImportantRole) {
    if (value != item->m_fields.m_important) {
      emit importanceChangeRequested(key, value, m_revision);
    }
  } else if (value != item->m_fields.m_checked) {
    emit checkedChangeRequested(key, value, m_revision);
  }
  return true;
}

Qt::ItemFlags TodoTaskModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return isReorderingEnabled() ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
  }
  if (!isOwnIndex(p_index)) {
    return Qt::NoItemFlags;
  }
  auto result = Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
  if (!itemAt(p_index.row())) {
    return result | Qt::ItemIsSelectable;
  }
  result |= Qt::ItemIsSelectable;
  if (m_editable) {
    result |= Qt::ItemIsUserCheckable;
  }
  if (isReorderingEnabled()) {
    result |= Qt::ItemIsDragEnabled;
  }
  return result;
}

QHash<int, QByteArray> TodoTaskModel::roleNames() const {
  auto roles = QAbstractListModel::roleNames();
  roles.insert(IdRole, "id");
  roles.insert(TitleRole, "title");
  roles.insert(CheckedRole, "checked");
  roles.insert(ImportantRole, "important");
  roles.insert(DueRole, "due");
  roles.insert(MyDayRole, "myDay");
  roles.insert(TagsRole, "tags");
  roles.insert(NoteRole, "note");
  roles.insert(StepsCompletedRole, "stepsCompleted");
  roles.insert(StepsTotalRole, "stepsTotal");
  roles.insert(SourceListIdRole, "sourceListId");
  roles.insert(SourceListNameRole, "sourceListName");
  roles.insert(SourceGroupIdRole, "sourceGroupId");
  roles.insert(SourceGroupNameRole, "sourceGroupName");
  roles.insert(SectionRole, "section");
  roles.insert(ExpandedRole, "expanded");
  roles.insert(CompletedCountRole, "completedCount");
  roles.insert(RevisionRole, "revision");
  return roles;
}

void TodoTaskModel::setSnapshot(const TodoDocument &p_document, const TodoProjection &p_projection,
                                bool p_editable, int p_revision) {
  beginResetModel();
  m_document = p_document;
  m_projection = p_projection;
  m_editable = p_editable && m_document.isValid();
  m_reorderingEnabled = m_editable && m_projection.m_view == TodoViewKind::List &&
                        m_projection.m_sort == TodoSortOrder::Manual && !hasFilters(m_projection) &&
                        m_document.findList(m_projection.m_listKey);
  m_revision = p_revision;
  ++m_snapshot;
  m_rows.clear();
  m_rowsByKey.clear();
  m_activeCount = 0;
  m_completedCount = 0;

  if (m_document.isValid()) {
    QVector<Row> completed;
    const auto &lists = std::as_const(m_document.m_lists);
    const bool filtered = hasFilters(m_projection);
    for (int listIndex = 0; listIndex < lists.size(); ++listIndex) {
      const auto &list = lists.at(listIndex);
      const auto &group = m_document.m_groups.at(list.m_groupIndex);
      const bool groupMatches =
          !group.m_isDefault && !m_projection.m_search.isEmpty() &&
          groupTitle(group).contains(m_projection.m_search, Qt::CaseInsensitive);
      for (int itemIndex = 0; itemIndex < list.m_items.size(); ++itemIndex) {
        const auto &item = list.m_items.at(itemIndex);
        if ((!filtered && !matchesView(item, list, m_projection.m_view, m_projection.m_listKey,
                                       m_projection.m_today)) ||
            !matchesFilters(item, list, groupMatches, m_projection)) {
          continue;
        }
        if (item.m_fields.m_checked) {
          ++m_completedCount;
          if (!m_projection.m_completedExpanded) {
            continue;
          }
        } else {
          ++m_activeCount;
        }
        Row row{listIndex, itemIndex,
                static_cast<int>(
                    std::count_if(item.m_steps.cbegin(), item.m_steps.cend(),
                                  [](const TodoStep &p_step) { return p_step.m_checked; }))};
        if (item.m_fields.m_checked) {
          completed.append(row);
        } else {
          m_rows.append(row);
        }
      }
    }
    if (m_projection.m_sort != TodoSortOrder::Manual) {
      const auto less = [this, &lists](const Row &p_left, const Row &p_right) {
        const auto &leftItem = lists.at(p_left.m_list).m_items.at(p_left.m_item);
        const auto &rightItem = lists.at(p_right.m_list).m_items.at(p_right.m_item);
        const auto &left = leftItem.m_fields;
        const auto &right = rightItem.m_fields;
        switch (m_projection.m_sort) {
        case TodoSortOrder::Importance:
          return left.m_important && !right.m_important;
        case TodoSortOrder::DueDate:
          if (left.m_due.isValid() != right.m_due.isValid()) {
            return left.m_due.isValid();
          }
          return left.m_due.isValid() && left.m_due < right.m_due;
        case TodoSortOrder::Title:
          return QString::compare(left.m_title, right.m_title, Qt::CaseInsensitive) < 0;
        case TodoSortOrder::AddedToMyDay:
          if (left.m_myDay.isValid() != right.m_myDay.isValid()) {
            return left.m_myDay.isValid();
          }
          return left.m_myDay.isValid() && left.m_myDay > right.m_myDay;
        case TodoSortOrder::CreationDate:
          if (leftItem.m_createdAt.isValid() != rightItem.m_createdAt.isValid()) {
            return leftItem.m_createdAt.isValid();
          }
          return leftItem.m_createdAt.isValid() && leftItem.m_createdAt > rightItem.m_createdAt;
        case TodoSortOrder::Manual:
          return false;
        }
        return false;
      };
      std::stable_sort(m_rows.begin(), m_rows.end(), less);
      std::stable_sort(completed.begin(), completed.end(), less);
    }
    if (m_completedCount > 0) {
      m_rows.append(Row{});
      m_rows.append(completed);
    }
    m_rowsByKey.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
      if (const auto *item = itemAt(row)) {
        m_rowsByKey.insert(item->m_key, row);
      }
    }
  }
  endResetModel();
}

int TodoTaskModel::rowForKey(const QString &p_key) const { return m_rowsByKey.value(p_key, -1); }

const TodoItem *TodoTaskModel::itemAt(int p_row) const {
  if (p_row < 0 || p_row >= m_rows.size()) {
    return nullptr;
  }
  const auto &row = m_rows.at(p_row);
  if (row.m_item < 0) {
    return nullptr;
  }
  return &m_document.m_lists.at(row.m_list).m_items.at(row.m_item);
}

bool TodoTaskModel::isReorderingEnabled() const { return m_reorderingEnabled; }

QStringList TodoTaskModel::mimeTypes() const { return {TaskMimeType}; }

QMimeData *TodoTaskModel::mimeData(const QModelIndexList &p_indexes) const {
  if (!isReorderingEnabled() || p_indexes.size() != 1 || !isOwnIndex(p_indexes.first())) {
    return nullptr;
  }
  const auto *item = itemAt(p_indexes.first().row());
  if (!item) {
    return nullptr;
  }
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << DragFormatVersion << m_dragIdentity << m_snapshot << m_revision << item->m_listKey
         << item->m_key;
  auto *mime = new QMimeData();
  mime->setData(TaskMimeType, bytes);
  return mime;
}

bool TodoTaskModel::decodeDrop(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                               int p_column, const QModelIndex &p_parent, QString *p_key,
                               QString *p_beforeKey) const {
  if (!isReorderingEnabled() || p_action != Qt::MoveAction || !p_data ||
      !p_data->hasFormat(TaskMimeType) || p_parent.isValid() || (p_column != -1 && p_column != 0) ||
      p_row < -1 || p_row > m_rows.size()) {
    return false;
  }

  QDataStream stream(p_data->data(TaskMimeType));
  stream.setVersion(QDataStream::Qt_6_0);
  quint32 version = 0;
  QUuid identity;
  quint64 snapshot = 0;
  int revision = -1;
  QString listKey;
  QString key;
  stream >> version >> identity >> snapshot >> revision >> listKey >> key;
  if (stream.status() != QDataStream::Ok || !stream.atEnd() || version != DragFormatVersion ||
      identity != m_dragIdentity || snapshot != m_snapshot || revision != m_revision ||
      listKey != m_projection.m_listKey) {
    return false;
  }
  const auto *item = itemAt(rowForKey(key));
  if (!item || item->m_listKey != listKey) {
    return false;
  }

  QString beforeKey;
  if (p_row >= 0) {
    // Completion groups are a projection, not source order. Inserting at the
    // active/completed boundary must not use an earlier physical completed task.
    for (int row = p_row; row < m_rows.size(); ++row) {
      const auto *before = itemAt(row);
      if (before && before->m_fields.m_checked == item->m_fields.m_checked) {
        beforeKey = before->m_key;
        break;
      }
    }
  }
  if (p_key) {
    *p_key = key;
  }
  if (p_beforeKey) {
    *p_beforeKey = beforeKey;
  }
  return true;
}

bool TodoTaskModel::canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                                    int p_column, const QModelIndex &p_parent) const {
  return decodeDrop(p_data, p_action, p_row, p_column, p_parent, nullptr, nullptr);
}

bool TodoTaskModel::dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                                 int p_column, const QModelIndex &p_parent) {
  QString key;
  QString beforeKey;
  if (!decodeDrop(p_data, p_action, p_row, p_column, p_parent, &key, &beforeKey)) {
    return false;
  }
  if (key == beforeKey) {
    return true;
  }
  const int sourceRow = rowForKey(key);
  const bool checked = itemAt(sourceRow)->m_fields.m_checked;
  QString currentNextKey;
  for (int row = sourceRow + 1; row < m_rows.size(); ++row) {
    const auto *next = itemAt(row);
    if (next && next->m_fields.m_checked == checked) {
      currentNextKey = next->m_key;
      break;
    }
  }
  if (currentNextKey == beforeKey) {
    return true;
  }
  const auto listKey = m_projection.m_listKey;
  emit moveRequested(key, listKey, beforeKey, m_revision);
  return true;
}

Qt::DropActions TodoTaskModel::supportedDragActions() const {
  return isReorderingEnabled() ? Qt::MoveAction : Qt::IgnoreAction;
}

Qt::DropActions TodoTaskModel::supportedDropActions() const { return supportedDragActions(); }
