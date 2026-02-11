#include "notebooknodeproxymodel.h"

#include <QRegularExpression>

#include "notebooknodemodel.h"
#include <core/notebook/node.h>

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

Node *NotebookNodeProxyModel::nodeFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return nullptr;
  }

  QModelIndex sourceIndex = mapToSource(p_index);
  auto *model = qobject_cast<NotebookNodeModel *>(sourceModel());
  if (model) {
    return model->nodeFromIndex(sourceIndex);
  }

  return nullptr;
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

  Node *node = model->nodeFromIndex(index);
  if (!node) {
    return false;
  }

  // Filter by node type
  if (node->isContainer()) {
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
    QString name = node->getName();
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

  Node *leftNode = model->nodeFromIndex(p_left);
  Node *rightNode = model->nodeFromIndex(p_right);

  if (!leftNode || !rightNode) {
    return QSortFilterProxyModel::lessThan(p_left, p_right);
  }

  // Folders always come before files
  bool leftIsContainer = leftNode->isContainer();
  bool rightIsContainer = rightNode->isContainer();

  if (leftIsContainer != rightIsContainer) {
    return leftIsContainer; // Folders first
  }

  // Same type - sort by name (case insensitive)
  return QString::compare(leftNode->getName(), rightNode->getName(), Qt::CaseInsensitive) < 0;
}
