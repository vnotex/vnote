#include "notebooknodeproxymodel.h"

#include <QRegularExpression>

#include "notebooknodemodel.h"

using namespace vnotex;

NotebookNodeProxyModel::NotebookNodeProxyModel(QObject *p_parent) : QSortFilterProxyModel(p_parent) {
  setRecursiveFilteringEnabled(true);
  setSortCaseSensitivity(Qt::CaseInsensitive);
}

NotebookNodeProxyModel::~NotebookNodeProxyModel() {}

void NotebookNodeProxyModel::setFilterFlags(FilterFlags p_flags) {
  if (m_filterFlags != p_flags) {
    m_filterFlags = p_flags;
    invalidateFilter();
  }
}

NotebookNodeProxyModel::FilterFlags NotebookNodeProxyModel::filterFlags() const {
  return m_filterFlags;
}

void NotebookNodeProxyModel::setNameFilter(const QString &p_pattern) {
  if (m_nameFilter != p_pattern) {
    m_nameFilter = p_pattern;
    invalidateFilter();
  }
}

QString NotebookNodeProxyModel::nameFilter() const { return m_nameFilter; }

void NotebookNodeProxyModel::setViewOrder(ViewOrder p_order) {
  if (m_viewOrder != p_order) {
    m_viewOrder = p_order;
    invalidate(); // Re-sort with new order
  }
}

ViewOrder NotebookNodeProxyModel::viewOrder() const { return m_viewOrder; }

NodeIdentifier NotebookNodeProxyModel::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeIdentifier();
  }

  QModelIndex sourceIndex = mapToSource(p_index);
  auto *model = qobject_cast<NotebookNodeModel *>(sourceModel());
  if (model) {
    return model->nodeIdFromIndex(sourceIndex);
  }

  return NodeIdentifier();
}

NodeInfo NotebookNodeProxyModel::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeInfo();
  }

  QModelIndex sourceIndex = mapToSource(p_index);
  auto *model = qobject_cast<NotebookNodeModel *>(sourceModel());
  if (model) {
    return model->nodeInfoFromIndex(sourceIndex);
  }

  return NodeInfo();
}

bool NotebookNodeProxyModel::filterAcceptsRow(int p_sourceRow,
                                              const QModelIndex &p_sourceParent) const {
  QModelIndex index = sourceModel()->index(p_sourceRow, 0, p_sourceParent);
  if (!index.isValid()) {
    return false;
  }

  auto *model = qobject_cast<NotebookNodeModel *>(sourceModel());
  if (!model) {
    return true;
  }

  NodeInfo nodeInfo = model->nodeInfoFromIndex(index);
  if (!nodeInfo.id.isValid()) {
    return false;
  }

  // Filter by node type
  if (nodeInfo.isFolder) {
    if (!(m_filterFlags & ShowFolders)) {
      // But still show if it has matching children (recursive filtering handles this)
      return false;
    }
  } else {
    if (!(m_filterFlags & ShowNotes)) {
      return false;
    }
  }

  // Filter by name pattern
  if (!m_nameFilter.isEmpty()) {
    QString name = nodeInfo.name;
    // Use wildcard matching
    QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(m_nameFilter),
                             QRegularExpression::CaseInsensitiveOption);
    if (!regex.match(name).hasMatch()) {
      return false;
    }
  }

  return true;
}

bool NotebookNodeProxyModel::lessThan(const QModelIndex &p_left, const QModelIndex &p_right) const {
  auto *model = qobject_cast<NotebookNodeModel *>(sourceModel());
  if (!model) {
    return QSortFilterProxyModel::lessThan(p_left, p_right);
  }

  NodeInfo leftInfo = model->nodeInfoFromIndex(p_left);
  NodeInfo rightInfo = model->nodeInfoFromIndex(p_right);

  if (!leftInfo.id.isValid() || !rightInfo.id.isValid()) {
    return QSortFilterProxyModel::lessThan(p_left, p_right);
  }

  // Folders always come before files
  if (leftInfo.isFolder != rightInfo.isFolder) {
    return leftInfo.isFolder; // Folders first
  }
  // Same type - sort based on view order
  switch (m_viewOrder) {
  case ViewOrder::OrderedByName:
    return QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) < 0;

  case ViewOrder::OrderedByNameReversed:
    return QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) > 0;

  case ViewOrder::OrderedByCreatedTime:
    return leftInfo.createdTimeUtc < rightInfo.createdTimeUtc;

  case ViewOrder::OrderedByCreatedTimeReversed:
    return leftInfo.createdTimeUtc > rightInfo.createdTimeUtc;

  case ViewOrder::OrderedByModifiedTime:
    return leftInfo.modifiedTimeUtc < rightInfo.modifiedTimeUtc;

  case ViewOrder::OrderedByModifiedTimeReversed:
    return leftInfo.modifiedTimeUtc > rightInfo.modifiedTimeUtc;

  case ViewOrder::OrderedByConfiguration:
  default:
    // Default: sort by name (case insensitive)
    return QString::compare(leftInfo.name, rightInfo.name, Qt::CaseInsensitive) < 0;
  }
}
